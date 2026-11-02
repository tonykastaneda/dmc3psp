#!/usr/bin/env python3
"""Generate freestanding C data for the first textured DMC3 PSP model."""

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, write_json
from dmc3_pac import PacReader
from mod_ps2 import parse_mod
from tim2 import decode_picture

SLOT = 0
ROTATE_COS = 0.8660254037844386
ROTATE_SIN = 0.5


def c_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("Non-finite runtime value")
    text = f"{value:.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def array_lines(values, formatter, per_line=6):
    lines = []
    for index in range(0, len(values), per_line):
        lines.append("    " + ", ".join(formatter(value) for value in values[index:index + per_line]) + ",")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/psp/generated"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        member = afs.entries[SLOT]
        def read_at(offset, size):
            return iso.read_file("GDATA.AFS", member["offset"] + offset, size)
        pac = PacReader(read_at, member["size"])
        texture_chunk = pac.entries[0]
        model_chunk = pac.entries[1]
        texture_data = read_at(texture_chunk["offset"], texture_chunk["size"])
        model_data = read_at(model_chunk["offset"], model_chunk["size"])

    objects, model_meta = parse_mod(model_data)
    skipped_objects = [item["index"] for item in objects if item["flags"] & 0x0002]
    # Bit-1 objects use a separate deformation path; keep them out of the static runtime.
    objects = [item for item in objects if not item["flags"] & 0x0002]
    static_positions = [position for item in objects for position in item["positions"]]
    model_meta["bounds"] = {
        "min": [min(vertex[axis] for vertex in static_positions) for axis in range(3)],
        "max": [max(vertex[axis] for vertex in static_positions) for axis in range(3)],
    }
    texture_count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from("<" + "I" * texture_count, texture_data, 4)
    textures = []
    offset = 0x800
    for index, sector_count in enumerate(sectors):
        size = sector_count * 0x800
        image = texture_data[offset:offset + size]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append({"width": picture["width"], "height": picture["height"], "rgba": rgba})
        offset += size
    if len(textures) != model_meta["texture_count"]:
        raise ValueError("MOD and texture-container counts disagree")

    positions = []
    normals = []
    uvs = []
    indices = []
    runtime_objects = []
    for item in objects:
        vertex_start = len(positions)
        index_start = len(indices)
        positions.extend(item["positions"])
        normals.extend(item["normals"])
        uvs.extend(item["uvs"])
        for face in item["faces"]:
            indices.extend(vertex_start + value for value in face)
        runtime_objects.append({
            "texture": item["header_count"],
            "index_start": index_start,
            "index_count": len(indices) - index_start,
        })
        if item["header_count"] >= len(textures):
            raise ValueError("Object texture index exceeds texture count")

    bounds = model_meta["bounds"]
    center = [(bounds["min"][axis] + bounds["max"][axis]) * 0.5 for axis in range(3)]
    rotated = []
    for x, y, z in positions:
        dx, dy, dz = x - center[0], y - center[1], z - center[2]
        rx = dx * ROTATE_COS - dz * ROTATE_SIN
        rz = dx * ROTATE_SIN + dz * ROTATE_COS
        rotated.append((rx, dy, rz))
    min_x = min(v[0] for v in rotated)
    max_x = max(v[0] for v in rotated)
    min_y = min(v[1] for v in rotated)
    max_y = max(v[1] for v in rotated)
    scale = min(440.0 / (max_x - min_x), 240.0 / (max_y - min_y))

    sections = [
        "#ifndef DMC3_MODEL_RUNTIME_H",
        "#define DMC3_MODEL_RUNTIME_H",
        "typedef struct { float x, y, z; } Dmc3Vec3;",
        "typedef struct { float u, v; } Dmc3Uv;",
        "typedef struct { unsigned int index_start, index_count, texture; } Dmc3ModelObject;",
        "typedef struct { unsigned short width, height; const unsigned char *rgba; } Dmc3Texture;",
        f"#define DMC3_MODEL_VERTEX_COUNT {len(positions)}",
        f"#define DMC3_MODEL_INDEX_COUNT {len(indices)}",
        f"#define DMC3_MODEL_OBJECT_COUNT {len(runtime_objects)}",
        f"#define DMC3_MODEL_TEXTURE_COUNT {len(textures)}",
        f"#define DMC3_MODEL_CENTER_X {c_float(center[0])}",
        f"#define DMC3_MODEL_CENTER_Y {c_float(center[1])}",
        f"#define DMC3_MODEL_CENTER_Z {c_float(center[2])}",
        f"#define DMC3_MODEL_SCALE {c_float(scale)}",
        "",
        "static const Dmc3Vec3 dmc3_model_positions[] = {",
        array_lines(positions, lambda v: "{" + ",".join(c_float(x) for x in v) + "}", 4),
        "};",
        "static const Dmc3Vec3 dmc3_model_normals[] = {",
        array_lines(normals, lambda v: "{" + ",".join(c_float(x) for x in v) + "}", 4),
        "};",
        "static const Dmc3Uv dmc3_model_uvs[] = {",
        array_lines(uvs, lambda v: "{" + ",".join(c_float(x) for x in v) + "}", 6),
        "};",
        "static const unsigned short dmc3_model_indices[] = {",
        array_lines(indices, str, 12),
        "};",
        "static const Dmc3ModelObject dmc3_model_objects[] = {",
        array_lines(runtime_objects, lambda v: "{" + f"{v['index_start']},{v['index_count']},{v['texture']}" + "}", 4),
        "};",
    ]
    for index, texture in enumerate(textures):
        sections.extend([
            f"static const unsigned char dmc3_model_texture_{index}[] = {{",
            array_lines(list(texture["rgba"]), lambda value: f"0x{value:02x}", 16),
            "};",
        ])
    sections.append("static const Dmc3Texture dmc3_model_textures[] = {")
    sections.extend(f"    {{{t['width']},{t['height']},dmc3_model_texture_{i}}}," for i, t in enumerate(textures))
    sections.extend(["};", "", "#endif", ""])
    destination = args.output / "model_runtime.h"
    destination.write_text("\n".join(sections), encoding="ascii")
    write_json(args.output / "model-runtime-source.json", {
        "generated_utc": now(),
        "source_iso": args.iso.name,
        "gdata_slot": SLOT,
        "model_sha256": hashlib.sha256(model_data).hexdigest(),
        "vertex_count": len(positions),
        "index_count": len(indices),
        "object_count": len(runtime_objects),
        "skipped_deformation_objects": skipped_objects,
        "texture_count": len(textures),
        "scale": scale,
        "header": str(destination),
    })
    print(f"Generated {destination}: {len(positions)} vertices, {len(indices) // 3} triangles, {len(textures)} textures")


if __name__ == "__main__":
    main()
