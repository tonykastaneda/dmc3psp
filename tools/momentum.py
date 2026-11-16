"""Momentum check: every move of Dante's in every situation he can start it from, the PS2 against the port,
scored on how he moves frame by frame (speed, rise and fall, turning, timing), not only on where he ends up.

    python tools/momentum.py list                 # the generated scripts
    python tools/momentum.py ps2 [names|prefix*]  # record them on the PS2 (starts our own PCSX2 if needed)
    python tools/momentum.py psp [names|prefix*]  # record them on the port (PPSSPP)
    python tools/momentum.py score [names]        # analysis/ref/momentum/score.json + report.md
    python tools/momentum.py show name            # the frame by frame curves of one script, both games

Scripts are "mo_<move>_<context>" and go through tools/ref_compare.py's recorders, so they run in the same
places with the same setup. Recording skips scripts already recorded unless FORCE=1.

The scores, per script, on 60 fps frames from the start (both games see each press on the same frame:
ref_compare delays the port's keys by PCSX2's input lag):
  speed    RMS of the horizontal speed, cm/frame (a 3-frame average, the PS2's samples are taken in real time)
  vy       RMS of the vertical speed, cm/frame
  pos      RMS of the position along the path, cm, curves lined up at the first movement (ps2_score's method)
  yaw      RMS of the facing, degrees
  onset    frames between the press and the first movement, PS2 - port
  stop     frames until he is still again (or lands), PS2 - port
  air      frames off the ground, PS2 - port
  motions  the combat motions in order and the frame each starts, where they differ
"""
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ref_compare as rc

OUT = rc.OUT
MO = OUT / "momentum"

# ---------------------------------------------------------------------------------------------------------------
# The moves. (tag, setup events, action kind, button). Port order of styles: 0 TR, 1 SM, 2 GS, 3 RG; weapons 0-4
# melee (Rebellion, Cerberus, Agni & Rudra, Nevan, Beowulf), 5-9 guns (E&I, Shotgun, Artemis, Spiral, Kalina Ann).
# The PS2's states hold one weapon of each kind (build/refemu/sstates/style_<style>[_<melee>][_<gun>].p2s), so
# each move uses a state that has its weapon.
MELEE = ((0, "reb"), (1, "cer"), (2, "agn"), (3, "nev"), (4, "beo"))
GUNS = ((5, "ei"), (6, "sg"), (7, "ar"), (8, "sp"), (9, "ka"))
STATE_TAG = {1: "cerberus", 2: "agni", 3: "nevan", 4: "beowulf", 6: "shotgun", 7: "artemis", 8: "spiral", 9: "kalina"}


def _setup(style=0, melee=0, gun=5):
    ev = [(f"style{style}", 2, 3)]
    if melee:
        ev.append(("melee", melee, 0))
    if gun != 5:
        ev.append(("gun", gun, 0))
    return ev


MOVES = []
for _w, _t in MELEE:
    MOVES.append((f"{_t}1", _setup(0, melee=_w), "tap", "triangle"))
    MOVES.append((f"{_t}3", _setup(0, melee=_w), "mash", "triangle"))
for _g, _t in GUNS:
    MOVES.append((f"{_t}1", _setup(0, gun=_g), "tap", "square"))
    MOVES.append((f"{_t}h", _setup(0, gun=_g), "hold", "square"))
# Style moves: Trickster and Royalguard once; Swordmaster with each melee weapon, Gunslinger with each gun.
MOVES.append(("tr", _setup(0), "tap", "circle"))
MOVES.append(("trm", _setup(0), "mash", "circle"))
MOVES.append(("rg", _setup(3), "tap", "circle"))
MOVES.append(("rgh", _setup(3), "hold", "circle"))
for _w, _t in MELEE:
    MOVES.append((f"sm{_t}", _setup(1, melee=_w), "tap", "circle"))
    MOVES.append((f"sm{_t}m", _setup(1, melee=_w), "mash", "circle"))
for _g, _t in GUNS:
    MOVES.append((f"gs{_t}", _setup(2, gun=_g), "tap", "circle"))
    MOVES.append((f"gs{_t}m", _setup(2, gun=_g), "mash", "circle"))

# The situations: (tag, events before the press, press frame, stick during the move or None, length after).
#   st   standing                       run   running, stick held on      let   running, stick let go at the press
#   go   in the run's first frames      sj    standing jump, at the top   rj    running jump, stick held
#   rjl  running jump, stick let go     steer standing, stick sideways    land  on the landing of a standing jump
CONTEXTS = (
    ("st", [], 20, None, 150),
    ("run", [("lup", 6, 999)], 50, "keep", 160),
    ("let", [("lup", 6, 49)], 50, None, 150),
    ("go", [("lup", 6, 999)], 12, "keep", 160),
    ("sj", [("cross", 20, 22)], 44, None, 170),
    ("rj", [("lup", 6, 999), ("cross", 50, 52)], 74, "keep", 170),
    ("rjl", [("lup", 6, 50), ("cross", 50, 52)], 74, None, 170),
    ("steer", [], 20, "side", 150),
    ("land", [("cross", 20, 22)], 76, None, 150),
)


def action_events(kind, button, at):
    if kind == "tap":
        return [(button, at, at + 2)], 0
    if kind == "mash":
        return [(button, at + 8 * k, at + 8 * k + 2) for k in range(6)], 40
    return [(button, at, at + 60)], 0


def build():
    out = {}
    for mtag, setup, kind, button in MOVES:
        for ctag, before, at, stick, after in CONTEXTS:
            acts, extra = action_events(kind, button, at)
            frames = at + after + extra
            ev = list(setup)
            for b, a, c in before:
                ev.append((b, a, min(c, frames - 2)))
            if stick == "side":
                ev.append(("lright", at + 4, frames - 2))
            ev += acts
            out[f"mo_{mtag}_{ctag}"] = (frames, ev)
    # Locomotion on its own: taps of each length, stops, turns at each point of a run, jumps.
    for n in (2, 4, 6, 8, 10, 14, 20, 30):
        out[f"mo_tap{n}"] = (100 + n, [("lup", 20, 20 + n)])
    for n in (8, 14, 20, 40):
        out[f"mo_rev{n}"] = (200, [("lup", 20, 20 + n), ("ldown", 21 + n, 150)])
        out[f"mo_side{n}"] = (200, [("lup", 20, 20 + n), ("lright", 21 + n, 150)])
    out["mo_jumpdrift"] = (160, [("cross", 20, 22), ("lup", 30, 120)])
    out["mo_jumpdrift2"] = (160, [("cross", 20, 22), ("lup", 50, 120)])
    out["mo_rjrev"] = (200, [("lup", 6, 60), ("cross", 50, 52), ("ldown", 61, 150)])
    out["mo_rjside"] = (200, [("lup", 6, 60), ("cross", 50, 52), ("lright", 61, 150)])
    out["mo_rjland"] = (220, [("lup", 6, 200), ("cross", 50, 52)])
    out["mo_jumpheld"] = (160, [("cross", 20, 60)])
    return out


SCRIPTS = build()
rc.SCRIPTS.update(SCRIPTS)


def ps2_state(name):
    """The PS2 state a script needs, or None if that state hasn't been made."""
    ev = SCRIPTS[name][1]
    style = next((int(b[5:]) for b, a, c in ev if b.startswith("style")), 0)
    melee = next((int(a) for b, a, c in ev if b == "melee"), 0)
    gun = next((int(a) for b, a, c in ev if b == "gun"), 5)
    tag = ["tr", "sm", "gs", "rg"][style] + (f"_{STATE_TAG[melee]}" if melee else "") + (f"_{STATE_TAG[gun]}" if gun != 5 else "")
    path = rc.ROOT / "build" / "refemu" / "sstates" / f"style_{tag}.p2s"
    return path if path.exists() else None


def pick(args):
    if not args:
        return list(SCRIPTS)
    out = []
    for a in args:
        out += [n for n in SCRIPTS if n.startswith(a[:-1])] if a.endswith("*") else [a]
    return out


# ---------------------------------------------------------------------------------------------------------------
# Recording.
def ps2_up():
    import pcsx2_ref as r
    try:
        p = r.Pine()
        p.title()
        return True
    except OSError:
        return False


def ps2_restart():
    """Closes only our own PCSX2 (build/refemu), never anyone else's, and boots it again."""
    import pcsx2_ref as r
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    f"Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object {{ $_.Path -eq '{r.EXE}' }} | Stop-Process -Force"])
    time.sleep(3)
    r.start()
    time.sleep(32)


def record(side, names):
    force = os.environ.get("FORCE") == "1"
    log = MO / f"record_{side}.log"
    MO.mkdir(parents=True, exist_ok=True)
    for i, n in enumerate(names):
        path = OUT / f"cmp_{side}_{n}.json"
        if path.exists() and not force:
            continue
        if side == "ps2" and ps2_state(n) is None:
            with log.open("a") as f:
                f.write(f"{n} skipped: no PS2 state\n")
            continue
        for attempt in range(3):
            try:
                if side == "ps2":
                    if not ps2_up():
                        ps2_restart()
                    rc.record_ps2(n)
                else:
                    rc.record_port(n)
                break
            except Exception as e:   # PCSX2 drops PINE now and then after a state load: restart and retry
                with log.open("a") as f:
                    f.write(f"{n} attempt {attempt}: {e!r}\n")
                if side == "ps2":
                    ps2_restart()
        print(f"[{i + 1}/{len(names)}] {side} {n}", flush=True)


# ---------------------------------------------------------------------------------------------------------------
# Scoring.
def load(side, n):
    path = OUT / f"cmp_{side}_{n}.json"
    return json.loads(path.read_text()) if path.exists() else None


def series(rows):
    """Per frame from frame 5 (the port places him on frame 1): horizontal speed, vertical speed, yaw (degrees),
    height over the start, and the combat motion ("b:m", or "" outside a move)."""
    rows = rows[5:]
    n = len(rows)
    # On the ground the PS2's height dips 10 cm for a frame or two now and then: a 5-frame median takes that out
    # (a jump's curve is smooth enough to come through it unchanged).
    ys = [r["y"] for r in rows]
    med = [sorted(ys[max(0, i - 2):i + 3])[len(ys[max(0, i - 2):i + 3]) // 2] for i in range(n)]
    rows = [dict(r, y=med[i]) for i, r in enumerate(rows)]
    sp, vy = [0.0] * n, [0.0] * n
    for i in range(1, n - 1):
        a, b = rows[i - 1], rows[i + 1]
        d = math.hypot(b["x"] - a["x"], b["z"] - a["z"]) / 2
        sp[i] = d if d < 100 else 0.0
        vy[i] = (b["y"] - a["y"]) / 2 if abs(b["y"] - a["y"]) < 200 else 0.0
    # 3-frame average on top: the PS2's rows are real-time samples, a frame late or doubled now and then.
    def smooth(v):
        return [sum(v[max(0, i - 1):i + 2]) / len(v[max(0, i - 1):i + 2]) for i in range(len(v))]
    y0 = max(r["y"] for r in rows[:4])
    return {"speed": smooth(sp), "vy": smooth(vy), "yaw": [r["yaw"] for r in rows], "h": [r["y"] - y0 for r in rows],
            "m": [motion(r["m"]) for r in rows], "xz": [(r["x"], r["z"]) for r in rows]}


def motion(m):
    """The PS2's "bbmm/state" and the port's "b:m" or "-" to one form, "b:m" for combat banks (>= 3), else ""."""
    if "/" in m:
        v = int(m.split("/")[0], 16)
        b, k = v >> 8, v & 0xFF
        return f"{b}:{k}" if b >= 3 else ""
    return "" if m == "-" or ":" not in m else (m if int(m.split(":")[0]) >= 3 else "")


def yaw_deg(v):
    """Facing in degrees: the PS2 writes radians at +0x84, the port its own yaw (radians)."""
    return math.degrees(v)


def wrap(d):
    return (d + 180.0) % 360.0 - 180.0


def onset(sp, from_frame, thr=0.6):
    for i in range(from_frame, len(sp)):
        if sp[i] > thr:
            return i
    return None


def settle(s, from_frame, still=8):
    """The first frame after he has moved from the press that starts `still` frames of standing still (speed and
    height steady), or None if he never moves or never stops."""
    moving = lambda i: s["speed"][i] > 0.6 or abs(s["vy"][i]) > 0.6
    n = len(s["speed"])
    start = next((i for i in range(from_frame, n) if moving(i)), None)
    if start is None:
        return None
    i = start
    while i < n - still:
        if not any(moving(j) for j in range(i, i + still)):
            return i
        i += 1
    return None


def airtime(s, k):
    """Frames off the ground: runs of height over 3 cm that rise or fall faster than 2 cm a frame somewhere (a
    curb or a step up isn't a jump)."""
    total, run, fast = 0, 0, False
    for i in range(k + 1):
        if i < k and s["h"][i] > 3:
            run += 1
            fast = fast or abs(s["vy"][i]) > 2
        else:
            total += run if fast else 0
            run, fast = 0, False
    return total


def transitions(ms):
    out, prev = [], ""
    for i, m in enumerate(ms):
        if m and m != prev:
            out.append((m, i))
        prev = m
    return out


def score_one(n):
    a, b = load("ps2", n), load("psp", n)
    if not a or not b or len(a) < 20 or len(b) < 20:
        return None
    sa, sb = series(a), series(b)
    k = min(len(sa["speed"]), len(sb["speed"]))
    press = min((e[1] for e in SCRIPTS.get(n, rc.SCRIPTS.get(n, (0, [])))[1]
                 if e[0] in ("triangle", "square", "circle")), default=None)
    press = (press - 5) if press is not None else 0
    rms = lambda u, v: math.sqrt(sum((u[i] - v[i]) ** 2 for i in range(k)) / k)
    speed = rms(sa["speed"], sb["speed"])
    vy = rms(sa["vy"], sb["vy"])
    # Facing: how it turns from the first input on (the PS2's +0x84 takes a few frames to follow the placement),
    # each side from its own yaw then.
    first = min((e[1] for e in SCRIPTS.get(n, rc.SCRIPTS.get(n, (0, [])))[1]
                 if e[0] in rc.FLIP_STICK or e[0] in ("triangle", "square", "circle", "cross", "r1", "select")), default=20)
    y0i = max(0, first - 5 - 1)
    ya0, yb0 = yaw_deg(sa["yaw"][y0i]), yaw_deg(sb["yaw"][y0i])
    # Only over the move (or the first second of a locomotion test): with the stick held long, the PS2's zone
    # camera swings and "forward" swings with it, which is the camera's doing, not his.
    yr = range(y0i, min(k, y0i + 60))
    yaw =math.sqrt(sum(wrap(wrap(yaw_deg(sa["yaw"][i]) - ya0) - wrap(yaw_deg(sb["yaw"][i]) - yb0)) ** 2 for i in yr) / max(1, len(yr)))
    # The window scored: the move and 15 frames after it (the last combat motion on either game), or for a
    # locomotion test with a second stick direction, 45 frames past the change. After that he only runs on with
    # the stick, into street props that stand in different places on the two games, under a PS2 zone camera
    # that swings "forward" round as he goes: the camera's and the street's doing, not his momentum.
    ev = SCRIPTS.get(n, rc.SCRIPTS.get(n, (0, [])))[1]
    win = k
    last_m = max([i for i in range(k) if sa["m"][i] or sb["m"][i]], default=None)
    if press and last_m is not None and last_m > press:
        win = min(k, last_m + 15)
    sticks = sorted(e[1] for e in ev if e[0] in rc.FLIP_STICK)
    if not press and len(sticks) > 1:
        win = min(k, sticks[-1] - 5 + 45)
    k = max(win, 20)
    # Before the press both games had the same input: a run-up that differs a lot means a bad recording (a key
    # PCSX2 dropped, focus lost), not a move to fix. Those are flagged to record again.
    suspect = False
    if press > 10:
        def pre(rows):
            d, last, top = 0.0, rows[5], 0.0
            for r in rows[5:5 + press]:
                d += math.hypot(r["x"] - last["x"], r["z"] - last["z"]); last = r
                top = max(top, r["y"] - rows[5]["y"])
            return d, top
        (da, ha), (db, hb) = pre(a), pre(b)
        suspect = abs(da - db) > max(40.0, 0.15 * max(da, db)) or abs(ha - hb) > 60.0
    # Path: travel and height, from ps2_score / hd_score.
    import hd_score as hs
    ca = hs.curves([{"f": r["f"], "x": r["x"], "y": r["y"], "z": r["z"]} for r in a[4:5 + k]])
    cb = hs.curves([{"f": r["f"], "x": r["x"], "y": r["y"], "z": r["z"]} for r in b[4:5 + k]])
    c = hs.compare(ca, cb) or {"rms_travel": 0.0, "rms_height": 0.0}
    travel = (ca["travel"][-1] - ca["travel"][0], cb["travel"][-1] - cb["travel"][0])
    peak = (max(ca["height"]), max(cb["height"]))
    # The move's own window: from the press.
    oa, ob = onset(sa["speed"], press), onset(sb["speed"], press)
    ea, eb = settle(sa, press), settle(sb, press)
    air = (airtime(sa, k), airtime(sb, k))
    # Speed through the move only (from the press to the later of the two stops): what a press feels like.
    end = max(ea or press, eb or press) + 5
    # Lined up on the first movement after the press: the onset term scores the delay, these the shape (else a
    # 2-frame later start would count twice, and PCSX2's keyboard path is late by 2 instead of 1 now and then).
    sh = (ob - oa) if oa is not None and ob is not None and abs(ob - oa) <= 6 else 0
    seg = [i for i in range(press, min(end, k)) if 0 <= i + sh < k]
    seg_speed = math.sqrt(sum((sa["speed"][i] - sb["speed"][i + sh]) ** 2 for i in seg) / max(1, len(seg)))
    # Vertical speed lined up on its own start (a take-off), which can come before the press (an air move).
    va = next((i for i in range(len(sa["vy"])) if abs(sa["vy"][i]) > 1.0), None)
    vb = next((i for i in range(len(sb["vy"])) if abs(sb["vy"][i]) > 1.0), None)
    shv = (vb - va) if va is not None and vb is not None and abs(vb - va) <= 6 else 0
    segv = [i for i in range(min(press, va if va is not None else press), min(end, k)) if 0 <= i + shv < k]
    seg_vy = math.sqrt(sum((sa["vy"][i] - sb["vy"][i + shv]) ** 2 for i in segv) / max(1, len(segv)))
    takeoff = (va - vb) if va is not None and vb is not None else None
    peak_sp = (max(sa["speed"][press:k], default=0), max(sb["speed"][press:k], default=0))
    # Combat motions.
    # Combat motions. The PS2 also reports its stance motions (each bank's 0, and the gun stance 8:4), which the
    # port logs as "-": those are left out. Motions both games play are compared by when each first starts.
    ta = [t for t in transitions(sa["m"][:k]) if not t[0].endswith(":0") and t[0] != "8:4"]
    tb = [t for t in transitions(sb["m"][:k]) if not t[0].endswith(":0")]
    seq_ok = [m for m, _ in ta] == [m for m, _ in tb]
    # Timing over the motions both play in the same order from the start (up to the first that differs).
    shifts = []
    for (ma, fa), (mb, fb) in zip(ta, tb):
        if ma != mb:
            break
        shifts.append(fb - fa)
    d = lambda u, v: None if u is None or v is None else u - v
    r = {
        "speed": round(speed, 2), "vy": round(vy, 2), "move_speed": round(seg_speed, 2), "move_vy": round(seg_vy, 2),
        "pos": round(c["rms_travel"], 1), "height": round(c["rms_height"], 1), "yaw": round(yaw, 1),
        "travel": [round(travel[0]), round(travel[1])], "peak": [round(peak[0]), round(peak[1])],
        "peak_speed": [round(peak_sp[0], 2), round(peak_sp[1], 2)],
        "onset": d(oa, ob), "stop": d(ea, eb), "air": [air[0], air[1]], "takeoff": takeoff,
        "motions_ps2": [f"{m}@{f}" for m, f in ta][:12], "motions_psp": [f"{m}@{f}" for m, f in tb][:12],
        "motion_order": seq_ok, "motion_shift": max(shifts, key=abs) if shifts else 0,
    }
    # Badness: each term scaled to "1 = clearly felt".
    terms = {
        "speed": seg_speed / 1.5, "vy": seg_vy / 1.5, "pos": c["rms_travel"] / 15, "height": c["rms_height"] / 15,
        "yaw": yaw / 25, "travel": abs(travel[0] - travel[1]) / max(20, 0.1 * max(travel)),
        "peak": abs(peak[0] - peak[1]) / max(15, 0.1 * max(peak)),
        "onset": abs(r["onset"] or 0) / 3, "stop": abs(r["stop"] or 0) / 6, "air": abs(air[0] - air[1]) / 6,
        "motion_shift": abs(r["motion_shift"]) / 4,
    }
    r["suspect"] = suspect
    r["worst"] = max(terms, key=terms.get)
    r["bad"] = round(max(terms.values()), 2)
    r["ok"] = r["bad"] <= 1.0
    return r


def score(names):
    MO.mkdir(parents=True, exist_ok=True)
    results = {}
    for n in names:
        s = score_one(n)
        if s:
            results[n] = s
    (MO / "score.json").write_text(json.dumps(results, indent=1))
    report(results)
    ok = sum(1 for v in results.values() if v["ok"])
    print(f"{ok} of {len(results)} within feel limits; worst first in {MO / 'report.md'}")
    return results


def report(results):
    rows = sorted(results.items(), key=lambda kv: -kv[1]["bad"])
    lines = ["# Momentum check", "", f"{sum(v['ok'] for v in results.values())} of {len(results)} scripts within limits.", "",
             "Badness 1 = clearly felt: move speed 1.5 cm/frame RMS, vertical 1.5, path 15 cm, facing 20 deg, "
             "start 3 frames, stop 6, airtime 6, motion timing 4, travel/peak 10%.", "",
             "| script | bad | worst | move speed | move vy | path | yaw | travel ps2/port | peak | onset | stop | air | motions |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for n, v in rows:
        lines.append(f"| {n} | {v['bad']} | {v['worst']} | {v['move_speed']} | {v['move_vy']} | {v['pos']} | {v['yaw']} | "
                     f"{v['travel'][0]}/{v['travel'][1]} | {v['peak'][0]}/{v['peak'][1]} | {v['onset']} | {v['stop']} | "
                     f"{v['air'][0]}/{v['air'][1]} | {'ok' if v['motion_order'] else 'differ'} {v['motion_shift']} |")
    (MO / "report.md").write_text("\n".join(lines) + "\n")


def show(n, step=3):
    sa, sb = series(load("ps2", n)), series(load("psp", n))
    print(f"{n}: frame | PS2 speed  vy  height  motion | port speed  vy  height  motion")
    for i in range(0, min(len(sa["speed"]), len(sb["speed"])), step):
        print(f"{i + 5:4d} | {sa['speed'][i]:6.2f} {sa['vy'][i]:6.2f} {sa['h'][i]:7.1f} {sa['m'][i]:>6} |"
              f" {sb['speed'][i]:6.2f} {sb['vy'][i]:6.2f} {sb['h'][i]:7.1f} {sb['m'][i]:>6}")
    print(json.dumps(score_one(n), indent=1))


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "list":
        for n in pick(args):
            print(n, SCRIPTS[n][0], "" if ps2_state(n) else "(no PS2 state)")
        print(len(pick(args)), "scripts")
    elif cmd in ("ps2", "psp"):
        record(cmd, pick(args))
    elif cmd == "all":
        # The whole run: the PS2 first (our own PCSX2, closed after), then the port, then the scores.
        names = pick(args)
        record("ps2", names)
        import pcsx2_ref as r
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object {{ $_.Path -eq '{r.EXE}' }} | Stop-Process -Force"])
        record("psp", names)
        score([n for n in names if (OUT / f"cmp_ps2_{n}.json").exists()])
    elif cmd == "score":
        names = pick(args) if args else [n for n in rc.SCRIPTS if (OUT / f"cmp_ps2_{n}.json").exists()]
        score(names)
    elif cmd == "show":
        show(args[0], int(args[1]) if len(args) > 1 else 3)
