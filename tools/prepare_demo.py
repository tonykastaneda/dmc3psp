"""Bake one of the game's real-time cutscenes for the test room (docs/CUTSCENES.md).

    python tools/prepare_demo.py <iso> <demo id> --output build/testroom

writes DEMO_nnn.BIN (the camera, baked per demo frame) and DSTG_nnn.BIN (the demo's stage, as tools/prepare_stage.py
builds a room). Everything stays local (the repo is public).

DEMO_nnn.BIN (little endian; offsets from its start):
  +0x00 "DMO3", u32 frames, camera, textures, texture count, actors, actor count, resident (the bytes before the
  MOTs: the runtime reads those and streams each motion as its clip comes up), subtitles, subtitle count; 64 bytes
  then fades, fade count (DMO3 +0x28: 0 in files without them)
  fade (16 bytes): {f32 from, length, u32 out (1) / in (0), u8 RGBA (A 128 = opaque)}
  fog (24 bytes, ScrEfc GFG, after the Program colour clips): {f32 start, life, StartZ, EndZ, u32 MaxColor
  ABGR, u8 MinAlpha, MaxAlpha, Type, pad}. Their count is the high 16 bits of the light count at +0x3c
  (0 in a bin from before this). Life is the clip's Life / ClipScale.
  subtitle (128 bytes): {f32 from, to (demo frames), i16 x, y (PS2 640 x 448), size, pad, char text[112] (\n breaks)}
  camera: frames x {f32 eye[3], f32 target[3], f32 fov (degrees), f32 channel 13}
  texture: {u32 width, height, pixels (8-bit, swizzled), palette (256 ABGR)}
  actor (128 bytes): i32 slot, parent actor (CnsPos, -1), parent bone, bones, vertices, indices, objects, motions;
    u32 parents (i8), local (f32x3), rest (f32x3), vertices ({u, v, normal, position} f32), skin ({bones, weights}
    u8x3 each, 8 bytes), indices (u16), objects ({texture (bits 16-19: its blend), first index, count}), motions ({f32 set, end, scale, skip;
    u32 MOT offset, size; 32 bytes}), the bone its bounding sphere is about, its radius (f32); f32 pos[3], rot[3] (the clip's adjustments), shown from / to; u32 hide switches, their offset ({f32 frame,
    u32 on}); the last 8 bytes are the cloth block's offset and size (0: no .clt). The block is u32 script count,
    clip count, then each script (u32 chain count; each chain f32 gravity, spring, max speed, stiffness, i32 limit,
    u32 bone count, u8 bones, i8 axes — 0 Y, 1 X, 2 NY, 3 NX, 4 Z, 5 NZ — padded to 4) and each clip (f32 set, end,
    i32 script or -1, mode or -1, reset 0/1/2, level).
"""
import argparse
import json
import os
import math
import re
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


def parse_hid(data: bytes):
    """A hide track (.hid; docs/CUTSCENES.md): its first frame (+0x0c), and per object its stepped keys
    [(frame, shown)] or None (no channel). Values: -1 shown, 0 hidden."""
    header = struct.unpack_from("<I", data, 0)[0]
    if data[4:8] != b"HID\0":
        raise ValueError("Not a HID")
    first = struct.unpack_from("<f", data, 0x0C)[0]
    n = data[0x1C]
    flags = data[0x1D:0x1D + n]
    at = header + 4
    channels = []
    for k in range(n):
        if not flags[k]:
            channels.append(None)
            continue
        size, count, kind = struct.unpack_from("<HHH", data, at)
        if kind in (2, 7):
            base, scale = struct.unpack_from("<2f", data, at + 8)
            keys = []
            for i in range(count):
                frame, q = struct.unpack_from("<2H", data, at + 16 + 4 * i)
                keys.append(((frame & 0x7FFF), base + scale * q / 65535.0 < -0.5))
            channels.append(keys)
        else:
            channels.append(None)
        at += size
    return first, channels


def scm_rig(data: bytes):
    """A stage piece (.scm) as a rigid rig (tools/scm_ps2.py has the format): a bone per node record (its parent, its
    local translation and rotation, R = Rz Ry Rx), each object's vertices in its node's own space, wholly on that
    node's bone (rest offset 0), one draw per sub-mesh. Returns (skel, mesh, order, rest_rot)."""
    from mod_skeleton import strip_faces
    object_count, node_count = data[0x10], data[0x11]
    block = struct.unpack_from("<I", data, 0x1C)[0]
    tables = struct.unpack_from("<4I", data, block)
    hier = data[block + tables[0]:block + tables[0] + node_count]
    remap = data[block + tables[1]:block + tables[1] + node_count]
    objmap = data[block + tables[2]:block + tables[2] + node_count]
    records = [struct.unpack_from("<8f", data, block + tables[3] + r * 32) for r in range(node_count)]
    parent = [-1] * node_count
    for node in range(node_count):
        parent[remap[node]] = -1 if hier[node] == 0xFF else hier[node]
    order, placed = [], set()
    while len(order) < node_count:
        before = len(order)
        for r in range(node_count):
            if r not in placed and (parent[r] < 0 or parent[r] in placed):
                order.append(r)
                placed.add(r)
        if len(order) == before:
            raise ValueError("Cycle in SCM nodes")
    object_node = {obj: r for r, obj in enumerate(objmap) if obj != 0xFF}
    mesh = {"positions": [], "normals": [], "uvs": [], "bones": [], "weights": [], "indices": [], "objects": []}
    for index in range(object_count):
        if index not in object_node:
            continue
        bone = object_node[index]
        flags, total, mesh_offset = struct.unpack_from("<HHI", data, 0x30 + index * 48)
        for k in range(flags & 0xFF):
            header = mesh_offset + 0x30 * k
            vc, texture = struct.unpack_from("<HH", data, header)
            ptrs = struct.unpack_from("<8I", data, header + 4)
            base = len(mesh["positions"])
            colors = [data[ptrs[7] + i * 4:ptrs[7] + i * 4 + 4] for i in range(vc)]
            for i in range(vc):
                mesh["positions"].append(struct.unpack_from("<3f", data, ptrs[2] + i * 12))
                mesh["normals"].append(struct.unpack_from("<3f", data, ptrs[3] + i * 12))
                mesh["uvs"].append(tuple(c / 4096.0 for c in struct.unpack_from("<2h", data, ptrs[4] + i * 4)))
                mesh["bones"].append((bone, 0, 0))
                mesh["weights"].append((31, 0, 0))
            start = len(mesh["indices"])
            for f in strip_faces([bool(c[3] & 2) for c in colors]):
                mesh["indices"].extend(base + v for v in f)
            if len(mesh["indices"]) > start:
                mesh["objects"].append({"texture": texture, "index_start": start, "index_count": len(mesh["indices"]) - start})
    skel = {"count": node_count, "parents": parent, "local": [r[:3] for r in records],
            "world": [(0.0, 0.0, 0.0)] * node_count}
    return skel, mesh, order, [r[4:7] for r in records]


def effect_records(names: bytes, body: bytes):
    """A demo's own effects ([11] names, [12] PNST body): the layout of the game's effect packs (tools/native_fx.py)."""
    from prepare_skinned_model import pnst_entries
    chunks = pnst_entries(body) if body[:4] == b"PNST" else []
    out, index = {}, 0
    for line in names.decode("ascii", errors="replace").splitlines():
        fields = line.split()
        if len(fields) != 2 or fields[0] not in "GVEPTAMC" or not fields[1].isdigit():
            continue
        kind, number = fields[0], int(fields[1])
        if index + (2 if kind == "M" else 1) > len(chunks):
            break
        out[kind, number] = (chunks[index], chunks[index + 1]) if kind == "M" else chunks[index]
        index += 2 if kind == "M" else 1
    return out


def bake_effects(timeline, work: Path, blob: bytearray, slot_to_actor):
    """The Effect tracks' V and E clips and the bank they need (nfx's NFX_DEMO, psp/testroom/nfx.c). Returns
    (bank offset, clips offset, clip count). V, E, G (emitter) and P (particle program, pfx.c) clips."""
    import native_fx
    from prepare_skinned_model import decode_textures, indexed_texture
    names, body = work / "fx_names.bin", work / "fx_body.bin"
    if not names.exists() or not body.exists():
        return 0, 0, 0
    recs = effect_records(names.read_bytes(), body.read_bytes())
    clips = []
    for track in timeline["tracks"]:
        if track["kind"] not in ("Effect", "EffectI"):
            continue
        for c in track["clips"]:
            kind, number = str(c.get("Id", "")), c.get("No")
            if kind not in ("V", "E", "G", "P") or not isinstance(number, int) or (kind, number) not in recs:
                continue
            # A G clip plays as a V one with its id flagged (native_fx.G_FLAG: G records share the V table).
            clips.append((float(c.get("SetFrame", 0.0)), 1 if kind == "E" else 2 if kind == "P" else 0,
                          number | native_fx.G_FLAG if kind == "G" else number,
                          c.get("Pos") or [0.0, 0.0, 0.0], c.get("Rot") or [0.0, 0.0, 0.0],
                          (c.get("Sca") or [1.0, 1.0, 1.0])[0] if isinstance(c.get("Sca"), list) else 1.0,
                          # SpTrackNo counts the Set section's lines (WorkRate 0, Camera 1, then Model / Motion / Hide per
                          # slot): its slot is (n - 2) / 3. SpFlag -1: not on one.
                          slot_to_actor.get((int(c.get("SpTrackNo") or 0) - 2) // 3, -1)
                          if isinstance(c.get("SpFlag"), int) and c.get("SpFlag") >= 0 else -1,
                          int(c.get("SpBoneNo") or 0)))
    if not clips:
        return 0, 0, 0
    v_ids = sorted({n for _, k, n, *_ in clips if k == 0})
    e_ids = sorted({n for _, k, n, *_ in clips if k == 1})
    try:
        vs, es, _ = native_fx.closure(recs, v_ids, e_ids)
    except KeyError:
        vs, es = set(i for i in v_ids), set(e_ids)
    vs = sorted(v for v in vs if native_fx.has(recs, v))
    es = sorted(e for e in es if ("E", e) in recs)
    # The P records the V / G reach and the P clips, with their sprites' sheets and atlases.
    try:
        ps, p_textures, atlases = native_fx.particles(recs, vs, sorted({n for _, k, n, *_ in clips if k == 2}))
    except (KeyError, struct.error):
        ps, p_textures, atlases = [], [], []
    textures, models = {}, {}
    for t in p_textures:
        try:
            textures.setdefault(t, native_fx.picture(recs["T", t]))
        except Exception:
            pass
    for e in es:
        rec = recs["E", e]
        kind, tex = rec[1], struct.unpack_from("<H", rec, 4)[0]
        if kind == 5:
            m = struct.unpack_from("<H", rec, 0x28)[0]
            if ("M", m) in recs:
                models.setdefault(m, None)
            m = struct.unpack_from("<H", rec, 0x212)[0]
            if rec[0x211] and ("M", m) in recs:   # its textures are model +0x212's (FUN_001f1760, nfx.c draw_model)
                models.setdefault(m, None)
        elif tex != 0xFFFF and ("T", tex) in recs:
            try:
                textures.setdefault(tex, native_fx.picture(recs["T", tex]))
            except Exception:
                pass
    model_tex = {}
    for m in sorted(models):
        mod, tex = recs["M", m]
        if len(tex) > 16:
            try:
                textures[10000 + m] = decode_textures(tex)[0]
                model_tex[m] = 10000 + m
            except Exception:
                pass
    # The bank: u32 v count, e count, texture count, model count, then their tables' offsets; then 'PFX1', the p and
    # a counts and their tables' offsets (nfx_set_demo_bank).
    bank_at = align(blob)
    blob += bytes(64)
    rec_tables = []
    for ids, kind in ((vs, "V"), (es, "E"), (ps, "P"), (atlases, "A")):
        table = align(blob)
        blob += bytes(8 * len(ids))
        for k, i in enumerate(ids):
            at = align(blob, 4)
            blob += native_fx.record(recs, kind, i)
            struct.pack_into("<2I", blob, table + 8 * k, i, at)
        rec_tables.append(table)
    tex_ids = sorted(textures)
    tex_table = align(blob)
    blob += bytes(20 * len(tex_ids))
    for k, tid in enumerate(tex_ids):
        tx = textures[tid]
        pixels, palette = indexed_texture(tx)
        pix = align(blob)
        blob += bytes(pixels)
        pal = align(blob)
        blob += struct.pack("<256I", *palette)
        struct.pack_into("<5I", blob, tex_table + 20 * k, tid, tx["width"], tx["height"], pix, pal)
    model_ids = sorted(models)
    model_table = align(blob)
    blob += bytes(24 * len(model_ids))
    for k, m in enumerate(model_ids):
        verts, idx = native_fx.mesh(recs["M", m][0])
        vat = align(blob)
        for v in verts:
            blob += struct.pack("<8f", *v)
        iat = align(blob)
        blob += struct.pack(f"<{len(idx)}H", *idx)
        tex_index = tex_ids.index(model_tex[m]) if m in model_tex else -1
        struct.pack_into("<3I2Ii", blob, model_table + 24 * k, m, len(verts), len(idx), vat, iat, tex_index)
    struct.pack_into("<8I", blob, bank_at, len(vs), len(es), len(tex_ids), len(model_ids),
                     rec_tables[0], rec_tables[1], tex_table, model_table)
    struct.pack_into("<4s4I", blob, bank_at + 32, b"PFX1", len(ps), len(atlases), rec_tables[2], rec_tables[3])
    # Clips: {f32 start, u32 kind (0 V, 1 E, 2 P), id, f32 pos[3], rot[3], scale, i32 actor, bone; 48 bytes}, by start.
    clips.sort(key=lambda c: c[0])
    clip_at = align(blob)
    for start, kind, number, pos, rot, scale, actor, bone in clips:
        blob += struct.pack("<f2I7f2i", start, kind, number, *[float(x) for x in pos[:3]],
                            *[float(x) for x in rot[:3]], float(scale), actor, bone)
    print(f"  effects: {len(clips)} clips, {len(vs)} V, {len(es)} E, {len(ps)} P, {len(tex_ids)} textures, {len(model_ids)} models")
    return bank_at, clip_at, len(clips)


def align(blob: bytearray, n: int = 16) -> int:
    while len(blob) % n:
        blob.append(0)
    return len(blob)


AXIS_CODE = {"Y": 0, "X": 1, "NY": 2, "NX": 3, "Z": 4, "NZ": 5}


def pack_cloth_block(model_clips, motion_clips, work: Path, bone_count: int, label: str) -> bytes:
    """The slot's chains (docs/CUTSCENES.md). A model clip's .clt is the chain until a motion clip names another.
    ResetClt / ClothMode are events on the demo clock. A bad Bone line is skipped, not fatal."""
    import prepare_skinned_model as psm
    if work is None:
        return b""
    scripts, index = [], {}

    def add_script(folder, name):
        key = (folder, name)
        if key in index:
            return index[key]
        path = work / folder / name
        if not path.exists():
            print(f"    {label} cloth {folder}/{name}: missing")
            index[key] = -1
            return -1
        text = path.read_text(encoding="latin1")
        # A line can carry two Bone records ("Bone 6 Bone 8 X", demo 53's motion pl101_01.clt). The first
        # one's axis token is the next record's keyword, so it takes the next bone's axis.
        fixed = []
        for line in text.splitlines():
            parts = line.split()
            if not parts or parts[0] != "Bone" or parts.count("Bone") < 2:
                fixed.append(line)
                continue
            idxs = [i for i, token in enumerate(parts) if token == "Bone"]
            chunks = [parts[i: idxs[n + 1] if n + 1 < len(idxs) else None] for n, i in enumerate(idxs)]
            known = [c[2] if len(c) >= 3 and c[2] in AXIS_CODE else None for c in chunks]
            for n, chunk in enumerate(chunks):
                if len(chunk) < 2:
                    continue
                axis = known[n] or next((a for a in known[n + 1:] if a), None) or next((a for a in known if a), "Y")
                if known[n] is None:
                    print(f"    {label} {name}: Bone {chunk[1]} takes axis {axis}")
                fixed.append(f"Bone {chunk[1]} {axis}")
        text = "\n".join(fixed)
        try:
            cloths = psm.parse_cloths(text)
        except (OSError, ValueError, IndexError) as error:
            print(f"    {label} cloth {name}: {error}")
            index[key] = -1
            return -1
        chains = []
        for cloth in cloths:
            bones, axes = [], []
            for bone, axis in zip(cloth.get("bones", []), cloth.get("axes", [])):
                code = AXIS_CODE.get(axis)
                if code is None or not isinstance(bone, int) or bone < 0 or bone >= bone_count:
                    print(f"    {label} {name}: bone {bone} axis {axis} skipped")
                    continue
                bones.append(bone)
                axes.append(code)
            if not bones:
                continue
            grav = cloth.get("Gravity", [0.0, -0.2, 0.0]) or [0.0, -0.2, 0.0]
            gy = float(grav[1] if len(grav) > 1 else grav[0])
            spring = float((cloth.get("SpringForce") or [0.05])[0])
            speed = float((cloth.get("MaxSpeed") or [50.0])[0])
            stiff = float((cloth.get("Stiffness") or [0.3])[0])
            limit = int(float((cloth.get("LimitLength") or [1])[0]))
            tail = bytes(bones) + bytes(axes)
            tail += bytes((-len(tail)) % 4)
            chains.append(struct.pack("<4fiI", gy, spring, speed, stiff, limit, len(bones)) + tail)
        if not chains:
            index[key] = -1
            return -1
        index[key] = len(scripts)
        scripts.append(struct.pack("<I", len(chains)) + b"".join(chains))
        return index[key]

    events = []

    def add_events(clips, folder, order0):
        for n, clip in enumerate(clips):
            name = clip.get("Cloth")
            script = -1
            if isinstance(name, str) and name.lower().endswith(".clt"):
                script = add_script(folder, name)
            mode = clip.get("ClothMode")
            mode = int(mode) if isinstance(mode, (int, float)) and not isinstance(mode, bool) else -1
            reset = 2 if "ResetClt2" in clip else 1 if "ResetClt" in clip else 0
            level = clip.get("ResetCltLevel")
            level = int(level) if isinstance(level, (int, float)) and not isinstance(level, bool) else 0
            if script < 0 and mode < 0 and not reset:
                continue
            start, end = clip_span(clip)
            events.append((start, order0 + n, end, script, mode, reset, level))

    add_events(model_clips, "model", 0)
    add_events(motion_clips, "motion", 1000)
    if not scripts:
        return b""
    events.sort(key=lambda e: (e[0], e[1]))
    out = struct.pack("<2I", len(scripts), len(events)) + b"".join(scripts)
    for start, _order, end, script, mode, reset, level in events:
        out += struct.pack("<2f4i", float(start), float(end), script, mode, reset, level)
    print(f"    {label} cloth: {len(scripts)} scripts, {len(events)} clips")
    return out


def bake_cast(timeline, files, blob: bytearray, work: Path = None):
    """Each model slot with a .mod: its skeleton, slimmed mesh, objects on the shared textures, its CnsPos and its
    motion clips (raw MOT). Returns (texture table offset, count, actor table offset, count)."""
    import prepare_skinned_model as psm
    from prepare_stage import swizzle8
    tracks = {(t["kind"], t["slot"]): t for t in timeline["tracks"]}
    textures, decoded, texture_ids = [], {}, {}

    def texture_set(name):
        """The set's textures, decoded once; each goes in the table only when an object uses it (texture_id)."""
        if name not in decoded:
            decoded[name] = psm.decode_textures(files[name])
        return decoded[name]

    def texture_id(name, k):
        sets = texture_set(name)
        k = k if k < len(sets) else 0
        if (name, k) not in texture_ids:
            tx = sets[k]
            pixels, palette = psm.indexed_texture(tx)
            texture_ids[(name, k)] = len(textures)
            textures.append((tx["width"], tx["height"], bytes(pixels), palette))
        return texture_ids[(name, k)]

    actors = []
    for slot in range(64):
        model = tracks.get(("Model", slot))
        if not model or not model["clips"]:
            continue
        clip = model["clips"][0]
        name = str(clip.get("Model", ""))
        lower = name.lower()
        is_piece = lower.endswith(".scm") and not lower.startswith("st")   # the stage proper is DSTG
        if not (lower.endswith((".mod", ".efm")) or is_piece) or name not in files or not files[name]:
            continue
        order = rest_rot = None
        try:
            if is_piece:
                skel, mesh, order, rest_rot = scm_rig(files[name])
                if skel["count"] > 48 or not mesh["positions"] or len(mesh["positions"]) > 4000:
                    continue   # a piece that big (a whole sub-stage) costs too much memory and skinning: left out
            else:
                data = files[name]
                if data[:4] == b"EFM ":
                    data = b"MOD " + data[4:]   # an effect model: the MOD layout (as the enemies' are read)
                skel = psm.parse_skeleton(data)
                # Objects kept apart, in the MOD's order: a cut's hide track keys them one by one.
                mesh = psm.slim_mesh(psm.build_mesh(data, skel["count"]), 0.7)
        except ValueError as error:   # a rig the converter doesn't take (a parent after its child): left out
            print(f"  slot {slot:2d} {name}: skipped ({error})")
            continue
        tex = texture_set(clip["Texture"]) if clip.get("Texture") in files else []
        tex_name = clip.get("Texture") if tex else None
        if is_piece and not tex:
            continue   # its textures are the room's (not in the package)
        motions = []
        for mclip in (tracks.get(("Motion", slot)) or {"clips": []})["clips"]:
            mot = mclip.get("Motion")
            if mot and files.get(mot):
                a, b = clip_span(mclip)
                motions.append((a, b, float(mclip.get("ClipScale", 1.0) or 1.0), float(mclip.get("SkipFrame", 0.0)), files[mot]))
        # Its hide track: DispOn / DispOff switch the whole slot at a clip's start (a cut's own .hid, which keys
        # each of the model's objects, counts as on [its per-object channels: not yet]).
        switches = []   # (start, scale, mode 0 off / 1 on / 2 per object, first frame, channels)
        for hclip in (tracks.get(("Hide", slot)) or {"clips": []})["clips"]:
            hid = str(hclip.get("Hide", "") or "")
            start, scale = float(hclip.get("SetFrame", 0.0)), float(hclip.get("ClipScale", 1.0) or 1.0)
            if not hid:
                # HideKind without a file: H hides the slot, D shows it (A plays a .hid). These were skipped, so
                # pieces meant to stay hidden showed: demo 3's ss001_01 (slot 35, hidden until 2964), its
                # coats in slots 32 / 33.
                kind = str(hclip.get("HideKind", "")).upper()[:1]
                if kind in ("H", "D"):
                    switches.append((start, scale, 0 if kind == "H" else 1, 0.0, []))
                continue
            if hid.lower() in ("dispoff.hid", "dispon.hid") or not files.get(hid):
                switches.append((start, scale, 0 if hid.lower() == "dispoff.hid" else 1, 0.0, []))
            else:
                try:
                    first, channels = parse_hid(files[hid])
                    switches.append((start, scale, 2, first, channels))
                except (ValueError, struct.error):   # not a hide track we read: shown
                    switches.append((start, scale, 1, 0.0, []))
        switches.sort(key=lambda s: s[0])
        cns = clip.get("CnsPos")
        cloth = pack_cloth_block(model["clips"], (tracks.get(("Motion", slot)) or {"clips": []})["clips"],
                                 work, skel["count"], name)
        actors.append({"slot": slot, "name": name, "skel": skel, "mesh": mesh, "tex": tex, "motions": motions,
                       "parent": cns[0] if isinstance(cns, list) else -1, "bone": cns[1] if isinstance(cns, list) else 0,
                       "pos": clip.get("Pos") or [0.0, 0.0, 0.0], "rot": clip.get("Rot") or [0.0, 0.0, 0.0],
                       "span": clip_span(clip), "switches": switches, "order": order, "rest_rot": rest_rot,
                       "tex_name": tex_name, "cloth": cloth})

    for a in actors:   # the textures its objects use, in the table before it's written
        a["tex_ids"] = {o["texture"]: texture_id(a["tex_name"], o["texture"]) for o in a["mesh"]["objects"]} if a["tex_name"] else {}
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
            # Bits 16-19: the object's blend (its MOD record's byte 8, prepare_skinned_model.build_mesh).
            t = a["tex_ids"].get(o["texture"], 0) | (o.get("mode", 0) & 0xF) << 16
            blob += struct.pack("<3I", t, o["index_start"], o["index_count"])
        motion_at = align(blob)
        blob += bytes(32 * len(a["motions"]))
        for k, (s, e, scale, skip, data) in enumerate(a["motions"]):
            pending.append((motion_at + 32 * k, s, e, scale, skip, data))
        # Hide clips: {f32 start, scale, first frame, u32 mode, channel table, object count; 24 bytes}; a channel
        # table: per object {u32 key count, keys offset}; keys {f32 frame, u32 shown}.
        tables = []
        for start, scale, mode, first, channels in a["switches"]:
            table_at = 0
            if mode == 2:
                key_at = []
                for keys in channels:
                    if keys:
                        key_at.append((len(keys), align(blob)))
                        for frame, shown in keys:
                            blob += struct.pack("<fI", float(frame), 1 if shown else 0)
                    else:
                        key_at.append((0, 0))
                table_at = align(blob)
                for count, off in key_at:
                    blob += struct.pack("<2I", count, off)
            tables.append(table_at)
        switch_at = align(blob)
        for (start, scale, mode, first, channels), table_at in zip(a["switches"], tables):
            blob += struct.pack("<3f3I", start, scale, first, mode, table_at, len(channels))
        struct.pack_into("<2I", blob, actor_table + 128 * i + 104, len(a["switches"]), switch_at)
        if a["order"]:
            order_at = align(blob)
            blob += bytes(a["order"])
            rest_at2 = align(blob)
            for r in a["rest_rot"]:
                blob += struct.pack("<3f", *r)
            struct.pack_into("<2I", blob, actor_table + 128 * i + 112, order_at, rest_at2)
        parent = slot_to_actor.get(a["parent"], -1)
        struct.pack_into("<8i10I6f2f", blob, actor_table + 128 * i,
                         a["slot"], parent, a["bone"], n, v, len(mesh["indices"]), len(mesh["objects"]), len(a["motions"]),
                         parents_at, local_at, rest_at, vert_at, skin_at, index_at, object_at, motion_at,
                         centre_bone, struct.unpack("<I", struct.pack("<f", radius))[0],
                         *a["pos"][:3], *a["rot"][:3], *a["span"])
        if a.get("cloth"):
            cloth_at = align(blob)
            blob += a["cloth"]
            struct.pack_into("<2I", blob, actor_table + 128 * i + 120, cloth_at, len(a["cloth"]))
        on = "" if parent < 0 else f" on slot {a['parent']} bone {a['bone']}"
        print(f"  slot {a['slot']:2d} {a['name']:14s} {n:2d} bones {v:5d} vertices {len(a['motions']):3d} motions{on}")
    fx = bake_effects(timeline, work, blob, slot_to_actor) if work else (0, 0, 0)
    resident = align(blob)
    for entry, s, e, scale, skip, data in pending:
        at = align(blob)
        blob += data
        struct.pack_into("<4f2I", blob, entry, s, e, scale, skip, at, len(data))
    return tex_table, len(textures), actor_table, len(actors), resident, fx


def subtitles(iso_path: Path, demo: int, timeline):
    """The Message track's lines, in English (the record's second language file: Japanese, English, then two more):
    each "# Msg n" block's text, its <PS x y> place (PS2 640 x 448 screen) and <SZ size>, shown from its clip's frame
    for the sum of its <WT frames> waits (<IT 0>: all at once; <FO 0> then clears it)."""
    elf = demo_unpack.gen_enemy_ai.Elf(demo_unpack.EXE.read_bytes())
    rec = demo_unpack.demo_record(elf, demo)
    clips = [c for t in timeline["tracks"] if t["kind"] == "Message" for c in t["clips"] if c.get("No") is not None]
    if not rec or not clips or rec[3] in (0, 0xFFFFFFFF):
        return []
    with demo_unpack.psm.Iso9660(iso_path) as iso:
        gdata = demo_unpack.psm.AfsArchive(iso, "GDATA.AFS")
        e = gdata.entries[rec[3] >> 16]
        text = iso.read_file("GDATA.AFS", e["offset"], e["size"]).decode("latin1").replace("\r", "")
    msgs = {int(n): body for n, body in re.findall(r"# Msg (\d+)\n(.*?)# End", text, re.S)}
    out = []
    for c in clips:
        body = msgs.get(int(c["No"]))
        if body is None:
            continue
        s = "".join(re.findall(r'"([^"]*)"', body))
        pos = re.search(r"<PS (\d+) (\d+)>", s)
        size = re.search(r"<SZ (\d+)>", s)
        wait = sum(int(w) for w in re.findall(r"<WT (\d+)>", s)) or 120
        line = re.sub(r"<BR>", "\n", s)
        line = re.sub(r"<[^>]*>", "", line).strip()
        start = float(c.get("SetFrame", 0.0))
        out.append((start, start + wait, int(pos.group(1)) if pos else 0, int(pos.group(2)) if pos else 420,
                    int(size.group(1)) if size else 22, line))
    return out


def bake_audio(iso_path: Path, timeline, out: Path, demo: int):
    """The demo's sound: SDATA.AFS's "<package>.adx" (its Sound clip names stream = the demo; the file is named after
    the demo's package, m03_s00 for demo 5), decoded by ffmpeg and stored as the movies' IMA ADPCM
    (tools/prepare_movies.py): DAUD_nnn.BIN = "DAU1", u32 rate, u32 sample pairs, f32 start frame, then a byte per
    stereo sample pair (left in the low nibble). Returns its path or None."""
    import subprocess as sp
    import prepare_movies
    track = next((t for t in timeline["tracks"] if t["kind"] == "Sound"), None)
    if not track or not track["clips"]:
        return None
    package = track["file"].replace("_SoundSet.txt", "")
    start = float(track["clips"][0].get("SetFrame", 0.0))
    with demo_unpack.psm.Iso9660(iso_path) as iso:
        sdata = demo_unpack.psm.AfsArchive(iso, "SDATA.AFS")
        entry = next((e for e in sdata.entries if e["name"].lower() == f"{package.lower()}.adx"), None)
        if not entry:
            return None
        adx = iso.read_file("SDATA.AFS", entry["offset"], entry["size"])
    rate = 44100
    pcm = sp.run([prepare_movies.FFMPEG, "-v", "error", "-f", "adx", "-i", "pipe:0", "-f", "s16le", "-ac", "2",
                  "-ar", str(rate), "pipe:1"], input=adx, capture_output=True, check=True).stdout
    samples = struct.unpack(f"<{len(pcm) // 2}h", pcm[:len(pcm) // 4 * 4])
    nibbles = prepare_movies.ima_encode(samples)[:len(samples) // 2]
    path = out / f"DAUD_{demo:03d}.BIN"
    path.write_bytes(struct.pack("<4s2If", b"DAU1", rate, len(samples) // 2, start) + bytes(nibbles))
    print(f"  sound: {package}.adx, {len(samples) / 2 / rate:.1f} s from frame {start:.0f} -> {path.name}")
    return path


def stage_hide_events(timeline, files):
    """The stage slot's Hide clips as (demo frame, SCM object, hidden) events in order (see main)."""
    slot = next((t["slot"] for t in timeline["tracks"] if t["kind"] == "Model" for c in t["clips"]
                 if str(c.get("Model", "")).lower().startswith("st") and str(c.get("Model", "")).lower().endswith(".scm")), None)
    track = next((t for t in timeline["tracks"] if t["kind"] == "Hide" and t["slot"] == slot), None)
    if slot is None or not track:
        return []
    events = []
    for clip in sorted(track["clips"], key=lambda c: float(c.get("SetFrame", 0.0))):
        start, scale = float(clip.get("SetFrame", 0.0)), float(clip.get("ClipScale", 1.0) or 1.0)
        hid = str(clip.get("Hide", "") or "")
        channels = []
        if hid and hid.lower() not in ("dispon.hid", "dispoff.hid") and files.get(hid):
            try:
                first, channels = parse_hid(files[hid])
            except (ValueError, struct.error):
                channels = []
        if not hid and str(clip.get("HideKind", "")).upper()[:1] == "H":
            events.append((start, 0xfffe, 1))   # HideKind H: the whole stage hidden
            continue
        events.append((start, 0xffff, 0))   # a clip begins: every object shown, then its own keys
        for obj, keys in enumerate(channels):
            if not keys:
                continue
            events.append((start, obj, 0 if keys[0][1] else 1))   # before its first key: that key's state
            for local, shown in keys:
                events.append((start + max(0.0, local - first) * scale, obj, 0 if shown else 1))
    events.sort(key=lambda e: e[0])
    return events


def piece_prop_events(timeline, files, room_bin):
    """A cutscene staged in a game room: its ss pieces textured from the room (left out of the cast) are the room's
    own placed props. Each prop batch (8th word 256 + its index) whose bounds sit inside a piece's follows that piece's
    H / D (DispOff / DispOn) hide clips: (demo frame, prop tag - 1, hidden). Demo 3's ss001_01 is hidden until 2964."""
    if not room_bin or not room_bin.exists():
        return []
    s = room_bin.read_bytes()
    hdr = struct.unpack_from("<4s7I4f6f6I2f2I", s, 0)
    batches, batch_table, chunk_table = hdr[3], hdr[19], hdr[-1]
    props = {}
    for i in range(batches):
        row = struct.unpack_from("<8I", s, batch_table + 32 * i)
        if row[7] < 256:
            continue
        for k in range(row[4], row[4] + row[5]):
            c = struct.unpack_from("<2I6f", s, chunk_table + 32 * k)
            lo, hi = props.setdefault(row[7], ([1e30] * 3, [-1e30] * 3))
            for a in range(3):
                lo[a] = min(lo[a], c[2 + a]); hi[a] = max(hi[a], c[5 + a])
    events = []
    for track in timeline["tracks"]:
        if track["kind"] != "Model" or not track["clips"]:
            continue
        clip = track["clips"][0]
        name = str(clip.get("Model", ""))
        if not name.lower().endswith(".scm") or name.lower().startswith("st") or clip.get("Texture") in files \
                or not files.get(name):
            continue
        try:
            _, mesh, _, _ = scm_rig(files[name])
        except (ValueError, struct.error):
            continue
        ps = mesh["positions"]
        plo = [min(p[a] for p in ps) - 100.0 for a in range(3)]
        phi = [max(p[a] for p in ps) + 100.0 for a in range(3)]
        tags = [t for t, (lo, hi) in props.items()
                if all(plo[a] <= (lo[a] + hi[a]) * 0.5 <= phi[a] for a in range(3))
                and all(hi[a] - lo[a] <= (phi[a] - plo[a]) * 1.2 for a in range(3))]
        hide = next((t for t in timeline["tracks"] if t["kind"] == "Hide" and t["slot"] == track["slot"]), None)
        for h in sorted((hide or {"clips": []})["clips"], key=lambda c: float(c.get("SetFrame", 0.0))):
            hid = str(h.get("Hide", "") or "").lower()
            kind = "H" if hid == "dispoff.hid" else "D" if hid == "dispon.hid" else str(h.get("HideKind", "")).upper()[:1]
            if kind in ("H", "D"):
                events += [(float(h.get("SetFrame", 0.0)), t - 1, 1 if kind == "H" else 0) for t in tags]
        if tags:
            print(f"  piece {name} (slot {track['slot']}): the room's props {[t - 256 for t in tags]}")
    return events


LIGHT_RECORD = 80


def bake_lights(timeline) -> bytes:
    """The Light track (FUN_00218fc0 parses a clip; FUN_002569c0 starts it as a light, FUN_0027fb10 runs it): each
    clip {f32 start, f32 frames (Life / ClipScale: its life counts down by ClipScale a frame), u8 ValidType, IsMinus,
    Type (1 eases Src to Dst over its life), pad; Src pos[3], force, range, rgb[3]; Dst the same; pad} = 80 bytes."""
    out = bytearray()
    clips = sorted((c for t in timeline["tracks"] if t["kind"] == "Light" for c in t["clips"]),
                   key=lambda c: float(c.get("SetFrame", 0.0)))
    vec = lambda v, n: [float(x) for x in (v if isinstance(v, list) else [0.0] * n)[:n]] + [0.0] * max(0, n - len(v if isinstance(v, list) else []))
    for c in clips:
        scale = float(c.get("ClipScale", 1.0) or 1.0)
        out += struct.pack("<2f4B", float(c.get("SetFrame", 0.0)), max(float(c.get("Life", 1.0)) / scale, 1.0),
                           int(c.get("ValidType", 0)) & 0xff, 1 if int(c.get("IsMinus", 0)) else 0,
                           1 if int(c.get("Type", 0)) == 1 else 0, 0)
        for side in ("Src", "Dst"):
            out += struct.pack("<3f2f3f", *vec(c.get(side + "Pos"), 3), float(c.get(side + "Force", 0.0)),
                               float(c.get(side + "Range", 0.0)), *vec(c.get(side + "Rgb"), 3))
        out += bytes(LIGHT_RECORD - 12 - 64)
    return bytes(out)


FOG_RECORD = 24


def bake_fog(timeline) -> bytes:
    """ScrEfc GFG clips. The runtime maps MinAlpha/128 at StartZ to MaxAlpha/128 at EndZ onto sceGuFog."""
    out = bytearray()
    clips = sorted((c for t in timeline["tracks"] if t["kind"] == "ScrEfc" for c in t["clips"]
                    if str(c.get("Id", "")) == "GFG"), key=lambda c: float(c.get("SetFrame", 0.0)))
    for c in clips:
        scale = float(c.get("ClipScale", 1.0) or 1.0)
        life = max(float(c.get("Life", 1.0)) / scale, 1.0)
        color = int(c.get("MaxColor", 0)) & 0xFFFFFFFF
        min_a = max(0, min(255, int(float(c.get("MinAlpha", 0)))))
        max_a = max(0, min(255, int(float(c.get("MaxAlpha", 0)))))
        kind = int(float(c.get("Type", 1))) & 0xFF
        out += struct.pack("<4fI4B", float(c.get("SetFrame", 0.0)), life, float(c.get("StartZ", 0.0)),
                           float(c.get("EndZ", 0.0)), color, min_a, max_a, kind, 0)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso", type=Path)
    parser.add_argument("demo", type=int)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "testroom")
    parser.add_argument("--audio-only", action="store_true", help="only its sound (DAUD_nnn.BIN)")
    parser.add_argument("--no-stage", action="store_true", help="write DEMO_nnn.BIN only; leave DSTG_nnn.BIN alone")
    args = parser.parse_args()
    # DMC3_DEMO_WORK: unpack into another folder (build/demo can be a shared cache)
    work = Path(os.environ.get("DMC3_DEMO_WORK") or ROOT / "build" / "demo") / str(args.demo)
    demo_unpack.unpack(args.iso, args.demo, work)
    timeline = json.loads((work / "timeline.json").read_text(encoding="utf-8"))
    if args.audio_only:
        args.output.mkdir(parents=True, exist_ok=True)
        bake_audio(args.iso, timeline, args.output, args.demo)
        return
    files = {p.name: p.read_bytes() for d in work.iterdir() if d.is_dir() for p in d.iterdir()}

    camera = bake_camera(timeline, files)
    blob = bytearray(64)
    cam_at_offset = len(blob)
    for eye, target, fov, roll in camera:
        blob += struct.pack("<8f", *eye, *target, fov, roll)
    subs = subtitles(args.iso, args.demo, timeline)
    sub_at = len(blob)
    for start, end, x, y, size, line in subs:
        raw = line.encode("latin1", "replace")[:111]
        blob += struct.pack("<2f3h2x112s", start, end, x, y, size, raw)
    # The Fade track: {from, length, 0 in / 1 out, RGBA (128 = opaque)}, by start.
    fades = sorted((float(c.get("SetFrame", 0.0)), float(c.get("Life", 1.0)) * float(c.get("ClipScale", 1.0) or 1.0),
                    1 if str(c.get("FadeType", "O")).upper().startswith("O") else 0,
                    [int(v) for v in (c.get("DistRGBA") if isinstance(c.get("DistRGBA"), list) else [0, 0, 0, 128])])
                   for t in timeline["tracks"] if t["kind"] == "Fade" for c in t["clips"])
    fade_at = len(blob)
    for start, life, out, rgba in fades:
        blob += struct.pack("<2fI4B", start, max(life, 1.0), out, *[min(255, v) for v in rgba[:4]])
    lights = bake_lights(timeline)
    blob += lights   # right after the fades; their count at +0x3c
    # Then the Program track's colour clips: u32 count, then {f32 start, u32 No, f32 Arg[4]} by start. No 5 the
    # characters' colour scale (128 = 1.0), No 6 their ambient [inferred: from the values, not the parser's code].
    programs = sorted((float(c.get("SetFrame", 0.0)), int(c.get("No", -1)), [float(x) for x in (c.get("Arg") or [0, 0, 0, 0])[:4]])
                      for t in timeline["tracks"] if t["kind"] == "Program" for c in t["clips"] if int(c.get("No", -1)) in (5, 6))
    blob += struct.pack("<I", len(programs))
    for start, no, arg in programs:
        blob += struct.pack("<fI4f", start, no, *(arg + [0.0] * (4 - len(arg))))
    fogs = bake_fog(timeline)
    blob += fogs   # right after the Program clips; their count is the high half of +0x3c
    # Then, after the fogs, the stage's own hide track (its model slot's Hide clips: a cut's .hid keys the stage's SCM objects one by
    # one; demo 3 hides the walls in front of its later cameras): u32 count, then {f32 demo frame, u16 object, u16
    # hidden}, by frame. A clip without a .hid shows them all. The stage's batches carry object + 1 (prepare_stage).
    events = stage_hide_events(timeline, files)
    room_match = re.match(r"st(\d+)", next((str(c.get("Model", "")).lower() for t in timeline["tracks"] if t["kind"] == "Model"
                                            for c in t["clips"] if str(c.get("Model", "")).lower().startswith("st")), ""))
    if room_match and not (work / "model" / f"st{room_match.group(1)}.scm").exists():
        events = sorted(events + piece_prop_events(timeline, files, args.output / f"ROOM_{room_match.group(1)}.BIN"),
                        key=lambda e: e[0])
    blob += struct.pack("<I", len(events))
    for frame, obj, hidden in events:
        blob += struct.pack("<f2H", frame, obj, hidden)
    tex_at, tex_count, actor_at, actor_count, resident, fx = bake_cast(timeline, files, blob, work)
    struct.pack_into("<4s11I", blob, 0, b"DMO3", len(camera), cam_at_offset, tex_at, tex_count, actor_at, actor_count,
                     resident, sub_at, len(subs), fade_at, len(fades))
    struct.pack_into("<3I", blob, 0x30, *fx)   # +0x30 effect bank, effect clips, clip count (0: none)
    struct.pack_into("<I", blob, 0x3c, (len(lights) // LIGHT_RECORD) | ((len(fogs) // FOG_RECORD) << 16))
    print(f"  {len(subs)} subtitles" + (f": {subs[0][5][:40]!r} ..." if subs else ""))
    args.output.mkdir(parents=True, exist_ok=True)
    out = args.output / f"DEMO_{args.demo:03d}.BIN"
    out.write_bytes(blob)

    if args.no_stage:
        print(f"  fog clips: {len(fogs) // FOG_RECORD}")
        return
    # The stage: slot 00's .scm clip, its texture, built as a room; its play area the camera's reach.
    # The stage proper (stNNN.scm), not its pieces (ss...scm).
    scms = [c for t in timeline["tracks"] if t["kind"] == "Model" for c in t["clips"]
            if str(c.get("Model", "")).lower().endswith(".scm")]
    stage_clip = next((c for c in scms if str(c["Model"]).lower().startswith("st")), scms[0] if scms else None)
    room_bin = None
    if stage_clip and not ((work / "model" / stage_clip["Model"]).exists() and (work / "model" / str(stage_clip.get("Texture"))).exists()):
        # A room's own stage (st232 is room 232): the room's BIN as built for the missions (tools/gen_rooms.py).
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
        view_file = work / "view.bin"   # camera samples for prepare_stage --view
        with open(view_file, "wb") as vf:
            for k in range(0, len(camera), 8):
                e, t, fov, _ = camera[k]
                vf.write(struct.pack("<7f", *e, *t, fov))
        for edge, crop in ((500, 0), (1000, 0), (2000, 0), (4000, 0), (100000, 0), (2000, -1), (100000, -1),
                           (100000, 30000), (100000, 15000), (100000, 8000), (100000, 4000)):
            subprocess.run([sys.executable, str(ROOT / "tools" / "prepare_stage.py"), str(args.iso),
                            "--output", str(stage_out),
                            "--scm", str(work / "model" / stage_clip["Model"]),
                            "--ptx", str(work / "model" / stage_clip["Texture"]),
                            "--play", *[f"{v:.1f}" for v in lo + hi], "--max-edge", str(edge)]
                           + (["--crop", str(crop)] if crop > 0 else []) + (["--view", str(view_file)] if crop else []),
                           check=True)
            if stage_out.stat().st_size <= budget:
                break
            print(f"stage {stage_out.stat().st_size:,} bytes over its {budget:,}: coarser")
        if stage_out.stat().st_size > budget:
            print(f"WARNING: {stage_out.name} doesn't fit ({stage_out.stat().st_size:,} > {budget:,})")
    bake_audio(args.iso, timeline, args.output, args.demo)
    e0, t0, fov0, roll0 = camera[0]
    print(f"{out}: {len(camera)} frames; first eye {[round(v) for v in e0]} target {[round(v) for v in t0]} "
          f"fov {fov0:.1f} ch13 {roll0:.2f}")


if __name__ == "__main__":
    main()
