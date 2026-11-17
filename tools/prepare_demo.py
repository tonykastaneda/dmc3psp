"""Bake one of the game's real-time cutscenes for the test room (docs/CUTSCENES.md).

    python tools/prepare_demo.py <iso> <demo id> --output build/testroom

writes DEMO_nnn.BIN (the camera, baked per demo frame) and DSTG_nnn.BIN (the demo's stage, as tools/prepare_stage.py
builds a room). Everything stays local (the repo is public).

DEMO_nnn.BIN (little endian):
  +0x00 "DMO1", u32 frames, u32 camera offset, u32 reserved[5]
  camera: frames x {f32 eye[3], f32 target[3], f32 fov (degrees), f32 roll}
"""
import argparse
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_unpack  # noqa: E402
from mot_ps2 import hermite  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def parse_cam(data: bytes):
    """A .cam (CCameraMotion, FUN_002116b0): the MOT header with CAM, a mask of 14 keyed channels, start / end,
    a base per channel, then MOT's channel records for the keyed ones (FUN_001fa410 / FUN_001fae10)."""
    header = struct.unpack_from("<I", data, 0)[0]
    if data[4:8] != b"CAM\0":
        raise ValueError("Not a CAM")
    mask, loops = struct.unpack_from("<2I", data, 8)
    start, end, loop_a, loop_b = struct.unpack_from("<4f", data, 0x10)
    bases = list(struct.unpack_from("<14f", data, 0x20))
    count = struct.unpack_from("<I", data, header)[0]
    keyed = [k for k in range(14) if mask >> k & 1]
    if len(keyed) != count:
        raise ValueError(f"CAM mask gives {len(keyed)} channels, header {count}")
    channels = {}
    at = header + 4
    for k in keyed:
        size, key_count, kind, offset = struct.unpack_from("<HHBxh", data, at)
        stride, head = {0: (8, 8), 1: (16, 8), 2: (4, 16), 3: (8, 32)}[kind]
        params = struct.unpack_from("<6f" if kind == 3 else "<2f", data, at + 8) if kind in (2, 3) else ()
        keys = []
        for i in range(key_count):
            p = at + head + i * stride
            raw = struct.unpack_from("<H", data, p)[0]
            frame = raw & 0x7FFF
            if kind == 1:
                v, tin, tout = struct.unpack_from("<3f", data, p + 4)
                if not raw & 0x8000:          # bit 15 clear: linear to the next key (FUN_001fae10)
                    tin = tout = None
                keys.append((frame, v, tin, tout))
            elif kind == 0:
                keys.append((frame, struct.unpack_from("<f", data, p + 4)[0], None, None))
            elif kind == 2:
                q = struct.unpack_from("<H", data, p + 2)[0]
                keys.append((frame, params[0] + params[1] * q / 65535.0, None, None))
            else:
                qv, qi, qo = struct.unpack_from("<3H", data, p + 2)
                keys.append((frame, params[0] + params[1] * qv / 65535.0,
                             params[2] + params[3] * qi / 65535.0, params[4] + params[5] * qo / 65535.0))
        # A linear key's tangents are None; hermite() then interpolates linearly to the next.
        channels[k] = (offset, keys)
        at += size
    return {"start": start, "end": end, "loops": loops, "bases": bases, "channels": channels}


def cam_at(cam, frame: float):
    values = list(cam["bases"])
    for k, (offset, keys) in cam["channels"].items():
        values[k] = hermite(keys, frame - offset) if keys else values[k]
    eye = [values[6 + i] + values[i] for i in range(3)]
    target = [values[9 + i] + values[i] for i in range(3)]
    return eye, target, values[12], values[13]


def clip_span(clip):
    start = float(clip.get("SetFrame", 0.0))
    return start, start + float(clip.get("Life", 0.0)) * float(clip.get("ClipScale", 1.0) or 1.0)


def bake_camera(timeline, files):
    track = next(t for t in timeline["tracks"] if t["kind"] == "Camera")
    clips = []
    for clip in track["clips"]:
        name = clip.get("Camera")
        if name and name in files:
            clips.append((clip, parse_cam(files[name])))
    frames = int(math.ceil(max(clip_span(c)[1] for c, _ in clips)))
    out = []
    current = None
    for f in range(frames):
        for clip, cam in clips:
            a, b = clip_span(clip)
            if a <= f < b:
                current = (clip, cam)
        clip, cam = current
        a, _ = clip_span(clip)
        scale = float(clip.get("ClipScale", 1.0) or 1.0)
        local = cam["start"] + float(clip.get("SkipFrame", 0.0)) + (f - a) / scale
        local = min(local, cam["end"])
        out.append(cam_at(cam, local))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso", type=Path)
    parser.add_argument("demo", type=int)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "testroom")
    args = parser.parse_args()
    work = ROOT / "build" / "demo" / str(args.demo)
    demo_unpack.unpack(args.iso, args.demo, work)
    timeline = json.loads((work / "timeline.json").read_text(encoding="utf-8"))
    files = {p.name: p.read_bytes() for d in work.iterdir() if d.is_dir() for p in d.iterdir()}

    camera = bake_camera(timeline, files)
    blob = bytearray(32)
    cam_at_offset = len(blob)
    for eye, target, fov, roll in camera:
        blob += struct.pack("<8f", *eye, *target, fov, roll)
    struct.pack_into("<4sII", blob, 0, b"DMO1", len(camera), cam_at_offset)
    args.output.mkdir(parents=True, exist_ok=True)
    out = args.output / f"DEMO_{args.demo:03d}.BIN"
    out.write_bytes(blob)

    # The stage: slot 00's .scm clip, its texture, built as a room; its play area the camera's reach.
    stage_clip = next((c for t in timeline["tracks"] if t["kind"] == "Model" for c in t["clips"]
                       if str(c.get("Model", "")).lower().endswith(".scm")), None)
    if stage_clip:
        pts = [p for e, t, _, _ in camera for p in (e, t)]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        subprocess.run([sys.executable, str(ROOT / "tools" / "prepare_stage.py"), str(args.iso),
                        "--output", str(args.output / f"DSTG_{args.demo:03d}.BIN"),
                        "--scm", str(work / "model" / stage_clip["Model"]),
                        "--ptx", str(work / "model" / stage_clip["Texture"]),
                        "--play", *[f"{v:.1f}" for v in lo + hi]], check=True)
    e0, t0, fov0, roll0 = camera[0]
    print(f"{out}: {len(camera)} frames; first eye {[round(v) for v in e0]} target {[round(v) for v in t0]} "
          f"fov {fov0:.1f} ch13 {roll0:.2f}")


if __name__ == "__main__":
    main()
