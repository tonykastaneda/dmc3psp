"""Export em008's native V timelines, E sprites and TIM2 sheets (PNST chunk 24)."""
from prepare_skinned_model import pnst_entries, indexed_texture, array_lines
from tim2 import decode_picture


def write_effects(pack, output):
    names, data = pnst_entries(pack[24])
    chunks = pnst_entries(data)
    records, index = {}, 0
    for line in names.decode('ascii', errors='replace').splitlines():
        fields = line.split()
        if len(fields) != 2 or fields[0] not in 'GVEPTAM':
            continue
        kind, number = fields[0], int(fields[1])
        records[kind, number] = chunks[index]
        index += 2 if kind == 'M' else 1
    if index != len(chunks):
        raise ValueError('Effect ID list does not account for every PNST child')
    lines = ['#pragma once', '/* Original em008 effect data; see docs/VANGUARD_EFFECTS.md. */']
    for number in (25, 48):
        raw = records['T', number]
        _, rgba, picture = decode_picture(raw, 0x80 if raw[5] == 1 else 0x10)
        pixels, palette = indexed_texture(dict(picture, rgba=rgba))
        lines += [f'static const unsigned char vg_fx_pixels_{number}[] __attribute__((aligned(16))) = {{',
                  array_lines(pixels, str, 24), '};',
                  f'static const unsigned int vg_fx_palette_{number}[256] __attribute__((aligned(16))) = {{',
                  array_lines(palette, lambda p: f'0x{p:08x}u', 8), '};']
    for kind, numbers in (('V', (702, 499, 516)), ('E', (796, 1193, 1311, 1312)), ('A', (117,))):
        for number in numbers:
            raw = records[kind, number]
            lines += [f'static const unsigned char vg_fx_{kind}{number}[] = {{',
                      array_lines(raw, str, 24), '};']
            (output / f'vg_fx_{kind}{number}.bin').write_bytes(raw)
    for kind, number in (('P',312),('E',797)):
        (output / f'vg_fx_{kind}{number}.bin').write_bytes(records[kind,number])
    (output / 'vanguard_fx_runtime.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
