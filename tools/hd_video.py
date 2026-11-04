"""Side-by-side video: one key script on the HD Collection's DMC3 and on the port.

    python tools/hd_video.py name   # analysis/ref/hd/video_name.mp4 (H.264, for texting)

Both run Turbo, as the user's save does, so they play at the same speed.
"""
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_ref as h
import hd_trace as t
import ref_compare as cmp
import ref_video as v

CAMERA_YAW = -169
ARIAL = v.ARIAL


def port_script(name):
    seconds, keys = t.SCRIPTS[name]
    frames = int(seconds * 60)
    events = [(k, int(a * 60) + 1, int(b * 60)) for k, a, b in keys if k != "tp"]
    events += [("enemy", 700, 0), ("camyaw", CAMERA_YAW, 0), ("turbo", 1, 0)]
    cmp.SCRIPTS[name] = (frames, events)
    return frames


def main():
    name = sys.argv[1]
    frames = port_script(name)
    seconds = frames / 60.0 + 0.8
    raw = t.OUT / f"trace_{name}.mp4"
    t.run(name, video=True)
    b = v.record_port(name, seconds)
    h.focus()
    out = t.OUT / f"video_{name}.mp4"
    label = "drawtext=fontfile='%s':text='%s':x=10:y=10:fontsize=30:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=6"
    filt = ("[0:v]fps=30,scale=-2:544,%s[a];[1:v]scale=-2:544,%s[b];[a][b]hstack=inputs=2,scale=-2:540[v]"
            % (label % (ARIAL, "HD Collection"), label % (ARIAL, "Port")))
    subprocess.run([h.FFMPEG, "-y", "-loglevel", "error", "-i", str(raw), "-i", str(b), "-filter_complex", filt,
                    "-map", "[v]", "-shortest", "-c:v", "libx264", "-profile:v", "high", "-pix_fmt", "yuv420p",
                    "-crf", "24", "-movflags", "+faststart", str(out)], check=True)
    print(out, out.stat().st_size // 1024, "KB")


if __name__ == "__main__":
    main()
