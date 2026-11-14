#include "mission8.h"
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
#include "mission8_runtime.h"

/* EventTbl08 room 300: five BREAK eyes set flags 1..5; counter 5 opens
 * the fang seal at five. The model is native; progressive fang poses and
 * blinking remain approximate. Each eye currently takes three light hits. */
static int current_room,eye_hp[5],render_ready;
static float acid_wait;
static M8Vertex *vertices[2];
static unsigned short *indices[2];
static void *textures[M8_TEXTURE_COUNT];

void mission8_reset(void) {
    int i;for(i=0;i<5;++i)eye_hp[i]=3;
    current_room=-1;acid_wait=0;
}
int mission8_stomach_open(void) {
    int i;for(i=0;i<5;++i)if(eye_hp[i]>0)return 0;
    return 1;
}
unsigned int mission8_eye_bits(void) {
    unsigned int bits=0;int i;
    for(i=0;i<5;++i)if(eye_hp[i]<=0)bits|=1u<<i;
    return bits;
}
static int init(void) {
    int i;
    if(render_ready)return 1;
    for(i=0;i<2;++i) {
        const M8Mesh *m=&m8_meshes[i];
        vertices[i]=memalign(16,sizeof(M8Vertex)*m->vertex_count);
        indices[i]=memalign(16,sizeof(unsigned short)*m->index_count);
        if(!vertices[i]||!indices[i])return 0;
        memcpy(vertices[i],m->vertices,sizeof(M8Vertex)*m->vertex_count);
        memcpy(indices[i],m->indices,sizeof(unsigned short)*m->index_count);
    }
    for(i=0;i<M8_TEXTURE_COUNT;++i) {
        const Dmc3Texture *t=&m8_textures[i];size_t bytes=(size_t)t->width*t->height;
        textures[i]=memalign(16,bytes);if(!textures[i])return 0;
        memcpy(textures[i],t->rgba,bytes);
        if(!swizzle_texture8(textures[i],t->width,t->height))return 0;
    }
    render_ready=1;sceKernelDcacheWritebackAll();return 1;
}
void mission8_enter(int number) {
    current_room=number;acid_wait=0;
    if(number==300) {
        init();
        if(mission8_stomach_open())stage_hide_group(128); /* set 100 STORY_TAG */
    }
}
void mission8_hit(float x,float y,float z,float yaw,float reach,int kind) {
    int i;
    if(current_room!=300)return;
    for(i=0;i<5;++i) {
        const M8Eye *eye=&m8_eyes[i];
        float dx=eye->pos[0]-x,dz=eye->pos[2]-z,dy=eye->pos[1]-y,d=sqrtf(dx*dx+dz*dz);
        if(eye_hp[i]<=0 || d>reach+120 || dy< -180 || dy>220)continue;
        if(d>1 && (dx*sinf(yaw)+dz*cosf(yaw))/d<.2f)continue;
        eye_hp[i]-=kind==HIT_LIGHT||kind==HIT_AIR?1:2;
        fx_hit_spark(eye->pos);
        if(eye_hp[i]<=0) {
            eye_hp[i]=0;fx_explosion(eye->pos);
            if(mission8_stomach_open())stage_hide_group(128);
        }
    }
}
void mission8_push_out(float *x,float *z,float y,float radius) {
    int i;
    if(current_room!=300)return;
    for(i=0;i<5;++i)if(eye_hp[i]>0) {
        const M8Eye *e=&m8_eyes[i];
        float dx=*x-e->pos[0],dz=*z-e->pos[2],d=sqrtf(dx*dx+dz*dz),limit=100+radius;
        if(y>e->pos[1]+150 || y+220<e->pos[1] || d>=limit)continue;
        if(d<1) {dx=sinf(e->rot[1]*.0174532925f);dz=cosf(e->rot[1]*.0174532925f);d=1;}
        *x=e->pos[0]+dx/d*limit;*z=e->pos[2]+dz/d*limit;
    }
}
float mission8_acid(float frames,float x,float y,float z,int grounded) {
    int i;
    acid_wait=fmaxf(0,acid_wait-frames);
    if(!grounded||acid_wait>0)return 0;
    for(i=0;i<M8_ACID_COUNT;++i) {
        const M8Acid *a=&m8_acid[i];const float *v=a->v;
        float d,u,w,h;
        if(a->room!=current_room)continue;
        d=(v[5]-v[8])*(v[0]-v[6])+(v[6]-v[3])*(v[2]-v[8]);
        if(fabsf(d)<.001f)continue;
        u=((v[5]-v[8])*(x-v[6])+(v[6]-v[3])*(z-v[8]))/d;
        w=((v[8]-v[2])*(x-v[6])+(v[0]-v[6])*(z-v[8]))/d;
        if(u<-.001f||w<-.001f||u+w>1.001f)continue;
        h=u*v[1]+w*v[4]+(1-u-w)*v[7];
        if(fabsf(y-h)>35)continue;
        /* PCSX2 Normal measurement: 25 HP approximately every seven frames. */
        acid_wait=7;return 25;
    }
    return 0;
}
static void draw_model(int part) {
    const M8Mesh *m=&m8_meshes[part];int i;
    for(i=0;i<m->object_count;++i) {
        const Dmc3ModelObject *o=&m->objects[i];const Dmc3Texture *t=&m8_textures[o->texture];
        sceGuClutLoad(32,m8_palettes[o->texture]);sceGuTexImage(0,t->width,t->height,t->width,textures[o->texture]);sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_NORMAL_32BITF|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D,o->index_count,indices[part]+o->index_start,vertices[part]);
    }
}
void mission8_draw(void) {
    int i;
    if(current_room!=300||!render_ready)return;
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_LIGHTING);sceGuEnable(GU_ALPHA_TEST);
    sceGuColor(0xffffffffu);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexMode(GU_PSM_T8,0,0,1);
    sceGuClutMode(GU_PSM_8888,0,255,0);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGumMatrixMode(GU_MODEL);
    for(i=0;i<5;++i)if(eye_hp[i]>0) {
        const M8Eye *e=&m8_eyes[i];ScePspFVector3 at={e->pos[0],e->pos[1],e->pos[2]};
        sceGumLoadIdentity();sceGumTranslate(&at);
        sceGumRotateY(e->rot[1]*.0174532925f);sceGumRotateX(e->rot[0]*.0174532925f);sceGumRotateZ(e->rot[2]*.0174532925f);
        draw_model(0);
    }
    if(mission8_stomach_open()) {sceGumLoadIdentity();draw_model(1);}
}
