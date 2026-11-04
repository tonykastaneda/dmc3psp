#!/usr/bin/env python3
"""Rooms for the missions, from the player's disc: ROOM_xxx.BIN stages and rooms_runtime.h.

A stage's data slot (the one after it) holds:
  a text chunk   doors: BoxIn (centre, size), NextRoom, NextPosId, Type, and Mission conditions
  POS            player starts, one for each NextPosId
  EST            the encounter scripts, as bytecode. Sets of ops, each headed by 12[set]:
                   2[class, ?] then 3[x, y, z]   spawn
                   9[1, frames]                  wait
                   9[0, n, frames]               wait until n or fewer are alive, then wait
                   0[]                           end of a thread
                 Others (5 facing, 7, 8, 13, 20, 32) aren't used yet.

Which set each room uses in a mission isn't read from the game yet: MISSIONS below says, chosen by
matching what the HD Collection spawned. See docs/GAME_FLOW.md.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_stage import pac_entries

SLOTS = {"000": 272, "001": 276, "002": 280, "003": 284, "004": 288, "005": 292, "006": 296, "009": 308,
         "100": 324, "110": 364, "111": 368, "112": 372}
# mission: first room, the door out of which ends it (None: clearing the script ends it), {room: script set}
MISSIONS = {
    1: ("000", None, {"000": 1}),
    2: ("001", None, {"001": 1}),
    3: ("002", "100", {"002": 3, "009": 2}),
    # The route is what the HD Collection's doors allowed. It saw no enemies in these rooms in the few seconds
    # spent in each, so these sets are stand-ins. The door from 112 to 113 is on a ledge a walking check
    # couldn't reach, so for now it's a temporary end: the real one isn't known. See docs/GAME_FLOW.md.
    4: ("100", "113", {"110": 2, "111": 1}),
}
OP_SPAWN, OP_WAIT, OP_WAIT_ALIVE = 0, 1, 2


def parse_doors(text):
    doors = []
    for block in re.split(r"\n#\s*DOOR|^#\s*DOOR", text)[1:]:
        box = re.search(r"^BoxIn\s+\S+\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)", block, re.M)
        room = re.search(r"^NextRoom\s+(\w+)", block, re.M)
        start = re.search(r"^NextPosId\s+(\d+)", block, re.M)
        kind = re.search(r"^Type\s+(\w+)", block, re.M)
        low, high = 0, 99
        for condition in re.findall(r"^Mission\s+(<=|>=|==)\s*(\d+)", block, re.M):
            value = int(condition[1])
            if condition[0] in ("<=", "=="):
                high = min(high, value)
            if condition[0] in (">=", "=="):
                low = max(low, value)
        if box and room:
            doors.append(dict(box=[float(x) for x in box.groups()], next=room[1], start=int(start[1]) if start else 0,
                              press=bool(kind and kind[1] == "BtnOn"), low=low, high=high))
    return doors


def parse_starts(data):
    chunk = next(c for c in data if c and c[:4] == b"POS\0")
    count = struct.unpack_from("<H", chunk, 6)[0]
    return [struct.unpack_from("<4f", chunk, 0x14 + i * 0x30) for i in range(count)]


def parse_sets(data):
    """Every set in the EST chunk, in order: [(set, [(op, args)])]."""
    chunk = next((c for c in data if c and c[:4] == b"EST\0"), None)
    if chunk is None:
        return []
    sets, current, at = [], None, 0x28
    while at + 4 <= len(chunk):
        op, count = chunk[at], chunk[at + 1]
        if count > 8:
            at += 4
            continue
        if at + 4 + 4 * count > len(chunk):
            break
        args = struct.unpack_from(f"<{count}i", chunk, at + 4)
        at += 4 + 4 * count
        if op == 12:
            current = (args[0], [])
            sets.append(current)
        elif current is not None and op != 0:
            current[1].append((op, args))
    return sets


def compile_script(sets, wanted):
    """The set's first thread spawns who's there at the start, and its next the timed waves. Later copies
    of a set (other difficulties, it seems) aren't used."""
    matching = [ops for number, ops in sets if number == wanted]
    if not matching:
        return []
    chosen = matching[:1]
    if not any(op == 9 for op, _ in matching[0]):
        chosen += [ops for ops in matching[1:] if any(op == 9 for op, _ in ops)][:1]
    out, pending = [], None
    for ops in chosen:
        for op, args in ops:
            if op == 2:
                pending = args[0]
            elif op == 3 and pending is not None:
                out.append((OP_SPAWN, pending, args[0], args[1], args[2]))
                pending = None
            elif op == 9 and args and args[0] == 1 and len(args) >= 2:
                out.append((OP_WAIT, args[1], 0, 0, 0))
            elif op == 9 and args and args[0] == 0 and len(args) >= 3:
                out.append((OP_WAIT_ALIVE, args[1], args[2], 0, 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True, help="where ROOM_xxx.BIN go")
    ap.add_argument("--header", type=Path, required=True)
    args = ap.parse_args()
    rooms = {}
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        for name, slot in SLOTS.items():
            entry = afs.entries[slot + 1]
            data = pac_entries(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
            rooms[name] = dict(doors=parse_doors(data[0].decode("latin1").replace("\r", "")), starts=parse_starts(data), sets=parse_sets(data))
    for name, slot in SLOTS.items():
        out = args.output / f"ROOM_{name}.BIN"
        if not out.exists():
            subprocess.run([sys.executable, str(Path(__file__).with_name("prepare_stage.py")), str(args.iso), "--output", str(out), "--slot", str(slot)], check=True)

    names = list(SLOTS)
    index = {n: i for i, n in enumerate(names)}
    lines = ["/* Generated from the disc by tools/gen_rooms.py */", "#pragma once",
             "typedef struct { float box[6]; short next, number, start; unsigned char press, low, high; } RoomDoor;",
             "typedef struct { short op, a, b, c, d; } RoomOp;",
             "typedef struct { const char *file; const float (*starts)[4]; const RoomDoor *doors; short door_count; } Room;",
             "typedef struct { const RoomOp *ops; short count; } RoomScript;",
             "typedef struct { short first_room, end_room; RoomScript scripts[%d]; } MissionInfo;" % len(names),
             "#define ROOM_NONE (-1)",
             "enum { ROOM_OP_SPAWN, ROOM_OP_WAIT, ROOM_OP_WAIT_ALIVE };",
             f"#define MISSION_COUNT {len(MISSIONS)}"]
    for name in names:
        r = rooms[name]
        lines.append(f"static const float room_{name}_starts[][4] = {{" + ",".join("{%.1ff,%.1ff,%.1ff,%.1ff}" % tuple(s) for s in r["starts"]) + "};")
        cells = []
        for d in r["doors"]:
            target = index.get(d["next"], "ROOM_NONE")
            box = ",".join(f"{v:.1f}f" for v in d["box"])
            cells.append(f"{{{{{box}}},{target},{int(d['next'])},{d['start']},{int(d['press'])},{d['low']},{d['high']}}}")
        lines.append(f"static const RoomDoor room_{name}_doors[] = {{" + ",".join(cells or ["{{0},ROOM_NONE,0,0,0,0,0}"]) + "};")
    lines.append("static const Room rooms[] = {")
    for name in names:
        lines.append(f'    {{"ROOM_{name}.BIN", room_{name}_starts, room_{name}_doors, {len(rooms[name]["doors"])}}},')
    lines.append("};")
    for mission, (first, end, sets) in MISSIONS.items():
        for room, number in sets.items():
            ops = compile_script(rooms[room]["sets"], number)
            print(f"mission {mission} room {room} set {number}: {len(ops)} ops, {sum(1 for o in ops if o[0] == OP_SPAWN)} spawns")
            lines.append(f"static const RoomOp m{mission}_{room}_ops[] = {{" + ",".join("{%d,%d,%d,%d,%d}" % o for o in ops) + "};")
    lines.append("static const MissionInfo missions[MISSION_COUNT] = {")
    for mission, (first, end, sets) in MISSIONS.items():
        cells = []
        for name in names:
            if name in sets:
                cells.append(f"{{m{mission}_{name}_ops,(short)(sizeof(m{mission}_{name}_ops)/sizeof(RoomOp))}}")
            else:
                cells.append("{0,0}")
        # The end is the number of the room whose door leaves the mission.
        lines.append(f'    {{{index[first]},{int(end) if end else "ROOM_NONE"},{{' + ",".join(cells) + "}},")
    lines.append("};")
    args.header.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
