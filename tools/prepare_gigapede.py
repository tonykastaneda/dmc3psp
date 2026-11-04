#!/usr/bin/env python3
"""The Gigapede (CEm023, Mission 4's boss) from the player's disc: gigapede_runtime.h.

em023.pac (GDATA slot 129, a PNST): [0] textures, [2] the head (MOD), [3] a body segment (MOD), [8] a PAC of
20 motions that are its movement paths: bone 1's translation is a world-space track through room 111, in
and out of the holes in its walls. See build/hdref/enemy_specs.md (section C).

The head and segments are drawn rigid, in their rest pose: their own motions ([9], [10]) aren't played yet.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_skinned_model import (array_lines, build_mesh, decode_textures, indexed_texture, pac_list, pnst_entries,
                                   psp_texture, vec)
from decimate import slim_mesh
from mod_skeleton import parse_skeleton
from mot_ps2 import hermite, parse_mot

SLOT = 129
STEP = 10   # path frames between baked points


def mesh_lines(name, mod):
    skel = parse_skeleton(mod)
    mesh = slim_mesh(build_mesh(mod, skel["count"]), 0.6)
    order, merged, indices = sorted(mesh["objects"], key=lambda o: o["texture"]), [], []
    for o in order:
        if not merged or merged[-1]["texture"] != o["texture"]:
            merged.append({**o, "index_start": len(indices), "index_count": 0})
        indices += mesh["indices"][o["index_start"]:o["index_start"] + o["index_count"]]
        merged[-1]["index_count"] += o["index_count"]
    print(f"Gigapede {name}: {len(mesh['positions'])} vertices, {len(indices) // 3} triangles")
    return [
        f"#define GIGA_{name.upper()}_VERTEX_COUNT {len(mesh['positions'])}",
        f"#define GIGA_{name.upper()}_INDEX_COUNT {len(indices)}",
        f"#define GIGA_{name.upper()}_OBJECT_COUNT {len(merged)}",
        f"static const Dmc3Vec3 giga_{name}_positions[] = {{", array_lines(mesh["positions"], vec, 4), "};",
        f"static const Dmc3Vec3 giga_{name}_normals[] = {{", array_lines(mesh["normals"], vec, 4), "};",
        f"static const Dmc3Uv giga_{name}_uvs[] = {{", array_lines(mesh["uvs"], vec, 6), "};",
        f"static const unsigned short giga_{name}_indices[] = {{", array_lines(indices, str, 12), "};",
        f"static const Dmc3ModelObject giga_{name}_objects[] = {{",
        array_lines(merged, lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};",
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    with Iso9660(args.iso) as iso:
        entry = AfsArchive(iso, "GDATA.AFS").entries[SLOT]
        pack = pnst_entries(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
    lines = ["/* Generated from the disc by tools/prepare_gigapede.py */", "#pragma once"]
    textures = decode_textures(pack[0])
    for i, tx in enumerate(textures):
        pixels, palette = indexed_texture(tx)
        lines += [f"static const unsigned char giga_texture_{i}[] = {{", array_lines(pixels, lambda v: f"0x{v:02x}", 16), "};",
                  f"static const unsigned int giga_palette_{i}[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda v: f"0x{v:08x}", 8), "};"]
    lines += [f"#define GIGA_TEXTURE_COUNT {len(textures)}",
              "static const Dmc3Texture giga_textures[] = {" +
              ",".join(f"{{{tx['width']},{tx['height']},giga_texture_{i}}}" for i, tx in enumerate(textures)) + "};",
              "static const unsigned int *const giga_palettes[] = {" + ",".join(f"giga_palette_{i}" for i in range(len(textures))) + "};"]
    lines += mesh_lines("head", pack[2]) + mesh_lines("body", pack[3])
    # The paths: bone 1's world translation every STEP frames.
    starts, points = [], []
    for k, raw in enumerate(pac_list(pack[8])):
        starts.append(len(points))
        if not raw:
            continue
        m = parse_mot(raw)
        ch = {(c["bone"], c["channel"]): c["keys"] for c in m["channels"]}
        if (1, 6) not in ch:
            continue
        frames = int(m["end_frame"])
        track = [tuple(hermite(ch[(1, c)], f) if (1, c) in ch else 0.0 for c in (6, 7, 8)) for f in range(0, frames + 1, STEP)]
        if max(abs(v) for p in track for v in p) < 1.0:
            continue   # paths 18 and 19 are empty
        points += track
    starts.append(len(points))
    lines += [f"#define GIGA_PATH_COUNT {len(starts) - 1}", f"#define GIGA_PATH_STEP {STEP}",
              "static const unsigned short giga_path_start[] = {" + ",".join(map(str, starts)) + "};",
              "static const float giga_path_points[][3] = {",
              array_lines(points, lambda p: "{%.1ff,%.1ff,%.1ff}" % p, 4), "};"]
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Generated {args.output}: {len(textures)} textures, {len(starts) - 1} paths, {len(points)} points")


if __name__ == "__main__":
    main()
