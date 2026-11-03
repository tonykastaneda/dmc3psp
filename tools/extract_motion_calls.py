#!/usr/bin/env python3
"""Extract every motion request (FUN_002500d0) from Ghidra's decompiled C.

FUN_002500d0(float blend, this, bank, index, p5, p6, p7) stores the motion
id as bank << 8 | index and looks the bank up in a per-character table
(0x593af0[character][bank]); it is the SE executable's "play motion" for
player characters. Each call site with constant (bank, index) names the
exact motion that code path plays.

Output: analysis/moveset/motion_calls.csv  (function, line, blend, bank,
index, p5, p6, p7, raw args)
"""

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMPILED = ROOT / "analysis/ghidra/export/decompiled"
OUT = ROOT / "analysis/moveset/motion_calls.csv"


def split_args(text):
    args, depth, cur = [], 0, ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def const(value):
    v = value.strip()
    m = re.fullmatch(r"\(?(?:\(\w+\))?\s*(-?(?:0x[0-9a-fA-F]+|\d+))\s*\)?", v)
    if not m:
        return None
    return int(m.group(1), 0)


def main():
    rows = []
    for path in sorted(DECOMPILED.glob("*.c")):
        func = path.stem.split("_")[0]
        text = path.read_text(errors="replace")
        for m in re.finditer(r"FUN_002500d0\(", text):
            start = m.end()
            depth, i = 1, start
            while i < len(text) and depth:
                depth += {"(": 1, ")": -1}.get(text[i], 0)
                i += 1
            args = split_args(text[start:i - 1])
            line = text.count("\n", 0, m.start()) + 1
            vals = [const(a) for a in args]
            rows.append({
                "function": func, "line": line,
                "blend": args[0] if args else "",
                "bank": vals[2] if len(vals) > 2 else None,
                "index": vals[3] if len(vals) > 3 else None,
                "p5": args[4] if len(args) > 4 else "", "p6": args[5] if len(args) > 5 else "",
                "p7": args[6] if len(args) > 6 else "",
                "raw": ", ".join(args),
            })
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    const_rows = [r for r in rows if r["bank"] is not None and r["index"] is not None]
    banks = {}
    for r in const_rows:
        banks.setdefault(r["bank"], set()).add(r["index"])
    print(f"{len(rows)} motion requests, {len(const_rows)} with constant bank/index -> {OUT}")
    for b in sorted(banks):
        print(f"  bank {b}: {len(banks[b])} distinct indices {sorted(banks[b])[:40]}")


if __name__ == "__main__":
    sys.exit(main())
