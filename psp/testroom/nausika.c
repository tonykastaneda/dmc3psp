#include "nausika.h"
#include "motion.h"
#include "enemy.h"
#include "swizzle.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "boss_nausika_runtime.h"
static struct { int enabled,started,node,state;float pos[3],yaw,age,timer,speed,hp,damage,stagger; } chase;
typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u,v,nx,ny,nz,x,y,z; } Vertex;
static Vertex *vertices[1];
static unsigned short *indices[1];
static void *textures[NAUSIKA_TEXTURE_COUNT];
static Transform poses[1][16];
static int actor_motion[1][8], render_ready;
static void identity(Transform *t) {
    memset(t, 0, sizeof(*t));
    t->m[0][0] = t->m[1][1] = t->m[2][2] = 1.0f;
}
static void multiply(Transform *out, const Transform *a, const Transform *b) {
    Transform t;
    int i, j;
    for (i = 0; i < 3; ++i) {
        t.p[i] = a->p[i];
        for (j = 0; j < 3; ++j) {
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
            t.p[i] += a->m[i][j] * b->p[j];
        }
    }
    *out = t;
}
static void euler(Transform *t, float x, float y, float z) {
    float cx = cosf(x), sx = sinf(x), cy = cosf(y), sy = sinf(y), cz = cosf(z), sz = sinf(z);
    identity(t);
    t->m[0][0] = cz * cy; t->m[0][1] = cz * sy * sx - sz * cx; t->m[0][2] = cz * sy * cx + sz * sx;
    t->m[1][0] = sz * cy; t->m[1][1] = sz * sy * sx + cz * cx; t->m[1][2] = sz * sy * cx - cz * sx;
    t->m[2][0] = -sy;     t->m[2][1] = cy * sx;              t->m[2][2] = cy * cx;
}
static void transform_point(const Transform *t, const float p[3], float out[3]) {
    int i;
    for (i = 0; i < 3; ++i) out[i] = t->p[i] + t->m[i][0] * p[0] + t->m[i][1] * p[1] + t->m[i][2] * p[2];
}

static void pose_actor(int part) {
    const NausikaMesh *m = &nausika_parts[part];
    MotionSample sample;
    int i,j,k,slot=0;
    float frame=chase.age;
    const Motion *motion;
    slot=chase.state==2?5:chase.state==3?3:chase.state==4?2:4;
    motion=motion_get(actor_motion[part][slot]);
    memset(&sample,0,sizeof(sample));
    if (motion) motion_sample(motion, (chase.state==2 || chase.state==3) ? fminf(frame,motion->end_frame) : motion_wrap(motion,frame),&sample);
    for (i=0;i<m->bone_count;++i) {
        Transform local;
        float r[3]={0,0,0},p[3]={m->local[i].x,m->local[i].y,m->local[i].z};
        for(k=0;k<3;++k) {
            if(sample.has_rot[i][k])r[k]=sample.rot[i][k];
            if(sample.has_trans[i][k])p[k]=sample.trans[i][k];
        }
        euler(&local,r[0],r[1],r[2]);
        memcpy(local.p,p,sizeof(p));
        if(m->parents[i]>=0)multiply(&poses[part][i],&poses[part][m->parents[i]],&local);
        else poses[part][i]=local;
    }
    for(i=0;i<m->vertices;++i) {
        float p[3]={0,0,0},n[3]={0,0,0};
        Vertex *v=&vertices[part][i];
        for(j=0;j<3;++j) {
            int b=m->bones[i][j],w=m->weights[i][j];
            const Transform *t=&poses[part][b];
            float src[3]={m->positions[i].x-m->rest[b].x,m->positions[i].y-m->rest[b].y,m->positions[i].z-m->rest[b].z};
            float ns[3]={m->normals[i].x,m->normals[i].y,m->normals[i].z};
            if(!w)continue;
            for(k=0;k<3;++k) {
                p[k]+=(t->p[k]+t->m[k][0]*src[0]+t->m[k][1]*src[1]+t->m[k][2]*src[2])*w/31.0f;
                n[k]+=(t->m[k][0]*ns[0]+t->m[k][1]*ns[1]+t->m[k][2]*ns[2])*w/31.0f;
            }
        }
        v->x=p[0];v->y=p[1];v->z=p[2];v->nx=n[0];v->ny=n[1];v->nz=n[2];
    }
    sceKernelDcacheWritebackRange(vertices[part],sizeof(Vertex)*m->vertices);
}
int nausika_init(void) {
    int i,j;
    if(render_ready)return 1;
    for(i=0;i<1;++i) {
        const NausikaMesh *m=&nausika_parts[i];
        vertices[i]=memalign(16,sizeof(Vertex)*m->vertices);
        indices[i]=memalign(16,sizeof(unsigned short)*m->indices_count);
        if(!vertices[i]||!indices[i])return 0;
        memcpy(indices[i],m->indices,sizeof(unsigned short)*m->indices_count);
        for(j=0;j<m->vertices;++j) {vertices[i][j].u=m->uvs[j].u;vertices[i][j].v=m->uvs[j].v;}
        for(j=0;j<8;++j) {char name[16];snprintf(name,sizeof(name),"ns%d-%02d",i,j);actor_motion[i][j]=motion_find(name);}
        if(actor_motion[i][0]<0)return 0;
    }
    for(i=0;i<NAUSIKA_TEXTURE_COUNT;++i) {
        const Dmc3Texture *t=&nausika_textures[i];
        size_t bytes=(size_t)t->width*t->height;
        textures[i]=memalign(16,bytes);
        if(!textures[i])return 0;
        memcpy(textures[i],t->rgba,bytes);
        if(!swizzle_texture8(textures[i],t->width,t->height))return 0;
    }
    render_ready=1;
    sceKernelDcacheWritebackAll();
    return 1;
}
void nausika_draw(void) {
    int part,i;
    if(!chase.enabled||!render_ready || chase.state==4)return;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    { ScePspFVector3 at={chase.pos[0],chase.pos[1],chase.pos[2]};sceGumTranslate(&at);sceGumRotateY(chase.yaw); }
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_LIGHTING);sceGuEnable(GU_ALPHA_TEST);
    sceGuColor(0xffffffffu);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexMode(GU_PSM_T8,0,0,1);sceGuClutMode(GU_PSM_8888,0,255,0);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    for(part=0;part<1;++part) {
        const NausikaMesh *m=&nausika_parts[part];
        pose_actor(part);
        for(i=0;i<m->objects_count;++i) {
            const Dmc3ModelObject *o=&m->objects[i];
            const Dmc3Texture *t=&nausika_textures[o->texture];
            sceGuClutLoad(32,nausika_palettes[o->texture]);
            sceGuTexImage(0,t->width,t->height,t->width,textures[o->texture]);sceGuTexFlush();
            sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_NORMAL_32BITF|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D,
                            o->index_count,indices[part]+o->index_start,vertices[part]);
        }
    }
}

/* Native path, 13 +/- 12 cm/frame chase, 750 cm attack decision,
 * 40/69-frame windup and 750 accumulated-damage stagger. Path steering and
 * hit attachment interpolation are approximated; see LEVIATHAN_SPEC.md. */
void nausika_enter(int room) {
    memset(&chase,0,sizeof(chase));
    if(room!=303 && room!=307)return;
    if(!nausika_init())return;
    chase.enabled=1;chase.hp=3000;chase.timer=30;chase.node=1;
    chase.pos[0]=nausika_path[0].x;chase.pos[1]=nausika_path[0].y;chase.pos[2]=nausika_path[0].z;
}
float nausika_update(float frames,float x,float y,float z) {
    float dx,dz,d,damage=0;
    if(!chase.enabled || chase.state==4)return 0;
    dx=x-chase.pos[0];dz=z-chase.pos[2];d=sqrtf(dx*dx+dz*dz);
    if(!chase.started) {
        /* Area-0 story camera is omitted: crossing the tunnel entrance starts it. */
        if(d>1300)return 0;
        chase.started=1;
    }
    chase.age+=frames;chase.timer-=frames;
    if(chase.state==3) {if(chase.timer<=0){chase.state=0;chase.age=0;chase.timer=30;}return 0;}
    if(chase.state==2) {
        if(chase.timer<=0) {
            if(d<750 && fabsf(y+100-chase.pos[1])<550)damage=1000;
            chase.state=0;chase.age=0;chase.timer=30;
        }
        return damage;
    }
    if(chase.timer<=0 && d<750) {chase.state=2;chase.age=0;chase.timer=chase.hp<1000?40:69;return 0;}
    if(chase.node<NAUSIKA_PATH_COUNT) {
        const Dmc3Vec3 *p=&nausika_path[chase.node];
        float step=(13+12*sinf(chase.age*.09817477f))*frames;
        dx=p->x-chase.pos[0];dz=p->z-chase.pos[2];d=sqrtf(dx*dx+dz*dz);
        if(d<=step){chase.pos[0]=p->x;chase.pos[1]=p->y;chase.pos[2]=p->z;++chase.node;}
        else {chase.pos[0]+=dx/d*step;chase.pos[2]+=dz/d*step;}
        chase.yaw=atan2f(dx,dz);
    }
    return 0;
}
void nausika_hit(float x,float y,float z,float yaw,float reach,int kind) {
    float dx=chase.pos[0]-x,dz=chase.pos[2]-z,d=sqrtf(dx*dx+dz*dz),damage;
    if(!chase.enabled || !chase.started || chase.state==4 || d>reach+500 || fabsf(y+150-chase.pos[1])>600)return;
    if(d>1 && (dx*sinf(yaw)+dz*cosf(yaw))/d<.2f)return;
    damage=(kind==HIT_LIGHT || kind==HIT_AIR)?42:84;
    chase.hp-=damage;chase.stagger+=damage;
    if(chase.hp<=0)chase.state=4;
    else if(chase.stagger>750) {chase.state=3;chase.age=0;chase.timer=90;chase.stagger=0;}
}

