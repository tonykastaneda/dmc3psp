#!/usr/bin/env python3
"""Disassemble a mission's event script (GDATA EventTblNN.bin, slot 238 + NN) into a readable listing.

    python tools/event_dump.py <iso> 12 --out <dir>      # writes evt_m12.txt and evt_m12.json there

Format (docs/EVENT_SCRIPTS.md): "EVT\\0", u16 1, u16 script count, u32 offset of the script table; each op is a
u32 word (low byte the opcode, byte 1 the argument count) and that many s32 arguments. Op names follow the
listings made for Missions 6-9; ops without a name print as op_XX. The output is the player's own disc data, so
it belongs in a local folder, not the repo.
"""
import argparse
import json
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660

NAMES = {
    0x01: "END", 0x02: "play_demo", 0x03: "goto_room", 0x04: "mission_start_screen", 0x05: "mission_clear",
    0x06: "queue_demo", 0x07: "ROOM", 0x08: "END_ROOM", 0x09: "AFTER_DEMO", 0x0a: "END_AFTER_DEMO",
    0x0b: "AFTER_START_SCREEN", 0x0c: "END_AFTER_START_SCREEN", 0x0d: "BLOCK", 0x0e: "END_BLOCK",
    0x0f: "sw_in_area", 0x10: "sw_flag", 0x11: "sw_counter", 0x12: "sw_event_flag", 0x13: "sw_trigger",
    0x14: "sw_item_count", 0x15: "sw_item_got", 0x17: "sw_enemies_cleared", 0x1c: "sw_action_pressed",
    0x1e: "sw_prev_room", 0x1f: "sw_cur_room", 0x29: "sw_is_vergil", 0x2c: "case_eq", 0x2d: "case_ne",
    0x2e: "case_ge", 0x2f: "case_le", 0x30: "default", 0x31: "end_switch", 0x32: "gimmick_bind_flag",
    0x33: "door_attach_seal", 0x36: "gimmick_cmd10", 0x37: "gimmick_cmd0c", 0x38: "gimmick_cmd2c",
    0x3d: "end_block", 0x3e: "spawn_group", 0x42: "op_42", 0x43: "hold_cleared", 0x44: "door_lock",
    0x45: "door_unlock", 0x46: "door_enable", 0x47: "door_disable", 0x48: "door_retarget", 0x49: "event_begin",
    0x4a: "event_cam_flags", 0x4c: "event_end", 0x4d: "cam_play", 0x4f: "cam_wait", 0x50: "thread_start",
    0x53: "skip_spawn_group", 0x54: "skip_bgm", 0x56: "skip_boss_fixup", 0x57: "thread_wait_time",
    0x58: "wait", 0x59: "cam_op", 0x5b: "player_warp", 0x5c: "give_item", 0x5d: "take_item",
    0x64: "area_enable", 0x65: "set_flag", 0x66: "counter_set", 0x67: "counter_inc", 0x68: "counter_dec",
    0x69: "set_event_flag", 0x6a: "effect", 0x6e: "message", 0x6f: "message_wait", 0x70: "bgm",
    0x71: "se_3d", 0x72: "se", 0x75: "cam_mode",
}
OPEN = {0x07, 0x09, 0x0b, 0x0d}
CLOSE = {0x08, 0x0a, 0x0c, 0x0e}


def disassemble(data):
    if data[:4] != b"EVT\0":
        raise ValueError("not an EVT file")
    count, table = struct.unpack_from("<HI", data, 6)
    scripts = list(struct.unpack_from(f"<{count}I", data, table))
    ops, lines = [], []
    for s, start in enumerate(scripts):
        at, depth = start, 0
        lines.append(f"---- script {s} @ {start:#x}")
        while at + 4 <= len(data):
            word = struct.unpack_from("<I", data, at)[0]
            op, argc = word & 0xFF, (word >> 8) & 0xFF
            args = list(struct.unpack_from(f"<{argc}i", data, at + 4)) if argc else []
            name = NAMES.get(op, f"op_{op:02x}")
            if op in CLOSE:
                depth = max(0, depth - 1)
            lines.append(f"{at:04x} {'  ' * depth}{name} {args if args else ''}".rstrip())
            ops.append({"off": at, "op": op, "name": name, "args": args, "depth": depth, "script": s})
            if op in OPEN:
                depth += 1
            at += 4 + 4 * argc
            if op == 0x01:
                break
    return scripts, ops, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("mission", type=int)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    slot = 238 + a.mission
    with Iso9660(a.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        e = afs.entries[slot]
        data = iso.read_file("GDATA.AFS", e["offset"], e["size"])
    scripts, ops, lines = disassemble(data)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / f"evt_m{a.mission}.txt").write_text("\n".join(lines) + "\n")
    (a.out / f"evt_m{a.mission}.json").write_text(json.dumps(
        {"source": f"GDATA.AFS slot {slot} (EventTbl{a.mission:02d}.bin)", "scripts": scripts, "ops": ops}, indent=0))
    print(f"EventTbl{a.mission:02d}: {len(scripts)} scripts, {len(ops)} ops")


if __name__ == "__main__":
    main()
