"""Export Hell Vanguard's original meshes, motions, collision and AI tables for the PSP port.

This prepares assets; it does not implement or enable the boss encounter.
The robe parts retain their own rigs. Cloth simulation and the EFM effects are not baked.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from dmc3_image import Iso9660, AfsArchive
from mod_skeleton import parse_skeleton
from prepare_skinned_model import (pnst_entries, pac_list, build_mesh, decode_textures,
                                   indexed_texture, array_lines, vec)
from mot_ps2 import parse_mot
from gen_enemy_ai import Elf, read_rows, floats
from vanguard_fx import write_effects

SLOT = 120
PARTS = [('body', 1), ('robe1', 3), ('robe2', 5), ('robe3', 7), ('robe4', 9),
         ('robe5', 11), ('robe6', 13), ('robe7', 15), ('scythe', 18)]


def write_ai(pack, executable, output):
    elf = Elf(executable)
    c = pack[21]
    u16 = lambda o: struct.unpack_from('<H', c, o)[0]
    i16 = lambda o: struct.unpack_from('<h', c, o)[0]
    sb, mb = u16(0), u16(2)
    ops, starts = [], []
    mapping = elf.word(0x61a668)
    for script in range(108):
        group, index = elf.byte(mapping + script * 2), elf.byte(mapping + script * 2 + 1)
        g = sb + u16(sb + group * 2)
        if index * 2 >= u16(g) or u16(g + index * 2) == 65535:
            starts.append(-1)
            continue
        p = g + u16(g + index * 2)
        starts.append(len(ops))
        seen, attack = {}, 0
        for _ in range(128):
            seen[p] = len(ops)
            op = c[p]
            if op == 0:
                end = c[p + 4] & 1
                ops.append((1, end, i16(p + 2), 0))
                p += 6
                if end:
                    break
            elif op == 1:
                mg = mb + u16(mb + c[p + 4] * 2)
                mi = c[p + 5]
                if mi * 2 >= u16(mg) or u16(mg + mi * 2) == 65535:
                    if script not in (90, 91):
                        raise ValueError(f'Missing motion map in script {script}')
                    del ops[starts[-1]:]
                    starts[-1] = -1
                    break  # unused reserved scripts in this pack
                entry = mg + u16(mg + mi * 2)
                slot = u16(entry + 6)
                # em008 has a second BODY motion PAC, not just weapon animation.
                motion = slot if slot < 100 else 71 + slot - 200 if 200 <= slot < 209 else -1
                if motion < 0:
                    raise ValueError(f'Unknown Vanguard motion {slot} in script {script}')
                ops.append((0, motion, i16(p + 6), c[p + 3] | (c[entry + 3] == 1) << 8))
                p += 8
            elif op == 2:
                target = p + 2 - i16(p + 2)
                if target not in seen:
                    raise ValueError(f'Unknown loop target {target:#x}')
                ops.append((3, 0, seen[target], 0))
                break
            elif op in (3, 4, 0x22):
                p += 6
            elif op in (5, 0x21, 0x23):
                p += 2
            elif op in (6, 7, 8):
                if op != 8:
                    attack = c[p + 1]
                p += 2
            elif 0x10 <= op <= 0x1f:
                count = op - 15
                for k in range(count):
                    cap = c[p + 4 + k * 2]
                    if pack[22][cap * 4] == 1:
                        ops.append((2, cap, i16(p + 2), attack))
                p += 4 + count * 2
            elif op in (0x20, 0x24):
                p += 4
            else:
                raise ValueError(f'Unknown script opcode {op:#x} at {p:#x}')
        else:
            raise ValueError(f'Vanguard script {script} did not terminate')
    caps = []
    for i in range(len(pack[22]) // 4):
        kind, bone, shape = struct.unpack_from('<BBH', pack[22], i * 4)
        at = shape * 80
        xyz = struct.unpack_from('<3f', pack[23], at + 16)
        shape_kind = struct.unpack_from('<I', pack[23], at)[0]
        radius = struct.unpack_from('<f', pack[23], at + (48 if shape_kind == 4 else 32))[0]
        caps.append((kind, bone, *xyz, radius))
    bands, rows = read_rows(elf, 0x5cdd00)
    # Ordinary E&I records: gp(00620170)-7580 = pointer 00618bf0 -> 005a5a30.
    pistol = elf.word(0x618bf0)
    if elf.byte(pistol+2)!=1 or elf.byte(pistol+0x18)!=255:
        raise ValueError('Unexpected ordinary pistol damage/reaction record')
    f = lambda x: f'{x:.6f}f'
    lines = ['#pragma once', '/* SLUS_213.61 em008 grouped scripts, shapes and decision parameters. */',
             'typedef struct { short op, a, b, c; } VanguardOp;',
             'typedef struct { int kind, bone; float x,y,z,radius; } VanguardCap;',
             'static const VanguardOp vg_ops[] = {' + ','.join('{' + ','.join(map(str, o)) + '}' for o in ops) + '};',
             'static const short vg_scripts[] = {' + ','.join(map(str, starts)) + '};',
             'static const VanguardCap vg_caps[] = {' + ','.join(
                 '{%d,%d,%s}' % (k, b, ','.join(map(f, v))) for k, b, *v in caps) + '};',
             f'#define VANGUARD_HP {f(elf.real(0x5cd590 + 44))}',
             f'#define VANGUARD_PISTOL_DAMAGE {f(elf.real(pistol+4))}',
             f'#define VANGUARD_PISTOL_STRAIN {f(elf.real(pistol+0x14))}',
             f'#define VANGUARD_STRAIN_MAX {f(elf.real(0x5cd590+0x34))}',
             'static const float vg_bands[] = {' + ','.join(map(f, bands)) + '};',
             'static const float vg_zones[][3] = {' + ','.join('{' + ','.join(map(f, floats(elf, 0x5cdcb0 + i*12, 3))) + '}' for i in range(3)) + '};',
             '/* Lowest AI rank, both attack-token lists, three distance bands. */',
             'typedef struct { int count, plan[8]; float weight[8]; } VanguardPick;',
             'static const VanguardPick vg_picks[2][3] = {']
    for table in rows:
        lines.append('{' + ','.join('{%d,{%s},{%s}}' % (len(band[0]), ','.join(str(p) for p,w in band[0]),
                                                       ','.join(f(w) for p,w in band[0])) for band in table) + '},')
    lines += ['};']
    (output / 'vanguard_ai.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
    return len(ops)


def generate(iso_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Iso9660(iso_path) as iso:
        entry = AfsArchive(iso, 'GDATA.AFS').entries[SLOT]
        data = iso.read_file('GDATA.AFS', entry['offset'], entry['size'])
        executable = iso.read_file('SLUS_213.61', 0, iso.files['SLUS_213.61']['size'])
    pack = pnst_entries(data)
    lines = ['/* Generated by prepare_vanguard.py. Include model_runtime.h first. */', '#pragma once',
             'typedef struct {',
             ' const Dmc3Vec3 *positions, *normals, *local, *rest; const Dmc3Uv *uvs;',
             ' const unsigned char (*bones)[3], (*weights)[3]; const signed char *parents;',
             ' const unsigned short *indices; const Dmc3ModelObject *objects;',
             ' int vertices, indices_count, objects_count, bone_count;',
             '} VanguardMesh;']
    manifest = {'slot': SLOT, 'pack_sha256': hashlib.sha256(data).hexdigest(),
                'parts': [], 'motions': [], 'limitations': [
                    'Only lowest-rank decision rows are exported for the current runtime.',
                    'Full cloth simulation and EFM effects are not reproduced by the runtime.']}
    table = []
    triple = lambda x: '{' + ','.join(map(str, x)) + '}'
    for name, chunk in PARTS:
        raw = pack[chunk]
        skeleton = parse_skeleton(raw)
        mesh = build_mesh(raw, skeleton['count'])
        prefix = 'vanguard_' + name
        # Keep the original geometry and all robe rigs; performance reduction comes after visual checks.
        arrays = [('Dmc3Vec3', 'positions', mesh['positions'], vec),
                  ('Dmc3Vec3', 'normals', mesh['normals'], vec),
                  ('Dmc3Vec3', 'local', skeleton['local'], vec),
                  ('Dmc3Vec3', 'rest', skeleton['world'], vec),
                  ('Dmc3Uv', 'uvs', mesh['uvs'], vec),
                  ('signed char', 'parents', skeleton['parents'], str),
                  ('unsigned short', 'indices', mesh['indices'], str)]
        for ctype, field, values, fmt in arrays:
            lines += [f'static const {ctype} {prefix}_{field}[] = {{', array_lines(values, fmt), '};']
        for field in ('bones', 'weights'):
            lines += [f'static const unsigned char {prefix}_{field}[][3] = {{',
                      array_lines(mesh[field], triple), '};']
        lines += [f'static const Dmc3ModelObject {prefix}_objects[] = {{',
                  array_lines(mesh['objects'], lambda o: '{%d,%d,%d}' %
                              (o['index_start'], o['index_count'], o['texture'])), '};']
        fields = ['positions', 'normals', 'local', 'rest', 'uvs', 'bones', 'weights', 'parents', 'indices', 'objects']
        table.append('{' + ','.join(f'{prefix}_{f}' for f in fields) + ',' +
                     f"{len(mesh['positions'])},{len(mesh['indices'])},{len(mesh['objects'])},{skeleton['count']}" + '}')
        manifest['parts'].append({'name': name, 'chunk': chunk, 'signature': raw[:4].decode('ascii'),
                                  'bones': skeleton['count'], 'vertices': len(mesh['positions']),
                                  'triangles': len(mesh['indices']) // 3,
                                  'sha256': hashlib.sha256(raw).hexdigest()})
    lines += ['#define VANGUARD_PART_COUNT 9', 'static const VanguardMesh vanguard_parts[] = {' + ','.join(table) + '};']
    textures = decode_textures(pack[0])
    for i, texture in enumerate(textures):
        pixels, palette = indexed_texture(texture)
        lines += [f'static const unsigned char vanguard_texture_{i}[] = {{',
                  array_lines(pixels, str, 24), '};',
                  f'static const unsigned int vanguard_palette_{i}[256] __attribute__((aligned(16))) = {{',
                  array_lines(palette, lambda v: f'0x{v:08x}u', 8), '};']
    lines += [f'#define VANGUARD_TEXTURE_COUNT {len(textures)}',
              'static const Dmc3Texture vanguard_textures[] = {' + ','.join(
                  f"{{{t['width']},{t['height']},vanguard_texture_{i}}}" for i, t in enumerate(textures)) + '};',
              'static const unsigned int *const vanguard_palettes[] = {' + ','.join(
                  f'vanguard_palette_{i}' for i in range(len(textures))) + '};']
    (output / 'vanguard_runtime.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
    # Raw MOTs preserve the disc's spline channels and timing for motion.c, without baking frame poses.
    motion_lines, motion_table = ['#pragma once'], []
    for chunk, prefix in ((19, 'vg'), (20, 'vx')):
        for index, raw in enumerate(pac_list(pack[chunk])):
            if not raw:
                continue
            motion = parse_mot(raw)
            name = f'{prefix}-{index:02d}'
            (output / f'{name}.mot').write_bytes(raw[:motion['bytes_used']])
            n = len(motion_table)
            motion_lines += [f'static const unsigned char vg_motion_{n}[] __attribute__((aligned(4))) = {{',
                             array_lines(list(raw[:motion['bytes_used']]), str, 24), '};']
            motion_table.append(f'{{"{name}",vg_motion_{n},{motion["bytes_used"]}}}')
            manifest['motions'].append({'name': name, 'chunk': chunk, 'index': index,
                                       'end_frame': motion['end_frame'], 'loop_start': motion['loop_start'],
                                       'channels': len(motion['channels'])})
    # Retain the grouped scripts/colliders for the next translation pass; do not guess their semantics.
    for chunk, name in ((21, 'scripts'), (22, 'caps'), (23, 'shapes')):
        (output / f'{name}.bin').write_bytes(pack[chunk])
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    motion_lines += [f'#define VANGUARD_MOTION_COUNT {len(motion_table)}',
                     'static const Dmc3MotionBlob vg_motion_blobs[] = {' + ','.join(motion_table) + '};']
    (output / 'vanguard_motions.h').write_text('\n'.join(motion_lines) + '\n', encoding='ascii')
    write_ai(pack, executable, output)
    write_effects(pack, output)
    print(f"Vanguard: {len(PARTS)} parts, {len(textures)} textures, {len(manifest['motions'])} motions -> {output}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.iso, args.output)
