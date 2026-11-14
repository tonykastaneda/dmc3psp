#!/usr/bin/env python3
"""Mission 8's eye seals, mouth mesh and acid collision, from the user's disc."""
import argparse
import struct
from pathlib import Path
from dmc3_image import Iso9660,AfsArchive
from prepare_skinned_model import pac_list,build_mesh,array_lines,decode_textures,indexed_texture,c_float
from mod_skeleton import parse_skeleton
from gimmicks import parse_game


def generate(iso_path,output):
    with Iso9660(iso_path) as iso:
        afs=AfsArchive(iso,'GDATA.AFS')
        def stage(room):
            e=afs.entries[680+4*(room-300)]
            return pac_list(iso.read_file('GDATA.AFS',e['offset'],e['size']))
        stomach=stage(300);props=pac_list(stomach[5])
        lines=['/* Generated from the user disc by prepare_mission8.py. */','#pragma once',
               'typedef struct {float u,v,nx,ny,nz,x,y,z;} M8Vertex;',
               'typedef struct {const M8Vertex *vertices;const unsigned short *indices;const Dmc3ModelObject *objects;int vertex_count,index_count,object_count;} M8Mesh;']
        table=[]
        for name,chunk in (('eye',70),('mouth',90)):
            raw=b'MOD '+props[chunk][4:];skel=parse_skeleton(raw);mesh=build_mesh(raw,skel['count'])
            vertices=[tuple(uv)+tuple(n)+tuple(p) for p,n,uv in zip(mesh['positions'],mesh['normals'],mesh['uvs'])]
            fmt=lambda v:'{'+','.join(c_float(x) for x in v)+'}'
            lines += [f'static const M8Vertex m8_{name}_vertices[] = {{',array_lines(vertices,fmt,2),'};',
                      f'static const unsigned short m8_{name}_indices[] = {{',array_lines(mesh['indices'],str,16),'};',
                      f'static const Dmc3ModelObject m8_{name}_objects[] = {{',array_lines(mesh['objects'],lambda o:'{%d,%d,%d}'%(o['index_start'],o['index_count'],o['texture'])),'};']
            table.append('{m8_%s_vertices,m8_%s_indices,m8_%s_objects,%d,%d,%d}'%(name,name,name,len(vertices),len(mesh['indices']),len(mesh['objects'])))
        lines+=['static const M8Mesh m8_meshes[] = {'+','.join(table)+'};']
        textures=decode_textures(stomach[1])
        for i,tx in enumerate(textures):
            pixels,palette=indexed_texture(tx)
            lines += [f'static const unsigned char m8_texture_{i}[] = {{',array_lines(pixels,str,24),'};',
                      f'static const unsigned int m8_palette_{i}[256] __attribute__((aligned(16))) = {{',array_lines(palette,lambda x:f'0x{x:08x}u',8),'};']
        lines += [f'#define M8_TEXTURE_COUNT {len(textures)}','static const Dmc3Texture m8_textures[] = {'+','.join(f"{{{tx['width']},{tx['height']},m8_texture_{i}}}" for i,tx in enumerate(textures))+'};',
                  'static const unsigned int *const m8_palettes[] = {'+','.join(f'm8_palette_{i}' for i in range(len(textures)))+'};']
        eyes=sorted((s for s in parse_game(stomach[4].decode('latin1')) if s['kind']=='BREAK' and s['number'] in (120,130,140,150,160)),key=lambda s:s['number'])
        lines += ['typedef struct {int number;float pos[3],rot[3];} M8Eye;',
                  'static const M8Eye m8_eyes[5] = {'+','.join('{%d,{%s},{%s}}'%(s['number'],','.join(c_float(x) for x in s['pos']),','.join(c_float(x) for x in s['rot'])) for s in eyes)+'};',
                  'typedef struct {short room;float v[9];} M8Acid;']
        triangles=[]
        for room in range(300,314):
            b=stage(room)[3];count=struct.unpack_from('<I',b,0x38)[0];base=struct.unpack_from('<I',b,0x40)[0]+8
            for i in range(count):
                at=base+i*56;flags=struct.unpack_from('<I',b,at)[0];ny=struct.unpack_from('<f',b,at+44)[0]
                if flags&255==13 and ny>=.5:
                    v=struct.unpack_from('<9f',b,at+4)
                    triangles.append('{%d,{%s}}'%(room,','.join(c_float(x) for x in v)))
        lines += [f'#define M8_ACID_COUNT {len(triangles)}','static const M8Acid m8_acid[] = {'+','.join(triangles)+'};']
    output.mkdir(parents=True,exist_ok=True)
    (output/'mission8_runtime.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    print(f'Mission 8: {len(eyes)} eye seals, mouth, {len(triangles)} authored acid triangles')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('iso',type=Path);ap.add_argument('--output',required=True,type=Path)
    args=ap.parse_args();generate(args.iso,args.output)
