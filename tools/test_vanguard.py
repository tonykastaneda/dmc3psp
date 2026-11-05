"""Exercise the real Mission 2 Vanguard in PPSSPP, including victory from real gunfire.

Only the preliminary waves are accelerated/cleared by the harness. The boss is never auto-killed.
--mode melee verifies real sword input and damage to Dante; clear stops on a game-written Mission Clear.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def scene(mode, normal=False):
    lines = [f'frames {2800 if mode == "clear" else 1600 if mode == "observe" else 1000}',
             'turbo 0', 'realdt 0', 'menu 1', 'movie 1', 'fastwaves 1', 'clearminions 30',
             'mkey cross 30', 'mkey right 60', 'mkey cross 110', 'mkey cross 160']
    if normal:
        lines += ['mkey down 80']
    if mode == 'melee':
        lines += ['place 180 3200 0 2384 90', 'walkto 3436 2384 185 380', 'r1 190 999',
                  'psp_shot 230', 'psp_shot 400', 'psp_shot 900']
        lines += [f'triangle {i} {i+2}' for i in range(200, 900, 12)]
    else:
        lines += ['place 180 3100 0 2400 90', 'r1 120 2799',
                  'psp_shot 200', 'psp_shot 480', 'psp_shot 1000', 'psp_shot 1500']
        if mode == 'clear':
            lines += ['fire 6']  # Uses the regular gun input, bullet collision, and enemy_hit path.
    return '\n'.join(lines) + '\n'


def verify(out, mode):
    rows = [dict(re.findall(r'(\w+)=([^\s]+)', line))
            for line in (out / 'VANGUARD_LOG.TXT').read_text().splitlines()]
    spawns = [r for r in rows if r.get('event') == 'spawn']
    assert spawns, 'Mission 2 did not spawn Vanguard'
    spawn = spawns[0]
    assert float(spawn['hp']) == 3000, 'Incorrect initial health'
    assert abs(float(spawn['x'])-3436) < 1 and abs(float(spawn['z'])-2384) < 1, 'Wrong cinematic/ground spawn'
    start = rows.index(spawn)
    rows = rows[start:]
    assert all(r.get('hidden') != '1' for r in rows if r.get('event') == 'hurt'), 'Hidden boss took damage'
    if mode == 'clear':
        death = next((i for i,r in enumerate(rows) if r.get('event') == 'death'), None)
        cleared = next((i for i,r in enumerate(rows) if r.get('event') == 'mission_clear'), None)
        assert death is not None and cleared is not None and death < cleared, 'Missing death → Mission Clear'
        assert any(r.get('event') == 'removed' for r in rows[death:cleared+1]), 'Death animation never finished'
        assert any(r.get('event') == 'hurt' and float(r['hp']) < 3000 for r in rows[:death]), 'No real gun damage'
        assert (out / 'MENU_CLEAR.BMP').exists(), 'Missing Mission Clear framebuffer'
    else:
        assert any(r.get('event') == 'warp' for r in rows), 'Teleport never ran'
        assert any(r.get('event') == 'landed' for r in rows), 'Boss attacks never connected'
        assert any(r.get('event') == 'player_hurt' and float(r['value']) < 20000 for r in rows), 'Dante took no damage'
        if mode == 'melee':
            assert any(r.get('event') == 'hurt' and float(r['hp']) < 3000 for r in rows), 'Sword attacks did not damage Vanguard'
        else:
            assert all(float(r['hp']) == 3000 for r in rows if 'hp' in r), 'Idle observation damaged the boss'
            assert any(r.get('event') == 'strike' and r.get('action') in ('69','70') for r in rows), 'No teleport strike'
    print(f'PASS: Vanguard {mode}: {out}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--mode', choices=('observe', 'melee', 'clear', 'all'), default='all')
    parser.add_argument('--normal', action='store_true', help='Normal instead of Easy on Mission Select')
    args = parser.parse_args()
    if args.build:
        iso = next(ROOT.glob('*.iso'))
        subprocess.run([sys.executable, 'tools/gen_rooms.py', str(iso), '--output', 'build/testroom',
                        '--header', 'build/testroom/generated/rooms_runtime.h'], cwd=ROOT, check=True)
        subprocess.run(['pwsh','-NoProfile','-File','tools/build_testroom.ps1'],cwd=ROOT,check=True)
    for mode in ('observe','melee','clear') if args.mode == 'all' else (args.mode,):
        out = ROOT / 'build' / 'tests' / f'vanguard_{mode}_{"normal" if args.normal else "easy"}'
        out.mkdir(parents=True, exist_ok=True)
        script = out / 'scene.txt'
        script.write_text(scene(mode,args.normal),encoding='ascii')
        cmd = ['pwsh','-NoProfile','-File','tools/test_scene.ps1','-Script',str(script),'-Out',str(out),'-TimeoutSeconds','300']
        if mode == 'clear':
            cmd.append('-MissionClear')
        subprocess.run(cmd,cwd=ROOT,check=True)
        verify(out,mode)


if __name__ == '__main__':
    main()
