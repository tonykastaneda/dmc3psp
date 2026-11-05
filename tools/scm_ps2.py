#!/usr/bin/env python3
"""DMC3 PS2 SCM (stage/scene model) parser -- format recovered from st001.

An SCM shares the MOD container layout (header counts at 0x10, 48-byte
object records from 0x30, a node block at the 0x1C pointer) with these
differences:

* Object flag low byte = number of sub-meshes. Each sub-mesh has its own
  0x30-byte header at object.mesh_offset + 0x30 * k:
      u16 vertex count, u16 texture index, then u32 pointers [8]:
      [2] float3 positions, [3] float3 normals, [4] s16x2 UVs (/4096),
      [5] 0x30 if another sub-mesh follows (else 0), [7] RGBA vertex colours.
  The object record's vertex count is the sum over its sub-meshes.
* No bone/weight streams: vertex colour byte 3 bit 1 is the GS ADC strip
  restart (the MOD weight word's bit 15 in characters).
* Vertex colours are PS2 baked lighting where 0x80 = 1.0.
* The node block is a scene hierarchy: table0 = parent per node (order
  arbitrary: parents may follow children), table1 = node -> record remap,
  table2 = record -> object index (255 = group node), table3 = per-record
  32 bytes: float3 translation, its distance from the origin, float3
  rotation (radians), 0. Object k is placed by the record that table2 maps
  to k. parse_scm(transform=True) applies the whole chain of translations
  and rotations (Z, then Y, then X: R = Rz Ry Rx); the default reading sums
  the translations only, which is right for most stages, where the
  rotations are zero or cancel. st002 (Mission 3's street) needs them: its
  root is turned 180 degrees about (5000, 0, 6000).
"""

import math
import struct

from mod_skeleton import strip_faces


def _matrix(record):
    x, y, z, _, rx, ry, rz, _ = record
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    # Rz * Ry * Rx, with the translation
    r = [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
         [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
         [-sy, cy * sx, cy * cx]]
    return [r[0] + [x], r[1] + [y], r[2] + [z]]


def _compose(a, b):
    out = []
    for i in range(3):
        row = [sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
        row.append(sum(a[i][k] * b[k][3] for k in range(3)) + a[i][3])
        out.append(row)
    return out


def parse_scm(data: bytes, root_xz: bool = True, transform: bool = False):
    if data[:4] != b"SCM ":
        raise ValueError("Not an SCM")
    object_count, node_count, texture_count = data[0x10], data[0x11], data[0x12]
    block = struct.unpack_from("<I", data, 0x1C)[0]
    tables = struct.unpack_from("<4I", data, block)
    hier = data[block + tables[0]:block + tables[0] + node_count]
    remap = data[block + tables[1]:block + tables[1] + node_count]
    objmap = data[block + tables[2]:block + tables[2] + node_count]
    records = [struct.unpack_from("<8f", data, block + tables[3] + r * 32) for r in range(node_count)]
    local = [r[:3] for r in records]
    parent = [-1] * node_count
    for node in range(node_count):
        parent[remap[node]] = -1 if hier[node] == 0xFF else hier[node]
    world = [None] * node_count
    root_record = next((r for r in range(node_count) if parent[r] < 0), 0)

    def resolve(record, depth=0):
        if depth > node_count:
            raise ValueError("Cycle in SCM node hierarchy")
        if world[record] is None:
            if parent[record] < 0:
                # Some stages' objects are relative to the root, and some already absolute with the root
                # elsewhere (st002's is at (5000, 0, 6000)). Callers tell by comparing with the collision.
                world[record] = local[record] if root_xz else (0.0, local[record][1], 0.0)
            else:
                base = resolve(parent[record], depth + 1)
                # In those stages a node at the root's own place is a copy of its offset, with absolute
                # vertices under it: st002's big pieces, its ground among them.
                root = local[root_record]
                if not root_xz and abs(local[record][0] - root[0]) < 1.0 and abs(local[record][2] - root[2]) < 1.0:
                    world[record] = (base[0], base[1] + local[record][1], base[2])
                else:
                    world[record] = tuple(base[k] + local[record][k] for k in range(3))
        return world[record]

    for record in range(node_count):
        resolve(record)
    matrices = [None] * node_count

    def chain(record, depth=0):
        if depth > node_count:
            raise ValueError("Cycle in SCM node hierarchy")
        if matrices[record] is None:
            m = _matrix(records[record])
            matrices[record] = m if parent[record] < 0 else _compose(chain(parent[record], depth + 1), m)
        return matrices[record]

    if transform:
        for record in range(node_count):
            chain(record)
    object_node = {}
    for record, obj in enumerate(objmap):
        if obj != 0xFF:
            if obj in object_node:
                raise ValueError(f"Object {obj} mapped to two nodes")
            object_node[obj] = record

    objects = []
    for index in range(object_count):
        record = 0x30 + index * 48
        flags, total, mesh_offset = struct.unpack_from("<HHI", data, record)
        count = flags & 0xFF
        offset = world[object_node[index]] if index in object_node else (0.0, 0.0, 0.0)
        submeshes, seen = [], 0
        for k in range(count):
            header = mesh_offset + 0x30 * k
            vc, texture = struct.unpack_from("<HH", data, header)
            ptrs = struct.unpack_from("<8I", data, header + 4)
            if (ptrs[5] == 0x30) != (k + 1 < count):
                raise ValueError(f"Object {index} sub-mesh {k}: chain pointer disagrees with flag count")
            positions = [tuple(v + o for v, o in zip(struct.unpack_from("<3f", data, ptrs[2] + i * 12), offset))
                         for i in range(vc)]
            normals = [struct.unpack_from("<3f", data, ptrs[3] + i * 12) for i in range(vc)]
            if transform and index in object_node:
                m = matrices[object_node[index]]
                raw = [struct.unpack_from("<3f", data, ptrs[2] + i * 12) for i in range(vc)]
                positions = [tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3)) for p in raw]
                normals = [tuple(m[r][0] * q[0] + m[r][1] * q[1] + m[r][2] * q[2] for r in range(3)) for q in normals]
            uvs = [tuple(c / 4096.0 for c in struct.unpack_from("<2h", data, ptrs[4] + i * 4)) for i in range(vc)]
            colors = [data[ptrs[7] + i * 4:ptrs[7] + i * 4 + 4] for i in range(vc)]
            faces = strip_faces([bool(c[3] & 2) for c in colors])
            submeshes.append({"texture": texture, "positions": positions, "normals": normals,
                              "uvs": uvs, "colors": [tuple(c[:3]) for c in colors], "faces": faces})
            seen += vc
        if seen != total:
            raise ValueError(f"Object {index}: sub-mesh vertices {seen} != record total {total}")
        objects.append({"index": index, "flags": flags, "node": object_node.get(index), "submeshes": submeshes})
    return {"objects": objects, "texture_count": texture_count, "node_world": world}
