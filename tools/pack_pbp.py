#!/usr/bin/env python3
"""Create and validate a minimal homebrew EBOOT.PBP using only stdlib."""

import argparse
import struct
from pathlib import Path


def align4(value: int) -> int:
    return (value + 3) & ~3


def make_sfo(title: str) -> bytes:
    entries = [
        ("BOOTABLE", 4, struct.pack("<I", 1)),
        ("CATEGORY", 2, b"MG\0"),
        ("DISC_ID", 2, b"DMC30001\0"),
        ("DISC_VERSION", 2, b"1.00\0"),
        ("MEMSIZE", 4, struct.pack("<I", 1)),
        ("PARENTAL_LEVEL", 4, struct.pack("<I", 1)),
        ("PSP_SYSTEM_VER", 2, b"1.00\0"),
        ("REGION", 4, struct.pack("<I", 0x8000)),
        ("TITLE", 2, title.encode("utf-8") + b"\0"),
    ]
    entries.sort(key=lambda entry: entry[0])
    count = len(entries)
    key_offset = 20 + 16 * count
    keys = bytearray()
    name_offsets = []
    for name, _, _ in entries:
        name_offsets.append(len(keys))
        keys.extend(name.encode("ascii") + b"\0")
    value_offset = align4(key_offset + len(keys))
    values = bytearray()
    records = bytearray()
    for (name, fmt, raw), name_offset in zip(entries, name_offsets):
        value_size = len(raw)
        total_size = align4(value_size)
        data_offset = len(values)
        records.extend(struct.pack("<HBBIII", name_offset, 4, fmt, value_size, total_size, data_offset))
        values.extend(raw)
        values.extend(b"\0" * (total_size - value_size))
    header = struct.pack("<IIIII", 0x46535000, 0x101, key_offset, value_offset, count)
    padding = b"\0" * (value_offset - (len(header) + len(records) + len(keys)))
    return header + records + keys + padding + values


def pack(title: str, elf: Path, output: Path) -> None:
    elf_data = elf.read_bytes()
    if not elf_data.startswith(b"\x7fELF"):
        raise ValueError(f"Not an ELF file: {elf}")
    sfo = make_sfo(title)
    offsets = [40]
    sizes = [len(sfo), 0, 0, 0, 0, 0, len(elf_data), 0]
    for size in sizes[:-1]:
        offsets.append(offsets[-1] + size)
    header = struct.pack("<4s4s8I", b"\0PBP", b"\0\0\x01\0", *offsets)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + sfo + elf_data)


def validate(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 40 or data[:4] != b"\0PBP":
        raise ValueError("Invalid PBP header")
    offsets = struct.unpack_from("<8I", data, 8)
    if offsets[0] != 40 or offsets[-1] > len(data):
        raise ValueError("Invalid PBP member offsets")
    if data[offsets[0]:offsets[0] + 4] != b"\0PSF":
        raise ValueError("PARAM.SFO is missing")
    if data[offsets[6]:offsets[6] + 4] != b"\x7fELF":
        raise ValueError("DATA.PSP ELF is missing")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", default="DMC3 PSP Research Boot")
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate", type=Path)
    args = parser.parse_args()
    if args.validate:
        validate(args.validate)
        print(f"Validated {args.validate}")
        return
    if not args.elf or not args.output:
        parser.error("--elf and --output are required unless --validate is used")
    pack(args.title, args.elf, args.output)
    validate(args.output)
    print(f"Created {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
