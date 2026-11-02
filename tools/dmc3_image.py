#!/usr/bin/env python3
"""Random-access ISO9660 and AFS tooling for the DMC3 PSP port."""

import argparse
import datetime as dt
import json
import struct
from pathlib import Path, PurePosixPath

SECTOR_SIZE = 2048
BOOT_FILES = ("SYSTEM.CNF", "IOPRP280.IMG", "MODULE.BIN", "0FLIST.DIR", "SLUS_213.61")


def now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) // boundary * boundary


def canonical_name(raw: bytes) -> str:
    return raw.decode("ascii", "replace").split(";", 1)[0].upper()


def safe_relative(name: str) -> Path:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"Unsafe archive path: {name}")
    clean = [part for part in path.parts if part not in ("", ".")]
    if not clean:
        raise ValueError("Empty archive path")
    return Path(*clean)


class Iso9660:
    def __init__(self, path: Path):
        self.path = path
        self.handle = path.open("rb")
        pvd = self.read_at(16 * SECTOR_SIZE, SECTOR_SIZE)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise ValueError("No ISO9660 primary volume descriptor at sector 16")
        root = pvd[156:190]
        extent, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        self.files = {}
        self.records = []
        for record in self._directory_records(extent, size):
            name = canonical_name(record["raw_name"])
            entry = {
                "name": name,
                "lba": record["extent"],
                "offset": record["extent"] * SECTOR_SIZE,
                "size": record["size"],
                "flags": record["flags"],
            }
            self.records.append(entry)
            if not record["flags"] & 2:
                self.files[name] = entry

    def close(self):
        self.handle.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def read_at(self, offset: int, size: int) -> bytes:
        self.handle.seek(offset)
        data = self.handle.read(size)
        if len(data) != size:
            raise ValueError(f"Short read at 0x{offset:x}: wanted {size}, got {len(data)}")
        return data

    def _directory_records(self, extent: int, size: int):
        data = self.read_at(extent * SECTOR_SIZE, size)
        offset = 0
        while offset < len(data):
            length = data[offset]
            if length == 0:
                offset = align(offset + 1, SECTOR_SIZE)
                continue
            if offset + length > len(data) or length < 34:
                raise ValueError("Malformed ISO9660 directory record")
            name_length = data[offset + 32]
            raw_name = data[offset + 33:offset + 33 + name_length]
            if raw_name not in (b"\x00", b"\x01"):
                yield {
                    "extent": struct.unpack_from("<I", data, offset + 2)[0],
                    "size": struct.unpack_from("<I", data, offset + 10)[0],
                    "flags": data[offset + 25],
                    "raw_name": raw_name,
                }
            offset += length

    def read_file(self, name: str, offset: int = 0, size: int | None = None) -> bytes:
        entry = self.files[name.upper()]
        size = entry["size"] - offset if size is None else size
        if offset < 0 or size < 0 or offset + size > entry["size"]:
            raise ValueError("Read outside ISO file")
        return self.read_at(entry["offset"] + offset, size)

    def extract_file(self, name: str, output_dir: Path) -> Path:
        entry = self.files[name.upper()]
        output_dir.mkdir(parents=True, exist_ok=True)
        destination = output_dir / safe_relative(entry["name"])
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        with temporary.open("wb") as output:
            remaining = entry["size"]
            position = 0
            while remaining:
                chunk = self.read_file(entry["name"], position, min(4 * 1024 * 1024, remaining))
                output.write(chunk)
                position += len(chunk)
                remaining -= len(chunk)
        temporary.replace(destination)
        return destination


class AfsArchive:
    def __init__(self, iso: Iso9660, name: str):
        self.iso = iso
        self.name = name.upper()
        self.entry = iso.files[self.name]
        self.base = self.entry["offset"]
        self.size = self.entry["size"]
        header = iso.read_file(self.name, 0, 8)
        if header[:4] != b"AFS\0":
            raise ValueError(f"{self.name} is not an AFS archive")
        self.count = struct.unpack_from("<I", header, 4)[0]
        if not 0 < self.count <= 100000:
            raise ValueError(f"Implausible AFS entry count: {self.count}")
        table = iso.read_file(self.name, 8, self.count * 8 + 8)
        self.entries = []
        for index in range(self.count):
            offset, size = struct.unpack_from("<II", table, index * 8)
            if offset > self.size or size > self.size - offset:
                raise ValueError(f"AFS entry {index} exceeds {self.name}")
            self.entries.append({"index": index, "name": "", "offset": offset, "size": size})
        name_offset, name_size = struct.unpack_from("<II", table, self.count * 8)
        self.name_table = {"offset": name_offset, "size": name_size}
        if name_size and name_offset <= self.size and name_size <= self.size - name_offset and name_size <= 64 * 1024 * 1024:
            names = iso.read_file(self.name, name_offset, name_size)
            record_size = name_size // self.count
            if record_size >= 32:
                for index, entry in enumerate(self.entries):
                    start = index * record_size
                    raw = names[start:start + 32].split(b"\0", 1)[0]
                    entry["name"] = raw.decode("cp932", "replace")

    def manifest(self):
        return {
            "generated_utc": now(),
            "source_iso": self.iso.path.name,
            "archive": self.name,
            "archive_offset": self.base,
            "archive_size": self.size,
            "entry_count": self.count,
            "name_table": self.name_table,
            "entries": self.entries,
        }

    def extract_member(self, index: int, output_dir: Path) -> Path:
        entry = self.entries[index]
        relative = safe_relative(entry["name"] or f"{index:05d}.bin")
        destination = output_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        with temporary.open("wb") as output:
            remaining = entry["size"]
            position = 0
            while remaining:
                size = min(4 * 1024 * 1024, remaining)
                output.write(self.iso.read_file(self.name, entry["offset"] + position, size))
                position += size
                remaining -= size
        temporary.replace(destination)
        return destination


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def inventory(iso_path: Path, output: Path) -> None:
    with Iso9660(iso_path) as iso:
        root_manifest = {
            "generated_utc": now(),
            "source_iso": str(iso_path),
            "iso_size": iso_path.stat().st_size,
            "files": iso.records,
        }
        write_json(output / "disc" / "root.json", root_manifest)
        print(f"ISO: {len(iso.records)} root entries")
        for entry in iso.records:
            print(f"  {entry['name']}: offset=0x{entry['offset']:x}, size={entry['size']}")
        for name in ("GDATA.AFS", "MDATA1.AFS", "MDATA2.AFS", "SDATA.AFS"):
            archive = AfsArchive(iso, name)
            manifest = archive.manifest()
            write_json(output / "afs" / f"{name}.json", manifest)
            named = sum(bool(entry["name"]) for entry in archive.entries)
            total = sum(entry["size"] for entry in archive.entries)
            print(f"{name}: {archive.count} entries, {named} named, {total} data bytes")
            for entry in archive.entries[:8]:
                print(f"  [{entry['index']}] {entry['name'] or '<unnamed>'} offset=0x{entry['offset']:x} size={entry['size']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inv = sub.add_parser("inventory")
    inv.add_argument("iso", type=Path)
    inv.add_argument("--output", type=Path, default=Path("analysis"))
    boot = sub.add_parser("extract-boot")
    boot.add_argument("iso", type=Path)
    boot.add_argument("--output", type=Path, default=Path("analysis/disc"))
    extract = sub.add_parser("extract-afs-member")
    extract.add_argument("iso", type=Path)
    extract.add_argument("archive")
    extract.add_argument("index", type=int)
    extract.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "inventory":
        inventory(args.iso, args.output)
    elif args.command == "extract-boot":
        with Iso9660(args.iso) as iso:
            for name in BOOT_FILES:
                print(iso.extract_file(name, args.output))
    elif args.command == "extract-afs-member":
        with Iso9660(args.iso) as iso:
            archive = AfsArchive(iso, args.archive)
            if args.index < 0 or args.index >= archive.count:
                parser.error("AFS member index is out of range")
            print(archive.extract_member(args.index, args.output))


if __name__ == "__main__":
    main()
