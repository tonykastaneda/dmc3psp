"""Batch fixed-step enemy collision captures without rebooting for every room."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from enemy_audit import ROOT, GAME_ROOT, encounters, summarize


def wave_budgets(header, interval, minimum):
    """Budget kill intervals through every native live-count gate, plus settling."""
    budgets = {}
    for m, room, body in re.findall(r"static const RoomOp m(\d+)_1_(\d+)_ops\[\] = \{(.*?)\};", header):
        m, room = int(m), int(room)
        alive = kills = 0
        for op, a, _, _, _ in re.findall(r"\{(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)\}", body):
            if int(op) == 0:
                alive += not (m == 20 and room == 12 and int(a) == 55)  # Lady is an ally.
            elif int(op) == 2 and alive > int(a):
                kills += 1
                alive = 0
        kills += bool(alive)
        frames = max(minimum, (kills + 1) * interval, 1800 if room in (113, 603) else 0)
        budgets[m, room] = ((frames + interval - 1) // interval) * interval
    return budgets


def split_rooms(data):
    """Retain zero-violation rooms as evidence, using their body/frame counters."""
    summaries = {}
    for m, r, frames, bodies, violations, expected in re.findall(
            r"# room_summary mission=(\d+) room=(-?\d+) frames=(\d+) bodies=(\d+) violations=(\d+)(?: expected=(\d+))?", data):
        key = int(m), int(r)
        row = summaries.setdefault(key, [0, 0, 0, 0])
        for i, value in enumerate((frames, bodies, violations, expected or 0)):
            row[i] += int(value)
    lines = data.splitlines()
    progress = {(int(m), int(r)): (int(at), int(total)) for m,r,at,total in re.findall(
        r"# room_summary mission=(\d+) room=(-?\d+)[^\n]*script_at=(-?\d+) script_total=(-?\d+)",data)}
    header = lines[0] + "\n"
    rows = {}
    for line in lines[1:]:
        if line.startswith("#") or not line.strip():
            continue
        fields = line.split(",")
        rows.setdefault((int(fields[0]), int(fields[1])), []).append(line)
    return {key: header + "\n".join(rows.get(key, [])) + "\n" +
            "# frames=%d bodies=%d violations=%d expected=%d\n" % tuple(counts) +
            ("# script_at=%d script_total=%d\n" % progress[key] if key in progress else "")
            for key, counts in summaries.items()}


def table(results, expected):
    lines = ["| Mission | Rooms observed / expected | Body samples | Violations | Authored crossings | Empty / error / unfinished rooms |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for m in sorted({case[0] for case in expected}):
        rows = [r for r in results if r["mission"] == m]
        lines.append(f"| {m} | {len(rows)} / {sum(a == m for a, _ in expected)} | "
                     f"{sum(r.get('bodies', 0) for r in rows):,} | "
                     f"{sum(r.get('violations', 0) for r in rows):,} | "
                     f"{sum(sum(r.get('expected', {}).values()) for r in rows):,} | "
                     f"{sum(r['status'] in ('EMPTY', 'ERROR', 'INCOMPLETE') for r in rows)} |")
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
    ap.add_argument("--complete-waves", action="store_true", help="extend each room through all script gates; reject unfinished scripts")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--line", action="append", default=[])
    ap.add_argument("--timeout", type=int, default=180, help="wall-clock timeout per emulator batch")
    args = ap.parse_args()
    slot = os.environ.get("DMC3_SLOT")
    if not slot:
        ap.error("set DMC3_SLOT before testing")
    if args.frames < 1 or args.batch_size < 1 or args.frames * args.batch_size > 17999:
        ap.error("batch must contain 1..17999 frames")
    if args.complete_waves and not args.kill_every:
        ap.error("--complete-waves requires --kill-every")
    header = (ROOT / "build/testroom/generated/rooms_runtime.h").read_text()
    cases = encounters(header)
    budgets = wave_budgets(header, args.kill_every, args.frames) if args.complete_waves else {}
    cases = [(m, r) for m, r in cases if (not args.mission or m in args.mission) and
             (not args.room or r in args.room)]
    out = ROOT / "analysis/testroom" / ("enemy_audit_" + args.label)
    out.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256((ROOT / "build/testroom/EBOOT.PBP").read_bytes()).hexdigest()
    metadata = dict(eboot_sha256=digest, frames=args.frames, stress=args.stress,
                    kill_every=args.kill_every, complete_waves=args.complete_waves, lines=args.line, cases=cases)
    meta_path = out / "metadata.json"
    if args.resume and meta_path.exists() and json.loads(meta_path.read_text()) != json.loads(json.dumps(metadata)):
        ap.error("resume metadata differs; use a new label for a changed executable or test")
    meta_path.write_text(json.dumps(metadata, indent=2))
    executable = out / "EBOOT.PBP"
    # Each batch must use the executable identified by metadata, even if a
    # subsequent local build finishes while this long sweep is still running.
    shutil.copy2(ROOT / "build/testroom/EBOOT.PBP", executable)
    results = []
    if args.resume and (out / "results.json").exists():
        results = json.loads((out / "results.json").read_text())
    complete = {(r["mission"], r["room"]) for r in results if r["status"] != "ERROR"}
    pending = [case for case in cases if case not in complete]
    game = GAME_ROOT / ("DMC3TESTROOM_" + slot)
    for first in range(0, len(pending), args.batch_size):
        batch = pending[first:first + args.batch_size]
        durations = [budgets.get(case, args.frames) for case in batch]
        starts = [sum(durations[:i]) for i in range(len(batch))]
        if sum(durations) > 17999:
            ap.error("extended batch exceeds 17999 frames; reduce --batch-size")
        name = f"enemy_{args.label}_batch_{first:03d}"
        source = game / "ENEMY_AUDIT.CSV"
        source.unlink(missing_ok=True)
        cmd = [sys.executable, str(ROOT / "tools/ppsspp_scene.py"), name,
               "--mission", str(batch[0][0]), "--room", str(batch[0][1]),
               "--frames", str(sum(durations)),
               "--eboot", str(executable),
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
                cmd += ["--line", f"auditroom {starts[i]} {m} {room} 0"]
            cmd += ["--shot", str(starts[i] + durations[i] - 1)]
            if room in (113, 603):
                at = starts[i]
                low = 35 if room == 603 else 20
                cmd += ["--line", f"place {at+2} 2250 90 1125 0 1",
                        "--line", f"place {at+4} 2750 {low} 2350 0"]
            if args.stress and (m, room) == (12, 228):
                cmd += ["--line", f"bosshits {starts[i]+120} 20"]  # Existing M12 regression's bridge trigger.
        (out / (name + ".command.json")).write_text(json.dumps(cmd, indent=2))
        run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        player_log = game / "CMP_OUT.TXT"
        if player_log.exists():
            (out / (name + ".player.txt")).write_bytes(player_log.read_bytes())
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
            result = dict(mission=m, room=room, screenshot=f"{name}_{starts[i] + durations[i] - 1}.png")
            try:
                if error:
                    raise ValueError(error)
                if (m, room) not in evidence:
                    raise ValueError("room never sampled")
                dest = out / f"m{m:02d}_r{room:03d}.csv"
                dest.write_text(evidence[m, room])
                result.update(summarize(dest))
                if result["frames"] < durations[i]:
                    raise ValueError(f"only {result['frames']} / {durations[i]} room frames sampled")
                result["status"] = "FAIL" if result["violations"] else "PASS" if result["bodies"] else "EMPTY"
                if args.complete_waves and not result.get("script_complete"):
                    result["status"] = "INCOMPLETE"
            except (OSError, ValueError) as exc:
                result.update(status="ERROR", error=str(exc))
            results = [r for r in results if (r["mission"], r["room"]) != (m, room)] + [result]
            print(json.dumps(result), flush=True)
        (out / "results.json").write_text(json.dumps(results, indent=2))
        (out / "summary.md").write_text(table(results, cases))
    return int(any(r["status"] != "PASS" for r in results))


if __name__ == "__main__":
    sys.exit(main())
