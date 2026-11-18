"""Bake one of the game's real-time cutscenes for the test room (docs/CUTSCENES.md).

    python tools/prepare_demo.py <iso> <demo id> --output build/testroom

writes DEMO_nnn.BIN (the camera, baked per demo frame) and DSTG_nnn.BIN (the demo's stage, as tools/prepare_stage.py
builds a room). Everything stays local (the repo is public).

DEMO_nnn.BIN (little endian; offsets from its start):
  +0x00 "DMO2", u32 frames, camera, textures, texture count, actors, actor count, resident (the bytes before the
  MOTs: the runtime reads those and streams each motion as its clip comes up)
  camera: frames x {f32 eye[3], f32 target[3], f32 fov (degrees), f32 channel 13}
  texture: {u32 width, height, pixels (8-bit, swizzled), palette (256 ABGR)}
  actor (128 bytes): i32 slot, parent actor (CnsPos, -1), parent bone, bones, vertices, indices, objects, motions;
    u32 parents (i8), local (f32x3), rest (f32x3), vertices ({u, v, normal, position} f32), skin ({bones, weights}
    u8x3 each, 8 bytes), indices (u16), objects ({texture, first index, count}), motions ({f32 set, end, scale, skip;
    u32 MOT offset, size; 32 bytes}), the bone its bounding sphere is about, its radius (f32); f32 pos[3], rot[3] (the clip's adjustments), shown from / to; u32 hide switches, their offset ({f32 frame,
    u32 on})
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
    current = min(clips, key=lambda c: clip_span(c[0])[0])   # before the first clip starts: the first
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


def align(blob: bytearray, n: int = 16) -> int:
    while len(blob) % n:
        blob.append(0)
    return len(blob)


def bake_cast(timeline, files, blob: bytearray):
    """Each model slot with a .mod: its skeleton, slimmed mesh, objects on the shared textures, its CnsPos and its
    motion clips (raw MOT). Returns (texture table offset, count, actor table offset, count)."""
    import prepare_skinned_model as psm
    from prepare_stage import swizzle8
    tracks = {(t["kind"], t["slot"]): t for t in timeline["tracks"]}
    textures, texture_index = [], {}

    def texture_set(name):
        if name not in texture_index:
            texture_index[name] = []
            for tx in psm.decode_textures(files[name]):
                pixels, palette = psm.indexed_texture(tx)
                texture_index[name].append(len(textures))
                textures.append((tx["width"], tx["height"], bytes(pixels), palette))
        return texture_index[name]

    actors = []
    for slot in range(64):
        model = tracks.get(("Model", slot))
        if not model or not model["clips"]:
            continue
        clip = model["clips"][0]
        name = str(clip.get("Model", ""))
        if not name.lower().endswith(".mod") or name not in files or not files[name]:
            continue
        try:
            skel, mesh = psm.enemy_mesh(files[name], None)
        except ValueError as error:   # a rig the converter doesn't take (a parent after its child): left out
            print(f"  slot {slot:2d} {name}: skipped ({error})")
            continue
        tex = texture_set(clip["Texture"]) if clip.get("Texture") in files else []
        motions = []
        for mclip in (tracks.get(("Motion", slot)) or {"clips": []})["clips"]:
            mot = mclip.get("Motion")
            if mot and files.get(mot):
                a, b = clip_span(mclip)
                motions.append((a, b, float(mclip.get("ClipScale", 1.0) or 1.0), float(mclip.get("SkipFrame", 0.0)), files[mot]))
        # Its hide track: DispOn / DispOff switch the whole slot at a clip's start (a cut's own .hid, which keys
        # each of the model's objects, counts as on [its per-object channels: not yet]).
        switches = []
        for hclip in (tracks.get(("Hide", slot)) or {"clips": []})["clips"]:
            hid = str(hclip.get("Hide", ""))
            if hid:
                switches.append((float(hclip.get("SetFrame", 0.0)), 0 if hid.lower() == "dispoff.hid" else 1))
        switches.sort()
        cns = clip.get("CnsPos")
        actors.append({"slot": slot, "name": name, "skel": skel, "mesh": mesh, "tex": tex, "motions": motions,
                       "parent": cns[0] if isinstance(cns, list) else -1, "bone": cns[1] if isinstance(cns, list) else 0,
                       "pos": clip.get("Pos") or [0.0, 0.0, 0.0], "rot": clip.get("Rot") or [0.0, 0.0, 0.0],
                       "span": clip_span(clip), "switches": switches})

    tex_table = align(blob)
    blob += bytes(16 * len(textures))
    for i, (w, h, pixels, palette) in enumerate(textures):
        pix = align(blob)
        blob += swizzle8(w, h, pixels)
        pal = align(blob)
        blob += struct.pack("<256I", *palette)
        struct.pack_into("<4I", blob, tex_table + 16 * i, w, h, pix, pal)

    slot_to_actor = {a["slot"]: i for i, a in enumerate(actors)}
    pending = []   # the MOTs go last: the runtime keeps what's before them and reads a motion as its clip comes up
    actor_table = align(blob)
    blob += bytes(128 * len(actors))
    for i, a in enumerate(actors):
        skel, mesh = a["skel"], a["mesh"]
        n, v = skel["count"], len(mesh["positions"])
        # Its bounding sphere for culling: about the bone that carries it (1: the hips carry a cutscene body's place).
        centre_bone = 1 if n > 1 else 0
        c = skel["world"][centre_bone]
        radius = max(math.dist(p[:3], c[:3]) for p in mesh["positions"]) * 1.25 + 30.0
        parents_at = align(blob)
        blob += struct.pack(f"<{n}b", *skel["parents"])
        local_at = align(blob)
        for p in skel["local"]:
            blob += struct.pack("<3f", *p[:3])
        rest_at = align(blob)
        for p in skel["world"]:
            blob += struct.pack("<3f", *p[:3])
        vert_at = align(blob)
        for k in range(v):
            blob += struct.pack("<8f", *mesh["uvs"][k][:2], *mesh["normals"][k][:3], *mesh["positions"][k][:3])
        skin_at = align(blob)
        for k in range(v):
            blob += struct.pack("<6B2x", *mesh["bones"][k][:3], *mesh["weights"][k][:3])
        index_at = align(blob)
        blob += struct.pack(f"<{len(mesh['indices'])}H", *mesh["indices"])
        object_at = align(blob)
        for o in mesh["objects"]:
            t = a["tex"][o["texture"]] if o["texture"] < len(a["tex"]) else (a["tex"][0] if a["tex"] else 0)
            blob += struct.pack("<3I", t, o["index_start"], o["index_count"])
        motion_at = align(blob)
        blob += bytes(32 * len(a["motions"]))
        for k, (s, e, scale, skip, data) in enumerate(a["motions"]):
            pending.append((motion_at + 32 * k, s, e, scale, skip, data))
        switch_at = align(blob)
        for frame, on in a["switches"]:
            blob += struct.pack("<fI", frame, on)
        struct.pack_into("<2I", blob, actor_table + 128 * i + 104, len(a["switches"]), switch_at)
        parent = slot_to_actor.get(a["parent"], -1)
        struct.pack_into("<8i10I6f2f", blob, actor_table + 128 * i,
                         a["slot"], parent, a["bone"], n, v, len(mesh["indices"]), len(mesh["objects"]), len(a["motions"]),
                         parents_at, local_at, rest_at, vert_at, skin_at, index_at, object_at, motion_at,
                         centre_bone, struct.unpack("<I", struct.pack("<f", radius))[0],
                         *a["pos"][:3], *a["rot"][:3], *a["span"])
        on = "" if parent < 0 else f" on slot {a['parent']} bone {a['bone']}"
        print(f"  slot {a['slot']:2d} {a['name']:14s} {n:2d} bones {v:5d} vertices {len(a['motions']):3d} motions{on}")
    resident = align(blob)
    for entry, s, e, scale, skip, data in pending:
        at = align(blob)
        blob += data
        struct.pack_into("<4f2I", blob, entry, s, e, scale, skip, at, len(data))
    return tex_table, len(textures), actor_table, len(actors), resident


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
    tex_at, tex_count, actor_at, actor_count, resident = bake_cast(timeline, files, blob)
    struct.pack_into("<4s7I", blob, 0, b"DMO2", len(camera), cam_at_offset, tex_at, tex_count, actor_at, actor_count, resident)
    args.output.mkdir(parents=True, exist_ok=True)
    out = args.output / f"DEMO_{args.demo:03d}.BIN"
    out.write_bytes(blob)

    # The stage: slot 00's .scm clip, its texture, built as a room; its play area the camera's reach.
    # The stage proper (stNNN.scm), not its pieces (ss...scm).
    scms = [c for t in timeline["tracks"] if t["kind"] == "Model" for c in t["clips"]
            if str(c.get("Model", "")).lower().endswith(".scm")]
    stage_clip = next((c for c in scms if str(c["Model"]).lower().startswith("st")), scms[0] if scms else None)
    room_bin = None
    if stage_clip and not ((work / "model" / stage_clip["Model"]).exists() and (work / "model" / str(stage_clip.get("Texture"))).exists()):
        # A room's own stage (st232 is room 232): the room's BIN as built for the missions (tools/gen_rooms.py).
        import re
        import shutil
        m = re.match(r"st(\d+)", stage_clip["Model"].lower())
        room_bin = args.output / f"ROOM_{m.group(1)}.BIN" if m else None
        if room_bin and room_bin.exists():
            shutil.copyfile(room_bin, args.output / f"DSTG_{args.demo:03d}.BIN")
            skinned = sum(struct.unpack_from("<i", blob, actor_at + 128 * i + 16)[0] * 32 + 64 for i in range(actor_count))
            if room_bin.stat().st_size + resident + skinned > 5400 * 1024:
                print(f"WARNING: {room_bin.name} and the demo's data don't fit together")
            print(f"stage {stage_clip['Model']}: {room_bin.name}")
        stage_clip = None
    if stage_clip:
        pts = [p for e, t, _, _ in camera for p in (e, t)]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        # It shares the stage buffer (5400 KB, psp/testroom/stage.c) with the demo's resident part and its skinned
        # vertices: coarser subdivision until it fits.
        stage_out = args.output / f"DSTG_{args.demo:03d}.BIN"
        skinned = sum(struct.unpack_from("<i", blob, actor_at + 128 * i + 16)[0] * 32 + 64 for i in range(actor_count))
        budget = 5400 * 1024 - resident - skinned - 128 * 1024
        # Coarser first, then cropped to what the cameras see (triangles wholly that far outside their reach dropped).
        for edge, crop in ((500, 0), (1000, 0), (2000, 0), (4000, 0), (100000, 0), (100000, 30000), (100000, 15000),
                           (100000, 8000), (100000, 4000)):
            subprocess.run([sys.executable, str(ROOT / "tools" / "prepare_stage.py"), str(args.iso),
                            "--output", str(stage_out),
                            "--scm", str(work / "model" / stage_clip["Model"]),
                            "--ptx", str(work / "model" / stage_clip["Texture"]),
                            "--play", *[f"{v:.1f}" for v in lo + hi], "--max-edge", str(edge)]
                           + (["--crop", str(crop)] if crop else []), check=True)
            if stage_out.stat().st_size <= budget:
                break
            print(f"stage {stage_out.stat().st_size:,} bytes over its {budget:,}: coarser")
        if stage_out.stat().st_size > budget:
            print(f"WARNING: {stage_out.name} doesn't fit ({stage_out.stat().st_size:,} > {budget:,})")
    e0, t0, fov0, roll0 = camera[0]
    print(f"{out}: {len(camera)} frames; first eye {[round(v) for v in e0]} target {[round(v) for v in t0]} "
          f"fov {fov0:.1f} ch13 {roll0:.2f}")


if __name__ == "__main__":
    main()
