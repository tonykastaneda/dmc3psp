#!/usr/bin/env python3
"""Export the Soul Eater's (class 16, CEm016, em016) scripts, caps, attack records and action table.

From the player's own disc: GDATA slot 126 (em016.pac, a PNST) and SLUS_213.61. [6] is its script bank, one script per
action (FUN_0049cdf0 plays the action's own number; FUN_0049ad90 hands [6] to FUN_00254d60); [4] / [5] its caps and
shapes; its attack records are at 0x5c13c0 (FUN_0049ad90 +0x1490: 500, 3000). Its action table (FUN_0049ce50,
FUN_0049cf80, FUN_0049dea0) is 0x2c bytes an action from 0x5c14f0: a time by rank (4 floats), a random addition by rank
(4), a turn rate in degrees a frame by rank (4 bytes), the decision list's pointer. Its rank comes from the game's
difficulty value against 0x5c1340 (330, 530, 9999). The motions (seNN, PAC [8] slot NN) come from
tools/prepare_skinned_model.py. See docs/ENEMY_SPECS.md H.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from gen_enemy_ai import Elf
from prepare_skinned_model import c_float, pnst_entries

SLOT = 126
ACTIONS = 19
ATTACKS_AT, ATTACK_COUNT = 0x5C13C0, 2
TABLE_AT = 0x5C14F0
OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO, OP_EVENT = range(5)
END_FRAME = 0x7FFF


def read_caps(pack):
    caps = []
    for i in range(len(pack[4]) // 4):
        kind, bone, shape = struct.unpack_from("<BBH", pack[4], i * 4)
        at = shape * 0x50
        xyz = struct.unpack_from("<3f", pack[5], at + 0x10)
        shape_kind = struct.unpack_from("<I", pack[5], at)[0]
        radius = struct.unpack_from("<f", pack[5], at + (0x30 if shape_kind == 4 else 0x20))[0]
        caps.append((kind, bone, *xyz, radius))
    return caps


def compile_scripts(chunk, caps):
    """The bank's group 0, the shared script format (tools/prepare_cerberus.py's compile_bank), but a wait to the
    motion's end ends a script whether or not it says finish: these scripts sit on a looping motion there."""
    u16 = lambda at: struct.unpack_from("<H", chunk, at)[0]
    i16 = lambda at: struct.unpack_from("<h", chunk, at)[0]
    script_base, motion_base = u16(0), u16(2)
    group = script_base + u16(script_base)
    motions = motion_base + u16(motion_base)
    ops, starts = [], []
    for script in range(ACTIONS):
        if script * 2 >= u16(group) or u16(group + script * 2) == 0xFFFF:
            starts.append(-1)
            continue
        p = group + u16(group + script * 2)
        starts.append(len(ops))
        attack = 0
        while True:
            op = chunk[p]
            if op == 0:
                frame, finish = i16(p + 2), chunk[p + 4] & 1
                ops.append((OP_WAIT, finish, -1 if frame == END_FRAME else frame, 0))
                p += 6
                if finish or frame == END_FRAME:
                    break
            elif op == 1:
                index = chunk[p + 5]
                entry = motions + u16(motions + index * 2)
                motion = u16(entry + 6)
                start = i16(p + 6)
                loops = 1 if chunk[entry] and chunk[entry + 3] == 1 else 0
                ops.append((OP_PLAY, motion, -1 if start == END_FRAME else start, chunk[p + 3] | loops << 8))
                p += 8
            elif op in (3, 4):
                ops.append((OP_EVENT, chunk[p + 1], chunk[p + 2], op))
                p += 6
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
            elif op == 0x22:
                p += 6
            elif op in (5, 0x21, 0x23):
                p += 2
            elif op in (0x20, 0x24):
                p += 4
            else:
                raise ValueError(f"Unknown opcode {op:#x} at {p:#x} in script {script}")
    return ops, starts


def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        pack = pnst_entries(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
        exe = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    caps = read_caps(pack)
    ops, starts = compile_scripts(pack[6], caps)
    rows = []
    for a in range(ACTIONS):
        base = TABLE_AT + a * 0x2C
        time = [exe.real(base + 4 * r) for r in range(3)]
        rand = [exe.real(base + 0x10 + 4 * r) for r in range(3)]
        turn = [exe.word(base + 0x20) >> (8 * r) & 0xFF for r in range(3)]
        rows.append((time, rand, turn))
    lines = ["/* The Soul Eater (em016): scripts, caps, attack records, action table. tools/prepare_soul_eater.py */",
             "#pragma once",
             "/* Include enemy_ai_runtime.h and agni_rudra_scripts.h first: the ops, caps and records are their types. */",
             f"#define SE_SCRIPTS {ACTIONS}",
             "static const EnemyAiOp se_ops[] = {" + ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
             "static const short se_starts[SE_SCRIPTS] = {" + ",".join(map(str, starts)) + "};",
             f"#define SE_CAPS {len(caps)}",
             "static const EnemyAiCap se_caps[] = {" + ",".join(
                 "{%d,%d,%s,%s,%s,%s}" % (k, b, c_float(x), c_float(y), c_float(z), c_float(r)) for k, b, x, y, z, r in caps) + "};",
             f"#define SE_ATTACKS {ATTACK_COUNT}",
             "static const ArAttack se_attacks[SE_ATTACKS] = {" + ",".join(
                 "{%s,%s,%d}" % (c_float(exe.real(ATTACKS_AT + i * 0x40 + 4)), c_float(exe.real(ATTACKS_AT + i * 0x40 + 0x1C)),
                                 exe.word(ATTACKS_AT + i * 0x40 + 0x18)) for i in range(ATTACK_COUNT)) + "};",
             "/* Per action, rank 0-2: frames (FUN_0049ce50: time + rand * random), turn in degrees a frame (FUN_0049cf80). */",
             "typedef struct { float time[3], rand[3]; unsigned char turn[3]; } SeAction;",
             "static const SeAction se_actions[SE_SCRIPTS] = {" + ",".join(
                 "{{%s},{%s},{%s}}" % (",".join(map(c_float, t)), ",".join(map(c_float, r)), ",".join(map(str, u)))
                 for t, r, u in rows) + "};"]
    output.mkdir(parents=True, exist_ok=True)
    (output / "soul_eater_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Soul Eater: {len(caps)} caps {caps}, {sum(1 for s in starts if s >= 0)} scripts, {len(ops)} ops -> {output}")
    for a, s in enumerate(starts):
        if s >= 0:
            k, seq = s, []
            while True:
                seq.append(ops[k])
                if ops[k][0] == OP_WAIT:
                    break
                k += 1
            print(f"  script {a}: {seq}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
