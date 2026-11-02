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
  textures: per texture {w, h, levels, alpha flag, level offsets[8]} + RGBA8888 levels
  batches: {texture, first vertex, vertex count, flags (1 = alpha test, 2 = decal, 4 = alpha blend)}
  vertices: {float u, v; u32 ABGR colour; float x, y, z} (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF)
  floor: walkable triangles (9 floats) + uniform XZ grid of triangle indices
"""

import argparse
import math
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from scm_ps2 import parse_scm
from tim2 import decode_picture

STAGE_SLOT = 276
STAGE_DATA_SLOT = 277
MAX_EDGE = 300.0          # cm
FLOOR_MIN_NY = 0.55       # walkable if the face normal's Y is at least this
GRID_CELL = 200.0         # cm
MIN_MIP = 8


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


def lerp_vertex(a, b):
    return tuple((x + y) * 0.5 for x, y in zip(a, b))


def subdivide(tri):
    """tri = 3 vertices (x, y, z, u, v, r, g, b); split the longest edge until short."""
    out, stack = [], [tri]
    while stack:
        t = stack.pop()
        edges = [(math.dist(t[i][:3], t[(i + 1) % 3][:3]), i) for i in range(3)]
        length, i = max(edges)
        if length <= MAX_EDGE:
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


def align(blob: bytearray):
    while len(blob) % 16:
        blob.append(0)
    return len(blob)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        read = lambda slot: iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"])
        stage = pac_entries(read(STAGE_SLOT))
        stage_data = pac_entries(read(STAGE_DATA_SLOT))
    name = stage[0][:16].split(b"\0")[0].decode("ascii", "replace")
    textures = decode_textures(stage[1])
    scm = parse_scm(stage[2])

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
                tris.extend(subdivide(tri))
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
            if face_normal(t)[1] >= FLOOR_MIN_NY or face_normal(t)[1] <= -FLOOR_MIN_NY:
                floor.append(tuple(t[i][k] for i in range(3) for k in range(3)))
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

    # ---- write the blob
    blob = bytearray(256)  # header placeholder
    tex_table = align(blob)
    blob += bytes(48 * len(textures))
    tex_entries = []
    for i, (w, h, rgba) in enumerate(textures):
        levels = mip_chain(w, h, rgba)
        offsets = []
        for lw, lh, data in levels:
            offsets.append(align(blob))
            blob += data
        tex_entries.append((w, h, len(levels), 2 if i in blend_textures else int(i in alpha_textures), offsets))
    for i, (w, h, n, alpha, offsets) in enumerate(tex_entries):
        struct.pack_into("<4I8I", blob, tex_table + 48 * i, w, h, n, alpha, *(offsets + [0] * (8 - len(offsets))))
    # Opaque, then alpha-tested, then decals (biased), then translucent (blended,
    # no depth writes) -- sub-mesh order kept inside each group.
    order = sorted(range(len(batches)), key=lambda i: (batches[i][1] == 2, batches[i][3], batches[i][1], i))
    batch_table = align(blob)
    blob += bytes(16 * len(batches))
    vertex_offset = align(blob)
    first = 0
    for slot, bi in enumerate(order):
        texture, kind, tris, decal = batches[bi]
        for t in tris:
            for v in t:
                x, y, z, u, vv, r, g, b = v
                col = 0xFF000000 | (min(255, int(b * 2)) << 16) | (min(255, int(g * 2)) << 8) | min(255, int(r * 2))
                blob += struct.pack("<2fI3f", u, vv, col, x, y, z)
        struct.pack_into("<4I", blob, batch_table + 16 * slot, texture, first, len(tris) * 3,
                         (1 if kind == 1 else 0) | (2 if decal else 0) | (4 if kind == 2 else 0))
        first += len(tris) * 3
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

    all_x = [v[0] for _, _, tris, _ in batches for t in tris for v in t]
    all_y = [v[1] for _, _, tris, _ in batches for t in tris for v in t]
    all_z = [v[2] for _, _, tris, _ in batches for t in tris for v in t]
    struct.pack_into("<4s7I4f6f6I2f2I", blob, 0, b"STG1", 1,
                     len(textures), len(batches), first, len(floor), gw, gh,
                     sx, sy, sz, math.radians(syaw),
                     min(all_x), min(all_y), min(all_z), max(all_x), max(all_y), max(all_z),
                     tex_table, batch_table, vertex_offset, floor_offset, grid_offset, index_base,
                     gx0, gz0, int(GRID_CELL), 0)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    tri_count = first // 3
    print(f"{args.output}: {name}, {len(textures)} textures (mipmapped), {len(batches)} batches, "
          f"{tri_count} triangles after subdividing to {MAX_EDGE:.0f} cm, {len(floor)} floor triangles "
          f"({gw}x{gh} grid), {len(shared)} shared planes -> {decal_batches} decal batches, "
          f"cut-out textures {sorted(alpha_textures)}, translucent {sorted(blend_textures)}, "
          f"spawn ({sx:.0f}, {sy:.1f}, {sz:.0f}) yaw {syaw:.0f}, {len(blob):,} bytes")


if __name__ == "__main__":
    main()
