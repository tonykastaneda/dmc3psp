"""Side-by-side video: one input script on the real game and on the port.

    python tools/ref_video.py name   # analysis/ref/video_name.mp4 (H.264, for texting)
"""
import ctypes
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pcsx2_ref as r
import ref_compare as cmp

ROOT = cmp.ROOT
OUT = cmp.OUT
GAME_DIR = cmp.GAME_DIR
PPSSPP = r"E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe"
ARIAL = "C\\:/Windows/Fonts/arialbd.ttf"

# The street behind the camera at the test spot is open: both games run the same way.
cmp.SCRIPTS.setdefault("demo", (660, [
    ("ldown", 20, 110), ("cross", 80, 82),                       # run, running jump
    ("triangle", 170, 172), ("triangle", 195, 197), ("triangle", 220, 222),  # Rebellion combo
    ("r1", 290, 420), ("lleft", 300, 360), ("square", 320, 322), ("square", 335, 337), ("square", 350, 352),  # strafe and shoot
    ("lright", 380, 400), ("cross", 385, 387),                   # lock-on flip
    ("cross", 470, 472),                                         # standing jump
    ("style0", 2, 3), ("lup", 540, 600), ("circle", 560, 562),   # Trickster dash
]))


def capture(hwnd_rect, path, seconds):
    x, y, w, h = hwnd_rect
    return subprocess.Popen([r.FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-framerate", "30",
                             "-offset_x", str(x), "-offset_y", str(y), "-video_size", f"{w & ~1}x{h & ~1}", "-i", "desktop",
                             "-t", str(seconds), "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", str(path)])


def record_ps2(name, seconds):
    r.focus()
    time.sleep(0.2)
    x, y, w, h = r.client_rect(r.find_window())
    done = threading.Event()
    # The state load and teleport take 1.8 s in record_ps2.
    path = OUT / f"raw_ps2_{name}.mp4"
    proc = None

    def start_capture():
        nonlocal proc
        time.sleep(1.75)
        proc = capture((x, y + 150, w, h - 195), path, seconds)
    t = threading.Thread(target=start_capture)
    t.start()
    cmp.record_ps2(name)
    t.join()
    proc.wait()
    return path


def record_port(name, seconds):
    cmp.write_port_script(name)
    (GAME_DIR / "FOCUS.TXT").write_text("cmp", encoding="ascii")
    done = GAME_DIR / "FOCUS_DONE.TXT"
    done.unlink(missing_ok=True)
    subprocess.run(["powershell", "-NoProfile", "-Command", f"Copy-Item '{ROOT / 'build/testroom/EBOOT.PBP'}' '{GAME_DIR / 'EBOOT.PBP'}' -Force"], check=True)
    proc = subprocess.Popen([PPSSPP, "--windowed", "--escape-exit", str(GAME_DIR / "EBOOT.PBP")], cwd=str(Path(PPSSPP).parent))
    user32 = ctypes.windll.user32
    hwnd = 0
    for _ in range(100):
        hwnd = user32.FindWindowW(None, None)
        found = []
        def cb(h, _):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(h, buf, 256)
            if "DMC3 Test Room" in buf.value:
                found.append(h)
            return True
        user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(cb), 0)
        if found:
            hwnd = found[0]
            break
        time.sleep(0.05)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.05)
    path = OUT / f"raw_psp_{name}.mp4"
    cap = capture(r.client_rect(hwnd), path, seconds)
    while not done.exists() and proc.poll() is None and time.time() < time.time() + 1:
        time.sleep(0.05)
        if cap.poll() is not None:
            break
    cap.wait()
    proc.terminate()
    (GAME_DIR / "FOCUS.TXT").unlink(missing_ok=True)
    return path


def main():
    name = sys.argv[1]
    frames = cmp.SCRIPTS[name][0]
    seconds = frames / 60.0 + 0.8
    a = record_ps2(name, seconds)
    b = record_port(name, seconds)
    out = OUT / f"video_{name}.mp4"
    label = "drawtext=fontfile='%s':text='%s':x=10:y=10:fontsize=30:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=6"
    filt = ("[0:v]scale=-2:544,%s[a];[1:v]scale=-2:544,%s[b];[a][b]hstack=inputs=2,scale=-2:540[v]"
            % (label % (ARIAL, "PS2 (real game)"), label % (ARIAL, "PSP port")))
    subprocess.run([r.FFMPEG, "-y", "-loglevel", "error", "-i", str(a), "-i", str(b), "-filter_complex", filt, "-map", "[v]",
                    "-c:v", "libx264", "-profile:v", "high", "-pix_fmt", "yuv420p", "-crf", "24", "-movflags", "+faststart", str(out)], check=True)
    print(out, out.stat().st_size // 1024, "KB")


if __name__ == "__main__":
    main()
