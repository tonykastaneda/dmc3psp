#!/usr/bin/env python3
"""Convert PS2 DMC3 MOD geometry to OBJ with a JSON structural manifest."""

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from dmc3_image import now, write_json


def finite(values):
    return all(math.isfinite(value) for value in values)


def parse_mod(data: bytes):
    if len(data) < 0x30 or data[:4] != b"MOD ":
        raise ValueError("Not a PS2 DMC3 MOD file")
    version = struct.unpack_from("<f", data, 4)[0]
    object_count, bone_count, texture_count = struct.unpack_from("<BBB", data, 0x10)
    skeleton_offset = struct.unpack_from("<I", data, 0x1C)[0]
    if not 0 < object_count <= 4096 or 0x30 + object_count * 48 > len(data):
        raise ValueError("Invalid MOD object count")
    if skeleton_offset >= len(data):
        raise ValueError("MOD skeleton offset exceeds file")
    objects = []
    all_positions = []
    for index in range(object_count):
        record = 0x30 + index * 48
        flags = struct.unpack_from("<H", data, record)[0]
        vertex_count, mesh_offset = struct.unpack_from("<HI", data, record + 2)
        center = struct.unpack_from("<3f", data, record + 36)
        if not 3 <= vertex_count <= 1000000 or mesh_offset + 36 > len(data):
            raise ValueError(f"Invalid mesh record for object {index}")
        header_count = struct.unpack_from("<H", data, mesh_offset + 2)[0]
        pointers = struct.unpack_from("<8I", data, mesh_offset + 4)
        position_offset, normal_offset, uv_offset, bone_offset, weight_offset = pointers[2:7]
        if (position_offset + vertex_count * 12 > len(data) or
                normal_offset + vertex_count * 12 > len(data) or
                uv_offset + vertex_count * 4 > len(data)):
            raise ValueError(f"Mesh buffers exceed MOD file for object {index}")
        positions = [struct.unpack_from("<3f", data, position_offset + i * 12) for i in range(vertex_count)]
        normals = [struct.unpack_from("<3f", data, normal_offset + i * 12) for i in range(vertex_count)]
        raw_uvs = [struct.unpack_from("<2h", data, uv_offset + i * 4) for i in range(vertex_count)]
        if (not all(finite(vertex) for vertex in positions) or
                not all(finite(normal) for normal in normals)):
            raise ValueError(f"Non-finite geometry in object {index}")
        faces = []
        for i in range(vertex_count - 2):
            face = (i, i + 1, i + 2) if i % 2 == 0 else (i + 1, i, i + 2)
            if len(set(face)) == 3:
                faces.append(face)
        objects.append({
            "index": index,
            "flags": flags,
            "vertex_count": vertex_count,
            "header_count": header_count,
            "mesh_offset": mesh_offset,
            "position_offset": position_offset,
            "normal_offset": normal_offset,
            "uv_offset": uv_offset,
            "bone_offset": bone_offset,
            "weight_offset": weight_offset,
            "command_fields": list(pointers[:2]),
            "center": center,
            "positions": positions,
            "normals": normals,
            "uvs": [(uv[0] / 4096.0, uv[1] / 4096.0) for uv in raw_uvs],
            "faces": faces,
        })
        all_positions.extend(positions)
    bounds = None
    if all_positions:
        bounds = {
            "min": [min(vertex[axis] for vertex in all_positions) for axis in range(3)],
            "max": [max(vertex[axis] for vertex in all_positions) for axis in range(3)],
        }
    metadata = {
        "generated_utc": now(),
        "version": version,
        "object_count": object_count,
        "bone_count": bone_count,
        "texture_count": texture_count,
        "skeleton_offset": skeleton_offset,
        "vertex_count": sum(obj["vertex_count"] for obj in objects),
        "triangle_count": sum(len(obj["faces"]) for obj in objects),
        "bounds": bounds,
    }
    return objects, metadata


def convert(path: Path, output: Path) -> None:
    data = path.read_bytes()
    objects, metadata = parse_mod(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    vertex_base = 1
    with output.open("w", encoding="ascii", newline="\n") as obj:
        obj.write(f"# DMC3 PS2 MOD converted from {path.name}\n")
        obj.write(f"# objects={len(objects)} vertices={metadata['vertex_count']} triangles={metadata['triangle_count']}\n")
        for item in objects:
            obj.write(f"o object_{item['index']:03d}\n")
            for position in item["positions"]:
                obj.write("v %.9g %.9g %.9g\n" % position)
            for uv in item["uvs"]:
                obj.write("vt %.9g %.9g\n" % uv)
            for normal in item["normals"]:
                obj.write("vn %.9g %.9g %.9g\n" % normal)
            obj.write(f"usemtl object_{item['index']:03d}\n")
            for face in item["faces"]:
                a, b, c = (vertex_base + index for index in face)
                obj.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
            vertex_base += item["vertex_count"]
    public_objects = []
    for item in objects:
        public_objects.append({key: value for key, value in item.items() if key not in ("positions", "normals", "uvs", "faces")} | {"triangle_count": len(item["faces"])})
    metadata.update({
        "source": str(path),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "obj": str(output),
        "objects": public_objects,
    })
    write_json(output.with_suffix(output.suffix + ".json"), metadata)
    print(f"{path.name}: {len(objects)} objects, {metadata['vertex_count']} vertices, {metadata['triangle_count']} triangles -> {output}")
    print(f"Bounds: {metadata['bounds']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    convert(args.input, args.output)


if __name__ == "__main__":
    main()
