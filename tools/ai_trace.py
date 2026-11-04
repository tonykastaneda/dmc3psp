"""Runs enemies with Dante standing still in PPSSPP and prints what their AI did.

    python tools/ai_trace.py [enemies] [frames] [extra script lines...]

Reads AI_TRACE.TXT: one line per change of plan or action, and per attack landing."""
import collections
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = Path(r"E:\SYSTEMS\PPSSPP\memstick\PSP\GAME\DMC3TESTROOM")
PLANS = {23: "decide", 24: "watch", 27: "swing", 28: "wide swing", 32: "leap", 33: "step left", 34: "step right",
         35: "step back", 51: "taunt A", 52: "taunt B", 0: "flinch", 1: "flinch back", 2: "launched", 9: "air hit"}


def run(count, frames, extra):
    script = ["frames 100", "turbo 0", "fps 60", f"ramp {frames}", f"rampfrom {count}", f"rampto {count}",
              "enemy 3000", "aitrace 1"] + extra
    (GAME_DIR / "CMP_SCRIPT.TXT").write_text("\n".join(script) + "\n")
    subprocess.run(["powershell", "-NoProfile", "-File", str(ROOT / "tools" / "test_focus.ps1"), "-Name", "cmp",
                    "-TimeoutSeconds", str(int(frames / 30) + 60)], check=False, capture_output=True)
    rows = []
    for line in (GAME_DIR / "AI_TRACE.TXT").read_text().splitlines():
        p = line.split()
        rows.append(dict(tick=int(p[0]), who=int(p[1]), plan=int(p[2]), action=int(p[3]), motion=int(p[4]),
                         landed=int(p[5]), x=float(p[6]), y=float(p[7]), z=float(p[8]), yaw=float(p[9]),
                         gauge=float(p[10]), frame=float(p[11])))
    return rows


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
    rows = run(count, frames, sys.argv[3:])
    picks = collections.Counter(PLANS.get(r["plan"], r["plan"]) for r in rows
                                if not r["landed"] and r["plan"] not in (23,))
    print(f"{len(rows)} rows; plans entered:", dict(picks))
    print("attacks landed:", sum(1 for r in rows if r["landed"]))
    for r in rows[:60 if count == 1 else 25]:
        print(f'{r["tick"]:5d} #{r["who"]:<2d} {PLANS.get(r["plan"], r["plan"])!s:11} action {r["action"]:2d} '
              f'y {r["y"]:6.1f} pos {r["x"]:7.1f} {r["z"]:7.1f} gauge {r["gauge"]:5.1f}'
              + (" LANDED" if r["landed"] else ""))


if __name__ == "__main__":
    main()
