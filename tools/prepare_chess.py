"""Export CEm017's per-piece waits and alternate-command odds from the owner's disc."""
import argparse
import struct
from pathlib import Path
from dmc3_image import Iso9660, AfsArchive
from gen_enemy_ai import Elf

def generate(iso_path, output):
    with Iso9660(iso_path) as iso:
        elf=Elf(iso.read_file('SLUS_213.61',0,iso.files['SLUS_213.61']['size']))
        entry=AfsArchive(iso,'GDATA.AFS').entries[127]
        from prepare_skinned_model import pac_list
        pack=pac_list(iso.read_file('GDATA.AFS',entry['offset'],entry['size']))
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
    lines+=['};', 'static const float chess_bishop_attack[3] = {%ff,%ff,%d};' % (elf.real(0x5c0b54),elf.real(0x5c0b6c),elf.word(0x5c0b68))]
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
    from prepare_vergil import compile_scripts
    caps=[]
    for at in range(0,len(pack[10]),4):
        kind,bone,index=struct.unpack_from('<BBH',pack[10],at)
        xyz=struct.unpack_from('<3f',pack[11],index*80+16)
        radius=struct.unpack_from('<f',pack[11],index*80+32)[0]
        caps.append((kind,bone,*xyz,radius))
    ops,starts=compile_scripts(pack[9],caps,table_end=len(pack[9]))
    translated=[];offsets=[]
    for state in range(14):
        offsets.append(len(translated))
        end=starts[0][state+1] if state<13 else len(ops)
        translated += [op for op in ops[starts[0][state]:end] if op[0]<5]
    # The final script has no WAIT: native action13 owns its completion. Add a safe stop.
    translated.append((1,1,1,0))
    data=['/* Local Knight scripts, caps and attack records. */','#pragma once',
          'static const EnemyAiOp cn_ops[] = {'+','.join('{%d,%d,%d,%d}'%op for op in translated)+'};',
          'static const short cn_starts[14] = {'+','.join(map(str,offsets))+'};',
          'static const EnemyAiCap cn_caps[] = {'+','.join('{%d,%d,%ff,%ff,%ff,%ff}'%c for c in caps)+'};',
          'static const ArAttack cn_attacks[2] = {'+','.join('{%ff,%ff,%d}'%(elf.real(0x5c0770+i*64+4),elf.real(0x5c0770+i*64+28),elf.word(0x5c0770+i*64+24)) for i in range(2))+'};',
          'static const float cn_hop[4] = {'+','.join('%ff'%elf.real(0x5c2c30+i*4) for i in range(4))+'};',
          'static const unsigned char cn_turn[14] = {'+','.join(str(elf.byte(0x5c08b0+i*52)) for i in range(14))+'};']
    output.with_name('chess_knight_runtime.h').write_text('\n'.join(data)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('iso',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();generate(a.iso,a.output)
