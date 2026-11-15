"""Wall probe: python tools/wall_probe.py mission [room] [deg_per_frame]. Spins the stick at full tilt with Trickster dashes
and jumps, then lists frames where Dante is still inside a wall (pen, cm), went through one since the last frame
(cross: 1 waist, 2 chest) or stands off the collision floor grid. PF=frames, SHOTS=a,b,c for stills. Needs DMC3_SLOT."""
import os, subprocess, sys
from pathlib import Path
m = sys.argv[1]; room = sys.argv[2] if len(sys.argv) > 2 else "-1"; rate = sys.argv[3] if len(sys.argv) > 3 else "0.9"
frames = int(os.environ.get("PF", "1200"))
lines = [f"spin {rate} 20 {frames}", "style0 2 3"]
for f in range(60, frames, 70):
    lines.append(f"circle {f} {f+2}")
for f in range(95, frames, 140):
    lines.append(f"cross {f} {f+2}")
args = ["python", "tools/ppsspp_scene.py", f"probe_m{m}_{room}", "--mission", m, "--frames", str(frames)]
if room != "-1": args += ["--room", room]
for l in lines: args += ["--line", l]
if os.environ.get("SHOTS"):
    for s in os.environ["SHOTS"].split(","): args += ["--shot", s]
r = subprocess.run(args, capture_output=True, text=True)
out = Path(r"E:\SYSTEMS\PPSSPP\memstick\PSP\GAME") / ("DMC3TESTROOM" + ("_" + os.environ["DMC3_SLOT"] if os.environ.get("DMC3_SLOT") else "")) / "CMP_OUT.TXT"
rows = [l.split() for l in out.read_text().splitlines()]
bad = []
for c in rows:
    if len(c) < 15: continue
    pen, cross, outside, ground, rm = float(c[-3]), int(c[-2]), int(c[-1]), int(c[-7]), c[-6]
    if pen > 3 or cross or (outside and ground):
        bad.append((int(c[0]), rm, round(float(c[1])), round(float(c[2])), round(float(c[3])), pen, cross, outside, c[6]))
rooms = sorted({c[-6] for c in rows if len(c) > 10})
print(f"M{m} room {room}: {len(rows)} frames, rooms {rooms}, bad {len(bad)}")
for b in bad[:40]: print("  ", b)
xs=[float(c[1]) for c in rows]; zs=[float(c[3]) for c in rows]
print("  x", round(min(xs)), round(max(xs)), "z", round(min(zs)), round(max(zs)), "max pen", max(float(c[-3]) for c in rows))
