"""Disassemble a character's motion script file (Dante: GDATA slot 0, PAC chunk 5).

Layout, recovered from SLUS_213.61 (SE):

  FUN_00254d60 (slot init): file = [u16 off_scripts, u16 off_motdefs, u16 off_3]
      slot+4 = file + off_scripts, slot+8 = file + off_motdefs
  FUN_00255830 (mode 0): table = part + u16(part)   (part starts with u16 4, u16 -1)
  FUN_002557f0: bank count = u16 entries until 0xffff
  FUN_002557b0: script = (t + t[bank]) + (t + t[bank])[index]   (two-level u16 offsets)

  Script bytecode (FUN_00254f10, ends at op 0 + 6-byte trailer read by FUN_00255790):
    01 b1 b2 b3 grp sub s16(frame/0x7fff)   play motion def (grp, sub)       8 bytes
    02 xx s16                                 jump back by s16 (loop)           4
    03/04 5 bytes                             copy cancel/flag window 0/1       6
    05 xx                                     clear windows                     2
    06/07 n, 08 xx                            effect-manager calls              2
    10..1f layer xx u16 id + n*u16 (n=op-0xf) attach effects                    4+2n
    20 layer mask xx                          kill effects by mask              4
    21 xx, 22 5 bytes, 23 xx, 24 layer s16                                      2/6/2/4

  Motion def (FUN_002558a0), via the second table with (grp, sub):
    u8 n, u8 pad, n x {u8 layer, u8 loop, u8 rootflag, u8 ?, u16 motion_id}
    motion_id = file * 100 + index into the slot's motion file list (slot+0x40)
    then u8 n_events, u8 pad, events {u8 op 0x30-0x39, u8 layer, ..., u16 frame}
    (0x30-0x33: 6 bytes, 0x34-0x39: 8 bytes; sound / effect cues).
"""
import argparse
import csv
import json
import struct
from pathlib import Path

SIZES = {1: 8, 2: 4, 3: 6, 4: 6, 5: 2, 6: 2, 7: 2, 8: 2, 0x20: 4, 0x21: 2, 0x22: 6, 0x23: 2, 0x24: 4}


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def table(d, part):
    base = part + u16(d, part)
    n = 0
    while u16(d, base + n * 2) != 0xFFFF:
        n += 1
    return base, n


def disasm(d, p, limit=4096):
    ops = []
    end = p + limit
    while p < min(end, len(d)):
        op = d[p]
        if op == 0:
            ops.append({"op": 0, "trailer": d[p:p + 6].hex()})
            break
        if 0x10 <= op <= 0x1F:
            size = 4 + 2 * (op - 0xF)
        else:
            size = SIZES.get(op)
        if size is None:
            ops.append({"op": op, "bad": True, "raw": d[p:p + 8].hex()})
            break
        raw = d[p:p + size]
        ins = {"op": op, "raw": raw.hex()}
        if op == 1:
            ins.update(loop=raw[1], b2=raw[2], b3=raw[3], grp=raw[4], sub=raw[5],
                       frame=struct.unpack_from("<h", raw, 6)[0])
        elif op == 2:
            ins["back"] = struct.unpack_from("<h", raw, 2)[0]
        ops.append(ins)
        p += size
    return ops


def motion_def(d, mbase, grp, sub):
    t = mbase + u16(d, mbase + grp * 2)
    p = t + u16(d, t + sub * 2)
    n = d[p]
    q = p + 2
    layers = []
    for _ in range(n):
        layer, loop, root, x, mid = struct.unpack_from("<BBBBH", d, q)
        layers.append({"layer": layer, "loop": loop, "root": root, "x": x,
                       "motion": mid, "file": mid // 100, "index": mid % 100})
        q += 6
    events = []
    ne = d[q]
    q += 2
    for _ in range(ne):
        op = d[q]
        size = 6 if op <= 0x33 else 8
        events.append({"op": op, "frame": u16(d, q + 4), "raw": d[q:q + size].hex()})
        q += size
    return {"layers": layers, "events": events}


WEAPONS = ["Rebellion", "Cerberus", "Agni&Rudra", "Nevan", "Beowulf",
           "Ebony&Ivory", "Shotgun", "Artemis", "Spiral", "KalinaAnn"]
STYLES = ["Trickster", "Royalguard", "Quicksilver", "Doppelganger"]


def motion_file(f):
    """Motion file f -> (GDATA slot for Dante, meaning).

    player+0x243c is a 34-entry table of motion PACs (FUN_00280110 / FUN_0025bb00):
    0-2 = character archive chunks 2-4 (also standalone as GDATA 20-22),
    3+w = weapon w moveset, 13+w (w<5) = Swordmaster extras for melee weapon w,
    18+(w-5) = Gunslinger extras for gun w, 23-26 = Trickster..Doppelganger.
    Loaded with FUN_001aefc0(.., 1, character*0x22 + f); for Dante that is GDATA 20+f.
    """
    if f <= 2:
        what = f"base{f} (archive chunk {f + 2})"
    elif f <= 12:
        what = WEAPONS[f - 3]
    elif f <= 17:
        what = f"Swordmaster:{WEAPONS[f - 13]}"
    elif f <= 22:
        what = f"Gunslinger:{WEAPONS[f - 18 + 5]}"
    elif f <= 26:
        what = STYLES[f - 23]
    else:
        what = "other"
    return 20 + f, what


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    d = a.script.read_bytes()
    off_s, off_m = u16(d, 0), u16(d, 2)
    sbase, nbanks = table(d, off_s)
    mbase, mbanks = table(d, off_m)
    bank_starts = sorted(sbase + u16(d, sbase + b * 2) for b in range(nbanks))
    rows, scripts = [], {}
    for bank in range(nbanks):
        t = sbase + u16(d, sbase + bank * 2)
        later = [x for x in bank_starts if x > t]
        count = u16(d, t) // 2
        if later:
            count = min(count, (later[0] - t) // 2)
        for idx in range(count):
            rel = u16(d, t + idx * 2)
            if rel == 0xFFFF or t + rel >= len(d):
                continue
            p = t + rel
            ops = disasm(d, p)
            plays = []
            for ins in ops:
                if ins["op"] == 1:
                    try:
                        md = motion_def(d, mbase, ins["grp"], ins["sub"])
                    except (struct.error, IndexError):
                        md = None
                    ins["def"] = md
                    plays.append(ins)
            scripts[f"{bank}:{idx}"] = {"offset": p, "ops": ops}
            rows.append({
                "bank": bank, "index": idx, "motion_id": f"{bank << 8 | idx:#06x}",
                "offset": f"{p:#x}", "n_ops": len(ops),
                "plays": " ".join(f"{i['grp']}/{i['sub']}" for i in plays),
                "motions": " ".join(
                    ",".join(f"L{l['layer']}:{l['file']}.{l['index']}" for l in (i["def"] or {"layers": []})["layers"])
                    for i in plays),
                "files": " ".join(sorted({motion_file(l["file"])[1] for i in plays
                                          for l in (i["def"] or {"layers": []})["layers"]})),
                "loops": int(any(i["op"] == 2 for i in ops)),
            })
    a.out.mkdir(parents=True, exist_ok=True)
    with (a.out / "motion_scripts.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    (a.out / "motion_scripts.json").write_text(json.dumps(scripts, indent=1))
    (a.out / "motion_files.csv").write_text("file,gdata_slot,meaning\n" + "".join(
        f"{f},{motion_file(f)[0]},{motion_file(f)[1]}\n" for f in range(34)))
    print(f"{nbanks} script banks, {mbanks} motion-def groups, {len(rows)} scripts")


if __name__ == "__main__":
    main()
