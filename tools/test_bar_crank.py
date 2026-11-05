"""PPSSPP regression for Bullseye's switch, seal, doorway, and return visit.

Run after build_testroom.ps1. --build also regenerates the room and rebuilds.
The scene places Dante for individual interactions; it does not certify the full mission route.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    subprocess.run(args, cwd=ROOT, check=True)


def scene(mode):
    common = ['turbo 0', 'realdt 0', 'menu 1', 'movie 1', 'startroom 3 0',
              'mkey cross 30', 'mkey right 60', 'mkey right 65',
              'mkey cross 110', 'mkey cross 160']
    if mode == 'flow':
        lines = ['frames 1000'] + common + [
            'place 5 2300 0 1900 180', 'walkto 2300 1300 10 75', 'gotoroom 80 4',
            'place 110 1800 0 1950 -90', 'place 650 2300 0 1900 180',
            'walkto 2300 1300 660 740', 'circle 738 740', 'gotoroom 820 3',
            'gotoroom 880 4', 'psp_shot 70', 'psp_shot 620', 'psp_shot 800']
        lines += [f'triangle {i} {i+2}' for i in range(120, 640, 12)]
    elif mode == 'access':
        lines = ['frames 1100'] + common + ['place 5 2100 0 1950 -90',
            'walkto 1780 1950 150 450', 'psp_shot 100', 'psp_shot 400',
            'psp_shot 800', 'psp_shot 1050']
        lines += [f'triangle {i} {i+2}' for i in range(30, 790, 12)]
    else:
        lines = ['frames 1000'] + common + ['place 5 1800 0 1950 -90',
                                         'psp_shot 350', 'psp_shot 950']
        lines += [f'triangle {i} {i+2}' for i in range(30, 340, 12)]
        lines += [f'square {i} {i+2}' for i in range(650, 940, 15)]
    return '\n'.join(lines) + '\n'


def verify(out, mode):
    states = [dict((k, float(v)) for k, v in re.findall(r'(\w+)=(-?[\d.]+)', line))
              for line in (out / 'SEAL_LOG.TXT').read_text().splitlines()]
    assert states, 'No switch telemetry'
    if mode == 'flow':
        rows = {int(r[0]): r for r in (x.split() for x in (out / 'CMP_OUT.TXT').read_text().splitlines())}
        assert min(float(rows[i][3]) for i in range(20, 76)) >= 1780, 'Closed seal did not stop walking'
        assert rows[100][-3] == '3', 'Door opened before the switch was lit'
        assert float(rows[735][3]) < 1500, 'Invisible wall still blocks the opened corridor'
        assert rows[800][-3] == '4', 'Walking and pressing Circle did not enter Love Planet'
        assert rows[830][-3] == '3' and rows[900][-3] == '4', 'Return visit did not retain the unlocked door'
        assert any(s['t'] == 1 and s['lit'] == 1 for s in states), 'Switch did not stay lit on return'
    elif mode == 'access':
        rows = [x.split() for x in (out / 'CMP_OUT.TXT').read_text().splitlines()]
        assert float(rows[450][1]) < 1800, 'Dart machines still block access to the switch'
        assert states[-1]['lit'] == 1, 'Walking in and striking did not light the switch'
    else:
        assert max(s['flames'] for s in states) > 70, 'Scene never charged the flames'
        assert not any(s['lit'] for s in states), 'Partial charge unexpectedly opened the seal'
        assert states[-1]['charge'] == states[-1]['flames'] == 0, 'Charge did not drain or gunfire recharged it'
    print(f'PASS: bar crank {mode}; evidence: {out}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--mode', choices=('flow', 'decay', 'access', 'all'), default='all')
    args = parser.parse_args()
    if args.build:
        iso = next(ROOT.glob('*.iso'))
        run([sys.executable, 'tools/prepare_stage.py', str(iso), '--slot', '284',
             '--output', 'build/testroom/ROOM_003.BIN'])
        run([sys.executable, 'tools/gen_rooms.py', str(iso), '--output', 'build/testroom',
             '--header', 'build/testroom/generated/rooms_runtime.h'])
        run(['pwsh', '-NoProfile', '-File', 'tools/build_testroom.ps1'])
    for mode in ('flow', 'decay', 'access') if args.mode == 'all' else (args.mode,):
        out = ROOT / 'build' / 'tests' / f'bar_crank_{mode}'
        out.mkdir(parents=True, exist_ok=True)
        script = out / 'scene.txt'
        script.write_text(scene(mode), encoding='ascii')
        run(['pwsh', '-NoProfile', '-File', 'tools/test_scene.ps1', '-Script', str(script),
             '-Out', str(out), '-TimeoutSeconds', '240'])
        verify(out, mode)


if __name__ == '__main__':
    main()
