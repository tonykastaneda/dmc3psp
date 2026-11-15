"""PS2 (harness PCSX2): the real game's camera in a room, for docs/CAMERA.md and psp/testroom/camera.c.
python tools/ps2_camera.py MISSION [--door FROM_ROOM_TEXT_TO NEW_ROOM X Z] [--spot X Z [FACE]]... [--tag NAME]

Boots fresh, loads the 100% save (card 2), starts the mission on Normal (as tools/ps2_vergil.py) and skips the
opening cutscene. --door patches a door record in RAM ("NextRoom<TAB><TAB>114" -> the new room, start 0) and walks
Dante into its box at (X, Z) with O. Then for each --spot it holds Dante there (enemies parked far away) for 3 s and
prints his feet, the camera's eye and target and the vertical FOV (CCameraCtrl at 0xEC6EF0: +0x70 eye, +0x80
target, +0x20 FOV), with a screenshot analysis/ref/cam/<tag>_<n>.png."""
import argparse
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pcsx2_ref as r

CTRL = 0xEC6EF0          # where it was on the M2 street; found again by its vtable
CTRL_VTABLE = 0x604450
DEFAULT_BUTTONS = [0x1000, 0x4000, 0x2000, 0x8000, 0x10, 0x40, 0x20, 0x80, 4, 1, 0x200, 8, 2, 0x400, 0x100]
DANTE_VTABLE = 0x602650
OUT = Path(__file__).resolve().parent.parent / "analysis" / "ref" / "cam"


STEP = [0]


def press(k, wait, hold=0.12):
    r.focus(); r.press(k, hold); time.sleep(wait)
    STEP[0] += 1
    try:   # every menu step, to see where a run went wrong
        r.shot(str(OUT / f"menu_{STEP[0]:02d}_{k}.png"))
    except Exception:
        pass


def find_words(p, words, lo=0x1000000, hi=0x2000000):
    pat = b"".join(struct.pack("<I", w) for w in words)
    hits, step = [], 0x40000
    for a in range(lo, hi, step):
        blob = p.read(a, step + len(pat))
        i = blob.find(pat)
        while i >= 0:
            if i % 4 == 0:
                hits.append(a + i)
            i = blob.find(pat, i + 4)
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mission", type=int)
    ap.add_argument("--door", nargs=4, metavar=("OLD", "NEW", "X", "Z"))
    ap.add_argument("--spot", nargs="+", action="append", default=[])
    ap.add_argument("--tag", default="cam")
    ap.add_argument("--hold", type=float, default=3.0)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    # Never stop someone else's emulator: refuse if one is running (the caller holds orchestration/locks/pcsx2.lock).
    # Mission 0: use the game already running (your own instance, in the room). Otherwise boot and go there; never
    # stop someone else's emulator (the caller holds orchestration/locks/pcsx2.lock).
    if args.mission:
        if b"pcsx2-qt" in subprocess.run(["tasklist"], capture_output=True).stdout:
            sys.exit("PCSX2 is already running; close your own instance first")
        r.start(); time.sleep(28)
        press("cross", 6)
        for _ in range(3):
            press("start", 5)
        # Load: the game first reports slot 1 empty; then slot 2. The d-pad doesn't reach it here: the stick does.
        press("cross", 8); press("cross", 2); press("ldown", 1.5, 0.2); press("cross", 4)
        press("lleft", 1.0, 0.2); press("cross", 2); press("lleft", 1.0, 0.2)     # save 1, "OK to load?" -> Yes
        press("cross", 6); press("cross", 6)                          # load, then Mission Select
        for _ in range(4):
            press("lup", 0.4, 0.15)
        for _ in range(20 - args.mission):
            press("lleft", 0.35, 0.12)
        press("cross", 24)                                            # Normal: the opening demo (loads ~20 s)
        press("start", 4)                                             # skip it: the mission card (on Customize)
        press("lleft", 1.5, 0.2); press("cross", 20)                  # Mission Start
    p = r.Pine()
    dante, t_end = 0, time.time() + 90
    while not dante and time.time() < t_end:
        hits = find_words(p, [DANTE_VTABLE])
        dante = hits[0] if hits else 0
        if not dante:
            time.sleep(3)
    print("Dante object", hex(dante))
    global CTRL
    ctrls = find_words(p, [CTRL_VTABLE], 0x800000, 0x2000000)
    if ctrls:
        CTRL = ctrls[0]
    print("CCameraCtrl", [hex(a) for a in ctrls])
    for i, v in enumerate(DEFAULT_BUTTONS):
        p.write16(0x6C37DA + 2 * i, v)
    import ps2_enemies

    def report(tag):
        pos = struct.unpack("<3f", p.read(dante + 0x50, 12))
        eye = struct.unpack("<3f", p.read(CTRL + 0x70, 12))
        tgt = struct.unpack("<3f", p.read(CTRL + 0x80, 12))
        fov = p.f32(CTRL + 0x20)
        print(f"{tag}: room {p.u32(0x1D0D738)} feet {pos[0]:.1f} {pos[1]:.1f} {pos[2]:.1f} eye {eye[0]:.1f} {eye[1]:.1f} "
              f"{eye[2]:.1f} target {tgt[0]:.1f} {tgt[1]:.1f} {tgt[2]:.1f} fov {fov * 57.29578:.2f}")
        try:
            r.shot(str(OUT / f"{args.tag}_{tag}.png"))
        except Exception as e:  # a minimised window can't be captured
            print("no shot:", e)

    time.sleep(3)
    report("start")
    if args.door:
        old, new, x, z = args.door
        key = f"NextRoom\t\t{old}\r\nNextPosId\t\t".encode()
        for a in range(0x100000, 0x2000000, 0x40000):
            blob = p.read(a, 0x40000 + 64)
            i = blob.find(key)
            if i >= 0:
                for k, ch in enumerate(new.encode()):
                    p.write8(a + i + 10 + k, ch)
                p.write8(a + i + len(key), ord("0"))
                print("door patched at", hex(a + i), p.read(a + i, 30))
                break
        for _ in range(6):
            p.writef(dante + 0x50, float(x)); p.writef(dante + 0x58, float(z))
            time.sleep(0.2)
        press("circle", 3); press("circle", 3)
        time.sleep(10)
        press("start", 3)
        report("door")
    parked = [a for a, _ in ps2_enemies.scan(p)]
    for n, spot in enumerate(args.spot):
        x, z = float(spot[0]), float(spot[1])
        r.focus()   # PCSX2 pauses without focus
        t_end = time.perf_counter() + args.hold
        while time.perf_counter() < t_end:
            for i, a in enumerate(parked):
                for k, v in enumerate((-30000.0 - 1000.0 * i, 5000.0, -30000.0)):
                    p.writef(a + 0x50 + 4 * k, v)
            p.writef(dante + 0x50, x); p.writef(dante + 0x58, z)
            if len(spot) > 2:
                p.write16(dante + 0x90, int(round(float(spot[2]) / 360.0 * 65536.0)) & 0xFFFF)
            p.writef(dante + 0x2AD4, 20000.0)
            time.sleep(0.02)
        report(str(n))


if __name__ == "__main__":
    main()
