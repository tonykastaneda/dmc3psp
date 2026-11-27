"""The heap by call site, from a -HeapStat build's HEAPSTAT.TXT (psp/testroom/heapstat.c).

    python tools/heapstat_report.py [HEAPSTAT.TXT] [--top 25]

Each dump's sites, largest first, with the function and line of the allocation (psp-addr2line on testroom.elf)."""
import argparse
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "testroom" / "testroom.elf"
ADDR2LINE = ROOT / ".tools" / "pspdev" / "bin" / "psp-addr2line.exe"
GAME = Path(r"E:\SYSTEMS\PPSSPP\memstick\PSP\GAME") / ("DMC3TESTROOM" + (("_" + os.environ["DMC3_SLOT"]) if os.environ.get("DMC3_SLOT") else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", type=Path, default=GAME / "HEAPSTAT.TXT")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()
    dumps = []
    for line in args.path.read_text().splitlines():
        if line.startswith("=="):
            dumps.append((line[3:], []))
        elif line.strip() and dumps:
            a, live, count = line.split()
            dumps[-1][1].append((int(a, 16), int(live), int(count)))
    addrs = sorted({a for _, s in dumps for a, _, _ in s})
    names = {}
    if addrs:
        out = subprocess.run([str(ADDR2LINE), "-f", "-s", "-e", str(ELF)] + [hex(a - 8) for a in addrs],
                             capture_output=True, text=True).stdout.splitlines()
        for i, a in enumerate(addrs):
            names[a] = f"{out[2 * i]} ({out[2 * i + 1]})" if 2 * i + 1 < len(out) else hex(a)
    for label, sites in dumps:
        print(f"== {label}  tracked {sum(l for _, l, _ in sites) // 1024} KB in sites of 16 KB or more")
        for a, live, count in sorted(sites, key=lambda s: -s[1])[: args.top]:
            print(f"  {live // 1024:7d} KB {count:5d}  {names.get(a, hex(a))}")


if __name__ == "__main__":
    main()
