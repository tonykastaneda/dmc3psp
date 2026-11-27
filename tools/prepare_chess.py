"""Export CEm017's per-piece waits and alternate-command odds from the owner's disc."""
import argparse
from pathlib import Path
from dmc3_image import Iso9660
from gen_enemy_ai import Elf

def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        elf=Elf(iso.read_file('SLUS_213.61',0,iso.files['SLUS_213.61']['size']))
    waits=(0x5c10d0,0x5c0890,0x5c0c80,0x5c04b0,0x5c00d0,0x5bfd30)
    odds=(0x5c0f00,0x5c0730,0x5c0b10,0x5c0350,0x5bffb0,0x5bfc10)
    strides=(36,20,20,36,20,20)
    lines=['/* Local disc data: 409690 waits, 40ab60 alternate-command odds. */','#pragma once',
           'static const float chess_waits[6][14][4][2] = {']
    for at in waits:
        lines+=['{']
        for state in range(14):
            lines+=['{'+','.join('{%0.6ff,%0.6ff}'%(elf.real(at+state*52+rank*4),elf.real(at+state*52+rank*4+16)) for rank in range(4))+'},']
        lines+=['},']
    lines+=['};','static const float chess_alternate_odds[6][4] = {']
    for at,stride in zip(odds,strides):lines+=['{'+','.join('%0.6ff'%elf.real(at+rank*stride) for rank in range(4))+'},']
    lines+=['};']
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('iso',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();generate(a.iso,a.output)
