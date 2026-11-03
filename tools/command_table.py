"""Dump a player command table (Dante in game: 0x5a8e90, set by FUN_0025bb00
into player+0x2838) from SLUS_213.61 into a CSV.

Record (0x28 bytes, list ends at type 5), walked by FUN_002458f0:
  [0]    type        3 = style command (needs style == [0xd]); 5 = end
  [1]    weapon req  -1 any, else compared with player+0x28a8 / 7 special
  [2]    action id   becomes player+0x2998 (current action) on success;
                     also a bit in the unlock mask at player+0x29d8
  [3]    category    10 = disabled entry
  [4]    u32 flags   0x100 usable from neutral, 0x200 usable as a cancel
                     (FUN_0024bc40 conditions), 0x400 extra check FUN_0024b510
  [8..a] input bytes (not yet decoded; FUN_0024b2d0 / FUN_0024b1d0)
  [0xb]  weapon      weapon id 0-9 (0xff = none), [0xc] its check arg
  [0xd]  style       0 SM, 1 GS, 2 TR, 3 RG, 4 QS, 5 DG
  [0x10] u32 posture 1 ground, 2 air, 3 both, 0x100000 special (FUN_00245db0)
  [0x14..0x24] five conditions: 0x80000000|n = gate n open (player+0x24dd[n],
         set by motion script window 1), bit 0x800000 = optional;
         0x40000000|id = current action is id.
"""
import argparse
import csv
import struct
from pathlib import Path


def reader(path):
    d = path.read_bytes()
    phoff, n = struct.unpack_from("<I", d, 0x1C)[0], struct.unpack_from("<H", d, 0x2C)[0]
    segs = [struct.unpack_from("<6I", d, phoff + i * 32) for i in range(n)]

    def rd(va, size):
        for _, off, v, _, fs, _ in segs:
            if v <= va < v + fs:
                return d[off + va - v: off + va - v + size]
        raise ValueError(hex(va))
    return rd


BUTTONS = {0x10: "TRI", 0x20: "CIR", 0x40: "X", 0x80: "SQ", 0x08: "R1", 0x04: "L1",
           0x02: "R2", 0x01: "L2"}
# Stick sector vs facing / target (FUN_00253650). Orientation fixed by data:
# action 7/8 (sector 0x10) dispatches to the three-phase Stinger scripts.
DIRS = {0x10: "fwd", 0x40: "back", 0x80: "fwd(target)", 0x20: "back(target)",
        0x90: "fwd(locked)", 0x30: "back(locked)", 0xF0: "neutral"}
PRECOND = {0: "", 1: "R1 held", 2: "not state 0x18", 3: "Artemis equipped"}
DEFAULT_PRESS = {0: "TRI", 1: "SQ", 3: "CIR"}   # FUN_00245f10 fallback per type
PATTERNS = 0x5950E0      # 58 x {s16 steps, s16 precond, s16 reset, s16 0, u32 steps_ptr}
INDIRECT = 0x594BC8      # [character] -> [k] -> [player+0x3d90] -> pattern id


def step_text(w):
    """One 10-byte input step (FUN_00253650)."""
    kind, btn = w[0], BUTTONS.get(w[1], f"btn{w[1]:#x}")
    if kind & 1:
        how = "press" if kind & 0x8000 else "release" if kind & 0x4000 else "hold"
        return f"{how} {btn}"
    if kind & 2:
        return DIRS.get(w[2] & 0xF0, f"dir{w[2]:#x}")
    if kind & 4:
        return f"charge {btn} {w[3]}f" + (" release" if kind & 0x4000 else "")
    if kind & 8:
        return f"mash {btn} >{w[3]}x (gap<={w[2]}f)"
    return f"step{kind:#x}"


def patterns(rd):
    out = []
    for i in range(58):
        steps, pre, _, _, ptr = struct.unpack_from("<4hI", rd(PATTERNS + i * 12, 12))
        seq = [step_text(struct.unpack_from("<5H", rd(ptr + k * 10, 10))) for k in range(steps)]
        text = " + ".join(seq)
        if PRECOND.get(pre):
            text = f"[{PRECOND[pre]}] " + text
        out.append(text)
    return out


def input_text(rd, pats, r):
    mode, k = r[8], r[9]
    if mode & 1:
        return pats[k]
    if mode & 2:
        u32 = lambda a: struct.unpack("<I", rd(a, 4))[0]
        ids = sorted({rd(u32(u32(u32(INDIRECT) + 4 * k) + 4 * m), 1)[0] for m in range(8)})
        return " | ".join(pats[i] for i in ids)
    return f"press {DEFAULT_PRESS[r[0]]}" if r[0] in DEFAULT_PRESS else ""


def cond(c):
    if c == 0:
        return ""
    if c >> 24 == 0x80:
        return f"gate{c & 0xff}" + ("?" if c & 0x800000 else "")
    if c >> 24 == 0x40:
        return f"after{c & 0xff}"
    return f"{c:08x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--table", type=lambda x: int(x, 16), default=0x5A8E90)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    rd = reader(a.elf)
    pats = patterns(rd)
    rows = []
    for i in range(1000):
        r = rd(a.table + i * 0x28, 0x28)
        if r[0] == 5:
            break
        flags, posture = struct.unpack_from("<I", r, 4)[0], struct.unpack_from("<I", r, 0x10)[0]
        rows.append({
            "row": i, "type": r[0], "weapon_req": r[1] if r[1] != 0xFF else -1,
            "action": r[2], "category": r[3], "flags": f"{flags:#x}",
            "input": r[8:0xB].hex(), "input_text": input_text(rd, pats, r), "weapon": r[0xB] if r[0xB] != 0xFF else -1,
            "weapon_arg": r[0xC], "style": r[0xD], "b_e": r[0xE], "b_f": r[0xF],
            "posture": {1: "ground", 2: "air", 3: "both"}.get(posture, f"{posture:#x}"),
            "conditions": " ".join(filter(None, (cond(c) for c in struct.unpack_from("<5I", r, 0x14)))),
        })
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with a.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    (a.out.parent / "input_patterns.csv").write_text(
        "id,pattern\n" + "".join(f'{i},"{t}"\n' for i, t in enumerate(pats)))
    print(f"{len(rows)} commands -> {a.out}")


if __name__ == "__main__":
    main()
