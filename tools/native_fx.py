#!/usr/bin/env python3
"""Export the PS2 effect records the port draws natively -> native_fx_runtime.h.

Two banks, read straight from the user's disc:
  0 static_effect.pac (GDATA 167): the red orbs. CItemOrb (FUN_002f2690) plays V45/V186-V189 for its five
    sizes and E185 when one is collected (FUN_002f3750).
  4 em026's effect pack (GDATA 131, PNST chunk 18): Agni & Rudra's charged blades, V588 / V589 (FUN_003ac280).
    (Bank 3 is a cutscene's own, loaded by demo.c.)
  5 em037's effect pack (GDATA 140, PAC entry 10): the Jester's cross and bomb (JESTER_V).
  1 em025's effect pack (GDATA 130, PNST chunk 23): Cerberus's ice. Its shells play V117 (icicle), V89 and V101
    (sphere), V48 (shard); its moves play the ids listed in CERBERUS_V below (FUN_00208590 call sites).

Each V timeline is exported with every V/E/G child it reaches. G records (generators: emitters, FUN_001b1cd0) go in
the V table with their id | 0x8000 (G_FLAG), so a bank's layout doesn't change; nfx.c runs them. P records (particle
programs, CPtcl*: FUN_001b3bf0 / FUN_001b45e0; pfx.c runs them) go in the P table raw, with the T sheets their sprites use
and the A records (atlas frames, FUN_001ec360) that cut those sheets. E records are kept raw (their curves are read at run
time, as for em008 in vanguard_fx.py); the textures and MOD meshes they use are converted for the PSP.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from mod_ps2 import parse_mod
from prepare_skinned_model import array_lines, c_float, decode_textures, indexed_texture, pac_list, pnst_entries
from tim2 import decode_picture

ORB_V = (45, 186, 187, 188, 189)
ORB_E = (185, 235)   # E235: Wall Hike's kick off the wall (FUN_002fd0c0, same bank)
CERBERUS_V = (
    115, 166, 171, 117, 89, 101, 97, 72, 48, 237, 90, 320, 321, 293, 180, 177, 178, 179, 181, 172, 173, 174,
    126, 691, 118, 150, 151, 78, 66,
)
CERBERUS_E = (291, 251, 199, 284)
# The punch's ice mist, on the paw (FUN_002a0510): P58 (0x3a) at events 2 and 4, P59 (0x3b) at 3 and 5.
CERBERUS_P = (58, 59)
# E2 (in V72, V180): model M18 scaled 0.1 -> 60 in 20 frames, an 18 m sphere at full white. Its GS blend isn't
# read, and drawn as an ordinary blend it fills the screen with white slabs, so it's left out.
SKIP_E = {2}
# Agni & Rudra's charged blades (FUN_003ac280): V588 on Agni's sword, V589 on Rudra's. The slam (FUN_003a8200 phase
# 0x1f): V785 at Agni's blade, V786 at Rudra's (E0 and the P24 / P44 particles).
AGNI_RUDRA_V = (588, 589, 785, 786, 0x7b, 0x1d5 | 0x8000)   # + the long attacks' V0x7b and G0x1d5 (FUN_003ac770 / 790)
# Started by their code: P44 as one lands (FUN_003ac750) and P44 / P15 as one falls dead (FUN_003a6820).
AGNI_RUDRA_P = (15, 44)
# The Jester's (em037 [10]): the cross (0054d610: V0x3f9 once, V0x3f8 looping; 0054dc70: V0x3fa at its end) and the
# bomb (0054e710: V0x417 in flight; 0054edc0: V0x41c as it goes off).
JESTER_V = (0x3f8, 0x3f9, 0x3fa, 0x417, 0x41c,
            0x3cb, 0x3cc, 0x3f3)   # his second set's (enemy.c jester_fx): 0x3cb / 0x3cc trails, 0x3f3 rising
# Effect models whose second chunk is a 16-byte reference rather than their own textures. The reference's
# meaning isn't decoded. A model takes a twin's textures when one has the same vertices, else the ice texture
# of the icicle model M54 (the ice pieces and frozen floor are the same material).
SHARED_TEXTURE = 54


def records(pack):
    names, body = pnst_entries(pack)
    chunks = pnst_entries(body)
    out, index = {}, 0
    for line in names.decode("ascii", errors="replace").splitlines():
        fields = line.split()
        if len(fields) != 2 or fields[0] not in "GVEPTAMC":
            continue
        kind, number = fields[0], int(fields[1])
        out[kind, number] = (chunks[index], chunks[index + 1]) if kind == "M" else chunks[index]
        index += 2 if kind == "M" else 1
    if index != len(chunks):
        raise ValueError("Effect ID list does not account for every PNST child")
    return out


G_FLAG = 0x8000


def record(recs, kind, number):
    """A record by its table id: in the V table, an id with G_FLAG is a G record."""
    if kind == "V" and number & G_FLAG:
        return recs["G", number & ~G_FLAG]
    return recs[kind, number]


def has(recs, number):
    return ("G", number & ~G_FLAG) in recs if number & G_FLAG else ("V", number) in recs


def closure(recs, v_ids, e_ids):
    """(V and G ids (G with G_FLAG), E ids, skipped counts) reached from the V (or flagged G) ids and E ids."""
    vs, es, skipped = set(), set(e_ids), {"P": 0, "G": 0}
    todo = list(v_ids)

    def reach(kind, child):
        if kind == 3:
            todo.append(child)
        elif kind == 2:
            todo.append(child | G_FLAG)
        elif kind == 1:
            if child not in SKIP_E:
                es.add(child)
        else:
            skipped["P"] += 1

    while todo:
        v = todo.pop()
        if v in vs:
            continue
        if not has(recs, v):   # not in this pack
            skipped["G"] += bool(v & G_FLAG)
            continue
        vs.add(v)
        data = record(recs, "V", v)
        if v & G_FLAG:
            # A G emits one child: its kind at +0x24 (0 P, 1 E, 2 G, 3 V), its id at +0x26.
            reach(data[0x24], struct.unpack_from("<H", data, 0x26)[0])
            continue
        for j in range(struct.unpack_from("<H", data, 0)[0]):
            reach(data[4 + j * 44], struct.unpack_from("<H", data, 6 + j * 44)[0])
    return sorted(vs), sorted(es), skipped


def p_param(rec):
    """A P record's parameter block and sub-records (FUN_001b3bf0): offsets from +0x10 at +0x10; the block at the
    first, its u16 sub-record count at block +0xc0, the sub-records at the third onwards."""
    offs = struct.unpack_from("<26I", rec, 0x10)
    at = 0x10 + offs[0]
    count = struct.unpack_from("<H", rec, at + 0xc0)[0]
    return at, [0x10 + offs[2 + i] for i in range(count)]


P_SPRITE = 3   # CPtclSprt00 (FUN_001bbe10): its T at block +0xd0, its A (atlas) at +0xf6


def particles(recs, vs, p_ids=()):
    """(P ids, T ids, A ids) the V / G ids (G with G_FLAG) reach, plus p_ids: P records and what their sprites draw."""
    ps = set(p for p in p_ids if ("P", p) in recs)
    for v in vs:
        data = record(recs, "V", v)
        kids = [(data[0x24], struct.unpack_from("<H", data, 0x26)[0])] if v & G_FLAG else             [(data[4 + j * 44], struct.unpack_from("<H", data, 6 + j * 44)[0])
             for j in range(struct.unpack_from("<H", data, 0)[0])]
        ps |= {c for k, c in kids if k == 0 and ("P", c) in recs}
    ts, As = set(), set()
    for p in ps:
        rec = recs["P", p]
        at, _ = p_param(rec)
        if rec[at + 1] == P_SPRITE:
            t, a = struct.unpack_from("<h", rec, at + 0xd0)[0], struct.unpack_from("<h", rec, at + 0xf6)[0]
            if ("T", t) in recs and ("A", a) in recs:
                ts.add(t)
                As.add(a)
    return sorted(ps), sorted(ts), sorted(As)


def picture(raw):
    _, rgba, pic = decode_picture(raw, 0x80 if raw[5] == 1 else 0x10)
    return {"width": pic["width"], "height": pic["height"], "rgba": rgba}


def mesh(mod):
    objects, _ = parse_mod(mod, True)
    verts, idx = [], []
    for obj in objects:
        base = len(verts)
        for p, n, uv in zip(obj["positions"], obj["normals"], obj["uvs"]):
            verts.append((uv[0], uv[1], n[0], n[1], n[2], p[0], p[1], p[2]))
        for f in obj["faces"]:
            idx += [base + f[0], base + f[1], base + f[2]]
    return verts, idx


def write_bank(lines, bank, recs, v_ids, e_ids, p_ids=()):
    vs, es, skipped = closure(recs, v_ids, e_ids)
    ps, p_textures, atlases = particles(recs, vs, p_ids)
    textures, models = {}, {}
    for t in p_textures:
        textures.setdefault(t, picture(recs["T", t]))
    for e in es:
        rec = recs["E", e]
        kind, tex = rec[1], struct.unpack_from("<H", rec, 4)[0]
        if kind == 5:
            models.setdefault(struct.unpack_from("<H", rec, 0x28)[0], None)
        elif tex != 0xFFFF:
            textures.setdefault(tex, picture(recs["T", tex]))
    model_tex = {}
    for m in sorted(models):
        mod, tex = recs["M", m]
        if len(tex) > 16:
            model_tex[m] = 10000 + m
            textures[10000 + m] = decode_textures(tex)[0]
    # A model without textures of its own takes those of a textured twin with the same vertices (M43 is M15).
    for m in models:
        if m not in model_tex:
            positions = [v[5:] for v in mesh(recs["M", m][0])[0]]
            for other, pair in recs.items():
                if other[0] != "M":
                    continue
                mod, tex = pair
                if len(tex) > 16 and [v[5:] for v in mesh(mod)[0]] == positions:
                    model_tex[m] = 10000 + other[1]
                    textures.setdefault(10000 + other[1], decode_textures(tex)[0])
                    break
    for m in models:
        if m not in model_tex:
            if SHARED_TEXTURE not in model_tex and ("M", SHARED_TEXTURE) in recs:
                textures[10000 + SHARED_TEXTURE] = decode_textures(recs["M", SHARED_TEXTURE][1])[0]
            model_tex[m] = 10000 + SHARED_TEXTURE if 10000 + SHARED_TEXTURE in textures else -1
    p = f"nfx{bank}"
    tex_ids = sorted(textures)
    for t in tex_ids:
        pixels, palette = indexed_texture(textures[t])
        lines += [f"static const unsigned char {p}_t{t}_pixels[] __attribute__((aligned(16))) = {{",
                  array_lines(pixels, str, 32), "};",
                  f"static const unsigned int {p}_t{t}_palette[256] __attribute__((aligned(16))) = {{",
                  array_lines(palette, lambda v: f"0x{v:08x}u", 8), "};"]
    lines.append(f"static const NfxTexture {p}_textures[] = {{" + ",".join(
        f"{{{t},{textures[t]['width']},{textures[t]['height']},{p}_t{t}_pixels,{p}_t{t}_palette}}" for t in tex_ids) + "};")
    for m in sorted(models):
        verts, idx = mesh(recs["M", m][0])
        lines += [f"static const NfxVertex {p}_m{m}_vertices[] __attribute__((aligned(16))) = {{",
                  array_lines(verts, lambda v: "{" + ",".join(c_float(x) for x in v) + "}", 3), "};",
                  f"static const unsigned short {p}_m{m}_indices[] __attribute__((aligned(16))) = {{",
                  array_lines(idx, str, 24), "};"]
    lines.append(f"static const NfxModel {p}_models[] = {{" + ",".join(
        f"{{{m},{len(mesh(recs['M', m][0])[0])},{len(mesh(recs['M', m][0])[1])},{p}_m{m}_vertices,{p}_m{m}_indices,"
        f"{tex_ids.index(model_tex[m]) if model_tex[m] in tex_ids else -1}}}" for m in sorted(models)) + "};")
    for kind, ids in (("V", vs), ("E", es), ("P", ps), ("A", atlases)):
        for i in ids:
            lines += [f"static const unsigned char {p}_{kind}{i}[] __attribute__((aligned(4))) = {{",
                      array_lines(record(recs, kind, i), str, 32), "};"]
        lines.append(f"static const NfxRecord {p}_{kind}[] = {{" + (",".join(f"{{{i},{p}_{kind}{i}}}" for i in ids) or "{0,0}") + "};")
    lines.append(f"static const NfxBank {p} = {{{p}_V,{len(vs)},{p}_E,{len(es)},{p}_textures,{len(tex_ids)},"
                 f"{p}_models,{len(models)},{p}_P,{len(ps)},{p}_A,{len(atlases)}}};")
    skipped["P"] = len(ps)
    return len(vs), len(es), len(tex_ids), len(models), skipped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    iso = Iso9660(args.iso)
    afs = AfsArchive(iso, "GDATA.AFS")
    read = lambda slot: iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"])
    lines = ["/* Generated by tools/native_fx.py from the PS2 disc. */", "#pragma once",
             "typedef struct { unsigned short id; const unsigned char *data; } NfxRecord;",
             "typedef struct { int id, width, height; const unsigned char *pixels; const unsigned int *palette; } NfxTexture;",
             "typedef struct { float u, v, nx, ny, nz, x, y, z; } NfxVertex;",
             "typedef struct { int id, vertex_count, index_count; const NfxVertex *vertices; const unsigned short *indices; int texture; } NfxModel;",
             "typedef struct { const NfxRecord *v; int v_count; const NfxRecord *e; int e_count; const NfxTexture *textures; int texture_count; const NfxModel *models; int model_count; const NfxRecord *p; int p_count; const NfxRecord *a; int a_count; } NfxBank;"]
    summary = []
    # P44 (dust): the Arachne's landings start it from the shared pack (FUN_003fb580 and its kin).
    summary.append(write_bank(lines, 0, records(read(167)), ORB_V, ORB_E, (44,)))
    summary.append(write_bank(lines, 1, records(pnst_entries(read(130))[23]), CERBERUS_V, CERBERUS_E, CERBERUS_P))
    leviathan = records(read(690))
    summary.append(write_bank(lines, 2, leviathan,
                              sorted(number for kind, number in leviathan if kind == "V"), ()))
    summary.append(write_bank(lines, 4, records(pnst_entries(read(131))[18]), AGNI_RUDRA_V, (), AGNI_RUDRA_P))
    summary.append(write_bank(lines, 5, records(pac_list(read(140))[10]), JESTER_V, ()))
    # 6 em008's pack (GDATA 120, PNST chunk 24): the Hell Vanguard's P312 only (its V / E are vanguard_fx.py's).
    summary.append(write_bank(lines, 6, records(pnst_entries(read(120))[24]), (), (), (312,)))
    # 7 em028's effect pack (GDATA 133, PAC entry 9): Nevan's. Action 22's V0x199 (FUN_003b6c90) and her bats' P
    # (FUN_003c9870: P203 / P235 / P251 at random on each).
    summary.append(write_bank(lines, 7, records(pac_list(read(133))[9]), (409, 0x110, 0x1a8 | G_FLAG, 0x97 | G_FLAG), (),
                              (203, 235, 251)))
    # 8 em034's pack (GDATA 138, PAC entry 28): Lady's rocket (CEm034Shl02: V0x179 in flight, V0x21f as it bursts).
    summary.append(write_bank(lines, 8, records(pac_list(read(138))[28]), (0x179, 0x21f), ()))
    # 9 em010's pack (GDATA 121, PNST chunk 21): the Arachne's web (CEm010Shl00).
    summary.append(write_bank(lines, 9, records(pnst_entries(read(121))[21]), (0x29e, 0x2a3, 0x308, 0x309, 0x2a2, 0x304), ()))
    # 10 em011's pack (GDATA 122, PNST chunk 8): the Fallen's spear (CEm011Shl00: V0x367, V0x368).
    summary.append(write_bank(lines, 10, records(pnst_entries(read(122))[8]), (0x367, 0x368), ()))
    # 11 room 405's effect pack (GDATA 758): Mission 19's mirrors (CStageSetMirror: V0x1ea shattering; their E0x2e0 glow is a model
    # whose texture reference isn't decoded, so mission19.c keeps its stand-in).
    summary.append(write_bank(lines, 11, records(read(758)), (0x1ea,), ()))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "native_fx_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    for bank, (v, e, t, m, skipped) in zip((0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11), summary):
        print(f"bank {bank}: {v} V, {e} E, {skipped['P']} P, {t} textures, {m} models; missing: {skipped['G']} G children")


if __name__ == "__main__":
    main()
