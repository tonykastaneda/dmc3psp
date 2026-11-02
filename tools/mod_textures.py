#!/usr/bin/env python3
"""Extract TIM2 textures from a PS2 DMC3 model's 0x800-sector container."""

import argparse
import json
import struct
from pathlib import Path

from dmc3_image import now, write_json
from tim2 import convert


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.input.read_bytes()
    if len(data) < 0x800:
        raise ValueError("Texture container is smaller than one sector")
    count = struct.unpack_from("<I", data, 0)[0]
    if not 1 <= count <= 256 or 4 + count * 4 > 0x800:
        raise ValueError("Invalid model texture count")
    sectors = list(struct.unpack_from("<" + "I" * count, data, 4))
    if any(size == 0 for size in sectors):
        raise ValueError("Empty model texture sector span")
    offset = 0x800
    records = []
    for index, sector_count in enumerate(sectors):
        size = sector_count * 0x800
        if offset + size > len(data):
            raise ValueError("Texture sector table exceeds container")
        image = data[offset:offset + size]
        if image[:4] != b"TIM2":
            raise ValueError(f"Texture {index} does not start with TIM2 at 0x{offset:x}")
        destination = args.output / f"texture_{index:03d}"
        destination.mkdir(parents=True, exist_ok=True)
        source = destination / f"texture_{index:03d}.tim2"
        source.write_bytes(image)
        convert(source, destination, True)
        records.append({"index": index, "offset": offset, "sectors": sector_count, "bytes": size})
        offset += size
    write_json(args.output / "textures.json", {
        "generated_utc": now(),
        "source": str(args.input),
        "texture_count": count,
        "textures": records,
        "bytes_consumed": offset,
        "container_size": len(data),
    })
    print(f"Converted {count} model textures; consumed 0x{offset:x}/0x{len(data):x} bytes")


if __name__ == "__main__":
    main()
