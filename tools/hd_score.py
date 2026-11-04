"""Movement scorecard: one key script on the HD Collection's DMC3 and on the port, compared.

    python tools/hd_score.py hd [names]     # record the real game (analysis/ref/hd/score_hd.json)
    python tools/hd_score.py port [30|60] [names]   # record the port, at 60 or the PSP's 30 FPS
    python tools/hd_score.py show [30|60]   # the table, also analysis/ref/hd/score.json

Both run Turbo, as the save does, so frames compare directly. Curves are
lined up at their first movement, so keyboard lag and the port's start don't count against it.
"""
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_trace as t

FPS = 60
CAMERA_YAW = int(Path(__file__).resolve().parent.parent.joinpath("build/hdref/camyaw.txt").read_text()) if Path(__file__).resolve().parent.parent.joinpath("build/hdref/camyaw.txt").exists() else 0
OUT = t.OUT
TESTS = {
    "Run": "w_d", "Jump (tap)": "jumptap", "Jump": "jump", "Jump (held)": "jumpheld", "Jump off a run": "runjump",
    "Air Hike (tap)": "hiketap", "Air Hike": "hike", "Air Hike (held)": "hikeheld", "2nd Air Hike refused": "hikehike",
    "Air Hike with stick": "hikestick", "Air dash": "star", "Air dash with stick": "starstick",
    "Hike, air dash": "hikestar", "Air dash, hike": "starhike", "2nd air dash refused": "hikestarstar",
    "Ground dash": "dash", "Dash chain": "dash3",
    "Lock-on dash fwd": "d_u", "Lock-on dash back": "d_d", "Lock-on dash left": "d_l", "Lock-on dash right": "d_r",
}


def load(path):
    return json.loads(path.read_text()) if path.exists() else {}


def record_hd(names):
    path = OUT / "score_hd.json"
    data = load(path)
    for n in names:
        rows = t.per_frame(t.run(n))
        data[n] = [{"f": r["frame"], "x": r["x"], "y": r["y"], "z": r["z"]} for r in rows]
        path.write_text(json.dumps(data))
        print("hd", n, len(rows))


def record_port(names):
    # In Turbo, as the save runs, frame for frame. fps: 60, or 30 as on the PSP.
    import ref_compare as cmp
    path = OUT / f"score_port{FPS}.json"
    data = load(path)
    per = 60 // FPS
    for n in names:
        seconds, keys = t.SCRIPTS[n]
        events = [({"space": "r1"}.get(k, k), (int(a * 60) + 1) // per, (int(b * 60) + 1) // per - 1) for k, a, b in keys]
        events += [("enemy", 700, 0), ("camyaw", CAMERA_YAW, 0), ("turbo", 1, 0), ("fps", FPS, 0)]
        cmp.SCRIPTS["_s"] = (int(seconds * 60) // per, events)
        cmp.record_port("_s")
        rows = json.loads((cmp.OUT / "cmp_psp__s.json").read_text())
        data[n] = [{"f": r["f"] * per, "x": r["x"], "y": r["y"], "z": r["z"]} for r in rows[4:]]
        path.write_text(json.dumps(data))


def curves(rows):
    """Travel and height at each normal-speed frame, from the first movement."""
    start, y0 = None, rows[0]["y"]
    # Line up on takeoff if he leaves the ground, else on the first step. Small settling at the
    # start of a recording isn't movement.
    for a, b in zip(rows, rows[1:]):
        if abs(b["y"] - a["y"]) > 3.0 and b["y"] > a["y"]:
            start = a["f"]
            break
    if start is None:
        for a, b in zip(rows, rows[1:]):
            if math.hypot(b["x"] - a["x"], b["z"] - a["z"]) > 2.5:
                start = a["f"]
                break
    if start is None:
        return {"travel": [0.0], "height": [0.0], "start": 0.0}
    last, d, pts = rows[0], 0.0, []
    for r in rows:
        step = math.hypot(r["x"] - last["x"], r["z"] - last["z"])
        if step < 200.0:   # not a teleport
            d += step
        last = r
        pts.append((r["f"] - start, d, r["y"] - y0))
    end = int(pts[-1][0])
    travel, height, i = [], [], 0
    for f in range(0, end):
        while i + 1 < len(pts) and pts[i + 1][0] <= f:
            i += 1
        travel.append(pts[i][1])
        height.append(pts[i][2])
    return {"travel": travel, "height": height, "start": start}


def air_frames(height):
    return sum(1 for h in height if h > 3.0)


def compare(a, b):
    n = min(len(a["height"]), len(b["height"]))
    if n == 0:
        return None
    base_a, base_b = a["travel"][0], b["travel"][0]
    rms_h = math.sqrt(sum((a["height"][i] - b["height"][i]) ** 2 for i in range(n)) / n)
    rms_d = math.sqrt(sum(((a["travel"][i] - base_a) - (b["travel"][i] - base_b)) ** 2 for i in range(n)) / n)
    return {"travel": (a["travel"][n - 1] - base_a, b["travel"][n - 1] - base_b), "peak": (max(a["height"]), max(b["height"])),
            "air": (air_frames(a["height"]), air_frames(b["height"])), "rms_height": rms_h, "rms_travel": rms_d}


def show():
    hd, port = load(OUT / "score_hd.json"), load(OUT / f"score_port{FPS}.json")
    results = {}
    print(f"{'test':24} {'travel real/port':>18} {'peak real/port':>16} {'air fr real/port':>18} {'height err':>10} {'travel err':>10}")
    for label, n in TESTS.items():
        if n not in hd or n not in port:
            continue
        c = compare(curves(hd[n]), curves(port[n]))
        results[label] = c
        print(f"{label:24} {c['travel'][0]:8.0f} /{c['travel'][1]:7.0f}  {c['peak'][0]:7.0f} /{c['peak'][1]:6.0f}  "
              f"{c['air'][0]:8d} /{c['air'][1]:7d}  {c['rms_height']:9.1f} {c['rms_travel']:9.1f}")
    (OUT / "score.json").write_text(json.dumps(results, indent=1))


if __name__ == "__main__":
    what = sys.argv[1]
    args = sys.argv[2:]
    if args and args[0] in ("30", "60"):
        FPS = int(args.pop(0))
    names = args or list(TESTS.values())
    if what == "hd":
        record_hd(names)
    elif what == "port":
        record_port(names)
    else:
        show()
