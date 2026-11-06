"""Validate a completed PPSSPP Cerberus combat run from game-written evidence."""
from pathlib import Path
import argparse
import re

def check(folder):
    log = (folder / 'CERBERUS_LOG.TXT').read_text()
    for event in ('spawn', 'armour_break', 'phase1', 'phase2', 'chain_break', 'chains_off', 'head_death', 'death'):
        if not re.search(rf'event={event}\b', log):
            raise RuntimeError(f'Missing Cerberus event: {event}')
    for move in re.findall(r'event=move\b.*?move=(-?\d+)', log):
        if int(move) not in (*range(10), 11):
            raise RuntimeError(f'Invalid attack ID: {move}')
    if not re.search(r'event=death\b.*hp=0/0/0/0\b', log):
        raise RuntimeError('Death did not exhaust all four health pools')
    if log.count('event=head_death ') != 3:
        raise RuntimeError('Expected three head deaths')
    clear_log = (folder / 'VANGUARD_LOG.TXT').read_text()
    if 'event=mission_clear' not in clear_log:
        raise RuntimeError('Mission Clear was not reached')
    image = folder / 'MENU_CLEAR.BMP'
    if image.stat().st_size != 54 + 480 * 272 * 3:
        raise RuntimeError('Mission Clear framebuffer is incomplete')
    print('PASS: armour, both phase transitions, chains, three head deaths, zero health, Mission Clear and complete framebuffer')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    check(parser.parse_args().folder)
