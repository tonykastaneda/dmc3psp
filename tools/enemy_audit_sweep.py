"""Batch fixed-step enemy collision captures without rebooting for every room."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from enemy_audit import ROOT, GAME_ROOT, encounters, summarize


def split_rooms(data):
    """Retain zero-violation rooms as evidence, using their body/frame counters."""
    summaries = {}
    for m, r, frames, bodies, violations in re.findall(
            r"# room_summary mission=(\d+) room=(-?\d+) frames=(\d+) bodies=(\d+) violations=(\d+)", data):
        key = int(m), int(r)
        row = summaries.setdefault(key, [0, 0, 0])
        for i, value in enumerate((frames, bodies, violations)):
            row[i] += int(value)
    lines = data.splitlines()
    header = lines[0] + "\n"
    rows = {}
    for line in lines[1:]:
        if line.startswith("#") or not line.strip():
            continue
        fields = line.split(",")
        rows.setdefault((int(fields[0]), int(fields[1])), []).append(line)
    return {key: header + "\n".join(rows.get(key, [])) + "\n" +
            "# frames=%d bodies=%d violations=%d\n" % tuple(counts)
            for key, counts in summaries.items()}


def table(results, expected):
    lines = ["| Mission | Rooms observed / expected | Body samples | Violations | Empty / error rooms |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for m in sorted({case[0] for case in expected}):
        rows = [r for r in results if r["mission"] == m]
        lines.append(f"| {m} | {len(rows)} / {sum(a == m for a, _ in expected)} | "
                     f"{sum(r.get('bodies', 0) for r in rows):,} | "
                     f"{sum(r.get('violations', 0) for r in rows):,} | "
                     f"{sum(r['status'] in ('EMPTY', 'ERROR') for r in rows)} |")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--label", required=True)
    ap.add_argument("--mission", type=int, action="append")
    ap.add_argument("--room", type=int, action="append")
    ap.add_argument("--frames", type=int, default=600, help="frames per room")
    ap.add_argument("--batch-size", type=int, default=10)
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--kill-every", type=int, default=0, help="retire waves at this scene interval")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--line", action="append", default=[])
    ap.add_argument("--timeout", type=int, default=180, help="wall-clock timeout per emulator batch")
    args = ap.parse_args()
    slot = os.environ.get("DMC3_SLOT")
    if not slot:
        ap.error("set DMC3_SLOT before testing")
    if args.frames < 1 or args.batch_size < 1 or args.frames * args.batch_size > 17999:
        ap.error("batch must contain 1..17999 frames")
    cases = encounters((ROOT / "build/testroom/generated/rooms_runtime.h").read_text())
    cases = [(m, r) for m, r in cases if (not args.mission or m in args.mission) and
             (not args.room or r in args.room)]
    out = ROOT / "analysis/testroom" / ("enemy_audit_" + args.label)
    out.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256((ROOT / "build/testroom/EBOOT.PBP").read_bytes()).hexdigest()
    metadata = dict(eboot_sha256=digest, frames=args.frames, stress=args.stress,
                    kill_every=args.kill_every, lines=args.line, cases=cases)
    meta_path = out / "metadata.json"
    if args.resume and meta_path.exists() and json.loads(meta_path.read_text()) != json.loads(json.dumps(metadata)):
        ap.error("resume metadata differs; use a new label for a changed executable or test")
    meta_path.write_text(json.dumps(metadata, indent=2))
    results = []
    if args.resume and (out / "results.json").exists():
        results = json.loads((out / "results.json").read_text())
    complete = {(r["mission"], r["room"]) for r in results if r["status"] != "ERROR"}
    pending = [case for case in cases if case not in complete]
    game = GAME_ROOT / ("DMC3TESTROOM_" + slot)
    for first in range(0, len(pending), args.batch_size):
        batch = pending[first:first + args.batch_size]
        name = f"enemy_{args.label}_batch_{first:03d}"
        source = game / "ENEMY_AUDIT.CSV"
        source.unlink(missing_ok=True)
        cmd = [sys.executable, str(ROOT / "tools/ppsspp_scene.py"), name,
               "--mission", str(batch[0][0]), "--room", str(batch[0][1]),
               "--frames", str(len(batch) * args.frames),
               "--timeout", str(args.timeout),
               "--line", "enemyaudit 2", "--line", "auditfast 1", "--line", "fastwaves 1"]
        if args.stress:
            cmd += ["--line", "enemystress 45"]
        if args.kill_every:
            cmd += ["--line", f"killevery {args.kill_every}"]
        for line in args.line:
            cmd += ["--line", line]
        if (8, 302) in batch:
            cmd += ["--line", "item 46 2"]  # Ignis Fatuus used: native Heart encounter trigger.
        for i, (m, room) in enumerate(batch):
            if i:
                cmd += ["--line", f"auditroom {i * args.frames} {m} {room} 0"]
            cmd += ["--shot", str((i + 1) * args.frames - 1)]
            if room in (113, 603):
                at = i * args.frames
                low = 35 if room == 603 else 20
                cmd += ["--line", f"place {at+2} 2250 90 1125 0 1",
                        "--line", f"place {at+4} 2750 {low} 2350 0"]
            if args.stress and (m, room) == (12, 228):
                cmd += ["--line", f"bosshits {i * args.frames+120} 20"]  # Existing M12 regression's bridge trigger.
        (out / (name + ".command.json")).write_text(json.dumps(cmd, indent=2))
        run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        (out / (name + ".log")).write_text(run.stdout + run.stderr)
        evidence = {}
        error = None
        try:
            if run.returncode:
                raise ValueError(f"scene exit {run.returncode}")
            data = source.read_text()
            (out / (name + ".csv")).write_text(data)
            summarize(source)  # Reject incomplete batches even if a room summary was flushed.
            evidence = split_rooms(data)
        except (OSError, ValueError) as exc:
            error = str(exc)
        for i, (m, room) in enumerate(batch):
            result = dict(mission=m, room=room, screenshot=f"{name}_{(i + 1) * args.frames - 1}.png")
            try:
                if error:
                    raise ValueError(error)
                if (m, room) not in evidence:
                    raise ValueError("room never sampled")
                dest = out / f"m{m:02d}_r{room:03d}.csv"
                dest.write_text(evidence[m, room])
                result.update(summarize(dest))
                if result["frames"] < args.frames:
                    raise ValueError(f"only {result['frames']} / {args.frames} room frames sampled")
                result["status"] = "FAIL" if result["violations"] else "PASS" if result["bodies"] else "EMPTY"
            except (OSError, ValueError) as exc:
                result.update(status="ERROR", error=str(exc))
            results = [r for r in results if (r["mission"], r["room"]) != (m, room)] + [result]
            print(json.dumps(result), flush=True)
        (out / "results.json").write_text(json.dumps(results, indent=2))
        (out / "summary.md").write_text(table(results, cases))
    return int(any(r["status"] in ("FAIL", "ERROR", "EMPTY") for r in results))


if __name__ == "__main__":
    sys.exit(main())
