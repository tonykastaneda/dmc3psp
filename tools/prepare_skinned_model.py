#!/usr/bin/env python3
"""Generate the skinned Dante runtime header for the PSP test room.

Same arrays as prepare_model_runtime.py (positions, normals, UVs, indices,
objects, textures) plus the 24-bone skeleton and per-vertex bone indices and
weights. Triangles honour the GS ADC strip restarts (see mod_skeleton.py).
"""

import argparse
import hashlib
import math
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, write_json
from dmc3_pac import PacReader
from mod_ps2 import parse_mod
from mod_skeleton import parse_skeleton, parse_skin, strip_faces
from mot_ps2 import parse_mot
from tim2 import decode_picture

SLOT = 0
MOTION_CHUNKS = (2, 3, 4)  # nested PACs of MOT files in Dante's archive
COAT_CHUNK = 12            # pl000_02: coat-tail MOD with its own 33-bone rig
CLOTH_CHUNK = 13           # pl000_02.clt: cloth parameters for the coat rig
REBELLION_SLOT = 97        # PNST: [0] TIM2 set, [1] MOD (rigid, bone 1), [2] effects, [3] SHW, [4] MOT
REBELLION_MOTION_SLOT = 23 # PAC of 21 MOTs: Dante's Rebellion moveset (first of slots 23-27)
STUDY_MOTION_SLOTS = (33, 34, 35, 36, 37)  # candidate style movesets, exported as sNN- for the viewer
ENEMY_SLOT = 117           # PNST: Seven Hells pack ([0] TIM2 set, [1] em000 body, [26] scythe, [35] motions)
ENEMY_BODY = 1             # em000 -- the training dummy (Hell Sloth candidate; confirm with the user)
ENEMY_WEAPON = 26          # first scythe model
ENEMY_MOTIONS = 35         # PAC of 71 shared Seven Hells motions, exported as hl-NN


def pnst_entries(data: bytes):
    if data[:4] != b"PNST":
        raise ValueError("Not a PNST container")
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = list(struct.unpack_from(f"<{count}I", data, 8)) + [len(data)]
    return [data[offsets[i]:offsets[i + 1]] for i in range(count)]


def decode_textures(texture_data: bytes):
    count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from("<" + "I" * count, texture_data, 4)
    textures, offset = [], 0x800
    for sector_count in sectors:
        image = texture_data[offset:offset + sector_count * 0x800]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append({"width": picture["width"], "height": picture["height"], "rgba": rgba})
        offset += sector_count * 0x800
    return textures


def mots_in_pac(pac: bytes, prefix: str):
    count = struct.unpack_from("<I", pac, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", pac, 8)
    present = sorted(o for o in offsets if o)
    for slot, start in enumerate(offsets):
        if not start:
            continue
        end = min([o for o in present if o > start] + [len(pac)])
        raw = pac[start:end]
        parsed = parse_mot(raw)
        yield {"name": f"{prefix}{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed}


def build_mesh(model_data: bytes, bone_count: int):
    """Every object of a MOD as flat runtime arrays, one draw entry per
    sub-mesh (flag low byte = sub-mesh count; e.g. Dante's object 8 and two
    coat pieces have two)."""
    objects, _ = parse_mod(model_data, split_submeshes=True)
    mesh = {"positions": [], "normals": [], "uvs": [], "bones": [], "weights": [],
            "indices": [], "objects": [], "old_triangles": 0, "skipped": []}
    for item in objects:
        vc = item["vertex_count"]
        b, w, adc = parse_skin(model_data, item["bone_offset"], item["weight_offset"], vc, bone_count)
        base, start = len(mesh["positions"]), len(mesh["indices"])
        mesh["positions"].extend(item["positions"])
        mesh["normals"].extend(item["normals"])
        mesh["uvs"].extend(item["uvs"])
        mesh["bones"].extend(b)
        mesh["weights"].extend(tuple(round(x * 31) for x in ww) for ww in w)
        for face in strip_faces(adc):
            mesh["indices"].extend(base + v for v in face)
        mesh["old_triangles"] += len(item["faces"])
        mesh["objects"].append({"texture": item["header_count"], "index_start": start,
                                "index_count": len(mesh["indices"]) - start})
    if len(mesh["positions"]) > 65535:
        raise ValueError("Too many vertices for 16-bit indices")
    return mesh


def parse_cloth(text: str):
    """Key/value lines of a .clt cloth script; Bone lines collected in order."""
    cloth = {"bones": []}
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0].startswith((";", "$")):
            continue
        if parts[0] == "Bone":
            cloth["bones"].append(int(parts[1]))
        elif parts[0] != "End":
            values = []
            for value in parts[1:]:
                try:
                    values.append(float(value))
                except ValueError:
                    pass
            cloth[parts[0]] = values
    return cloth


def c_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("Non-finite runtime value")
    text = f"{value:.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def array_lines(values, formatter, per_line=6):
    return "\n".join(
        "    " + ", ".join(formatter(v) for v in values[i:i + per_line]) + ","
        for i in range(0, len(values), per_line)
    )


def vec(values):
    return "{" + ",".join(c_float(v) for v in values) + "}"


def write_coat(output: Path, coat_data: bytes, cloth_data: bytes) -> None:
    """Coat tails: own MOD + 33-bone rig + cloth parameters -> coat_runtime.h."""
    coat_skel = parse_skeleton(coat_data)
    coat = build_mesh(coat_data, coat_skel["count"])
    cloth = parse_cloth(cloth_data.replace(b"\0", b"").decode("ascii", "replace"))
    free = set(cloth["bones"])
    count = coat_skel["count"]
    strands = []
    for anchor in (b for b in range(count) if coat_skel["parents"][b] == 0):
        chain, current = [anchor], anchor
        while True:
            children = [b for b in range(count) if coat_skel["parents"][b] == current]
            if len(children) != 1:
                break
            current = children[0]
            chain.append(current)
        if not all(b in free for b in chain[1:]):
            raise ValueError(f"Coat strand {chain} has bones the cloth script does not drive")
        strands.append(chain)
    width = max(len(chain) for chain in strands)
    tuple3 = lambda t: "{" + ",".join(str(x) for x in t) + "}"
    lines = [
        "#ifndef DMC3_COAT_RUNTIME_H",
        "#define DMC3_COAT_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: coat tails (pl000_02). */",
        f"#define DMC3_COAT_VERTEX_COUNT {len(coat['positions'])}",
        f"#define DMC3_COAT_INDEX_COUNT {len(coat['indices'])}",
        f"#define DMC3_COAT_OBJECT_COUNT {len(coat['objects'])}",
        f"#define DMC3_COAT_BONE_COUNT {count}",
        f"#define DMC3_COAT_STRAND_COUNT {len(strands)}",
        f"#define DMC3_COAT_STRAND_MAX {width}",
        "/* Cloth script values (per-frame units at 60 fps, as authored). */",
        f"#define DMC3_COAT_GRAVITY {c_float(cloth.get('Gravity', [0, -0.01, 0])[1])}",
        f"#define DMC3_COAT_SPRING {c_float(cloth.get('SpringForce', [0.02])[0])}",
        f"#define DMC3_COAT_MAX_SPEED {c_float(cloth.get('MaxSpeed', [50.0])[0])}",
        f"#define DMC3_COAT_STIFFNESS {c_float(cloth.get('Stiffness', [0.25])[0])}",
        "static const signed char dmc3_coat_bone_parent[] = {",
        array_lines(coat_skel["parents"], str, 33), "};",
        "static const Dmc3Vec3 dmc3_coat_bone_rest[] = {",
        array_lines(coat_skel["world"], vec, 4), "};",
        "/* Bone chains from each strand's anchor (child of the root) to its tip. */",
        "static const signed char dmc3_coat_strands[][DMC3_COAT_STRAND_MAX] = {",
        array_lines(strands, lambda chain: tuple3(chain + [-1] * (width - len(chain))), 1), "};",
        "static const Dmc3Vec3 dmc3_coat_positions[] = {",
        array_lines(coat["positions"], vec, 4), "};",
        "static const Dmc3Vec3 dmc3_coat_normals[] = {",
        array_lines(coat["normals"], vec, 4), "};",
        "static const Dmc3Uv dmc3_coat_uvs[] = {",
        array_lines(coat["uvs"], vec, 6), "};",
        "static const unsigned char dmc3_coat_bones[][3] = {",
        array_lines(coat["bones"], tuple3, 8), "};",
        "static const unsigned char dmc3_coat_weights[][3] = {",
        array_lines(coat["weights"], tuple3, 8), "};",
        "static const unsigned short dmc3_coat_indices[] = {",
        array_lines(coat["indices"], str, 12), "};",
        "static const Dmc3ModelObject dmc3_coat_objects[] = {",
        array_lines(coat["objects"], lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};",
        "", "#endif", "",
    ]
    (output / "coat_runtime.h").write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {output / 'coat_runtime.h'}: {len(coat['positions'])} vertices, "
          f"{len(coat['indices']) // 3} triangles, {count} bones, {len(strands)} strands "
          f"{strands}, skipped objects {coat['skipped']}, cloth {cloth}")


def write_weapon(output: Path, name: str, mod_data: bytes, texture_data: bytes) -> None:
    """A rigid weapon MOD (every vertex on one bone at the origin) -> <name>_runtime.h."""
    skel = parse_skeleton(mod_data)
    mesh = build_mesh(mod_data, skel["count"])
    for bones, weights in zip(mesh["bones"], mesh["weights"]):
        used = {b for b, w in zip(bones, weights) if w}
        if any(skel["world"][b] != (0.0, 0.0, 0.0) for b in used):
            raise ValueError(f"{name}: vertex bound to a bone away from the origin; not rigid")
    textures = decode_textures(texture_data)
    up = name.upper()
    lines = [
        f"#ifndef DMC3_{up}_RUNTIME_H",
        f"#define DMC3_{up}_RUNTIME_H",
        f"/* Generated by tools/prepare_skinned_model.py: {name} (rigid weapon). */",
        f"#define DMC3_{up}_VERTEX_COUNT {len(mesh['positions'])}",
        f"#define DMC3_{up}_INDEX_COUNT {len(mesh['indices'])}",
        f"#define DMC3_{up}_TEXTURE_WIDTH {textures[0]['width']}",
        f"#define DMC3_{up}_TEXTURE_HEIGHT {textures[0]['height']}",
        f"static const Dmc3Vec3 dmc3_{name}_positions[] = {{",
        array_lines(mesh["positions"], vec, 4), "};",
        f"static const Dmc3Vec3 dmc3_{name}_normals[] = {{",
        array_lines(mesh["normals"], vec, 4), "};",
        f"static const Dmc3Uv dmc3_{name}_uvs[] = {{",
        array_lines(mesh["uvs"], vec, 6), "};",
        f"static const unsigned short dmc3_{name}_indices[] = {{",
        array_lines(mesh["indices"], str, 12), "};",
        f"static const unsigned char dmc3_{name}_texture[] = {{",
        array_lines(list(textures[0]["rgba"]), lambda value: f"0x{value:02x}", 16), "};",
        "", "#endif", "",
    ]
    (output / f"{name}_runtime.h").write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {output / (name + '_runtime.h')}: {len(mesh['positions'])} vertices, "
          f"{len(mesh['indices']) // 3} triangles, texture {textures[0]['width']}x{textures[0]['height']}")


def write_enemy(output: Path, body: bytes, texture_data: bytes, weapon: bytes) -> None:
    """Enemy body (skinned MOD) + its weapon (rigid MOD) + texture set -> enemy_runtime.h."""
    skel = parse_skeleton(body)
    mesh = build_mesh(body, skel["count"])
    textures = decode_textures(texture_data)
    wskel = parse_skeleton(weapon)
    wmesh = build_mesh(weapon, wskel["count"])
    tuple3 = lambda t: "{" + ",".join(str(x) for x in t) + "}"
    # The scythe: positions relative to its (single) bound bone's rest position.
    wbones = {b for bb, ww in zip(wmesh["bones"], wmesh["weights"]) for b, w in zip(bb, ww) if w}
    lines = [
        "#ifndef DMC3_ENEMY_RUNTIME_H",
        "#define DMC3_ENEMY_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: Seven Hells training dummy. */",
        f"#define ENEMY_VERTEX_COUNT {len(mesh['positions'])}",
        f"#define ENEMY_INDEX_COUNT {len(mesh['indices'])}",
        f"#define ENEMY_OBJECT_COUNT {len(mesh['objects'])}",
        f"#define ENEMY_TEXTURE_COUNT {len(textures)}",
        f"#define ENEMY_BONE_COUNT {skel['count']}",
        f"#define ENEMY_WEAPON_VERTEX_COUNT {len(wmesh['positions'])}",
        f"#define ENEMY_WEAPON_INDEX_COUNT {len(wmesh['indices'])}",
        f"#define ENEMY_WEAPON_TEXTURE {wmesh['objects'][0]['texture'] if wmesh['objects'] else 0}",
        f"/* weapon bones used: {sorted(wbones)} */",
        "static const signed char enemy_bone_parent[] = {", array_lines(skel["parents"], str, 24), "};",
        "static const Dmc3Vec3 enemy_bone_local[] = {", array_lines(skel["local"], vec, 4), "};",
        "static const Dmc3Vec3 enemy_bone_rest[] = {", array_lines(skel["world"], vec, 4), "};",
        "static const Dmc3Vec3 enemy_positions[] = {", array_lines(mesh["positions"], vec, 4), "};",
        "static const Dmc3Vec3 enemy_normals[] = {", array_lines(mesh["normals"], vec, 4), "};",
        "static const Dmc3Uv enemy_uvs[] = {", array_lines(mesh["uvs"], vec, 6), "};",
        "static const unsigned char enemy_bones[][3] = {", array_lines(mesh["bones"], tuple3, 8), "};",
        "static const unsigned char enemy_weights[][3] = {", array_lines(mesh["weights"], tuple3, 8), "};",
        "static const unsigned short enemy_indices[] = {", array_lines(mesh["indices"], str, 12), "};",
        "static const Dmc3ModelObject enemy_objects[] = {",
        array_lines(mesh["objects"], lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};",
        "static const Dmc3Vec3 enemy_weapon_positions[] = {", array_lines(wmesh["positions"], vec, 4), "};",
        "static const Dmc3Vec3 enemy_weapon_normals[] = {", array_lines(wmesh["normals"], vec, 4), "};",
        "static const Dmc3Uv enemy_weapon_uvs[] = {", array_lines(wmesh["uvs"], vec, 6), "};",
        "static const unsigned short enemy_weapon_indices[] = {", array_lines(wmesh["indices"], str, 12), "};",
    ]
    for i, t in enumerate(textures):
        lines += [f"static const unsigned char enemy_texture_{i}[] = {{",
                  array_lines(list(t["rgba"]), lambda value: f"0x{value:02x}", 16), "};"]
    lines.append("static const Dmc3Texture enemy_textures[] = {")
    lines += [f"    {{{t['width']},{t['height']},enemy_texture_{i}}}," for i, t in enumerate(textures)]
    lines += ["};", "", "#endif", ""]
    (output / "enemy_runtime.h").write_text("\n".join(lines), encoding="ascii")
    b = [min(v[k] for v in mesh["positions"]) for k in range(3)], [max(v[k] for v in mesh["positions"]) for k in range(3)]
    print(f"Generated {output / 'enemy_runtime.h'}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} "
          f"triangles, {skel['count']} bones, {len(textures)} textures, scythe {len(wmesh['positions'])} vertices "
          f"(bones {sorted(wbones)}), bounds {[round(x) for x in b[0]]}..{[round(x) for x in b[1]]}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    with Iso9660(args.iso) as iso:
        member = AfsArchive(iso, "GDATA.AFS").entries[SLOT]
        read_at = lambda offset, size: iso.read_file("GDATA.AFS", member["offset"] + offset, size)
        pac = PacReader(read_at, member["size"])
        chunks = {i: read_at(pac.entries[i]["offset"], pac.entries[i]["size"])
                  for i in (0, 1) + MOTION_CHUNKS + (COAT_CHUNK, CLOTH_CHUNK)}
        gdata = AfsArchive(iso, "GDATA.AFS")
        def read_slot(slot):
            entry = gdata.entries[slot]
            return iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
        rebellion = pnst_entries(read_slot(REBELLION_SLOT))
        rebellion_moves = read_slot(REBELLION_MOTION_SLOT)
        study_moves = {slot: read_slot(slot) for slot in STUDY_MOTION_SLOTS}
        enemy_pack = pnst_entries(read_slot(ENEMY_SLOT))
    read_chunk = chunks.__getitem__
    texture_data = chunks[0]
    model_data = chunks[1]

    skeleton = parse_skeleton(model_data)
    mesh = build_mesh(model_data, skeleton["count"])
    skipped = mesh["skipped"]

    texture_count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from("<" + "I" * texture_count, texture_data, 4)
    textures, offset = [], 0x800
    for sector_count in sectors:
        image = texture_data[offset:offset + sector_count * 0x800]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append({"width": picture["width"], "height": picture["height"], "rgba": rgba})
        offset += sector_count * 0x800

    positions, normals, uvs = mesh["positions"], mesh["normals"], mesh["uvs"]
    bones, weights, indices = mesh["bones"], mesh["weights"], mesh["indices"]
    runtime_objects, old_triangles = mesh["objects"], mesh["old_triangles"]

    s = skeleton
    lines = [
        "#ifndef DMC3_MODEL_RUNTIME_H",
        "#define DMC3_MODEL_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py from the user's ISO. */",
        "#define DMC3_MODEL_SKINNED 1",
        "typedef struct { float x, y, z; } Dmc3Vec3;",
        "typedef struct { float u, v; } Dmc3Uv;",
        "typedef struct { unsigned int index_start, index_count, texture; } Dmc3ModelObject;",
        "typedef struct { unsigned short width, height; const unsigned char *rgba; } Dmc3Texture;",
        f"#define DMC3_MODEL_VERTEX_COUNT {len(positions)}",
        f"#define DMC3_MODEL_INDEX_COUNT {len(indices)}",
        f"#define DMC3_MODEL_OBJECT_COUNT {len(runtime_objects)}",
        f"#define DMC3_MODEL_TEXTURE_COUNT {len(textures)}",
        f"#define DMC3_BONE_COUNT {s['count']}",
        "",
        "/* Parent record per bone (-1 = root); parents precede children. */",
        "static const signed char dmc3_bone_parent[] = {",
        array_lines(s["parents"], str, 24),
        "};",
        "/* Rest-pose offset from the parent (rotation is identity in the rest pose). */",
        "static const Dmc3Vec3 dmc3_bone_local[] = {",
        array_lines(s["local"], vec, 4),
        "};",
        "/* Rest-pose model-space position of each bone. */",
        "static const Dmc3Vec3 dmc3_bone_rest[] = {",
        array_lines(s["world"], vec, 4),
        "};",
        "static const Dmc3Vec3 dmc3_model_positions[] = {",
        array_lines(positions, vec, 4),
        "};",
        "static const Dmc3Vec3 dmc3_model_normals[] = {",
        array_lines(normals, vec, 4),
        "};",
        "static const Dmc3Uv dmc3_model_uvs[] = {",
        array_lines(uvs, vec, 6),
        "};",
        "/* Three bone records per vertex; weights in 31sts summing to 31. */",
        "static const unsigned char dmc3_model_bones[][3] = {",
        array_lines(bones, lambda t: "{" + ",".join(str(x) for x in t) + "}", 8),
        "};",
        "static const unsigned char dmc3_model_weights[][3] = {",
        array_lines(weights, lambda t: "{" + ",".join(str(x) for x in t) + "}", 8),
        "};",
        "static const unsigned short dmc3_model_indices[] = {",
        array_lines(indices, str, 12),
        "};",
        "static const Dmc3ModelObject dmc3_model_objects[] = {",
        array_lines(runtime_objects, lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4),
        "};",
    ]
    for i, t in enumerate(textures):
        lines += [f"static const unsigned char dmc3_model_texture_{i}[] = {{",
                  array_lines(list(t["rgba"]), lambda value: f"0x{value:02x}", 16), "};"]
    lines.append("static const Dmc3Texture dmc3_model_textures[] = {")
    lines += [f"    {{{t['width']},{t['height']},dmc3_model_texture_{i}}}," for i, t in enumerate(textures)]
    lines += ["};", "", "#endif", ""]
    header = args.output / "model_runtime.h"
    header.write_text("\n".join(lines), encoding="ascii")

    write_coat(args.output, chunks[COAT_CHUNK], chunks[CLOTH_CHUNK])
    write_weapon(args.output, "rebellion", rebellion[1], rebellion[0])
    write_enemy(args.output, enemy_pack[ENEMY_BODY], enemy_pack[0], enemy_pack[ENEMY_WEAPON])

    # Motions: every MOT in Dante's PAC chunks 2-4, embedded raw and parsed on
    # the PSP (psp/testroom/motion.c). Validated here with tools/mot_ps2.py.
    motions = []
    for chunk_index in MOTION_CHUNKS:
        motions.extend(mots_in_pac(read_chunk(chunk_index), f"{chunk_index}-"))
    # Rebellion moveset follows Dante's base motions ("rb-NN" = PAC slot NN).
    motions.extend(mots_in_pac(rebellion_moves, "rb-"))
    for slot, data in study_moves.items():
        motions.extend(mots_in_pac(data, f"s{slot}-"))
    motions.extend(mots_in_pac(enemy_pack[ENEMY_MOTIONS], "hl-"))
    mlines = [
        "#ifndef DMC3_MOTION_RUNTIME_H",
        "#define DMC3_MOTION_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: Dante's raw MOT files. */",
        "typedef struct { const char *name; const unsigned char *data; unsigned int size; } Dmc3MotionBlob;",
        f"#define DMC3_MOTION_COUNT {len(motions)}",
    ]
    for i, m in enumerate(motions):
        mlines += [f"static const unsigned char __attribute__((aligned(4))) dmc3_motion_{i}[] = {{",
                   array_lines(list(m["data"]), lambda value: f"0x{value:02x}", 16), "};"]
    mlines.append("static const Dmc3MotionBlob dmc3_motions[] = {")
    mlines += [f"    {{\"{m['name']}\", dmc3_motion_{i}, {len(m['data'])}}}," for i, m in enumerate(motions)]
    mlines += ["};", "", "#endif", ""]
    (args.output / "motion_runtime.h").write_text("\n".join(mlines), encoding="ascii")
    write_json(args.output / "motions.json", [{
        "index": i, "name": m["name"], "bytes": len(m["data"]),
        "loop_start": m["parsed"]["loop_start"], "end_frame": m["parsed"]["end_frame"],
        "channels": len(m["parsed"]["channels"]), "masks": m["parsed"]["masks"],
    } for i, m in enumerate(motions)])
    print(f"Generated {args.output / 'motion_runtime.h'}: {len(motions)} motions, "
          f"{sum(len(m['data']) for m in motions):,} bytes")

    write_json(args.output / "model-runtime-source.json", {
        "generated_utc": now(),
        "source_iso": args.iso.name,
        "gdata_slot": SLOT,
        "model_sha256": hashlib.sha256(model_data).hexdigest(),
        "vertex_count": len(positions),
        "triangle_count": len(indices) // 3,
        "triangle_count_without_strip_restarts": old_triangles,
        "skipped_deformation_objects": skipped,
        "bone_count": s["count"],
        "bone_parents": s["parents"],
        "bone_rest_world": s["world"],
        "bone_flags": s["flags"],
    })
    print(f"Generated {header}: {len(positions)} vertices, {len(indices) // 3} triangles "
          f"(was {old_triangles} without strip restarts), {s['count']} bones")


if __name__ == "__main__":
    main()
