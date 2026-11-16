#!/usr/bin/env python3
"""The XMB's pictures for the EBOOT: ICON0.PNG (144x80, the game list's icon) and PIC1.PNG (480x272, the
background while it's chosen).

ICON0 uses the optional local Portable logo, falling back to the player's disc logo (GDATA slot 2820 chunk 15),
on black. PIC1 is
a picture of the player's own choosing (--background, by default .tools/xmb/PIC1_source.jpg, which isn't in the
repo), cropped to the PSP's shape around a point that keeps its subject in frame, and shrunk. The optional
Portable logo is composited in its open left side, without changing the background crop.
"""
import argparse
from pathlib import Path

from PIL import Image
from portable_branding import DEFAULT_LOGO, fit_logo, load_logo

from dmc3_image import AfsArchive, Iso9660
import prepare_menu as M

ROOT = Path(__file__).resolve().parents[1]
ICON_W, ICON_H = 144, 80
PIC_W, PIC_H = 480, 272


def logo(iso):
    """The title's logo alone, on black, cropped to what's lit."""
    with Iso9660(iso) as image:
        afs = AfsArchive(image, "GDATA.AFS")
        e = afs.entries[M.TITLE]
        slots = {M.TITLE: image.read_file("GDATA.AFS", e["offset"], e["size"])}
    art = M.bake(slots, [(M.TITLE, 15, False, M.CENTRE, {1: None, 2: None, 3: None})])
    box = art.convert("L").point(lambda v: 255 if v > 24 else 0).getbbox()
    return art.crop(box) if box else art


def icon(art):
    canvas = Image.new("RGB", (ICON_W, ICON_H), (0, 0, 0))
    scale = min((ICON_W - 8) / art.width, (ICON_H - 8) / art.height)
    piece = art.resize((max(1, round(art.width * scale)), max(1, round(art.height * scale))), Image.Resampling.LANCZOS)
    canvas.paste(piece, ((ICON_W - piece.width) // 2, (ICON_H - piece.height) // 2), piece.getchannel("A") if piece.mode == "RGBA" else None)
    return canvas


def background(path, focus_x, top):
    """The widest 480:272 crop of the picture, its left edge set so focus_x (0-1 across) sits in it, from top."""
    picture = Image.open(path).convert("RGB")
    w = picture.width
    h = round(w * PIC_H / PIC_W)
    if h > picture.height:
        h = picture.height
        w = round(h * PIC_W / PIC_H)
    left = min(max(0, round(focus_x * picture.width - w / 2)), picture.width - w)
    top = min(max(0, round(top * picture.height)), picture.height - h)
    return picture.crop((left, top, left + w, top + h)).resize((PIC_W, PIC_H), Image.Resampling.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True, help="the folder for ICON0.PNG and PIC1.PNG")
    ap.add_argument("--background", type=Path, default=ROOT / ".tools" / "xmb" / "PIC1_source.jpg")
    ap.add_argument("--focus", type=float, default=0.5, help="how far across the picture its subject is, 0-1")
    ap.add_argument("--top", type=float, default=0.0, help="where the crop starts down the picture, 0-1")
    ap.add_argument("--logo", type=Path, default=DEFAULT_LOGO, help="optional local transparent Portable logo")
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    portable = load_logo(args.logo)
    icon(portable if portable is not None else logo(args.iso)).save(args.output / "ICON0.PNG", optimize=True)
    print(f"ICON0.PNG: {ICON_W}x{ICON_H}, {'Portable' if portable is not None else 'disc'} logo")
    if args.background.exists():
        picture = background(args.background, args.focus, args.top).convert("RGBA")
        if portable is not None:
            piece = fit_logo(portable, 244, 132)
            picture.alpha_composite(piece, (12, (PIC_H - piece.height) // 2))
        picture.convert("RGB").save(args.output / "PIC1.PNG", optimize=True)
        print(f"PIC1.PNG: {PIC_W}x{PIC_H} from {args.background.name}")
    else:
        print(f"no {args.background}: no PIC1.PNG")


if __name__ == "__main__":
    main()
