/* EventTbl13 / CStageSetCrystal layout 1. Independent beam walker and native
 * stage meshes; grid and directions from 5be9d0, placement from 4206c0.
 * Durability, beam rendering and reset-switch charging are approximations. */
#include "mission13.h"
#include "enemy.h"
#include "stage.h"
#include "fx.h"
#include "swizzle.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "mission13_runtime.h"
static int room,difficulty,solved,ready,beam_count,return_at,return_count,return_alive;
static unsigned int broken;
static float switch_charge,return_wait;
static const M13Op *return_ops;
static struct {float a[3],b[3];} beams[24];
static M13Vertex *mesh_vertices[5][8];
static void *textures[M13_TEXTURE_COUNT];
static void node_pos(int n,float p[3]){p[0]=1700+m13_nodes[n].x*200;p[1]=164;p[2]=299+m13_nodes[n].z*200;}
static int trace_beam(void){
 static const signed char dx[8]={0,1,1,1,0,-1,-1,-1},dz[8]={1,1,0,-1,-1,-1,0,1};
 int current=0,step,dir;unsigned int seen=0;beam_count=0;
 for(step=0;step<24;++step){
  int x=m13_nodes[current].x,z=m13_nodes[current].z,j,hit=-1;
  if(seen&(1u<<current))return 0;
  seen|=1u<<current;
  dir=m13_nodes[current].direction;
  for(;;){x+=dx[dir];z+=dz[dir];if(x<0||x>=20||z<0||z>=20)break;
   for(j=0;j<M13_NODE_COUNT;++j)if(!(broken&(1u<<j))&&m13_nodes[j].x==x&&m13_nodes[j].z==z){hit=j;break;}
   if(hit>=0)break;
  }
  if(beam_count<24){node_pos(current,beams[beam_count].a);beams[beam_count].a[1]+=80;
   beams[beam_count].b[0]=1700+x*200;beams[beam_count].b[1]=244;beams[beam_count].b[2]=299+z*200;++beam_count;}
  if(hit<0 || m13_nodes[hit].kind==0 || m13_nodes[hit].kind==3)return 0;
  if(m13_nodes[hit].kind==1)return 1;
  current=hit;
 }
 return 0;
}
static int init(void){
 int i,j;if(ready)return 1;
 for(i=0;i<5;++i)for(j=0;j<m13_meshes[i].count;++j){const M13Batch *b=&m13_meshes[i].batches[j];mesh_vertices[i][j]=memalign(16,sizeof(M13Vertex)*b->count);if(!mesh_vertices[i][j])return 0;memcpy(mesh_vertices[i][j],b->vertices,sizeof(M13Vertex)*b->count);}
 for(i=0;i<M13_TEXTURE_COUNT;++i){const Dmc3Texture*t=&m13_textures[i];int bytes=t->width*t->height;textures[i]=memalign(16,bytes);if(!textures[i])return 0;memcpy(textures[i],t->rgba,bytes);if(!swizzle_texture8(textures[i],t->width,t->height))return 0;}
 ready=1;sceKernelDcacheWritebackAll();return 1;
}
void mission13_reset(int d){difficulty=d&1;room=-1;solved=0;broken=0;switch_charge=0;return_ops=0;return_count=return_at=0;return_wait=0;}
void mission13_enter(int number,int previous){
 room=number;return_ops=0;return_at=return_count=0;return_wait=0;return_alive=-1;
 if(number==233){init();trace_beam();if(solved)stage_hide_group(129);}
 if(number==241 && (previous==233 || previous==241)){
  return_ops=difficulty?m13_return_1:m13_return_0;return_count=difficulty?M13_RETURN_COUNT_1:M13_RETURN_COUNT_0;
 }
}
int mission13_returning(void){return return_ops!=0;}
int mission13_busy(void){return return_ops && (return_at<return_count || return_wait>0);}
int mission13_open(void){return solved;}
unsigned int mission13_broken(void){return broken;}
void mission13_update(float frames){
 int i;if(return_ops){
  if(return_wait>0){if(return_alive>=0 && enemy_alive_count()>return_alive)return;return_wait-=frames;if(return_wait>0)return;return_alive=-1;}
  while(return_at<return_count){const M13Op*o=&return_ops[return_at++];
   if(o->kind==0){enemy_spawn_height(o->c);enemy_add_kind(enemy_kind_for_class(o->a),o->b,o->d);}
   else {return_wait=o->kind==1?o->a:o->b;return_alive=o->kind==2?o->a:-1;break;}
  }
 }
 if(room!=233)return;
 if(!solved && trace_beam()){solved=1;stage_hide_group(129);}
 for(i=0;i<beam_count;++i)fx_tracer(beams[i].a,beams[i].b,0xffa0ffffu,5);
}
void mission13_hit(float x,float y,float z,float yaw,float reach,int kind){
 int i;float dx=x-2102,dz=z-1685;
 if(room!=233 || solved)return;
 if(dx*dx+dz*dz<(reach+70)*(reach+70) && fabsf(y-150)<220){
  switch_charge+=kind==HIT_LIGHT||kind==HIT_AIR?42:84;
  if(switch_charge>=120){switch_charge=0;broken=0;trace_beam();}
  return;
 }
 for(i=0;i<M13_NODE_COUNT;++i){float p[3],d;
  if((broken&(1u<<i)) || m13_nodes[i].kind<2)continue;
  node_pos(i,p);dx=p[0]-x;dz=p[2]-z;d=sqrtf(dx*dx+dz*dz);
  if(d>reach+80 || fabsf(y-p[1])>230 || (d>1 && (dx*sinf(yaw)+dz*cosf(yaw))/d<.2f))continue;
  broken|=1u<<i;fx_hit_spark(p);
 }
 if(trace_beam()){solved=1;stage_hide_group(129);}
}
void mission13_push_out(float*x,float*z,float y,float radius){
 int i;if(room!=233)return;
 for(i=0;i<M13_NODE_COUNT;++i)if(!(broken&(1u<<i))){float p[3],dx,dz,d,r;node_pos(i,p);if(y>p[1]+230 || y+180<p[1]-100)continue;
  dx=*x-p[0];dz=*z-p[2];d=sqrtf(dx*dx+dz*dz);r=radius+(m13_nodes[i].kind==3?120:60);
  if(d>=r)continue;
  if(d<1){dx=0;dz=1;d=1;}*x=p[0]+dx/d*r;*z=p[2]+dz/d*r;
 }
}
void mission13_draw(void){
 int i,j;if(room!=233 || !ready)return;
 sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_LIGHTING);sceGuEnable(GU_ALPHA_TEST);sceGuColor(0xffffffffu);
 sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexMode(GU_PSM_T8,0,0,1);sceGuClutMode(GU_PSM_8888,0,255,0);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGumMatrixMode(GU_MODEL);
 for(i=0;i<=M13_NODE_COUNT;++i)if((i==M13_NODE_COUNT && !solved) || (i<M13_NODE_COUNT && !(broken&(1u<<i)))){float p[3];ScePspFVector3 at;int sw=i==M13_NODE_COUNT;
  int model=sw?4:m13_nodes[i].kind==0?1:m13_nodes[i].kind==1?2:m13_nodes[i].kind==2?0:3;
  if(sw){p[0]=2102;p[1]=150;p[2]=1685;}else node_pos(i,p);
  at.x=p[0];at.y=p[1];at.z=p[2];sceGumLoadIdentity();sceGumTranslate(&at);if(!sw)sceGumRotateY(m13_nodes[i].direction*.78539816f);
  for(j=0;j<m13_meshes[model].count;++j){const M13Batch*b=&m13_meshes[model].batches[j];const Dmc3Texture*t=&m13_textures[b->texture];sceGuClutLoad(32,m13_palettes[b->texture]);sceGuTexImage(0,t->width,t->height,t->width,textures[b->texture]);sceGuTexFlush();sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,b->count,0,mesh_vertices[model][j]);}
 }
}
