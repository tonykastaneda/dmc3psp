"""The Seven Hells' AI data, read from the player's own disc into a C header.

From CEm000's pack (GDATA slot 117): chunk 38, the motion scripts that say which motions play and
when attacks are live; chunks 39 and 40, the attack spheres. From the executable: the tables its
decision code picks moves from. See docs/ENEMY_AI.md.
"""
import struct

SCRIPT_CHUNK, CAP_CHUNK, SHAPE_CHUNK = 38, 39, 40
AI_TABLE = 0x5A8300
# Per class, from their set-ups (build/hdref/enemy_specs.md): the Pride, the Gluttony (CEm001), the Sloth (CEm003).
CLASS_TABLES = (("pride", 0x5A8300), ("lust", 0x5ACC10), ("gluttony", 0x5AB9D0), ("sloth", 0x5B0E60), ("envy", 0x5CF5B0), ("greed", 0x5B0330), ("wrath", 0x5B1460),
                ("abyss", 0x5CE910))   # the Abyss (CEm006): its AI code is a clone of the Hells' (CComEm006); its set-up
                                       # (FUN_0041c240) hands FUN_004a8f00 its block's +0x10, 0x5ce910, laid out as
                                       # theirs: three bands (300, 600), its own zones and rows, plans 40 / 58 / 59 too
ABYSS_GROUP1 = 100   # the Abyss's own scripts (its bank's group 1) answer to 100 + n, playing PAC [36]'s motions (100+)
HIT_SCRIPTS = 0x5A7A80   # the list states name their hit reactions through
HIT_SCRIPT_COUNT = 28
SCRIPT_COUNT = 100
END_FRAME = 0x7FFF
OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO = 0, 1, 2, 3


class Elf:
    def __init__(self, data):
        self.data = data
        phoff = struct.unpack_from("<I", data, 28)[0]
        count = struct.unpack_from("<H", data, 44)[0]
        self.segments = [struct.unpack_from("<8I", data, phoff + i * 32) for i in range(count)]

    def _at(self, address):
        for s in self.segments:
            if s[0] == 1 and s[2] <= address < s[2] + s[4]:
                return s[1] + address - s[2]
        raise ValueError(f"{address:#x} is not in the executable")

    def word(self, address):
        return struct.unpack_from("<I", self.data, self._at(address))[0]

    def byte(self, address):
        return self.data[self._at(address)]

    def real(self, address):
        return struct.unpack_from("<f", self.data, self._at(address))[0]


def compile_scripts(chunk, caps, group=0, script_count=None, motion_limit=100):
    """Flatten the bytecode the game's motion player runs (FUN_00254f10) into a simple op list. group: which of the
    bank's script groups (a script id's hundreds); motion_limit: motion ids under it are kept (the hundreds pick the
    PAC: 0-99 PAC [35], 100-199 [36], ...), the rest play nothing."""
    u16 = lambda o: struct.unpack_from("<H", chunk, o)[0]
    i16 = lambda o: struct.unpack_from("<h", chunk, o)[0]
    script_base, map_base = u16(0), u16(2)
    scripts = script_base + u16(script_base + group * 2)
    count = SCRIPT_COUNT if script_count is None else min(script_count, u16(scripts) // 2)

    def motion_entry(group, index):
        entry = map_base + u16(map_base + group * 2)
        return entry + u16(entry + index * 2)

    def motion_slot(group, index):
        entry = motion_entry(group, index)
        if entry + 8 > len(chunk) or not chunk[entry]:
            return 255
        motion = u16(entry + 2 + 4)  # hundreds pick the PAC
        return motion if motion < motion_limit else 255

    def motion_loops(group, index):
        entry = motion_entry(group, index)
        return 1 if entry + 8 <= len(chunk) and chunk[entry] and chunk[entry + 2 + 1] == 1 else 0

    ops, starts = [], []
    for n in range(count):
        offset = u16(scripts + n * 2)
        # The table has no count: entries past its end read as junk, so they fall off the chunk.
        if offset == 0xFFFF or scripts + offset >= len(chunk):
            starts.append(-1)
            continue
        p = scripts + offset
        starts.append(len(ops))
        attack = 0
        seen = {}
        for _ in range(64):
            if p >= len(chunk):
                break
            seen[p] = len(ops)
            op = chunk[p]
            if op == 0:
                frame, finish = i16(p + 2), chunk[p + 4] & 1
                ops.append((OP_WAIT, finish, -1 if frame == END_FRAME else frame, 0))
                p += 6
                if finish:
                    break
            elif op == 1:
                start = i16(p + 6)
                group, index = chunk[p + 4], chunk[p + 5]
                ops.append((OP_PLAY, motion_slot(group, index), -1 if start == END_FRAME else start,
                            chunk[p + 3] | motion_loops(group, index) << 8))
                p += 8
            elif op == 2:
                target = p + 2 - i16(p + 2)
                if target in seen:
                    ops.append((OP_GOTO, 0, seen[target], 0))
                break
            elif op in (3, 4):
                p += 6
            elif op == 5:
                p += 2
            elif op in (6, 7, 8):
                if op != 8:
                    attack = chunk[p + 1]
                p += 2
            elif 0x10 <= op <= 0x1F:
                count, duration = op - 0xF, i16(p + 2)
                for i in range(count):
                    cap = chunk[p + 4 + i * 2]
                    if cap < len(caps) and caps[cap][0] == 1:
                        ops.append((OP_HIT, cap, duration, attack))
                p += 4 + count * 2
            elif op == 0x20 or op == 0x24:
                p += 4
            elif op == 0x21 or op == 0x23:
                p += 2
            elif op == 0x22:
                p += 6
            else:
                break
    return ops, starts


def read_caps(pack):
    caps, table, shapes = [], pack[CAP_CHUNK], pack[SHAPE_CHUNK]
    for i in range(len(table) // 4):
        kind, bone = table[i * 4], table[i * 4 + 1]
        shape = struct.unpack_from("<H", table, i * 4 + 2)[0] * 0x50
        x, y, z = struct.unpack_from("<3f", shapes, shape + 0x10)
        radius = struct.unpack_from("<f", shapes, shape + 0x20)[0]
        if struct.unpack_from("<I", shapes, shape)[0] == 4:
            radius = struct.unpack_from("<f", shapes, shape + 0x30)[0]
        caps.append((kind, bone, x, y, z, radius))
    return caps


def read_rows(elf, table):
    """The weighted lists: rows[list][distance band][rank column] = [(plan, weight)]."""
    def row(address):
        out, total = [], 0.0
        while total < 99.0 and len(out) < 8:
            plan, weight = elf.word(address), elf.real(address + 4)
            address += 8
            total += weight
            if weight > 0.0:
                out.append((plan, weight))
        return out
    bands = []
    a = elf.word(table + 0xC)
    while elf.real(a) > 0.0:
        bands.append(elf.real(a))
        a += 4
    lists = []
    for which in range(2):
        base = elf.word(table + which * 4)
        lists.append([[row(elf.word(elf.word(base + b * 4) + c * 4)) for c in range(7)]
                      for b in range(len(bands) + 1)])
    return bands, lists


def real(value):
    return f"{value:.6f}f"


def floats(elf, address, count):
    return [elf.real(address + i * 4) for i in range(count)]


def write_header(path, pack, executable, envy_pack=None, abyss_pack=None):
    elf = Elf(executable)
    caps = read_caps(pack)
    ops, starts = compile_scripts(pack[SCRIPT_CHUNK], caps)
    # CEm005 maps its actions to group 1. Keep these after the shared group 0.
    greed_ops, greed_starts = compile_scripts(pack[SCRIPT_CHUNK], caps, group=1, script_count=9, motion_limit=110)
    base = len(ops)
    ops.extend((op, a, b + base if op == OP_GOTO else b, c) for op, a, b, c in greed_ops)
    starts.extend(start + base if start >= 0 else -1 for start in greed_starts)
    bands, lists = read_rows(elf, AI_TABLE)
    times = floats(elf, elf.word(AI_TABLE + 0x10), 6)
    thresholds = floats(elf, elf.word(AI_TABLE + 0x14), 6)
    zones, a = [], elf.word(AI_TABLE + 0x18)
    while elf.real(a) > 0.0:
        zones.append(floats(elf, a, 3))
        a += 12
    lines = ["/* Generated from the disc by tools/gen_enemy_ai.py */", "#pragma once",
             "typedef struct { unsigned char op, a; short b, c; } EnemyAiOp;",
             "typedef struct { unsigned char plan; float weight; } EnemyAiPick;",
             "typedef struct { unsigned char count; EnemyAiPick picks[8]; } EnemyAiRow;",
             "typedef struct { unsigned char kind, bone; float x, y, z, radius; } EnemyAiCap;",
             "typedef struct { float damage, reaction; int kind; } EnemyAiAttack;",
             f"#define ENEMY_AI_BANDS {len(bands) + 1}",
             "static const EnemyAiOp enemy_ai_ops[] = {"]
    lines += [f"    {{{o},{x},{b},{c}}}," for o, x, b, c in ops]
    lines += ["};", f"static const short enemy_ai_scripts[{len(starts)}] = {{" + ",".join(map(str, starts)) + "};",
              "static const EnemyAiCap enemy_ai_caps[] = {"]
    lines += [f"    {{{k},{b},{real(x)},{real(y)},{real(z)},{real(r)}}}," for k, b, x, y, z, r in caps]
    lines += ["};",
              "static const unsigned char enemy_ai_hit_scripts[] = {" +
              ",".join(str(elf.byte(HIT_SCRIPTS + i * 2 + 1)) for i in range(HIT_SCRIPT_COUNT)) + "};",
              "static const float enemy_ai_bands[] = {" + ",".join(map(real, bands)) + "};",
              "static const float enemy_ai_times[] = {" + ",".join(map(real, times)) + "};",
              "static const float enemy_ai_thresholds[] = {" + ",".join(map(real, thresholds)) + "};",
              "static const float enemy_ai_zones[][3] = {" +
              ",".join("{" + ",".join(map(real, z)) + "}" for z in zones) + "};",
              f"#define ENEMY_AI_ZONES {len(zones)}",
              "static const EnemyAiRow enemy_ai_rows[2][ENEMY_AI_BANDS][7] = {"]
    for table in lists:
        lines.append("  {")
        for band in table:
            cells = ["{%d,{%s}}" % (len(row), ",".join("{%d,%s}" % (p, real(w)) for p, w in row)) for row in band]
            lines.append("    {" + ",".join(cells) + "},")
        lines.append("  },")
    lines.append("};")
    # Each Seven Hell's own decision table (the rest of the AI is shared): see build/hdref/enemy_specs.md.
    classes = []
    for name, address in CLASS_TABLES:
        b, l = read_rows(elf, address)
        tm = floats(elf, elf.word(address + 0x10), 6)
        th = floats(elf, elf.word(address + 0x14), 6)
        zs, a = [], elf.word(address + 0x18)
        while elf.real(a) > 0.0:
            zs.append(floats(elf, a, 3))
            a += 12
        classes.append((name, b, l, tm, th, zs))
    most = max(len(b) + 1 for _, b, *_ in classes)
    lines += [f"#define ENEMY_AI_MAX_BANDS {most}",
              "typedef struct { short band_count, zone_count; const float *bands, *times, *thresholds; "
              "const float (*zones)[3]; const EnemyAiRow (*rows)[ENEMY_AI_MAX_BANDS][7]; } EnemyAiClass;"]
    for name, b, l, tm, th, zs in classes:
        lines += [f"static const float ai_{name}_bands[] = {{" + ",".join(map(real, b + [0.0])) + "};",
                  f"static const float ai_{name}_times[] = {{" + ",".join(map(real, tm)) + "};",
                  f"static const float ai_{name}_thresholds[] = {{" + ",".join(map(real, th)) + "};",
                  f"static const float ai_{name}_zones[][3] = {{" + ",".join("{" + ",".join(map(real, z)) + "}" for z in zs) + "};",
                  f"static const EnemyAiRow ai_{name}_rows[2][ENEMY_AI_MAX_BANDS][7] = {{"]
        for table in l:
            lines.append("  {")
            for band in table:
                cells = ["{%d,{%s}}" % (len(row), ",".join("{%d,%s}" % (p_, real(w)) for p_, w in row)) for row in band]
                lines.append("    {" + ",".join(cells) + "},")
            lines.append("  },")
        lines.append("};")
    lines.append("static const EnemyAiClass enemy_ai_classes[] = {" + ",".join(
        f"{{{len(b) + 1},{len(zs)},ai_{n}_bands,ai_{n}_times,ai_{n}_thresholds,ai_{n}_zones,ai_{n}_rows}}"
        for n, b, l, tm, th, zs in classes) + "};")
    lines.append("enum { " + ", ".join(f"AI_CLASS_{n.upper()}" for n, *_ in classes) + " };")
    if envy_pack is not None:
        envy_caps = read_caps(envy_pack)
        envy_ops, envy_starts = compile_scripts(envy_pack[SCRIPT_CHUNK], envy_caps)
        lines += ["static const EnemyAiOp envy_ai_ops[] = {" +
                  ",".join("{%d,%d,%d,%d}" % op for op in envy_ops) + "};",
                  "static const short envy_ai_scripts[] = {" + ",".join(map(str, envy_starts)) + "};",
                  "static const EnemyAiCap envy_ai_caps[] = {" + ",".join(
                      "{%d,%d,%s,%s,%s,%s}" % (k, b, real(x), real(y), real(z), real(r))
                      for k, b, x, y, z, r in envy_caps) + "};"]
        lines += ["static const unsigned char envy_ai_hit_scripts[] = {" +
                  ",".join(str(elf.byte(0x5CED30 + i * 2 + 1)) for i in range(HIT_SCRIPT_COUNT)) + "};"]
    if abyss_pack is not None:
        # The Abyss (em006, GDATA 118): the Hells' motions and caps layout, its own scripts [38] and caps [39] / [40].
        # FUN_0041c240 installs the block's +0xc at enemy+0x2304 for the hit-script lookup.
        abyss_hit_scripts = elf.word(0x61A678 + 0x0C)
        attack_table = elf.word(0x61A678)
        attack_end = elf.word(0x61A678 + 4)
        lines += ["static const EnemyAiAttack abyss_ai_attacks[] = {" +
                  ",".join("{%s,%s,%d}" % (real(elf.real(a + 4)), real(elf.real(a + 0x1c)), elf.word(a + 0x18))
                           for a in range(attack_table, attack_end, 0x40)) + "};"]
        abyss_caps = read_caps(abyss_pack)
        abyss_ops, abyss_starts = compile_scripts(abyss_pack[SCRIPT_CHUNK], abyss_caps, motion_limit=200)
        # Its own scripts (group 1, ids 100+: 100 the rise, 101 the sink, 102 the sweep), after the shared ones.
        own_ops, own_starts = compile_scripts(abyss_pack[SCRIPT_CHUNK], abyss_caps, group=1, script_count=10, motion_limit=200)
        base = len(abyss_ops)
        abyss_ops += [(o, a, b + base if o == OP_GOTO else b, c) for o, a, b, c in own_ops]
        abyss_starts = abyss_starts[:ABYSS_GROUP1] + [-1] * (ABYSS_GROUP1 - len(abyss_starts)) + \
            [s + base if s >= 0 else -1 for s in own_starts]
        lines += ["static const EnemyAiOp abyss_ai_ops[] = {" +
                  ",".join("{%d,%d,%d,%d}" % op for op in abyss_ops) + "};",
                  "static const short abyss_ai_scripts[] = {" + ",".join(map(str, abyss_starts)) + "};",
                  "static const EnemyAiCap abyss_ai_caps[] = {" + ",".join(
                      "{%d,%d,%s,%s,%s,%s}" % (k, b, real(x), real(y), real(z), real(r))
                      for k, b, x, y, z, r in abyss_caps) + "};",
                  "static const unsigned char abyss_ai_hit_scripts[] = {" +
                  ",".join(str(elf.byte(abyss_hit_scripts + i * 2 + 1)) for i in range(HIT_SCRIPT_COUNT)) + "};"]
    path.write_text("\n".join(lines) + "\n")
