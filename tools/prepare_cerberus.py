#!/usr/bin/env python3
"""Export Mission 3's Cerberus (CEm025) meshes, motions, scripts and collision data.

The source is the player's own SLUS_213.61 disc, GDATA slot 130 (em025). The generated
runtime keeps the body, three independently animated heads, ice armour, collar, chains
and small head models separate so the PSP runtime can reproduce the fight's damage and
phase rules. See docs/CERBERUS_SPEC.md.
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
from prepare_skinned_model import (array_lines, build_mesh, c_float, decode_textures, indexed_texture,
                                   pac_list, pnst_entries, vec)

SLOT = 130
SCRIPT_COUNT = 60
OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO, OP_EVENT = range(5)
END_FRAME = 0x7FFF
PARTS = (
    ("body", 1), ("head0", 2), ("head1", 3), ("head2", 4),
    ("body_ice", 12), ("head_ice0", 13), ("head_ice1", 14), ("head_ice2", 15),
    ("collar", 5), ("chain", 6), ("small0", 16), ("small1", 17), ("small2", 18),
)


def signed(word):
    return word - 0x100000000 if word & 0x80000000 else word


def read_caps(pack):
    caps = []
    for i in range(len(pack[29]) // 4):
        kind, bone, shape = struct.unpack_from("<BBH", pack[29], i * 4)
        at = shape * 0x50
        xyz = struct.unpack_from("<3f", pack[30], at + 0x10)
        shape_kind = struct.unpack_from("<I", pack[30], at)[0]
        radius = struct.unpack_from("<f", pack[30], at + (0x30 if shape_kind == 4 else 0x20))[0]
        caps.append((kind, bone, *xyz, radius))
    return caps


def compile_bank(chunk, caps):
    """Compile one grouped S0 script bank, retaining op3/4 events and attack caps."""
    u16 = lambda at: struct.unpack_from("<H", chunk, at)[0]
    i16 = lambda at: struct.unpack_from("<h", chunk, at)[0]
    script_base, motion_base = u16(0), u16(2)
    group = script_base + u16(script_base)  # S0, group 0
    ops, starts = [], []
    for script in range(SCRIPT_COUNT):
        if script * 2 >= u16(group) or u16(group + script * 2) == 0xFFFF:
            starts.append(-1)
            continue
        p = group + u16(group + script * 2)
        starts.append(len(ops))
        seen, attack = {}, 0
        for _ in range(128):
            if p >= len(chunk):
                raise ValueError(f"Script {script} runs outside its bank")
            seen[p] = len(ops)
            op = chunk[p]
            if op == 0:
                frame, finish = i16(p + 2), chunk[p + 4] & 1
                ops.append((OP_WAIT, finish, -1 if frame == END_FRAME else frame, 0))
                p += 6
                if finish:
                    break
            elif op == 1:
                group_id, index = chunk[p + 4], chunk[p + 5]
                motion_group = motion_base + u16(motion_base + group_id * 2)
                if index * 2 >= u16(motion_group) or u16(motion_group + index * 2) == 0xFFFF:
                    raise ValueError(f"Missing motion map in script {script}")
                entry = motion_group + u16(motion_group + index * 2)
                motion = u16(entry + 6)
                if motion >= SCRIPT_COUNT:
                    raise ValueError(f"Unexpected motion {motion} in script {script}")
                start = i16(p + 6)
                loops = 1 if chunk[entry] and chunk[entry + 3] == 1 else 0
                ops.append((OP_PLAY, motion, -1 if start == END_FRAME else start,
                            chunk[p + 3] | loops << 8))
                p += 8
            elif op == 2:
                target = p + 2 - i16(p + 2)
                if target not in seen:
                    raise ValueError(f"Unknown loop target in script {script}")
                ops.append((OP_GOTO, 0, seen[target], 0))
                break
            elif op in (3, 4):
                ops.append((OP_EVENT, chunk[p + 1], chunk[p + 2], op))
                p += 6
            elif op in (5, 0x21, 0x23):
                p += 2
            elif op in (6, 7, 8):
                if op != 8:
                    attack = chunk[p + 1]
                p += 2
            elif 0x10 <= op <= 0x1F:
                count, duration = op - 0x0F, i16(p + 2)
                for k in range(count):
                    cap = chunk[p + 4 + k * 2]
                    if cap < len(caps) and caps[cap][0] == 1:
                        ops.append((OP_HIT, cap, duration, attack))
                p += 4 + count * 2
            elif op in (0x20, 0x24):
                p += 4
            elif op == 0x22:
                p += 6
            else:
                raise ValueError(f"Unknown opcode {op:#x} at {p:#x} in script {script}")
        else:
            raise ValueError(f"Script {script} did not terminate")
    return ops, starts


def write_models(pack, output):
    lines = ["/* Generated from em025 by tools/prepare_cerberus.py. Include model_runtime.h first. */",
             "#pragma once",
             "typedef struct {",
             " const Dmc3Vec3 *positions, *normals, *local, *rest; const Dmc3Uv *uvs;",
             " const unsigned char (*bones)[3], (*weights)[3]; const signed char *parents;",
             " const unsigned short *indices; const Dmc3ModelObject *objects;",
             " int vertices, indices_count, objects_count, bone_count;",
             "} CerberusMesh;"]
    table, manifest = [], []
    triple = lambda values: "{" + ",".join(map(str, values)) + "}"
    for name, chunk in PARTS:
        raw = pack[chunk]
        skeleton = parse_skeleton(raw)
        mesh = build_mesh(raw, skeleton["count"])
        prefix = "cb_" + name
        arrays = (("Dmc3Vec3", "positions", mesh["positions"], vec),
                  ("Dmc3Vec3", "normals", mesh["normals"], vec),
                  ("Dmc3Vec3", "local", skeleton["local"], vec),
                  ("Dmc3Vec3", "rest", skeleton["world"], vec),
                  ("Dmc3Uv", "uvs", mesh["uvs"], vec),
                  ("signed char", "parents", skeleton["parents"], str),
                  ("unsigned short", "indices", mesh["indices"], str))
        for ctype, field, values, formatter in arrays:
            lines += [f"static const {ctype} {prefix}_{field}[] = {{",
                      array_lines(values, formatter), "};"]
        for field in ("bones", "weights"):
            lines += [f"static const unsigned char {prefix}_{field}[][3] = {{",
                      array_lines(mesh[field], triple), "};"]
        lines += [f"static const Dmc3ModelObject {prefix}_objects[] = {{",
                  array_lines(mesh["objects"], lambda o: "{%d,%d,%d}" %
                              (o["index_start"], o["index_count"], o["texture"])), "};"]
        fields = ("positions", "normals", "local", "rest", "uvs", "bones", "weights", "parents", "indices", "objects")
        table.append("{" + ",".join(f"{prefix}_{field}" for field in fields) +
                     f",{len(mesh['positions'])},{len(mesh['indices'])},{len(mesh['objects'])},{skeleton['count']}" + "}")
        manifest.append({"name": name, "chunk": chunk, "bones": skeleton["count"],
                         "vertices": len(mesh["positions"]), "triangles": len(mesh["indices"]) // 3,
                         "objects": len(mesh["objects"]), "sha256": hashlib.sha256(raw).hexdigest()})
        print(f"Cerberus {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, "
              f"{len(mesh['objects'])} objects")
    lines += [f"#define CERBERUS_PART_COUNT {len(PARTS)}",
              "static const CerberusMesh cerberus_parts[] = {" + ",".join(table) + "};"]
    textures = decode_textures(pack[0])
    for i, texture in enumerate(textures):
        pixels, palette = indexed_texture(texture)
        lines += [f"static const unsigned char cb_texture_{i}[] = {{",
                  array_lines(pixels, str, 24), "};",
                  f"static const unsigned int cb_palette_{i}[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda value: f"0x{value:08x}u", 8), "};"]
    lines += [f"#define CERBERUS_TEXTURE_COUNT {len(textures)}",
              "static const Dmc3Texture cerberus_textures[] = {" + ",".join(
                  f"{{{t['width']},{t['height']},cb_texture_{i}}}" for i, t in enumerate(textures)) + "};",
              "static const unsigned int *const cerberus_palettes[] = {" + ",".join(
                  f"cb_palette_{i}" for i in range(len(textures))) + "};"]
    caps = read_caps(pack)
    real = c_float
    lines += ["typedef struct { unsigned char kind, bone; float x, y, z, radius; } CerberusCap;",
              "static const CerberusCap cerberus_caps[] = {" + ",".join(
                  "{%d,%d,%s}" % (k, b, ",".join(map(real, values))) for k, b, *values in caps) + "};"]
    (output / "boss_cerberus_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return caps, manifest


def write_motions(pack, output):
    lines = ["/* Raw em025 MOTs; parsed by motion.c, including direct type 0/1 channels. */", "#pragma once"]
    table, manifest = [], []
    for bank, (chunk, prefix) in enumerate(((19, "cb"), (20, "c0"), (21, "c1"), (22, "c2"))):
        for index, raw in enumerate(pac_list(pack[chunk])):
            if not raw:
                continue
            motion = parse_mot(raw)
            name = f"{prefix}-{index:02d}"
            data = raw[:motion["bytes_used"]]
            n = len(table)
            lines += [f"static const unsigned char cb_motion_{n}[] __attribute__((aligned(4))) = {{",
                      array_lines(list(data), str, 24), "};"]
            table.append(f'{{"{name}",cb_motion_{n},{len(data)}}}')
            manifest.append({"bank": bank, "name": name, "end_frame": motion["end_frame"],
                             "channels": len(motion["channels"]), "bytes": len(data),
                             "interpolation": sorted({c["kind"] for c in motion["channels"]})})
    lines += [f"#define CERBERUS_MOTION_COUNT {len(table)}",
              "static const Dmc3MotionBlob cerberus_motion_blobs[] = {" + ",".join(table) + "};"]
    (output / "boss_cerberus_motions.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return manifest


def write_scripts(pack, caps, executable, output):
    lines = ["/* em025 grouped motion scripts and original attack/projectile data. */", "#pragma once",
             "typedef struct { short op, a, b, c; } CerberusOp;",
             "typedef struct { const CerberusOp *ops; const short *starts; } CerberusScriptBank;"]
    for bank, chunk in enumerate(pack[25:29]):
        ops, starts = compile_bank(chunk, caps)
        lines += [f"static const CerberusOp cb_ops_{bank}[] = {{" +
                  ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
                  f"static const short cb_starts_{bank}[{SCRIPT_COUNT}] = {{" + ",".join(map(str, starts)) + "};"]
    lines += ["static const CerberusScriptBank cerberus_script_banks[4] = {" + ",".join(
                  f"{{cb_ops_{i},cb_starts_{i}}}" for i in range(4)) + "};",
              "typedef struct { float damage, reaction; int kind; } CerberusAttack;"]
    attacks = []
    for record in range(18):
        at = 0x5B3350 + record * 0x40
        attacks.append((executable.real(at + 4), executable.real(at + 0x1C), executable.word(at + 0x18)))
    lines += ["static const CerberusAttack cerberus_attacks[18] = {" + ",".join(
                  "{%s,%s,%d}" % (c_float(damage), c_float(reaction), kind) for damage, reaction, kind in attacks) + "};",
              "static const CerberusAttack cerberus_icicle_attack = {240.0f,120.0f,1};",
              "static const CerberusAttack cerberus_sphere_attack = {1800.0f,900.0f,2};",
              "static const CerberusAttack cerberus_shard_attack = {720.0f,400.0f,7};",
              "static const float cerberus_breath_spread[10][2] = {"]
    # Ten signed integer {x,y,z,w} vectors; the ground spread uses x and z.
    spread = [(signed(executable.word(0x5B3D60 + i * 16)), signed(executable.word(0x5B3D68 + i * 16))) for i in range(10)]
    lines[-1] += ",".join("{%.1ff,%.1ff}" % pair for pair in spread) + "};"
    lines += ["typedef struct { float delay, scatter, gravity, stay, scale; } CerberusIcicle;"]
    for phase, (address, count) in enumerate(((0x5B3E40, 8), (0x5B4098, 15))):
        values = [executable.real(address + i * 4) for i in range(count * 5)]
        lines += [f"static const CerberusIcicle cerberus_icicles_{phase}[{count}] = {{" +
                  ",".join("{" + ",".join(c_float(value) for value in values[i:i + 5]) + "}"
                           for i in range(0, len(values), 5)) + "};"]
    (output / "boss_cerberus_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    return {"attacks": attacks, "breath_spread": spread}


def generate(iso_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        data = iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
        executable = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    pack = pnst_entries(data)
    if len(pack) != 39:
        raise ValueError(f"em025 has {len(pack)} chunks, expected 39")
    caps, parts = write_models(pack, output)
    motions = write_motions(pack, output)
    extra = write_scripts(pack, caps, executable, output)
    manifest = {"slot": SLOT, "pack_sha256": hashlib.sha256(data).hexdigest(),
                "parts": parts, "motions": motions, **extra}
    (output / "boss_cerberus_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Cerberus: {len(parts)} parts, {len(motions)} motions, 4 script banks -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
