#!/usr/bin/env python3
"""Export Mission 19's Arkham (CEm035? no: class 35, em pack 23) meshes and motions.

The source is the player's own disc, GDATA slot 137 (a PAC; EST 53 -> class 35 -> pack 23, and the Legions, EST 54 ->
class 36 -> pack 24, share it). [0] textures; [1] Arkham's body (39 bones), its motions in [3] ("akNN", PAC slot NN);
[2] his outer shell (12 bones), its motions in [4] ("asNN"); [6] the motion scripts (state n plays script n);
[11] the Legions' textures and [12] a Legion (8 bones; cloth em033_00.clt in [13]). See docs/ARKHAM_SPEC.md.
"""
import argparse
import hashlib
import json
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from mod_skeleton import parse_skeleton
from mot_ps2 import parse_mot
from prepare_skinned_model import (array_lines, build_mesh, decode_textures, indexed_texture, pac_list,
                                   pnst_entries, vec)
from decimate import slim_mesh

SLOT = 137
PARTS = (("body", 1, 0), ("shell", 2, 0), ("legion", 12, 11))
MOTIONS = ((3, "ak"), (4, "as"))


def write_models(pack, output):
    lines = ["/* Generated from em036 by tools/prepare_arkham.py. Include model_runtime.h first. */",
             "#pragma once",
             "typedef struct {",
             " const Dmc3Vec3 *positions, *normals, *local, *rest; const Dmc3Uv *uvs;",
             " const unsigned char (*bones)[3], (*weights)[3]; const signed char *parents;",
             " const unsigned short *indices; const Dmc3ModelObject *objects;",
             " int vertices, indices_count, objects_count, bone_count;",
             "} ArkhamMesh;"]
    table, manifest = [], []
    triple = lambda values: "{" + ",".join(map(str, values)) + "}"
    texture_offset = len(decode_textures(pack[0]))
    for name, chunk, texture_chunk in PARTS:
        raw = pack[chunk]
        skeleton = parse_skeleton(raw)
        mesh = build_mesh(raw, skeleton["count"])
        if name == "body":
            mesh = slim_mesh(mesh, 0.7)
        if texture_chunk:   # the Legion's own texture set follows the main one
            for o in mesh["objects"]:
                o["texture"] += texture_offset
        prefix = "ak_" + name
        arrays = (("Dmc3Vec3", "positions", mesh["positions"], vec),
                  ("Dmc3Vec3", "normals", mesh["normals"], vec),
                  ("Dmc3Vec3", "local", skeleton["local"], vec),
                  ("Dmc3Vec3", "rest", skeleton["world"], vec),
                  ("Dmc3Uv", "uvs", mesh["uvs"], vec),
                  ("signed char", "parents", skeleton["parents"], str),
                  ("unsigned short", "indices", mesh["indices"], str))
        for ctype, field, values, formatter in arrays:
            lines += [f"static const {ctype} {prefix}_{field}[] = {{", array_lines(values, formatter), "};"]
        for field in ("bones", "weights"):
            lines += [f"static const unsigned char {prefix}_{field}[][3] = {{", array_lines(mesh[field], triple), "};"]
        lines += [f"static const Dmc3ModelObject {prefix}_objects[] = {{",
                  array_lines(mesh["objects"], lambda o: "{%d,%d,%d}" %
                              (o["index_start"], o["index_count"], o["texture"])), "};"]
        fields = ("positions", "normals", "local", "rest", "uvs", "bones", "weights", "parents", "indices", "objects")
        table.append("{" + ",".join(f"{prefix}_{field}" for field in fields) +
                     f",{len(mesh['positions'])},{len(mesh['indices'])},{len(mesh['objects'])},{skeleton['count']}" + "}")
        manifest.append({"name": name, "chunk": chunk, "bones": skeleton["count"], "vertices": len(mesh["positions"]),
                         "triangles": len(mesh["indices"]) // 3, "sha256": hashlib.sha256(raw).hexdigest()})
        print(f"Arkham {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, "
              f"{skeleton['count']} bones")
    lines += [f"#define ARKHAM_PART_COUNT {len(PARTS)}",
              "static const ArkhamMesh arkham_parts[] = {" + ",".join(table) + "};"]
    textures = decode_textures(pack[0]) + decode_textures(pack[11])
    for i, texture in enumerate(textures):
        pixels, palette = indexed_texture(texture)
        lines += [f"static const unsigned char ak_texture_{i}[] = {{", array_lines(pixels, str, 24), "};",
                  f"static const unsigned int ak_palette_{i}[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda value: f"0x{value:08x}u", 8), "};"]
    lines += [f"#define ARKHAM_TEXTURE_COUNT {len(textures)}",
              "static const Dmc3Texture arkham_textures[] = {" + ",".join(
                  f"{{{t['width']},{t['height']},ak_texture_{i}}}" for i, t in enumerate(textures)) + "};",
              "static const unsigned int *const arkham_palettes[] = {" + ",".join(
                  f"ak_palette_{i}" for i in range(len(textures))) + "};"]
    (output / "boss_arkham_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def write_motions(pack, output):
    lines = ["/* Raw em036 MOTs: akNN ([3]) and asNN ([4]); parsed by motion.c. */",
             "#pragma once"]
    table, manifest = [], []
    for chunk, prefix in MOTIONS:
        for index, raw in enumerate(pac_list(pack[chunk])):
            if not raw:
                continue
            try:
                motion = parse_mot(raw)
            except ValueError:   # slot 50 of each is not a MOT
                continue
            data = raw[:motion["bytes_used"]]
            n = len(table)
            lines += [f"static const unsigned char ak_motion_{n}[] __attribute__((aligned(4))) = {{",
                      array_lines(list(data), str, 24), "};"]
            table.append(f'{{"{prefix}{index:02d}",ak_motion_{n},{len(data)}}}')
            manifest.append({"name": f"{prefix}{index:02d}", "end_frame": motion["end_frame"], "bytes": len(data)})
    lines += [f"#define ARKHAM_MOTION_COUNT {len(table)}",
              "static const Dmc3MotionBlob arkham_motion_blobs[] = {" + ",".join(table) + "};"]
    (output / "boss_arkham_motions.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def generate(iso_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        data = iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
    pack = pac_list(data)
    parts = write_models(pack, output)
    motions = write_motions(pack, output)
    (output / "boss_arkham_manifest.json").write_text(json.dumps(
        {"slot": SLOT, "pack_sha256": hashlib.sha256(data).hexdigest(), "parts": parts, "motions": motions},
        indent=2) + "\n", encoding="utf-8")
    print(f"Arkham: {len(parts)} parts, {len(motions)} motions -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
