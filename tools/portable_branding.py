"""Shared, optional local branding. Disc-derived PNGs must never enter Git."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOGO = ROOT / ".tools/branding/dmc3_portable.png"


def load_logo(path=DEFAULT_LOGO):
    if not path.is_file():
        return None
    image = Image.open(path).convert("RGBA")
    box = image.getchannel("A").getbbox()
    if box is None:
        raise ValueError(f"Empty logo: {path}")
    return image.crop(box)


def fit_logo(image, width, height):
    scale = min(width / image.width, height / image.height)
    return image.resize((max(1, round(image.width * scale)),
                         max(1, round(image.height * scale))), Image.Resampling.LANCZOS)


def title_logo(background, logo):
    canvas = background.convert("RGBA")
    piece = fit_logo(logo, 362, 152)
    canvas.alpha_composite(piece, ((480 - piece.width) // 2, 69 + (152 - piece.height) // 2))
    return canvas.convert("RGB")
