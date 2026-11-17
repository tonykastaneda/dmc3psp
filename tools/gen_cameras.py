#!/usr/bin/env python3
"""Every room's camera, from the player's disc: camera_runtime.h for psp/testroom/camera.c.

Per room (see docs/CAMERA.md):
  # GAME CONFIG (the stage slot's text)  cam_rotx (pitch, degrees), cam_len (normal, battle distance),
                                        cam_aov (vertical FOV, degrees), cam_init (safe eye points)
  CAM (the data slot's PAC entry 3)     camera zones: type, priority, transitions, areas, and the Pan or
                                        Rail camera's parameters
  POS                                   each start's camera yaw offset (+0x14, degrees)
Values the game would ignore (negative, FUN_0024e6a0) are written as -1 so the runtime keeps its default.
"""
import argparse
import re
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_stage import pac_entries
from gen_rooms import SLOTS

PARAM_SIZE = {0: 0x70, 1: 0x450, 2: 0x860, 3: 0x70}
MAX_INIT = 8


def parse_config(text):
    """The CONFIG set's camera keys, as FUN_002ff0f0 reads them (comments after ';' dropped)."""
    out = dict(pitch=-1.0, len=-1.0, battle=-1.0, aov=-1.0, init=[])
    # A set's header starts its line: ";# SET 0 CONFIG" is a commented-out one (";#" is one token to the game).
    block = re.search(r"^#\s*SET\s+\d+\s+CONFIG(.*?)(?=^#\s*SET|\Z)", text, re.S | re.M)
    if not block:
        return out
    # The game reads tokens, not lines: ';' is just a token it doesn't know, so a setting after one ("; cam_aov 57")
    # still counts and, coming later, wins (PCSX2, room 111: FOV 57, not the 64 before it).
    tokens = block.group(1).replace(",", " ").split()

    def nums(i, n):
        vals = []
        for t in tokens[i + 1:i + 1 + n]:
            try:
                vals.append(float(t))
            except ValueError:
                break
        return vals
    for i, key in enumerate(tokens):
        if key == "cam_rotx" and (v := nums(i, 1)):
            out["pitch"] = v[0]
        elif key == "cam_len" and len(v := nums(i, 2)) == 2:
            out["len"], out["battle"] = v
        elif key == "cam_aov" and (v := nums(i, 1)):
            out["aov"] = v[0]
        elif key == "cam_init" and len(v := nums(i, 3)) == 3 and len(out["init"]) < MAX_INIT:
            out["init"].append(v)
    return out


def parse_zones(chunk):
    zones = []
    count = struct.unpack_from("<H", chunk, 6)[0]
    at = 0x10
    for _ in range(count):
        h = chunk[at:at + 0x40]
        kind = h[1]
        p = chunk[at + 0x40:at + 0x40 + PARAM_SIZE.get(kind, 0)]
        areas_at = at + 0x40 + PARAM_SIZE.get(kind, 0)
        areas = []
        for k in range(h[0x14]):
            a = chunk[areas_at + k * 0x50:areas_at + (k + 1) * 0x50]
            corners = [struct.unpack_from("<3f", a, 16 * j) for j in range(4)]
            areas.append((corners, struct.unpack_from("<f", a, 0x40)[0]))
        z = dict(kind=kind, stick=h[2], battle=h[6], pri=h[0x35],
                 trans=[(h[0x15 + 2 * k], h[0x16 + 2 * k]) for k in range(16) if h[0x16 + 2 * k]], areas=areas, points=[])
        if kind in (0, 1):
            z.update(axis=p[0], target=p[1], zoom_mode=p[2], jump=p[3], lock_fov=struct.unpack_from("<f", p, 4)[0])
        if kind == 0:
            z.update(fixed=struct.unpack_from("<3f", p, 0x10), mat_off=struct.unpack_from("<3f", p, 0x20),
                     world_off=struct.unpack_from("<3f", p, 0x30), zoom=struct.unpack_from("<4f", p, 0x40),
                     eye=struct.unpack_from("<3f", p, 0x50), fov=struct.unpack_from("<f", p, 0x60)[0],
                     roll=struct.unpack_from("<f", p, 0x64)[0], points=[])
        elif kind == 1:
            n = p[0x440]
            z.update(fixed=(0.0, 0.0, 0.0), mat_off=struct.unpack_from("<3f", p, 0x10),
                     world_off=struct.unpack_from("<3f", p, 0x20), zoom=struct.unpack_from("<4f", p, 0x30),
                     eye=(0.0, 0.0, 0.0), fov=0.0, roll=0.0,
                     points=[(struct.unpack_from("<3f", p, 0x40 + 12 * k), struct.unpack_from("<3f", p, 0x1c0 + 12 * k),
                              struct.unpack_from("<f", p, 0x340 + 4 * k)[0], struct.unpack_from("<f", p, 0x3c0 + 4 * k)[0])
                             for k in range(n)])
        zones.append(z)
        at = areas_at + h[0x14] * 0x50
    if at != len(chunk):
        raise ValueError(f"CAM chunk ends at {at:#x}, expected {len(chunk):#x}")
    return zones


def parse_cuts(chunk):
    """DCA (the data PAC's entry 8): the event scripts' camera cuts (FUN_003201d0 / FUN_003201f0). u16 count at +6,
    cuts from +0x10, 0x410 bytes each: 32 eye points (xyz) at +0, 32 target points at +0x180, FOV per point at
    +0x300, roll per point at +0x380, point count (byte) at +0x400, frame count (s32) at +0x404."""
    cuts = []
    count = struct.unpack_from("<H", chunk, 6)[0]
    for k in range(count):
        b = 0x10 + k * 0x410
        if b + 0x410 > len(chunk):
            break
        n = min(chunk[b + 0x400], 32)
        frames = struct.unpack_from("<i", chunk, b + 0x404)[0]
        pts = [(struct.unpack_from("<3f", chunk, b + 12 * i), struct.unpack_from("<3f", chunk, b + 0x180 + 12 * i),
                struct.unpack_from("<f", chunk, b + 0x300 + 4 * i)[0], struct.unpack_from("<f", chunk, b + 0x380 + 4 * i)[0])
               for i in range(n)]
        cuts.append((pts, frames))
    return cuts


def f(v):
    return "%.4ff" % v


def v3(v):
    return "{%s,%s,%s}" % (f(v[0]), f(v[1]), f(v[2]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--header", type=Path, required=True)
    args = ap.parse_args()
    rooms, areas, zones, points, cuts = [], [], [], [], []
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        for name, slot in SLOTS.items():
            stage = pac_entries(iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"]))
            data = pac_entries(iso.read_file("GDATA.AFS", afs.entries[slot + 1]["offset"], afs.entries[slot + 1]["size"]))
            game = next((c for c in stage if c[:6] == b"# GAME"), b"").decode("latin1").replace("\r", "")
            cfg = parse_config(game)
            cam = next((c for c in data if c and c[:4] == b"CAM\0"), None)
            pos = next(c for c in data if c and c[:4] == b"POS\0")
            yaw_off = [struct.unpack_from("<f", pos, 0x10 + i * 0x30 + 0x14)[0]
                       for i in range(struct.unpack_from("<H", pos, 6)[0])]
            room_zones = parse_zones(cam) if cam else []
            zone_first = len(zones)
            for z in room_zones:
                area_first, point_first = len(areas), len(points)
                areas.extend(z["areas"])
                points.extend(z["points"])
                zones.append((z, area_first, point_first))
            dca = next((c for c in data if c and c[:4] == b"DCA\0"), None)
            room_cuts = parse_cuts(dca) if dca else []
            cut_first = len(cuts)
            for pts, frames in room_cuts:
                cuts.append((len(points), len(pts), frames))
                points.extend(pts)
            rooms.append((name, cfg, yaw_off, zone_first, len(room_zones), cut_first, len(room_cuts)))

    lines = ["/* Generated by tools/gen_cameras.py from the game disc: every room's camera (docs/CAMERA.md). */",
             "#ifndef DMC3PSP_CAMERA_RUNTIME_H", "#define DMC3PSP_CAMERA_RUNTIME_H",
             "typedef struct { float c[4][3]; float height; } CamArea;",
             "typedef struct { float eye[3], path[3], fov, roll; } CamPoint;",
             "typedef struct { unsigned char kind, stick, battle, pri, axis, target, zoom_mode, jump, trans_count;",
             "                 unsigned char trans[16][2]; short area_first, area_count, point_first, point_count;",
             "                 float lock_fov, fixed[3], mat_off[3], world_off[3], zoom[4], eye[3], fov, roll; } CamZone;",
             "typedef struct { const char *file; float pitch, len, battle, aov; unsigned char init_count, start_count;",
             "                 float init[%d][3]; float start_yaw[8]; short zone_first, zone_count, cut_first, cut_count; } CamRoom;" % MAX_INIT,
             "typedef struct { short point_first, point_count; int frames; } CamCut;   /* eye = CamPoint.eye, target = .path */"]
    lines.append("static const CamArea cam_areas[] = {" + (",".join(
        "{{%s},%s}" % (",".join(v3(c) for c in corners), f(height)) for corners, height in areas) or "{{{0}}}") + "};")
    lines.append("static const CamPoint cam_points[] = {" + (",".join(
        "{%s,%s,%s,%s}" % (v3(e), v3(p), f(fov), f(roll)) for e, p, fov, roll in points) or "{{0}}") + "};")
    cells = []
    for z, area_first, point_first in zones:
        trans = z["trans"] + [(0, 0)] * (16 - len(z["trans"]))
        cells.append("{%d,%d,%d,%d,%d,%d,%d,%d,%d,{%s},%d,%d,%d,%d,%s,%s,%s,%s,{%s},%s,%s,%s}" % (
            z["kind"], z["stick"], z["battle"], z["pri"], z.get("axis", 0), z.get("target", 0), z.get("zoom_mode", 0),
            z.get("jump", 0), len(z["trans"]), ",".join("{%d,%d}" % t for t in trans), area_first, len(z["areas"]),
            point_first, len(z["points"]), f(z.get("lock_fov", 0.0)), v3(z.get("fixed", (0, 0, 0))),
            v3(z.get("mat_off", (0, 0, 0))), v3(z.get("world_off", (0, 0, 0))),
            ",".join(f(v) for v in z.get("zoom", (0, 0, 0, 0))), v3(z.get("eye", (0, 0, 0))), f(z.get("fov", 0.0)),
            f(z.get("roll", 0.0))))
    lines.append("static const CamZone cam_zones[] = {" + (",".join(cells) or "{0}") + "};")
    lines.append("static const CamCut cam_cuts[] = {" + (",".join("{%d,%d,%d}" % c for c in cuts) or "{0}") + "};")
    cells = []
    for name, cfg, yaw_off, zone_first, zone_count, cut_first, cut_count in rooms:
        init = cfg["init"] + [[0.0, 0.0, 0.0]] * (MAX_INIT - len(cfg["init"]))
        yaws = (yaw_off + [0.0] * 8)[:8]
        cells.append('{"ROOM_%s.BIN",%s,%s,%s,%s,%d,%d,{%s},{%s},%d,%d,%d,%d}' % (
            name, f(cfg["pitch"]), f(cfg["len"]), f(cfg["battle"]), f(cfg["aov"]), len(cfg["init"]), min(len(yaw_off), 8),
            ",".join(v3(p) for p in init), ",".join(f(y) for y in yaws), zone_first, zone_count, cut_first, cut_count))
    lines.append("#define CAM_ROOM_COUNT %d" % len(rooms))
    lines.append("static const CamRoom cam_rooms[] = {" + ",".join(cells) + "};")
    lines.append("#endif")
    args.header.write_text("\n".join(lines) + "\n")
    print(f"{args.header}: {len(rooms)} rooms, {len(zones)} zones, {len(areas)} areas, {len(cuts)} event cuts, {len(points)} spline points")


if __name__ == "__main__":
    main()
