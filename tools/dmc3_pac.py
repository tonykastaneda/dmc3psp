#!/usr/bin/env python3
"""Inventory and extract DMC3 PAC containers, including nested PAC chunks."""

import argparse
import collections
import json
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, safe_relative, write_json

MAGIC_EXTENSIONS = {b"PAC\0": ".pac", b"MOD ": ".mod", b"SHW ": ".shw", b"MOT ": ".mot"}


class PacReader:
    def __init__(self, read_at, size: int, depth: int = 0):
        self.read_at = read_at
        self.size = size
        header = read_at(0, 8)
        if len(header) != 8 or header[:4] != b"PAC\0":
            raise ValueError("Missing PAC\\0 signature")
        self.count = struct.unpack_from("<I", header, 4)[0]
        if not 0 < self.count <= 100000 or 8 + self.count * 4 > size:
            raise ValueError(f"Invalid PAC chunk count: {self.count}")
        table = read_at(8, self.count * 4)
        self.offsets = list(struct.unpack("<" + "I" * self.count, table))
        previous = 8 + self.count * 4
        for offset in self.offsets:
            if offset == 0:
                continue
            if offset < previous or offset >= size:
                raise ValueError(f"Invalid/non-monotonic PAC offset: 0x{offset:x}")
            previous = offset
        self.entries = []
        for index, offset in enumerate(self.offsets):
            if offset == 0:
                self.entries.append({
                    "index": index, "offset": 0, "size": 0,
                    "signature_hex": "", "magic": "null",
                })
                continue
            end = next((later for later in self.offsets[index + 1:] if later != 0), size)
            signature = read_at(offset, min(16, end - offset))
            self.entries.append({
                "index": index,
                "offset": offset,
                "size": end - offset,
                "signature_hex": signature.hex(),
                "magic": magic_name(signature[:4]),
            })


def magic_name(magic: bytes) -> str:
    if len(magic) < 4:
        return magic.hex()
    if all(value == 0 or 32 <= value < 127 for value in magic):
        return "".join("\\0" if value == 0 else chr(value) for value in magic)
    if magic[:1] in (b";", b"#"):
        return "text"
    return "hex:" + magic.hex()


def extension_for(entry) -> str:
    if entry["magic"] == "text":
        return ".txt"
    return MAGIC_EXTENSIONS.get(bytes.fromhex(entry["signature_hex"][:8]), ".bin")


def inventory_gdata(iso_path: Path, output: Path, nested: bool) -> None:
    output.mkdir(parents=True, exist_ok=True)
    jsonl = output / "gdata-packs.jsonl"
    magic_counts = collections.Counter()
    container_counts = collections.Counter()
    failures = []
    total_chunks = 0
    nested_pacs = 0
    non_pac = 0
    with Iso9660(iso_path) as iso, jsonl.open("w", encoding="utf-8") as stream:
        afs = AfsArchive(iso, "GDATA.AFS")
        for slot, member in enumerate(afs.entries):
            record = {"slot": slot, "offset": member["offset"], "size": member["size"]}
            try:
                def read_at(offset, size, base=member["offset"]):
                    return iso.read_file("GDATA.AFS", base + offset, size)
                signature = read_at(0, min(16, member["size"]))
                container_magic = magic_name(signature[:4])
                container_counts[container_magic] += 1
                record["container_magic"] = container_magic
                if signature[:4] != b"PAC\0":
                    record["signature_hex"] = signature.hex()
                    record["chunks"] = []
                    non_pac += 1
                    stream.write(json.dumps(record, ensure_ascii=False) + "\n")
                    continue
                pac = PacReader(read_at, member["size"])
                record["chunks"] = pac.entries
                total_chunks += pac.count
                for chunk in pac.entries:
                    magic_counts[chunk["magic"]] += 1
                    if nested and chunk["magic"] == "PAC\\0":
                        try:
                            child = PacReader(lambda off, size, base=chunk["offset"]: read_at(base + off, size), chunk["size"], 1)
                            chunk["nested_chunk_count"] = child.count
                            chunk["nested_magic"] = [item["magic"] for item in child.entries]
                            nested_pacs += 1
                            magic_counts.update(item["magic"] for item in child.entries)
                        except ValueError as error:
                            chunk["nested_error"] = str(error)
            except ValueError as error:
                record["error"] = str(error)
                failures.append({"slot": slot, "error": str(error)})
            stream.write(json.dumps(record, ensure_ascii=False) + "\n")
            if (slot + 1) % 250 == 0:
                print(f"Inspected {slot + 1}/{len(afs.entries)} GDATA PAC members")
        summary = {
            "generated_utc": now(),
            "source_iso": iso_path.name,
            "archive": "GDATA.AFS",
            "pack_count": len(afs.entries),
            "pac_count": len(afs.entries) - non_pac,
            "non_pac_count": non_pac,
            "malformed_pack_count": len(failures),
            "top_level_chunk_count": total_chunks,
            "nested_pac_count": nested_pacs,
            "container_magic_counts": dict(container_counts.most_common()),
            "magic_counts": dict(magic_counts.most_common()),
            "failures": failures,
        }
        write_json(output / "gdata-summary.json", summary)
    print(json.dumps({key: value for key, value in summary.items() if key != "failures"}, indent=2))


def extract_pac(path: Path, output: Path, recursive: bool) -> None:
    with path.open("rb") as handle:
        def read_at(offset, size):
            handle.seek(offset)
            data = handle.read(size)
            if len(data) != size:
                raise ValueError("Short PAC read")
            return data
        pac = PacReader(read_at, path.stat().st_size)
        output.mkdir(parents=True, exist_ok=True)
        for chunk in pac.entries:
            destination = output / f"chunk_{chunk['index']:03d}{extension_for(chunk)}"
            with destination.open("wb") as out:
                remaining = chunk["size"]
                position = chunk["offset"]
                while remaining:
                    size = min(4 * 1024 * 1024, remaining)
                    out.write(read_at(position, size))
                    position += size
                    remaining -= size
            print(destination)
            if recursive and chunk["magic"] == "PAC\\0":
                extract_pac(destination, output / f"chunk_{chunk['index']:03d}_nested", True)
    write_json(output / "pac.json", {
        "source": str(path), "chunks": pac.entries, "generated_utc": now()
    })


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inv = sub.add_parser("inventory-gdata")
    inv.add_argument("iso", type=Path)
    inv.add_argument("--output", type=Path, default=Path("analysis/pac"))
    inv.add_argument("--no-nested", action="store_true")
    ext = sub.add_parser("extract")
    ext.add_argument("pac", type=Path)
    ext.add_argument("--output", type=Path, required=True)
    ext.add_argument("--recursive", action="store_true")
    args = parser.parse_args()
    if args.command == "inventory-gdata":
        inventory_gdata(args.iso, args.output, not args.no_nested)
    else:
        extract_pac(args.pac, args.output, args.recursive)


if __name__ == "__main__":
    main()
