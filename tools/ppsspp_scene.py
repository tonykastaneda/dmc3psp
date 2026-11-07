"""Run a scripted scene in PPSSPP and keep what it showed: stills and an mp4.

    python tools/ppsspp_scene.py gigapede --mission 4 --room 111 --frames 900 --video 0 900 --shot 300
    python tools/ppsspp_scene.py name --script extra.txt ...   # more CMP_SCRIPT.TXT lines

The game writes its own frames (SHOT_n.BMP, main.c's "psp_shot" and "psp_video"), so the pictures are the
PSP's 480x272 framebuffer, not the emulator window. Output: analysis/testroom/<name>_*.png and <name>.mp4.
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "testroom"
EMU = Path(r"E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe")
GAME = EMU.parent / "memstick" / "PSP" / "GAME" / "DMC3TESTROOM"
OUT = ROOT / "analysis" / "testroom"
FFMPEG = Path(r"C:\ffmpeg\bin\ffmpeg.exe")


def sync():
    """The EBOOT and the data files beside it, where they changed."""
    GAME.mkdir(parents=True, exist_ok=True)
    for src in [BUILD / "EBOOT.PBP", BUILD / "STAGE.BIN", BUILD / "MENU.BIN", *BUILD.glob("ROOM_*.BIN"), *BUILD.glob("STAGE_*.BIN")]:
        dst = GAME / src.name
        if src.exists() and (not dst.exists() or dst.stat().st_size != src.stat().st_size or dst.stat().st_mtime < src.stat().st_mtime):
            shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--mission", type=int, default=4)
    ap.add_argument("--difficulty", type=int, default=1, help="0 Easy, 1 Normal")
    ap.add_argument("--room", type=int, default=-1)
    ap.add_argument("--start", type=int, default=0, help="the room's player start")
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--shot", type=int, action="append", default=[])
    ap.add_argument("--video", type=int, nargs=2, default=None, help="first and last frame to record")
    ap.add_argument("--script", type=Path, help="more CMP_SCRIPT.TXT lines")
    ap.add_argument("--line", action="append", default=[], help="one more CMP_SCRIPT.TXT line")
    ap.add_argument("--timeout", type=int, default=0)
    args = ap.parse_args()

    sync()
    for f in list(GAME.glob("SHOT_*.BMP")) + [GAME / "FOCUS_DONE.TXT"]:
        f.unlink(missing_ok=True)
    lines = [f"frames {args.frames}", "menu 1", "fps 30"]
    if args.room >= 0:
        lines.append(f"startroom {args.room} {args.start}")
    lines += [f"psp_shot {s}" for s in args.shot]
    if args.video:
        lines.append(f"psp_video {args.video[0]} {args.video[1]}")
    if args.script:
        lines += args.script.read_text().splitlines()
    lines += args.line
    (GAME / "CMP_SCRIPT.TXT").write_text("\n".join(lines) + "\n")
    (GAME / "FOCUS.TXT").write_text("cmp\n")
    (GAME / "BOOT_MISSION.TXT").write_text(f"{args.mission} {args.difficulty}\n")
    timeout = args.timeout or 60 + args.frames // 10 + (args.video[1] - args.video[0] if args.video else 0) // 4
    proc = subprocess.Popen([str(EMU), "--windowed", "--escape-exit", str(GAME / "EBOOT.PBP")], cwd=str(EMU.parent))
    start = time.time()
    try:
        while not (GAME / "FOCUS_DONE.TXT").exists():
            if proc.poll() is not None:
                print("PPSSPP exited early, code", proc.returncode)
                break
            if time.time() - start > timeout:
                print("timed out after", timeout, "s")
                break
            time.sleep(0.5)
        time.sleep(1.0)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        for f in ("FOCUS.TXT", "BOOT_MISSION.TXT", "CMP_SCRIPT.TXT"):
            (GAME / f).unlink(missing_ok=True)
    print("ran", round(time.time() - start, 1), "s")
    done = GAME / "FOCUS_DONE.TXT"
    if done.exists():
        print(done.read_text()[:2000])
    for extra in ("PERF.TXT", "DBG.TXT"):
        if (GAME / extra).exists() and (GAME / extra).stat().st_mtime > start:
            print(f"--- {extra}\n" + (GAME / extra).read_text()[:1500])
    shots = sorted(GAME.glob("SHOT_*.BMP"), key=lambda p: int(p.stem.split("_")[1]))
    stills = set(args.shot)
    frames_dir = OUT / f"{args.name}_frames"
    if args.video:
        shutil.rmtree(frames_dir, ignore_errors=True)
        frames_dir.mkdir(parents=True)
    k = 0
    for f in shots:
        n = int(f.stem.split("_")[1])
        img = Image.open(f).convert("RGB")
        if n in stills:
            out = OUT / f"{args.name}_{n}.png"
            img.save(out)
            print(out)
        if args.video and args.video[0] <= n <= args.video[1]:
            img.save(frames_dir / f"{k:05d}.png")
            k += 1
        f.unlink()
    if args.video and k:
        mp4 = OUT / f"{args.name}.mp4"
        subprocess.run([str(FFMPEG), "-y", "-v", "error", "-framerate", "30", "-i", str(frames_dir / "%05d.png"),
                        "-vf", "scale=960:544:flags=neighbor", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
                        "-movflags", "+faststart", str(mp4)], check=True)
        shutil.rmtree(frames_dir, ignore_errors=True)
        print(mp4, k, "frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
