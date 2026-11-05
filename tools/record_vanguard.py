"""Capture every PPSSPP game framebuffer at 60 simulation frames/second.

This is deterministic frame capture, not a measurement of emulator throughput.
Only preliminary minion waves are skipped; regular gun input defeats Vanguard.
"""
import argparse
from pathlib import Path
import subprocess

from test_vanguard import ROOT, scene, verify


def encode(folder, output, ffmpeg):
    frames = sorted(folder.glob('SHOT_*.BMP'), key=lambda p: int(p.stem.split('_')[1]))
    numbers = [int(p.stem.split('_')[1]) for p in frames]
    if not numbers or numbers != list(range(180, numbers[-1] + 1)):
        raise RuntimeError('Recording has missing frames')
    # Each input image represents one distinct 1/60-second simulation step.
    subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-framerate', '60',
                    '-start_number', '180', '-i', str(folder / 'SHOT_%d.BMP'),
                    '-i', str(folder / 'MENU_CLEAR.BMP'), '-filter_complex',
                    '[0:v]setpts=PTS-STARTPTS[fight];'
                    '[1:v]loop=loop=119:size=1:start=0,setpts=N/(60*TB),fps=60[clear];'
                    '[fight][clear]concat=n=2:v=1:a=0,scale=960:544:flags=neighbor[v]',
                    '-map', '[v]', '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                    '-crf', '18', '-movflags', '+faststart', str(output)], check=True)
    print(f'{len(frames)} distinct fight frames at 60 fps: {output}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--encode-only', action='store_true')
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/bin/ffmpeg.exe')
    parser.add_argument('--folder', type=Path, default=ROOT / 'build/recordings/vanguard60')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/testroom/vanguard_boss_fight_60fps.mp4')
    args = parser.parse_args()
    folder = args.folder.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    if not args.encode_only:
        if list(folder.glob('SHOT_*.BMP')):
            raise RuntimeError('Choose a fresh recording directory before recording again')
        lines = [line for line in scene('clear').splitlines() if not line.startswith('psp_shot')]
        lines += ['fps 60', 'psp_video 180 2799']
        script = folder / 'scene.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='ascii')
        subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'tools/test_scene.ps1'),
                        '-Script', str(script), '-Out', str(folder),
                        '-TimeoutSeconds', '900', '-MissionClear'], cwd=ROOT, check=True)
    verify(folder, 'clear')
    encode(folder, args.output.resolve(), args.ffmpeg)


if __name__ == '__main__':
    main()
