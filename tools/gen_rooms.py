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
import math
import re
import struct
import subprocess
import sys
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from prepare_stage import pac_entries
import gimmicks

# How many light hits a breakable takes: its vital over about what one light hit does (a Pride's 250 is 6 hits,
# an estimate already). Red orb crystals give some orbs a hit, then shatter: not read from the game yet.
VITAL_PER_HIT = 42.0
CRYSTAL_HITS, CRYSTAL_ORBS = 10, 10

# Stage slots (stNNN.pac); the room's data is the next slot.
# Which of a room's story pieces (gimmicks.story_tag) show, by mission: {room: [(first mission, last mission,
# sets shown)]}; the room's other tagged sets are hidden then. From the event scripts' on-enter switches
# (EventTbl03 room 6: all off, then on by its counter). 006 before Cerberus wakes: his ice (102), the upper fence
# (104) and the ice (109). After he's beaten (counter 2): the entrance ice (100), his broken ice (103), the fence.
# Mission 4's script has no room 6, so the room stays as Mission 3 left it.
STORY = {"006": [(1, 3, (102, 104, 109)), (4, 20, (100, 103, 104))]}
# COLLISION sets that belong to a story piece of the same number: 122's rubble (STAY 100) and its three walls
# in front of the door to 127, which the Mute Goddess blasts away in Mission 6 (psp/testroom/mission6.c).
STORY_WALLS = {"122": (100,)}
SLOTS = {"000": 272, "001": 276, "002": 280, "003": 284, "004": 288, "005": 292, "006": 296, "009": 308,
         "100": 324, "101": 328, "102": 332, "104": 340, "105": 344, "110": 364, "111": 368, "112": 372,
         "103": 336, "113": 376, "120": 404, "121": 408, "122": 412, "123": 416, "124": 420, "125": 424, "126": 428, "127": 432,
         # SE: the Jester's arenas follow st400 at 736, four slots a room. 422 is Mission 5's.
         "422": 824}
# mission: first room, the door out of which ends it (None: clearing the script ends it), {room: groups}
MISSIONS = {
    1: ("000", None, {"000": [0, 1]}),
    # EventTbl02: clear group 0, then group 1, then the ground-level Vanguard (group 3).
    # Group 2 is the rooftop cinematic actor; group 4 is the cutscene-skip alternative to group 3.
    2: ("001", None, {"001": [0, 1, 3]}),
    # 002 66 Slum Avenue: the Enigmas, then the Seven Hells; 009 Bullseye; 004 Love Planet, the Hell Sloths.
    # 006's only group is one class-43 enemy (Cerberus).
    # From its event script (EventTbl03): 002 runs groups 2, 1, 0 as Dante moves along it, then 3 (the main
    # wave); 009 runs 2 on entry; 004 seals its doors and runs 1, then 0; 006 starts group 0 after demo 6.
    3: ("002", "100", {"002": [2, 1, 0, 3], "009": [2], "004": [1, 0], "006": [0]}),
    # Mission 4, from its event script (GDATA EventTbl04): 100 Chamber of Echoes -> 101 Entranceway -> 102 Living
    # Statue Room -> 103 Silence Statuary (its lift) -> 110 Incandescent Space -> 100 2nd floor -> 112 Endless
    # Infernum, falling into 104 Chamber of Sins -> back to 112 -> 111 Giantwalker Chamber (the Gigapede, class 41)
    # -> 110, where the Astronomical Board (item 36) ends it. 101's group isn't in the script: 0 is a guess.
    4: ("100", None, {"101": [0], "102": [1], "110": [0], "104": [0], "111": [0]}),
    # Mission 5, from EventTbl05 (GDATA 243): after demo 11 it starts in 111 at its start 2, where 7 Blood-goyles
    # (group 1, EST 32 = CEm014) lock the doors. Cleared, a door to 422 opens: the Jester (group 0, EST 62 =
    # CEm044). Beating him unlocks 111. Then 112 -> 100 -> 101 -> 102 (group 3: Enigmas and Gluttonies; the Soul
    # of Steel) -> back to 112, whose door to 113 the Soul of Steel opens -> 113 Surge of Fortunas, its lift
    # (groups 0-3, waves as it climbs) -> 120 Azure Garden (the Divinity Statue) -> 121 Firestorm Chamber: Agni
    # & Rudra (group 0, EST 45 = CEm027). Their fall clears the mission. 110 (groups 1, 0) is off the route.
    5: ("111", None, {"111": [1], "422": [0], "102": [3], "113": [0, 1, 2, 3], "110": [1], "121": [0]}),
    # Mission 6, from EventTbl06 (GDATA 244): 122 Mute Goddess' Chamber -> 123 Chamber of 3 Trials -> the trials 126
    # (Wisdom: group 0 only after a wrong doorway), 125 (Skill: group 0 once its Essence is taken) and 124 (Warrior:
    # group 0 under a barrier the two crest switches lift) -> 122, the Essences into the statue -> 127 The Dark
    # Corridor, whose area 0 (demo 15) clears it. When 126's and 125's groups run is psp/testroom/mission6.c's.
    6: ("122", None, {"124": [0], "125": [0], "126": [0]}),
}
# Where a mission starts in its first room, when not at start 0.
FIRST_START = {5: 2}
# A mission cleared by a room's fight being over (its boss), not a door or an item.
CLEAR_ROOMS = {5: "121"}
OP_SPAWN, OP_WAIT, OP_WAIT_ALIVE = 0, 1, 2
# Divinity Statues: where the event scripts open the shop (op 7a after examining in an EVE area). Mission 3's
# is in 13th Avenue, area 0 of room 005's EVE chunk (its four corners average to this).
SHOPS = {"005": (5211.4, 780.7, 1539.8)}
# Lifts that work: 103's carries Dante up to the door into 110 in Mission 4. Others aren't on a route yet.
LIFT_ROOMS = {"103", "113"}
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
    (4, "111", "422", "999"),   # the Jester's door is Mission 5's
    (5, "111", "112", "422"),   # 111 unlocks when the Jester's beaten (flag 2)
    (5, "111", "110", "422"),
    (5, "112", "113", "102"),   # the Soul of Steel, from 102, opens 112's way to 113
]
# Seals a room's crest switch takes down (CStageSetSwitch / CStageSetSeal, docs/BAR_CRANK_SPEC.md): EventTbl03
# room 3 binds switch 100 to flag 32 and puts seal 200, blue, on door 1 (to 004). (mission, room, switch set,
# seal set, the door's next room, colour 0 red / 1 blue / 2 purple.)
SWITCH_SEALS = [(3, "003", 100, 200, "004", 1)]
SWITCH_HIT = ((0.0, 0.0, -20.0), 60.0)                  # sphere: local centre, radius (CStageSetSwitch +0x7c0)
SEAL_BOX = ((0.0, 0.0, -50.0), (200.0, 180.0, 50.0))   # local centre, half sizes (CStageSetSeal +0xfc0, layout inferred)
SEAL_BOARD = (154.8, -218.1, 203.7)                     # model 17: half width, bottom, top


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


def compile_script(groups, wanted, difficulty, group_delays=None):
    """The groups in order, threads one after another: the first thread of a group spawns who's there at
    the start, its next the timed waves."""
    out, pending = [], None
    for g in wanted:
        if g >= len(groups):
            continue
        if group_delays and g in group_delays:
            out.append((OP_WAIT_ALIVE, 0, group_delays[g], 0, 0))
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
                               groups=parse_groups(data), lifts=parse_lifts(game),
                               breakables=gimmicks.breakables(game, stage[5]) if len(stage) > 5 else [],
                               story=gimmicks.story_tags(game), sets={g["number"]: g for g in gimmicks.parse_game(game)})
            tags = {s["index"]: n for n, s, *_ in rooms[name]["breakables"]}
            # Walls that go with a story piece: they share its tag, so hiding the piece takes them away too.
            for s in gimmicks.parse_game(game):
                if s["kind"] == "COLLISION" and s["number"] in STORY_WALLS.get(name, ()):
                    tags[s["index"]] = gimmicks.STORY_TAG + s["number"] - 100
            rooms[name]["boxes"] = gimmicks.collision_boxes(game, tags)
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
             "short goal_room, goal_after; float goal[3]; short first_start, clear_room; } MissionInfo;  /* scripts[Easy, Normal][room] */" % len(names),
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
                ops = compile_script(rooms[room]["groups"], wanted, difficulty, {1: 40, 3: 30} if mission == 2 else None)
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
    lines.append("/* A room's breakable prop: its number (the stage batches' tag), where, how big, hits it takes, and up to")
    lines.append(" * two red orb rolls (amount, % chance), or a red orb crystal paying orbs each hit. tools/gimmicks.py. */")
    lines.append("typedef struct { short room, number; float x, y, z, radius, height; short hits, crystal, orbs[2], chance[2]; } RoomBreakable;")
    cells = []
    for name in names:
        for number, s, (x, y, z), radius, height in rooms[name]["breakables"]:
            crystal = s["kind"] == "ORBREAK"
            hits = CRYSTAL_HITS if crystal else max(1, round((s["vital"] or 1) / VITAL_PER_HIT))
            rolls = [(a, ch) for item, a, ch in s["items"] if item == "RED_ORB"][:2]
            if crystal:
                rolls = [(CRYSTAL_ORBS, 100)]
            rolls += [(0, 0)] * (2 - len(rolls))
            cells.append(f"{{{index[name]},{number},{x:.1f}f,{y:.1f}f,{z:.1f}f,{radius:.1f}f,{height:.1f}f,{hits},{int(crystal)},"
                         f"{{{rolls[0][0]},{rolls[1][0]}}},{{{rolls[0][1]},{rolls[1][1]}}}}}")
    lines.append(f"#define ROOM_BREAKABLE_COUNT {len(cells)}")
    lines.append("static const RoomBreakable room_breakables[] = {" + (",".join(cells) or "{0}") + "};")
    print(f"{len(cells)} breakables")
    lines.append("/* A room's solid boxes (tools/gimmicks.py collision_boxes): its COLLISION walls and breakable props, centre,")
    lines.append(" * half sizes and yaw; tag = a breakable's number (gone once broken), 0 = always. */")
    lines.append("typedef struct { short room, tag; float x, y, z, hx, hy, hz, yaw; } RoomBox;")
    cells = []
    for name in names:
        for tag, (x, y, z), (hx, hy, hz), yaw in rooms[name]["boxes"]:
            cells.append(f"{{{index[name]},{tag},{x:.1f}f,{y:.1f}f,{z:.1f}f,{hx:.1f}f,{hy:.1f}f,{hz:.1f}f,{yaw:.4f}f}}")
    lines.append(f"#define ROOM_BOX_COUNT {len(cells)}")
    lines.append("static const RoomBox room_boxes[] = {" + (",".join(cells) or "{0}") + "};")
    print(f"{len(cells)} solid boxes")
    lines.append("/* A room's story pieces hidden in a span of missions (tag = the stage batches', from tools/gimmicks.py). */")
    lines.append("typedef struct { short room, tag, first, last; } RoomStory;")
    cells = []
    for name in names:
        tags = rooms[name]["story"]
        for first, last, shown in STORY.get(name, []):
            for tag in sorted(set(tags.values())):
                if tag - gimmicks.STORY_TAG + 100 not in shown:
                    cells.append(f"{{{index[name]},{tag},{first},{last}}}")
    lines.append(f"#define ROOM_STORY_COUNT {len(cells)}")
    lines.append("static const RoomStory room_story[] = {" + (",".join(cells) or "{0}") + "};")
    lines.append("/* A crest switch and the seal it takes down: the switch's hit sphere (x y z r) and place (x y z yaw), the")
    lines.append(" * seal's wall (centre, half sizes, yaw radians) and board (centre x y z, half width, bottom, top, yaw). */")
    lines.append("typedef struct { short mission, room, next_number, colour; float hit[4], disc[4], box[7], board[7]; } RoomSwitchSeal;")
    cells = []
    for mission, name, sw_n, seal_n, door, colour in SWITCH_SEALS:
        if name not in index:
            continue
        sw, seal = rooms[name]["sets"][sw_n], rooms[name]["sets"][seal_n]
        def world(g, local):
            r, p = gimmicks._rotation(g["rot"]), g["pos"]
            return tuple(p[i] + sum(r[i][k] * local[k] for k in range(3)) for i in range(3))
        hit = world(sw, SWITCH_HIT[0])
        box = world(seal, SEAL_BOX[0])
        yaw = math.radians(seal["rot"][1])
        board = tuple(seal["pos"]) + (SEAL_BOARD[0], seal["pos"][1] + SEAL_BOARD[1], seal["pos"][1] + SEAL_BOARD[2], yaw)
        f = lambda v: ",".join(f"{float(x):.1f}f" for x in v)
        cells.append(f"{{{mission},{index[name]},{int(door)},{colour},{{{f(hit + (SWITCH_HIT[1],))}}},"
                     f"{{{f(tuple(sw['pos']) + (sw['rot'][1],))}}},{{{f(box + SEAL_BOX[1])},{yaw:.4f}f}},"
                     f"{{{f(board[:6])},{yaw:.4f}f}}}}")
        print(f"room {name}: switch {sw_n} hit {hit}, seal {seal_n} wall {box} +- {SEAL_BOX[1]}, door to {door}")
    lines.append(f"#define ROOM_SWITCH_SEAL_COUNT {len(cells)}")
    lines.append("static const RoomSwitchSeal room_switch_seals[] = {" + (",".join(cells) or "{0}") + "};")
    lines.append("#define SWITCH_CHARGE 300.0f      /* CStageSetSwitch: damage to spin it up, */")
    lines.append("#define SWITCH_FLAMES 560.0f      /* then to light all 8 flames, */")
    lines.append("#define SWITCH_PER_FLAME 70.0f")
    lines.append("#define SWITCH_WINDOW 120.0f      /* frames after a hit before it runs down, */")
    lines.append("#define SWITCH_DRAIN 2.0f         /* by this a frame */")
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
        tail += f',{FIRST_START.get(mission, 0)},{index[CLEAR_ROOMS[mission]] if mission in CLEAR_ROOMS else "ROOM_NONE"}'
        lines.append(f'    {{{index[first]},{int(end) if end else "ROOM_NONE"},{{' + ",".join(per) + "}" + tail + "},")
    lines.append("};")
    args.header.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
