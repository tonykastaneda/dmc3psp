#!/usr/bin/env python3
"""M13 native crystal meshes, textures and layout, from the user's disc."""
import argparse,struct
from pathlib import Path
from dmc3_image import Iso9660,AfsArchive
from prepare_skinned_model import pac_list,array_lines,c_float,indexed_texture,decode_textures
from gimmicks import model_triangles
from gen_enemy_ai import Elf
from gen_rooms import parse_groups,compile_script

def generate(path,out):
 with Iso9660(path) as iso:
  e=AfsArchive(iso,'GDATA.AFS').entries[644];s=pac_list(iso.read_file('GDATA.AFS',e['offset'],e['size']))
  elf=Elf(iso.read_file('SLUS_213.61',0,iso.files['SLUS_213.61']['size']))
  e=AfsArchive(iso,'GDATA.AFS').entries[677];return_groups=parse_groups(pac_list(iso.read_file('GDATA.AFS',e['offset'],e['size'])))
 props=pac_list(s[5]);meshes=[model_triangles(props[i]) for i in [0,10,20,30,60]]
 used=sorted({t for m in meshes for tri,t in m});texture_map={t:i for i,t in enumerate(used)}
 lines=['/* Generated native M13 crystal data. */','#pragma once',
 'typedef struct {float u,v;unsigned int color;float x,y,z;} M13Vertex;',
 'typedef struct {const M13Vertex *vertices;int count,texture;} M13Batch;',
 'typedef struct {const M13Batch *batches;int count;} M13Mesh;']
 table=[]
 for model,m in enumerate(meshes):
  batches=[]
  for tex in sorted({t for tri,t in m}):
   vs=[]
   for tri,t in m:
    if t!=tex:continue
    for x,y,z,u,v,r,g,b in tri:
     color=0xff000000|sum(min(255,int(max(0,c)*2))<<(8*k) for k,c in enumerate([r,g,b]))
     vs.append('{%s,%s,0x%08xu,%s,%s,%s}'%(c_float(u),c_float(v),color,c_float(x),c_float(y),c_float(z)))
   name=f'm13_{model}_{tex}'
   lines += [f'static const M13Vertex {name}[] = {{',array_lines(vs,str,2),'};']
   batches.append('{%s,%d,%d}'%(name,len(vs),texture_map[tex]))
  lines += [f'static const M13Batch m13_{model}_batches[] = {{'+','.join(batches)+'};']
  table.append('{m13_%d_batches,%d}'%(model,len(batches)))
 lines += ['static const M13Mesh m13_meshes[] = {'+','.join(table)+'};']
 alltex=decode_textures(s[1]);textures=[alltex[t] for t in used]
 for i,t in enumerate(textures):
  pix,pal=indexed_texture(t)
  lines += [f'static const unsigned char m13_tex_{i}[] = {{',array_lines(pix,str,24),'};',
   f'static const unsigned int m13_pal_{i}[256] __attribute__((aligned(16))) = {{',array_lines(pal,lambda c:f'0x{c:08x}u',8),'};']
 lines += [f'#define M13_TEXTURE_COUNT {len(textures)}','static const Dmc3Texture m13_textures[] = {'+','.join(f"{{{t['width']},{t['height']},m13_tex_{i}}}" for i,t in enumerate(textures))+'};',
 'static const unsigned int *const m13_palettes[] = {'+','.join(f'm13_pal_{i}' for i in range(len(textures)))+'};']
 addr=elf.word(0x5bef44);nodes=[]
 for i in range(24):
  node=struct.unpack_from('<4h',elf.data,elf._at(addr+i*8))
  if node[0]==4:break
  nodes.append(node)
 lines += ['typedef struct {short kind,direction,x,z;} M13Node;',f'#define M13_NODE_COUNT {len(nodes)}',
 'static const M13Node m13_nodes[] = {'+','.join('{'+','.join(map(str,n))+'}' for n in nodes)+'};']
 lines += ['typedef struct {short kind,a,b,c,d;} M13Op;']
 for diff in range(2):
  ops=compile_script(return_groups,[1],diff)
  lines += [f'static const M13Op m13_return_{diff}[] = {{'+','.join('{'+','.join(map(str,op))+'}' for op in ops)+'};',f'#define M13_RETURN_COUNT_{diff} {len(ops)}']
 out.mkdir(parents=True,exist_ok=True);(out/'mission13_runtime.h').write_text('\n'.join(lines)+'\n')
 print('M13 crystals:',len(nodes),'native nodes,',len(textures),'textures; node table',hex(addr))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('iso',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();generate(a.iso,a.output)
