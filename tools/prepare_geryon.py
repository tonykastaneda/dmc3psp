#!/usr/bin/env python3
"""Export Mission 12's Geryon (CEm029, em029.pac) meshes and motions.

The source is the player's own disc, GDATA slot 134 (em029, a PNST; EST 50 -> class 32 -> pack 20; the factory's
FUN_00381710 builds CEm029, vtable 0x608560, and its cart CEm029Cart). [0] textures; [9] the horse (33 bones) with
its 21 motions in [11]; [13] the carriage (19 bones) with its 21 motions in [15]; [3], [5], [7] the shells (a missile,
the red time sphere, a small one). Action n plays motion n of each (inferred: their scripts [12] and [16] have one
script per action). See docs/GERYON_SPEC.md.
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

SLOT = 134
PARTS = (("horse", 9), ("cart", 13), ("missile", 3), ("sphere", 5), ("small", 7))
MOTIONS = ((11, "gh"), (15, "gc"))


def write_models(pack, output):
    lines = ["/* Generated from em029 by tools/prepare_geryon.py. Include model_runtime.h first. */",
             "#pragma once",
             "typedef struct {",
             " const Dmc3Vec3 *positions, *normals, *local, *rest; const Dmc3Uv *uvs;",
             " const unsigned char (*bones)[3], (*weights)[3]; const signed char *parents;",
             " const unsigned short *indices; const Dmc3ModelObject *objects;",
             " int vertices, indices_count, objects_count, bone_count;",
             "} GeryonMesh;"]
    table, manifest = [], []
    triple = lambda values: "{" + ",".join(map(str, values)) + "}"
    for name, chunk in PARTS:
        raw = pack[chunk]
        skeleton = parse_skeleton(raw)
        mesh = build_mesh(raw, skeleton["count"])
        if name == "cart":
            mesh = slim_mesh(mesh, 0.6)   # 3,737 triangles is more than the PSP wants for one prop
        prefix = "gy_" + name
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
        print(f"Geryon {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, "
              f"{skeleton['count']} bones")
    lines += [f"#define GERYON_PART_COUNT {len(PARTS)}",
              "static const GeryonMesh geryon_parts[] = {" + ",".join(table) + "};"]
    textures = decode_textures(pack[0])
    for i, texture in enumerate(textures):
        pixels, palette = indexed_texture(texture)
        lines += [f"static const unsigned char gy_texture_{i}[] = {{", array_lines(pixels, str, 24), "};",
                  f"static const unsigned int gy_palette_{i}[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda value: f"0x{value:08x}u", 8), "};"]
    lines += [f"#define GERYON_TEXTURE_COUNT {len(textures)}",
              "static const Dmc3Texture geryon_textures[] = {" + ",".join(
                  f"{{{t['width']},{t['height']},gy_texture_{i}}}" for i, t in enumerate(textures)) + "};",
              "static const unsigned int *const geryon_palettes[] = {" + ",".join(
                  f"gy_palette_{i}" for i in range(len(textures))) + "};"]
    (output / "boss_geryon_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def write_motions(pack, output):
    lines = ["/* Raw em029 MOTs: the horse's ghNN ([11]) and the carriage's gcNN ([15]); parsed by motion.c. */",
             "#pragma once"]
    table, manifest = [], []
    for chunk, prefix in MOTIONS:
        for index, raw in enumerate(pac_list(pack[chunk])):
            if not raw:
                continue
            motion = parse_mot(raw)
            data = raw[:motion["bytes_used"]]
            n = len(table)
            lines += [f"static const unsigned char gy_motion_{n}[] __attribute__((aligned(4))) = {{",
                      array_lines(list(data), str, 24), "};"]
            table.append(f'{{"{prefix}{index:02d}",gy_motion_{n},{len(data)}}}')
            manifest.append({"name": f"{prefix}{index:02d}", "end_frame": motion["end_frame"], "bytes": len(data)})
    lines += [f"#define GERYON_MOTION_COUNT {len(table)}",
              "static const Dmc3MotionBlob geryon_motion_blobs[] = {" + ",".join(table) + "};"]
    (output / "boss_geryon_motions.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def generate(iso_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        data = iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
    pack = pnst_entries(data)
    parts = write_models(pack, output)
    motions = write_motions(pack, output)
    (output / "boss_geryon_manifest.json").write_text(json.dumps(
        {"slot": SLOT, "pack_sha256": hashlib.sha256(data).hexdigest(), "parts": parts, "motions": motions},
        indent=2) + "\n", encoding="utf-8")
    print(f"Geryon: {len(parts)} parts, {len(motions)} motions -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
