"""Runs one input script on the real game (PCSX2) and on the port (PPSSPP), side by side.

    python tools/ref_compare.py ps2 run         # record the real game, needs a loaded state
    python tools/ref_compare.py psp run         # record the port
    python tools/ref_compare.py show run        # distance / height per frame, both

Scripts are "button first last" lines, in 60 fps frames from the start.
"""
import json
import math
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "ref"
GAME_DIR = Path(r"E:\SYSTEMS\PPSSPP\memstick\PSP\GAME\DMC3TESTROOM")
PLAYER = 0x17E0A40
CAMERA_EYE, CAMERA_TARGET = 0xEC6F60, 0xEC6F70
# Both games start at the test spot (2750, 2450). The PS2's street camera there looks along CAMERA_YAW (degrees
# from +z toward +x; it is a zone camera that writes can't move); the port's is put the same way. The open street,
# about 18 m clear, is the other way, so Dante faces FACE_YAW and every script's stick is turned round
# (FLIP_STICK: up <-> down, left <-> right) on both games: "forward" in a script runs into the open street.
CAMERA_YAW = 122
FACE_YAW = -58
FLIP_STICK = {"lup": "ldown", "ldown": "lup", "lleft": "lright", "lright": "lleft"}


def flipped(events):
    return [(FLIP_STICK.get(b, b), a, c) for b, a, c in events]


# The test dummy, unless a script says otherwise: 15 m ahead, inside the port's 25 m lock-on and the PS2's, and
# clear of the longest moves. Both games get one there.
DUMMY_DISTANCE = 1500


def with_dummy(events, distance=None):
    return events if any(b == "enemy" for b, a, c in events) else events + [("enemy", distance or DUMMY_DISTANCE, 0)]


# Where a script runs. "open": the open street above, camera in front of Dante, stick turned round. "behind",
# for anything with R1: with lock-on the PS2 reads the stick against the camera (FUN_00254180) and the port
# against Dante's facing, the same only with the camera behind him, so these run at (1800, 3000), where the
# PS2's zone camera settles at -60 deg, about 7 m clear ahead; the dummy 6 m ahead.
SETUPS = {
    "open": dict(x=2750.0, z=2450.0, face=FACE_YAW, cam=CAMERA_YAW, flip=True, dummy=DUMMY_DISTANCE, settle=0.6),
    "behind": dict(x=1800.0, z=3000.0, face=-60, cam=-60, flip=False, dummy=600, settle=2.5),
}


def setup_for(events):
    return SETUPS["behind" if any(b == "r1" for b, a, c in events) else "open"]


def prepared(events):
    s = setup_for(events)
    return with_dummy(flipped(events) if s["flip"] else list(events), s["dummy"]), s

SCRIPTS = {
    "lod_auto": (640, [("enemy", 300, 0), ("menu_start", 10, 10), ("menu_down", 13, 13), ("menu_right", 16, 16), ("menu_right", 18, 18), ("menu_right", 20, 20), ("menu_right", 22, 22), ("menu_right", 24, 24), ("menu_right", 26, 26), ("menu_right", 28, 28), ("menu_start", 33, 33), ("shot", 300, 0), ("shot", 303, 0), ("shot", 306, 0)]),
    "lod_full": (640, [("enemy", 300, 0), ("menu_start", 10, 10), ("menu_down", 13, 13), ("menu_right", 16, 16), ("menu_right", 18, 18), ("menu_right", 20, 20), ("menu_right", 22, 22), ("menu_right", 24, 24), ("menu_right", 26, 26), ("menu_right", 28, 28), ("menu_down", 33, 33), ("menu_down", 35, 35), ("menu_cross", 37, 37), ("menu_start", 40, 40), ("shot", 300, 0)]),
    "dummies": (900, [("enemy", 300, 0), ("menu_start", 10, 10), ("menu_down", 13, 13), ("menu_right", 16, 16), ("menu_right", 19, 19),
                      ("menu_right", 22, 22), ("menu_down", 25, 25), ("menu_cross", 28, 28), ("shot", 30, 0), ("menu_start", 33, 33),
                      ("shot", 120, 0), ("shot", 300, 0), ("shot", 500, 0), ("shot", 700, 0), ("shot", 880, 0)]),
    "menu2": (120, [("menu_start", 5, 5), ("menu_cross", 8, 8), ("menu_start", 11, 11), ("cross", 20, 22)]),
    "menu": (240, [("menu_start", 20, 20), ("lup", 22, 38), ("menu_cross", 30, 30), ("shot", 35, 0), ("menu_start", 45, 45),
                   ("lup", 60, 200), ("shot", 120, 0), ("menu_start", 150, 150), ("menu_down", 155, 155), ("shot", 160, 0),
                   ("menu_start", 165, 165)]),
    "a_wall": (340, [("lup", 6, 330), ("cross", 180, 186), ("circle", 215, 221)]),
    "a_trick": (170, [("r1", 6, 160), ("lup", 20, 60), ("circle", 40, 46), ("enemy", 900, 0)]),
    "a_jump": (110, [("cross", 6, 12)]),
    "a_jumpheld": (130, [("cross", 6, 72)]),
    "a_hike": (140, [("cross", 6, 12), ("cross", 41, 47)]),
    "a_hikeheld": (150, [("cross", 6, 12), ("cross", 41, 90)]),
    "a_hikestar": (170, [("cross", 6, 12), ("cross", 41, 47), ("circle", 70, 76)]),
    "a_starhike": (170, [("cross", 6, 12), ("circle", 41, 47), ("cross", 70, 76)]),
    "a_star": (110, [("cross", 6, 12), ("circle", 41, 47)]),
    "qs": (160, [("style4", 5, 6), ("r1", 18, 26), ("circle", 20, 22), ("shot", 30, 0), ("shot", 70, 0), ("shot", 110, 0)]),
    "dg": (300, [("style5", 5, 6), ("r1", 18, 26), ("circle", 20, 22), ("ldown", 180, 250), ("shot", 170, 0), ("shot", 205, 0), ("shot", 235, 0), ("shot", 260, 0)]),
    "taunt": (200, [("select", 20, 22)]),
    "devil": (130, [("devil", 20, 21), ("shot", 22, 0), ("shot", 40, 0), ("ldown", 45, 75), ("triangle", 85, 87), ("shot", 72, 0), ("shot", 96, 0)]),
    # Trickster: dash, Sky Star (the air dash). Royalguard: block.
    "fxdash": (120, [("style0", 2, 3), ("lup", 10, 100), ("circle", 40, 42),
                     ("shot", 45, 0), ("shot", 52, 0), ("shot", 60, 0), ("shot", 70, 0)]),
    "fxsky": (170, [("style0", 2, 3), ("cross", 20, 22), ("lup", 40, 110), ("circle", 50, 52),
                    ("shot", 54, 0), ("shot", 60, 0), ("shot", 66, 0), ("shot", 76, 0), ("shot", 90, 0)]),
    "fxguard": (120, [("style3", 2, 3), ("circle", 30, 80), ("shot", 34, 0), ("shot", 45, 0), ("shot", 70, 0)]),
    "fxland": (120, [("cross", 10, 12), ("shot", 52, 0), ("shot", 56, 0), ("shot", 62, 0)]),
    # Looks at the effects: guns on a close dummy.
    "fxgun": (150, [("enemy", 420, 0), ("r1", 5, 140), ("square", 40, 42), ("square", 60, 62), ("square", 80, 82),
                    ("shot", 41, 0), ("shot", 43, 0), ("shot", 62, 0), ("shot", 82, 0), ("shot", 100, 0)]),
    "fxmelee": (150, [("enemy", 240, 0), ("triangle", 20, 22), ("triangle", 45, 47), ("triangle", 70, 72),
                      ("shot", 28, 0), ("shot", 34, 0), ("shot", 52, 0), ("shot", 84, 0)]),
    # A weapon's own motions in its first attacks.
    "wcer": (130, [("melee", 1, 0), ("enemy", 3000, 0), ("triangle", 20, 22), ("triangle", 45, 47), ("shot", 30, 0), ("shot", 40, 0), ("shot", 60, 0)]),
    "wagn": (130, [("melee", 2, 0), ("enemy", 3000, 0), ("triangle", 20, 22), ("triangle", 45, 47), ("shot", 30, 0), ("shot", 40, 0), ("shot", 60, 0)]),
    "wnev": (130, [("melee", 3, 0), ("enemy", 3000, 0), ("triangle", 20, 22), ("triangle", 45, 47), ("shot", 30, 0), ("shot", 40, 0), ("shot", 60, 0)]),
    "wart": (130, [("gun", 7, 0), ("enemy", 3000, 0), ("square", 20, 22), ("shot", 10, 0), ("shot", 30, 0), ("shot", 60, 0)]),
    # Run from a stop, let go.
    "run": (240, [("lup", 20, 140)]),
    # Run, reverse the stick.
    "turn": (260, [("lup", 20, 120), ("ldown", 121, 200)]),
    # Standing jump.
    "jump": (160, [("cross", 20, 22)]),
    # Running jump, stick kept held.
    "runjump": (240, [("lup", 10, 200), ("cross", 70, 72)]),
    # Double jump (Air Hike with the right loadout).
    "jump2": (220, [("cross", 20, 22), ("cross", 50, 52)]),
    # Air Hike: Trickster's double jump.
    "hike": (240, [("style0", 2, 3), ("cross", 20, 22), ("cross", 50, 52)]),
    # Trickster dash, stick held.
    "dash": (200, [("style0", 2, 3), ("lup", 20, 120), ("circle", 50, 52)]),
    # Dash from a stand.
    "dash0": (160, [("style0", 2, 3), ("circle", 30, 32)]),
    # Lock-on flips: forward, left, right, back from a stand.
    "flipl": (200, [("r1", 10, 120), ("lleft", 40, 70), ("cross", 45, 47)]),
    "flipr": (200, [("r1", 10, 120), ("lright", 40, 70), ("cross", 45, 47)]),
    "flipb": (200, [("r1", 10, 120), ("ldown", 40, 70), ("cross", 45, 47)]),
    "flipf": (200, [("r1", 10, 120), ("lup", 40, 70), ("cross", 45, 47)]),
    # Steering in the air: run jump, stick reversed at the top.
    "airturn": (220, [("lup", 10, 70), ("cross", 40, 42), ("ldown", 71, 110)]),
    # Stick only after take-off from a stand.
    "airdrift": (180, [("cross", 20, 22), ("lup", 35, 60)]),
    # Lock-on strafe.
    "strafe": (240, [("r1", 10, 200), ("lright", 30, 150)]),
    # Roll: R1 + direction + cross.
    "roll": (200, [("r1", 10, 120), ("lright", 30, 70), ("cross", 40, 42)]),
    # First melee hits.
    "melee": (200, [("triangle", 20, 22), ("triangle", 45, 47), ("triangle", 70, 72)]),
}


def _from_hd(name, style=None):
    """One of tools/hd_trace.py's key scripts (seconds, Space = R1) as frames at 60, for the PS2 and the port."""
    import hd_trace
    seconds, keys = hd_trace.SCRIPTS[name]
    events = [({"space": "r1"}.get(k, k), int(a * 60) + 6, int(b * 60) + 6) for k, a, b in keys if k not in ("tp", "wall")]
    if style is not None:
        events.insert(0, (f"style{style}", 2, 3))
    return (int(seconds * 60) + 20, events)


# Rebellion's attacks and Trickster's dashes (style 0 = Trickster in the port's order), from the HD scripts.
for _n in ("r_1", "r_2", "r_3", "r_4", "r_gap", "r_gapl", "r_st", "r_ht", "r_hth", "r_helm", "r_ms", "r_rise"):
    SCRIPTS.setdefault(_n, _from_hd(_n))
for _n in ("w_u", "w_d", "w_l", "w_r", "w_au", "w_aa", "d_n", "d_u", "d_d", "d_l", "d_r", "t_air", "t_airb", "dash3"):
    SCRIPTS.setdefault(_n, _from_hd(_n, style=0))
# The circle button's style moves (the HD's s_* scripts) in Swordmaster (1), Gunslinger (2) and Royalguard (3).
#: The other melee weapons (states style_tr_<weapon>.p2s): the HD's triangle scripts with "melee n".
MELEE_MOVES = ("c_1", "c_hold", "c_air", "c_airm", "c_b", "c_f", "c_mash", "r_gap", "r_4")
for _w, _tag in ((1, "cer"), (2, "agn"), (3, "nev"), (4, "beo")):
    for _n in MELEE_MOVES:
        _f, _e = _from_hd(_n, style=0)
        SCRIPTS.setdefault(f"{_tag}_{_n.replace('_', '')}", (_f, _e + [("melee", _w, 0)]))
STYLE_MOVES = ("s_1", "s_2", "s_3", "s_mash", "s_hold", "s_air", "s_airm", "s_lf", "s_lb", "s_lfm", "s_lbm", "s_lh", "s_airs")
for _style, _tag in ((1, "sm"), (2, "gs"), (3, "rg")):
    for _n in STYLE_MOVES:
        SCRIPTS.setdefault(f"{_tag}_{_n[2:]}", _from_hd(_n, style=_style))


PS2_INPUT_LAG = 2


def write_port_script(name):
    frames, events = SCRIPTS[name]
    # Option lines: ("shot", frame, 0), ("enemy", cm, 0), ("melee", n, 0), ("gun", n, 0).
    events, s = prepared(events)
    # The PS2's keys arrive PS2_INPUT_LAG frames after they're pressed (PCSX2's keyboard path; measured from the
    # first movement of single presses): the port gets them that much later too, so both games see each press
    # at the same point of a move.
    options = ("shot", "enemy", "melee", "gun", "camyaw", "turbo", "fps")
    events = [(b, a, c) if b in options or b.startswith("style") else (b, a + PS2_INPUT_LAG, c + PS2_INPUT_LAG)
              for b, a, c in events]
    text = f"frames {frames}\nspot {int(s['x'])} {int(s['z'])}\nfaceyaw {s['face']}\ncamyaw {s['cam']}\ncamlock 1\n" +"".join(f"{b} {a}\n" if b in ("shot", "enemy", "melee", "gun", "camyaw", "turbo", "fps") else f"{b} {a} {c}\n" for b, a, c in events)
    (GAME_DIR / "CMP_SCRIPT.TXT").write_text(text, encoding="ascii")


def record_port(name):
    write_port_script(name)
    out = GAME_DIR / "CMP_OUT.TXT"
    out.unlink(missing_ok=True)
    subprocess.run(["powershell", "-NoProfile", "-File", str(ROOT / "tools" / "test_focus.ps1"), "-Name", "cmp", "-TimeoutSeconds", "60"], check=True)
    rows = []
    for line in out.read_text().splitlines():
        c = line.split()
        rows.append({"f": int(c[0]), "x": float(c[1]), "y": float(c[2]), "z": float(c[3]), "yaw": float(c[4]), "m": c[5]})
    (OUT / f"cmp_psp_{name}.json").write_text(json.dumps(rows))
    print("port", name, len(rows))


def record_ps2(name, state_key=0x72):
    import pcsx2_ref as r
    frames, events = SCRIPTS[name]
    # The style's own state in slot 1 (build/hdref/make_style_state.py: chosen in Customize, which writing the style
    # byte doesn't do); scripts without a style use Trickster's. Port order: 0 TR, 1 SM, 2 GS, 3 RG.
    style = next((int(b[5:]) for b, a, c in events if b.startswith("style")), 0)
    import glob
    import shutil
    weapon = next((int(a) for b, a, c in events if b == "melee"), 0)
    tag = ['tr', 'sm', 'gs', 'rg'][style] + ("" if weapon == 0 else "_" + ["", "cerberus", "agni", "nevan", "beowulf"][weapon])
    source = ROOT / "build" / "refemu" / "sstates" / f"style_{tag}.p2s"
    slot1 = glob.glob(str(ROOT / "build" / "refemu" / "sstates" / "SLUS-21361 (*).01.p2s"))
    if source.exists() and slot1:
        shutil.copy(source, slot1[0])
    p = r.Pine()
    r.focus()
    time.sleep(0.2)
    r.key(state_key, True)
    time.sleep(0.05)
    r.key(state_key, False)
    events, s = prepared(events)
    time.sleep(2.5)   # writes too soon after a state load have crashed PCSX2
    for button in r.KEYS:
        r.release(button)
    # The street's enemies first, held 50 m up out of reach for the whole run (tools/ps2_enemies.py finds
    # them by their class vtables), so nothing hits or pushes him while he's placed or during the test.
    import ps2_enemies
    parked = [a for a, _ in ps2_enemies.scan(p) if abs(struct.unpack("<f", p.read(a + 0x50, 4))[0]) > 100.0]
    # One enemy stands where the port puts its test dummy (the script's "enemy" distance, else 3000, ahead along
    # FACE_YAW from the test spot) and is held there, so lock-on and Air Trick have the same target on both. The
    # rest go far off sideways as well as up: Enemy Step's reach and lock-on ignore height (a jump press next to
    # an enemy 50 m overhead became an Enemy Step, state 33).
    dist = next((float(a) for b, a, c in events if b == "enemy"), float(DUMMY_DISTANCE))
    dummy = (s["x"] + math.sin(math.radians(s["face"])) * dist, -3.0, s["z"] + math.cos(math.radians(s["face"])) * dist)
    def park():
        for i, a in enumerate(parked):
            spot = dummy if i == 0 else (-30000.0 - 1000.0 * i, 5000.0, -30000.0)
            for k3, v in enumerate(spot):
                p.write32(a + 0x50 + 4 * k3, struct.unpack("<I", struct.pack("<f", v))[0])
    park()
    # Style: player+0x3c6c, in the game's order (0 Swordmaster, 1 Gunslinger, 2 Trickster, 3 Royalguard).
    # The port's order is Trickster, Swordmaster, Gunslinger, Royalguard.
    game_style = {0: 2, 1: 0, 2: 1, 3: 3}
    for b, a, c in events:
        if b.startswith("style"):
            p.write8(PLAYER + 0x3C6C, game_style[int(b[5:])])
    # The setup's spot and facing, held while the zone camera settles.
    t_end = time.perf_counter() + s["settle"]
    while time.perf_counter() < t_end:
        park()
        p.writef(PLAYER + 0x50, s["x"])
        p.writef(PLAYER + 0x54, 30.0)
        p.writef(PLAYER + 0x58, s["z"])
        # The facing is the u16 angle at +0x90 (FUN_00225c90; 0 = +z, toward +x as in the port); +0x84 follows it.
        p.write16(PLAYER + 0x90, int(round(s["face"] / 360.0 * 65536.0)) & 0xFFFF)
        time.sleep(0.03)
    keys = [(b, a, c) for b, a, c in events if not b.startswith("style") and b not in ("shot", "enemy", "melee", "gun")]
    pending = sorted([(a, True, b) for b, a, c in keys] + [(c + 1, False, b) for b, a, c in keys], key=lambda e: e[0])
    t0 = time.perf_counter()
    samples = []
    i = 0
    while True:
        now = (time.perf_counter() - t0) * 59.94
        while i < len(pending) and pending[i][0] <= now:
            (r.hold if pending[i][1] else r.release)(pending[i][2])
            i += 1
        d = p.read(PLAYER + 0x50, 12)
        x, y, z = struct.unpack("<3f", d)
        samples.append((now, x, y, z, p.f32(PLAYER + 0x84), p.u16(PLAYER + 0x24C6), p.u16(PLAYER + 0x2848)))
        p.writef(PLAYER + 0x2AD4, 5300.0)
        park()
        if now >= frames:
            break
    rows, s = [], 0
    for f in range(frames):
        while s + 1 < len(samples) and samples[s + 1][0] <= f:
            s += 1
        a = samples[s]
        rows.append({"f": f, "x": a[1], "y": a[2], "z": a[3], "yaw": a[4], "m": f"{a[5]:04x}/{a[6]}"})
    (OUT / f"cmp_ps2_{name}.json").write_text(json.dumps(rows))
    print("ps2", name, len(rows), "samples", len(samples))


def show(name, step=6):
    a = json.loads((OUT / f"cmp_ps2_{name}.json").read_text())
    b = json.loads((OUT / f"cmp_psp_{name}.json").read_text())
    def table(rows):
        d, last = 0.0, rows[5]   # the port places Dante on frame 1
        out = [(0.0, 0.0, r["m"]) for r in rows[:5]]
        y0 = rows[5]["y"]
        for r in rows[5:]:
            d += math.hypot(r["x"] - last["x"], r["z"] - last["z"])
            last = r
            out.append((d, r["y"] - y0, r["m"]))
        return out
    ta, tb = table(a), table(b)
    print(f"{name}: frame | PS2 dist  height  state | port dist  height  move")
    for f in range(0, min(len(ta), len(tb)), step):
        print(f"{f:4d} | {ta[f][0]:8.1f} {ta[f][1]:7.1f} {ta[f][2]:>10} | {tb[f][0]:8.1f} {tb[f][1]:7.1f} {tb[f][2]}")
    print(f"end  | {ta[-1][0]:8.1f} | {tb[-1][0]:8.1f}   peak height {max(x[1] for x in ta):.1f} vs {max(x[1] for x in tb):.1f}")


if __name__ == "__main__":
    what, name = sys.argv[1], sys.argv[2]
    if what == "ps2":
        record_ps2(name)
    elif what == "psp":
        record_port(name)
    else:
        show(name, int(sys.argv[3]) if len(sys.argv) > 3 else 6)
