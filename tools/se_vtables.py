#!/usr/bin/env python3
"""Find C++ RTTI type_info objects and vtables for named classes in the SE
executable (analysis/disc/SLUS_213.61, MIPS R5900 little-endian ELF).

GCC 2.9x layout (verified on CPlWpSword): a type_info object is {name
pointer, base type_info}; each vtable is [type_info][this-offset][methods..]
(three per class here: offsets 0, -64, -160, as in the decomp's rtti.txt). We scan
the loadable data for both and print each vtable's function pointers.
"""

import argparse
import struct
from pathlib import Path


def load_elf(path: Path):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF")
    phoff, = struct.unpack_from("<I", data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segments = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from("<8I", data, phoff + i * phentsize)
        if p_type == 1:
            segments.append((p_vaddr, p_offset, p_filesz))
    return data, segments


def v2o(segments, vaddr):
    for va, off, size in segments:
        if va <= vaddr < va + size:
            return off + vaddr - va
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("elf", type=Path)
    ap.add_argument("classes", nargs="+")
    ap.add_argument("--code-lo", type=lambda x: int(x, 0), default=0x100000)
    ap.add_argument("--code-hi", type=lambda x: int(x, 0), default=0x560000)
    args = ap.parse_args()
    data, segments = load_elf(args.elf)
    words = {}
    for va, off, size in segments:
        for i in range(0, size - 3, 4):
            words[va + i] = struct.unpack_from("<I", data, off + i)[0]
    by_value = {}
    for addr, w in words.items():
        by_value.setdefault(w, []).append(addr)
    for name in args.classes:
        needle = name.encode() + b"\0"
        found = []
        for va, off, size in segments:
            start = 0
            blob = data[off:off + size]
            while True:
                k = blob.find(needle, start)
                if k < 0:
                    break
                found.append(va + k)
                start = k + 1
        for s in found:
            tinfos = by_value.get(s, [])
            for ti_name_slot in tinfos:
                # type_info candidates: the word before/at the name slot
                for ti in (ti_name_slot - 4, ti_name_slot):
                    for slot in by_value.get(ti, []):
                        funcs = []
                        a = slot + 8  # [type_info][this-offset][methods...]
                        while a in words and args.code_lo <= words[a] < args.code_hi and len(funcs) < 80:
                            funcs.append(words[a])
                            a += 4
                        if len(funcs) >= 3:
                            print(f"{name}: string {s:#x} typeinfo {ti:#x} vtable slot {slot:#x} -> {len(funcs)} funcs")
                            print("  " + " ".join(f"{f:08X}" for f in funcs))


if __name__ == "__main__":
    main()
