#!/usr/bin/env python3
"""Inspect TIM2 pictures and convert base levels to PNG/RGBA using stdlib."""

import argparse
import binascii
import hashlib
import json
import struct
import zlib
from pathlib import Path

from dmc3_image import now, write_json

BPP_NAMES = {1: "RGBA16", 2: "RGB24", 3: "RGBA32", 4: "IDTEX4", 5: "IDTEX8"}


def scale5(value: int) -> int:
    return (value << 3) | (value >> 2)


def alpha32(value: int) -> int:
    return min(255, value * 2)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows.extend(rgba[y * stride:(y + 1) * stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    payload += png_chunk(b"IEND", b"")
    path.write_bytes(payload)


def validate_png(path: Path, width: int, height: int) -> None:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG output: {path}")
    actual = struct.unpack_from(">II", data, 16)
    if actual != (width, height):
        raise ValueError(f"PNG dimensions {actual} do not match {(width, height)}")


def deshuffle_csm1(palette: list[tuple[int, int, int, int]]) -> list[tuple[int, int, int, int]]:
    if len(palette) != 256:
        return palette
    result = palette[:]
    for page in range(8):
        base = page * 32
        result[base + 8:base + 16] = palette[base + 16:base + 24]
        result[base + 16:base + 24] = palette[base + 8:base + 16]
    return result


def parse_palette(data: bytes, colors: int, clut_type: int, linear: bool, csm: int):
    kind = clut_type & 0x0F
    sizes = {1: 2, 2: 3, 3: 4}
    if kind not in sizes:
        raise ValueError(f"Unsupported CLUT color type {kind}")
    width = sizes[kind]
    required = colors * width
    if required > len(data):
        raise ValueError("CLUT data is shorter than the declared color count")
    palette = []
    for index in range(colors):
        item = data[index * width:(index + 1) * width]
        if kind == 1:
            value = struct.unpack("<H", item)[0]
            palette.append((scale5(value & 31), scale5((value >> 5) & 31),
                            scale5((value >> 10) & 31), 255 if value & 0x8000 else 0))
        elif kind == 2:
            palette.append((item[0], item[1], item[2], 255))
        else:
            palette.append((item[0], item[1], item[2], alpha32(item[3])))
    if csm == 0 and colors == 256 and not linear:
        palette = deshuffle_csm1(palette)
    return palette


def decode_picture(data: bytes, offset: int):
    if offset + 48 > len(data):
        raise ValueError("Truncated TIM2 picture header")
    fields = struct.unpack_from("<IIIHHBBBBHHQQII", data, offset)
    (total, clut_size, image_size, header_size, colors, picture_format, mipmaps,
     clut_type, bpp, width, height, gs_tex0, gs_tex1, gs_regs, gs_clut) = fields
    if bpp not in BPP_NAMES:
        raise ValueError(f"Unsupported TIM2 image color type {bpp}")
    if not 1 <= width <= 4096 or not 1 <= height <= 4096 or not 1 <= mipmaps <= 16:
        raise ValueError("Invalid TIM2 dimensions or mipmap count")
    if header_size < 48 or offset + header_size > len(data):
        raise ValueError("Invalid TIM2 picture header size")
    if total < header_size or offset + total > len(data):
        raise ValueError("TIM2 picture exceeds file bounds")
    if image_size > total or clut_size > total - header_size:
        raise ValueError("TIM2 image/CLUT sizes exceed picture bounds")
    image_start = offset + header_size
    clut_start = image_start + image_size
    if clut_start + clut_size > offset + total:
        raise ValueError("TIM2 image and CLUT overlap the next picture")
    image = data[image_start:clut_start]
    clut = data[clut_start:clut_start + clut_size]
    palette = []
    if bpp in (4, 5):
        if colors <= 0:
            raise ValueError("Indexed TIM2 picture has no palette")
        palette = parse_palette(clut, colors, clut_type, bool(clut_type & 0x80), picture_format)
    rgba = bytearray(width * height * 4)
    pixels = width * height
    for index in range(pixels):
        if bpp == 4:
            packed = image[index // 2]
            color_index = packed & 15 if index % 2 == 0 else packed >> 4
            if color_index >= len(palette):
                raise ValueError("TIM2 4-bit pixel index exceeds palette")
            color = palette[color_index]
        elif bpp == 5:
            color_index = image[index]
            if color_index >= len(palette):
                raise ValueError("TIM2 8-bit pixel index exceeds palette")
            color = palette[color_index]
        elif bpp == 1:
            value = struct.unpack_from("<H", image, index * 2)[0]
            color = (scale5(value & 31), scale5((value >> 5) & 31),
                     scale5((value >> 10) & 31), 255 if value & 0x8000 else 0)
        elif bpp == 2:
            color = (image[index * 3], image[index * 3 + 1], image[index * 3 + 2], 255)
        else:
            base = index * 4
            color = (image[base], image[base + 1], image[base + 2], alpha32(image[base + 3]))
        rgba[index * 4:index * 4 + 4] = bytes(color)
    metadata = {
        "offset": offset,
        "total_size": total,
        "clut_size": clut_size,
        "image_size": image_size,
        "header_size": header_size,
        "colors": colors,
        "picture_format": picture_format,
        "mipmap_count": mipmaps,
        "clut_type": clut_type,
        "image_color_type": bpp,
        "format": BPP_NAMES[bpp],
        "width": width,
        "height": height,
        "gs_tex0": f"0x{gs_tex0:016x}",
        "gs_tex1": f"0x{gs_tex1:016x}",
        "gs_regs": f"0x{gs_regs:08x}",
        "gs_clut": f"0x{gs_clut:08x}",
    }
    return total, bytes(rgba), metadata


def convert(path: Path, output: Path, raw: bool) -> None:
    data = path.read_bytes()
    if len(data) < 64:
        raise ValueError("TIM2 file is too small")
    magic, revision, file_format, count = struct.unpack_from("<4sBBH", data, 0)
    if magic != b"TIM2" or not 1 <= count <= 1024:
        raise ValueError("Not a supported TIM2 file")
    output.mkdir(parents=True, exist_ok=True)
    offset = 0x80 if file_format == 1 else 0x10
    pictures = []
    for index in range(count):
        total, rgba, metadata = decode_picture(data, offset)
        destination = output / f"{path.stem}_p{index:03d}.png"
        write_png(destination, metadata["width"], metadata["height"], rgba)
        validate_png(destination, metadata["width"], metadata["height"])
        metadata["png"] = destination.name
        if raw:
            raw_path = destination.with_suffix(".rgba")
            raw_path.write_bytes(rgba)
            metadata["rgba"] = raw_path.name
        pictures.append(metadata)
        print(f"[{index}] {metadata['width']}x{metadata['height']} {metadata['format']} -> {destination}")
        offset += total
    manifest = {
        "generated_utc": now(),
        "source": str(path),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "revision": revision,
        "file_format": file_format,
        "picture_count": count,
        "bytes_consumed": offset,
        "file_size": len(data),
        "pictures": pictures,
    }
    write_json(output / f"{path.stem}.json", manifest)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--raw", action="store_true", help="also write raw RGBA32 bytes")
    args = parser.parse_args()
    convert(args.input, args.output, args.raw)


if __name__ == "__main__":
    main()
