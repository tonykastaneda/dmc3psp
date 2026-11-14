#!/usr/bin/env python3
"""Generate the skinned Dante runtime header for the PSP test room.

Same arrays as prepare_model_runtime.py (positions, normals, UVs, indices,
objects, textures) plus the 24-bone skeleton and per-vertex bone indices and
weights. Triangles honour the GS ADC strip restarts (see mod_skeleton.py).
"""

import argparse
import hashlib
import math
import struct
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, write_json
from dmc3_pac import PacReader
from mod_ps2 import parse_mod
from mod_skeleton import parse_skeleton, parse_skin, strip_faces
from mot_ps2 import parse_mot
from tim2 import decode_picture
from decimate import slim_mesh
import gen_enemy_ai

SLOT = 0
MOTION_CHUNKS = (2, 3, 4)  # nested PACs of MOT files in Dante's archive
COAT_CHUNK = 12            # pl000_02: coat-tail MOD with its own 33-bone rig
CLOTH_CHUNK = 13           # pl000_02.clt: cloth parameters for the coat rig
REBELLION_SLOT = 97        # PNST: [0] TIM2 set, [1] MOD (rigid, bone 1), [2] effects, [3] SHW, [4] MOT
REBELLION_MOTION_SLOT = 23 # PAC of 21 MOTs: Dante's Rebellion moveset (first of slots 23-27)
# Every other moveset pack, exported as sNN- (docs/MOVESET_RE.md): 24-32 weapons,
# 33-37 Swordmaster, 38-42 Gunslinger, 43-46 styles, 47-53 not identified yet.
STUDY_MOTION_SLOTS = tuple(range(24, 54))
ENEMY_SLOT = 117           # PNST: Seven Hells pack ([0] TIM2 set, [1] em000 body, [26] scythe, [35] motions)
ENEMY_BODY = 1             # em000 -- the training dummy (the Hell Pride, class CEm000; confirm with the user)
ENEMY_WEAPON = 26          # first scythe model
ENEMY_MOTIONS = 35         # PAC of 71 shared Seven Hells motions, exported as hl-NN
ENIGMA_SLOT = 124          # em013.pac, the Enigma: [0] textures, [1] body, [2] bow, [4] and [5] motions


def pnst_entries(data: bytes):
    if data[:4] != b"PNST":
        raise ValueError("Not a PNST container")
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = list(struct.unpack_from(f"<{count}I", data, 8)) + [len(data)]
    return [data[offsets[i]:offsets[i + 1]] for i in range(count)]


def decode_textures(texture_data: bytes):
    count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from("<" + "I" * count, texture_data, 4)
    textures, offset = [], 0x800
    for sector_count in sectors:
        image = texture_data[offset:offset + sector_count * 0x800]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append({"width": picture["width"], "height": picture["height"], "rgba": rgba})
        offset += sector_count * 0x800
    return textures


def mots_in_pac(pac: bytes, prefix: str):
    count = struct.unpack_from("<I", pac, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", pac, 8)
    present = sorted(o for o in offsets if o)
    for slot, start in enumerate(offsets):
        if not start:
            continue
        end = min([o for o in present if o > start] + [len(pac)])
        raw = pac[start:end]
        parsed = parse_mot(raw)
        yield {"name": f"{prefix}{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed}


def build_mesh(model_data: bytes, bone_count: int):
    """Every object of a MOD as flat runtime arrays, one draw entry per
    sub-mesh (flag low byte = sub-mesh count; e.g. Dante's object 8 and two
    coat pieces have two)."""
    objects, _ = parse_mod(model_data, split_submeshes=True)
    mesh = {"positions": [], "normals": [], "uvs": [], "bones": [], "weights": [],
            "indices": [], "objects": [], "old_triangles": 0, "skipped": []}
    for item in objects:
        vc = item["vertex_count"]
        b, w, adc = parse_skin(model_data, item["bone_offset"], item["weight_offset"], vc, bone_count)
        base, start = len(mesh["positions"]), len(mesh["indices"])
        mesh["positions"].extend(item["positions"])
        mesh["normals"].extend(item["normals"])
        mesh["uvs"].extend(item["uvs"])
        mesh["bones"].extend(b)
        mesh["weights"].extend(tuple(round(x * 31) for x in ww) for ww in w)
        for face in strip_faces(adc):
            mesh["indices"].extend(base + v for v in face)
        mesh["old_triangles"] += len(item["faces"])
        mesh["objects"].append({"texture": item["header_count"], "index_start": start,
                                "index_count": len(mesh["indices"]) - start})
    if len(mesh["positions"]) > 65535:
        raise ValueError("Too many vertices for 16-bit indices")
    return mesh


def parse_cloth(text: str):
    """Key/value lines of a .clt cloth script; Bone lines collected in order."""
    cloth = {"bones": []}
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0].startswith((";", "$")):
            continue
        if parts[0] == "Bone":
            cloth["bones"].append(int(parts[1]))
        elif parts[0] != "End":
            values = []
            for value in parts[1:]:
                try:
                    values.append(float(value))
                except ValueError:
                    pass
            cloth[parts[0]] = values
    return cloth


def c_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("Non-finite runtime value")
    text = f"{value:.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def array_lines(values, formatter, per_line=6):
    return "\n".join(
        "    " + ", ".join(formatter(v) for v in values[i:i + per_line]) + ","
        for i in range(0, len(values), per_line)
    )


def vec(values):
    return "{" + ",".join(c_float(v) for v in values) + "}"


def write_coat(output: Path, coat_data: bytes, cloth_data: bytes) -> None:
    """Coat tails: own MOD + 33-bone rig + cloth parameters -> coat_runtime.h."""
    coat_skel = parse_skeleton(coat_data)
    coat = build_mesh(coat_data, coat_skel["count"])
    cloth = parse_cloth(cloth_data.replace(b"\0", b"").decode("ascii", "replace"))
    free = set(cloth["bones"])
    count = coat_skel["count"]
    strands = []
    for anchor in (b for b in range(count) if coat_skel["parents"][b] == 0):
        chain, current = [anchor], anchor
        while True:
            children = [b for b in range(count) if coat_skel["parents"][b] == current]
            if len(children) != 1:
                break
            current = children[0]
            chain.append(current)
        if not all(b in free for b in chain[1:]):
            raise ValueError(f"Coat strand {chain} has bones the cloth script does not drive")
        strands.append(chain)
    width = max(len(chain) for chain in strands)
    tuple3 = lambda t: "{" + ",".join(str(x) for x in t) + "}"
    lines = [
        "#ifndef DMC3_COAT_RUNTIME_H",
        "#define DMC3_COAT_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: coat tails (pl000_02). */",
        f"#define DMC3_COAT_VERTEX_COUNT {len(coat['positions'])}",
        f"#define DMC3_COAT_INDEX_COUNT {len(coat['indices'])}",
        f"#define DMC3_COAT_OBJECT_COUNT {len(coat['objects'])}",
        f"#define DMC3_COAT_BONE_COUNT {count}",
        f"#define DMC3_COAT_STRAND_COUNT {len(strands)}",
        f"#define DMC3_COAT_STRAND_MAX {width}",
        "/* Cloth script values (per-frame units at 60 fps, as authored). */",
        f"#define DMC3_COAT_GRAVITY {c_float(cloth.get('Gravity', [0, -0.01, 0])[1])}",
        f"#define DMC3_COAT_SPRING {c_float(cloth.get('SpringForce', [0.02])[0])}",
        f"#define DMC3_COAT_MAX_SPEED {c_float(cloth.get('MaxSpeed', [50.0])[0])}",
        f"#define DMC3_COAT_STIFFNESS {c_float(cloth.get('Stiffness', [0.25])[0])}",
        "static const signed char dmc3_coat_bone_parent[] = {",
        array_lines(coat_skel["parents"], str, 33), "};",
        "static const Dmc3Vec3 dmc3_coat_bone_rest[] = {",
        array_lines(coat_skel["world"], vec, 4), "};",
        "/* Bone chains from each strand's anchor (child of the root) to its tip. */",
        "static const signed char dmc3_coat_strands[][DMC3_COAT_STRAND_MAX] = {",
        array_lines(strands, lambda chain: tuple3(chain + [-1] * (width - len(chain))), 1), "};",
        "static const Dmc3Vec3 dmc3_coat_positions[] = {",
        array_lines(coat["positions"], vec, 4), "};",
        "static const Dmc3Vec3 dmc3_coat_normals[] = {",
        array_lines(coat["normals"], vec, 4), "};",
        "static const Dmc3Uv dmc3_coat_uvs[] = {",
        array_lines(coat["uvs"], vec, 6), "};",
        "static const unsigned char dmc3_coat_bones[][3] = {",
        array_lines(coat["bones"], tuple3, 8), "};",
        "static const unsigned char dmc3_coat_weights[][3] = {",
        array_lines(coat["weights"], tuple3, 8), "};",
        "static const unsigned short dmc3_coat_indices[] = {",
        array_lines(coat["indices"], str, 12), "};",
        "static const Dmc3ModelObject dmc3_coat_objects[] = {",
        array_lines(coat["objects"], lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};",
        "", "#endif", "",
    ]
    (output / "coat_runtime.h").write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {output / 'coat_runtime.h'}: {len(coat['positions'])} vertices, "
          f"{len(coat['indices']) // 3} triangles, {count} bones, {len(strands)} strands "
          f"{strands}, skipped objects {coat['skipped']}, cloth {cloth}")


def write_weapon(output: Path, name: str, mod_data: bytes, texture_data: bytes, strict: bool = True) -> None:
    """A rigid weapon MOD (every vertex on one bone at the origin) -> <name>_runtime.h.
    strict=False keeps a weapon with moving parts in its rest pose."""
    skel = parse_skeleton(mod_data)
    mesh = slim_mesh(build_mesh(mod_data, skel["count"]))
    for bones, weights in zip(mesh["bones"], mesh["weights"]):
        used = {b for b, w in zip(bones, weights) if w}
        if strict and any(skel["world"][b] != (0.0, 0.0, 0.0) for b in used):
            raise ValueError(f"{name}: vertex bound to a bone away from the origin; not rigid")
    textures = [psp_texture(t) for t in decode_textures(texture_data)]
    up = name.upper()
    # Triangles grouped by the bone they ride on, for the weapon's own motions.
    by_bone = {}
    for t in range(0, len(mesh["indices"]), 3):
        v = mesh["indices"][t]
        k = max(range(len(mesh["weights"][v])), key=lambda i: mesh["weights"][v][i])
        by_bone.setdefault(mesh["bones"][v][k], []).extend(mesh["indices"][t:t + 3])
    flat, groups = [], []
    for bone, tris in sorted(by_bone.items()):
        groups.append(f"{{{bone},{len(flat)},{len(tris)}}}")
        flat += tris
    mesh["indices"] = flat
    lines = [
        f"#ifndef DMC3_{up}_RUNTIME_H",
        f"#define DMC3_{up}_RUNTIME_H",
        f"/* Generated by tools/prepare_skinned_model.py: {name} (rigid weapon). */",
        f"#define DMC3_{up}_VERTEX_COUNT {len(mesh['positions'])}",
        f"#define DMC3_{up}_INDEX_COUNT {len(mesh['indices'])}",
        f"#define DMC3_{up}_TEXTURE_WIDTH {textures[0]['width']}",
        f"#define DMC3_{up}_TEXTURE_HEIGHT {textures[0]['height']}",
        f"static const Dmc3Vec3 dmc3_{name}_positions[] = {{",
        array_lines(mesh["positions"], vec, 4), "};",
        f"static const Dmc3Vec3 dmc3_{name}_normals[] = {{",
        array_lines(mesh["normals"], vec, 4), "};",
        f"static const Dmc3Uv dmc3_{name}_uvs[] = {{",
        array_lines(mesh["uvs"], vec, 6), "};",
        f"static const unsigned short dmc3_{name}_indices[] = {{",
        array_lines(mesh["indices"], str, 12), "};",
        f"static const unsigned char dmc3_{name}_texture[] = {{",
        array_lines(list(textures[0]["rgba"]), lambda value: f"0x{value:02x}", 16), "};",
        "#ifndef DMC3_WEAPON_GROUP", "#define DMC3_WEAPON_GROUP",
        "typedef struct { int bone; unsigned int start, count; } WeaponGroup;", "#endif",
        f"#define DMC3_{up}_BONE_COUNT {skel['count']}",
        f"#define DMC3_{up}_GROUP_COUNT {len(groups)}",
        f"static const WeaponGroup dmc3_{name}_groups[] = {{{','.join(groups)}}};",
        f"static const short dmc3_{name}_parents[] = {{{','.join(map(str, skel['parents']))}}};",
        f"static const float dmc3_{name}_rest[][3] = {{{','.join('{%.4ff,%.4ff,%.4ff}' % tuple(w) for w in skel['world'])}}};",
        "", "#endif", "",
    ]
    (output / f"{name}_runtime.h").write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {output / (name + '_runtime.h')}: {len(mesh['positions'])} vertices, "
          f"{len(mesh['indices']) // 3} triangles, texture {textures[0]['width']}x{textures[0]['height']}")


def write_devils(output: Path, packs) -> None:
    """Devil Trigger bodies (pl005-009, one per melee weapon, order inferred from the names)
    -> devil_runtime.h. Same 24-bone skeleton as Dante; only the body MOD is used."""
    lines = ["#ifndef DMC3_DEVIL_RUNTIME_H", "#define DMC3_DEVIL_RUNTIME_H",
             "/* Generated by tools/prepare_skinned_model.py. */",
             "typedef struct { const Dmc3Vec3 *positions, *normals, *rest; const Dmc3Uv *uvs;",
             "    const unsigned char (*bones)[3], (*weights)[3]; const unsigned short *indices;",
             "    const Dmc3ModelObject *objects; const Dmc3Texture *textures;",
             "    int vertex_count, index_count, object_count, texture_count; } DevilModel;"]
    tuple3 = lambda t: "{" + ",".join(str(x) for x in t) + "}"
    table = []
    for i, pac in enumerate(packs):
        count = struct.unpack_from("<I", pac, 4)[0]
        offsets = struct.unpack_from(f"<{count}I", pac, 8)
        present = sorted(o for o in offsets if o) + [len(pac)]
        part = lambda k: pac[offsets[k]:present[present.index(offsets[k]) + 1]]
        body = part(1)
        skel = parse_skeleton(body)
        mesh = slim_mesh(build_mesh(body, skel["count"]), 0.8)
        textures = [psp_texture(t) for t in decode_textures(part(0))]
        n = f"devil{i}"
        lines += [f"static const Dmc3Vec3 {n}_rest[] = {{" + ",".join("{%.4ff,%.4ff,%.4ff}" % tuple(w) for w in skel["world"]) + "};",
                  f"static const Dmc3Vec3 {n}_positions[] = {{", array_lines(mesh["positions"], vec, 4), "};",
                  f"static const Dmc3Vec3 {n}_normals[] = {{", array_lines(mesh["normals"], vec, 4), "};",
                  f"static const Dmc3Uv {n}_uvs[] = {{", array_lines(mesh["uvs"], vec, 6), "};",
                  f"static const unsigned char {n}_bones[][3] = {{", array_lines(mesh["bones"], tuple3, 8), "};",
                  f"static const unsigned char {n}_weights[][3] = {{", array_lines(mesh["weights"], tuple3, 8), "};",
                  f"static const unsigned short {n}_indices[] = {{", array_lines(mesh["indices"], str, 12), "};",
                  f"static const Dmc3ModelObject {n}_objects[] = {{",
                  array_lines(mesh["objects"], lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};"]
        for k, t in enumerate(textures):
            lines += [f"static const unsigned char {n}_texture_{k}[] = {{",
                      array_lines(list(t["rgba"]), lambda value: f"0x{value:02x}", 16), "};"]
        lines.append(f"static const Dmc3Texture {n}_textures[] = {{" +
                     ",".join(f"{{{t['width']},{t['height']},{n}_texture_{k}}}" for k, t in enumerate(textures)) + "};")
        table.append(f"{{{n}_positions,{n}_normals,{n}_rest,{n}_uvs,{n}_bones,{n}_weights,{n}_indices,{n}_objects,{n}_textures,"
                     f"{len(mesh['positions'])},{len(mesh['indices'])},{len(mesh['objects'])},{len(textures)}}}")
        print(f"devil{i}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, {len(textures)} textures, bones {skel['count']}")
    lines += [f"#define DMC3_DEVIL_COUNT {len(packs)}", "static const DevilModel dmc3_devils[] = {" + ",".join(table) + "};", "", "#endif", ""]
    (output / "devil_runtime.h").write_text("\n".join(lines), encoding="ascii")


def psp_texture(texture):
    """Replaces a texture's RGBA with an 8-bit one: 1024 bytes of palette, then the pixels. Tiny ones
    grow to fit the GE's 16-byte x 8-row swizzle blocks; UVs don't care."""
    from PIL import Image
    w, h = texture["width"], texture["height"]
    if w < 16 or h < 8 or w % 16 or h % 8:
        nw, nh = max(16, (w + 15) // 16 * 16), max(8, (h + 7) // 8 * 8)
        image = Image.frombytes("RGBA", (w, h), bytes(texture["rgba"])).resize((nw, nh), Image.Resampling.NEAREST)
        texture.update(width=nw, height=nh, rgba=image.tobytes())
    pixels, words = indexed_texture(texture)
    texture["rgba"] = struct.pack("<256I", *words) + bytes(pixels)
    return texture


def indexed_texture(texture):
    """(pixels, 256 ABGR words), colour and alpha quantized together."""
    from PIL import Image
    image = Image.frombytes("RGBA", (texture["width"], texture["height"]), bytes(texture["rgba"]))
    quant = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    palette = quant.getpalette("RGBA")
    palette += [0] * (1024 - len(palette))
    words = [palette[i] | palette[i + 1] << 8 | palette[i + 2] << 16 | palette[i + 3] << 24 for i in range(0, 1024, 4)]
    return list(quant.tobytes()), words


# The enemy kinds the port draws, in the order of psp/testroom/enemy.h's EnemyKind. Seven Hells bodies are
# entries of em000.pac (GDATA 117), named by their cloth files (em00N_01.clt follows each body); their scythes
# per the class set-ups: Pride [26], Gluttony [28], Sloth [27] (the Lust, Wrath and Greed aren't read yet: the Pride's). The Enigma is em013.pac (GDATA 124):
# [0] its textures, [1] body, [2] bow.
ARACHNE_SLOT = 121
NEVAN_SLOT = 133
BEOWULF_SLOT = 135
ENEMY_KINDS = (
    ("pride", 117, 1, 26),
    ("gluttony", 117, 5, 28),
    ("lust", 117, 8, 26),
    ("sloth", 117, 13, 27),
    ("wrath", 117, 18, 26),
    ("greed", 117, 19, 26),
    ("enigma", 124, 1, 2),
    # The Blood-goyle, em014.pac (GDATA 125, a PNST): [0] textures, [1] its body, tagged "EFM " but laid out
    # as a MOD, [8] its 28 motions. No weapon.
    ("goyle", 125, 1, None),
    # The Jester (SE), em037.pac (GDATA 140, a PAC): [0] textures, [1] body (36 bones), [7] his hat; motions in
    # [3], [4], [5], [6], [19], [28], [29]. Found through the pack table at 0x58e740 (pack 27: slot 0x8c).
    ("jester", 140, 1, 7),
    # Agni & Rudra, em026.pac (GDATA 131, pack 18 in the table at 0x58e740): [0] textures, [1] the body both
    # brothers share, [2] and [3] their swords; motions in [11] (a PAC of 100 slots, 54 used).
    ("agni", 131, 1, 2),
    ("rudra", 131, 1, 3),
    ("envy", 119, 1, 26),
    # The Arachne (class 10, EST 28), em010.pac: GDATA 121, pack 5 through the pack table at 0x58e740. [0] textures,
    # [1] its body (24 bones; [2] is a second body of the same size), motions in PACs [7]-[15]. No weapon.
    ("arachne", ARACHNE_SLOT, 1, None),
    # Nevan (class 31, EST 49), em028.pac: GDATA 133, pack 19 in the table at 0x58e740. [0] textures, [1] her body
    # (23 bones), motions in PACs [2] and [3]. Her other MODs ([4]-[6]) aren't drawn yet.
    ("nevan", NEVAN_SLOT, 1, None),
    # The Damned Pawn (CEm017, EST 35), em017.pac (GDATA 127, pack 12): [0] textures, [2] the Pawn (8 bones), its one
    # motion in [3]. The other chessmen are [7], [12] and [17].
    ("pawn", 127, 2, None),
    ("dullahan", 123, 1, None),
    # Beowulf (class 33, EST 51), em030.pac: GDATA 135, pack 21 in the table at 0x58e740. [0] textures, [1] his body
    # (36 bones), motions in the PAC [2] (0x70-byte MOT headers).
    ("beowulf", BEOWULF_SLOT, 1, None),
)
AR_SLOT = 131
JESTER_SLOT = 140
PAWN_SLOT = 127
JESTER_MOTION_CHUNKS = (3, 4, 5, 6, 19, 28, 29)
GOYLE_SLOT = 125
# The Arachne's motions the port plays, "anCNN" (chunk C, slot NN). Which is which is read from their root paths
# [inferred]: 7-10 still (idle), 7-01 forward 1120 in 63 frames (run), 10-11 lunge 274, 10-08 leap 704 (pounce),
# 8-01 short (flinch), 8-00 back 210 (knocked back), 8-15 330 frames (death).
ARACHNE_MOTIONS = ((7, 10), (7, 1), (10, 11), (10, 8), (8, 1), (8, 0), (8, 15))
# Nevan's, "nvCNN", read the same way [inferred]: 2-00 still (hover), 2-01 forward 128 (glide), 2-14 163 frames in
# place (blade), 2-40 236 in place (bats), 2-13 the shortest (flinch), 2-45 back 260 (knocked back), 2-49 (death).
NEVAN_MOTIONS = ((2, 0), (2, 1), (2, 14), (2, 40), (2, 13), (2, 45), (2, 49))
# Beowulf's, "bwNN" (PAC [2] slot NN), read the same way [inferred]: 00 still (idle), 01 forward 1257 in 149 frames
# (walk), 07 forward 1673 (charge on all fours), 30/31/32 forward 290/341/206 (the three punches), 39 197 frames in
# place (stomp and pillar), 36 forward 1354 rising 286 (leaping rush), 37 276 in place (the feather laser's charge),
# 38 127 in place (flinch), 35 back 209 (knocked back), 16 back 277 over 310 (death).
BEOWULF_MOTIONS = (0, 1, 7, 30, 31, 32, 39, 36, 37, 38, 35, 16)
GOYLE_RATIO = 0.4   # of its 1,226 distinct triangles: up to a dozen are on screen once they split


def layered_mesh(body: bytes, skel, ratio):
    """A body drawn in layers (the Blood-goyle's flesh under its scrolling blood): each surface once, the
    first object's copy, then cut across UV seams (tools/decimate_rigid.py, keeping the skinning)."""
    from decimate import weld
    from decimate_rigid import decimate_rigid
    mesh = build_mesh(body, skel["count"])
    P, I = mesh["positions"], mesh["indices"]
    key = lambda v: (round(P[v][0], 2), round(P[v][1], 2), round(P[v][2], 2))
    seen, keep, groups = set(), [], []
    for k, o in enumerate(mesh["objects"]):
        for t in range(o["index_start"], o["index_start"] + o["index_count"], 3):
            s3 = frozenset(key(v) for v in I[t:t + 3])
            if s3 in seen:
                continue
            seen.add(s3)
            keep += I[t:t + 3]
            groups.append(k)
    used, idx = weld(P, mesh["normals"], mesh["uvs"], mesh["bones"], mesh["weights"], keep)
    pos, nor, uvs, idx, groups, skins = decimate_rigid(
        [P[u] for u in used], [mesh["normals"][u] for u in used], [mesh["uvs"][u] for u in used], idx, ratio, groups,
        [(mesh["bones"][u], mesh["weights"][u]) for u in used])
    objects, start = [], 0
    for k, o in enumerate(mesh["objects"]):
        n = groups.count(k) * 3
        if n:
            objects.append({**o, "index_start": start, "index_count": n})
        start += n
    return {**mesh, "positions": pos, "normals": nor, "uvs": uvs, "indices": idx, "objects": objects,
            "bones": [b for b, w in skins], "weights": [w for b, w in skins]}


def enemy_mesh(body: bytes, ratio=None):
    skel = parse_skeleton(body)
    if ratio is not None:
        mesh = layered_mesh(body, skel, ratio)
    else:
        mesh = slim_mesh(build_mesh(body, skel["count"]), 0.7)   # disposable and many on screen
    # One draw per texture: on the PSP each dummy was 22 draws of 2 textures.
    order, merged = sorted(mesh["objects"], key=lambda o: o["texture"]), []
    indices = []
    for o in order:
        if not merged or merged[-1]["texture"] != o["texture"]:
            merged.append({**o, "index_start": len(indices), "index_count": 0})
        indices += mesh["indices"][o["index_start"]:o["index_start"] + o["index_count"]]
        merged[-1]["index_count"] += o["index_count"]
    mesh["indices"], mesh["objects"] = indices, merged
    return skel, mesh


def write_enemy(output: Path, packs: dict) -> None:
    """Every kind in ENEMY_KINDS: skinned body + rigid weapon, with one 8-bit texture set per pack
    -> enemy_runtime.h. packs maps a GDATA slot to its entries."""
    tuple3 = lambda t: "{" + ",".join(str(x) for x in t) + "}"
    lines = [
        "#ifndef DMC3_ENEMY_RUNTIME_H",
        "#define DMC3_ENEMY_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: the enemy kinds (bodies, weapons, texture sets). */",
        "typedef struct { const Dmc3Texture *textures; const unsigned int *const *palettes; int count; } EnemyTextureSet;",
        "typedef struct {",
        "    const char *name;",
        "    const signed char *bone_parent; const Dmc3Vec3 *bone_local, *bone_rest;",
        "    const Dmc3Vec3 *positions, *normals; const Dmc3Uv *uvs;",
        "    const unsigned char (*bones)[3], (*weights)[3];",
        "    const unsigned short *indices; const Dmc3ModelObject *objects;",
        "    const Dmc3Vec3 *weapon_positions, *weapon_normals; const Dmc3Uv *weapon_uvs; const unsigned short *weapon_indices;",
        "    int vertex_count, index_count, object_count, bone_count;",
        "    int weapon_vertex_count, weapon_index_count, weapon_texture;",
        "    int texture_set;",
        "    float height;",
        "} EnemyModel;",
    ]
    sets = {}
    for slot in dict.fromkeys(k[1] for k in ENEMY_KINDS):
        texture_entry = packs[slot][0]
        textures = decode_textures(texture_entry)
        n = f"ets{len(sets)}"
        for i, tx in enumerate(textures):
            pixels, palette = indexed_texture(tx)
            lines += [f"static const unsigned char {n}_texture_{i}[] = {{",
                      array_lines(pixels, lambda value: f"0x{value:02x}", 16), "};",
                      f"static const unsigned int {n}_palette_{i}[256] __attribute__((aligned(16))) = {{",
                      array_lines(palette, lambda value: f"0x{value:08x}", 8), "};"]
        lines.append(f"static const Dmc3Texture {n}_textures[] = {{" +
                     ",".join(f"{{{tx['width']},{tx['height']},{n}_texture_{i}}}" for i, tx in enumerate(textures)) + "};")
        lines.append(f"static const unsigned int *const {n}_palettes[] = {{" +
                     ",".join(f"{n}_palette_{i}" for i in range(len(textures))) + "};")
        sets[slot] = (len(sets), n, len(textures))
    table = []
    max_vertices = max_bones = 0
    for kind, (name, slot, body_index, weapon_index) in enumerate(ENEMY_KINDS):
        body = packs[slot][body_index]
        if body[:4] == b"EFM ":
            body = b"MOD " + body[4:]
        skel, mesh = enemy_mesh(body, GOYLE_RATIO if slot == GOYLE_SLOT else 0.45 if slot in (JESTER_SLOT, AR_SLOT, BEOWULF_SLOT) else None)
        if weapon_index is None:
            # No weapon: one degenerate triangle keeps the tables the same shape.
            wmesh = {"positions": [(0.0, 0.0, 0.0)], "normals": [(0.0, 1.0, 0.0)], "uvs": [(0.0, 0.0)],
                     "indices": [0, 0, 0], "objects": [{"texture": 0}]}
        else:
            weapon = packs[slot][weapon_index]
            wskel = parse_skeleton(weapon)
            wmesh = slim_mesh(build_mesh(weapon, wskel["count"]))
            if name == "jester":
                # His hat is modelled where it sits on him: make it relative to his head (bone 5).
                hx, hy, hz = skel["world"][5]
                wmesh["positions"] = [(x - hx, y - hy, z - hz) for x, y, z in wmesh["positions"]]
        n = f"ek{kind}"
        height = max(v[1] for v in mesh["positions"])
        lines += [
            f"/* {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, {skel['count']} bones */",
            f"static const signed char {n}_bone_parent[] = {{", array_lines(skel["parents"], str, 24), "};",
            f"static const Dmc3Vec3 {n}_bone_local[] = {{", array_lines(skel["local"], vec, 4), "};",
            f"static const Dmc3Vec3 {n}_bone_rest[] = {{", array_lines(skel["world"], vec, 4), "};",
            f"static const Dmc3Vec3 {n}_positions[] = {{", array_lines(mesh["positions"], vec, 4), "};",
            f"static const Dmc3Vec3 {n}_normals[] = {{", array_lines(mesh["normals"], vec, 4), "};",
            f"static const Dmc3Uv {n}_uvs[] = {{", array_lines(mesh["uvs"], vec, 6), "};",
            f"static const unsigned char {n}_bones[][3] = {{", array_lines(mesh["bones"], tuple3, 8), "};",
            f"static const unsigned char {n}_weights[][3] = {{", array_lines(mesh["weights"], tuple3, 8), "};",
            f"static const unsigned short {n}_indices[] = {{", array_lines(mesh["indices"], str, 12), "};",
            f"static const Dmc3ModelObject {n}_objects[] = {{",
            array_lines(mesh["objects"], lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4), "};",
            f"static const Dmc3Vec3 {n}_weapon_positions[] = {{", array_lines(wmesh["positions"], vec, 4), "};",
            f"static const Dmc3Vec3 {n}_weapon_normals[] = {{", array_lines(wmesh["normals"], vec, 4), "};",
            f"static const Dmc3Uv {n}_weapon_uvs[] = {{", array_lines(wmesh["uvs"], vec, 6), "};",
            f"static const unsigned short {n}_weapon_indices[] = {{", array_lines(wmesh["indices"], str, 12), "};",
        ]
        table.append(
            f'    {{"{name}",{n}_bone_parent,{n}_bone_local,{n}_bone_rest,{n}_positions,{n}_normals,{n}_uvs,'
            f'{n}_bones,{n}_weights,{n}_indices,{n}_objects,{n}_weapon_positions,{n}_weapon_normals,{n}_weapon_uvs,'
            f'{n}_weapon_indices,{len(mesh["positions"])},{len(mesh["indices"])},{len(mesh["objects"])},{skel["count"]},'
            f'{len(wmesh["positions"])},{len(wmesh["indices"])},{wmesh["objects"][0]["texture"] if wmesh["objects"] else 0},'
            f'{sets[slot][0]},{height:.1f}f}},')
        max_vertices = max(max_vertices, len(mesh["positions"]))
        max_bones = max(max_bones, skel["count"])
        print(f"Enemy {name}: {len(mesh['positions'])} vertices, {len(mesh['indices']) // 3} triangles, "
              f"{skel['count']} bones, weapon {len(wmesh['positions'])} vertices, height {height:.0f}")
    lines += [f"#define ENEMY_KIND_COUNT {len(ENEMY_KINDS)}",
              f"#define ENEMY_MAX_VERTICES {max_vertices}",
              f"#define ENEMY_MAX_BONES {max_bones}",
              f"#define ENEMY_TEXTURE_SET_COUNT {len(sets)}",
              "static const EnemyTextureSet enemy_texture_sets[] = {" +
              ",".join(f"{{{n}_textures,{n}_palettes,{count}}}" for _, n, count in sorted(sets.values())) + "};",
              "static const EnemyModel enemy_models[] = {"] + table + ["};", "", "#endif", ""]
    (output / "enemy_runtime.h").write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {output / 'enemy_runtime.h'}: {len(ENEMY_KINDS)} kinds, {len(sets)} texture sets")


def pac_list(data: bytes):
    """A PAC's entries as a list (empty slots as b'')."""
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = list(struct.unpack_from(f"<{count}I", data, 8))
    present = sorted(o for o in offsets if o)
    return [data[o:min([x for x in present if x > o] + [len(data)])] if o else b"" for o in offsets]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    with Iso9660(args.iso) as iso:
        member = AfsArchive(iso, "GDATA.AFS").entries[SLOT]
        read_at = lambda offset, size: iso.read_file("GDATA.AFS", member["offset"] + offset, size)
        pac = PacReader(read_at, member["size"])
        chunks = {i: read_at(pac.entries[i]["offset"], pac.entries[i]["size"])
                  for i in (0, 1) + MOTION_CHUNKS + (COAT_CHUNK, CLOTH_CHUNK)}
        gdata = AfsArchive(iso, "GDATA.AFS")
        def read_slot(slot):
            entry = gdata.entries[slot]
            return iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
        rebellion = pnst_entries(read_slot(REBELLION_SLOT))
        rebellion_moves = read_slot(REBELLION_MOTION_SLOT)
        study_moves = {slot: read_slot(slot) for slot in STUDY_MOTION_SLOTS}
        enemy_pack = pnst_entries(read_slot(ENEMY_SLOT))
        envy_pack = pac_list(read_slot(119))
        enigma_pack = pac_list(read_slot(ENIGMA_SLOT))
        goyle_pack = pnst_entries(read_slot(GOYLE_SLOT))
        jester_pack = pac_list(read_slot(JESTER_SLOT))
        pawn_pack = pac_list(read_slot(PAWN_SLOT))
        ar_pack = pnst_entries(read_slot(AR_SLOT))
        arachne_pack = pac_list(read_slot(ARACHNE_SLOT))
        nevan_pack = pac_list(read_slot(NEVAN_SLOT))
        dullahan_pack = pac_list(read_slot(123))
        dullahan_pack[0], dullahan_pack[1] = dullahan_pack[1], dullahan_pack[0]
        beowulf_pack = pac_list(read_slot(BEOWULF_SLOT))
        executable = iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"])
    read_chunk = chunks.__getitem__
    texture_data = chunks[0]
    model_data = chunks[1]

    skeleton = parse_skeleton(model_data)
    mesh = slim_mesh(build_mesh(model_data, skeleton["count"]), 0.8)
    skipped = mesh["skipped"]

    texture_count = struct.unpack_from("<I", texture_data, 0)[0]
    sectors = struct.unpack_from("<" + "I" * texture_count, texture_data, 4)
    textures, offset = [], 0x800
    for sector_count in sectors:
        image = texture_data[offset:offset + sector_count * 0x800]
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        textures.append({"width": picture["width"], "height": picture["height"], "rgba": rgba})
        offset += sector_count * 0x800

    textures = [psp_texture(t) for t in textures]

    positions, normals, uvs = mesh["positions"], mesh["normals"], mesh["uvs"]
    bones, weights, indices = mesh["bones"], mesh["weights"], mesh["indices"]
    runtime_objects, old_triangles = mesh["objects"], mesh["old_triangles"]

    s = skeleton
    lines = [
        "#ifndef DMC3_MODEL_RUNTIME_H",
        "#define DMC3_MODEL_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py from the user's ISO. */",
        "#define DMC3_MODEL_SKINNED 1",
        "typedef struct { float x, y, z; } Dmc3Vec3;",
        "typedef struct { float u, v; } Dmc3Uv;",
        "typedef struct { unsigned int index_start, index_count, texture; } Dmc3ModelObject;",
        "typedef struct { unsigned short width, height; const unsigned char *rgba; } Dmc3Texture;",
        f"#define DMC3_MODEL_VERTEX_COUNT {len(positions)}",
        f"#define DMC3_MODEL_INDEX_COUNT {len(indices)}",
        f"#define DMC3_MODEL_OBJECT_COUNT {len(runtime_objects)}",
        f"#define DMC3_MODEL_TEXTURE_COUNT {len(textures)}",
        f"#define DMC3_BONE_COUNT {s['count']}",
        "",
        "/* Parent record per bone (-1 = root); parents precede children. */",
        "static const signed char dmc3_bone_parent[] = {",
        array_lines(s["parents"], str, 24),
        "};",
        "/* Rest-pose offset from the parent (rotation is identity in the rest pose). */",
        "static const Dmc3Vec3 dmc3_bone_local[] = {",
        array_lines(s["local"], vec, 4),
        "};",
        "/* Rest-pose model-space position of each bone. */",
        "static const Dmc3Vec3 dmc3_bone_rest[] = {",
        array_lines(s["world"], vec, 4),
        "};",
        "static const Dmc3Vec3 dmc3_model_positions[] = {",
        array_lines(positions, vec, 4),
        "};",
        "static const Dmc3Vec3 dmc3_model_normals[] = {",
        array_lines(normals, vec, 4),
        "};",
        "static const Dmc3Uv dmc3_model_uvs[] = {",
        array_lines(uvs, vec, 6),
        "};",
        "/* Three bone records per vertex; weights in 31sts summing to 31. */",
        "static const unsigned char dmc3_model_bones[][3] = {",
        array_lines(bones, lambda t: "{" + ",".join(str(x) for x in t) + "}", 8),
        "};",
        "static const unsigned char dmc3_model_weights[][3] = {",
        array_lines(weights, lambda t: "{" + ",".join(str(x) for x in t) + "}", 8),
        "};",
        "static const unsigned short dmc3_model_indices[] = {",
        array_lines(indices, str, 12),
        "};",
        "static const Dmc3ModelObject dmc3_model_objects[] = {",
        array_lines(runtime_objects, lambda v: f"{{{v['index_start']},{v['index_count']},{v['texture']}}}", 4),
        "};",
    ]
    for i, t in enumerate(textures):
        lines += [f"static const unsigned char dmc3_model_texture_{i}[] = {{",
                  array_lines(list(t["rgba"]), lambda value: f"0x{value:02x}", 16), "};"]
    lines.append("static const Dmc3Texture dmc3_model_textures[] = {")
    lines += [f"    {{{t['width']},{t['height']},dmc3_model_texture_{i}}}," for i, t in enumerate(textures)]
    lines += ["};", "", "#endif", ""]
    header = args.output / "model_runtime.h"
    header.write_text("\n".join(lines), encoding="ascii")

    write_coat(args.output, chunks[COAT_CHUNK], chunks[CLOTH_CHUNK])
    write_weapon(args.output, "rebellion", rebellion[1], rebellion[0])
    # Gun packs, in weapon order (texture set first, then the MODs): moving parts
    # (the shotgun's action, Artemis' arms) stay in their rest pose for now.
    with Iso9660(args.iso) as iso:
        gdata = AfsArchive(iso, "GDATA.AFS")
        def pack(slot):
            entry = gdata.entries[slot]
            return pnst_entries(iso.read_file("GDATA.AFS", entry["offset"], entry["size"]))
        ei = pack(102)
        write_weapon(args.output, "ebony", ei[1], ei[0], strict=False)
        write_weapon(args.output, "ivory", ei[2], ei[0], strict=False)
        for name, slot in (("shotgun", 103), ("artemis", 104), ("spiral", 105), ("kalina", 106),
                           ("cerberus", 98), ("agni", 99), ("nevan", 100)):
            guns = pack(slot)
            write_weapon(args.output, name, guns[1], guns[0], strict=False)
        # Beowulf is worn: its MOD is in Dante's own rest pose, on bones along his limbs.
        # Each triangle follows the Dante bone nearest its weapon bone.
        weapon_packs = {slot: pack(slot) for slot in (97, 98, 99, 100, 103, 104)}
        devil_packs = [iso.read_file("GDATA.AFS", gdata.entries[slot]["offset"], gdata.entries[slot]["size"]) for slot in (11, 12, 13, 14, 15)]
        fight = pack(101)
        write_weapon(args.output, "beowulf", fight[1], fight[0], strict=False)
        fight_skel = parse_skeleton(fight[1])
        fight_mesh = slim_mesh(build_mesh(fight[1], fight_skel["count"]))
        def nearest(point):
            return min(range(skeleton["count"]), key=lambda b: sum((point[k] - skeleton["world"][b][k]) ** 2 for k in range(3)))
        groups = {}
        idx = fight_mesh["indices"]
        for t in range(0, len(idx), 3):
            v = idx[t]
            weight = max(range(3), key=lambda k: fight_mesh["weights"][v][k])
            # Rigid root vertices: the vertex's own position says which limb it's on.
            bone = fight_mesh["bones"][v][weight]
            at = fight_skel["world"][bone] if bone else fight_mesh["positions"][v]
            groups.setdefault(nearest(at), []).extend(idx[t:t + 3])
        flat, table = [], []
        for bone, tri in sorted(groups.items()):
            table.append(f"{{{bone},{len(flat)},{len(tri)}}}")
            flat += tri
        (args.output / "beowulf_groups.h").write_text(
            "/* Generated by tools/prepare_skinned_model.py: Beowulf's triangles by Dante bone. */\n"
            "#ifndef DMC3_WEAPON_GROUP\n#define DMC3_WEAPON_GROUP\n"
            "typedef struct { int bone; unsigned int start, count; } WeaponGroup;\n#endif\n"
            f"static const unsigned short dmc3_beowulf_limb_indices[] = {{{','.join(map(str, flat))}}};\n"
            f"static const WeaponGroup dmc3_beowulf_limb_groups[] = {{{','.join(table)}}};\n"
            f"#define DMC3_BEOWULF_GROUP_COUNT {len(table)}\n", encoding="ascii")
    write_enemy(args.output, {117: enemy_pack, 119: envy_pack, 124: enigma_pack, GOYLE_SLOT: goyle_pack, JESTER_SLOT: jester_pack, AR_SLOT: ar_pack, ARACHNE_SLOT: arachne_pack, NEVAN_SLOT: nevan_pack, PAWN_SLOT: pawn_pack, 123: dullahan_pack, BEOWULF_SLOT: beowulf_pack})
    gen_enemy_ai.write_header(args.output / "enemy_ai_runtime.h", enemy_pack, executable, envy_pack)
    write_devils(args.output, devil_packs)

    # Motions: every MOT in Dante's PAC chunks 2-4, embedded raw and parsed on
    # the PSP (psp/testroom/motion.c). Validated here with tools/mot_ps2.py.
    motions = []
    for chunk_index in MOTION_CHUNKS:
        motions.extend(mots_in_pac(read_chunk(chunk_index), f"{chunk_index}-"))
    # Rebellion moveset follows Dante's base motions ("rb-NN" = PAC slot NN).
    motions.extend(mots_in_pac(rebellion_moves, "rb-"))
    for slot, data in study_moves.items():
        motions.extend(mots_in_pac(data, f"s{slot}-"))
    # The weapons' own motions, "w<weapon>-NN"; a placement record's mode picks one.
    for weapon, slot, chunk in ((0, 97, 4), (1, 98, 4), (2, 99, 4), (3, 100, 3), (6, 103, 3), (7, 104, 3)):
        motions.extend(mots_in_pac(weapon_packs[slot][chunk], f"w{weapon}-"))
    motions.extend(mots_in_pac(enemy_pack[ENEMY_MOTIONS], "hl-"))
    for chunk in (6,7,8,9):
        motions.extend(mots_in_pac(dullahan_pack[chunk], f"du{chunk}"))
    # The Enigma's own: "eaNN" from [4], "ebNN" from [5].
    for chunk, prefix in ((4, "ea"), (5, "eb"), (7, "ec")):   # ec: the bow arms (motions 300-302)
        count = struct.unpack_from("<I", enigma_pack[chunk], 4)[0]
        for slot, raw in enumerate(pac_list(enigma_pack[chunk])):
            if not raw:
                continue
            try:
                parsed = parse_mot(raw)
            except ValueError as error:
                print(f"Enigma {prefix}{slot:02d}: not a MOT ({error}), skipped")
                continue
            motions.append({"name": f"{prefix}{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    # The Jester's: "jsNN", numbered in the order of JESTER_MOTION_CHUNKS.
    count = 0
    for chunk in JESTER_MOTION_CHUNKS:
        for raw in pac_list(jester_pack[chunk]):
            if not raw:
                continue
            try:
                parsed = parse_mot(raw)
            except ValueError:
                continue
            motions.append({"name": f"js{count:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
            count += 1
    print(f"Jester: {count} motions")
    # The Damned Pawn's: "dpNN" by their slot in [3].
    for slot, raw in enumerate(pac_list(pawn_pack[3])):
        if raw:
            parsed = parse_mot(raw)
            motions.append({"name": f"dp{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    # Agni & Rudra's: "arNN" by their slot in [11].
    for slot, raw in enumerate(pac_list(ar_pack[11])):
        if not raw:
            continue
        try:
            parsed = parse_mot(raw)
        except ValueError:
            continue
        motions.append({"name": f"ar{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    # The Arachne's: "anCNN" (ARACHNE_MOTIONS).
    for chunk, slot in ARACHNE_MOTIONS:
        raw = pac_list(arachne_pack[chunk])[slot]
        parsed = parse_mot(raw)
        motions.append({"name": f"an{chunk}{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    for chunk, slot in NEVAN_MOTIONS:
        raw = pac_list(nevan_pack[chunk])[slot]
        parsed = parse_mot(raw)
        motions.append({"name": f"nv{chunk}{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    for slot in BEOWULF_MOTIONS:
        raw = pac_list(beowulf_pack[2])[slot]
        parsed = parse_mot(raw)
        motions.append({"name": f"bw{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    # The Blood-goyle's: "gbNN" from [8], one a state (motion N plays in state N).
    for slot, raw in enumerate(pac_list(goyle_pack[8])):
        if not raw:
            continue
        try:
            parsed = parse_mot(raw)
        except ValueError as error:
            print(f"Blood-goyle gb{slot:02d}: not a MOT ({error}), skipped")
            continue
        motions.append({"name": f"gb{slot:02d}", "data": raw[:parsed["bytes_used"]], "parsed": parsed})
    mlines = [
        "#ifndef DMC3_MOTION_RUNTIME_H",
        "#define DMC3_MOTION_RUNTIME_H",
        "/* Generated by tools/prepare_skinned_model.py: Dante's raw MOT files. */",
        "typedef struct { const char *name; const unsigned char *data; unsigned int size; } Dmc3MotionBlob;",
        f"#define DMC3_MOTION_COUNT {len(motions)}",
    ]
    for i, m in enumerate(motions):
        mlines += [f"static const unsigned char __attribute__((aligned(4))) dmc3_motion_{i}[] = {{",
                   array_lines(list(m["data"]), lambda value: f"0x{value:02x}", 16), "};"]
    mlines.append("static const Dmc3MotionBlob dmc3_motions[] = {")
    mlines += [f"    {{\"{m['name']}\", dmc3_motion_{i}, {len(m['data'])}}}," for i, m in enumerate(motions)]
    mlines += ["};", "", "#endif", ""]
    (args.output / "motion_runtime.h").write_text("\n".join(mlines), encoding="ascii")
    write_json(args.output / "motions.json", [{
        "index": i, "name": m["name"], "bytes": len(m["data"]),
        "loop_start": m["parsed"]["loop_start"], "end_frame": m["parsed"]["end_frame"],
        "channels": len(m["parsed"]["channels"]), "masks": m["parsed"]["masks"],
    } for i, m in enumerate(motions)])
    print(f"Generated {args.output / 'motion_runtime.h'}: {len(motions)} motions, "
          f"{sum(len(m['data']) for m in motions):,} bytes")

    write_json(args.output / "model-runtime-source.json", {
        "generated_utc": now(),
        "source_iso": args.iso.name,
        "gdata_slot": SLOT,
        "model_sha256": hashlib.sha256(model_data).hexdigest(),
        "vertex_count": len(positions),
        "triangle_count": len(indices) // 3,
        "triangle_count_without_strip_restarts": old_triangles,
        "skipped_deformation_objects": skipped,
        "bone_count": s["count"],
        "bone_parents": s["parents"],
        "bone_rest_world": s["world"],
        "bone_flags": s["flags"],
    })
    print(f"Generated {header}: {len(positions)} vertices, {len(indices) // 3} triangles "
          f"(was {old_triangles} without strip restarts), {s['count']} bones")


if __name__ == "__main__":
    main()
