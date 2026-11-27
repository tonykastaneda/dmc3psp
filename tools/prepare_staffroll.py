#!/usr/bin/env python3
"""The staff roll's pages for Mission 20's ending street (docs/CREDITS_FIGHT.md 3) -> STAFF_nn.BIN.

CStaffRoll's records (0x5e0d00) name ids 4501-4535; FUN_004392b0 maps them to GDATA 3140-3174, and the 100-kill
card (id 4536) to GDATA 3175 + the language (0x5c8a50; English is 0). Each is a PAC of 2: a MOD that is one quad
over the whole 2D screen (512 x 384 units, UVs 0-1 by 0-0.875) and its 512 x 512 texture. So a page is a picture of
the screen with its text where the game shows it: this keeps the picture's top 512 x 448 texels.

Output (beside the EBOOT, from the player's disc, never committed): STAFF_00.BIN .. STAFF_34.BIN the pages,
STAFF_35.BIN the card. Each: b"STF1", u16 width, u16 height, a 256-entry CLUT (u32, PSP ABGR), then width x height
bytes of indices (PSP GU_PSM_T8).

    python tools/prepare_staffroll.py <iso> --output build/testroom
"""
import argparse
import struct
from pathlib import Path

from PIL import Image

from dmc3_image import AfsArchive, Iso9660
import prepare_hud as hud

PAGES = range(3140, 3175)   # ids 4501-4535 (gp - 0x6358 + 4k)
CARD = 3175                 # id 4536, English (0x5c8a50[0])
ROWS = 448                  # the quad's V runs 0 to 0.875 of 512


def page_image(iso, manifest, slot):
    entry = manifest["entries"][slot]
    chunks = hud.split_pac(iso.read_at(manifest["archive_offset"] + entry["offset"], entry["size"]))
    tex = next(v for v in chunks.values() if v[:4] != b"MOD ")
    w, h, rgba = hud.first_texture(tex)
    return Image.frombytes("RGBA", (w, h), bytes(rgba)).crop((0, 0, w, ROWS))


def write_t8(image, path):
    q = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette("RGBA")[: 256 * 4]
    pal += [0] * (256 * 4 - len(pal))
    clut = b"".join(struct.pack("<BBBB", pal[i], pal[i + 1], pal[i + 2], pal[i + 3]) for i in range(0, 1024, 4))
    path.write_bytes(b"STF1" + struct.pack("<HH", *image.size) + clut + q.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with Iso9660(args.iso) as iso:
        manifest = AfsArchive(iso, "GDATA.AFS").manifest()
        for n, slot in enumerate(list(PAGES) + [CARD]):
            write_t8(page_image(iso, manifest, slot), args.output / f"STAFF_{n:02d}.BIN")
    print("staff roll:", len(PAGES) + 1, "pictures ->", args.output)


if __name__ == "__main__":
    main()
