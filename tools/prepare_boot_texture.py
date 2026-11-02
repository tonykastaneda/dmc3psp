#!/usr/bin/env python3
"""Extract and convert the DMC3 logo, then emit a freestanding C texture."""

import argparse
import json
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, write_json
from tim2 import convert

SLOT = 2832


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/psp/generated"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        member = afs.entries[SLOT]
        data = iso.read_file("GDATA.AFS", member["offset"], member["size"])
    if data[:4] != b"TIM2":
        raise ValueError(f"GDATA slot {SLOT} is not the expected TIM2 logo")
    source = args.output / f"slot_{SLOT:05d}.tim2"
    source.write_bytes(data)
    convert(source, args.output / "converted", True)
    rgba_path = args.output / "converted" / f"slot_{SLOT:05d}_p000.rgba"
    manifest_path = args.output / "converted" / f"slot_{SLOT:05d}.json"
    rgba = rgba_path.read_bytes()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    picture = manifest["pictures"][0]
    lines = []
    for offset in range(0, len(rgba), 12):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in rgba[offset:offset + 12]) + ",")
    header = (
        "#ifndef DMC3_BOOT_TEXTURE_H\n#define DMC3_BOOT_TEXTURE_H\n\n"
        f"#define BOOT_TEXTURE_WIDTH {picture['width']}\n"
        f"#define BOOT_TEXTURE_HEIGHT {picture['height']}\n"
        f"#define BOOT_TEXTURE_SOURCE_SLOT {SLOT}\n\n"
        "static const unsigned char boot_texture_pixels[] = {\n"
        + "\n".join(lines)
        + "\n};\n\n#endif\n"
    )
    destination = args.output / "boot_texture.h"
    destination.write_text(header, encoding="ascii")
    write_json(args.output / "boot-texture-source.json", {
        "generated_utc": now(),
        "source_iso": args.iso.name,
        "gdata_slot": SLOT,
        "tim2_sha256": manifest["source_sha256"],
        "width": picture["width"],
        "height": picture["height"],
        "rgba_bytes": len(rgba),
        "header": str(destination),
    })
    print(f"Generated {destination} from GDATA slot {SLOT}: {picture['width']}x{picture['height']}, {len(rgba)} RGBA bytes")


if __name__ == "__main__":
    main()
