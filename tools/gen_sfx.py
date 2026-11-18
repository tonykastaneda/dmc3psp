#!/usr/bin/env python3
"""The game's sound effects (docs/MUSIC.md, Sound effects): a sound bank resolved for psp/testroom/sfx.c.

A bank is three files: a table "dbsT" (u32 count at +12, then 32-byte records from +16, one per sound id), a "Head"
(chunks Prog, Smpl, Vagi at the offsets in its +0x14 / +0x18 / +0x1c) and its samples (SPU ADPCM). The game's EE sound
code reads them (FUN_00304110 plays an id: the table set per type by FUN_00304010, read by FUN_00304030):
  - a record: +0 its command (1 plays a note), +6 the program, +7 the note, +12 volume, +13 pan (0x40 centre),
    +14 i16 pitch in cents, +22 i16 the next id it chains to (-1 none);
  - Prog: u32 count - 1 at +8, offsets from +16; a program is a byte tone count, 7 more bytes, then 14-byte tones
    (+2 / +3 the key range, +8 volume, +9 pan, u16 +12 its Smpl);
  - Smpl: 12-byte records (+5 the root key, +6 volume, +7 pan, u16 +10 its Vagi);
  - Vagi: 16-byte records (u32 offset into the samples, size, flags, sample rate).
The system bank, type 0 (menus, orbs and the like [inferred from the ids its callers pass]), is GDATA 170 (dbsT),
169 (Head) and 171 (samples). Dante's own, type 1, is a PAC (its entries 0, 1, 2): the file FUN_001aff20 names from the
table at 0x58e840 by character, GDATA 173 for him.

Dante's motion definitions (GDATA 0, PAC entry 5; tools/motion_scripts.py) carry timed sound events
(FUN_00255d30): 0x30 {op, layer, type, id, u16 frame} and 0x34 (the same on a bone). Those of types 0 and 1 are
exported by the motion they go with (its layer-0 motion, named as the runtime names them).

    python tools/gen_sfx.py <iso> --output build/testroom --header build/testroom/generated/sfx_runtime.h

writes SFX_SYS.BIN and SFX_DANTE.BIN, each: "SFX1", u32 id count, u32 sample count, u32 samples' offset; per id {u16 sample (0xffff none),
u8 volume, u8 pan, f32 its playback step at 44.1 kHz, i16 next id, 2 pad}; per sample {u32 offset, u32 size}; then the
samples as they are (sfx.c decodes them as it plays). Everything it reads and writes stays local.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_skinned_model import pac_list
import motion_scripts

OUT_RATE = 44100
# name: (dbsT, Head, samples) GDATA entries, or a PAC's entry (its 0, 1, 2). Their order is sfx.c's bank numbers.
BANKS = (("SYS", (170, 169, 171)), ("DANTE", 173))
EVENT_TYPES = {0: 0, 1: 1}   # the game's sound type -> the port's bank


def runtime_name(f, i):
    """As tools/gen_combat_data.py names Dante's motions."""
    if f <= 2:
        return f"{f + 2}-{i:02d}"
    if f == 3:
        return f"rb-{i:02d}"
    return f"s{20 + f}-{i:02d}"


def motion_events(script):
    """{(motion name, frame, bank, id)} from every motion definition's 0x30 / 0x34 events of the exported types."""
    off_m = motion_scripts.u16(script, 2)
    mbase, groups = motion_scripts.table(script, off_m)
    out = set()
    for grp in range(groups):
        t = mbase + motion_scripts.u16(script, mbase + grp * 2)
        for sub in range(motion_scripts.u16(script, t) // 2):
            try:
                md = motion_scripts.motion_def(script, mbase, grp, sub)
            except Exception:
                continue
            lay = [l for l in md["layers"] if l["layer"] == 0]
            if not lay:
                continue
            name = runtime_name(lay[0]["file"], lay[0]["index"])
            for ev in md["events"]:
                raw = bytes.fromhex(ev["raw"])
                if ev["op"] in (0x30, 0x34) and raw[2] in EVENT_TYPES:
                    out.add((name, ev["frame"], EVENT_TYPES[raw[2]], raw[3]))
    return sorted(out)


def parse_head(head):
    prog_at, smpl_at, vagi_at = struct.unpack_from("<3I", head, 0x14)
    n = struct.unpack_from("<I", head, vagi_at + 8)[0] + 1
    vagi = [struct.unpack_from("<4I", head, vagi_at + 16 + 16 * k) for k in range(n)]
    n = struct.unpack_from("<I", head, smpl_at + 8)[0] + 1
    smpl = [head[smpl_at + 16 + 12 * k: smpl_at + 28 + 12 * k] for k in range(n)]
    n = struct.unpack_from("<I", head, prog_at + 8)[0] + 1
    offsets = struct.unpack_from("<%dI" % n, head, prog_at + 16)
    programs = []
    for off in offsets:
        at = prog_at + off
        tones = [head[at + 8 + 14 * t: at + 22 + 14 * t] for t in range(head[at])]
        programs.append(tones)
    return programs, smpl, vagi


def resolve(table, programs, smpl, vagi):
    """Per id: (vagi index or None, volume, pan, step, next)."""
    count = struct.unpack_from("<I", table, 12)[0]
    out = []
    for k in range(count):
        r = table[16 + 32 * k: 48 + 32 * k]
        nxt = struct.unpack_from("<h", r, 22)[0]
        entry = (None, 0, 0x40, 0.0, nxt)
        if r[0] == 1 and r[6] < len(programs):
            note, cents = r[7], struct.unpack_from("<h", r, 14)[0]
            for tone in programs[r[6]]:
                if tone[2] <= note <= tone[3]:
                    s = struct.unpack_from("<H", tone, 12)[0]
                    if s >= len(smpl):
                        break
                    root, v = smpl[s][5], struct.unpack_from("<H", smpl[s], 10)[0]
                    if v >= len(vagi):
                        break
                    rate = vagi[v][3] or 22050
                    step = rate * 2.0 ** ((note - root) / 12.0 + cents / 1200.0) / OUT_RATE
                    volume = r[12] * tone[8] * smpl[s][6] // (127 * 127)
                    entry = (v, max(0, min(127, volume)), r[13], step, nxt)
                    break
        out.append(entry)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--header", type=Path, required=True)
    args = ap.parse_args()
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        read = lambda k: iso.read_file("GDATA.AFS", afs.entries[k]["offset"], afs.entries[k]["size"])
        events = motion_events(pac_list(read(0))[5])
        for name, source in BANKS:
            if isinstance(source, tuple):
                table, head, body = (read(k) for k in source)
            else:
                table, head, body = pac_list(read(source))[:3]
            programs, smpl, vagi = parse_head(head)
            ids = resolve(table, programs, smpl, vagi)
            header = struct.pack("<4s3I", b"SFX1", len(ids), len(vagi), 0)
            id_rows = b"".join(struct.pack("<HBBfh2x", 0xFFFF if v is None else v, vol, pan, step, nxt)
                               for v, vol, pan, step, nxt in ids)
            sample_rows = b"".join(struct.pack("<2I", off, size) for off, size, _, _ in vagi)
            at = len(header) + len(id_rows) + len(sample_rows)
            header = struct.pack("<4s3I", b"SFX1", len(ids), len(vagi), at)
            path = args.output / f"SFX_{name}.BIN"
            path.write_bytes(header + id_rows + sample_rows + body)
            used = sum(1 for v, *_ in ids if v is not None)
            print(f"{path.name}: {len(ids)} ids ({used} playable), {len(vagi)} samples, {len(body)} bytes")
    lines = ["/* Generated by tools/gen_sfx.py: Dante's motion sound events (FUN_00255d30 ops 0x30 / 0x34). */",
             "#pragma once",
             "typedef struct { const char *motion; unsigned short frame; unsigned char bank, id; } SfxEvent;",
             f"#define SFX_EVENT_COUNT {len(events)}",
             "static const SfxEvent sfx_events[SFX_EVENT_COUNT] = {"]
    lines += [f'    {{"{m}", {f}, {b}, {i}}},' for m, f, b, i in events]
    lines.append("};")
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"{args.header}: {len(events)} events")


if __name__ == "__main__":
    main()
