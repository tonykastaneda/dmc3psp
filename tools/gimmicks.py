#!/usr/bin/env python3
"""A room's gimmick props, placed by its "# GAME" sets: what prepare_stage.py bakes into a room and
gen_rooms.py lists as breakables, so both number them the same way.

Stage PAC [5] is a PNST: 'PNST', u32 count (models x 10), u32 offsets (0 = empty). Model N's geometry is slot
10N, an SCM in model space (origin at the prop's foot) whose sub-meshes use the stage's own textures (PAC [1]).
Slot 10N+1, when there, is the motion of the broken pieces (not used yet). A set places a model at
world = pos + Ry * Rx * Rz * (scale * v), angles in degrees. Decoded in build/hdref/gimmick/.
"""
import math
import re
import struct

from scm_ps2 import parse_scm

# The sets drawn: breakables (BREAK; ORBREAK, the red orb crystals) and the static props (STAY). Lifts,
# switches and seals have their own stand-ins and aren't drawn from here yet.
DRAWN = ("BREAK", "ORBREAK", "STAY")
BREAKABLE = ("BREAK", "ORBREAK")


def models(pnst):
    """{model number: SCM bytes}."""
    if pnst[:4] != b"PNST":
        return {}
    count = struct.unpack_from("<I", pnst, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", pnst, 8)
    present = sorted(o for o in offsets if o) + [len(pnst)]
    out = {}
    for k in range(0, count, 10):
        o = offsets[k]
        if o:
            out[k // 10] = pnst[o:min(x for x in present if x > o)]
    return out


def _numbers(rest):
    return [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", rest)]


def parse_game(text):
    """Every "# SET" of a room's "# GAME" text: kind, model, pos, rot, scale, vital, and its item rolls
    [(name, amount, chance %)]."""
    sets = []
    for block in re.split(r"\n(?=# SET)", text.replace("\r", "")):
        if not block.startswith("# SET"):
            continue
        first = block.split("\n")[0]
        try:   # the comments are Shift-JIS; the text arrives read as Latin-1
            comment = first.split(";", 1)[1].strip().encode("latin1").decode("shift_jis") if ";" in first else ""
        except (UnicodeEncodeError, UnicodeDecodeError):
            comment = ""
        lines = [l.split(";")[0].strip() for l in block.split("\n")]
        head = lines[0].split()
        number = int(head[2]) if len(head) > 2 and head[2].isdigit() else -1
        s = {"index": len(sets), "number": number, "comment": comment,
             "kind": head[3] if len(head) > 3 else "", "model": None, "bmodel": None, "pos": (0.0, 0.0, 0.0),
             "rot": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0), "vital": None, "items": [],
             "box": None, "hit_rot": (0.0, 0.0, 0.0), "hitattr": []}
        for l in lines[1:]:
            if not l:
                continue
            key = l.split()[0]
            rest = l[len(key):]
            if key in ("model", "bmodel"):
                n = _numbers(rest)
                s[key] = int(n[0]) if n else None
            elif key in ("pos", "rot", "scale"):
                n = _numbers(rest)
                if len(n) >= 3:
                    s[key] = tuple(n[:3])
            elif key == "vital":
                n = _numbers(rest)
                s["vital"] = n[0] if n else None
            elif key == "hit" and rest.split() and rest.split()[0] == "box":
                n = _numbers(rest)
                if len(n) >= 6:
                    s["box"] = tuple(n[:6])   # centre (relative to pos for props), then half sizes
            elif key == "hit_rot":
                n = _numbers(rest)
                if len(n) >= 3:
                    s["hit_rot"] = tuple(n[:3])
            elif key == "hitattr":
                s["hitattr"].append(" ".join(rest.split()))
            elif key == "item":
                parts = rest.split()
                n = _numbers(" ".join(parts[1:]))
                if parts and len(n) >= 2:
                    s["items"].append((parts[0], int(n[0]), int(n[1])))
        sets.append(s)
    return sets


def model_triangles(data, static_efm=False):
    """Model space triangles: [((x, y, z, u, v, r, g, b) x 3, texture)]. SCMs, plus optional rest-pose EFM stage meshes.
    Other effect models and MODs give nothing."""
    if static_efm and data[:4] == b"EFM ":
        # Leviathan's tunnel walls are animated MOD-layout effect meshes.
        # Bake their rest geometry; the room's UV and bone animation is not
        # yet available in stage.c. Other missions retain the SCM-only path.
        from mod_ps2 import parse_mod
        from mod_skeleton import strip_faces
        raw = b"MOD " + data[4:]
        objects, _ = parse_mod(raw, split_submeshes=True)
        out = []
        for obj in objects:
            words = struct.unpack_from("<%dH" % obj["vertex_count"], raw, obj["weight_offset"])
            # Some EFM streams end with zero-weight padding. They are not
            # skinned here; omit triangles touching padding and retain ADC.
            for face in strip_faces([bool(w & 0x8000) for w in words]):
                if any((words[i] & 0x7fff) == 0 for i in face):
                    continue
                tri = []
                for i in face:
                    shade = 112.0 + 32.0 * max(0.0, obj["normals"][i][1])
                    tri.append(tuple(obj["positions"][i]) + tuple(obj["uvs"][i]) + (shade, shade, shade))
                out.append((tuple(tri), obj["header_count"]))
        return out
    if data[:4] != b"SCM ":
        return []
    out = []
    for obj in parse_scm(data, True)["objects"]:
        for m in obj["submeshes"]:
            for f in m["faces"]:
                out.append((tuple(tuple(m["positions"][i]) + tuple(m["uvs"][i]) + tuple(m["colors"][i]) for i in f), m["texture"]))
    return out


def _rotation(rot):
    x, y, z = (math.radians(a) for a in rot)
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))
    mul = lambda a, b: tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
    return mul(mul(ry, rx), rz)


# A lift's batches carry this tag: the runtime draws them raised to the lift's height.
LIFT_TAG = 254


def placed(game_text, pnst, extra=(), static_efm=False):
    """[(set, kind, [(tri, texture)])] for every drawn set with a model that has geometry, in the room's order.
    Breakables are numbered by their place in this list among the breakable kinds (see breakables())."""
    found = models(pnst)
    cache, out = {}, []
    for s in parse_game(game_text):
        if (s["kind"] not in DRAWN and s["kind"] not in extra) or s["model"] is None or s["model"] not in found:
            continue
        key = (s["model"], static_efm and s["kind"] == "STAY")
        if key not in cache:
            cache[key] = model_triangles(found[s["model"]], key[1])
        if not cache[key]:
            continue
        r, sc, p = _rotation(s["rot"]), s["scale"], s["pos"]
        tris = []
        for tri, texture in cache[key]:
            vs = []
            for v in tri:
                x, y, z = v[0] * sc[0], v[1] * sc[1], v[2] * sc[2]
                vs.append((p[0] + r[0][0] * x + r[0][1] * y + r[0][2] * z,
                           p[1] + r[1][0] * x + r[1][1] * y + r[1][2] * z,
                           p[2] + r[2][0] * x + r[2][1] * y + r[2][2] * z) + tuple(v[3:]))
            tris.append((tuple(vs), texture))
        out.append((s, s["kind"], tris))
    return out


def breakables(game_text, pnst):
    """The breakables in order: [(number from 1, set, centre (x, y, z), radius, height)] from the placed model's
    bounds. Number n is the group prepare_stage.py tags the prop's triangles with."""
    out = []
    for s, kind, tris in placed(game_text, pnst):
        if kind not in BREAKABLE:
            continue
        pts = [v for tri, _ in tris for v in tri]
        lo = [min(v[k] for v in pts) for k in range(3)]
        hi = [max(v[k] for v in pts) for k in range(3)]
        centre = ((lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2)
        radius = max(30.0, max(hi[0] - lo[0], hi[2] - lo[2]) / 2)
        out.append((len(out) + 1, s, centre, radius, max(40.0, hi[1] - lo[1])))
    return out


# Story pieces: a room's static sets (STAY) that its event script switches on and off by the story's state (ops
# 36 "all off" and 37 "on", docs/EVENT_SCRIPTS.md). Each drawn STAY set numbered 100 and up is tagged STORY_TAG +
# (number - 100), above the breakables' numbers; tools/gen_rooms.py says which are shown when.
STORY_TAG = 128


def story_tag(s):
    """The stage batch tag for a set, or 0."""
    if s["kind"] == "STAY" and 100 <= s["number"] < 100 + 256 - STORY_TAG:
        return STORY_TAG + s["number"] - 100
    return 0


def story_tags(game_text):
    """{set index: tag} for every taggable set (see story_tag)."""
    return {s["index"]: story_tag(s) for s in parse_game(game_text) if story_tag(s)}


def collision_boxes(game_text, tags, include_stay=False, stay_numbers=()):
    """The sets that stop Dante and the enemies: [(tag, centre (x, y, z), half sizes (x, y, z), yaw radians)].
    COLLISION sets are invisible walls in world space; BREAK props are solid until broken; STAY pieces only when
    they hit characters ("hitattr charhit"). A prop's box is relative to its pos and turned with it; hit_rot
    turns the box itself. tags: {set index: the stage batch tag that hides it}, 0 = always there.
    stay_numbers explicitly enables authored STAY colliders whose native default is solid (Lady's perches),
    without admitting other rooms' STAY camera volumes."""
    out = []
    for s in parse_game(game_text):
        if s["box"] is None:
            continue
        attrs = s["hitattr"]
        if "off charhit" in attrs:
            continue
        if s["kind"] == "COLLISION":
            solid = True
        elif s["kind"] in BREAKABLE:
            solid = True
        elif s["kind"] == "STAY":
            # Off by default: st006's ice "boxes" are 40 m across around the room's start, not the ice's shape,
            # so these aren't the pieces' outlines (camera / attack volumes?).
            solid = (include_stay and "charhit" in attrs) or s["number"] in stay_numbers
        else:
            solid = False
        if not solid:
            continue
        ox, oy, oz, hx, hy, hz = s["box"]
        r, p = _rotation(s["rot"]), s["pos"]
        centre = (p[0] + r[0][0] * ox + r[0][1] * oy + r[0][2] * oz,
                  p[1] + r[1][0] * ox + r[1][1] * oy + r[1][2] * oz,
                  p[2] + r[2][0] * ox + r[2][1] * oy + r[2][2] * oz)
        yaw = math.radians(s["rot"][1] + s["hit_rot"][1])
        out.append((tags.get(s["index"], 0), centre, (abs(hx), abs(hy), abs(hz)), yaw))
    return out


def set_motion(pnst, model):
    """A prop's own looping motion (PNST slot 10N+1, a PAC holding a MOT; Secret Mission 12's blocks in st611):
    (pivot, centre, channels) or None. Channels are [(axis, [(frame, value)])], axis 0-2 a translation x/y/z and
    3-5 a rotation x/y/z (radians), each as a change from frame 0, which is the pose the SCM is built in.

    The MOT's mask bits are Dante's (3-5 rotation, 6-8 translation), but a bone's records come translations
    first: st611 model 16 animates all six on one bone and its frame 0 equals the node's SCM record (772.8, 2900,
    -185.2 / 0, -120.56, 0) only read that way, as do models 24 and 31 (translation y, then a spin). The bone is
    the SCM record of the same number. Pivot: that record's place (the sum of its chain's translations, as
    parse_scm places the vertices); centre: the place of the record holding the mesh. Hermite channels are
    resampled every 4 frames."""
    from mot_ps2 import parse_mot, hermite
    if pnst[:4] != b"PNST":
        return None
    count = struct.unpack_from("<I", pnst, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", pnst, 8)
    if 10 * model + 1 >= count or not offsets[10 * model] or not offsets[10 * model + 1]:
        return None
    present = sorted(o for o in offsets if o) + [len(pnst)]
    end = lambda o: min(x for x in present if x > o)
    scm = pnst[offsets[10 * model]:end(offsets[10 * model])]
    pac = pnst[offsets[10 * model + 1]:end(offsets[10 * model + 1])]
    if scm[:4] != b"SCM " or pac[:4] != b"PAC\0":
        return None
    n = struct.unpack_from("<I", pac, 4)[0]
    mots = [pac[o:] for o in struct.unpack_from(f"<{n}I", pac, 8) if pac[o + 4:o + 8] == b"MOT\0"]
    if not mots:
        return None
    mot = parse_mot(mots[0])
    nodes = scm[0x11]
    block = struct.unpack_from("<I", scm, 0x1C)[0]
    tables = struct.unpack_from("<4I", scm, block)
    hier = scm[block + tables[0]:block + tables[0] + nodes]
    remap = scm[block + tables[1]:block + tables[1] + nodes]
    objmap = scm[block + tables[2]:block + tables[2] + nodes]
    local = [struct.unpack_from("<3f", scm, block + tables[3] + r * 32) for r in range(nodes)]
    parent = [-1] * nodes
    for node in range(nodes):
        parent[remap[node]] = -1 if hier[node] == 0xFF else hier[node]
    def world(r, depth=0):
        if parent[r] < 0 or depth > nodes:
            return local[r]
        b = world(parent[r], depth + 1)
        return tuple(b[k] + local[r][k] for k in range(3))
    mesh = next((r for r, o in enumerate(objmap) if o == 0), 0)
    # Records per bone, in the order stored: translations (mask bits 6-8), then rotations (3-5).
    by_bone = {}
    for c in mot["channels"]:
        by_bone.setdefault(c["bone"], []).append(c)
    channels, bone = [], None
    for b, chans in by_bone.items():
        bits = [c["channel"] for c in chans]
        order = [x for x in bits if 6 <= x <= 8] + [x for x in bits if 3 <= x <= 5]
        for c, bit in zip(chans, order):
            axis = bit - 6 if bit >= 6 else bit
            keys = c["keys"]
            if c["kind"] in (1, 3):
                last = int(keys[-1][0])
                frames = list(range(int(keys[0][0]), last, 4)) + [last]
                keys = [(f, hermite(keys, f)) for f in frames]
            else:
                keys = [(k[0], k[1]) for k in keys]
            v0 = keys[0][1]
            channels.append((axis, [(int(f), v - v0) for f, v in keys]))
            bone = b
    if bone is None or bone >= nodes:
        return None
    return world(bone), world(mesh), channels, mot["end_frame"]


def set_motion_bones(pnst, model, index=0):
    """A prop's motion `index` by bone: {bone: (place, [(axis, [(frame, value)])])}, axis 0-2 move x/y/z, 3-5 turn
    x/y/z, 6-8 scale x/y/z, values as stored (not changes). A bone's records come translations, then rotations,
    then scales (st404's sand: mask 0x87 holds a drop of y, then the three scales; see set_motion). Hermite
    channels are resampled every 8 frames. None if there's no such motion."""
    from mot_ps2 import parse_mot, hermite
    if pnst[:4] != b"PNST":
        return None
    count = struct.unpack_from("<I", pnst, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", pnst, 8)
    if 10 * model + 1 >= count or not offsets[10 * model] or not offsets[10 * model + 1]:
        return None
    present = sorted(o for o in offsets if o) + [len(pnst)]
    end = lambda o: min(x for x in present if x > o)
    scm = pnst[offsets[10 * model]:end(offsets[10 * model])]
    pac = pnst[offsets[10 * model + 1]:end(offsets[10 * model + 1])]
    if scm[:4] != b"SCM " or pac[:4] != b"PAC\0":
        return None
    n = struct.unpack_from("<I", pac, 4)[0]
    entries = struct.unpack_from(f"<{n}I", pac, 8)
    if index >= n or pac[entries[index] + 4:entries[index] + 8] != b"MOT\0":
        return None
    mot = parse_mot(pac[entries[index]:])
    nodes = scm[0x11]
    block = struct.unpack_from("<I", scm, 0x1C)[0]
    tables = struct.unpack_from("<4I", scm, block)
    hier = scm[block + tables[0]:block + tables[0] + nodes]
    remap = scm[block + tables[1]:block + tables[1] + nodes]
    local = [struct.unpack_from("<3f", scm, block + tables[3] + r * 32) for r in range(nodes)]
    parent = [-1] * nodes
    for node in range(nodes):
        parent[remap[node]] = -1 if hier[node] == 0xFF else hier[node]
    def world(r, depth=0):
        if parent[r] < 0 or depth > nodes:
            return local[r]
        b = world(parent[r], depth + 1)
        return tuple(b[k] + local[r][k] for k in range(3))
    out = {}
    by_bone = {}
    for c in mot["channels"]:
        by_bone.setdefault(c["bone"], []).append(c)
    for b, chans in by_bone.items():
        bits = [c["channel"] for c in chans]
        order = [x for x in bits if 6 <= x <= 8] + [x for x in bits if 3 <= x <= 5] + [x for x in bits if x <= 2]
        axes = []
        for c, bit in zip(chans, order):
            axis = bit - 6 if bit >= 6 else (bit if bit >= 3 else bit + 6)
            keys = c["keys"]
            if c["kind"] in (1, 3):
                last = int(keys[-1][0])
                keys = [(f, hermite(keys, f)) for f in list(range(int(keys[0][0]), last, 8)) + [last]]
            axes.append((axis, [(int(k[0]), k[1]) for k in keys]))
        out[b] = (world(b) if b < nodes else (0.0, 0.0, 0.0), axes)
    return out
