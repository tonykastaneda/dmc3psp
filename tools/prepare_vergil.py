#!/usr/bin/env python3
"""Export Mission 7's Vergil (CEm035, em035.pac) meshes, motions, scripts and collision data.

The source is the player's own SLUS_213.61 disc, GDATA slot 139 (em035, a PAC; EST 58 -> class 40 -> pack 26).
Mission 7 is the class's variant 0 (room 144's EST op 8 [0, 0]): body [1], coat [3], Yamato [25] and its
scabbard [28]; motions [7] [8] [9] [10] are motion files 0-3; [11] is the body's motion script file; [12] and
[13] are its collision caps and shapes. See docs/VERGIL_SPEC.md.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from gen_enemy_ai import Elf
from mod_skeleton import parse_skeleton
from mot_ps2 import parse_mot
from prepare_skinned_model import array_lines, build_mesh, c_float, decode_textures, indexed_texture, pac_list, vec
from prepare_stage import pac_entries

SLOT = 139
OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO, OP_EVENT, OP_ATTACK = range(6)
END_FRAME = 0x7FFF
PARTS = (("body", 1), ("coat", 3), ("yamato", 25), ("scabbard", 28), ("beowulf", 55), ("dt_body", 51), ("force_edge", 32))
MOTION_CHUNKS = (7, 8, 9, 10)          # motion files 0-3 (the set-up's list at +0xb570)
SCRIPT_CHUNK, CAP_CHUNK, SHAPE_CHUNK = 11, 12, 13
ATTACKS = 0x5DC4D0                      # attack records, 0x40 each (FUN_002942e0's table)
ATTACK_COUNT = 21


def read_caps(pack):
    """(kind, bone, x, y, z, radius) per cap: kind 1 attack, 4 hurt, 2 push, 6 other (as em025)."""
    caps = []
    for i in range(len(pack[CAP_CHUNK]) // 4):
        kind, bone, shape = struct.unpack_from("<BBH", pack[CAP_CHUNK], i * 4)
        at = shape * 0x50
        xyz = struct.unpack_from("<3f", pack[SHAPE_CHUNK], at + 0x10)
        shape_kind = struct.unpack_from("<I", pack[SHAPE_CHUNK], at)[0]
        radius = struct.unpack_from("<f", pack[SHAPE_CHUNK], at + (0x30 if shape_kind == 4 else 0x20))[0]
        caps.append((kind, bone, *xyz, radius))
    return caps


def direct_table(chunk, part):
    """The script file's banks: u16 offsets from the part itself, to 0xffff (FUN_00255830's mode 1)."""
    out = []
    while struct.unpack_from("<H", chunk, part + 2 * len(out))[0] != 0xFFFF:
        out.append(part + struct.unpack_from("<H", chunk, part + 2 * len(out))[0])
    return out


def compile_scripts(chunk, caps):
    """Every script of banks 0-3 as flat ops. Banks are the action ranges of FUN_004e0fc0: actions 0-1 are
    bank 0, 2-5 and 0x11-0x13 bank 1 (action - 2), 0x14 bank 2, 0x15 and up bank 3 (action - 0x15)."""
    u16 = lambda at: struct.unpack_from("<H", chunk, at)[0]
    i16 = lambda at: struct.unpack_from("<h", chunk, at)[0]
    banks = direct_table(chunk, u16(0))
    motion_groups = direct_table(chunk, u16(2))
    ends = sorted(banks + [u16(2)])
    ops, starts = [], []
    for bank, table in enumerate(banks):
        count = min(u16(table) // 2, (min(x for x in ends if x > table) - table) // 2)
        row = []
        for script in range(count):
            rel = u16(table + script * 2)
            if rel == 0xFFFF:
                row.append(-1)
                continue
            p = table + rel
            row.append(len(ops))
            seen, attack = {}, 0
            for _ in range(160):
                seen[p] = len(ops)
                op = chunk[p]
                if op == 0:
                    frame, finish = i16(p + 2), chunk[p + 4] & 1
                    ops.append((OP_WAIT, finish, -1 if frame == END_FRAME else frame, 0))
                    p += 6
                    if finish:
                        break
                elif op == 1:
                    group, index = chunk[p + 4], chunk[p + 5]
                    entry = motion_groups[group] + u16(motion_groups[group] + index * 2)
                    motion = u16(entry + 6)            # layer 0's motion id = file * 100 + index
                    loops = 1 if chunk[entry] and chunk[entry + 3] == 1 else 0
                    start = i16(p + 6)
                    ops.append((OP_PLAY, motion, -1 if start == END_FRAME else start, chunk[p + 3] | loops << 8))
                    p += 8
                elif op == 2:
                    target = p + 2 - i16(p + 2)
                    ops.append((OP_GOTO, 0, seen.get(target, row[-1]), 0))
                    break
                elif op in (3, 4):
                    # Window 0 (op 3) byte 0: Yamato's place (1 drawn, 2 sheathed); window 1 (op 4) byte 0: the
                    # chain point FUN_00255af0(slot, 1, 0) the attack states wait for.
                    ops.append((OP_EVENT, op - 3, chunk[p + 1], chunk[p + 2] | chunk[p + 3] << 8))
                    p += 6
                elif op in (5, 0x21, 0x23):
                    p += 2
                elif op in (6, 7, 8):
                    if op != 8:
                        attack = chunk[p + 1]
                        ops.append((OP_ATTACK, attack, 0, 0))
                    p += 2
                elif 0x10 <= op <= 0x1F:
                    count_caps, duration = op - 0x0F, i16(p + 2)
                    for k in range(count_caps):
                        cap = struct.unpack_from("<H", chunk, p + 4 + k * 2)[0]
                        if cap < len(caps) and caps[cap][0] == 1:
                            ops.append((OP_HIT, cap, duration, attack))
                    p += 4 + count_caps * 2
                elif op in (0x20, 0x24):
                    p += 4
                elif op == 0x22:
                    p += 6
                else:
                    raise ValueError(f"Unknown opcode {op:#x} at {p:#x} (bank {bank} script {script})")
            else:
                raise ValueError(f"Script {bank}:{script} did not terminate")
        starts.append(row)
    return ops, starts


def write_models(pack, output):
    lines = ["/* Generated from em035 by tools/prepare_vergil.py. Include model_runtime.h first. */",
             "#pragma once",
             "typedef struct {",
             " const Dmc3Vec3 *positions, *normals, *local, *rest; const Dmc3Uv *uvs;",
             " const unsigned char (*bones)[3], (*weights)[3]; const signed char *parents;",
             " const unsigned short *indices; const Dmc3ModelObject *objects;",
             " int vertices, indices_count, objects_count, bone_count;",
             "} VergilMesh;"]
    table, manifest = [], []
    texture_banks = {0: decode_textures(pack[0]), 54: decode_textures(pack[54]), 50: decode_textures(pack[50]), 31: decode_textures(pack[31])}
    offsets={};textures=[]
    for bank,values in texture_banks.items():offsets[bank]=len(textures);textures.extend(values)
    triple = lambda values: "{" + ",".join(map(str, values)) + "}"
    for name, chunk in PARTS:
        raw = pack[chunk]
        skeleton = parse_skeleton(raw)
        mesh = build_mesh(raw, skeleton["count"])
        texture_offset=offsets[54 if chunk==55 else 50 if chunk==51 else 31 if chunk==32 else 0]
        for obj in mesh["objects"]:obj["texture"]+=texture_offset
        prefix = "vv_" + name
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
        manifest.append({"name": name, "chunk": chunk, "bones": skeleton["count"],
                         "vertices": len(mesh["positions"]), "triangles": len(mesh["indices"]) // 3,
                         "objects": len(mesh["objects"]), "sha256": hashlib.sha256(raw).hexdigest()})
        print(f"Vergil {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, "
              f"{len(mesh['objects'])} objects, {skeleton['count']} bones")
    lines += [f"#define VERGIL_PART_COUNT {len(PARTS)}",
              "static const VergilMesh vergil_parts[] = {" + ",".join(table) + "};"]
    for i, texture in enumerate(textures):
        pixels, palette = indexed_texture(texture)
        lines += [f"static const unsigned char vv_texture_{i}[] = {{", array_lines(pixels, str, 24), "};",
                  f"static const unsigned int vv_palette_{i}[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda value: f"0x{value:08x}u", 8), "};"]
    lines += [f"#define VERGIL_TEXTURE_COUNT {len(textures)}",
              "static const Dmc3Texture vergil_textures[] = {" + ",".join(
                  f"{{{t['width']},{t['height']},vv_texture_{i}}}" for i, t in enumerate(textures)) + "};",
              "static const unsigned int *const vergil_palettes[] = {" + ",".join(
                  f"vv_palette_{i}" for i in range(len(textures))) + "};"]
    body=parse_skeleton(pack[1])["world"];beowulf=parse_skeleton(pack[55])["world"]
    mapping=[min(range(len(body)),key=lambda i:sum((p[k]-body[i][k])**2 for k in range(3))) for p in beowulf]
    lines += ['static const unsigned char vergil_beowulf_bodymap[] = {'+','.join(map(str,mapping))+'};']
    caps = read_caps(pack)
    lines += ["typedef struct { unsigned char kind, bone; float x, y, z, radius; } VergilCap;",
              f"#define VERGIL_CAP_COUNT {len(caps)}",
              "static const VergilCap vergil_caps[] = {" + ",".join(
                  "{%d,%d,%s}" % (k, b, ",".join(map(c_float, values))) for k, b, *values in caps) + "};"]
    (output / "boss_vergil_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return caps, manifest


def write_motions(pack, output):
    lines = ["/* Raw em035 MOTs (files 0-3 = chunks 7-10), named vrF-II; parsed by motion.c. */", "#pragma once"]
    table, manifest = [], []
    for file, chunk in enumerate(MOTION_CHUNKS):
        for index, raw in enumerate(pac_list(pack[chunk])):
            if not raw:
                continue
            motion = parse_mot(raw)
            name = f"vr{file}-{index:02d}"
            data = raw[:motion["bytes_used"]]
            n = len(table)
            lines += [f"static const unsigned char vr_motion_{n}[] __attribute__((aligned(4))) = {{",
                      array_lines(list(data), str, 24), "};"]
            table.append(f'{{"{name}",vr_motion_{n},{len(data)}}}')
            manifest.append({"file": file, "name": name, "end_frame": motion["end_frame"],
                             "channels": len(motion["channels"]), "bytes": len(data)})
    lines += [f"#define VERGIL_MOTION_COUNT {len(table)}",
              "static const Dmc3MotionBlob vergil_motion_blobs[] = {" + ",".join(table) + "};"]
    (output / "boss_vergil_motions.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def write_scripts(pack, caps, executable, output):
    ops, starts = compile_scripts(pack[SCRIPT_CHUNK], caps)
    width = max(len(row) for row in starts)
    lines = ["/* em035's body motion scripts (chunk 11) and attack records, by tools/prepare_vergil.py. */",
             "#pragma once",
             "typedef struct { short op, a, b, c; } VergilOp;",
             f"#define VERGIL_BANK_WIDTH {width}",
             "static const VergilOp vergil_ops[] = {" + ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
             f"static const short vergil_script_starts[4][{width}] = {{" + ",".join(
                 "{" + ",".join(map(str, row + [-1] * (width - len(row)))) + "}" for row in starts[:4]) + "};",
             "typedef struct { float damage, reaction; int kind; } VergilAttack;", f"#define VERGIL_ATTACK_COUNT {ATTACK_COUNT}"]
    attacks = []
    for record in range(ATTACK_COUNT):
        at = ATTACKS + record * 0x40
        attacks.append((executable.real(at + 4), executable.real(at + 0x1C), executable.word(at + 0x18)))
    lines.append(f"static const VergilAttack vergil_attacks[{ATTACK_COUNT}] = {{" + ",".join(
        "{%s,%s,%d}" % (c_float(damage), c_float(reaction), kind) for damage, reaction, kind in attacks) + "};")
    (output / "boss_vergil_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return {"attacks": attacks, "ops": len(ops), "scripts": [len(row) for row in starts]}


def generate(iso_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        data = iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
        executable = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    pack = pac_entries(data)
    if len(pack) != 64:
        raise ValueError(f"em035 has {len(pack)} chunks, expected 64")
    caps, parts = write_models(pack, output)
    motions = write_motions(pack, output)
    extra = write_scripts(pack, caps, executable, output)
    manifest = {"slot": SLOT, "pack_sha256": hashlib.sha256(data).hexdigest(),
                "parts": parts, "motions": motions, **extra}
    (output / "boss_vergil_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Vergil: {len(parts)} parts, {len(motions)} motions, {extra['ops']} script ops -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
