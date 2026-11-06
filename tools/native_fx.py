#!/usr/bin/env python3
"""Export the PS2 effect records the port draws natively -> native_fx_runtime.h.

Two banks, read straight from the user's disc:
  0 static_effect.pac (GDATA 167): the red orbs. CItemOrb (FUN_002f2690) plays V45/V186-V189 for its five
    sizes and E185 when one is collected (FUN_002f3750).
  1 em025's effect pack (GDATA 130, PNST chunk 23): Cerberus's ice. Its shells play V117 (icicle), V89 and V101
    (sphere), V48 (shard); its moves play the ids listed in CERBERUS_V below (FUN_00208590 call sites).

Each V timeline is exported with every V/E child it reaches. P (VU particle) and G (generator) children are
counted but not exported: their programs aren't ported. E records are kept raw (their curves are read at run
time, as for em008 in vanguard_fx.py); the textures and MOD meshes they use are converted for the PSP.
"""
import argparse
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660
from mod_ps2 import parse_mod
from prepare_skinned_model import array_lines, c_float, decode_textures, indexed_texture, pnst_entries
from tim2 import decode_picture

ORB_V = (45, 186, 187, 188, 189)
ORB_E = (185,)
CERBERUS_V = (
    115, 166, 171, 117, 89, 101, 97, 72, 48, 237, 90, 320, 321, 293, 180, 177, 178, 179, 181, 172, 173, 174,
    126, 691, 118, 150, 151, 78, 66,
)
CERBERUS_E = (291, 251, 199, 284)
# E2 (in V72, V180): model M18 scaled 0.1 -> 60 in 20 frames, an 18 m sphere at full white. Its GS blend isn't
# read, and drawn as an ordinary blend it fills the screen with white slabs, so it's left out.
SKIP_E = {2}
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


def closure(recs, v_ids, e_ids):
    vs, es, skipped = set(), set(e_ids), {"P": 0, "G": 0}
    todo = list(v_ids)
    while todo:
        v = todo.pop()
        if v in vs:
            continue
        vs.add(v)
        data = recs["V", v]
        for j in range(struct.unpack_from("<H", data, 0)[0]):
            kind, child = data[4 + j * 44], struct.unpack_from("<H", data, 6 + j * 44)[0]
            if kind == 3:
                todo.append(child)
            elif kind == 1:
                if child not in SKIP_E:
                    es.add(child)
            else:
                skipped["P" if kind == 0 else "G"] += 1
    return sorted(vs), sorted(es), skipped


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


def write_bank(lines, bank, recs, v_ids, e_ids):
    vs, es, skipped = closure(recs, v_ids, e_ids)
    textures, models = {}, {}
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
    for kind, ids in (("V", vs), ("E", es)):
        for i in ids:
            lines += [f"static const unsigned char {p}_{kind}{i}[] __attribute__((aligned(4))) = {{",
                      array_lines(recs[kind, i], str, 32), "};"]
        lines.append(f"static const NfxRecord {p}_{kind}[] = {{" + ",".join(f"{{{i},{p}_{kind}{i}}}" for i in ids) + "};")
    lines.append(f"static const NfxBank {p} = {{{p}_V,{len(vs)},{p}_E,{len(es)},{p}_textures,{len(tex_ids)},"
                 f"{p}_models,{len(models)}}};")
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
             "typedef struct { const NfxRecord *v; int v_count; const NfxRecord *e; int e_count; const NfxTexture *textures; int texture_count; const NfxModel *models; int model_count; } NfxBank;"]
    summary = []
    summary.append(write_bank(lines, 0, records(read(167)), ORB_V, ORB_E))
    summary.append(write_bank(lines, 1, records(pnst_entries(read(130))[23]), CERBERUS_V, CERBERUS_E))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "native_fx_runtime.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    for bank, (v, e, t, m, skipped) in enumerate(summary):
        print(f"bank {bank}: {v} V, {e} E, {t} textures, {m} models; not ported: {skipped['P']} P, {skipped['G']} G children")


if __name__ == "__main__":
    main()
