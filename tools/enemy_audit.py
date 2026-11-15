"""Run repeatable enemy collision scenes and retain complete per-frame evidence.

Requires a built testroom and DMC3_SLOT. Selects encounter rooms from the generated
mission scripts, including all secret-room encounters once. A nonzero exit means
violations OR missing/incomplete evidence, never a silent pass.
"""
import argparse
from collections import Counter
import csv
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GAME_ROOT = Path(r"E:\SYSTEMS\PPSSPP\memstick\PSP\GAME")


def encounters(header):
    return sorted({(int(m), int(r)) for m, r in re.findall(
        r"static const RoomOp m(\d+)_1_(\d+)_ops", header)
        if int(r) < 600 or int(m) == 1})


def summarize(path):
    data = path.read_text()
    match = re.search(r"# frames=(\d+) bodies=(\d+) violations=(\d+)", data)
    if not match:
        raise ValueError("audit footer missing: run did not finish")
    counts = Counter()
    depths = {}
    for row in csv.DictReader(io.StringIO(data.split("#", 1)[0])):
        counts[row["violation"]] += 1
        depths[row["violation"]] = max(depths.get(row["violation"], 0), float(row["depth"]))
    if sum(counts.values()) != int(match[3]):
        raise ValueError("audit row count differs from footer")
    return dict(frames=int(match[1]), bodies=int(match[2]), violations=int(match[3]),
                counts=dict(counts), max_depth=depths)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mission", type=int, action="append")
    ap.add_argument("--room", type=int)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--label", default="baseline")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--stress", action="store_true", help="apply heavy/launch/air/slam to all live enemies repeatedly")
    ap.add_argument("--force-encounters", action="store_true", help="bypass item/puzzle holds in direct room captures")
    ap.add_argument("--line", action="append", default=[], help="extra scene instruction")
    ap.add_argument("--eboot", type=Path, help="pin all scenes to this executable")
    args = ap.parse_args()
    slot = os.environ.get("DMC3_SLOT")
    if not slot:
        ap.error("set DMC3_SLOT before testing")
    game = GAME_ROOT / ("DMC3TESTROOM_" + slot)
    out = ROOT / "analysis" / "testroom" / ("enemy_audit_" + args.label)
    out.mkdir(parents=True, exist_ok=True)
    cases = encounters((ROOT / "build/testroom/generated/rooms_runtime.h").read_text())
    results = []
    for mission, room in cases:
        if args.mission and mission not in args.mission or args.room is not None and room != args.room:
            continue
        name = f"m{mission:02d}_r{room:03d}"
        dest = out / (name + ".csv")
        result = dict(mission=mission, room=room)
        try:
            if not (args.resume and dest.exists()):
                audit = game / "ENEMY_AUDIT.CSV"
                audit.unlink(missing_ok=True)
                cmd = [sys.executable, str(ROOT / "tools/ppsspp_scene.py"),
                       "enemy_" + args.label + "_" + name, "--mission", str(mission),
                       "--room", str(room), "--frames", str(args.frames),
                       "--shot", str(args.frames - 1), "--line", "enemyaudit " + ("2" if args.force_encounters else "1"),
                       "--line", "fastwaves 1"]
                if args.stress:
                    cmd += ["--line", "enemystress 45"]
                for line in args.line:
                    cmd += ["--line", line]
                if args.eboot:
                    cmd += ["--eboot", str(args.eboot.resolve())]
                log = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                (out / (name + ".log")).write_text(log.stdout + log.stderr)
                if log.returncode:
                    raise ValueError(f"scene exit {log.returncode}")
                if not audit.exists():
                    raise ValueError("no fresh audit file")
                dest.write_bytes(audit.read_bytes())
            result.update(summarize(dest))
            result["status"] = "FAIL" if result["violations"] else "PASS" if result["bodies"] else "EMPTY"
        except (OSError, ValueError) as exc:
            result.update(status="ERROR", error=str(exc))
        results.append(result)
        (out / "results.json").write_text(json.dumps(results, indent=2))
        print(name, json.dumps(result), flush=True)
    return int(any(r["status"] in ("FAIL", "ERROR") for r in results))


if __name__ == "__main__":
    sys.exit(main())
