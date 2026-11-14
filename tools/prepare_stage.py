#!/usr/bin/env python3
"""Build a PSP stage blob (build/testroom/STAGE.BIN) from a DMC3 stage slot.

Default: st001, the street outside the Devil May Cry office where Mission 2
starts (GDATA slot 276: [0] name, [1] TIM2 set, [2] SCM, [3] HITS, ...; slot
277 holds the stage's LIG2/CAM/POS/... records, POS = player starts).

Anti-flicker measures taken here (the PSP has a 16-bit depth buffer and drops
primitives that leave the GE guard band):
* triangles longer than MAX_EDGE are subdivided so none spans the guard band
  near the camera (no popping),
* every texture gets a box-filtered mipmap chain (no minification shimmer),
* coplanar overlapping triangles from different sub-meshes are counted and
  given a draw-order rank so the runtime can draw decals last with a depth
  bias (no z-fighting); the count is printed.

Blob layout (little-endian, every section 16-byte aligned):
  header  'STG1', version, counts, spawn, bounds, section offsets
  textures: per texture {w, h, levels, alpha flag, level offsets[8]} + RGBA8888 levels (GE swizzled)
  batches: {texture, first vertex, vertex count, flags (1 = alpha test, 2 = decal, 4 = alpha blend),
            first chunk, chunk count, 2 pad}
  chunks (header 'reserved' = offset): {first vertex, vertex count, float min[3], max[3]}; a
            batch's triangles grouped by CHUNK x CHUNK XZ cell for culling
  vertices: {float u, v; u32 ABGR colour; float x, y, z} (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF)
  floor: walkable triangles (9 floats) + uniform XZ grid of triangle indices
  hits:  the stage's own collision (HITS chunk): triangles {9 floats vertices,
         3 floats normal, 1 float plane d} + XZ grids for walls and floors

HITS layout (st001 entry 3): 'HITS', u32 size-12, float3 min, float3 max,
u32[3] cell size, u32[3] grid dims, u32 triangle count, u32 (unknown),
u32 triangle-data offset, u32[cells] per-cell list offsets (all offsets
relative to +8); cell list = u32 byte offsets into the triangle data ending
in 0xFFFFFFFF; triangle record = 56 bytes {u16 type, u8, u8, float3 v[3],
float3 normal, float d} with n.p + d = 0.
"""

import argparse
import math
import re
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from scm_ps2 import parse_scm
from tim2 import decode_picture

STAGE_SLOT = 276
STAGE_DATA_SLOT = 277
MAX_EDGE = 500.0          # cm
FLOOR_MIN_NY = 0.55       # walkable if the face normal's Y is at least this
GRID_CELL = 200.0         # cm
MIN_MIP = 8               # also keeps every level swizzlable (>= 4 px wide, 8 rows)
CHUNK = 800.0             # cm: draw chunks (XZ cells by triangle centre) culled on the PSP


def swizzle(w, h, data):
    """PSP GE swizzle for 32-bit texels: 16-byte x 8-row blocks, row-major.
    The GE fetches these far faster than linear textures from main RAM."""
    row = w * 4
    out = bytearray()
    for by in range(h // 8):
        for bx in range(row // 16):
            for y in range(8):
                o = (by * 8 + y) * row + bx * 16
                out += data[o:o + 16]
    return bytes(out)


def pac_entries(data: bytes):
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", data, 8)
    present = sorted(o for o in offsets if o)
    return [data[o:min([x for x in present if x > o] + [len(data)])] if o else b"" for o in offsets]


def decode_textures(texture_data: bytes):
    count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from(f"<{count}I", texture_data, 4)
    textures, offset = [], 0x800
    for n in sectors:
        image = texture_data[offset:offset + n * 0x800]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append((picture["width"], picture["height"], bytes(rgba)))
        offset += n * 0x800
    return textures


# PS2 doubles vertex colour; the real street then comes out darker than this (material, lights).
STAGE_BRIGHTNESS = 1.25


def mip_chain(w, h, rgba):
    levels = [(w, h, rgba)]
    while w > MIN_MIP and h > MIN_MIP and len(levels) < 8:
        nw, nh = w // 2, h // 2
        out = bytearray(nw * nh * 4)
        for y in range(nh):
            for x in range(nw):
                for c in range(4):
                    s = (rgba[((2 * y) * w + 2 * x) * 4 + c] + rgba[((2 * y) * w + 2 * x + 1) * 4 + c] +
                         rgba[((2 * y + 1) * w + 2 * x) * 4 + c] + rgba[((2 * y + 1) * w + 2 * x + 1) * 4 + c])
                    out[(y * nw + x) * 4 + c] = (s + 2) // 4
        w, h, rgba = nw, nh, bytes(out)
        levels.append((w, h, rgba))
    return levels


def paletted(w, h, rgba):
    """8-bit levels for the PSP, all from one palette: (w, h, pixels) per level, 256 ABGR words.
    Swizzling needs 16-byte-wide blocks 8 rows high, so tiny textures grow and the chain stops early."""
    import numpy as np
    from PIL import Image
    image = Image.frombytes("RGBA", (w, h), bytes(rgba))
    while w < 16 or h < 8:
        w, h = max(w, 16), max(h, 8)
        image = image.resize((w, h), Image.Resampling.NEAREST)
    quant = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    palette = quant.getpalette("RGBA")
    palette += [0] * (1024 - len(palette))
    table = np.array(palette, dtype=np.int32).reshape(256, 4)
    used = table
    levels, pixels = [], image.tobytes()
    chain = [lv for lv in mip_chain(w, h, pixels) if lv[0] >= 16 and lv[1] >= 8][:7]
    for i, (lw, lh, data) in enumerate(chain):
        if i == 0:
            levels.append((lw, lh, quant.tobytes()))
            continue
        rows = np.frombuffer(data, dtype=np.uint8).reshape(-1, 4).astype(np.int32)
        indices = np.zeros(len(rows), dtype=np.uint8)
        for start in range(0, len(rows), 4096):
            part = rows[start:start + 4096]
            indices[start:start + 4096] = ((part[:, None, :] - used[None, :, :]) ** 2).sum(axis=2).argmin(axis=1)
        levels.append((lw, lh, indices.tobytes()))
    words = [palette[i] | palette[i + 1] << 8 | palette[i + 2] << 16 | palette[i + 3] << 24 for i in range(0, 1024, 4)]
    return levels, words


def swizzle8(w, h, data):
    """The GE's swizzled layout for 8-bit: 16 bytes x 8 rows, row-major."""
    out = bytearray(w * h)
    at = 0
    for by in range(h // 8):
        for bx in range(w // 16):
            for y in range(8):
                start = (by * 8 + y) * w + bx * 16
                out[at:at + 16] = data[start:start + 16]
                at += 16
    return bytes(out)


def lerp_vertex(a, b):
    return tuple((x + y) * 0.5 for x, y in zip(a, b))


PLAY_MARGIN = 300.0       # cm around the collision's bounds
FAR_EDGE = 0.3            # past the play area, edges may be this fraction of their distance from it


def play_distance(point, play):
    """How far a point is outside the box Dante can reach (the collision's bounds), 0 inside it."""
    if play is None:
        return 0.0
    return math.sqrt(sum(max(play[0][k] - point[k], 0.0, point[k] - play[1][k]) ** 2 for k in range(3)))


def subdivide(tri, play=None):
    """tri = 3 vertices (x, y, z, u, v, r, g, b); split the longest edge until short. Near the play area
    short means MAX_EDGE; scenery far outside it (the sky and backdrops some rooms are wrapped in, tens of
    km across) only needs edges short against its distance from the camera, which can't leave the area."""
    out, stack = [], [tri]
    while stack:
        t = stack.pop()
        edges = [(math.dist(t[i][:3], t[(i + 1) % 3][:3]), i) for i in range(3)]
        length, i = max(edges)
        limit = MAX_EDGE
        if play is not None:
            limit = max(MAX_EDGE, FAR_EDGE * min(play_distance(v, play) for v in t))
        if length <= limit:
            out.append(t)
            continue
        a, b, c = t[i], t[(i + 1) % 3], t[(i + 2) % 3]
        m = lerp_vertex(a, b)
        stack.append((a, m, c))
        stack.append((m, b, c))
    return out


def face_normal(t):
    ax, ay, az = (t[1][k] - t[0][k] for k in range(3))
    bx, by, bz = (t[2][k] - t[0][k] for k in range(3))
    n = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
    length = math.sqrt(sum(c * c for c in n)) or 1.0
    return tuple(c / length for c in n)


def _surface_cells(hits, size=50.0):
    """The 50 cm cells the collision's surfaces pass through."""
    cells = set()
    for t in hits:
        a, b, c = t[0:3], t[3:6], t[6:9]
        n = max(2, int(max(math.dist(a, b), math.dist(a, c)) / 25))
        for i in range(n + 1):
            for j in range(n + 1 - i):
                u, v = i / n, j / n
                cells.add(tuple(int((a[k] + (b[k] - a[k]) * u + (c[k] - a[k]) * v) // size) for k in range(3)))
    return cells


def _fit(obj, cells, size=50.0):
    """The share of an object's vertices (a sample) within a cell of the collision's surfaces; None if empty."""
    points = [p for s in obj["submeshes"] for p in s["positions"]]
    if not points:
        return None
    points = points[::max(1, len(points) // 60)]
    hit = 0
    for p in points:
        x, y, z = (int(v // size) for v in p)
        if any((x + i, y + j, z + k) in cells for i in (-1, 0, 1) for j in (-1, 0, 1) for k in (-1, 0, 1)):
            hit += 1
    return hit / len(points)


def choose_scm(scm_data, hits):
    """How the scene's objects are placed, checked against the collision, which is in world coordinates.

    First whichever reading of the root's offset lines the visuals up with the collision (the middle of each
    one's vertices against the collision's). Then the nodes' full transforms (their rotations too) against
    that, object by object: the stage takes whichever fits the collision better overall, for all its objects. Most stages' rotations are zero or cancel and nothing
    changes; st002's (Mission 3's street) are what turn its pieces the right way round."""
    def middle(values):
        values = sorted(values)
        return values[len(values) // 2] if values else 0.0
    target = (middle([t[0] for t in hits]), middle([t[2] for t in hits]))
    best, best_error = None, None
    for root_xz in (True, False):
        scm = parse_scm(scm_data, root_xz)
        points = [p for o in scm["objects"] for s in o["submeshes"] for p in s["positions"]]
        error = abs(middle([p[0] for p in points]) - target[0]) + abs(middle([p[2] for p in points]) - target[1])
        if best is None or error < best_error:
            best, best_error = scm, error
    full = parse_scm(scm_data, True, transform=True)
    cells = _surface_cells(hits)
    fits = [(_fit(a, cells), _fit(b, cells)) for a, b in zip(best["objects"], full["objects"])]
    plain_total = sum(f[0] or 0.0 for f in fits)
    full_total = sum(f[1] or 0.0 for f in fits)
    prefer_full = full_total > plain_total + 0.5
    # One reading for the whole stage. Object by object the fit misleads: st002's building pieces read without
    # their rotations hang over its far end, beside tall collision walls, and score well there.
    objects = full["objects"] if prefer_full else best["objects"]
    print(f"scene placement: {'full transforms' if prefer_full else 'translations'} "
          f"(collision fit {plain_total:.1f} translations, {full_total:.1f} full)")
    best = dict(best)
    best["objects"] = objects
    return best


def parse_hits(data: bytes):
    if data[:4] != b"HITS":
        raise ValueError("Not a HITS chunk")
    count = struct.unpack_from("<I", data, 0x38)[0]
    base = struct.unpack_from("<I", data, 0x40)[0] + 8
    tris = []
    for i in range(count):
        r = base + 56 * i
        v = struct.unpack_from("<9f", data, r + 4)
        n = struct.unpack_from("<4f", data, r + 40)
        tris.append(v + n)
    return tris


def _floor_heights(floors, x, z):
    out = []
    for t in floors:
        x0, y0, z0, x1, y1, z1, x2, y2, z2 = t[:9]
        d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
        if abs(d) < 1e-6:
            continue
        a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d
        b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d
        if min(a, b, 1 - a - b) >= -1e-4:
            out.append(a * y0 + b * y1 + (1 - a - b) * y2)
    return out


def _wall_near(walls, mx, mz, y):
    """A collision wall within 40 cm of (mx, mz), standing over the height just above the floor."""
    for t in walls:
        xs, ys, zs = t[0:9:3], t[1:9:3], t[2:9:3]
        if max(ys) < y + 20 or min(ys) > y + 150:
            continue
        for i in range(3):
            ax, az, bx, bz = xs[i], zs[i], xs[(i + 1) % 3], zs[(i + 1) % 3]
            dx, dz = bx - ax, bz - az
            l2 = dx * dx + dz * dz
            u = 0 if l2 < 1e-6 else max(0, min(1, ((mx - ax) * dx + (mz - az) * dz) / l2))
            if math.hypot(mx - (ax + u * dx), mz - (az + u * dz)) < 40:
                return True
    return False


NO_EDGE_WALL_SLOTS = {380}   # st114
EDGE_WALL_HEIGHT = 2000.0   # cm above the edge: past any jump, Air Hike and Sky Star


def open_edge_walls(floors, walls, rendered_floors=(), door_boxes=()):
    """Walls for the collision floor's open edges: an edge of the walkable floor with no collision wall on it and
    no lower floor beyond it to drop to. The disc's collision leaves these open (room 100's courtyard rim, the
    end of Mission 3's street); in the game Dante never goes over them, so the port stands a wall there. Each is a
    quad from below the edge to EDGE_WALL_HEIGHT above it, or to just under a floor higher up over it, facing the
    floor. Edges with a floor below them (ledges he drops from) are left open."""
    edges = {}
    for t in floors:
        pts = [tuple(t[k * 3:k * 3 + 3]) for k in range(3)]
        cx, cz = sum(p[0] for p in pts) / 3, sum(p[2] for p in pts) / 3
        for i in range(3):
            a, b = pts[i], pts[(i + 1) % 3]
            k = tuple(sorted((tuple(round(v) for v in a), tuple(round(v) for v in b))))
            edges.setdefault(k, []).append((a, b, cx, cz))
    out = []
    for v in edges.values():
        if len(v) != 1:
            continue
        a, b, cx, cz = v[0]
        length = math.dist((a[0], a[2]), (b[0], b[2]))
        if length < 1.0:
            continue
        y = max(a[1], b[1])
        nx, nz = -(b[2] - a[2]) / length, (b[0] - a[0]) / length   # a horizontal normal; flip it to face the floor
        mx, mz = (a[0] + b[0]) / 2, (a[2] + b[2]) / 2
        if (cx - mx) * nx + (cz - mz) * nz < 0:
            nx, nz = -nx, -nz
        if _wall_near(walls, mx, mz, y):
            continue
        # HITS sometimes ends at an arch while the rendered floor continues to the door (Bullseye).
        # Preserve that approach; sealing it here would survive the puzzle's dynamic wall removal.
        doorway = False
        for dx, dy, dz, dw, dh, dd in door_boxes:
            vx, vz = dx - mx, dz - mz
            outward = -(vx * nx + vz * nz)
            lateral = abs(vx * nz - vz * nx)
            if not (0 < outward <= 600 and lateral <= max(dw, dd) * 0.5 + 40 and dy - 80 <= y <= dy + dh):
                continue
            # Require a floor along the entire approach, not merely a door beyond a drop.
            if all(any(abs(h - y) < 60 for h in _floor_heights(rendered_floors, mx + vx * f, mz + vz * f))
                   for f in (0.1, 0.5, 0.9)):
                doorway = True
                break
        if doorway:
            continue
        # beyond the edge, at its middle and near both ends: a lower floor to land on means it's a ledge
        probes = [(mx - nx * 30, mz - nz * 30)]
        if length > 80:
            for f in (0.15, 0.85):
                probes.append((a[0] + (b[0] - a[0]) * f - nx * 30, a[2] + (b[2] - a[2]) * f - nz * 30))
        below = False
        top = y + EDGE_WALL_HEIGHT
        for px, pz in probes:
            for h in _floor_heights(floors, px, pz):
                if h < y + 60:
                    below = True     # a ledge to drop from, or floor carrying on (a T-junction in the mesh)
                elif h >= y + 60:
                    top = min(top, h - 20)
        if below:
            continue
        lo = min(a[1], b[1]) - 100
        d = -(nx * mx + nz * mz)
        a0, a1 = (a[0], lo, a[2]), (a[0], top, a[2])
        b0, b1 = (b[0], lo, b[2]), (b[0], top, b[2])
        out.append(a0 + b0 + b1 + (nx, 0.0, nz, d))
        out.append(a0 + b1 + a1 + (nx, 0.0, nz, d))
    return out


def build_grid(tris, x0, z0, cell, w, h, margin=0.0):
    """Each triangle in every cell its XZ box, grown by margin, touches. The walls' margin is past Dante's radius:
    stage_collide looks only in the cell his centre is in, and a wall on a cell's edge (Mission 3's bar, x 3000)
    must be found from the cell beside it too."""
    cells = [[] for _ in range(w * h)]
    for ti, t in enumerate(tris):
        cx0 = max(0, int((min(t[0:9:3]) - margin - x0) // cell)); cx1 = min(w - 1, int((max(t[0:9:3]) + margin - x0) // cell))
        cz0 = max(0, int((min(t[2:9:3]) - margin - z0) // cell)); cz1 = min(h - 1, int((max(t[2:9:3]) + margin - z0) // cell))
        for cz in range(cz0, cz1 + 1):
            for cx in range(cx0, cx1 + 1):
                cells[cz * w + cx].append(ti)
    return cells


def write_grid(blob, cells):
    offset = align(blob)
    cursor = 0
    for c in cells:
        blob += struct.pack("<II", cursor, len(c))
        cursor += len(c)
    for c in cells:
        for ti in c:
            blob += struct.pack("<I", ti)
    return offset


def align(blob: bytearray):
    while len(blob) % 16:
        blob.append(0)
    return len(blob)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--slot", type=int, default=STAGE_SLOT, help="GDATA slot of the stage; its data is the next one")
    args = parser.parse_args()

    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        read = lambda slot: iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"])
        stage = pac_entries(read(args.slot))
        stage_data = pac_entries(read(args.slot + 1))
    name = stage[0][:16].split(b"\0")[0].decode("ascii", "replace")
    textures = decode_textures(stage[1])
    hits = parse_hits(stage[3])
    scm = choose_scm(stage[2], hits)
    hit_walls = [t for t in hits if abs(t[10]) < 0.5]
    hit_floors = [t for t in hits if t[10] >= 0.5]
    # Where Dante can be: the collision's bounds, a little enlarged.
    play = ([min(t[k + 3 * j] for t in hits for j in range(3)) - PLAY_MARGIN for k in range(3)],
            [max(t[k + 3 * j] for t in hits for j in range(3)) + PLAY_MARGIN for k in range(3)])

    # Player start: first POS record (x, y, z at +4, yaw in degrees at +16).
    pos = next(c for c in stage_data if c[:4] == b"POS\0")
    sx, sy, sz = struct.unpack_from("<3f", pos, 0x14)
    syaw = struct.unpack_from("<f", pos, 0x20)[0]

    # Gather triangles per (texture) batch in sub-mesh order.
    batches = []  # (texture, alpha, tris)
    # Alpha classes: cut-out (alpha only ~0 or ~255 -> alpha test) versus
    # translucent (graded alpha, e.g. the grime/shadow overlays -> blend; a hard
    # alpha test renders their mid-alpha texels as solid black).
    alpha_textures, blend_textures = set(), set()
    for i, (w, h, rgba) in enumerate(textures):
        alphas = rgba[3::4]
        if any(a < 250 for a in alphas):
            graded = sum(1 for a in alphas if 8 < a < 247)
            (blend_textures if graded > len(alphas) // 50 else alpha_textures).add(i)
    for obj in scm["objects"]:
        for sub in obj["submeshes"]:
            tris = []
            for f in sub["faces"]:
                tri = tuple(sub["positions"][i] + sub["uvs"][i] + sub["colors"][i] for i in f)
                tris.extend(subdivide(tri, play))
            if tris:
                kind = 2 if sub["texture"] in blend_textures else (1 if sub["texture"] in alpha_textures else 0)
                batches.append([sub["texture"], kind, tris, 0])

    # Coplanar overlap check between batches (z-fighting candidates).
    planes = {}
    decal_tris = 0
    for bi, (_, _, tris, _) in enumerate(batches):
        for t in tris:
            n = face_normal(t)
            d = sum(n[k] * t[0][k] for k in range(3))
            key = (round(n[0], 2), round(n[1], 2), round(n[2], 2), round(d / 2.0))
            planes.setdefault(key, set()).add(bi)
    shared = {k: v for k, v in planes.items() if len(v) > 1}
    for v in shared.values():
        for bi in sorted(v)[1:]:
            batches[bi][3] = 1  # draw after its plane-mates with a depth bias
    decal_batches = sum(1 for b in batches if b[3])

    # Floor triangles + grid.
    floor = []
    for _, _, tris, _ in batches:
        for t in tris:
            if play_distance(t[0], play) > 0 and play_distance(t[1], play) > 0 and play_distance(t[2], play) > 0:
                continue  # backdrop: nothing stands there
            if face_normal(t)[1] >= FLOOR_MIN_NY or face_normal(t)[1] <= -FLOOR_MIN_NY:
                floor.append(tuple(t[i][k] for i in range(3) for k in range(3)))
    door_boxes = [tuple(map(float, m)) for m in re.findall(
        r"^BoxIn\s+\S+\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)",
        stage_data[0].decode("latin1").replace("\r", ""), re.M)]
    # Rooms whose open edges he's meant to drop from though no floor shows right under them: 114 Heavenrise
    # Chamber's balconies over its shaft (the walkthrough: "drop down to the ground level").
    edge_walls = [] if args.slot in NO_EDGE_WALL_SLOTS else open_edge_walls(hit_floors, hit_walls, floor, door_boxes)
    hit_walls += edge_walls
    xs = [v for t in floor for v in t[0::3]]
    zs = [v for t in floor for v in t[2::3]]
    gx0, gz0 = math.floor(min(xs) / GRID_CELL) * GRID_CELL, math.floor(min(zs) / GRID_CELL) * GRID_CELL
    gw = int(math.ceil((max(xs) - gx0) / GRID_CELL)) + 1
    gh = int(math.ceil((max(zs) - gz0) / GRID_CELL)) + 1
    cells = [[] for _ in range(gw * gh)]
    for ti, t in enumerate(floor):
        cx0 = int((min(t[0::3]) - gx0) // GRID_CELL); cx1 = int((max(t[0::3]) - gx0) // GRID_CELL)
        cz0 = int((min(t[2::3]) - gz0) // GRID_CELL); cz1 = int((max(t[2::3]) - gz0) // GRID_CELL)
        for cz in range(cz0, cz1 + 1):
            for cx in range(cx0, cx1 + 1):
                cells[cz * gw + cx].append(ti)

    # The room's props (tools/gimmicks.py): breakables and static pieces, one batch per (prop, texture), each
    # breakable's tagged with its number so the runtime can stop drawing it once it's broken. Not floor.
    import gimmicks
    game_text = next((c for c in stage if c[:6] == b"# GAME"), b"").decode("latin1")
    # Keyed by the set's place in the room's list: each call parses the text afresh.
    numbers = {s["index"]: n for n, s, *_ in gimmicks.breakables(game_text, stage[5])} if len(stage) > 5 else {}
    for index, tag in gimmicks.story_tags(game_text).items():
        numbers.setdefault(index, tag)
    prop_batches = 0
    # A room with a lift also gets its lift and the lift's switch drawn from their own models (the lift tagged so
    # the runtime can raise it); other switches (Mission 3's crest switch) are drawn by the runtime.
    extra = ("LIFT", "SWITCH") if re.search(r"# SET\s+\d+\s+LIFT", game_text) else ()
    for s, kind, tris in (gimmicks.placed(game_text, stage[5], extra, static_efm=680 <= args.slot <= 732) if len(stage) > 5 else []):
        by_texture = {}
        for tri, texture in tris:
            by_texture.setdefault(texture, []).extend(subdivide(tri, play))
        for texture, subdivided in by_texture.items():
            texture = min(texture, len(textures) - 1)
            kind_flag = 2 if texture in blend_textures else (1 if texture in alpha_textures else 0)
            batches.append([texture, kind_flag, subdivided, 0, gimmicks.LIFT_TAG if kind == "LIFT" else numbers.get(s["index"], 0)])
            prop_batches += 1

    # ---- write the blob
    blob = bytearray(256)  # header placeholder
    tex_table = align(blob)
    blob += bytes(48 * len(textures))
    tex_entries = []
    for i, (w, h, rgba) in enumerate(textures):
        levels, words = paletted(w, h, rgba)
        offsets = []
        for lw, lh, data in levels:
            offsets.append(align(blob))
            blob += swizzle8(lw, lh, data)
        palette_at = align(blob)
        blob += struct.pack("<256I", *words)
        tex_entries.append((levels[0][0], levels[0][1], len(levels), 2 if i in blend_textures else int(i in alpha_textures), offsets, palette_at))
    for i, (w, h, n, alpha, offsets, palette_at) in enumerate(tex_entries):
        # The last slot holds the palette.
        struct.pack_into("<4I8I", blob, tex_table + 48 * i, w, h, n, alpha, *(offsets + [0] * (7 - len(offsets))), palette_at)
    # Opaque, then alpha-tested, then decals (biased), then translucent (blended,
    # no depth writes) -- sub-mesh order kept inside each group.
    order = sorted(range(len(batches)), key=lambda i: (batches[i][1] == 2, batches[i][3], batches[i][1], i))
    batch_table = align(blob)
    blob += bytes(32 * len(batches))
    vertex_offset = align(blob)
    first = 0
    chunks = []  # (first vertex, vertex count, min xyz, max xyz)
    for slot, bi in enumerate(order):
        texture, kind, tris, decal = batches[bi][:4]
        group = batches[bi][4] if len(batches[bi]) > 4 else 0
        # Group the batch's triangles by XZ chunk (centre), keeping order inside.
        groups = {}
        for t in tris:
            cx = sum(v[0] for v in t) / 3.0
            cz = sum(v[2] for v in t) / 3.0
            groups.setdefault((math.floor(cx / CHUNK), math.floor(cz / CHUNK)), []).append(t)
        batch_first, chunk_first = first, len(chunks)
        for key in sorted(groups):
            g = groups[key]
            for t in g:
                for v in t:
                    x, y, z, u, vv, r, g_, b = v
                    col = 0xFF000000 | (min(255, int(b * STAGE_BRIGHTNESS)) << 16) | (min(255, int(g_ * STAGE_BRIGHTNESS)) << 8) | min(255, int(r * STAGE_BRIGHTNESS))
                    blob += struct.pack("<2fI3f", u, vv, col, x, y, z)
            pts = [v for t in g for v in t]
            chunks.append((first, len(g) * 3,
                           min(v[0] for v in pts), min(v[1] for v in pts), min(v[2] for v in pts),
                           max(v[0] for v in pts), max(v[1] for v in pts), max(v[2] for v in pts)))
            first += len(g) * 3
        struct.pack_into("<7I", blob, batch_table + 32 * slot, texture, batch_first, first - batch_first,
                         (1 if kind == 1 else 0) | (2 if decal else 0) | (4 if kind == 2 else 0),
                         chunk_first, len(chunks) - chunk_first, group)
    chunk_table = align(blob)
    for c in chunks:
        blob += struct.pack("<2I6f", *c)
    floor_offset = align(blob)
    for t in floor:
        blob += struct.pack("<9f", *t)
    grid_offset = align(blob)
    index_base = grid_offset + 8 * len(cells)
    cursor = 0
    for c in cells:
        blob += struct.pack("<II", cursor, len(c))
        cursor += len(c)
    for c in cells:
        for ti in c:
            blob += struct.pack("<I", ti)
    align(blob)
    # Collision: walls and floors in their own XZ grids over the same extent.
    hits_offset = align(blob)
    for t in hit_walls + hit_floors:
        blob += struct.pack("<13f", *t)
    wall_grid = write_grid(blob, build_grid(hit_walls, gx0, gz0, GRID_CELL, gw, gh, margin=60.0))
    hfloor_grid = write_grid(blob, build_grid(hit_floors, gx0, gz0, GRID_CELL, gw, gh, margin=1.0))
    align(blob)

    all_x = [v[0] for b in batches for t in b[2] for v in t]
    all_y = [v[1] for b in batches for t in b[2] for v in t]
    all_z = [v[2] for b in batches for t in b[2] for v in t]
    struct.pack_into("<4I", blob, 0x80, hits_offset, len(hit_walls), len(hit_floors), wall_grid)
    struct.pack_into("<I", blob, 0x90, hfloor_grid)
    struct.pack_into("<4s7I4f6f6I2f2I", blob, 0, b"STG1", 3,
                     len(textures), len(batches), first, len(floor), gw, gh,
                     sx, sy, sz, math.radians(syaw),
                     min(all_x), min(all_y), min(all_z), max(all_x), max(all_y), max(all_z),
                     tex_table, batch_table, vertex_offset, floor_offset, grid_offset, index_base,
                     gx0, gz0, int(GRID_CELL), chunk_table)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    tri_count = first // 3
    print(f"{args.output}: {name}, {len(textures)} textures (mipmapped), {len(batches)} batches in {len(chunks)} chunks, "
          f"{tri_count} triangles after subdividing to {MAX_EDGE:.0f} cm, {len(floor)} floor triangles "
          f"({gw}x{gh} grid), {len(shared)} shared planes -> {decal_batches} decal batches, "
          f"collision {len(hit_walls)} walls ({len(edge_walls)} on open floor edges) / {len(hit_floors)} floors, "
          f"cut-out textures {sorted(alpha_textures)}, translucent {sorted(blend_textures)}, "
          f"spawn ({sx:.0f}, {sy:.1f}, {sz:.0f}) yaw {syaw:.0f}, {len(blob):,} bytes")


if __name__ == "__main__":
    main()
