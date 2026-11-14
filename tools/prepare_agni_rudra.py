#!/usr/bin/env python3
"""Export Agni & Rudra's (class 27, em026) motion scripts, hit caps and attack records.

From the player's own disc: GDATA slot 131 (em026.pac) and SLUS_213.61. em026 [12] is Agni's script bank and [13]
Rudra's (the brothers' state machine plays script N, `docs/ENEMY_SPECS.md` F); [14]/[15] are the caps, laid out as
Cerberus's [29]/[30]; the attack records (0x40 bytes: damage +4, kind +0x18, reaction +0x1c) are at 0x5cf6f0.
The motions themselves (arNN) come from tools/prepare_skinned_model.py.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from gen_enemy_ai import Elf
from prepare_skinned_model import c_float, pnst_entries
import prepare_cerberus

SLOT = 131
SCRIPTS = 100
ATTACKS_AT, ATTACK_COUNT = 0x5CF6F0, 22


def read_caps(pack):
    caps = []
    for i in range(len(pack[14]) // 4):
        kind, bone, shape = struct.unpack_from("<BBH", pack[14], i * 4)
        at = shape * 0x50
        xyz = struct.unpack_from("<3f", pack[15], at + 0x10)
        shape_kind = struct.unpack_from("<I", pack[15], at)[0]
        radius = struct.unpack_from("<f", pack[15], at + (0x30 if shape_kind == 4 else 0x20))[0]
        caps.append((kind, bone, *xyz, radius))
    return caps


def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[SLOT]
        pack = pnst_entries(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
        executable = Elf(iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"]))
    caps = read_caps(pack)
    prepare_cerberus.SCRIPT_COUNT = SCRIPTS
    lines = ["/* Agni & Rudra (em026): motion scripts, caps and attack records. tools/prepare_agni_rudra.py */",
             "#pragma once",
             "/* Include enemy_ai_runtime.h first: the ops and caps are the Seven Hells' types (op 4 is an event). */",
             f"#define AR_SCRIPTS {SCRIPTS}"]
    for bank, index in enumerate((12, 13)):
        ops, starts = prepare_cerberus.compile_bank(pack[index], caps)
        lines += [f"static const EnemyAiOp ar_ops_{bank}[] = {{" + ",".join("{%d,%d,%d,%d}" % op for op in ops) + "};",
                  f"#define AR_OPS_{bank} {len(ops)}",
                  f"static const short ar_starts_{bank}[{SCRIPTS}] = {{" + ",".join(map(str, starts)) + "};"]
    lines += ["/* A cap: kind (1 = attack), bone, centre in the bone's space, radius. */",
              f"#define AR_CAPS {len(caps)}",
              "static const EnemyAiCap ar_caps[] = {" + ",".join(
                  "{%d,%d,%s,%s,%s,%s}" % (k, b, c_float(x), c_float(y), c_float(z), c_float(r)) for k, b, x, y, z, r in caps) + "};",
              "typedef struct { float damage, reaction; int kind; } ArAttack;",
              f"static const ArAttack ar_attacks[{ATTACK_COUNT}] = {{" + ",".join(
                  "{%s,%s,%d}" % (c_float(executable.real(ATTACKS_AT + i * 0x40 + 4)),
                                  c_float(executable.real(ATTACKS_AT + i * 0x40 + 0x1C)),
                                  executable.word(ATTACKS_AT + i * 0x40 + 0x18)) for i in range(ATTACK_COUNT)) + "};"]
    output.mkdir(parents=True, exist_ok=True)
    (output / "agni_rudra_scripts.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Agni & Rudra: {len(caps)} caps, 2 script banks -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
