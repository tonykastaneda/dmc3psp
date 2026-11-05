"""Generate psp/testroom/combat_data.h from the decoded game data.

Inputs (analysis/moveset, see docs/MOVESET_RE.md):
  motion_scripts.json  script timelines (motion, waits, windows)
  command_table.csv    Dante's command table (0x5a8e90)

Emits, for the scripts the test room plays:
  - the runtime motion name and an event list in motion frames:
      EV_ACTIVE  a=1/0   window 0 byte 1 bit 0x80 (attack active, +0x24ed)
      EV_HIT     a=n     window 0 byte 2 0x2n (FUN_002e21a0: discrete hit,
                         e.g. each stab of Million Stab)
      EV_GATE    a=n, b=state   window 1: 0x3n -> 1 buffer, 0x2n -> 0
                         closed, 0x1n -> 2 open (player+0x24dd[n])
  - the Rebellion / Swordmaster-Rebellion / Trickster commands in table order (the
    order the game walks them in FUN_002458f0).
"""
import argparse
import csv
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from moveset_phases import ACTIONS, WEAPON_NAMES

SCRIPTS = ["3:3", "3:4", "3:5", "3:6", "3:7", "3:8", "3:9", "3:10", "3:11",
           "3:14", "3:15", "3:16", "3:17", "3:19",
           "13:0", "13:19", "13:13", "13:12", "13:17",
           "3:46", "3:47", "3:48", "3:49", "3:50", "3:51", "3:52", "3:53",
           "13:18", "13:20", "13:7", "13:8", "13:9", "13:10",
           # Trickster (GDATA 43): dash fwd/back/left/right, the same from the
           # second chained dash on, fwd after fwd, Sky Star, Air Trick
           # (ground / air), Wall Hike and its flip off the wall.
           "23:0", "23:1", "23:2", "23:3", "23:24", "23:25", "23:26", "23:27", "23:17",
           "23:11", "23:15", "23:16", "23:5", "23:6",
           # Royalguard block states' first scripts (FUN_00224850, FUN_00225a00).
           "24:0", "24:4", "2:0", "25:0", "26:0", "3:13", "3:42", "24:5", "24:30", "24:31"]

INPUTS = {  # command_table.csv input_text -> C enum
    "press TRI": "IN_TRI", "press CIR": "IN_CIR",
    "[R1 held] fwd + press TRI": "IN_LOCK_FWD_TRI", "[R1 held] back + press TRI": "IN_LOCK_BACK_TRI",
    "[R1 held] fwd + press CIR": "IN_LOCK_FWD_CIR", "[R1 held] back + press CIR": "IN_LOCK_BACK_CIR",
    "press TRI + charge TRI 90f release": "IN_CHARGE_TRI",
    "mash TRI >6x (gap<=13f)": "IN_MASH_TRI", "mash CIR >6x (gap<=13f)": "IN_MASH_CIR",
    "[R1 held] fwd(target) + press CIR": "IN_LOCK_TARGET_CIR",
    "press SQ": "IN_SQ", "press SQ + charge SQ 90f release": "IN_CHARGE_SQ",
    "mash SQ >6x (gap<=13f)": "IN_MASH_SQ", "mash CIR >2x (gap<=9f)": "IN_MASH_CIR_SHORT",
    "[Artemis equipped] hold SQ + charge SQ 0f release": "IN_RELEASE_SQ",
    "[Artemis equipped] hold CIR + charge CIR 0f release": "IN_RELEASE_CIR",
}
GAME_STYLES = {"2", "3"}  # weapon-independent style rows emitted: Trickster, Royalguard
ADV = {"end": 0, "flag1": 2, "flag2": 3, "land": 4, "mash": 5, "charge": 6, "timer": 7, "stance": 8, "shot": 9}
BTN = {"TRI": "BTN_TRI", "CIR": "BTN_CIR", "SQ": "BTN_SQ"}
POSTURE = {"ground": "POSTURE_GROUND", "air": "POSTURE_AIR", "both": "POSTURE_BOTH"}


def runtime_name(f, i):
    if f <= 2:
        return f"{f + 2}-{i:02d}"
    if f == 3:
        return f"rb-{i:02d}"
    return f"s{20 + f}-{i:02d}"


def events(script):
    frame, out, motion, start = 0, [], None, -1
    for op in script["ops"]:
        raw = bytes.fromhex(op.get("raw", "") or "")
        if op["op"] == 0:
            if op["wait"] == 0x7FFF:
                break
            frame = op["wait"]
        elif op["op"] == 1 and motion is None:
            lay = [l for l in op["def"]["layers"] if l["layer"] == 0]
            motion = runtime_name(lay[0]["file"], lay[0]["index"])
            start = -1 if op["frame"] == 0x7FFF else op["frame"]
        elif op["op"] == 3:
            out.append((frame, "EV_ACTIVE", 1 if raw[2] & 0x80 else 0, 0))
            out.append((frame, "EV_PLACE", raw[2] & 0x3F, 0))  # weapon placement state, +0x24d2
            if raw[5]:   # window 0 byte 4 -> player+0x24d5 (one-shot; handlers branch on it)
                out.append((frame, "EV_FLAG", raw[5], 0))
            if raw[3] & 0xF0 == 0x20:
                out.append((frame, "EV_HIT", raw[3] & 0xF, 0))
        elif op["op"] == 4:
            for b in raw[1:6]:
                state = {1: 2, 2: 0, 3: 1}.get(b >> 4)
                if state is not None:
                    out.append((frame, "EV_GATE", b & 0xF, state))
    return motion, start, out


def air_values(spec, cfg):
    """(speed, speed accel, vy, gravity) per frame from a moveset_phases air spec."""
    if spec is None:
        return None
    rd = lambda off: struct.unpack_from("<f", cfg, off)[0]
    if spec[0] == "cfg":
        return (rd(spec[1]), rd(spec[2]), rd(spec[3]), rd(spec[4]))
    if spec[0] == "vy":
        return (0.0, 0.0, rd(spec[1]), rd(spec[2]))
    return tuple(spec[1:5])


def weapon_of(rows, action):
    for c in rows:
        if c["action"] == str(action) and c["weapon"] not in ("-1", "128"):
            return int(c["weapon"])
    return -1


def build_moves(rows, weapon_cfg):
    """Flatten ACTIONS into MOVE_G_n phases; loops and charge tiers get their own."""
    moves, first = [], {}

    def add(p, name, is_gun, next_ref):
        i = len(moves)
        moves.append(dict(p=p, name=name, gun=is_gun, next=next_ref, loop=-1, lock=-1, tiers=[-1, -1, -1], tier_start=[-1.0] * 3))
        return i

    for action, phases in sorted(ACTIONS.items()):
        w = weapon_of(rows, action)
        name = WEAPON_NAMES.get(w, "ROYALGUARD" if action >= 190 else "MOVE")
        is_gun = w >= 5
        ids = []
        for p in phases:
            ids.append(add(p, name, is_gun, None))
        for k, i in enumerate(ids):
            moves[i]["next"] = ids[k + 1] if k + 1 < len(ids) else None
        for k, i in enumerate(ids):
            p = moves[i]["p"]
            if p.loop:
                from moveset_phases import P
                lp = P(p.loop, adv="mash", start=p.loop_start, loop=p.loop, loop_start=p.loop_start, btn=p.btn, auto=p.auto, shot=p.shot)
                j = add(lp, name, is_gun, moves[i]["next"])
                moves[j]["loop"] = j
                moves[i]["loop"] = j
            if p.lock:
                from moveset_phases import P
                j = add(P(p.lock, adv=p.adv, frames=p.frames, refire=p.refire, shot=p.shot, layered=p.layered), name, is_gun, moves[i]["next"])
                moves[i]["lock"] = j
            for t, (script, start) in enumerate(p.tiers):
                from moveset_phases import P
                j = add(P(script, start=start), name, is_gun, moves[i]["next"])
                moves[i]["tiers"][t] = j
                moves[i]["tier_start"][t] = start
        first[action] = ids[0]
    return moves, first, weapon_cfg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--moveset", type=Path, default=Path("analysis/moveset"))
    ap.add_argument("--out", type=Path, default=Path("psp/testroom/combat_data.h"))
    a = ap.parse_args()
    scripts = json.loads((a.moveset / "motion_scripts.json").read_text())
    rows = list(csv.DictReader((a.moveset / "command_table.csv").open()))
    weapon_cfg = (a.moveset.parent / "assets/gdata/slot_00000/chunk_011.bin").read_bytes()
    gen_moves, gen_first, _ = build_moves(rows, weapon_cfg)
    all_scripts = list(SCRIPTS)
    for m in gen_moves:
        if m["p"].script not in all_scripts:
            all_scripts.append(m["p"].script)
    L = ["/* Generated by tools/gen_combat_data.py from the DMC3 SE motion scripts",
         " * and command table -- do not edit. See docs/MOVESET_RE.md. */",
         "#ifndef DMC3PSP_TESTROOM_COMBAT_DATA_H", "#define DMC3PSP_TESTROOM_COMBAT_DATA_H", "",
         "typedef enum { EV_ACTIVE, EV_HIT, EV_GATE, EV_FLAG, EV_PLACE } ScriptEventKind;",
         "typedef struct { unsigned short frame; unsigned char kind, a, b; } ScriptEvent;",
         "typedef struct { const char *key, *motion; short start; short count; const ScriptEvent *events; } ScriptData;",
         ""]
    script_rows = []
    for i, key in enumerate(all_scripts):
        motion, start, ev = events(scripts[key])
        L.append(f"static const ScriptEvent script_events_{i}[] = {{ /* {key} */")
        L += [f"    {{{f}, {k}, {x}, {y}}}," for f, k, x, y in ev] or ["    {0, EV_ACTIVE, 0, 0},"]
        L.append("};")
        script_rows.append(f'    {{"{key}", "{motion}", {start}, {max(len(ev), 1)}, script_events_{i}}},')
    L += ["", f"#define SCRIPT_DATA_COUNT {len(all_scripts)}", "static const ScriptData script_data[] = {"] + script_rows + ["};", ""]

    # Gun stance tables (FUN_00222e00): row = direction of travel against the aim
    # (FUN_00223e80: 0 fwd, 1 / 4 sides, 2 / 3 back, 5 still). Column 6 is the idle
    # with no target, 2 the level aim with one, 3 the level shot. A script's layer 1
    # is the upper body, layer 0 the legs.
    elf = (a.moveset.parent / "disc/SLUS_213.61").read_bytes()
    phoff = struct.unpack_from("<I", elf, 0x1C)[0]
    _, seg_off, seg_va = struct.unpack_from("<III", elf, phoff)
    def script_motions(key):
        layers = [o for o in scripts[key]["ops"] if o["op"] == 1][0]["def"]["layers"]
        by = {l["layer"]: runtime_name(l["file"], l["index"]) for l in layers}
        return by.get(1, by[0]), by[0]
    L += ["/* [gun 0-4: E&I, shotgun, Artemis, Spiral, Kalina Ann][row]. */",
          "typedef struct { const char *idle, *aim, *shot, *legs; } GunStanceRow;",
          "static const GunStanceRow gun_stance_rows[5][6] = {"]
    for bank, table in ((8, 0x594420), (9, 0x5944D0), (10, 0x594580), (11, 0x594630), (12, 0x5946E0)):
        L.append("    {")
        for r in range(6):
            cols = struct.unpack_from("<7I", elf, seg_off + table + r * 0x1C - seg_va)
            idle, legs = script_motions(f"{bank}:{cols[6]}")
            aim, _ = script_motions(f"{bank}:{cols[2]}")
            shot, _ = script_motions(f"{bank}:{cols[3]}")
            L.append(f'        {{"{idle}", "{aim}", "{shot}", "{legs}"}},')
        L.append("    },")
    L += ["};", ""]
    # Weapon placement (FUN_00293910): each weapon's table of records by state.
    # mode: 0 fixed to a bone, 255 hidden, else a weapon motion plays. owner: 0 Dante's
    # skeleton, nonzero the coat model's (player+0x6d10, FUN_00296190). Tables from each
    # class's setup (+0xe4). Guns with two models keep the second's at +0x30 (FUN_00293a20).
    u32 = lambda p: struct.unpack_from("<I", elf, seg_off + p - seg_va)[0]
    L += ["#define WEAPON_PLACE_STATES 56",
          "/* 0-4 melee, 5-9 guns. */",
          "static const WeaponPlace weapon_places[10][WEAPON_PLACE_STATES] = {"]
    for table in (0x596200, 0x5970C0, 0x596930, 0x597A70, 0, 0x597C60, 0x597E90, 0x5980B0, 0x5981B0, 0x598340):
        L.append("    {")
        for state in range(56):
            p = u32(table + state * 4) if table else 0
            if not (0x590000 <= p < 0x5B0000):
                L.append("        {255,0,0,{0,0,0},{0,0,0}},")
                continue
            b = elf[seg_off + p - seg_va:seg_off + p - seg_va + 0x30]
            pos = struct.unpack_from("<3f", b, 0x10)
            rot = struct.unpack_from("<3f", b, 0x20)
            L.append(f"        {{{b[0]},{b[2]},{b[3]},{{{pos[0]:.4f}f,{pos[1]:.4f}f,{pos[2]:.4f}f}},{{{rot[0]:.4f}f,{rot[1]:.4f}f,{rot[2]:.4f}f}}}},")
        L.append("    },")
    L += ["};", ""]
    # Two-model weapons keep the second's record at +0x30 of the same entry.
    for name, table in (("ivory_places", 0x597C60), ("rudra_places", 0x596930)):
        L += [f"static const WeaponPlace {name}[WEAPON_PLACE_STATES] = {{"]
        for state in range(56):
            p = u32(table + state * 4)
            if not (0x590000 <= p < 0x5B0000):
                L.append("    {255,0,0,{0,0,0},{0,0,0}},")
                continue
            b = elf[seg_off + p - seg_va:][:0x60]
            pos = struct.unpack_from("<3f", b, 0x40)
            rot = struct.unpack_from("<3f", b, 0x50)
            L.append(f"    {{{b[0]},{b[0x30]},{b[0x31]},{{{pos[0]:.4f}f,{pos[1]:.4f}f,{pos[2]:.4f}f}},{{{rot[0]:.4f}f,{rot[1]:.4f}f,{rot[2]:.4f}f}}}},")
        L += ["};", ""]
    L += ["typedef enum { IN_TRI, IN_CIR, IN_LOCK_FWD_TRI, IN_LOCK_BACK_TRI, IN_LOCK_FWD_CIR,",
          "               IN_LOCK_BACK_CIR, IN_CHARGE_TRI, IN_MASH_TRI, IN_MASH_CIR,",
          "               IN_LOCK_TARGET_CIR, IN_SQ, IN_CHARGE_SQ, IN_MASH_SQ, IN_MASH_CIR_SHORT,",
          "               IN_RELEASE_SQ, IN_RELEASE_CIR } CommandInput;",
          "typedef enum { POSTURE_GROUND = 1, POSTURE_AIR = 2, POSTURE_BOTH = 3 } Posture;",
          "/* gates: n, | 0x80 = optional, 0xff = none. after: action ids, 0 = none. */",
          "typedef struct {",
          "    unsigned char action, type, category, style; /* game style (0 SM, 1 GS, 2 TR, 3 RG); 0xff = any */",
          "    unsigned char weapon;  /* 0-4 melee, 5-9 guns, 0xff = any */",
          "    unsigned short flags;  /* 0x100 from neutral, 0x200 as a cancel, 0x400 extra check */",
          "    unsigned char posture, input;",
          "    unsigned char check;   /* [0xe]: FUN_0024b510 extra check when flags & 0x400 */",
          "    unsigned char gates[5], after[3];",
          "} CommandData;", "", "static const CommandData command_data[] = {"]
    n = 0
    for c in rows:
        style_row = c["type"] == "3" and c["weapon"] == "-1" and c["style"] in GAME_STYLES
        if (c["weapon"] == "-1" and not style_row) or c["weapon"] == "128" or c["input_text"] not in INPUTS:
            continue
        if c["posture"] not in POSTURE:
            continue
        gates, after = [], []
        for tok in c["conditions"].split():
            if tok.startswith("gate"):
                g = int(tok[4:].rstrip("?"))
                gates.append(g | (0x80 if tok.endswith("?") else 0))
            elif tok.startswith("after"):
                after.append(int(tok[5:]))
        gates += [0xFF] * (5 - len(gates))
        after += [0] * (3 - len(after))
        style = c["style"] if c["type"] == "3" else "0xff"
        weapon = "0xff" if c["weapon"] == "-1" else c["weapon"]
        L.append(f"    {{{c['action']}, {c['type']}, {c['category']}, {style}, {weapon}, {c['flags']}, "
                 f"{POSTURE[c['posture']]}, {INPUTS[c['input_text']]}, {c['b_e']}, "
                 f"{{{', '.join(f'{x:#x}' for x in gates)}}}, {{{', '.join(map(str, after))}}}}}, /* row {c['row']} */")
        n += 1
    L += ["};", f"#define COMMAND_DATA_COUNT {n}", ""]
    # FUN_0024b510 cases 5/6/7/0xb: minimum height above the floor, from the
    # player config (archive chunk 9 at player+0x283c).
    cfg = (a.moveset.parent / "assets/gdata/slot_00000/chunk_009.bin").read_bytes()
    h = {k: struct.unpack_from("<f", cfg, off)[0] for k, off in ((5, 0x2F8), (6, 0x2FC), (7, 0x114), (11, 0x32C))}
    L += ["/* Extra check -> minimum height above the floor (cm), or <0 = not a height check. */",
          "static float command_min_height(int check) {",
          "    switch (check) {"]
    L += [f"    case {k}: return {v:.1f}f;" for k, v in h.items()]
    L += ["    default: return -1.0f;", "    }", "}", "", "#endif", ""]
    a.out.write_text("\n".join(L), encoding="ascii")

    # Generated phases: combat_moves.h is the id list (combat.h), combat_moves_table.h
    # the MoveDef rows and action map (combat.c).
    ids = ", ".join(f"MOVE_G_{i}" for i in range(len(gen_moves)))
    (a.out.parent / "combat_moves.h").write_text(
        "/* Generated by tools/gen_combat_data.py from tools/moveset_phases.py. */\n"
        f"#define GEN_MOVE_IDS {ids},\n", encoding="ascii")
    ref = lambda i: "MOVE_NONE" if i is None or i < 0 else f"MOVE_G_{i}"
    fl = lambda v: f"{float(v):.4f}f"
    T = ["/* Generated by tools/gen_combat_data.py from tools/moveset_phases.py. */", "#ifdef GEN_MOVE_TABLE"]
    for i, m in enumerate(gen_moves):
        p = m["p"]
        air = air_values(p.air, weapon_cfg)
        flag_air = air_values(p.flag_air, weapon_cfg)
        f = [f'.script = "{p.script}"', f'.name = "{m["name"]}"', f".next = {ref(m['next'])}",
             f".next_on = {ADV[p.adv]}", f".start = {fl(p.start)}",
             f".reach = {0.0 if m['gun'] else 260.0:.1f}f", f".hit_kind = {'-1' if m['gun'] else 'HIT_LIGHT'}", f".gun = {int(m['gun'])}"]
        if m["lock"] >= 0:
            f.append(f".lock_move = {ref(m['lock'])}")
        if p.shot:
            f.append(f".shot = {p.shot}")
        if p.layered:
            f.append(".layered = 1")
        if p.flag_g:
            f.append(f".flag_g = {fl(p.flag_g)}")
        if p.stop_below:
            f.append(f".stop_below = {fl(p.stop_below)}")
        if p.hover:
            f.append(".hover = 1")
        if p.auto:
            f.append(f".auto_fire = {fl(p.auto)}")
        if p.refire:
            f.append(f".refire = {fl(p.refire)}")
        if p.adv in ("mash", "charge"):
            f.append(f".btn = {BTN[p.btn]}")
        if m["loop"] >= 0:
            f.append(f".loop_move = {ref(m['loop'])}")
        if p.frames:
            f.append(f".frames = {fl(p.frames)}")
        if p.tiers:
            f.append(".tiers = {" + ", ".join(ref(t) for t in m["tiers"]) + "}")
        if air:
            f.append(".air = 1, .air_v = {" + ", ".join(fl(v) for v in air) + "}")
        if flag_air:
            f.append(".flag_air = 1, .flag_v = {" + ", ".join(fl(v) for v in flag_air) + "}")
        T.append(f"    [MOVE_G_{i}] = {{{', '.join(f)}}},")
    T += ["#else", "static MoveId gen_action_move(int action) {", "    switch (action) {"]
    T += [f"    case {act}: return MOVE_G_{first};" for act, first in sorted(gen_first.items())]
    T += ["    default: return MOVE_NONE;", "    }", "}"]
    T += [f"static const unsigned char gen_actions[] = {{{', '.join(str(x) for x in sorted(gen_first))}}};", "#endif", ""]
    (a.out.parent / "combat_moves_table.h").write_text("\n".join(T), encoding="ascii")
    print(f"{len(all_scripts)} scripts, {n} commands, {len(gen_moves)} generated phases for {len(gen_first)} actions -> {a.out}")


if __name__ == "__main__":
    main()
