#!/usr/bin/env python3
"""Convert the game's cutscene movies from the player's disc for the PSP's hardware JPEG decoder.

MDATA1.AFS and MDATA2.AFS hold MPEG-2 video at 512x320 with ADX audio, about 6.5 Mbit/s: too much
to play as they are. Each frame becomes a JPEG (decoded by sceJpeg) and the sound becomes IMA ADPCM.

A .MOV here, little-endian:
  header (64 bytes): 'DMV1', frames, fps x 1000, width, height, audio rate, audio frame count,
                     ADPCM block bytes, index offset, then zeros
  blocks: [ADPCM block][JPEG], one pair per frame, 2048-byte aligned so reads stay on sector boundaries
  index: {u32 offset, u32 ADPCM bytes, u32 JPEG bytes} per frame

Needs ffmpeg. Usage: prepare_movies.py game.iso --output build/testroom/MOVIES [names...]
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660

FFMPEG = r"C:\ffmpeg\bin\ffmpeg.exe"
# name: (archive, entry). Which is which was found by looking at them: docs/GAME_FLOW.md.
MOVIES = {
    "OPENING": ("MDATA2.AFS", 0),
    "M1_INTRO": ("MDATA2.AFS", 1),
    "M2_INTRO": ("MDATA1.AFS", 13),
}
WIDTH, HEIGHT = 480, 272       # the whole screen; sceJpeg rejects widths that aren't multiples of 16
FPS = 30
JPEG_QUALITY = 6               # ffmpeg's scale: lower is better, 2-31
AUDIO_RATE = 44100
ALIGN = 2048

STEP_TABLE = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
              97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
              724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
              4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
              18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_encode(samples):
    """samples: stereo interleaved s16. Returns one nibble per sample, left in the low nibble."""
    out = bytearray(len(samples) // 2 + 1)
    predicted = [0, 0]
    index = [0, 0]
    for i, sample in enumerate(samples):
        channel = i & 1
        step = STEP_TABLE[index[channel]]
        delta = sample - predicted[channel]
        nibble = 0
        if delta < 0:
            nibble = 8
            delta = -delta
        change = step >> 3
        if delta >= step:
            nibble |= 4
            delta -= step
            change += step
        step >>= 1
        if delta >= step:
            nibble |= 2
            delta -= step
            change += step
        step >>= 1
        if delta >= step:
            nibble |= 1
            change += step
        predicted[channel] += -change if nibble & 8 else change
        predicted[channel] = max(-32768, min(32767, predicted[channel]))
        index[channel] = max(0, min(88, index[channel] + INDEX_TABLE[nibble & 7]))
        out[i >> 1] |= nibble << (4 * channel)
    return bytes(out)


def convert(name, source, output, seconds=None):
    limit = ["-t", str(seconds)] if seconds else []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        mpeg = scratch / "movie.mpg"
        mpeg.write_bytes(source)
        print(f"{name}: {len(source) / 1e6:.0f} MB in", flush=True)
        # The source is 4:3: fill the width and crop top and bottom evenly, as the HD Collection does.
        scale = f"scale={WIDTH}:360:flags=lanczos,crop={WIDTH}:{HEIGHT},fps={FPS}"
        subprocess.run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error", "-i", str(mpeg), *limit, "-an", "-vf", scale,
                        "-c:v", "mjpeg", "-q:v", str(JPEG_QUALITY), "-pix_fmt", "yuvj420p", "-f", "image2pipe",
                        str(scratch / "video.mjpeg")], check=True)
        subprocess.run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error", "-i", str(mpeg), *limit, "-vn", "-map", "0:a:0",
                        "-ac", "2", "-ar", str(AUDIO_RATE), "-f", "s16le", str(scratch / "audio.raw")], check=True)
        video = (scratch / "video.mjpeg").read_bytes()
        pcm = (scratch / "audio.raw").read_bytes()
    jpegs, at = [], 0
    while True:
        start = video.find(b"\xff\xd8", at)
        if start < 0:
            break
        end = video.find(b"\xff\xd9", start + 2)
        if end < 0:
            break
        jpegs.append(video[start:end + 2])
        at = end + 2
    frames = len(jpegs)
    samples = struct.unpack(f"<{len(pcm) // 2}h", pcm[:len(pcm) // 2 * 2])
    adpcm = ima_encode(samples)
    per_frame = AUDIO_RATE / FPS            # stereo sample pairs, one byte each
    with open(output, "wb") as f:
        f.write(bytes(64))
        index = []
        for i, jpeg in enumerate(jpegs):
            a = int(i * per_frame)
            b = int((i + 1) * per_frame) if i + 1 < frames else len(adpcm)
            block = adpcm[a:b]
            offset = f.tell()
            f.write(block)
            f.write(jpeg)
            f.write(bytes(-f.tell() % ALIGN))
            index.append((offset, len(block), len(jpeg)))
        index_at = f.tell()
        for row in index:
            f.write(struct.pack("<3I", *row))
        f.seek(0)
        f.write(struct.pack("<4s8I", b"DMV1", frames, FPS * 1000, WIDTH, HEIGHT, AUDIO_RATE, len(adpcm), 0, index_at))
    size = output.stat().st_size
    print(f"{name}: {frames} frames, {frames / FPS:.0f} s, {size / 1e6:.0f} MB, {size * FPS / frames / 1e3:.0f} KB/s, "
          f"biggest frame {max(len(j) for j in jpegs) // 1024} KB", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--seconds", type=int, help="only the start of each, for quick tests")
    ap.add_argument("names", nargs="*", default=list(MOVIES))
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with Iso9660(args.iso) as iso:
        for name in args.names or MOVIES:
            archive, entry = MOVIES[name]
            e = AfsArchive(iso, archive).entries[entry]
            convert(name, iso.read_file(archive, e["offset"], e["size"]), args.output / f"{name}.MOV", args.seconds)


if __name__ == "__main__":
    sys.exit(main())
