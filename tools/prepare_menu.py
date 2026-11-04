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
    # For now on white, without the game's background or its Dante and Vergil (chunks 1, 12 and 9), until the
    # port has art of its own: the logo and the band behind the options stay.
    ("IMG_TITLE", [(TITLE, 15, False, CENTRE, {1: SPAN, 2: None, 3: None})]),
    ("IMG_MISSION", [(MISSION, 0, True, EXTEND, {})]),
    ("IMG_BANNER_1", [(BANNER_BASE, 1, False, ZOOM, {})]),
    ("IMG_BANNER_2", [(BANNER_BASE + ENGLISH_BANNER_STEP, 1, False, ZOOM, {})]),
    ("IMG_BANNER_3", [(BANNER_BASE + ENGLISH_BANNER_STEP * 2, 1, False, ZOOM, {})]),
    ("IMG_BANNER_4", [(BANNER_BASE + ENGLISH_BANNER_STEP * 3, 1, False, ZOOM, {})]),
    # Only the heading and the band for now: the results themselves aren't tracked yet.
    ("IMG_CLEAR", [(1665, 1, False, None, {0: CENTRE}), (1665, 23, False, SPAN, {})]),
]
# The Divinity Statue's shop: the game's Customize screens (build/hdref/shop/shop_ui.md). Each is a PAC of flat
# MODs, each followed by its texture set, and a "# Msg" block of its English text.
CUSTOMIZE, ITEM_SHOP, ACTION_SHOP, GUN_SHOP, EQUIP_SHOP, STYLE_SHOP = 2294, 2649, 2322, 3017, 2302, 1716
ITEM_PRICES = 0x5DB680    # in SLUS_213.61: 8 items x 8 prices, rising with each one bought
ITEM_MAX_HELD, ITEM_MAX_BUYS = 0x61A868, 0x61A870   # s8 each: how many can be held, and bought (< 1: no limit)
SHOP_TEXT = (CUSTOMIZE, ITEM_SHOP, ACTION_SHOP, GUN_SHOP)
IMAGES += [
    # Chunk 6's second quad is a rotated brush stroke, which a box of its corners can't draw.
    ("IMG_CUSTOMIZE", [(CUSTOMIZE, 0, True, EXTEND, {}), (CUSTOMIZE, 6, True, CENTRE, {1: None})]),
    ("IMG_ITEM_SHOP", [(ITEM_SHOP, 0, True, EXTEND, {})]),
    # Not working yet, so shown whole as they are on the disc.
    ("IMG_ACTION_SHOP", "all", ACTION_SHOP),
    ("IMG_GUN_SHOP", "all", GUN_SHOP),
    ("IMG_EQUIP_SHOP", "all", EQUIP_SHOP),
    ("IMG_STYLE_SHOP", "all", STYLE_SHOP),
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
    # Only drawn from the font: the briefing's two choices, where its strip has them (BRIEFING_* in menu.c).
    ("GROUP_BRIEFING_TEXT", None, [(-60.0, -166.0, 116.0, -140.0), (136.0, -166.0, 236.0, -140.0)], None),
    # Font-only places: Mission Select's heading and its mission numbers 1-20 (where the disc's digit quads sit),
    # and the Mission Clear screen's two headings (where the disc's lettering was, now left out of IMG_CLEAR).
    ("GROUP_MISSION_HEADING", None, [(-232.0, 141.0, -40.0, 171.0)], None),
    ("GROUP_MISSION_NUMBERS", None, [(-40.0, 100.0, -7.0, 123.0)] * 20, None),
    ("GROUP_CLEAR_TEXT", None, [(-225.0, 116.0, 10.0, 155.0), (-8.0, -162.0, 141.0, -136.0)], None),
    ("GROUP_CUSTOM_BOX", CUSTOMIZE, 2, True),
    ("GROUP_CUSTOM_LABELS", CUSTOMIZE, 4, True),
    ("GROUP_CUSTOM_BARS", CUSTOMIZE, 8, True),
    ("GROUP_ITEM_ROWS", ITEM_SHOP, 2, True),
    ("GROUP_ITEM_ICONS", ITEM_SHOP, 4, True),
    ("GROUP_ITEM_DIGITS", ITEM_SHOP, 6, True),
    ("GROUP_ITEM_COUNT", ITEM_SHOP, 8, True),
    ("GROUP_ITEM_YELLOW", ITEM_SHOP, 10, True),
    ("GROUP_ITEM_FRAGMENT", ITEM_SHOP, 12, True),
]


# Menu labels drawn from a font instead of the disc's lettering, cleaner on the PSP's screen: each takes the
# place and height of the label it replaces. (group, {quad: (text, alignment)}). The font is open (SIL OFL) and
# lives in .tools/fonts, not the repo; without it the disc's labels stay.
FONT_SOURCE = Path(__file__).resolve().parents[1] / ".tools" / "fonts" / "CormorantGaramond-Italic[wght].ttf"
FONT = FONT_SOURCE.with_name("menu-font.ttf")


def make_font():
    """The source font at its Bold Italic weight, with its digits swapped for its full-height lining ones
    (its default figures are old-style, at lowercase height). Needs fontTools; without it the source is used."""
    global FONT
    if FONT.exists() or not FONT_SOURCE.exists():
        return
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
    except ImportError:
        print("fontTools isn't installed: menu digits stay old-style")
        FONT = FONT_SOURCE
        return
    font = instancer.instantiateVariableFont(TTFont(str(FONT_SOURCE)), {"wght": 700})
    names = set(font.getGlyphOrder())
    digits = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"]
    for table in font["cmap"].tables:
        for code in range(0x30, 0x3A):
            lining = digits[code - 0x30] + ".lf"
            if code in table.cmap and lining in names:
                table.cmap[code] = lining
    font.save(str(FONT))
LEFT_EDGE, MIDDLE = "left", "middle"
# Mission Select's row containers span x -238..25: centred labels sit on their middle, as on the HD Collection,
# and Save and Load at about 31% and 72% across theirs (measured off the HD Collection's screen).
BAR_LEFT, BAR_RIGHT = -238.0, 25.0
BAR_MIDDLE = (BAR_LEFT + BAR_RIGHT) / 2
RECREATE = {
    "GROUP_TITLE_OPTIONS": {0: ("NEW GAME", MIDDLE), 1: ("LOAD GAME", MIDDLE), 2: ("OPTIONS", MIDDLE)},
    "GROUP_MISSION_LABELS": {12: ("EASY", LEFT_EDGE), 1: ("NORMAL", LEFT_EDGE), 2: ("HARD", LEFT_EDGE),
                             13: ("VERY HARD", LEFT_EDGE), 3: ("DANTE MUST DIE", LEFT_EDGE), 4: ("HEAVEN OR HELL", LEFT_EDGE),
                             14: ("BLOODY PALACE", BAR_MIDDLE), 15: ("DEMO DIGEST", BAR_MIDDLE), 5: ("TOTAL RANKING", BAR_MIDDLE),
                             6: ("DEBUG ROOM", BAR_MIDDLE), 7: ("SAVE", BAR_LEFT + 0.31 * (BAR_RIGHT - BAR_LEFT)),
                             8: ("LOAD", BAR_LEFT + 0.72 * (BAR_RIGHT - BAR_LEFT))},
    "GROUP_PAUSE": {2: ("STATUS", MIDDLE), 3: ("SAVE DATA", MIDDLE), 4: ("QUIT THE MISSION", MIDDLE),
                    5: ("RESTART THE MISSION", MIDDLE), 6: ("EXIT", MIDDLE), 7: ("OPTIONS", MIDDLE), 8: ("YES", MIDDLE), 9: ("NO", MIDDLE)},
    "GROUP_MISSION_HEADING": {0: ("MISSION SELECT", LEFT_EDGE)},
    "GROUP_MISSION_NUMBERS": {n: (str(n + 1), MIDDLE) for n in range(20)},
    "GROUP_MISSION_TAGS": {0: ("TURBO", MIDDLE)},
    "GROUP_CLEAR_TEXT": {0: ("MISSION CLEAR", LEFT_EDGE), 1: ("MISSION SELECT", MIDDLE)},
    "GROUP_BRIEFING_TEXT": {0: ("MISSION START", 40.0), 1: ("CUSTOMIZE", 190.0)},
    "GROUP_CUSTOM_LABELS": {0: ("CUSTOMIZE", LEFT_EDGE), 1: ("STYLE", LEFT_EDGE), 2: ("EQUIP", LEFT_EDGE), 3: ("ITEM", LEFT_EDGE),
                            4: ("ACTION", LEFT_EDGE), 5: ("GUNS", LEFT_EDGE), 6: ("EXIT", LEFT_EDGE)},
}
FONT_ATLAS = 512


def font_labels(quads, groups):
    """Draws RECREATE's labels into one atlas and points their quads at it. Returns the atlas, or None."""
    from PIL import ImageDraw, ImageFilter, ImageFont
    if not FONT.exists():
        print(f"{FONT} not found: keeping the disc's menu labels")
        return None
    sheets = [Image.new("RGBA", (FONT_ATLAS, FONT_ATLAS), (0, 0, 0, 0))]   # the PSP's textures stop at 512
    x = y = row_height = 0
    placed = []
    names = [g[0] for g in GROUPS]
    for group_name, labels in RECREATE.items():
        first, _ = groups[names.index(group_name)]
        for index, (text, align) in labels.items():
            q = list(quads[first + index])
            box_h = q[8] - q[6]                       # screen units
            px_h = max(12, round(box_h * UNIT * 2))   # twice the PSP's pixels, for a clean filter down
            font = ImageFont.truetype(str(FONT), round(px_h * 0.92))
            try:
                font.set_variation_by_name(b"Bold Italic") if FONT == FONT_SOURCE else None
            except (OSError, ValueError):
                pass
            l, top, r, bottom = font.getbbox(text)
            w, h = r - l + 8, px_h
            if x + w > FONT_ATLAS:
                x, y, row_height = 0, y + row_height, 0
            if y + h > FONT_ATLAS:
                sheets.append(Image.new("RGBA", (FONT_ATLAS, FONT_ATLAS), (0, 0, 0, 0)))
                x = y = row_height = 0
            cell = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            draw = ImageDraw.Draw(cell)
            baseline = (h - (bottom - top)) // 2 - top
            # A soft dark edge under white lettering, so it reads on the light and the dark screens alike.
            shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            ImageDraw.Draw(shadow).text((4 - l, baseline), text, font=font, fill=(0, 0, 0, 255), stroke_width=3, stroke_fill=(0, 0, 0, 255))
            cell.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(1.2)))
            draw.text((4 - l, baseline), text, font=font, fill=(255, 255, 255, 255), stroke_width=1, stroke_fill=(20, 20, 20, 255))
            sheets[-1].alpha_composite(cell, (x, y))
            # Same height and place as the disc's label; the width is the text's own.
            width_units = w / 2.0 / UNIT
            if align == LEFT_EDGE:
                x0 = q[5]
            elif align == MIDDLE:
                x0 = (q[5] + q[7]) * 0.5 - width_units * 0.5
            else:   # centred on this x
                x0 = align - width_units * 0.5
            placed.append((first + index, len(sheets) - 1, (x, y, w, h), (x0, q[6], x0 + width_units, q[8])))
            x += w + 2
            row_height = max(row_height, h + 2)
    return sheets, placed


GLYPH_SIZE = 36          # px: drawn smaller on the PSP, the filter keeps it clean
GLYPHS = "".join(chr(c) for c in range(32, 127))


def glyph_sheet():
    """Every printable ASCII character of the font on one 512x512 sheet, for text the PSP writes as it runs.
    Returns the sheet and [(char, u0, v0, u1, v1, advance, ascent)] in pixels at GLYPH_SIZE, or None."""
    from PIL import ImageDraw, ImageFilter, ImageFont
    if not FONT.exists():
        return None
    font = ImageFont.truetype(str(FONT), GLYPH_SIZE)
    try:
        font.set_variation_by_name(b"Bold Italic")
    except (OSError, ValueError):
        pass
    sheet = Image.new("RGBA", (FONT_ATLAS, FONT_ATLAS), (0, 0, 0, 0))
    ascent, descent = font.getmetrics()
    cell_h = ascent + descent + 6
    x = y = 0
    table = []
    for ch in GLYPHS:
        l, top, r, bottom = font.getbbox(ch)
        w = max(1, r - min(l, 0)) + 6
        if x + w > FONT_ATLAS:
            x, y = 0, y + cell_h
        cell = Image.new("RGBA", (w, cell_h), (0, 0, 0, 0))
        shadow = Image.new("RGBA", (w, cell_h), (0, 0, 0, 0))
        ImageDraw.Draw(shadow).text((3 - min(l, 0), 3), ch, font=font, fill=(0, 0, 0, 255), stroke_width=3, stroke_fill=(0, 0, 0, 255))
        cell.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(1.2)))
        ImageDraw.Draw(cell).text((3 - min(l, 0), 3), ch, font=font, fill=(255, 255, 255, 255), stroke_width=1, stroke_fill=(20, 20, 20, 255))
        sheet.alpha_composite(cell, (x, y))
        table.append((ch, x, y, x + w, y + cell_h, font.getlength(ch), 3 - min(l, 0)))
        x += w + 1
    return sheet, table, cell_h


def whole_screen(data):
    """Every MOD of a screen, each with the texture set after it: the first extends, the rest sit as laid out."""
    parts = split_pac(data)
    mods = [k for k, part in enumerate(parts) if part is not None and part[:4] == b"MOD " and k + 1 < len(parts) and is_textures(parts[k + 1])]
    return [(None, k, True, EXTEND if n == 0 else CENTRE, {}) for n, k in enumerate(mods)]


def messages(data):
    """A screen's "# Msg n" blocks: {n: text}, with <BR> as a new line and button glyphs as names."""
    import re
    buttons = {"1": "TRIANGLE", "2": "SQUARE", "3": "X", "4": "R", "5": "L", "7": "L", "8": "L", "16": "X", "17": "O"}
    out = {}
    for m in re.finditer(rb"# Msg (\d+)\s*\n(.*?)# End", data, re.S):
        text = "".join(re.findall(r'"(.*?)"', m.group(2).decode("cp932", "replace"), re.S))
        text = text.replace("<HBR>", "").replace("<BR>", "\n").replace("<N00>", "%d")
        text = re.sub(r"<BTN (\d+)>", lambda b: buttons.get(b.group(1), "?"), text)
        out[int(m.group(1))] = text.encode("ascii", "replace").decode("ascii")
    return out


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


WHITE_IMAGES = {"IMG_TITLE"}
# Lettering painted into a background, covered with the plain band beside it (PSP pixels, and how far right the
# cover comes from). The font draws the heading over it.
HEADING_PATCHES = [("IMG_MISSION", (30, 8, 200, 42), 190)]


def bake(slots, layers, background=(0, 0, 0, 255)):
    """Lay the quads of these MODs over black (or another colour) at twice the size, then shrink to the PSP's."""
    canvas = Image.new("RGBA", (ART_W * 2, ART_H * 2), background)
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
        wanted = {g[1] for g in GROUPS if g[1] is not None} | {NOW_LOADING} | set(SHOP_TEXT)
        for _, layers, *rest in IMAGES:
            wanted |= {rest[0]} if layers == "all" else {layer[0] for layer in layers}
        slots = {s: read(s) for s in wanted}
        executable = iso.read_file("SLUS_213.61", 0, iso.files["SLUS_213.61"]["size"])
    for n, (name, layers, *rest) in enumerate(IMAGES):
        if layers == "all":
            IMAGES[n] = (name, [(rest[0], *layer[1:]) for layer in whole_screen(slots[rest[0]])])

    images = [bake(slots, layers, (255, 255, 255, 255) if name in WHITE_IMAGES else (0, 0, 0, 255)) for name, layers in IMAGES]
    for name, (left, top, right, bottom), shift in HEADING_PATCHES:
        image = images[[n for n, _ in IMAGES].index(name)]
        image.paste(image.crop((left + shift, top, right + shift, bottom)), (left, top))
    # Now Loading is a bare picture, over black.
    _, rgba, pic = decode_picture(slots[NOW_LOADING], 0x80 if slots[NOW_LOADING][5] == 1 else 0x10)

    atlases, atlas_index, groups, quads = [], {}, [], []
    atlases.append((pic["width"], pic["height"], bytes(rgba)))
    for name, slot, chunk, after in GROUPS:
        if slot is None:
            # Places only, for font_labels to fill.
            groups.append((len(quads), len(chunk)))
            quads += [(0, 0.0, 0.0, 0.0, 0.0, *rect) for rect in chunk]
            continue
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

    make_font()
    made = font_labels(quads, groups)
    if made:
        sheets, placed = made
        font_atlas = len(atlases)
        for sheet in sheets:
            atlases.append((FONT_ATLAS, FONT_ATLAS, sheet.tobytes()))
        for at, sheet, (ax, ay, aw, ah), (x0, y0, x1, y1) in placed:
            quads[at] = (font_atlas + sheet, ax / FONT_ATLAS, ay / FONT_ATLAS, (ax + aw) / FONT_ATLAS, (ay + ah) / FONT_ATLAS, x0, y0, x1, y1)
        if args.dump:
            sheets[0].save("build/hdref/menu_font_atlas.png")
        print(f"menu labels from {FONT.name}: {len(placed)}")
    glyphs = glyph_sheet()
    glyph_atlas = len(atlases)
    if glyphs:
        sheet, table, cell_h = glyphs
        atlases.append((FONT_ATLAS, FONT_ATLAS, sheet.tobytes()))

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
    # The shop's prices, from the executable, and its text, from the screens' own message blocks.
    from gen_enemy_ai import Elf
    elf = Elf(executable)
    lines.append("static const unsigned int shop_item_prices[8][8] = {" + ",".join(
        "{" + ",".join(str(elf.word(ITEM_PRICES + item * 32 + k * 4)) for k in range(8)) + "}" for item in range(8)) + "};")
    signed = lambda a: elf.byte(a) - 256 if elf.byte(a) > 127 else elf.byte(a)
    lines.append("static const signed char shop_item_max_held[8] = {" + ",".join(str(signed(ITEM_MAX_HELD + k)) for k in range(8)) + "};")
    lines.append("static const signed char shop_item_max_buys[8] = {" + ",".join(str(signed(ITEM_MAX_BUYS + k)) for k in range(8)) + "};")
    if glyphs:
        lines += [f"#define MENU_GLYPH_ATLAS {glyph_atlas}", f"#define MENU_GLYPH_SIZE {GLYPH_SIZE}", f"#define MENU_GLYPH_CELL {cell_h}",
                  "typedef struct { unsigned short u0, v0, u1, v1; float advance, left; } MenuGlyph;",
                  "static const MenuGlyph menu_glyphs[95] = {" +
                  ",".join(f"{{{u0},{v0},{u1},{v1},{adv:.2f}f,{left}}}" for _, u0, v0, u1, v1, adv, left in table) + "};"]
    else:
        lines.append("#define MENU_GLYPH_ATLAS (-1)")
    lines.append("typedef struct { short screen, id; const char *text; } ShopText;")
    cells = []
    for screen, slot in enumerate(SHOP_TEXT):
        for number, text in sorted(messages(slots[slot]).items()):
            escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
            cells.append(f'{{{screen},{number},"{escaped}"}}')
    lines.append("static const ShopText shop_text[] = {" + ",".join(cells) + "};")
    lines.append(f"#define SHOP_TEXT_COUNT {len(cells)}")
    args.header.write_text("\n".join(lines) + "\n")
    print(f"MENU.BIN: {len(blob):,} bytes, {len(images)} images, {len(atlases)} atlases, {len(quads)} quads")


if __name__ == "__main__":
    main()
