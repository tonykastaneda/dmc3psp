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
  translation (no rotation). Object k is placed at the world position of
  the record that table2 maps to k.
"""

import struct

from mod_skeleton import strip_faces


def parse_scm(data: bytes):
    if data[:4] != b"SCM ":
        raise ValueError("Not an SCM")
    object_count, node_count, texture_count = data[0x10], data[0x11], data[0x12]
    block = struct.unpack_from("<I", data, 0x1C)[0]
    tables = struct.unpack_from("<4I", data, block)
    hier = data[block + tables[0]:block + tables[0] + node_count]
    remap = data[block + tables[1]:block + tables[1] + node_count]
    objmap = data[block + tables[2]:block + tables[2] + node_count]
    local = [struct.unpack_from("<3f", data, block + tables[3] + r * 32) for r in range(node_count)]
    parent = [-1] * node_count
    for node in range(node_count):
        parent[remap[node]] = -1 if hier[node] == 0xFF else hier[node]
    world = [None] * node_count

    def resolve(record, depth=0):
        if depth > node_count:
            raise ValueError("Cycle in SCM node hierarchy")
        if world[record] is None:
            base = (0.0, 0.0, 0.0) if parent[record] < 0 else resolve(parent[record], depth + 1)
            world[record] = tuple(base[k] + local[record][k] for k in range(3))
        return world[record]

    for record in range(node_count):
        resolve(record)
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
