#!/usr/bin/env python3
"""Export Beowulf's (class 33, em030) motion scripts, hit caps and attack records.

From the player's own disc: GDATA slot 135 (em030.pac) and SLUS_213.61. em030 [8] is his script bank; his actions play
scripts of its groups 0, 1 and 3 (FUN_002da3d0 -> FUN_002da800(group, script), docs/ENEMY_SPECS.md I). [11] / [12]
are his caps and shapes, laid out as Cerberus's; his attack records (0x40 bytes: damage +4, kind +0x18, reaction
+0x1c) are at 0x5b7fa0 (FUN_002d8680's FUN_002942e0). The motions themselves (bwNN, PAC [2] slot NN) come from
tools/prepare_skinned_model.py. Script n of group g is entry g * 16 + n.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from gen_enemy_ai import Elf
from prepare_skinned_model import c_float, pac_list
import prepare_cerberus

SLOT = 135
GROUPS = (0, 1, 3)
PER_GROUP = 16
ATTACKS_AT, ATTACK_COUNT = 0x5B7FA0, 20
OP_GOTO = 3


def read_caps(pack):
    caps = []
    for i in range(len(pack[11]) // 4):
        kind, bone, shape = struct.unpack_from("<BBH", pack[11], i * 4)
        at = shape * 0x50
        xyz = struct.unpack_from("<3f", pack[12], at + 0x10)
        shape_kind = struct.unpack_from("<I", pack[12], at)[0]
        radius = struct.unpack_from("<f", pack[12], at + (0x30 if shape_kind == 4 else 0x20))[0]
        caps.append((kind, bone, *xyz, radius))
    return caps


def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        pack = pac_list(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
        executable = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    caps = read_caps(pack)
    prepare_cerberus.SCRIPT_COUNT = 64   # motion ids run to 42; each group has fewer than 16 scripts
    ops, starts = [], [-1] * (PER_GROUP * 4)
    for group in GROUPS:
        group_ops, group_starts = prepare_cerberus.compile_bank(pack[8], caps, group)
        base = len(ops)
        ops += [(op, a, b + base if op == OP_GOTO else b, c) for op, a, b, c in group_ops]
        for n, start in enumerate(group_starts[:PER_GROUP]):
            if start >= 0:
                starts[group * PER_GROUP + n] = start + base
    lines = ["/* Beowulf (em030): motion scripts, caps and attack records. tools/prepare_beowulf.py */",
             "#pragma once",
             "/* Include enemy_ai_runtime.h and agni_rudra_scripts.h first: the ops, caps and records are their types. */",
             f"#define BW_SCRIPTS {PER_GROUP * 4}",
             "static const EnemyAiOp bw_ops[] = {" + ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
             f"static const short bw_starts[BW_SCRIPTS] = {{" + ",".join(map(str, starts)) + "};",
             f"#define BW_CAPS {len(caps)}",
             "static const EnemyAiCap bw_caps[] = {" + ",".join(
                 "{%d,%d,%s,%s,%s,%s}" % (k, b, c_float(x), c_float(y), c_float(z), c_float(r)) for k, b, x, y, z, r in caps) + "};",
             f"#define BW_ATTACKS {ATTACK_COUNT}",
             f"static const ArAttack bw_attacks[BW_ATTACKS] = {{" + ",".join(
                 "{%s,%s,%d}" % (c_float(executable.real(ATTACKS_AT + i * 0x40 + 4)),
                                 c_float(executable.real(ATTACKS_AT + i * 0x40 + 0x1C)),
                                 executable.word(ATTACKS_AT + i * 0x40 + 0x18)) for i in range(ATTACK_COUNT)) + "};"]
    output.mkdir(parents=True, exist_ok=True)
    (output / "beowulf_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Beowulf: {len(caps)} caps, {sum(1 for s in starts if s >= 0)} scripts, {len(ops)} ops -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
