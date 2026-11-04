"""Run the game's real flow on the PSP from a script, and save what it showed.

    python tools/flow_test.py                 # title, Mission Select, briefing, into Mission 1
    python tools/flow_test.py --mission 2

Pictures are saved by the game itself: analysis/testroom/flow_*.png.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import psp_bench as bench


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mission", type=int, default=1)
    ap.add_argument("--movie", type=int, default=45, help="frames of each cutscene to play")
    ap.add_argument("--game-frames", type=int, default=240)
    args = ap.parse_args()
    keys = [("cross", 30)]                 # title -> Mission Select
    if args.mission == 2:
        keys.append(("right", 62))
    keys += [("down", 80), ("up", 90), ("cross", 110), ("cross", 160)]
    shots = [10, 25, 70, 85, 100, 145, 170]
    game_shots = [args.game_frames // 2, args.game_frames - 10]
    lines = [f"frames {args.game_frames}", "turbo 1", "fps 30", "menu 1", f"movie {args.movie}", "attack 1"]
    lines += [f"mkey {k} {f}" for k, f in keys] + [f"mshot {f}" for f in shots] + [f"psp_shot {f}" for f in game_shots]
    for f in list(bench.BUILD.glob("MENU_*.BMP")) + list(bench.BUILD.glob("SHOT_*.BMP")):
        f.unlink()
    (bench.BUILD / "MOVIE_STATS.TXT").unlink(missing_ok=True)
    bench.run_scene(lines, args.game_frames // 10 + 120)
    saved = []
    for f in sorted(bench.BUILD.glob("MENU_*.BMP")) + sorted(bench.BUILD.glob("SHOT_*.BMP")):
        out = bench.OUT / f"flow{args.mission}_{f.stem.lower()}.png"
        Image.open(f).convert("RGB").save(out)
        saved.append(out)
        print(out)
    stats = bench.BUILD / "MOVIE_STATS.TXT"
    if stats.exists():
        print(stats.read_text())
    return saved


if __name__ == "__main__":
    main()
