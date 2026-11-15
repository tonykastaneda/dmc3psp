#!/usr/bin/env python3
"""Check psp/testroom/camera.c against the PS2's camera, on the host (no PSP, no emulator).

Builds camera.c with the host gcc against build/testroom/generated/camera_runtime.h (tools/gen_cameras.py) and stub
stage functions (no collision), holds Dante at each spot for 5 s and compares the eye and target with what
tools/ps2_camera.py read from the real game there (docs/CAMERA.md, "Checked against the PS2").
Then walks Dante 6 m from every start of every room, checking the camera stays sane every frame (that caught
camera.c reading a bad zone index once he left every zone).
python tools/camera_check.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
# room, Dante (x, y, z), PS2 eye, PS2 target
PS2 = [
    ("ROOM_002.BIN", (1700, 0, 3500), (2573.1, 300.4, 3826.0), (1700.0, 134.0, 3500.0)),
    ("ROOM_002.BIN", (2500, 0, 4500), (2469.6, 104.4, 5070.4), (2500.0, 180.0, 4500.0)),
    ("ROOM_002.BIN", (2500, 0, 2600), (2566.8, 180.7, 3133.9), (2500.0, 180.0, 2600.0)),
    ("ROOM_003.BIN", (2500, 0, 2500), (2867.9, 207.7, 2106.7), (2500.0, 183.0, 2500.0)),
    ("ROOM_003.BIN", (2300, 0, 1600), (2382.2, 84.8, 2053.7), (2332.0, 132.0, 1752.0)),
    ("ROOM_003.BIN", (2798, 0, 2919), (2786.7, 196.2, 2308.6), (2788.9, 181.6, 2906.0)),
    ("ROOM_003.BIN", (2884.5, 0, 3059.6), (2759.5, 192.4, 2376.2), (2884.5, 181.0, 3059.6)),
]
TOLERANCE = 15.0   # cm; the PS2's target lags a held-still Dante by a little (his idle sway)

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera.h"
int stage_loaded(void) { return 0; }
int stage_raycast(const float a[3], const float b[3], float *t) { (void)a; (void)b; *t = 1.0f; return 0; }
int stage_floor(float x, float y, float z, float step, float *out) { (void)x; (void)y; (void)z; (void)step; (void)out; return 0; }
int main(int argc, char **argv) {
    /* room x y z [x1 z1 frames start yaw]: held at (x, z), or walked to (x1, z1) checking every frame */
    Player p; FollowCamera c; int i, n = argc > 7 ? atoi(argv[7]) : 300;
    float x0, z0, x1, z1;
    memset(&p, 0, sizeof p); memset(&c, 0, sizeof c);
    camera_room(argv[1], argc > 8 ? atoi(argv[8]) : 0);
    p.x = x0 = x1 = (float)atof(argv[2]); p.y = (float)atof(argv[3]); p.z = z0 = z1 = (float)atof(argv[4]); p.on_ground = 1;
    if (argc > 6) { x1 = (float)atof(argv[5]); z1 = (float)atof(argv[6]); }
    if (argc > 9) p.yaw = (float)atof(argv[9]);
    camera_start(&p, &c);
    for (i = 0; i < n; ++i) {
        float t = (float)i / n, dx, dy, dz, d;
        p.x = x0 + (x1 - x0) * t; p.z = z0 + (z1 - z0) * t;
        camera_update(&p, &c, 0, 1.0f / 60.0f);
        dx = c.x - c.tx; dy = c.y - c.ty; dz = c.z - c.tz; d = dx * dx + dy * dy + dz * dz;
        if (!(d > 50.0f * 50.0f && d < 6000.0f * 6000.0f) || c.fov <= 0.0f) { printf("BAD frame %d\n", i); return 0; }
    }
    printf("%f %f %f %f %f %f\n", c.x, c.y, c.z, c.tx, c.ty, c.tz);
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / "harness.c", Path(tmp) / "harness.exe"
        src.write_text(HARNESS)
        subprocess.run(["gcc", "-O1", "-o", str(exe), str(src), str(ROOT / "psp/testroom/camera.c"),
                        "-I" + str(ROOT / "psp/testroom"), "-I" + str(ROOT / "build/testroom/generated"), "-lm"], check=True)
        worst, failed = 0.0, 0
        for room, spot, eye, tgt in PS2:
            out = [float(v) for v in subprocess.run([str(exe), room] + [str(v) for v in spot], capture_output=True,
                                                    text=True, check=True).stdout.split()]
            de = max(abs(a - b) for a, b in zip(out[:3], eye))
            dt = max(abs(a - b) for a, b in zip(out[3:], tgt))
            ok = de <= TOLERANCE and dt <= TOLERANCE
            failed += not ok
            worst = max(worst, de, dt)
            print(f"{room} {spot}: eye off {de:5.1f}, target off {dt:5.1f}  {'ok' if ok else 'FAIL'}")
        print(f"worst {worst:.1f} cm; {failed} failed")
        failed += sweep(exe)
        sys.exit(1 if failed else 0)


def sweep(exe):
    """Every room's every start, Dante walked 6 m the way he faces: the camera stays sane (no NaN, eye 0.5-60 m
    from the target, a FOV) every frame."""
    import math
    from dmc3_image import AfsArchive, Iso9660
    from prepare_stage import pac_entries
    from gen_rooms import SLOTS, parse_starts
    iso_path = next(ROOT.glob("*.iso"))
    bad = runs = 0
    with Iso9660(iso_path) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        for name, slot in SLOTS.items():
            e = afs.entries[slot + 1]
            for k, s in enumerate(parse_starts(pac_entries(iso.read_file("GDATA.AFS", e["offset"], e["size"])))):
                yaw = math.radians(s[3])
                args = [str(exe), f"ROOM_{name}.BIN", str(s[0]), str(s[1]), str(s[2]), str(s[0] + math.sin(yaw) * 600),
                        str(s[2] + math.cos(yaw) * 600), "200", str(k), str(yaw)]
                out = subprocess.run(args, capture_output=True, text=True).stdout
                runs += 1
                if "BAD" in out or not out.strip():
                    bad += 1
                    print(f"sweep: ROOM_{name} start {k}: {out.strip() or 'crashed'}")
    print(f"sweep: {runs} room starts walked, {bad} bad")
    return bad


if __name__ == "__main__":
    main()
