#!/usr/bin/env python3
"""The event scripts' on-screen messages, from the player's own disc, into a local generated header.

    python tools/gen_messages.py <iso> --header build/testroom/generated/messages_runtime.h

Two parts, both read at build time and never committed (the header lands in build/, which is gitignored):

* The text: msg_eng.pac (GDATA slot 221) holds "# Msg <id>" blocks of quoted strings with tags. <BR> breaks a
  line, <WI> waits for a button, <CL> clears the box (a new page), <FC r g b> sets the colour (PS2 0-128),
  <PS x y> places the box (all the script's messages use 35 416, the bottom of the 640x448 screen). Pages are
  joined with '\\f', lines with '\\n'; a colour is \\x01 then r, g, b each as 1 + value / 2 (never a 0 byte),
  and \\x02 goes back to the default. Other tags are dropped.
* The blocks: in each mission's event script (EventTblNN.bin, GDATA 238 + NN, docs/EVENT_SCRIPTS.md) every
  BLOCK inside a ROOM section (those that show a message or move the camera, and the rest for the flags and
  counters they set) is kept whole, its ops as they are, for psp/testroom/messages.c to run: the switches it
  can answer (areas, examine, items, flags and counters it keeps itself, enemies cleared), cases, waits,
  `message` and `message_wait`, and the event camera's ops. Other ops are kept so the jumps stay right, and
  skipped. A room section's first block is the on-enter one (type 3 here). Each block's areas (the room's EVE
  records) come along as quads.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dmc3_image import AfsArchive, Iso9660  # noqa: E402
from prepare_stage import pac_entries  # noqa: E402
import gen_rooms  # noqa: E402

MSG_SLOT = 221
EVENT_SLOT = 238
SWITCHES = range(0x0F, 0x2C)


def parse_messages(raw):
    text = raw.decode("cp932", "replace")
    out = {}
    for m in re.finditer(r"# Msg (\d+)\s*\n(.*?)# End", text, re.S):
        body = "".join(re.findall(r'"(.*?)"', m.group(2), re.S))
        s = ""
        for tok in re.split(r"(<[^>]*>)", body):
            if not tok.startswith("<"):
                s += tok
                continue
            tag = tok[1:-1].split()
            if not tag:
                continue
            if tag[0] == "BR":
                s += "\n"
            elif tag[0] == "CL":
                s = s.rstrip("\n") + "\f"
            elif tag[0] == "FC" and len(tag) == 4:
                rgb = [int(v) for v in tag[1:]]
                s += "\x02" if rgb == [88, 88, 88] else "\x01" + "".join(chr(1 + min(254, v * 2) // 2) for v in rgb)
        s = s.replace("\r", "").strip("\n\f")
        out[int(m.group(1))] = s
    return out


def script_ops(data):
    """Script 0 (the main one) as (op, args), to its END."""
    count, table = struct.unpack_from("<HI", data, 6)
    at = struct.unpack_from("<I", data, table)[0]
    ops = []
    while at + 4 <= len(data):
        word = struct.unpack_from("<I", data, at)[0]
        op, argc = word & 0xFF, (word >> 8) & 0xFF
        if at + 4 + 4 * argc > len(data):
            break
        ops.append((op, list(struct.unpack_from(f"<{argc}i", data, at + 4))))
        at += 4 + 4 * argc
        if op == 0x01:
            break
    return ops


ON_ENTER = 3   # the room section's first block: FUN_0020b7a0 runs it once as the room loads, whatever its type


def message_blocks(ops):
    """(room, block type, ops) for each BLOCK in a ROOM section."""
    out, room, i, first = [], None, 0, False
    while i < len(ops):
        op, args = ops[i]
        if op == 0x07:
            room, first = args[0], True
        elif op in (0x08, 0x09, 0x0B):
            room = None
        elif op == 0x0D and room is not None:
            j = i + 1
            while j < len(ops) and ops[j][0] != 0x0E:
                j += 1
            body = ops[i + 1:j]
            # Every block: those without a message or a camera op still set the scripts' flags and counters,
            # which the kept ones test (an intro's "already seen" flag, a counter a demo block steps).
            out.append((room, ON_ENTER if first else args[0], body))
            first = False
            i = j
        i += 1
    return out


def c_string(s):
    out = '"'
    for ch in s:
        o = ord(ch)
        if ch == "\\" or ch == '"':
            out += "\\" + ch
        elif 32 <= o < 127:
            out += ch
        elif o < 256:
            out += f'\\{o:03o}'
        else:
            out += "?"
    return out + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--header", type=Path, required=True)
    a = ap.parse_args()
    blocks, areas = [], {}
    with Iso9660(a.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        read = lambda slot: iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"])
        texts = parse_messages(read(MSG_SLOT))
        for mission in range(1, 22):   # 21: the Bloody Palace (its rooms' cameras: the portals' reveal, the bosses' entrances and exits)
            for room, kind, body in message_blocks(script_ops(read(EVENT_SLOT + mission))):
                name = f"{room:03d}"
                if name not in gen_rooms.SLOTS:
                    continue
                blocks.append((mission, room, kind, body))
                for op, args in body:
                    if op == 0x0F and (room, args[0]) not in areas:
                        data = pac_entries(read(gen_rooms.SLOTS[name] + 1))
                        try:
                            areas[(room, args[0])] = gen_rooms.parse_area(data, args[0])
                        except (StopIteration, struct.error):
                            pass
    used = sorted({args[0] for *_, body in blocks for op, args in body if op == 0x6E and args and args[0] in texts})
    lines = ["/* Generated by tools/gen_messages.py from the player's disc: the event scripts' messages. Not in the repo. */",
             "#ifndef MESSAGES_RUNTIME_H", "#define MESSAGES_RUNTIME_H",
             "typedef struct { short id; const char *text; } MsgText;",
             "typedef struct { short op, a, b, c, d; } MsgOp;",
             "typedef struct { short mission, room, kind, first, count; } MsgBlock;",
             "typedef struct { short room, area; float xz[4][2], y, height; } MsgArea;",
             f"#define MSG_TEXT_COUNT {len(used)}", "static const MsgText msg_texts[] = {"]
    lines += [f"    {{{i}, {c_string(texts[i])}}}," for i in used] + ["};"]
    flat, table = [], []
    for mission, room, kind, body in blocks:
        table.append((mission, room, kind, len(flat), len(body)))
        flat += [(op, *(args + [0, 0, 0, 0])[:4]) for op, args in body]
    lines += [f"#define MSG_OP_COUNT {len(flat)}", "static const MsgOp msg_ops[] = {"]
    lines += ["    " + " ".join("{%d,%d,%d,%d,%d}," % t for t in flat[k:k + 8]) for k in range(0, len(flat), 8)] + ["};"]
    lines += [f"#define MSG_BLOCK_COUNT {len(table)}", "static const MsgBlock msg_blocks[] = {"]
    lines += [f"    {{{m}, {r}, {k}, {f}, {n}}}," for m, r, k, f, n in table] + ["};"]
    lines += [f"#define MSG_AREA_COUNT {len(areas)}", "static const MsgArea msg_areas[] = {"]
    for (room, area), (quad, y, h) in sorted(areas.items()):
        q = ", ".join(f"{{{x:.1f}f, {z:.1f}f}}" for x, z in quad)
        lines.append(f"    {{{room}, {area}, {{{q}}}, {y:.1f}f, {h:.1f}f}},")
    lines += ["};", "#endif", ""]
    a.header.parent.mkdir(parents=True, exist_ok=True)
    a.header.write_text("\n".join(lines))
    print(f"messages: {len(used)} texts, {len(table)} blocks, {len(flat)} ops, {len(areas)} areas -> {a.header}")


if __name__ == "__main__":
    main()
