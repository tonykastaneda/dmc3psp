#!/usr/bin/env python3
"""Rooms for the missions, from the player's disc: ROOM_xxx.BIN stages and rooms_runtime.h.

A stage's data slot (the one after it) holds:
  a text chunk   doors: BoxIn (centre, size), NextRoom, NextPosId, Type, and Mission conditions
  POS            player starts, one for each NextPosId
  EST            the encounter scripts, as bytecode. After a 0x20 header, a table at the end holds
                 n groups x 5 difficulties (Easy, Normal, Hard, Very Hard, Dante Must Die) of offsets
                 into the ops; each runs up to the next offset in the table. Ops:
                   12[rank]                      heads a set (3, 5, 10: seems to be the enemies' rank)
                   2[class, ?] then 3[x, y, z]   spawn
                   9[1, frames]                  wait
                   9[0, n, frames]               wait until n or fewer are alive, then wait
                   0[]                           end of a thread
                 Others (5 facing, 7, 8, 13, 20, 32) aren't used yet.

Which groups a room runs in a mission isn't read from the game yet: MISSIONS below says, chosen by
matching what the HD Collection spawned and the walkthrough. See docs/GAME_FLOW.md.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_stage import pac_entries

# Stage slots (stNNN.pac); the room's data is the next slot.
SLOTS = {"000": 272, "001": 276, "002": 280, "003": 284, "004": 288, "005": 292, "006": 296, "009": 308,
         "100": 324, "101": 328, "102": 332, "104": 340, "105": 344, "110": 364, "111": 368, "112": 372,
         "103": 336, "113": 376, "120": 404, "121": 408, "122": 412, "123": 416, "124": 420, "125": 424, "126": 428}
# mission: first room, the door out of which ends it (None: clearing the script ends it), {room: groups}
MISSIONS = {
    1: ("000", None, {"000": [0, 1]}),
    2: ("001", None, {"001": [0]}),
    # 002 66 Slum Avenue: the Enigmas, then the Seven Hells; 009 Bullseye; 004 Love Planet, the Hell Sloths.
    # 006's only group is one class-43 enemy (Cerberus, not ported), so it has no fight yet.
    # From its event script (EventTbl03): 002 runs groups 2, 1, 0 as Dante moves along it, then 3 (the main
    # wave); 009 runs 2 on entry; 004 seals its doors and runs 1, then 0.
    3: ("002", "100", {"002": [2, 1, 0, 3], "009": [2], "004": [1, 0]}),
    # Mission 4, from its event script (GDATA EventTbl04): 100 Chamber of Echoes -> 101 Entranceway -> 102 Living
    # Statue Room -> 103 Silence Statuary (its lift) -> 110 Incandescent Space -> 100 2nd floor -> 112 Endless
    # Infernum, falling into 104 Chamber of Sins -> back to 112 -> 111 Giantwalker Chamber (the Gigapede, class 41)
    # -> 110, where the Astronomical Board (item 36) ends it. 101's group isn't in the script: 0 is a guess.
    4: ("100", None, {"101": [0], "102": [1], "110": [0], "104": [0], "111": [0]}),
}
OP_SPAWN, OP_WAIT, OP_WAIT_ALIVE = 0, 1, 2
# Divinity Statues: where the event scripts open the shop (op 7a after examining in an EVE area). Mission 3's
# is in 13th Avenue, area 0 of room 005's EVE chunk (its four corners average to this).
SHOPS = {"005": (5211.4, 780.7, 1539.8)}
# Lifts that work: 103's carries Dante up to the door into 110 in Mission 4. Others aren't on a route yet.
LIFT_ROOMS = {"103"}
# A mission that ends on an item instead of a door: room, the item's place (the room's ITM chunk), and the
# room whose fight must be over first.
GOALS = {4: ("110", (1517.4, 838.0, 417.3), "111")}
# Doors that stay shut until another room's fight is over, as the event script's flags do:
# (mission, room, the door's next room, the room that must be cleared).
GATES = [
    (4, "100", "112", "110"),   # the 2nd floor's way on opens after Incandescent Space (flag 0)
    (4, "100", "110", "111"),   # Incandescent Space is reached by 103's lift first
    (4, "112", "111", "104"),   # Endless Infernum's seal holds until the Chamber of Sins (flag 6)
    (4, "110", "111", "111"),   # the Giantwalker Chamber is entered from 112's side
    (4, "112", "113", "999"),   # not part of Mission 4
]


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


def parse_lifts(text):
    """A stage's LIFT gimmicks (its "# GAME" text): place, turn, hit box (offset, half sizes), and the
    SWITCH that sets it going."""
    lifts, switch = [], None
    for block in re.split(r"\n(?=# SET)", text):
        head = block.split("\n", 1)[0]
        values = {}
        for line in block.split("\n")[1:]:
            line = line.split(";")[0].strip()
            if line:
                m = re.match(r"(\w+)\s*(.*)", line)
                if m:
                    values[m[1]] = [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", m[2])]
        if " SWITCH" in head and "pos" in values:
            switch = values["pos"][:3]
        if " LIFT" in head and "pos" in values and "hit" in values:
            lifts.append(dict(pos=values["pos"][:3], yaw=(values.get("rot") or [0, 0, 0])[1], box=values["hit"][:6]))
    for lift in lifts:
        lift["switch"] = switch
    return lifts


def ops_between(chunk, start, end):
    out, at = [], start
    while at + 4 <= end:
        op, count = chunk[at], chunk[at + 1]
        if count > 8:
            at += 4
            continue
        if at + 4 + 4 * count > len(chunk):
            break
        args = struct.unpack_from(f"<{count}i", chunk, at + 4)
        at += 4 + 4 * count
        if op != 12:
            out.append((op, args))
    return out


def parse_groups(data):
    """The EST chunk's groups: [group][difficulty] = [(op, args)]."""
    chunk = next((c for c in data if c and c[:4] == b"EST\0"), None)
    if chunk is None:
        return []
    _, count, table_at = struct.unpack_from("<HHI", chunk, 4)
    table = struct.unpack_from(f"<{count * 5}I", chunk, table_at)
    bounds = sorted(set(x for x in table if x) | {table_at})
    return [[ops_between(chunk, table[g * 5 + d], min(b for b in bounds if b > table[g * 5 + d])) if table[g * 5 + d] else []
             for d in range(5)] for g in range(count)]


def compile_script(groups, wanted, difficulty):
    """The groups in order, threads one after another: the first thread of a group spawns who's there at
    the start, its next the timed waves."""
    out, pending = [], None
    for g in wanted:
        if g >= len(groups):
            continue
        for op, args in groups[g][difficulty]:
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
            stage = pac_entries(iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"]))
            game = next((c for c in stage if c[:6] == b"# GAME"), b"").decode("latin1").replace("\r", "")
            rooms[name] = dict(doors=parse_doors(data[0].decode("latin1").replace("\r", "")), starts=parse_starts(data),
                               groups=parse_groups(data), lifts=parse_lifts(game))
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
             "typedef struct { short first_room, end_room; RoomScript scripts[2][%d]; "
             "short goal_room, goal_after; float goal[3]; } MissionInfo;  /* scripts[Easy, Normal][room] */" % len(names),
             "typedef struct { short mission, room, next_number, needs_room; } DoorGate;",
             "/* A lift: centre, turn (degrees), half sizes, its top's low and high heights, and the switch. */",
             "typedef struct { short room; float x, z, yaw, half_x, half_z, low, high, switch_pos[3]; } RoomLift;",
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
        for room, wanted in sets.items():
            for difficulty in (0, 1):
                ops = compile_script(rooms[room]["groups"], wanted, difficulty)
                print(f"mission {mission} {'EN'[difficulty]} room {room} groups {wanted}: {len(ops)} ops, "
                      f"{sum(1 for o in ops if o[0] == OP_SPAWN)} spawns")
                lines.append(f"static const RoomOp m{mission}_{difficulty}_{room}_ops[] = {{" +
                             ",".join("{%d,%d,%d,%d,%d}" % o for o in ops or [(0, 0, 0, 0, 0)]) + "};")
                if not ops:
                    lines[-1] = lines[-1].replace("{{0,0,0,0,0}}", "{{0,0,0,0,0}}")
    gates = [(m, index[r], int(n), index.get(need, 99)) for m, r, n, need in GATES if r in index]
    lines.append(f"#define DOOR_GATE_COUNT {len(gates)}")
    lines.append("static const DoorGate door_gates[] = {" + ",".join("{%d,%d,%d,%d}" % g for g in gates) + "};")
    lifts = []
    for name in names:
        if name not in LIFT_ROOMS:
            continue
        for lift in rooms[name]["lifts"]:
            (x, y, z), box = lift["pos"], lift["box"]
            # The hit box hangs from its place: (0, -3000, 0) with half height 3000 is a column whose top is
            # the lift's floor. Where it rises to: the highest door in the room above its floor.
            top = max([d["box"][1] for d in rooms[name]["doors"] if abs(d["box"][0] - x) < box[3] + 50
                       and abs(d["box"][2] - z) < box[5] + 50] + [y])
            sw = lift["switch"] or [x, y, z]
            lifts.append(f"{{{index[name]},{x:.1f}f,{z:.1f}f,{lift['yaw']:.1f}f,{box[3]:.1f}f,{box[5]:.1f}f,{y + box[1] + box[4]:.1f}f,"
                         f"{top:.1f}f,{{{sw[0]:.1f}f,{sw[1]:.1f}f,{sw[2]:.1f}f}}}}")
            print(f"room {name}: lift at {x:.0f},{z:.0f} from {y + box[1] + box[4]:.0f} to {top:.0f}, switch {sw}")
    shops = [f"{{{index[r]},{x:.1f}f,{y:.1f}f,{z:.1f}f}}" for r, (x, y, z) in SHOPS.items() if r in index]
    lines.append("typedef struct { short room; float x, y, z; } RoomShop;")
    lines.append(f"#define ROOM_SHOP_COUNT {len(shops)}")
    lines.append("static const RoomShop room_shops[] = {" + (",".join(shops) or "{0}") + "};")
    lines.append(f"#define ROOM_LIFT_COUNT {len(lifts)}")
    lines.append("static const RoomLift room_lifts[] = {" + (",".join(lifts) or "{0}") + "};")
    lines.append("static const MissionInfo missions[MISSION_COUNT] = {")
    for mission, (first, end, sets) in MISSIONS.items():
        per = []
        for difficulty in (0, 1):
            cells = []
            for name in names:
                if name in sets and compile_script(rooms[name]["groups"], sets[name], difficulty):
                    v = f"m{mission}_{difficulty}_{name}_ops"
                    cells.append(f"{{{v},(short)(sizeof({v})/sizeof(RoomOp))}}")
                else:
                    cells.append("{0,0}")
            per.append("{" + ",".join(cells) + "}")
        # The end is the number of the room whose door leaves the mission.
        goal = GOALS.get(mission)
        tail = (f',{index[goal[0]]},{index[goal[2]]},{{{goal[1][0]:.1f}f,{goal[1][1]:.1f}f,{goal[1][2]:.1f}f}}' if goal
                else ',ROOM_NONE,ROOM_NONE,{0,0,0}')
        lines.append(f'    {{{index[first]},{int(end) if end else "ROOM_NONE"},{{' + ",".join(per) + "}" + tail + "},")
    lines.append("};")
    args.header.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
