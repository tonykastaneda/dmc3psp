"""Verify that generated stage grids contain every native collision vertex.

--repair regenerates only truncated stages from the player's own ISO. No disc
data or generated binaries belong in a commit.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

from gen_rooms import SLOTS
from prepare_stage import STAGE_SLOT

ROOT = Path(__file__).resolve().parents[1]


def inspect(path):
    blob = path.read_bytes()
    h = struct.unpack_from("<4s7I4f6f6I2f2I", blob)
    if h[:2] != (b"STG1", 3):
        raise ValueError(f"unsupported stage {path}")
    offset, walls, floors = struct.unpack_from("<3I", blob, 0x80)
    x0, z0, cell = h[24:27]
    w, height = h[6:8]
    outside = 0
    for i in range(walls + floors):
        t = struct.unpack_from("<9f", blob, offset + i * 52)
        if any(not (x0 <= t[v*3] < x0+w*cell and z0 <= t[v*3+2] < z0+height*cell)
               for v in range(3)):
            outside += 1
    return dict(file=path.name, width=w, height=height, outside_triangles=outside,
                collision_triangles=walls+floors)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repair", action="store_true")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    directory = ROOT / "build/testroom"
    files = sorted(directory.glob("ROOM_*.BIN")) + sorted(directory.glob("STAGE*.BIN"))
    results = []
    iso = next(ROOT.glob("*.iso"), None)
    for path in files:
        result = inspect(path)
        if result["outside_triangles"] and args.repair:
            if not iso:
                ap.error("repair requires the player's ISO")
            slot = SLOTS[path.stem[5:]] if path.stem.startswith("ROOM_") else 272 if path.stem == "STAGE_M1" else STAGE_SLOT
            result["before"] = dict(result)
            subprocess.run([sys.executable, str(ROOT / "tools/prepare_stage.py"), str(iso),
                            "--output", str(path), "--slot", str(slot)], check=True)
            result.update(inspect(path))
        results.append(result)
        if result["outside_triangles"] or "before" in result:
            print(json.dumps(result), flush=True)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(results, indent=2))
    remaining = sum(bool(r["outside_triangles"]) for r in results)
    print(f"{len(results)} stages checked, {remaining} truncated grids")
    return int(remaining != 0)


if __name__ == "__main__":
    sys.exit(main())
