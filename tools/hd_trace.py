"""Run a key script on the HD Collection's DMC3 and print what Dante did, per 60 fps frame.

    python tools/hd_trace.py name          # analysis/ref/hd/trace_name.csv, table on stdout
    python tools/hd_trace.py name video    # also analysis/ref/hd/trace_name.mp4
"""
import csv
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import hd_ref as h

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "ref" / "hd"
PLAYER_POINTER = 0xD074C8

# name: (seconds, [(key, first second, last second)]).
SCRIPTS = {
    "idle": (1.0, []),
    "jump": (1.6, [("cross", 0.1, 0.2)]),
    "hike": (2.2, [("cross", 0.1, 0.2), ("cross", 0.6, 0.7)]),
    "star": (2.0, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7)]),
    "hikestar": (2.6, [("cross", 0.1, 0.2), ("cross", 0.6, 0.7), ("circle", 1.0, 1.1)]),
    "starhike": (2.6, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7), ("cross", 1.0, 1.1)]),
    "starstar": (2.6, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7), ("circle", 1.0, 1.1)]),
    "hikehike": (2.6, [("cross", 0.1, 0.2), ("cross", 0.6, 0.7), ("cross", 1.0, 1.1)]),
    "starhikestar": (3.2, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7), ("cross", 1.1, 1.2), ("circle", 1.6, 1.7)]),
    "hikestarstar": (3.2, [("cross", 0.1, 0.2), ("cross", 0.6, 0.7), ("circle", 1.0, 1.1), ("circle", 1.5, 1.6)]),
    "shs": (3.4, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7), ("cross", 0.95, 1.05), ("circle", 1.3, 1.4)]),
    "earlyhike": (2.4, [("cross", 0.1, 0.2), ("circle", 0.6, 0.7), ("cross", 0.7, 0.8)]),
    "earlystar": (2.4, [("cross", 0.1, 0.2), ("cross", 0.6, 0.7), ("circle", 0.65, 0.75)]),
    "jumpheld": (1.8, [("cross", 0.1, 1.2)]),
    "hikeheld": (2.4, [("cross", 0.1, 0.2), ("cross", 0.6, 1.3)]),
    "jumptap": (1.6, [("cross", 0.1, 0.12)]),
    "hiketap": (2.2, [("cross", 0.1, 0.2), ("cross", 0.6, 0.63)]),
    "runjump": (2.4, [("lup", 0.1, 2.0), ("cross", 0.9, 1.0)]),
    "hikestick": (2.4, [("cross", 0.1, 0.2), ("lright", 0.5, 1.8), ("cross", 0.6, 0.7)]),
    "starstick": (2.2, [("cross", 0.1, 0.2), ("lright", 0.5, 1.6), ("circle", 0.6, 0.7)]),
    "dash3": (2.0, [("circle", 0.1, 0.15), ("circle", 0.45, 0.5), ("circle", 0.8, 0.85), ("circle", 1.15, 1.2)]),
    "air": (9.6, [("cross", 0.3, 0.4), ("cross", 0.8, 1.0), ("circle", 1.15, 1.25),
                  ("tp", 3.1, 3.1), ("cross", 3.4, 3.5), ("circle", 3.9, 4.0), ("cross", 4.3, 4.5),
                  ("tp", 6.2, 6.2), ("cross", 6.5, 7.5),
                  ("tp", 8.3, 8.3), ("circle", 8.5, 8.6), ("circle", 8.95, 9.05)]),
    "d_n": (1.4, [("space", 0.1, 1.3), ("circle", 0.5, 0.56)]),
    "d_l": (1.4, [("space", 0.1, 1.3), ("lleft", 0.3, 1.2), ("circle", 0.5, 0.56)]),
    "d_r": (1.4, [("space", 0.1, 1.3), ("lright", 0.3, 1.2), ("circle", 0.5, 0.56)]),
    "d_u": (1.4, [("space", 0.1, 1.3), ("lup", 0.3, 1.2), ("circle", 0.5, 0.56)]),
    "d_d": (1.4, [("space", 0.1, 1.3), ("ldown", 0.3, 1.2), ("circle", 0.5, 0.56)]),
    "t_air": (2.4, [("cross", 0.1, 0.2), ("space", 0.3, 2.2), ("lup", 0.4, 1.0), ("circle", 0.6, 0.66)]),
    "t_airb": (2.4, [("cross", 0.1, 0.2), ("space", 0.3, 2.2), ("ldown", 0.4, 1.0), ("circle", 0.6, 0.66)]),
    "w_u": (2.6, [("lup", 0.1, 2.0), ("circle", 0.9, 0.96)]),
    "w_d": (2.6, [("ldown", 0.1, 2.0), ("circle", 0.9, 0.96)]),
    "w_l": (2.6, [("lleft", 0.1, 2.0), ("circle", 0.9, 0.96)]),
    "w_r": (2.6, [("lright", 0.1, 2.0), ("circle", 0.9, 0.96)]),
    "w_au": (2.2, [("cross", 0.1, 0.2), ("lup", 0.3, 1.8), ("circle", 0.55, 0.6)]),
    "w_aa": (2.2, [("cross", 0.1, 0.2), ("lup", 0.1, 1.8), ("circle", 0.8, 0.86)]),
    "wg_u": (2.2, [("wall", 0.05, 0.05), ("lup", 0.2, 1.6), ("circle", 0.5, 0.56)]),
    "wa_u": (2.2, [("wall", 0.05, 0.05), ("cross", 0.1, 0.2), ("lup", 0.3, 1.6), ("circle", 0.6, 0.66)]),
    "wg_d": (2.2, [("wall", 0.05, 0.05), ("ldown", 0.2, 1.6), ("circle", 0.5, 0.56)]),
    "wa_d": (2.2, [("wall", 0.05, 0.05), ("cross", 0.1, 0.2), ("ldown", 0.3, 1.6), ("circle", 0.6, 0.66)]),
    "wg_l": (2.2, [("wall", 0.05, 0.05), ("lleft", 0.2, 1.6), ("circle", 0.5, 0.56)]),
    "wa_l": (2.2, [("wall", 0.05, 0.05), ("cross", 0.1, 0.2), ("lleft", 0.3, 1.6), ("circle", 0.6, 0.66)]),
    "wg_r": (2.2, [("wall", 0.05, 0.05), ("lright", 0.2, 1.6), ("circle", 0.5, 0.56)]),
    "wa_r": (2.2, [("wall", 0.05, 0.05), ("cross", 0.1, 0.2), ("lright", 0.3, 1.6), ("circle", 0.6, 0.66)]),
    "r_1": (1.8, [("triangle", 0.2, 0.25)]),
    "r_2": (2.0, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6)]),
    "r_3": (2.6, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6), ("triangle", 0.9, 0.95)]),
    "r_4": (3.0, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6), ("triangle", 0.9, 0.95), ("triangle", 1.25, 1.3)]),
    "r_gap": (3.2, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6), ("triangle", 1.25, 1.3), ("triangle", 1.6, 1.65), ("triangle", 1.95, 2.0)]),
    "r_st": (2.2, [("space", 0.1, 1.9), ("lright", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "r_ht": (2.2, [("space", 0.1, 1.9), ("lleft", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "r_hth": (2.6, [("space", 0.1, 2.3), ("lleft", 0.3, 0.9), ("triangle", 0.5, 1.3)]),
    "r_helm": (2.0, [("cross", 0.1, 0.2), ("triangle", 0.55, 0.6)]),
    "r_ms": (3.2, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6), ("triangle", 0.9, 0.95), ("triangle", 1.05, 1.1), ("triangle", 1.2, 1.25), ("triangle", 1.35, 1.4), ("triangle", 1.5, 1.55), ("triangle", 1.65, 1.7), ("triangle", 1.8, 1.85)]),
    "g_1": (1.4, [("square", 0.2, 0.25)]),
    "g_m": (1.8, [("square", 0.2, 0.25), ("square", 0.4, 0.45), ("square", 0.6, 0.65), ("square", 0.8, 0.85)]),
    "g_h": (2.2, [("square", 0.2, 1.8)]),
    "g_a": (1.8, [("cross", 0.1, 0.2), ("square", 0.55, 0.6), ("square", 0.75, 0.8)]),
    "r_rise": (2.8, [("space", 0.1, 2.5), ("lright", 0.3, 0.9), ("triangle", 0.5, 1.3)]),
    "c_1": (1.8, [("triangle", 0.2, 0.25)]),
    "c_hold": (2.4, [("triangle", 0.2, 1.4)]),
    "c_air": (2.2, [("cross", 0.1, 0.2), ("triangle", 0.55, 0.6)]),
    "c_airm": (2.8, [("cross", 0.1, 0.2), ("triangle", 0.55, 0.6), ("triangle", 0.75, 0.8), ("triangle", 0.95, 1.0), ("triangle", 1.15, 1.2)]),
    "c_b": (2.4, [("space", 0.1, 2.2), ("lleft", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "c_f": (2.4, [("space", 0.1, 2.2), ("lright", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "c_mash": (3.2, [("triangle", 0.2, 0.25), ("triangle", 0.4, 0.45), ("triangle", 0.6, 0.65), ("triangle", 0.8, 0.85), ("triangle", 1.0, 1.05), ("triangle", 1.2, 1.25), ("triangle", 1.4, 1.45), ("triangle", 1.6, 1.65), ("triangle", 1.8, 1.85)]),
    "r_gapl": (4.4, [("triangle", 0.2, 0.25), ("triangle", 0.55, 0.6), ("triangle", 1.25, 1.3), ("triangle", 1.6, 1.65), ("triangle", 1.95, 2.0)]),
    "s_1": (2.2, [("circle", 0.2, 0.25)]),
    "s_2": (2.6, [("circle", 0.2, 0.25), ("circle", 0.6, 0.65)]),
    "s_3": (3.0, [("circle", 0.2, 0.25), ("circle", 0.6, 0.65), ("circle", 1.0, 1.05)]),
    "s_mash": (3.2, [("circle", 0.2, 0.25), ("circle", 0.35, 0.4), ("circle", 0.5, 0.55), ("circle", 0.65, 0.7), ("circle", 0.8, 0.85), ("circle", 0.95, 1.0), ("circle", 1.1, 1.15), ("circle", 1.25, 1.3), ("circle", 1.4, 1.45), ("circle", 1.55, 1.6), ("circle", 1.7, 1.75)]),
    "s_hold": (3.2, [("circle", 0.2, 2.2)]),
    "s_air": (2.4, [("cross", 0.1, 0.2), ("circle", 0.55, 0.6)]),
    "s_airm": (3.0, [("cross", 0.1, 0.2), ("circle", 0.55, 0.6), ("circle", 0.7, 0.75), ("circle", 0.85, 0.9), ("circle", 1.0, 1.05), ("circle", 1.15, 1.2), ("circle", 1.3, 1.35)]),
    "s_lf": (2.6, [("space", 0.1, 2.4), ("lright", 0.3, 0.9), ("circle", 0.5, 0.55)]),
    "s_lb": (2.6, [("space", 0.1, 2.4), ("lleft", 0.3, 0.9), ("circle", 0.5, 0.55)]),
    "s_lfm": (3.4, [("space", 0.1, 3.2), ("lright", 0.3, 0.9), ("circle", 0.5, 0.55), ("circle", 0.7, 0.75), ("circle", 0.9, 0.95), ("circle", 1.1, 1.15), ("circle", 1.3, 1.35), ("circle", 1.5, 1.55)]),
    "s_lbm": (3.4, [("space", 0.1, 3.2), ("lleft", 0.3, 0.9), ("circle", 0.5, 0.55), ("circle", 0.7, 0.75), ("circle", 0.9, 0.95), ("circle", 1.1, 1.15), ("circle", 1.3, 1.35), ("circle", 1.5, 1.55)]),
    "s_lh": (3.4, [("space", 0.1, 3.2), ("lright", 0.3, 0.9), ("circle", 0.5, 2.2)]),
    "s_airs": (3.0, [("cross", 0.1, 0.2), ("circle", 0.5, 0.55), ("circle", 0.72, 0.77), ("circle", 0.94, 0.99), ("circle", 1.16, 1.21), ("circle", 1.38, 1.43)]),
    "a_y": (2.6, [("triangle", 0.2, 0.25), ("triangle", 0.6, 0.65), ("triangle", 1.0, 1.05)]),
    "a_ym": (3.4, [("triangle", 0.2, 0.25), ("triangle", 0.45, 0.5), ("triangle", 0.7, 0.75), ("triangle", 0.95, 1.0), ("triangle", 1.2, 1.25), ("triangle", 1.45, 1.5), ("triangle", 1.7, 1.75), ("triangle", 1.95, 2.0)]),
    "a_yh": (2.6, [("triangle", 0.2, 1.6)]),
    "a_ya": (2.4, [("cross", 0.1, 0.2), ("triangle", 0.55, 0.6)]),
    "a_lf": (2.6, [("space", 0.1, 2.4), ("lright", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "a_lb": (2.6, [("space", 0.1, 2.4), ("lleft", 0.3, 0.9), ("triangle", 0.5, 0.55)]),
    "g_h3": (3.6, [("square", 0.2, 3.0)]),
    "g_l": (2.6, [("space", 0.1, 2.4), ("square", 0.6, 0.65), ("square", 1.2, 1.25)]),
    "s_airp": (2.8, [("cross", 0.1, 1.0), ("circle", 0.42, 0.47)]),
    "s_airh": (3.2, [("cross", 0.1, 1.0), ("circle", 0.42, 1.8)]),
    "s_12": (2.4, [("circle", 0.2, 0.25), ("circle", 0.3, 0.35)]),
    "s_13": (2.4, [("circle", 0.2, 0.25), ("circle", 0.42, 0.47)]),
    "s_14": (2.4, [("circle", 0.2, 0.25), ("circle", 0.55, 0.6)]),
    "s_a48": (2.4, [("cross", 0.1, 0.2), ("circle", 0.48, 0.53)]),
    "s_a51": (2.4, [("cross", 0.1, 0.2), ("circle", 0.51, 0.56)]),
    "g_2": (2.2, [("square", 0.2, 0.25), ("square", 0.6, 0.65)]),
    "q_t": (4.6, [("circle", 0.2, 0.25), ("lup", 1.6, 2.6), ("circle", 3.6, 3.65)]),
    "q_run": (2.0, [("lup", 0.2, 1.4)]),
    "melee": (11.0, [("triangle", 0.3, 0.35), ("triangle", 0.65, 0.7), ("triangle", 1.0, 1.05),
                     ("tp", 2.6, 2.6), ("space", 2.7, 4.4), ("lright", 2.9, 3.4), ("triangle", 3.1, 3.15),
                     ("tp", 4.9, 4.9), ("space", 5.0, 6.8), ("lleft", 5.2, 5.8), ("triangle", 5.4, 6.2),
                     ("tp", 7.7, 7.7), ("cross", 7.8, 7.9), ("triangle", 8.25, 8.3),
                     ("tp", 9.4, 9.4), ("cross", 9.5, 9.6), ("square", 9.95, 10.0), ("square", 10.2, 10.25)]),
    "wg_long": (2.6, [("wall", 0.05, 0.05), ("lup", 0.2, 2.2), ("circle", 1.2, 1.26)]),
    "move": (9.0, [("ldown", 0.3, 1.5), ("cross", 1.2, 1.3), ("cross", 1.7, 1.9), ("circle", 2.1, 2.2),
                   ("tp", 4.0, 4.0), ("circle", 4.3, 4.4), ("circle", 4.8, 4.9),
                   ("tp", 6.2, 6.2), ("cross", 6.5, 7.6)]),
    "dash": (1.5, [("circle", 0.1, 0.2)]),
    "run": (1.5, [("lup", 0.1, 1.2)]),
}


def field_row(m, p, t):
    x, y, z = m.vec3(p + 0x80)
    return {"t": t, "x": x, "y": y, "z": z, "yaw": m.u16(p + 0xC0), "state": m.u32(p + 0x3E00), "motion": m.u16(p + 0x39B0),
            "style": m.u32(p + 0x6338), "hp": m.f32(p + 0x411C)}


def run(name, video=False):
    seconds, keys = SCRIPTS[name]
    m = h.Memory()
    p = m.u64(m.base + PLAYER_POINTER)
    h.ensure_running(m)
    # The same spot and facing every run.
    m.write(p + 0x80, struct.pack("<3f", *h.TEST_SPOT))
    m.write(p + 0xC0, struct.pack("<H", 0x4000 if name.startswith("w_") else 0xAF26))
    time.sleep(0.5)
    recorder = h.record_start(OUT / f"trace_{name}.mp4") if video else None
    if recorder:
        time.sleep(0.5)
    rows, begun, ended = [], set(), set()
    start = time.perf_counter()
    while True:
        t = time.perf_counter() - start
        if t > seconds:
            break
        for i, (key, a, b) in enumerate(keys):
            if t >= a and i not in begun:
                begun.add(i)
                if key == "tp":
                    m.write(p + 0x80, struct.pack("<3f", *h.TEST_SPOT))
                elif key == "wall":
                    m.write(p + 0x80, struct.pack("<3f", *h.WALL_SPOT))
                    m.write(p + 0xC0, struct.pack("<H", 0x4000))
                else:
                    h.hold(key)
            if t >= b and i not in ended:
                ended.add(i)
                if key not in ("tp", "wall"):
                    h.release(key)
        rows.append(field_row(m, p, t))
        time.sleep(0.002)
    for key, _, _ in keys:
        if key not in ("tp", "wall"):
            h.release(key)
    if recorder:
        h.record_stop(recorder)
    return rows


def per_frame(rows):
    """The last sample in each 60 fps slot."""
    frames = {}
    for r in rows:
        frames[int(r["t"] * 60)] = r
    return [dict(frames[f], frame=f) for f in sorted(frames)]


def show(name, rows):
    frames = per_frame(rows)
    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / f"trace_{name}.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(frames[0]))
        writer.writeheader()
        writer.writerows(frames)
    last = None
    print(" fr      y    dy   dxz  yaw state")
    for r in frames:
        if last:
            dxz = ((r["x"] - last["x"]) ** 2 + (r["z"] - last["z"]) ** 2) ** 0.5
            print(f'{r["frame"]:3d} {r["y"]:6.1f} {r["y"] - last["y"]:5.1f} {dxz:5.1f} {r["yaw"]:5d} {r["state"]:5d}')
        last = r


if __name__ == "__main__":
    name = sys.argv[1]
    show(name, run(name, len(sys.argv) > 2))
