#!/usr/bin/env python3
"""The game's menus, read from the player's disc into build/testroom/MENU.BIN.

Menu slots in GDATA are PACs of texture sets and flat MODs in screen units: origin at the centre, Y up,
with the art filling +-256 x +-192. Some slots pair each MOD with the texture set before it, others
with the one after. Full-screen art is baked to 16-bit images at the PSP's size. Everything that can
change (options, labels, arrows) stays as 8-bit atlases with one quad per item, so the runtime can
highlight or hide each. See docs/GAME_FLOW.md.

The originals are 4:3. These are recomposed for the PSP's 16:9, layer by layer, rather than stretched:
figures anchor to the screen's edges, backgrounds extend, and banners zoom to fill.

MENU.BIN, little-endian, each section 16-byte aligned:
  'MNU1', u32 image count, u32 atlas count, u32 group count, u32 quad count, then section offsets
  images: {u32 offset} 5650 pixels, IMAGE_W stride, IMAGE_H rows
  atlases: {u32 width, height, palette offset, pixels offset}
  groups: {u32 first quad, quad count}
  quads: {u32 atlas, float u0, v0, u1, v1, x0, y0, x1, y1}
"""
import argparse
import struct
from pathlib import Path

from PIL import Image

from dmc3_image import AfsArchive, Iso9660
from mod_ps2 import parse_mod
from mod_skeleton import parse_skeleton, parse_skin, strip_faces
from tim2 import decode_picture

IMAGE_W, IMAGE_H = 512, 272
ART_W, ART_H = 480, 272   # the whole screen
UNIT = ART_H / 384.0      # PSP pixels to one screen unit: the art's height fits the screen's
SCREEN_UNITS_X = ART_W / UNIT / 2.0   # half the screen's width, in units

TITLE, MISSION, STRIP, PORTRAIT, NOW_LOADING, BANNER_BASE = 2820, 3183, 1673, 3207, 2831, 1506  # English
ENGLISH_BANNER_STEP = 8
MISSIONS = 2

# How a layer goes from 4:3 to the wide screen:
SPAN, LEFT, RIGHT, CENTRE, ZOOM, EXTEND = "span", "left", "right", "centre", "zoom", "extend"
EDGE = SCREEN_UNITS_X - 256.0     # how far a +-256 layer moves to sit against the screen's edge
# (name, layers), a layer being (slot, MOD chunk, whether its texture set comes after it, placement, {quads: placement}).
# A placement in the dict overrides the layer's for that quad, and None leaves the quad out.
# The title's logo chunk: 0 the logo, 1 the band behind the options, 2 and 3 the Vergil / Dante bar for choosing a character.
IMAGES = [
    ("IMG_TITLE", [(TITLE, 1, False, SPAN, {}), (TITLE, 12, False, LEFT, {}), (TITLE, 9, False, RIGHT, {}),
                   (TITLE, 15, False, CENTRE, {1: SPAN, 2: None, 3: None})]),
    ("IMG_MISSION", [(MISSION, 0, True, EXTEND, {})]),
    ("IMG_BANNER_1", [(BANNER_BASE, 1, False, ZOOM, {})]),
    ("IMG_BANNER_2", [(BANNER_BASE + ENGLISH_BANNER_STEP, 1, False, ZOOM, {})]),
    ("IMG_BANNER_3", [(BANNER_BASE + ENGLISH_BANNER_STEP * 2, 1, False, ZOOM, {})]),
    ("IMG_BANNER_4", [(BANNER_BASE + ENGLISH_BANNER_STEP * 3, 1, False, ZOOM, {})]),
    # Only the heading and the band for now: the results themselves aren't tracked yet.
    ("IMG_CLEAR", [(1665, 1, False, None, {0: CENTRE, 1: CENTRE}), (1665, 23, False, SPAN, {}),
                   (1665, 25, False, None, {1: CENTRE})]),
]
GROUPS = [
    ("GROUP_TITLE_OPTIONS", TITLE, 3, False),
    ("GROUP_TITLE_TUTORIAL", TITLE, 5, False),
    ("GROUP_TITLE_DIFFICULTY", TITLE, 7, False),
    ("GROUP_MISSION_ARROWS", MISSION, 2, True),
    ("GROUP_MISSION_ROWS", MISSION, 4, True),
    ("GROUP_MISSION_LABELS", MISSION, 6, True),
    ("GROUP_MISSION_DIGITS", MISSION, 8, True),
    ("GROUP_MISSION_TAGS", MISSION, 20, True),
    ("GROUP_BRIEFING", STRIP, 1, False),
    ("GROUP_PAUSE", 2075, 1, False),
]


def split_pac(data):
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = struct.unpack_from(f"<{count}I", data, 8)
    present = sorted(o for o in offsets if o) + [len(data)]
    return [data[o:min(p for p in present if p > o)] if o else None for o in offsets]


def textures(data):
    """A texture set: (width, height, RGBA bytes) each."""
    count = struct.unpack_from("<I", data, 0)[0]
    sectors = struct.unpack_from(f"<{count}I", data, 4)
    offset, out = 0x800, []
    for size in sectors:
        image = data[offset:offset + size * 0x800]
        offset += size * 0x800
        _, rgba, picture = decode_picture(image, 0x80 if image[5] == 1 else 0x10)
        out.append((picture["width"], picture["height"], bytes(rgba)))
    return out


def is_textures(part):
    return part is not None and len(part) > 0x804 and part[0x800:0x804] == b"TIM2"


def chunk_textures(parts, chunk, after):
    if after:
        return textures(parts[chunk + 1])
    for i in range(chunk - 1, -1, -1):
        if is_textures(parts[i]):
            return textures(parts[i])
    raise ValueError(f"no textures before chunk {chunk}")


def mod_quads(mod):
    """One quad per object sub-mesh of a flat MOD: (texture, u0, v0, u1, v1, x0, y0, x1, y1)."""
    skeleton = parse_skeleton(mod)
    objects, _ = parse_mod(mod, split_submeshes=True)
    quads = []
    for obj in objects:
        try:
            _, _, adc = parse_skin(mod, obj["bone_offset"], obj["weight_offset"], obj["vertex_count"], skeleton["bone_count"])
            faces = strip_faces(adc)
        except (ValueError, KeyError):
            faces = obj["faces"]
        points = [v for face in faces for v in face]
        if not points:
            continue
        texture = struct.unpack_from("<H", mod, obj["mesh_offset"] + 2)[0] if "mesh_offset" in obj else 0
        us = [obj["uvs"][v][0] for v in points]
        vs = [obj["uvs"][v][1] for v in points]
        xs = [obj["positions"][v][0] for v in points]
        ys = [obj["positions"][v][1] for v in points]
        quads.append((texture, min(us), min(vs), max(us), max(vs), min(xs), min(ys), max(xs), max(ys)))
    return quads


def place(canvas, source, box, x0, y0, x1, y1):
    """Draw part of a picture at these screen units, clipped by the canvas."""
    per_unit = canvas.height / 384.0
    size = (max(1, round((x1 - x0) * per_unit)), max(1, round((y1 - y0) * per_unit)))
    piece = source.crop(box).resize(size, Image.Resampling.LANCZOS)
    at = (round(canvas.width / 2 + x0 * per_unit), round(canvas.height / 2 - y1 * per_unit))
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(piece, at)
    canvas.alpha_composite(layer)


def bake(slots, layers):
    """Lay the quads of these MODs over black at twice the size, then shrink to the PSP's."""
    canvas = Image.new("RGBA", (ART_W * 2, ART_H * 2), (0, 0, 0, 255))
    for slot, chunk, after, placement, overrides in layers:
        parts = split_pac(slots[slot])
        sets = chunk_textures(parts, chunk, after)
        for number, (texture, u0, v0, u1, v1, x0, y0, x1, y1) in enumerate(mod_quads(parts[chunk])):
            how = overrides.get(number, placement)
            if how is None:
                continue
            w, h, rgba = sets[min(texture, len(sets) - 1)]
            source = Image.frombytes("RGBA", (w, h), rgba)
            left, top, right, bottom = int(u0 * w), int(v0 * h), max(int(u0 * w) + 1, int(u1 * w)), max(int(v0 * h) + 1, int(v1 * h))
            box = (left, top, right, bottom)
            if how == SPAN:
                place(canvas, source, box, -SCREEN_UNITS_X, y0, SCREEN_UNITS_X, y1)
            elif how == LEFT:
                place(canvas, source, box, x0 - EDGE, y0, x1 - EDGE, y1)
            elif how == RIGHT:
                place(canvas, source, box, x0 + EDGE, y0, x1 + EDGE, y1)
            elif how == CENTRE:
                place(canvas, source, box, x0, y0, x1, y1)
            elif how == ZOOM:
                zoom = SCREEN_UNITS_X / 256.0
                place(canvas, source, box, x0 * zoom, y0 * zoom, x1 * zoom, y1 * zoom)
            elif how == EXTEND:
                # The left part holds lettering, so it stays true to size; the right, plain, stretches.
                cut = left + (right - left) * 2 // 5
                at = x0 + (x1 - x0) * 2 / 5 - EDGE
                place(canvas, source, (left, top, cut, bottom), x0 - EDGE, y0, at, y1)
                place(canvas, source, (cut, top, right, bottom), at, y0, SCREEN_UNITS_X, y1)
    return canvas.convert("RGB").resize((ART_W, ART_H), Image.Resampling.LANCZOS)


def rgb565(image):
    out = bytearray(IMAGE_W * IMAGE_H * 2)
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b = pixels[x, y]
            struct.pack_into("<H", out, (y * IMAGE_W + x) * 2, (b >> 3) << 11 | (g >> 2) << 5 | r >> 3)
    return bytes(out)


def fit_pow2(width, height, rgba):
    """The GE swizzles nothing here, but T8 still wants 16-pixel-wide rows for its stride."""
    return width, height, rgba


def t8(width, height, rgba):
    image = Image.frombytes("RGBA", (width, height), rgba)
    quant = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    palette = quant.getpalette("RGBA")
    palette += [0] * (1024 - len(palette))
    words = [palette[i] | palette[i + 1] << 8 | palette[i + 2] << 16 | palette[i + 3] << 24 for i in range(0, 1024, 4)]
    return struct.pack("<256I", *words), quant.tobytes()


def align(blob):
    while len(blob) % 16:
        blob.append(0)
    return len(blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--header", type=Path, required=True)
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        read = lambda slot: iso.read_file("GDATA.AFS", afs.entries[slot]["offset"], afs.entries[slot]["size"])
        wanted = {g[1] for g in GROUPS} | {layer[0] for _, layers in IMAGES for layer in layers} | {NOW_LOADING}
        slots = {s: read(s) for s in wanted}

    images = [bake(slots, layers) for _, layers in IMAGES]
    # Now Loading is a bare picture, over black.
    _, rgba, pic = decode_picture(slots[NOW_LOADING], 0x80 if slots[NOW_LOADING][5] == 1 else 0x10)

    atlases, atlas_index, groups, quads = [], {}, [], []
    atlases.append((pic["width"], pic["height"], bytes(rgba)))
    for name, slot, chunk, after in GROUPS:
        parts = split_pac(slots[slot])
        sets = chunk_textures(parts, chunk, after)
        first = len(quads)
        for texture, *rest in mod_quads(parts[chunk]):
            key = (slot, chunk + 1 if after else chunk, min(texture, len(sets) - 1))
            if key not in atlas_index:
                atlas_index[key] = len(atlases)
                atlases.append(sets[key[2]])
            quads.append((atlas_index[key], *rest))
        groups.append((first, len(quads) - first))
        if args.dump:
            print(name)
            for i, q in enumerate(quads[first:]):
                print(f"  {i:2d} atlas{q[0]} uv {q[1]:.3f},{q[2]:.3f}-{q[3]:.3f},{q[4]:.3f} at {q[5]:.0f},{q[6]:.0f} .. {q[7]:.0f},{q[8]:.0f}")

    header = 16 + 16 + 16
    blob = bytearray(header)
    image_table = align(blob)
    blob += bytes(4 * len(images))
    atlas_table = align(blob)
    blob += bytes(16 * len(atlases))
    group_table = align(blob)
    for first, count in groups:
        blob += struct.pack("<2I", first, count)
    quad_table = align(blob)
    for q in quads:
        blob += struct.pack("<I8f", *q)
    for i, image in enumerate(images):
        struct.pack_into("<I", blob, image_table + i * 4, align(blob))
        blob += rgb565(image)
    for i, (w, h, rgba) in enumerate(atlases):
        palette, pixels = t8(w, h, rgba)
        at_palette = align(blob)
        blob += palette
        at_pixels = align(blob)
        blob += pixels
        struct.pack_into("<4I", blob, atlas_table + i * 16, w, h, at_palette, at_pixels)
    struct.pack_into("<4s4I4I", blob, 0, b"MNU1", len(images), len(atlases), len(groups), len(quads),
                     image_table, atlas_table, group_table, quad_table)
    args.output.write_bytes(blob)

    lines = ["/* Generated from the disc by tools/prepare_menu.py */", "#pragma once",
             f"#define MENU_IMAGE_W {IMAGE_W}", f"#define MENU_IMAGE_H {IMAGE_H}",
             f"#define MENU_ART_W {ART_W}", f"#define MENU_ART_H {ART_H}", "#define ATLAS_NOW_LOADING 0"]
    lines += [f"#define {name} {i}" for i, (name, *_) in enumerate(IMAGES)]
    lines += [f"#define {name} {i}" for i, (name, *_) in enumerate(GROUPS)]
    args.header.write_text("\n".join(lines) + "\n")
    print(f"MENU.BIN: {len(blob):,} bytes, {len(images)} images, {len(atlases)} atlases, {len(quads)} quads")


if __name__ == "__main__":
    main()
