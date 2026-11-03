"""Recover Dante's action id -> handler map from SLUS_213.61.

FUN_002625d0 is a compare chain on the current action (lbu v1, 0x2998(a0);
li a2, id; beq v1, a2, case). Each case block jal's one handler; the handler
requests its scripts with FUN_002500d0 (analysis/moveset/motion_calls.csv).
Ghidra does not decompile the chain, so it is emulated here.
"""
import argparse
import json
import struct
from pathlib import Path

START, END = 0x2625D0, 0x264400


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    d = a.elf.read_bytes()
    phoff = struct.unpack_from("<I", d, 0x1C)[0]
    _, off, va, _, _, _ = struct.unpack_from("<6I", d, phoff)
    word = lambda x: struct.unpack_from("<I", d, off + x - va)[0]
    regs, cases = {}, {}
    for pc in range(START, END, 4):
        w = word(pc)
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op in (0x09, 0x0D) and rs == 0:
            regs[rt] = simm if op == 0x09 else imm
        elif op == 4 and 3 in (rs, rt):
            other = rt if rs == 3 else rs
            if other in regs:
                cases.setdefault(regs[other], pc + 4 + (simm << 2))
    out = {}
    for action, block in sorted(cases.items()):
        pc = block
        for _ in range(64):
            w = word(pc)
            if w >> 26 == 3:
                out[action] = {"block": f"{block:#x}", "handler": f"{(w & 0x3FFFFFF) << 2:#x}"}
                break
            if w == 0x03E00008:
                break
            pc += 4
    a.out.write_text(json.dumps(out, indent=1))
    print(f"{len(cases)} actions, {len(out)} with a handler -> {a.out}")


if __name__ == "__main__":
    main()
