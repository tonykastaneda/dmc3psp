import sys
from pathlib import Path
sys.path.insert(0,'tools')
from prepare_hud import *
from PIL import Image,ImageDraw
with Iso9660(next(Path('.').glob('*.iso'))) as iso:
 e=AfsArchive(iso,'GDATA.AFS').entries[SLOT]; chunks=split_pac(iso.read_file('GDATA.AFS',e['offset'],e['size']))
canvas=Image.new('RGBA',(700,600),(40,40,40,255)); d=ImageDraw.Draw(canvas)
for n,i in enumerate([0,2,3,5]):
 try:
  w,h,rgba=first_texture(chunks[i]); im=Image.frombytes('RGBA',(w,h),rgba); im.thumbnail((330,250)); canvas.paste(im,(n%2*350,n//2*300),im); d.text((n%2*350,n//2*300+255),str(i),fill='white')
 except Exception as e: print(i,e)
for i in [4,6]:
 print(i,[(a,b,(min(v[2] for v in vs),max(v[2] for v in vs),min(v[3] for v in vs),max(v[3] for v in vs))) for a,b,vs in triangles(chunks[i])])
canvas.save('analysis/testroom/hud_texture_reference.png')
