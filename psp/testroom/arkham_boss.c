#include "arkham_boss.h"

#include "enemy.h"
#include "fx.h"
#include "motion.h"
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
#include "boss_arkham_runtime.h"

/* Arkham, class 35 (vtable 0x60a9a0, update 3d1390, alive 3d2220). Frames and distances keep the original 60 Hz /
 * centimetre units; the PS2's 65536-to-a-turn angles are converted where quoted. See docs/ARKHAM_SPEC.md. */
#define PI 3.14159265358979323846f
#define UNITS (2.0f * PI / 65536.0f)
#define MAX_HEALTH 18000.0f           /* DAT_005c1e6c, his health at +0x1c24 */
#define ROUND_TWO 9000.0f             /* group 0 ends here (demo 53); group 1 starts at 0x460ca000 = 9000 */
#define ZONE_RANGE 700.0f             /* FUN_00359b40(700, ...) */
#define BODY_RADIUS 220.0f            /* [estimate] the body's width (mesh x -179..222) */
#define DANTE_RADIUS 40.0f
#define LEGIONS 40                    /* FUN_003d4ca0 counts 0x28 */
#define LEGION_SPEED 8.0f             /* [estimate] "as fast as Dante runs" */
#define LEGION_RADIUS 70.0f
#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

extern unsigned int enemy_ai_random(void);
extern int testroom_frame;

enum { PART_BODY, PART_SHELL, PART_LEGION };
/* The states (+0x1424); state n plays script n, its motions slot n of [3] (the body) and [4] (the shell). */
enum {
    S_IDLE = 0, S_WALK = 1, S_TURN = 2, S_RECOVER = 6, S_RECOVER2 = 7, S_HURT = 10, S_SUMMON = 0x0b, S_ROAR = 0x0c,
    S_RIGHT = 0x0d, S_RIGHT2 = 0x0e, S_FRONT = 0x0f, S_LEFT = 0x10, S_LEFT2 = 0x11, S_GREEN = 0x12, S_GREEN2 = 0x13,
    S_WIDE = 0x14, S_MISSILES_IN = 0x15, S_MISSILES = 0x16, S_ROUND = 0x30, S_DEATH = 0x31
};
static const signed char state_motion[0x19] = {
    0, 1, 1, 2, 3, 4, 20, 21, 0, 0, 0, 30, 10, 34, 35, 31, 32, 33, 36, 36, 38, 39, 40, 39, 40
};

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;
typedef struct { int on; float pos[3], yaw, life; } Legion;
typedef struct { int on; float pos[3], vel[3], life; } Missile;

static struct {
    int active, dying, state, step, phase, round, hit_done, landed, landed_heavy, tick;
    float pos[3], yaw, hp, timer, frame, shell_frame, floor_y, dante[3];
    float zone[5];                    /* +0x1cbc front, +0x1cc0 back, +0x1cc4 left, +0x1cc8 right, +0x1ccc away */
    float summon_clock, hurt_since, landed_x, landed_z, landed_damage;
    int body_motion, shell_motion;
    Legion legions[LEGIONS];
    Missile missiles[8];
} ak;

static int body_ids[51], shell_ids[51];
static Vertex *vertices[ARKHAM_PART_COUNT];
static unsigned short *indices[ARKHAM_PART_COUNT];
static void *textures[ARKHAM_TEXTURE_COUNT];
static Transform body_pose[40], shell_pose[12], legion_pose[8];
static char trace_path[320];
static int trace_on, boss_difficulty = 1;
static int (*floor_fn)(float x, float y, float z, float *out);

void arkham_boss_set_difficulty(int difficulty) { boss_difficulty = difficulty; }
void arkham_boss_set_floor(int (*fn)(float x, float y, float z, float *out)) { floor_fn = fn; }

static float wrap_angle(float a) { while (a > PI) a -= 2.0f * PI; while (a < -PI) a += 2.0f * PI; return a; }
static float unit_random(void) { return (float)enemy_ai_random() * 2.3283064e-10f; }
static float flat(const float a[3], const float b[3]) { float dx = a[0] - b[0], dz = a[2] - b[2]; return sqrtf(dx * dx + dz * dz); }
static float angle_to(const float at[3]) { return atan2f(at[0] - ak.pos[0], at[2] - ak.pos[2]); }
static void trace(const char *event) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "t=%d f=%d %s state=%#x phase=%d round=%d frame=%.1f hp=%.0f zones=%.0f,%.0f,%.0f,%.0f,%.0f d=%.0f\n",
            ak.tick, testroom_frame, event, ak.state, ak.phase, ak.round, ak.frame, ak.hp, ak.zone[0], ak.zone[1],
            ak.zone[2], ak.zone[3], ak.zone[4], flat(ak.pos, ak.dante));
    fclose(f);
}
void arkham_boss_trace(const char *path) {
    trace_on = path && *path;
    if (trace_on) snprintf(trace_path, sizeof(trace_path), "%s", path);
}

/* ---- The decision (FUN_003d3ae0 and FUN_003d7930), rank by difficulty 0 ---------------------------------- */
/* DAT_005c22d0 [phase] for variant 0: front 40 30 20, back 50 40 30, left 50 40 30, right 50 40 30, away 200 150 120 */
static const float zone_limit[5][3] = {{40, 30, 20}, {50, 40, 30}, {50, 40, 30}, {50, 40, 30}, {200, 150, 120}};
static const float chance_green[3] = {0, 30, 50};       /* 0x5c25c0: -> 0x12 */
static const float chance_missiles[3] = {0, 5, 8};      /* 0x5c2620: -> 0x15 */
static const float chance_roar[3] = {20, 30, 40};       /* 0x5c25f0: away -> 0xc */
static const float chance_far_missiles[3] = {0, 15, 20}; /* 0x5c2650: away -> 0x15 */
static const float summon_damage[3] = {2000, 1800, 1600}; /* 0x5c2270: damage since the last summon -> 0xb */
#define SUMMON_CLOCK 900.0f           /* [estimate] +0x15e4's reload isn't decoded */

/* Which of his four sides (FUN_00359b40: within 700, a sector) Dante is on: 0 front (40 deg), 1 back (100 deg),
 * 2 left (110 deg about +75), 3 right (110 deg about -75), or 4 none. */
static int dante_zone(void) {
    float d = flat(ak.pos, ak.dante), a;
    if (d > ZONE_RANGE) return 4;
    a = wrap_angle(angle_to(ak.dante) - ak.yaw);
    if (fabsf(a) <= 0x1c72 * UNITS * 0.5f) return 0;
    if (fabsf(wrap_angle(a - PI)) <= 0x471c * UNITS * 0.5f) return 1;
    if (fabsf(wrap_angle(a - 0x3555 * UNITS)) <= 0x4e39 * UNITS * 0.5f) return 2;
    if (fabsf(wrap_angle(a + 0x3554 * UNITS)) <= 0x4e39 * UNITS * 0.5f) return 3;
    return 4;
}
static void set_state(int state);
static void decide(float f) {
    int z = dante_zone(), i, p = ak.phase;
    /* The timers fill while Dante is on that side, and empty when he isn't */
    for (i = 0; i < 4; ++i) {
        if (i == z) { if (ak.zone[i] < 1000.0f) ak.zone[i] += f; }
        else if (ak.zone[i] > 0.0f) ak.zone[i] = fmaxf(0.0f, ak.zone[i] - f);
    }
    if (z == 4) { if (ak.zone[4] < 1000.0f) ak.zone[4] += f; }
    else ak.zone[4] = fmaxf(0.0f, ak.zone[4] - f);
    /* Legions: their clock (+0x15e4), or the damage taken since the last call (+0x1cb4 - +0x1c24) */
    ak.summon_clock -= f;
    if (ak.summon_clock < 0.0f || ak.hurt_since > summon_damage[p]) { set_state(S_SUMMON); return; }
    if (z != 4) {
        int pick = -1;
        if (ak.zone[0] > zone_limit[0][p]) { pick = S_FRONT; ak.zone[0] = 0.0f; }
        else if (ak.zone[3] > zone_limit[3][p]) { pick = p ? S_RIGHT2 : S_RIGHT; ak.zone[3] = 0.0f; }
        else if (ak.zone[2] > zone_limit[2][p]) { pick = p ? S_LEFT2 : S_LEFT; ak.zone[2] = 0.0f; }
        else if (ak.zone[1] > zone_limit[1][p]) { pick = S_TURN; ak.zone[1] = 0.0f; }
        if (pick < 0) return;
        if (unit_random() * 100.0f < chance_green[p]) pick = S_GREEN;
        if (unit_random() * 100.0f < chance_missiles[p]) pick = S_MISSILES_IN;
        set_state(pick);
    } else if (ak.zone[4] > zone_limit[4][p]) {
        int pick = -1;
        if (unit_random() * 100.0f < chance_roar[p]) pick = S_ROAR;
        if (unit_random() * 100.0f < chance_far_missiles[p]) pick = S_MISSILES_IN;
        ak.zone[4] = 0.0f;
        if (pick >= 0) set_state(pick);
    }
}

/* ---- Motions ---------------------------------------------------------------------------------------------- */
static int motion_end(int id) { const Motion *m = motion_get(id); return m ? (int)m->end_frame : 0; }
static void set_state(int state) {
    int slot = state >= 0 && state < 0x19 ? state_motion[state] : 0;
    ak.state = state;
    ak.step = 0;
    ak.timer = 0.0f;
    ak.frame = ak.shell_frame = 0.0f;
    ak.hit_done = 0;
    if (state == S_DEATH) slot = 11;
    if (body_ids[slot] >= 0) ak.body_motion = body_ids[slot];
    ak.shell_motion = shell_ids[slot] >= 0 ? shell_ids[slot] : shell_ids[0];
    trace("state");
}
static int done(void) {
    int end = motion_end(ak.shell_motion);
    return ak.shell_frame >= (float)end;
}
/* FUN_003d7cc0: what follows each state when its script ends */
static void next_state(void) {
    switch (ak.state) {
    case S_RECOVER: set_state(S_RECOVER2); break;
    case S_RECOVER2: set_state(S_IDLE); break;
    case S_GREEN: set_state(S_GREEN2); break;
    case S_GREEN2: set_state(S_WIDE); break;
    case S_MISSILES_IN: set_state(S_MISSILES); break;
    case S_MISSILES: set_state(S_RECOVER); break;
    default: set_state(S_IDLE); break;
    }
}

/* ---- His blows -------------------------------------------------------------------------------------------- */
static void land(float damage, int heavy) {
    if (ak.landed) return;
    ak.landed = 1;
    ak.landed_x = ak.pos[0]; ak.landed_z = ak.pos[2];
    ak.landed_damage = damage;
    ak.landed_heavy = heavy;
    trace("landed");
}
/* A sweep of the tentacles over one side, on the script's attack frame [the reach is the zone's 700; damage estimated] */
static void sweep(int zone, float at, float damage) {
    if (ak.hit_done || ak.frame < at) return;
    ak.hit_done = 1;
    if (dante_zone() == zone) land(damage, 1);
}
static void fire_missiles(void) {
    int i, n = 0;
    for (i = 0; i < 8 && n < 3; ++i) if (!ak.missiles[i].on) {
        Missile *m = &ak.missiles[i];
        float a = ak.yaw + (n - 1) * 0.7f;
        m->on = 1; m->life = 300.0f;
        m->pos[0] = ak.pos[0] + sinf(a) * 200.0f; m->pos[1] = ak.pos[1] + 300.0f; m->pos[2] = ak.pos[2] + cosf(a) * 200.0f;
        m->vel[0] = sinf(a) * 6.0f; m->vel[1] = 8.0f; m->vel[2] = cosf(a) * 6.0f;
        ++n;
    }
}
static void summon(int count) {
    int i, n = 0;
    for (i = 0; i < LEGIONS && n < count; ++i) if (!ak.legions[i].on) {
        Legion *l = &ak.legions[i];
        float a = unit_random() * 2.0f * PI;
        l->on = 1; l->life = 0.0f;
        l->pos[0] = ak.pos[0] + sinf(a) * 300.0f; l->pos[1] = ak.floor_y + 100.0f; l->pos[2] = ak.pos[2] + cosf(a) * 300.0f;
        l->yaw = a;
        ++n;
    }
}
static void shells_update(float f) {
    int i;
    for (i = 0; i < 8; ++i) {
        Missile *m = &ak.missiles[i];
        float to[3], d;
        if (!m->on) continue;
        if ((m->life -= f) <= 0.0f) { m->on = 0; continue; }
        to[0] = ak.dante[0] - m->pos[0]; to[1] = ak.dante[1] + 100.0f - m->pos[1]; to[2] = ak.dante[2] - m->pos[2];
        d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (d > 1.0f && m->life < 270.0f) {
            int k;
            for (k = 0; k < 3; ++k) m->vel[k] += (to[k] / d * 14.0f - m->vel[k]) * fminf(1.0f, 0.05f * f);
        }
        m->pos[0] += m->vel[0] * f; m->pos[1] += m->vel[1] * f; m->pos[2] += m->vel[2] * f;
        if (d < 60.0f + DANTE_RADIUS) { fx_explosion(m->pos); land(300.0f, 1); m->on = 0; }
    }
    for (i = 0; i < LEGIONS; ++i) {
        Legion *l = &ak.legions[i];
        float dx, dz, d;
        if (!l->on) continue;
        l->life += f;
        dx = ak.dante[0] - l->pos[0]; dz = ak.dante[2] - l->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        l->yaw = atan2f(dx, dz);
        if (d > 1.0f) { l->pos[0] += dx / d * LEGION_SPEED * f; l->pos[2] += dz / d * LEGION_SPEED * f; }
        l->pos[1] += (ak.dante[1] + 100.0f - l->pos[1]) * fminf(1.0f, 0.05f * f);
        /* They burst on touching him (the walkthrough; the explosion's record isn't read) */
        if (d < LEGION_RADIUS + DANTE_RADIUS && fabsf(ak.dante[1] + 100.0f - l->pos[1]) < 150.0f) {
            fx_explosion(l->pos);
            land(400.0f, 1);
            l->on = 0;
        }
    }
}
void arkham_boss_strike(float x, float y, float z, float yaw, float reach) {
    int i;
    for (i = 0; i < LEGIONS; ++i) {
        Legion *l = &ak.legions[i];
        float dx = l->pos[0] - x, dz = l->pos[2] - z, d = sqrtf(dx * dx + dz * dz), dy = l->pos[1] - y;
        if (!l->on || d > reach + LEGION_RADIUS || dy < -150.0f || dy > 300.0f) continue;
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        fx_hit_spark(l->pos);
        l->on = 0;
        trace("legion down");
    }
}

static void ai_step(float f) {
    ak.timer += f;
    switch (ak.state) {
    case S_IDLE: case S_WALK:
        decide(f);
        break;
    case S_TURN:
        /* 2: round to face Dante (0x42f00000: 120 frames) [the turn rate is an estimate] */
        { float d = wrap_angle(angle_to(ak.dante) - ak.yaw), r = 0x200 * UNITS * f; ak.yaw = wrap_angle(ak.yaw + fmaxf(-r, fminf(r, d))); }
        if (ak.timer > 120.0f) set_state(S_IDLE);
        break;
    case S_FRONT: sweep(0, 45.0f, 500.0f); if (done()) next_state(); break;     /* 0xf: script 0:15, motion 31 */
    case S_RIGHT: sweep(3, 105.0f, 500.0f); if (done()) next_state(); break;    /* 0xd: 0:13, motion 34, record 0 */
    case S_RIGHT2: sweep(3, 40.0f, 500.0f); if (done()) next_state(); break;    /* 0xe: 0:14, motion 35, record 1 */
    case S_LEFT: sweep(2, 105.0f, 500.0f); if (done()) next_state(); break;     /* 0x10: 0:16, motion 32 */
    case S_LEFT2: sweep(2, 45.0f, 500.0f); if (done()) next_state(); break;     /* 0x11: 0:17, motion 33 */
    case S_GREEN: case S_GREEN2:
        if (done()) next_state();   /* 0x12 -> 0x13: the body turns green (0:18, 0:19) */
        break;
    case S_WIDE:
        /* 0x14 (0:20, motion 38): the wide blast in front [frame and reach estimated: 20, 900 within 60 deg] */
        if (!ak.hit_done && ak.frame >= 20.0f) {
            float a = wrap_angle(angle_to(ak.dante) - ak.yaw);
            ak.hit_done = 1;
            if (flat(ak.pos, ak.dante) < 900.0f && fabsf(a) < PI / 3.0f) land(700.0f, 1);
        }
        if (done()) next_state();
        break;
    case S_MISSILES:
        /* 0x16 (0:22, motion 40): attack record 5 at 25 and 95: the homing red missiles */
        if (ak.step == 0 && ak.frame >= 25.0f) { fire_missiles(); ak.step = 1; }
        if (ak.step == 1 && ak.frame >= 95.0f) { fire_missiles(); ak.step = 2; }
        if (done()) next_state();
        break;
    case S_SUMMON:
        /* 0xb (0:11, motion 30): the Legions at 90 and 165 [four each, estimated] */
        if (ak.step == 0 && ak.frame >= 90.0f) { summon(4); ak.step = 1; }
        if (ak.step == 1 && ak.frame >= 165.0f) { summon(4); ak.step = 2; }
        if (done()) { ak.summon_clock = SUMMON_CLOCK; ak.hurt_since = 0.0f; next_state(); }
        break;
    case S_ROUND:
        /* Group 0 down: demo 53, Vergil joins, and group 1 is Arkham again at 9000 (not played: a pause) */
        if (ak.timer > 120.0f) { ak.round = 2; set_state(S_IDLE); }
        break;
    case S_DEATH:
        break;
    default:
        if (done()) next_state();
        break;
    }
}

/* ---- Drawing ---------------------------------------------------------------------------------------------- */
static void identity(Transform *t) { memset(t, 0, sizeof(*t)); t->m[0][0] = t->m[1][1] = t->m[2][2] = 1.0f; }
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
static void build(const ArkhamMesh *mesh, int motion, float frame, Transform *out) {
    static MotionSample s;
    int i, k, have = motion >= 0;
    if (have) motion_sample(motion_get(motion), frame, &s);
    for (i = 0; i < mesh->bone_count; ++i) {
        float r[3] = {0.0f, 0.0f, 0.0f}, p[3] = {mesh->local[i].x, mesh->local[i].y, mesh->local[i].z};
        Transform local;
        for (k = 0; k < 3 && have; ++k) {
            if (s.has_rot[i][k]) r[k] = s.rot[i][k];
            if (s.has_trans[i][k]) p[k] = s.trans[i][k];
        }
        if (i == 0) { p[0] = 0.0f; p[2] = 0.0f; }
        euler(&local, r[0], r[1], r[2]);
        memcpy(local.p, p, sizeof(p));
        if (mesh->parents[i] < 0) out[i] = local;
        else multiply(&out[i], &out[mesh->parents[i]], &local);
    }
}
static void skin_part(int part, const Transform *bones) {
    const ArkhamMesh *m = &arkham_parts[part];
    int i, j, k;
    for (i = 0; i < m->vertices; ++i) {
        Vertex *v = &vertices[part][i];
        float p[3] = {0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
        for (j = 0; j < 3; ++j) {
            int b = m->bones[i][j], w = m->weights[i][j];
            const Transform *t;
            float src[3], ns[3], weight;
            if (!w) continue;
            if (b >= m->bone_count) b = 0;
            t = &bones[b];
            weight = (float)w / 31.0f;
            src[0] = m->positions[i].x - m->rest[b].x; src[1] = m->positions[i].y - m->rest[b].y; src[2] = m->positions[i].z - m->rest[b].z;
            ns[0] = m->normals[i].x; ns[1] = m->normals[i].y; ns[2] = m->normals[i].z;
            for (k = 0; k < 3; ++k) {
                p[k] += (t->p[k] + t->m[k][0] * src[0] + t->m[k][1] * src[1] + t->m[k][2] * src[2]) * weight;
                n[k] += (t->m[k][0] * ns[0] + t->m[k][1] * ns[1] + t->m[k][2] * ns[2]) * weight;
            }
        }
        v->x = p[0]; v->y = p[1]; v->z = p[2]; v->nx = n[0]; v->ny = n[1]; v->nz = n[2];
    }
    sceKernelDcacheWritebackRange(vertices[part], sizeof(Vertex) * m->vertices);
}
static void draw_part(int part) {
    const ArkhamMesh *m = &arkham_parts[part];
    int i, bound = -1;
    for (i = 0; i < m->objects_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if ((int)o->texture != bound) {
            const Dmc3Texture *t;
            bound = (int)o->texture;
            if (bound >= ARKHAM_TEXTURE_COUNT) bound = 0;
            t = &arkham_textures[bound];
            sceGuClutLoad(32, arkham_palettes[bound]);
            sceGuTexImage(0, t->width, t->height, t->width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[part] + o->index_start, vertices[part]);
    }
}
static void place(const float at[3], float yaw) {
    ScePspFVector3 p = {at[0], at[1], at[2]};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(yaw);
}
void arkham_boss_draw(void) {
    int i;
    if (!ak.active) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuColor(ak.state == S_GREEN || ak.state == S_GREEN2 || ak.state == S_WIDE ? 0xff80ff80u : 0xffffffffu);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    if (!(ak.dying && ak.timer > 240.0f)) {
        build(&arkham_parts[PART_BODY], ak.body_motion, ak.frame, body_pose);
        build(&arkham_parts[PART_SHELL], ak.shell_motion, ak.shell_frame, shell_pose);
        skin_part(PART_BODY, body_pose);
        skin_part(PART_SHELL, shell_pose);
        if (trace_on && (ak.tick % 120) == 1) {
            char line[200];
            snprintf(line, sizeof(line), "draw b0=%.0f,%.0f,%.0f b900=%.0f,%.0f,%.0f s0=%.0f,%.0f,%.0f motions=%d,%d pos=%.0f,%.0f,%.0f",
                     vertices[0][0].x, vertices[0][0].y, vertices[0][0].z, vertices[0][900].x, vertices[0][900].y, vertices[0][900].z,
                     vertices[1][0].x, vertices[1][0].y, vertices[1][0].z, ak.body_motion, ak.shell_motion, ak.pos[0], ak.pos[1], ak.pos[2]);
            trace(line);
        }
        place(ak.pos, ak.yaw);
        draw_part(PART_BODY);
        draw_part(PART_SHELL);
    }
    sceGuColor(0xffffffffu);
    for (i = 0; i < LEGIONS; ++i) if (ak.legions[i].on) {
        place(ak.legions[i].pos, ak.legions[i].yaw);
        draw_part(PART_LEGION);
    }
    for (i = 0; i < 8; ++i) if (ak.missiles[i].on && (ak.tick & 1)) fx_hit_spark(ak.missiles[i].pos);
    sceGuDisable(GU_ALPHA_TEST);
}

/* ---- Being hit ------------------------------------------------------------------------------------------ */
int arkham_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage) {
    if (!ak.landed) return 0;
    ak.landed = 0;
    *from_x = ak.landed_x; *from_z = ak.landed_z; *heavy = ak.landed_heavy; *damage = ak.landed_damage;
    return 1;
}
int arkham_boss_hit(int kind, float from_x, float from_z, int mortal) {
    float at[3], damage;
    (void)from_x; (void)from_z;
    if (!ak.active || ak.dying || ak.state == S_ROUND) return 0;
    arkham_boss_target_position(at);
    fx_hit_spark(at);
    damage = kind == HIT_PISTOL ? 2.0f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    if (!mortal) damage = 0.0f;
    ak.hp = fmaxf(0.0f, ak.hp - damage);
    ak.hurt_since += damage;
    /* FUN_003d4540: the phase byte follows his health */
    ak.phase = ak.hp > 9000.0f ? 0 : ak.hp > 3000.0f ? 1 : 2;
    trace("hit");
    if (ak.round == 1 && ak.hp <= ROUND_TWO) {
        ak.hp = ROUND_TWO;
        memset(ak.legions, 0, sizeof(ak.legions));
        set_state(S_ROUND);
        return 0;
    }
    if (ak.hp <= 0.0f) {
        ak.dying = 1;
        memset(ak.legions, 0, sizeof(ak.legions));
        memset(ak.missiles, 0, sizeof(ak.missiles));
        set_state(S_DEATH);
        return 1;
    }
    return 0;
}

/* ---- Frame -------------------------------------------------------------------------------------------- */
void arkham_boss_update(float frames, float px, float py, float pz, int attacks) {
    const Motion *m;
    if (!ak.active) return;
    ++ak.tick;
    ak.dante[0] = px; ak.dante[1] = py; ak.dante[2] = pz;
    if (!ak.dying && attacks) ai_step(frames);
    else if (ak.dying) ak.timer += frames;
    ak.frame += frames;
    ak.shell_frame += frames;
    if ((m = motion_get(ak.body_motion)) && ak.frame >= m->end_frame)
        ak.frame = m->looping || ak.state <= S_WALK ? motion_wrap(m, ak.frame) : m->end_frame;
    if ((m = motion_get(ak.shell_motion)) && ak.shell_frame >= m->end_frame && (m->looping || ak.state <= S_WALK))
        ak.shell_frame = motion_wrap(m, ak.shell_frame);
    if (!ak.dying) shells_update(frames);
    if (trace_on && (ak.tick % 60) == 0) trace("tick");
}
void arkham_boss_spawn(float x, float y, float z) {
    memset(&ak, 0, sizeof(ak));
    ak.active = 1;
    ak.round = 1;
    /* His model hangs from its origin (y -730 to 663): the origin rides that far over the floor [inferred] */
    ak.pos[0] = x; ak.floor_y = y; ak.pos[1] = y + 730.0f; ak.pos[2] = z;
    ak.yaw = 0.0f;
    ak.hp = MAX_HEALTH;
    ak.summon_clock = SUMMON_CLOCK;
    ak.body_motion = ak.shell_motion = -1;
    set_state(S_IDLE);
    trace("spawn");
}
void arkham_boss_kill(void) { if (ak.active && !ak.dying) { ak.hp = 0.0f; ak.dying = 1; set_state(S_DEATH); } }
int arkham_boss_dying(void) { return ak.dying; }
int arkham_boss_hidden(void) { return 0; }
float arkham_boss_health(void) { return ak.hp; }
float arkham_boss_max_health(void) { return MAX_HEALTH; }
void arkham_boss_position(float out[3]) { memcpy(out, ak.pos, sizeof(ak.pos)); }
void arkham_boss_target_position(float out[3]) {
    /* His near side, at Dante's height: the body is about 4 m across */
    float d = flat(ak.pos, ak.dante), k = d > BODY_RADIUS ? (d - BODY_RADIUS * 0.8f) / d : 0.0f;
    out[0] = ak.pos[0] + (ak.dante[0] - ak.pos[0]) * (1.0f - k);
    out[2] = ak.pos[2] + (ak.dante[2] - ak.pos[2]) * (1.0f - k);
    out[1] = ak.dante[1] + 100.0f;
}
void arkham_boss_push_out(float *x, float *z, float y, float r) {
    float dx, dz, d, min = BODY_RADIUS + r;
    (void)y;
    if (!ak.active || ak.dying) return;
    dx = *x - ak.pos[0]; dz = *z - ak.pos[2];
    d = sqrtf(dx * dx + dz * dz);
    if (d >= min) return;
    if (d < 0.01f) { dx = 0.0f; dz = 1.0f; d = 1.0f; }
    *x = ak.pos[0] + dx / d * min;
    *z = ak.pos[2] + dz / d * min;
}
int arkham_boss_init(void) {
    int i, j;
    for (i = 0; i < 51; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "ak%02d", i); body_ids[i] = motion_find(name);
        snprintf(name, sizeof(name), "as%02d", i); shell_ids[i] = motion_find(name);
    }
    if (body_ids[0] < 0 || shell_ids[0] < 0) return 0;
    for (i = 0; i < ARKHAM_PART_COUNT; ++i) {
        const ArkhamMesh *m = &arkham_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices);
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) return 0;
        memcpy(indices[i], m->indices, sizeof(unsigned short) * m->indices_count);
        for (j = 0; j < m->vertices; ++j) { vertices[i][j].u = m->uvs[j].u; vertices[i][j].v = m->uvs[j].v; }
    }
    /* The Legion is drawn in its rest pose [approximation: it has no motions of its own in the pack] */
    build(&arkham_parts[PART_LEGION], -1, 0.0f, legion_pose);
    skin_part(PART_LEGION, legion_pose);
    for (i = 0; i < ARKHAM_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &arkham_textures[i];
        size_t bytes = (size_t)t->width * t->height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}
