#!/usr/bin/env python3
"""The missions' music (docs/MUSIC.md): each room's on-enter BGM setting from its mission's event script, and the tracks.

Event op 0x70 (bgm t0, t1, c0, c1; FUN_0020be20 -> FUN_0032cef0) sets two stream channels, a track and a command each.
A track is an index into the table at 0x5b14b0, whose word is an SDATA.AFS entry (track 1 -> 2, Battle_01.adx; 36 ->
135, Stage_01.adx; 48 -> 33, Kaze_01.adx). The scripts put the room's battle track on channel 0 with command 12 (held
until a fight) and its stage or ambience on channel 1 (15 / 16); boss rooms fade channel 0 (3) and play the boss on 1.

    python tools/gen_music.py <iso> --output build/testroom --header build/testroom/generated/music_runtime.h

writes music_runtime.h (room table: mission, room, SDATA track and command per channel; the first bgm op of the room's
section) and BGM_nnn.BIN for each track used (nnn = its SDATA index): "BGM2", u32 rate, u32 sample pairs, u32 loop
start and end (sample pairs), the IMA coder's state at the loop start (i16 predicted x2, u8 index x2, 2 pad), then the
movies' IMA ADPCM, a byte per stereo pair (tools/prepare_movies.py). psp/testroom/music.c plays to the loop end and
goes back to its start. The loop is the ADX header's (version 4: flag +0x24, start +0x28, end +0x30, in the track's
samples, scaled to 44.1 kHz); a track without one loops whole. Tracks already written are kept. Everything it reads
and writes stays local.
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dmc3_image import AfsArchive, Iso9660  # noqa: E402
import event_dump  # noqa: E402
import gen_enemy_ai  # noqa: E402
import prepare_movies  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TRACKS = 0x5B14B0
RATE = 44100


def room_settings(iso, elf):
    """[(mission, room, sdata0, cmd0, sdata1, cmd1)]: each ROOM section's first bgm op."""
    afs = AfsArchive(iso, "GDATA.AFS")
    rows = []
    for mission in range(1, 22):  # EventTbl21 supplies Bloody Palace's arena and boss music.
        e = afs.entries[238 + mission]
        _, ops, _ = event_dump.disassemble(iso.read_file("GDATA.AFS", e["offset"], e["size"]))
        room, seen = None, set()
        for o in ops:
            if o["name"] == "ROOM":
                room = o["args"][0]
            elif o["name"] == "bgm" and room is not None and room not in seen and len(o["args"]) >= 4:
                seen.add(room)
                t0, t1, c0, c1 = o["args"][:4]
                s0 = elf.word(TRACKS + t0 * 4) if c0 else 0
                s1 = elf.word(TRACKS + t1 * 4) if c1 else 0
                rows.append((mission, room, s0, c0, s1, c1))
    return rows


def write_track(iso, sdata, index, out: Path):
    path = out / f"BGM_{index:03d}.BIN"
    if path.exists() and path.read_bytes()[:4] == b"BGM2":
        return path
    entry = sdata.entries[index]
    adx = iso.read_file("SDATA.AFS", entry["offset"], entry["size"])
    pcm = subprocess.run([prepare_movies.FFMPEG, "-v", "error", "-f", "adx", "-i", "pipe:0", "-f", "s16le", "-ac", "2",
                          "-ar", str(RATE), "pipe:1"], input=adx, capture_output=True, check=True).stdout
    samples = struct.unpack(f"<{len(pcm) // 4 * 2}h", pcm[:len(pcm) // 4 * 4])
    pairs = len(samples) // 2
    start, end = adx_loop(adx)
    if start is None or not 0 <= start < end:
        start, end = 0, pairs
    else:
        start, end = start * RATE // adx_rate(adx), min(pairs, end * RATE // adx_rate(adx))
    snapshot = {"at": start}
    nibbles = prepare_movies.ima_encode(samples, snapshot)[:pairs]
    p, i = snapshot.get("predicted", [0, 0]), snapshot.get("index", [0, 0])
    path.write_bytes(struct.pack("<4s4I2h2B2x", b"BGM2", RATE, pairs, start, end, p[0], p[1], i[0], i[1]) + bytes(nibbles))
    print(f"  {entry['name']}: {pairs / RATE:.1f} s, loop {start / RATE:.1f}-{end / RATE:.1f} s -> {path.name}")
    return path


def adx_rate(adx):
    return struct.unpack_from(">I", adx, 8)[0]


def adx_loop(adx):
    """(start, end) in the track's samples, or (None, None): the version 4 header's loop (flag +0x24, start +0x28, end
    +0x30), when the header reaches it (its data begins at the u16 +2 plus 4)."""
    if adx[0x12] != 4 or struct.unpack_from(">H", adx, 2)[0] + 4 < 0x34:
        return None, None
    flag, start = struct.unpack_from(">2I", adx, 0x24)
    end = struct.unpack_from(">I", adx, 0x30)[0]
    return (start, end) if flag == 1 else (None, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--header", type=Path, required=True)
    args = ap.parse_args()
    elf = gen_enemy_ai.Elf((ROOT / "analysis" / "disc" / "SLUS_213.61").read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)
    with Iso9660(args.iso) as iso:
        rows = room_settings(iso, elf)
        sdata = AfsArchive(iso, "SDATA.AFS")
        # The menus' (the game's menu code loads 0x81, Option.adx; the results screen's 0x82, Result.adx).
        used = sorted({s for r in rows for s in (r[2], r[4]) if s and s < len(sdata.entries)} | {129, 130})
        for index in used:
            write_track(iso, sdata, index, args.output)
    lines = ["/* Generated by tools/gen_music.py: each room's on-enter BGM (event op 0x70), tracks as SDATA indices. */",
             "#pragma once",
             "typedef struct { unsigned char mission; short room; short track[2]; unsigned char command[2]; } MusicRoom;",
             f"#define MUSIC_ROOM_COUNT {len(rows)}",
             "static const MusicRoom music_rooms[MUSIC_ROOM_COUNT] = {"]
    lines += [f"    {{{m}, {r}, {{{s0}, {s1}}}, {{{c0}, {c1}}}}}," for m, r, s0, c0, s1, c1 in rows]
    lines.append("};")
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"{args.header}: {len(rows)} rooms, {len(used)} tracks")


if __name__ == "__main__":
    main()
