#include "leviathan_boss.h"

#include "motion.h"
#include "swizzle.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "boss_leviathan_runtime.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

/* Independently implemented from CStageSetHeart's exported state handlers:
 * 45fe70 dispatch, 460520 exposed, 4607c0 attack, 461630 damage.
 * Native timing is 60 Hz; callers pass elapsed 60 Hz frames.
 * Rendering and the lungs' charge rules are provided separately from this
 * encounter state. See docs/LEVIATHAN_SPEC.md for remaining approximations. */
enum { HEART_CLOSED, HEART_OPENING, HEART_EXPOSED, HEART_ATTACK, HEART_DEAD };
static struct {
    int active, state, target, difficulty, attack_kind;
    float hp, timer, age, lung_hp[2], charge[2], down[2];
} heart;
static int difficulty = 1;
static const float target_pos[3][3] = {
    {4978.54f, 244.41f, 2651.36f},
    {5994.44f, 274.10f, 2853.51f},
    {3824.31f, 570.85f, 3195.44f}
};
/* DAT_005cccf0 / DAT_005ccd20, first pair for each rank. */
static const float exposed_time[5] = {600, 540, 480, 420, 360};
static const float double_time[5] = {900, 810, 720, 630, 540};
static const float damage_scale[5] = {2.0f, 1.0f, .9f, .7f, .5f};

void leviathan_boss_set_difficulty(int value) {
    difficulty = value < 0 ? 0 : value > 4 ? 4 : value;
}
void leviathan_boss_spawn(void) {
    memset(&heart, 0, sizeof(heart));
    heart.active = 1;
    heart.difficulty = difficulty;
    heart.hp = 6500.0f; /* DAT_005ccf9c */
    heart.lung_hp[0] = 1500.0f; /* DAT_005c986c, type 0 */
    heart.lung_hp[1] = 2000.0f; /* DAT_005c98cc, type 1 */
}
void leviathan_boss_kill(void) { heart.active = 0; }
float leviathan_boss_health(void) { return heart.active ? fmaxf(0, heart.hp) : 0; }
float leviathan_boss_max_health(void) { return 6500.0f; }
int leviathan_boss_dying(void) { return heart.state == HEART_DEAD; }
int leviathan_boss_open(void) { return heart.state == HEART_EXPOSED || heart.state == HEART_ATTACK; }
int leviathan_boss_target(void) { return heart.target; }
void leviathan_boss_target_position(float out[3]) {
    memcpy(out, target_pos[heart.target], sizeof(float) * 3);
}
void leviathan_boss_select_target(float px, float pz) {
    float best = 1.0e30f;
    int i;
    for (i = 0; i < 3; ++i) {
        float dx = px - target_pos[i][0], dz = pz - target_pos[i][2];
        float d = dx * dx + dz * dz;
        if (i && heart.down[i-1] > 0) continue;
        if (d < best) { best = d; heart.target = i; }
    }
}
void leviathan_boss_absorb(int part, float amount) {
    if (!heart.active || part < 1 || part > 2 || amount <= 0) return;
    heart.charge[part-1] += amount;
}
int leviathan_boss_hit(int part, float damage, int mortal) {
    if (!heart.active || heart.state == HEART_DEAD || part < 0 || part > 2 || damage <= 0) return 0;
    if (part) {
        int side = part - 1;
        if (heart.down[side] > 0) return 0;
        heart.lung_hp[side] -= damage * damage_scale[heart.difficulty];
        if (heart.lung_hp[side] <= 0) {
            heart.down[side] = exposed_time[heart.difficulty] + 55.0f;
            heart.state = HEART_OPENING;
            heart.timer = 55.0f; /* +0x838, native opening vulnerability delay */
        }
        return 0;
    }
    if (!leviathan_boss_open()) return 0;
    damage *= damage_scale[heart.difficulty];
    if (heart.state == HEART_ATTACK) damage *= 1.5f;
    if (mortal) heart.hp -= damage;
    if (heart.hp <= 0) {
        heart.hp = 0;
        heart.state = HEART_DEAD;
        heart.timer = 120;
        return 1;
    }
    return 0;
}
int leviathan_boss_update(float frames, float px, float py, float pz, int attacks) {
    int i;
    (void)px; (void)py; (void)pz; (void)attacks;
    if (!heart.active || frames <= 0) return 0;
    heart.age += frames;
    for (i = 0; i < 2; ++i) if (heart.down[i] > 0) {
        heart.down[i] -= frames;
        if (heart.down[i] <= 0) { heart.down[i] = 0; heart.lung_hp[i] = i ? 2000.0f : 1500.0f; }
    }
    if (heart.state == HEART_OPENING) {
        heart.timer -= frames;
        if (heart.timer <= 0) {
            heart.state = HEART_EXPOSED;
            heart.timer = (heart.down[0] > 0 && heart.down[1] > 0 ? double_time : exposed_time)[heart.difficulty];
        }
    } else if (heart.state == HEART_EXPOSED || heart.state == HEART_ATTACK) {
        heart.timer -= frames;
        if (heart.timer <= 0) heart.state = HEART_CLOSED;
    } else if (heart.state == HEART_DEAD) {
        heart.timer -= frames;
        return heart.timer <= 0;
    }
    return 0;
}

/* Native stage models are authored in world coordinates, so no boss root
 * translation is applied. Only nine bone transforms are needed per actor. */
typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u,v,nx,ny,nz,x,y,z; } Vertex;
static Vertex *vertices[3];
static unsigned short *indices[3];
static void *textures[LEVIATHAN_TEXTURE_COUNT];
static Transform poses[3][9];
static int actor_motion[3][8], render_ready;
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
    const LeviathanMesh *m = &leviathan_parts[part];
    MotionSample sample;
    int i,j,k,slot=0;
    float frame=heart.age;
    const Motion *motion;
    /* Native slots: idle 0, Heart opening 1, closing 3; lung states are
     * refined with their state machine. Clamp transition clips. */
    if (!part && heart.state == HEART_OPENING) { slot=1; frame=55-heart.timer; }
    else if (!part && heart.state == HEART_EXPOSED) { slot=1; frame=10000; }
    else if (part && heart.down[part-1]>0) { slot=3; frame=10000; }
    motion=motion_get(actor_motion[part][slot]);
    memset(&sample,0,sizeof(sample));
    if (motion) motion_sample(motion, slot ? fminf(frame,motion->end_frame) : motion_wrap(motion,frame),&sample);
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
int leviathan_boss_init(void) {
    int i,j;
    if(render_ready)return 1;
    for(i=0;i<3;++i) {
        const LeviathanMesh *m=&leviathan_parts[i];
        vertices[i]=memalign(16,sizeof(Vertex)*m->vertices);
        indices[i]=memalign(16,sizeof(unsigned short)*m->indices_count);
        if(!vertices[i]||!indices[i])return 0;
        memcpy(indices[i],m->indices,sizeof(unsigned short)*m->indices_count);
        for(j=0;j<m->vertices;++j) {vertices[i][j].u=m->uvs[j].u;vertices[i][j].v=m->uvs[j].v;}
        for(j=0;j<8;++j) {char name[16];snprintf(name,sizeof(name),"lv%d-%02d",i,j);actor_motion[i][j]=motion_find(name);}
        if(actor_motion[i][0]<0)return 0;
    }
    for(i=0;i<LEVIATHAN_TEXTURE_COUNT;++i) {
        const Dmc3Texture *t=&leviathan_textures[i];
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
void leviathan_boss_draw(void) {
    int part,i;
    if(!heart.active||!render_ready)return;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_LIGHTING);sceGuEnable(GU_ALPHA_TEST);
    sceGuColor(0xffffffffu);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexMode(GU_PSM_T8,0,0,1);sceGuClutMode(GU_PSM_8888,0,255,0);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    for(part=0;part<3;++part) {
        const LeviathanMesh *m=&leviathan_parts[part];
        pose_actor(part);
        for(i=0;i<m->objects_count;++i) {
            const Dmc3ModelObject *o=&m->objects[i];
            const Dmc3Texture *t=&leviathan_textures[o->texture];
            sceGuClutLoad(32,leviathan_palettes[o->texture]);
            sceGuTexImage(0,t->width,t->height,t->width,textures[o->texture]);sceGuTexFlush();
            sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_NORMAL_32BITF|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D,
                            o->index_count,indices[part]+o->index_start,vertices[part]);
        }
    }
}
