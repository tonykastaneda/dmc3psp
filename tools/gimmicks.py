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
             "rot": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0), "vital": None, "items": []}
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
            elif key == "item":
                parts = rest.split()
                n = _numbers(" ".join(parts[1:]))
                if parts and len(n) >= 2:
                    s["items"].append((parts[0], int(n[0]), int(n[1])))
        sets.append(s)
    return sets


def model_triangles(data):
    """Model space triangles: [((x, y, z, u, v, r, g, b) x 3, texture)]. SCMs only; effect models (EFM) and
    MODs give nothing."""
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


def placed(game_text, pnst):
    """[(set, kind, [(tri, texture)])] for every drawn set with a model that has geometry, in the room's order.
    Breakables are numbered by their place in this list among the breakable kinds (see breakables())."""
    found = models(pnst)
    cache, out = {}, []
    for s in parse_game(game_text):
        if s["kind"] not in DRAWN or s["model"] is None or s["model"] not in found:
            continue
        if s["model"] not in cache:
            cache[s["model"]] = model_triangles(found[s["model"]])
        if not cache[s["model"]]:
            continue
        r, sc, p = _rotation(s["rot"]), s["scale"], s["pos"]
        tris = []
        for tri, texture in cache[s["model"]]:
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
