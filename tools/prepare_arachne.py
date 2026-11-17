#!/usr/bin/env python3
"""Export the Arachne's (class 10, em010) motion scripts, hit caps and attack records.

From the player's own disc: GDATA slot 121 (em010.pac) and SLUS_213.61. em010 [16] is its script bank: its motion
table (0x5bd0d0, 20 bytes a row: group, script, ...) plays script n of group g (0-3) through FUN_003f6460 ->
FUN_00254df0; a script's motion id g*100+n is PAC [7+g] slot n ("an{7+g}{n:02d}", tools/prepare_skinned_model.py).
[17] / [18] are its caps and shapes, laid out as Beowulf's; its attack records (0x40 bytes) are at 0x5c3570
(FUN_003f2380's FUN_002942e0; 0x5c3330 for the other spawn block). Script n of group g is entry g * 24 + n.
"""
import argparse
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from gen_enemy_ai import Elf
from prepare_skinned_model import c_float, pac_list
import prepare_beowulf
import prepare_cerberus

SLOT = 121
GROUPS = (0, 1, 2, 3)
PER_GROUP = 24
ATTACKS_AT, ATTACK_COUNT = 0x5C3570, 16
MOTION_TABLE, MOTION_ROWS = 0x5BD0D0, 0x32
OP_GOTO = 3


def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        pack = pac_list(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
        executable = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    shaped = list(pack)
    shaped[11], shaped[12] = pack[17], pack[18]
    caps = prepare_beowulf.read_caps(shaped)
    prepare_cerberus.SCRIPT_COUNT = PER_GROUP
    prepare_cerberus.MOTION_LIMIT = 1000
    ops, starts = [], [-1] * (PER_GROUP * len(GROUPS))
    for group in GROUPS:
        group_ops, group_starts = prepare_cerberus.compile_bank(pack[16], caps, group)
        base = len(ops)
        ops += [(op, a, b + base if op == OP_GOTO else b, c) for op, a, b, c in group_ops]
        for n, start in enumerate(group_starts[:PER_GROUP]):
            if start >= 0:
                starts[group * PER_GROUP + n] = start + base
    table = [(executable.byte(MOTION_TABLE + i * 0x14), executable.byte(MOTION_TABLE + i * 0x14 + 1)) for i in range(MOTION_ROWS)]
    lines = ["/* The Arachne (em010): motion scripts, caps, attack records and its motion table. tools/prepare_arachne.py */",
             "#pragma once",
             "/* Include enemy_ai_runtime.h and agni_rudra_scripts.h first: the ops, caps and records are their types. */",
             f"#define AN_SCRIPTS {PER_GROUP * len(GROUPS)}",
             f"#define AN_PER_GROUP {PER_GROUP}",
             "static const EnemyAiOp an_ops[] = {" + ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
             f"static const short an_starts[AN_SCRIPTS] = {{" + ",".join(map(str, starts)) + "};",
             f"#define AN_CAPS {len(caps)}",
             "static const EnemyAiCap an_caps[] = {" + ",".join(
                 "{%d,%d,%s,%s,%s,%s}" % (k, b, c_float(x), c_float(y), c_float(z), c_float(r)) for k, b, x, y, z, r in caps) + "};",
             f"#define AN_ATTACKS {ATTACK_COUNT}",
             f"static const ArAttack an_attacks[AN_ATTACKS] = {{" + ",".join(
                 "{%s,%s,%d}" % (c_float(executable.real(ATTACKS_AT + i * 0x40 + 4)),
                                 c_float(executable.real(ATTACKS_AT + i * 0x40 + 0x1C)),
                                 executable.word(ATTACKS_AT + i * 0x40 + 0x18)) for i in range(ATTACK_COUNT)) + "};",
             f"#define AN_MOTION_ROWS {MOTION_ROWS}",
             "/* 0x5bd0d0: the script (group * AN_PER_GROUP + n) each motion index plays. */",
             "static const short an_motion_script[AN_MOTION_ROWS] = {" + ",".join(
                 str(g * PER_GROUP + n) for g, n in table) + "};"]
    output.mkdir(parents=True, exist_ok=True)
    (output / "arachne_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Arachne: {len(caps)} caps, {sum(1 for s in starts if s >= 0)} scripts, {len(ops)} ops -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
