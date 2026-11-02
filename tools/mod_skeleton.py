#!/usr/bin/env python3
"""DMC3 PS2 MOD skeleton and skinning streams.

Layout recovered from Dante (GDATA slot 0, PAC chunk 1) and cross-checked
against the mesh geometry:

Skeleton block at MOD+0x1C pointer:
  +0x00  u32[4] offsets (relative to the block) of the four tables below
  +0x10  u32 bone count (== MOD header byte 0x11)
  table0 u8[n]  hierarchy: parent *record* index for node j, 0xFF = root
  table1 u8[n]  remap: node j describes record remap[j]
  table2 u8[32] per-bone flags (not interpreted yet)
  table3 n x 32 bytes: float4 (x, y, z, length) offset from the parent in the
         rest pose, then 16 bytes of zero (runtime scratch on PS2)
  So parent[remap[j]] = hierarchy[j]; offsets are indexed by record.

Per-vertex streams of each mesh (pointer slots 5 and 6 of the mesh header):
  bone stream   u8[4]: byte0 unused (0), bytes1..3 = bone record index * 4
  flag stream   u16:  bits 0-4 / 5-9 / 10-14 = weights of bones 1..3 in 31sts,
                      bit 15 = PS2 GS ADC (no-kick): the vertex starts a strip
"""

import struct


def parse_skeleton(data: bytes):
    bone_count = data[0x11]
    block = struct.unpack_from("<I", data, 0x1C)[0]
    offsets = struct.unpack_from("<4I", data, block)
    count = struct.unpack_from("<I", data, block + 0x10)[0]
    if count != bone_count:
        raise ValueError(f"Skeleton count {count} disagrees with MOD header {bone_count}")
    hierarchy = data[block + offsets[0]:block + offsets[0] + count]
    remap = data[block + offsets[1]:block + offsets[1] + count]
    flags = data[block + offsets[2]:block + offsets[2] + 32]
    local = []
    for record in range(count):
        x, y, z, length = struct.unpack_from("<4f", data, block + offsets[3] + record * 32)
        if abs((x * x + y * y + z * z) ** 0.5 - length) > 1e-3 * max(1.0, length):
            raise ValueError(f"Bone {record} offset length check failed")
        local.append((x, y, z))
    parents = [-1] * count
    for node in range(count):
        record = remap[node]
        parents[record] = -1 if hierarchy[node] == 0xFF else hierarchy[node]
    world = [None] * count
    for record in range(count):
        if parents[record] >= count:
            raise ValueError(f"Bone {record} parent out of range")
    # Parents always precede children in this data; verify instead of assuming.
    for record in range(count):
        parent = parents[record]
        if parent >= record:
            raise ValueError(f"Bone {record} parent {parent} does not precede it")
        base = (0.0, 0.0, 0.0) if parent < 0 else world[parent]
        world[record] = tuple(base[axis] + local[record][axis] for axis in range(3))
    return {
        "count": count,
        "parents": parents,
        "local": local,
        "world": world,
        "flags": list(flags),
    }


def parse_skin(data: bytes, bone_offset: int, flag_offset: int, vertex_count: int, bone_count: int):
    bones = []
    weights = []
    strip_start = []
    for i in range(vertex_count):
        raw = data[bone_offset + i * 4:bone_offset + i * 4 + 4]
        word = struct.unpack_from("<H", data, flag_offset + i * 2)[0]
        indices = []
        for byte in raw[1:4]:
            if byte % 4:
                raise ValueError(f"Vertex {i} bone byte 0x{byte:02x} is not a multiple of 4")
            if byte // 4 >= bone_count:
                raise ValueError(f"Vertex {i} bone {byte // 4} exceeds bone count")
            indices.append(byte // 4)
        w = [(word >> shift) & 0x1F for shift in (0, 5, 10)]
        if sum(w) != 31:
            raise ValueError(f"Vertex {i} weights {w} do not sum to 31")
        bones.append(tuple(indices))
        weights.append(tuple(value / 31.0 for value in w))
        strip_start.append(bool(word & 0x8000))
    return bones, weights, strip_start


def strip_faces(strip_start):
    """Triangles of a GS triangle strip honouring ADC restarts."""
    # GS semantics: every vertex enters the strip queue; one with ADC set does
    # not kick a triangle. Winding parity counts from the latest ADC run.
    faces = []
    start = 0
    for i in range(len(strip_start)):
        if strip_start[i]:
            if i == 0 or not strip_start[i - 1]:
                start = i
            continue
        if i < 2:
            continue
        a, b, c = i - 2, i - 1, i
        faces.append((a, b, c) if (i - start) % 2 == 0 else (b, a, c))
    return faces
