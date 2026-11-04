#include "enemy.h"

#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <psputils.h>
#include <pspthreadman.h>

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "motion.h"
#include "player.h"

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "swizzle.h"
#include "model_runtime.h"   /* Dmc3Vec3 / Dmc3Uv / Dmc3ModelObject / Dmc3Texture types */
#include "enemy_runtime.h"
#include "enemy_ai_runtime.h"

/* The real logic of CEm000 / CComEm000, the Seven Hell at this model: see docs/ENEMY_AI.md.
 * Three layers, as on the disc:
 *   plan    what it means to do. Most just run one action until it ends, then go back to deciding.
 *   action  a state with its own update: which motions, turning, jumps, reactions.
 *   script  the motion data's own list of what plays and when attacks are live.
 * Angles in the comments are the game's, 65536 to the circle; frames are 1/60 s. */
#define HL_MOTIONS 71
#define ANGLE(u) ((float)(u) * 6.2831853f / 65536.0f)
#define PI 3.1415927f
#define GRAVITY_STEP 0.54f

enum {  /* The actions this enemy uses, by the game's numbers. */
    A_FLINCH_FRONT = 0, A_FLINCH_BACK = 1, A_LAUNCHED = 2, A_LAUNCHED_BACK = 3, A_LAUNCH_UP = 7, A_AIR_HIT_HEAVY = 8, A_AIR_HIT = 12,
    A_NONE = 14, A_WALK = 15, A_DEATH = 26, A_SWING = 16, A_SWING_WIDE = 17, A_LEAP = 21,
    A_STEP_LEFT = 22, A_STEP_RIGHT = 23, A_STEP_BACK = 24, A_TAUNT_A = 58, A_TAUNT_B = 59,
    /* The Sloth's and the Gluttony's (build/hdref/enemy_specs.md): warps to 900, 600 and 300 cm from Dante,
     * the warp strike, the Gluttony's wind-up and its spew. */
    A_WARP_FAR = 41, A_WARP_MID = 42, A_WARP_NEAR = 43, A_WARP_STRIKE = 57, A_WIND_UP = 56, A_SPEW = 31
};
enum {  /* Its plans. PLAN_STAND is ours: the debug menu's dummies that don't attack. */
    P_FLINCH_FRONT = 0, P_FLINCH_BACK = 1, P_LAUNCHED = 2, P_LAUNCHED_BACK = 3, P_LAUNCH_UP = 7, P_AIR_HIT_HEAVY = 8, P_AIR_HIT = 9,
    P_DEATH = 20, P_DECIDE = 23, P_WATCH = 24, P_SWING = 27, P_SWING_WIDE = 28, P_LEAP = 32, P_STEP_LEFT = 33, P_STEP_RIGHT = 34,
    P_STEP_BACK = 35, P_TAUNT_A = 51, P_TAUNT_B = 52, PLAN_STAND = 200,
    P_SPEW = 37, P_SPEW_WOUND = 45, P_WARP_STRIKE = 46, P_WARP_FAR = 47, P_WARP_MID = 48, P_WARP_NEAR = 49
};
enum { OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO };

/* How long after one enemy is let attack before another may. The game passes 3.0 to a manager this
 * port hasn't read (FUN_00369180). Measured on the HD Collection in Turbo: 49 swings from 4 enemies,
 * never under 3.0 real seconds apart, so it doesn't scale with game speed. */
#define ATTACK_TOKEN_SECONDS 3.0f
/* The game's value comes from a mission object not read yet. 0 gives what Mission 2 on Easy shows
 * on the HD Collection: plans 27, 32, 51 and 52 only. */
#define AI_RANK 0.0f
/* How much a Pride takes, in units of a light hit. An estimate: on the HD Collection a maxed Dante kills
 * one in about three to five hits on Easy. The game's health and damage values aren't read yet. */
#define ENEMY_HEALTH 6
/* Dante as the attack spheres see him. Not read from the game. */
#define DANTE_RADIUS 40.0f
#define DANTE_LOW 20.0f
#define DANTE_HIGH 160.0f
/* What the hit records of Dante's moves give. A combo's throw is measured on the HD Collection: flat,
 * at speed 20. Helm Breaker's slam isn't measured. High Time needs nothing: its state is fixed. */
#define HEAVY_ANGLE 0.0f
#define HEAVY_SPEED 20.0f
#define SLAM_SPEED 30.0f

/* Measured on the HD Collection: enemies stop 96 cm apart, and 88 cm from Dante. */
#define ENEMY_RADIUS 48.0f
/* When an enemy walks into Dante it mostly stops, and he gives a little. An estimate: he drifts
 * about 0.4 cm a frame while they walk at about 2. The rest of the overlap is enemy_push_out's. */
#define ENEMY_YIELDS 0.8f
/* Shoved further than this in a frame by another enemy, a watching enemy leaps (M[0x34] in plan 24). */
#define PUSH_LEAPS 1.0f
#define HAND_BONE 9                 /* a Seven Hell's -X hand holds its scythe */
#define ENIGMA_HAND_BONE 20         /* the Enigma's bow hand: a placeholder until its rig is read */
#define MAX_BONES ENEMY_MAX_BONES

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } Vertex;
typedef struct { float m[3][3]; } M3;

typedef struct {
    int active;
    int kind;                       /* EnemyKind: which model and logic */
    float pos[3], yaw, floor_y, frame, fall;
    int motion;
    /* Cross-fade between motions, over as many frames as the script says. */
    int prev_motion;
    float prev_frame, fade, fade_rate;
    /* The layers. */
    unsigned char plan, plan_step, action, step;
    unsigned char air, loop, done, stop_finish, pushed;
    unsigned char hidden;           /* a Sloth mid-warp */
    /* A second motion over some bones: the Enigma's bow arms. */
    int overlay_motion;
    float overlay_frame;
    unsigned char overlay_lo, overlay_hi;
    /* The Enigma's three bow-arm sets: a cooldown each, and the one shooting. */
    float shoot_timer[3], shoot_t;
    signed char shoot_set;
    unsigned char shoot_fan, shoot_phase;
    signed char health;
    short script_at, stop;
    float gauge, timer, wake;
    float vel[3], acc[3];
    float yaw_rate;
    float hit_angle, hit_speed;
    /* Attack spheres live right now. */
    float hit_left[2];
    unsigned char hit_cap[2], hit_type, hit_done;
    /* Each its own: the display list reads them after the next dummy is skinned, and they
     * stay as they are on the frames this one's pose isn't updated. */
    Vertex *vertices;
    float hand_rot[3][3], hand_pos[3];
    float anim_wait, posed_frame;
    int posed_motion;
} Enemy;

static int hl_motion[HL_MOTIONS];
static Vertex *weapon_vertices[ENEMY_KIND_COUNT];
static unsigned short *indices[ENEMY_KIND_COUNT], *weapon_indices[ENEMY_KIND_COUNT];
static void *textures[ENEMY_TEXTURE_SET_COUNT][8];
/* Which bone holds the weapon, by kind. */
static const signed char kind_hand[ENEMY_KIND_COUNT] = {HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, ENIGMA_HAND_BONE};
static Enemy enemies[ENEMY_MAX];
static Enemy *e = &enemies[0];
static int hits, attacks_on;
static int landed;
static float landed_x, landed_z;
static int landed_heavy;
static float attack_token;
static int mortal;
static int dummy_kind;
static float dante_y;   /* this frame's, for attacks that test his height in their action */
/* Each kind's decision table: the Seven Hells share the rest of their logic. */
static const EnemyAiClass *ai_class(void) {
    switch (e->kind) {
    case ENEMY_GLUTTONY: return &enemy_ai_classes[AI_CLASS_GLUTTONY];
    case ENEMY_SLOTH: return &enemy_ai_classes[AI_CLASS_SLOTH];
    default: return &enemy_ai_classes[AI_CLASS_PRIDE];
    }
}
static int ai_only;
/* For tests: every change of plan or action, and every attack landing. */
typedef struct { int tick; short who, plan, action, motion, landed; float x, y, z, yaw, gauge, frame; } TraceRow;
#define TRACE_ROWS 4000
static TraceRow *trace;
static int trace_count, trace_tick;
static int anim_auto = 1, poses_done;
static int test_no_texture, test_no_lighting;
static float spawn_ring = 320.0f;
static unsigned int anim_last_time;

static M3 world_rot[MAX_BONES];
static float world_pos[MAX_BONES][3];

static void m_identity(M3 *m) { memset(m, 0, sizeof(*m)); m->m[0][0] = m->m[1][1] = m->m[2][2] = 1.0f; }
static void m_mul(M3 *o, const M3 *a, const M3 *b) {
    M3 r; int i, j;
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j)
        r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    *o = r;
}
static void m_rot(M3 *m, int axis, float a) {
    float c = cosf(a), s = sinf(a);
    m_identity(m);
    if (axis == 0) { m->m[1][1] = c; m->m[1][2] = -s; m->m[2][1] = s; m->m[2][2] = c; }
    else if (axis == 1) { m->m[0][0] = c; m->m[0][2] = s; m->m[2][0] = -s; m->m[2][2] = c; }
    else { m->m[0][0] = c; m->m[0][1] = -s; m->m[1][0] = s; m->m[1][1] = c; }
}
/* MOT order: X first, R = Rz * Ry * Rx (same as Dante). */
static void m_euler(M3 *o, float x, float y, float z) {
    M3 rx, ry, rz, t;
    m_rot(&rx, 0, x); m_rot(&ry, 1, y); m_rot(&rz, 2, z);
    m_mul(&t, &ry, &rx); m_mul(o, &rz, &t);
}

/* The Enigma's own logic: see the section at the end. */
static int enigma_init(void);
static void enigma_spawn(void);
static void enigma_update(float f, float px, float py, float pz);
static void enigma_hit(HitKind kind, float from_x, float from_z);
static void land_attack(int heavy);
static void evaluate_pose(void);
static void trace_row(int did_land);
static void arrows_update(float f, float px, float py, float pz);
static void arrows_draw(void);
static void arrows_clear(int i);
static int giga_init(void);
static void giga_spawn(void);
static void giga_part_update(float f, float px, float py, float pz);
static void giga_hit(HitKind kind);
static void giga_draw(void);
#define ARROW_MAX_PUBLIC 24

int enemy_init(void) {
    int i, k;
    for (i = 0; i < HL_MOTIONS; ++i) {
        char name[6] = {'h', 'l', '-', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        hl_motion[i] = motion_find(name);
        if (hl_motion[i] < 0) return 0;
    }
    if (!enigma_init()) return 0;
    if (!giga_init()) return 0;
    for (k = 0; k < ENEMY_KIND_COUNT; ++k) {
        const EnemyModel *m = &enemy_models[k];
        indices[k] = memalign(16, sizeof(unsigned short) * m->index_count);
        weapon_vertices[k] = memalign(16, sizeof(Vertex) * m->weapon_vertex_count);
        weapon_indices[k] = memalign(16, sizeof(unsigned short) * m->weapon_index_count);
        if (!indices[k] || !weapon_vertices[k] || !weapon_indices[k]) return 0;
        memcpy(indices[k], m->indices, sizeof(unsigned short) * m->index_count);
        memcpy(weapon_indices[k], m->weapon_indices, sizeof(unsigned short) * m->weapon_index_count);
        for (i = 0; i < m->weapon_vertex_count; ++i) {
            Vertex *v = &weapon_vertices[k][i];
            v->u = m->weapon_uvs[i].u; v->v = m->weapon_uvs[i].v;
            v->nx = m->weapon_normals[i].x; v->ny = m->weapon_normals[i].y; v->nz = m->weapon_normals[i].z;
            v->x = m->weapon_positions[i].x; v->y = m->weapon_positions[i].y; v->z = m->weapon_positions[i].z;
        }
    }
    for (k = 0; k < ENEMY_MAX; ++k) enemies[k].motion = enemies[k].prev_motion = -1;
    for (k = 0; k < ENEMY_TEXTURE_SET_COUNT; ++k) {
        const EnemyTextureSet *set = &enemy_texture_sets[k];
        for (i = 0; i < set->count && i < 8; ++i) {
            size_t bytes = (size_t)set->textures[i].width * set->textures[i].height;
            textures[k][i] = memalign(16, bytes);
            if (!textures[k][i]) return 0;
            memcpy(textures[k][i], set->textures[i].rgba, bytes);
            if (!swizzle_texture8(textures[k][i], set->textures[i].width, set->textures[i].height)) return 0;
        }
    }
    sceKernelDcacheWritebackAll();
    return 1;
}

static int floor_under(float x, float y, float z, float *out) {
    if (!player_floor_fn) { *out = 0.0f; return 1; }
    return player_floor_fn(x, y + 200.0f, z, 0.0f, out);
}

static float spawn_search_y = 2000.0f;
void enemy_spawn_height(float y) { spawn_search_y = y + 100.0f; }

static void script_play(int n);

/* How much each takes, in units of a light hit. Estimates: the game's health and damage values aren't
 * read yet. On the HD Collection a maxed Dante kills a Pride in about three to five hits on Easy. */
static int kind_health(int kind) {
    /* In proportion to the parameter blocks' health (inferred): Pride 250, Gluttony 700, Sloth 1200,
     * Enigma 600 (build/hdref/enemy_specs.md). */
    switch (kind) {
    case ENEMY_GLUTTONY: return ENEMY_HEALTH * 700 / 250;
    case ENEMY_SLOTH: return ENEMY_HEALTH * 1200 / 250;
    case ENEMY_ENIGMA: return ENEMY_HEALTH * 600 / 250;
    default: return ENEMY_HEALTH;
    }
}


static void spawn_one(int kind, float x, float z) {
    const EnemyModel *m;
    int i;
    if (kind == ENEMY_GIGAPEDE) { giga_spawn(); return; }
    if (kind < 0 || kind >= ENEMY_KIND_COUNT) kind = ENEMY_PRIDE;
    m = &enemy_models[kind];
    /* Each dummy's own skinned copy, the first time that slot is used; big enough for any kind. */
    if (!e->vertices) {
        e->vertices = memalign(16, sizeof(Vertex) * ENEMY_MAX_VERTICES);
        if (!e->vertices) return;
    }
    e->kind = kind;
    for (i = 0; i < m->vertex_count; ++i) { e->vertices[i].u = m->uvs[i].u; e->vertices[i].v = m->uvs[i].v; }
    e->pos[0] = x; e->pos[2] = z;
    e->pos[1] = 0.0f;
    if (!floor_under(x, spawn_search_y, z, &e->pos[1])) e->pos[1] = 0.0f;
    e->floor_y = e->pos[1];
    e->yaw = 0.0f;
    e->fall = 0.0f;
    e->gauge = 0.0f;
    e->air = 0;
    e->motion = -1;
    e->fade = 0.0f;
    e->hit_left[0] = e->hit_left[1] = 0.0f;
    e->action = A_NONE;
    e->step = 0;
    e->plan = P_DECIDE;
    e->plan_step = 0;
    e->health = kind_health(kind);
    e->posed_motion = -1;
    e->overlay_motion = -1;
    e->hidden = 0;
    e->active = 1;
    if (kind == ENEMY_ENIGMA) { enigma_spawn(); return; }
    script_play(0);
    e->fade = 0.0f;
}

void enemy_spawn(float x, float z) {
    int i;
    for (i = 1; i < ENEMY_MAX; ++i) enemies[i].active = 0;
    e = &enemies[0];
    spawn_one(dummy_kind, x, z);
}

void enemy_set_mortal(int on) { mortal = on; }

int enemy_add(float x, float z) { return enemy_add_kind(ENEMY_PRIDE, x, z); }

int enemy_add_kind(int kind, float x, float z) {
    Enemy *selected = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active) {
            e = &enemies[i];
            spawn_one(kind, x, z);
            e = selected;
            return 1;
        }
    }
    return 0;
}

int enemy_alive_count(void) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) n += enemies[i].active && enemies[i].plan != P_DEATH;
    return n;
}

void enemy_kill_all(void) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].active && enemies[i].plan != P_DEATH) {
            enemies[i].plan = P_DEATH;
            enemies[i].plan_step = 0;
            enemies[i].air = 0;
        }
    }
}

int enemy_count(void) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) n += enemies[i].active;
    return n;
}

void enemy_set_count(int count, float x, float z) {
    int i;
    if (count == 0) for (i = 0; i < ARROW_MAX_PUBLIC; ++i) arrows_clear(i);
    for (i = 0; i < ENEMY_MAX; ++i) {
        e = &enemies[i];
        if (i >= count) {
            e->active = 0;
        } else if (!e->active) {
            float a = 0.785f * (float)i + 0.6f, r = spawn_ring + 60.0f * (float)(i / 8);
            spawn_one(dummy_kind, x + sinf(a) * r, z + cosf(a) * r);
        }
    }
    e = &enemies[0];
}

void enemy_select(int index) { e = &enemies[index]; }

void enemy_select_nearest(float x, float z) {
    int i;
    float best = 1.0e12f;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        float dx = o->pos[0] - x, dz = o->pos[2] - z, d = dx * dx + dz * dz;
        if (o->active && d < best) { best = d; e = &enemies[i]; }
    }
}

void enemy_set_attacks(int attacks) { attacks_on = attacks; }
void enemy_set_dummy_kind(int kind) { dummy_kind = kind >= 0 && kind < ENEMY_KIND_COUNT ? kind : ENEMY_PRIDE; }
int enemy_kind(void) { return e->kind; }

int enemy_kind_for_class(int spawn_class) {
    /* EST spawn ids: a Seven Hell is 4 x its class number (0 Pride, 4 Gluttony, 8 Lust, 12 Sloth,
     * 16 Wrath, 20 Greed), and 31 is the Enigma. Anything not ported yet stands in as a Pride. */
    if (spawn_class >= 0 && spawn_class < 24) return spawn_class / 4;
    if (spawn_class == 31) return ENEMY_ENIGMA;
    if (spawn_class == 41) return ENEMY_GIGAPEDE;
    return ENEMY_PRIDE;
}
void enemy_set_anim_auto(int on) { anim_auto = on; }
void enemy_set_test(int no_texture, int no_lighting, float ring) {
    test_no_texture = no_texture;
    test_no_lighting = no_lighting;
    spawn_ring = ring;
}
int enemy_poses_done(void) { return poses_done; }

int enemy_anim_rate(void) {
    int n;
    if (!anim_auto) return 0;
    n = enemy_count();
    return n <= 2 ? 25 : n <= 4 ? 20 : 15;
}

int enemy_attack_landed(float *from_x, float *from_z, int *heavy) {
    if (!landed) return 0;
    landed = 0;
    *from_x = landed_x;
    *from_z = landed_z;
    *heavy = landed_heavy;
    return 1;
}

int enemy_active(void) { return e->active; }
void enemy_position(float out[3]) { memcpy(out, e->pos, sizeof(e->pos)); }
float enemy_height(void) { return e->pos[1] - e->floor_y; }
int enemy_hits_taken(void) { return hits; }

/* The game's random numbers: its Mersenne Twister, tempered the same way. */
static unsigned int mt[624];
static int mt_index = 625;
static unsigned int ai_random(void) {
    unsigned int y;
    if (mt_index > 623) {
        int i;
        if (mt_index > 624) {
            mt[0] = 5489u;
            for (i = 1; i < 624; ++i) mt[i] = 1812433253u * (mt[i - 1] ^ mt[i - 1] >> 30) + (unsigned int)i;
        }
        for (i = 0; i < 624; ++i) {
            y = (mt[i] & 0x80000000u) | (mt[(i + 1) % 624] & 0x7fffffffu);
            mt[i] = mt[(i + 397) % 624] ^ y >> 1 ^ (y & 1u ? 0x9908b0dfu : 0u);
        }
        mt_index = 0;
    }
    y = mt[mt_index++];
    y ^= y >> 11;
    y ^= (y & 0x13a58adu) << 7;
    y ^= (y & 0x1df8cu) << 15;
    return y ^ y >> 18;
}
static float ai_unit(void) { return (float)ai_random() * 2.3283064e-10f; }

/* ---- Scripts: which motion, from where, until when. ---- */

static void script_play_op(const EnemyAiOp *op) {
    int m, blend = op->c & 0xff;
    if (op->a >= HL_MOTIONS) return;  /* a motion from a pack the port doesn't export */
    m = hl_motion[op->a];
    if (blend > 0 && e->motion >= 0) {
        e->prev_motion = e->motion;
        e->prev_frame = e->frame;
        e->fade = 1.0f;
        e->fade_rate = 1.0f / (float)blend;
    } else {
        e->fade = 0.0f;
    }
    e->motion = m;
    e->frame = op->b < 0 ? 0.0f : (float)op->b;
    e->loop = (unsigned char)(op->c >> 8);
}

static void script_run(void) {
    for (;;) {
        const EnemyAiOp *op = &enemy_ai_ops[e->script_at++];
        switch (op->op) {
        case OP_PLAY: script_play_op(op); break;
        case OP_WAIT: e->stop = op->b; e->stop_finish = op->a; return;
        case OP_HIT: {
            int slot = e->hit_left[0] > 0.0f ? 1 : 0;
            e->hit_left[slot] = op->b < 0 ? 1.0e6f : (float)op->b;
            e->hit_cap[slot] = op->a;
            e->hit_type = (unsigned char)op->c;
            e->hit_done = 0;
            break;
        }
        default: e->script_at = op->b; break;
        }
    }
}

static void script_play(int n) {
    e->hit_left[0] = e->hit_left[1] = 0.0f;
    e->done = 0;
    if (enemy_ai_scripts[n] < 0) { e->done = 1; return; }
    e->script_at = enemy_ai_scripts[n];
    script_run();
}

/* The second list a state can name motions through; the game's PTR_DAT_00618c38. */
static void script_play_hit(int n) { script_play(enemy_ai_hit_scripts[n]); }

/* Move the script on by f frames. root gets how far the motion's root travelled. */
static void script_tick(float f, float root[3]) {
    const Motion *m = motion_get(e->motion);
    float a[3], b[3], before = e->frame;
    int k;
    if (e->fade > 0.0f) {
        e->fade -= e->fade_rate * f;
        if (e->fade < 0.0f) e->fade = 0.0f;
    }
    for (k = 0; k < 2; ++k) if (e->hit_left[k] > 0.0f) e->hit_left[k] -= f;
    root[0] = root[1] = root[2] = 0.0f;
    if (e->done) return;
    e->frame += f;
    motion_root(m, before, a);
    if (e->loop) {
        if (e->frame > m->end_frame) {
            motion_root(m, m->end_frame, b);
            for (k = 0; k < 3; ++k) root[k] = b[k] - a[k];
            e->frame = motion_wrap(m, e->frame);
            motion_root(m, m->loop_start, a);
        }
        motion_root(m, e->frame, b);
        for (k = 0; k < 3; ++k) root[k] += b[k] - a[k];
    } else {
        float stop = e->stop < 0 ? m->end_frame : (float)e->stop;
        int passed = e->frame > stop;
        motion_root(m, passed ? stop : e->frame, b);
        for (k = 0; k < 3; ++k) root[k] = b[k] - a[k];
        if (passed) {
            if (e->stop_finish) { e->frame = stop; e->done = 1; }
            else script_run();
        }
    }
}

/* ---- Actions. ---- */

static float wrap_angle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static float angle_to(float x, float z) { return wrap_angle(atan2f(x - e->pos[0], z - e->pos[2]) - e->yaw); }

/* Turn toward Dante, at most this much a frame. */
static void turn(float px, float pz, float limit, float f) {
    float d = angle_to(px, pz), l = ANGLE(limit);
    if (d > l) d = l;
    if (d < -l) d = -l;
    e->yaw += d * f;
}
/* During a stretch of the motion, swing round by a fixed step. */
static void turn_window(float px, float pz, float from, float to, float step, float f) {
    if (e->frame >= from && e->frame <= to) {
        float d = angle_to(px, pz), s = step * f;
        if (d > s) e->yaw += s;
        if (d < -s) e->yaw -= s;
    }
}
static void move_local(float x, float y, float z) {
    float c = cosf(e->yaw), s = sinf(e->yaw);
    e->pos[0] += x * c + z * s;
    e->pos[1] += y;
    e->pos[2] += z * c - x * s;
}
/* FUN_00367e40, in the game's own steps however long the PSP's frame was: integrating a 30 FPS frame
 * as one step would shrink a leap's arc. With Turbo's 1.2 steps this gives the 458 and 343 cm peaks
 * measured on the HD Collection. */
static void physics(float f) {
    int steps = (int)(f + 0.001f), i, k;
    float step;
    if (steps < 1) steps = 1;
    step = f / (float)steps;
    for (i = 0; i < steps; ++i) {
        for (k = 0; k < 3; ++k) e->vel[k] += e->acc[k] * step;
        move_local(e->vel[0] * step, e->vel[1] * step, e->vel[2] * step);
    }
}
static void set_physics(float vx, float vy, float vz, float gravity) {
    e->vel[0] = vx; e->vel[1] = vy; e->vel[2] = vz;
    e->acc[0] = 0.0f; e->acc[1] = gravity; e->acc[2] = 0.0f;
}
static int on_floor(void) { return e->pos[1] <= e->floor_y; }

static void action_enter(int a) {
    e->action = (unsigned char)a;
    e->step = 0;
    switch (a) {
    case A_FLINCH_FRONT: script_play_hit((int)(ai_random() & 1u)); break;
    case A_FLINCH_BACK: script_play_hit(2 + (int)(ai_random() & 1u)); break;
    case A_LAUNCHED:
    case A_LAUNCHED_BACK: {
        float rad = e->hit_angle * PI / 180.0f;
        e->air = 1;
        e->fall = 0.0f;
        script_play_hit(a == A_LAUNCHED ? 16 : 18);
        set_physics(0.0f, e->hit_speed * sinf(rad), -e->hit_speed * cosf(rad), -GRAVITY_STEP);
        if (a == A_LAUNCHED_BACK) e->vel[2] = -e->vel[2];
        physics(1.0f);
        break;
    }
    case A_LAUNCH_UP:
        /* High Time. Peaks at the 302 cm measured on the HD Collection in Turbo. */
        e->air = 1;
        e->fall = 0.0f;
        script_play_hit(7);
        set_physics(0.0f, 21.0f, 0.0f, -0.7f);
        physics(1.0f);
        break;
    case A_AIR_HIT_HEAVY:
    case A_AIR_HIT:
        e->air = 1;
        e->fall = 0.0f;
        script_play_hit(a == A_AIR_HIT ? 21 : 8);
        set_physics(0.0f, 0.0f, 0.0f, a == A_AIR_HIT ? -GRAVITY_STEP : -1.0f);
        e->timer = 10.0f;
        break;
    case A_WALK: {
        unsigned int r = ai_random() % 3u;
        script_play(r == 2u ? 3 : r == 1u ? 2 : 1);
        break;
    }
    case A_DEATH: script_play(31); break;
    case A_SWING: script_play(47); break;
    case A_SWING_WIDE: script_play(48); break;
    case A_LEAP:
        script_play(55);
        e->yaw_rate = 0.0f;
        e->timer = 10.0f;
        break;
    case A_STEP_LEFT: script_play(8); break;
    case A_STEP_RIGHT: script_play(9); break;
    case A_STEP_BACK: script_play(10); break;
    case A_TAUNT_A: script_play(85); break;
    case A_TAUNT_B: script_play(86); break;
    case A_WARP_FAR:
    case A_WARP_MID:
    case A_WARP_NEAR: script_play(68); e->timer = 0.0f; break;   /* hl-46 */
    case A_WARP_STRIKE: script_play(66); e->timer = 0.0f; break; /* hl-60 */
    case A_WIND_UP: script_play(71); break;                      /* hl-10 */
    case A_SPEW: script_play(52); e->timer = 0.0f; break;        /* hl-52 */
    default: break;
    }
    if (a != A_WARP_FAR && a != A_WARP_MID && a != A_WARP_NEAR && a != A_WARP_STRIKE) e->hidden = 0;
}

/* The Sloth reappears this far from Dante at a random angle, on the floor under him, facing him. */
static void warp_to(float px, float pz, float r) {
    float a = (float)(ai_random() & 0xffffu) * (2.0f * PI / 65536.0f), g;
    e->pos[0] = px + sinf(a) * r;
    e->pos[2] = pz + cosf(a) * r;
    if (player_floor_fn && player_floor_fn(e->pos[0], dante_y + 400.0f, e->pos[2], 0.0f, &g)) e->pos[1] = g;
    else e->pos[1] = dante_y;
    e->floor_y = e->pos[1];
    e->yaw = atan2f(px - e->pos[0], pz - e->pos[2]);
}

static void lying_then_up(int get_up) {
    if (e->frame >= e->wake) {
        script_play_hit(get_up);
        e->step = (unsigned char)(e->step + 1);
    }
}

/* Returns whether the motion's root moves it this frame. */
static int action_update(float px, float pz, float f) {
    switch (e->action) {
    case A_FLINCH_FRONT:
    case A_FLINCH_BACK:
        turn(px, pz, 0x200, f);
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_LAUNCHED:
    case A_LAUNCHED_BACK:
        switch (e->step) {
        case 0:
            physics(f);
            if (e->vel[1] * f < 0.0f && on_floor()) {
                e->air = 0;
                script_play_hit(17);
                set_physics(0.0f, 2.0f, e->action == A_LAUNCHED ? -20.0f : 20.0f, -GRAVITY_STEP);
                physics(f);
                e->step = 1;
            }
            return 0;
        case 1:
            e->vel[2] -= e->vel[2] / 14.0f * f;
            physics(f);
            if (e->done) {
                script_play_hit(24);
                set_physics(0.0f, 0.0f, 0.0f, 0.0f);
                e->wake = 45.0f + ai_unit() * 45.0f;
                e->step = 2;
            }
            return 0;
        case 2:
            lying_then_up(5);
            return 0;
        default:
            if (e->done) action_enter(A_NONE);
            return 1;
        }
    case A_LAUNCH_UP:
    case A_AIR_HIT_HEAVY:
    case A_AIR_HIT:
        switch (e->step) {
        case 0:
            if (e->action == A_LAUNCH_UP) {
                /* Rises, then holds its falling pose until it lands. */
                if (e->frame >= 35.0f && e->wake == 0.0f) { script_play_hit(22); e->wake = 1.0f; }
                physics(f);
                if (e->vel[1] < 0.0f && on_floor()) {
                    e->air = 0;
                    script_play_hit(13);
                    e->step = 2;
                }
                return 0;
            }
            /* Hangs where it was hit. */
            e->timer -= f;
            if (e->timer < 0.0f) e->step = 1;
            return 0;
        case 1:
            if (e->done) script_play_hit(22);
            physics(f);
            if (on_floor()) {
                e->air = 0;
                script_play_hit(13);
                e->step = 2;
            }
            return 0;
        case 2:
            if (e->done) {
                script_play_hit(23);
                e->wake = 45.0f + ai_unit() * 45.0f;
                e->step = 3;
            }
            return 1;
        case 3:
            lying_then_up(6);
            return 1;
        default:
            if (e->done) action_enter(A_NONE);
            return 1;
        }
    case A_WALK:
        turn(px, pz, 0x100, f);
        return 1;
    case A_DEATH:
        /* Collapses, then goes. Its body fading out isn't in yet. */
        if (e->done) e->active = 0;
        return 1;
    case A_SWING:
        turn_window(px, pz, 15.0f, 30.0f, ANGLE(728.0f), f);  /* 4 degrees */
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_SWING_WIDE:
        turn_window(px, pz, 0.0f, 30.0f, ANGLE(364.0f), f);   /* 2 degrees */
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_LEAP:
        turn(px, pz, 0x100, f);
        if (e->step == 0) {
            e->timer -= f;
            if (e->timer > 0.0f) e->yaw += e->yaw_rate * f;
            if (e->done) {
                script_play(56);
                /* One of two arcs, at random. */
                if (ai_random() & 1u) set_physics(0.0f, 44.0f, 4.0f, -2.0f);
                else set_physics(0.0f, 33.0f, 8.0f, -1.5f);
                physics(f);
                e->step = 1;
            }
        } else if (e->step == 1) {
            physics(f);
            if (e->vel[1] < 0.0f && on_floor()) {
                script_play(57);
                set_physics(0.0f, 0.0f, 0.0f, 0.0f);
                e->step = 2;
            }
        } else if (e->done) {
            action_enter(A_NONE);
        }
        return 0;
    case A_STEP_LEFT:
    case A_STEP_RIGHT:
        turn(px, pz, 0x100, f);
        turn_window(px, pz, 25.0f, 30.0f, ANGLE(2731.0f) / MOTION_FPS, f);
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_STEP_BACK:
        turn(px, pz, 0x100, f);
        turn_window(px, pz, 35.0f, 40.0f, ANGLE(2731.0f) / MOTION_FPS, f);
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_TAUNT_A:
    case A_TAUNT_B:
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_WARP_FAR:
    case A_WARP_MID:
    case A_WARP_NEAR:
        /* hl-46: hidden from frame 44 to 54, moved at 45. */
        e->timer += f;
        if (e->step == 0 && e->timer >= 44.0f) { e->hidden = 1; e->step = 1; }
        if (e->step == 1 && e->timer >= 45.0f) {
            warp_to(px, pz, e->action == A_WARP_FAR ? 900.0f : e->action == A_WARP_MID ? 600.0f : 300.0f);
            e->step = 2;
        }
        if (e->step == 2 && e->timer >= 54.0f) { e->hidden = 0; e->step = 3; }
        if (e->done) { e->hidden = 0; action_enter(A_NONE); }
        return 0;
    case A_WARP_STRIKE:
        /* hl-60, then from its frame 104 hidden and gone to 300 cm from Dante, where it comes out of the
         * Pride's swing (script 87, hl-47) from frame 25, shown 9 frames later. */
        e->timer += f;
        if (e->step == 0 && e->timer >= 104.0f) {
            e->hidden = 1;
            warp_to(px, pz, 300.0f);
            script_play(87);
            e->frame += 25.0f;
            e->step = 1;
        }
        if (e->step == 1 && e->timer >= 113.0f) { e->hidden = 0; e->step = 2; }
        if (e->step == 2 && e->done) action_enter(A_NONE);
        return e->step != 1;
    case A_WIND_UP:
        turn(px, pz, 0x100, f);
        if (e->done) action_enter(A_SPEW);
        return 1;
    case A_SPEW:
        /* Turns to him over frames 15 to 35, then at 129 a burst three spheres long (radius 100, out to
         * 400 cm at mouth height) for about 3 frames. A heavy hit. */
        e->timer += f;
        if (e->timer >= 15.0f && e->timer <= 35.0f) turn(px, pz, ANGLE(546.0f), f);
        if (e->step == 0 && e->timer >= 129.0f) {
            float c = cosf(e->yaw), s = sinf(e->yaw);
            int k;
            for (k = 1; k <= 3; ++k) {
                float d = k == 1 ? 100.0f : k == 2 ? 250.0f : 400.0f;
                float x = e->pos[0] + s * d - px, z = e->pos[2] + c * d - pz, y = e->pos[1] + 100.0f;
                float near_y = y < dante_y + DANTE_LOW ? dante_y + DANTE_LOW : y > dante_y + DANTE_HIGH ? dante_y + DANTE_HIGH : y;
                float reach = 100.0f + DANTE_RADIUS;
                if (x * x + (y - near_y) * (y - near_y) + z * z < reach * reach) { land_attack(1); break; }
            }
            e->step = 1;
        }
        if (e->done) action_enter(A_NONE);
        return 1;
    default:
        return 0;
    }
}

/* ---- Plans. ---- */

static void plan_set(int p) { e->plan = (unsigned char)p; e->plan_step = 0; }

/* Pick from the game's lists: which list by whether it may attack, which row by how far Dante is. */
static void decide(float distance) {
    int attack = attack_token <= 0.0f, band = 0, column = 0, i;
    const EnemyAiRow *row;
    float r, total = 0.0f;
    if (attack) attack_token = ATTACK_TOKEN_SECONDS;
    e->gauge = 0.0f;
    const EnemyAiClass *ai = ai_class();
    while (band < ai->band_count - 1 && distance >= ai->bands[band]) ++band;
    while (column < 6 && ai->times[column] <= AI_RANK) ++column;
    row = &ai->rows[attack ? 0 : 1][band][column];
    r = ai_unit() * 99.0f;
    for (i = 0; i < row->count; ++i) {
        total += row->picks[i].weight;
        if (r < total) break;
    }
    if (i >= row->count) i = row->count - 1;
    if (i >= 0) plan_set(row->picks[i].plan);
}

static void plan_run_action(int action) {
    if (e->plan_step == 0) {
        action_enter(action);
        e->plan_step = 1;
    } else if (e->action != action) {
        plan_set(P_DECIDE);
    }
}

static void plan_update(float px, float pz, float f) {
    switch (e->plan) {
    case P_DECIDE:
        plan_set(attacks_on ? P_WATCH : PLAN_STAND);
        break;
    case PLAN_STAND:
        if (e->plan_step == 0) {
            action_enter(A_NONE);
            script_play(0);
            e->plan_step = 1;
        }
        if (attacks_on) plan_set(P_DECIDE);
        break;
    case P_WATCH: {
        /* Walk and watch. Time spent near him, in front of him, fills a gauge; full, it decides. */
        float dx = px - e->pos[0], dz = pz - e->pos[2], distance = sqrtf(dx * dx + dz * dz), d;
        int i, column = 0;
        if (e->plan_step == 0) {
            action_enter(A_WALK);
            e->plan_step = 1;
        }
        if (!attacks_on) { plan_set(P_DECIDE); break; }
        if (e->pushed) {
            /* Crowded out: straight into a leap, without the gauge or the lists. */
            plan_set(P_LEAP);
            break;
        }
        d = angle_to(px, pz);
        {
            const EnemyAiClass *ai = ai_class();
            for (i = 0; i < ai->zone_count; ++i) {
                if (distance < ai->zones[i][0] && fabsf(d) < ai->zones[i][1] * PI / 180.0f) {
                    e->gauge += ai->zones[i][2] * f;
                    break;
                }
            }
            while (column < 6 && ai->times[column] <= AI_RANK) ++column;
            if (e->gauge > ai->thresholds[column < 6 ? column : 5]) decide(distance);
        }
        break;
    }
    case P_DEATH:
        if (e->plan_step == 0) { action_enter(A_DEATH); e->plan_step = 1; }
        break;
    case P_FLINCH_FRONT: plan_run_action(A_FLINCH_FRONT); break;
    case P_FLINCH_BACK: plan_run_action(A_FLINCH_BACK); break;
    case P_LAUNCHED: plan_run_action(A_LAUNCHED); break;
    case P_LAUNCHED_BACK: plan_run_action(A_LAUNCHED_BACK); break;
    case P_LAUNCH_UP: e->wake = 0.0f; plan_run_action(A_LAUNCH_UP); break;
    case P_AIR_HIT_HEAVY: plan_run_action(A_AIR_HIT_HEAVY); break;
    case P_AIR_HIT: plan_run_action(A_AIR_HIT); break;
    case P_SWING: plan_run_action(A_SWING); break;
    case P_SWING_WIDE: plan_run_action(A_SWING_WIDE); break;
    case P_LEAP:
        if (e->plan_step == 0) {
            action_enter(A_LEAP);
            /* Swing round a tenth of the way each of its first ten frames. */
            e->yaw_rate = angle_to(px, pz) / 10.0f;
            e->plan_step = 1;
        } else if (e->action != A_LEAP) {
            plan_set(P_DECIDE);
        }
        break;
    case P_STEP_LEFT: plan_run_action(A_STEP_LEFT); break;
    case P_STEP_RIGHT: plan_run_action(A_STEP_RIGHT); break;
    case P_STEP_BACK: plan_run_action(A_STEP_BACK); break;
    case P_TAUNT_A: plan_run_action(A_TAUNT_A); break;
    case P_TAUNT_B: plan_run_action(A_TAUNT_B); break;
    case P_WARP_STRIKE: plan_run_action(A_WARP_STRIKE); break;
    case P_WARP_FAR: plan_run_action(A_WARP_FAR); break;
    case P_WARP_MID: plan_run_action(A_WARP_MID); break;
    case P_WARP_NEAR: plan_run_action(A_WARP_NEAR); break;
    case P_SPEW: plan_run_action(A_SPEW); break;
    case P_SPEW_WOUND:
        /* Winds up, then spews. */
        if (e->plan_step == 0) { action_enter(A_WIND_UP); e->plan_step = 1; }
        else if (e->action != A_WIND_UP && e->action != A_SPEW) plan_set(P_DECIDE);
        break;
    default:
        /* A pick this enemy's lists can only reach at higher ranks, not ported yet. */
        plan_set(P_DECIDE);
        break;
    }
}

/* ---- Being hit. ---- */

static int is_lying(void) {
    return ((e->action == A_LAUNCHED || e->action == A_LAUNCHED_BACK) && e->step >= 1) ||
           ((e->action == A_AIR_HIT || e->action == A_AIR_HIT_HEAVY || e->action == A_LAUNCH_UP) && e->step >= 2);
}

void enemy_hit(HitKind kind, float from_x, float from_z) {
    float toward;
    int behind;
    if (!e->active) return;
    ++hits;  /* infinite health: hits only cause reactions */
    {
        /* On the side facing the attacker, at chest height. */
        float d[2] = {from_x - e->pos[0], from_z - e->pos[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1]);
        float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
        if (len > 1.0f) { at[0] += d[0] / len * 40.0f; at[2] += d[1] / len * 40.0f; }
        fx_hit_spark(at);
    }
    if (e->kind == ENEMY_ENIGMA) { enigma_hit(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_GIGAPEDE) { giga_hit(kind); return; }
    if (e->plan == P_DEATH) return;
    if (mortal) {
        e->health = (signed char)(e->health - (kind == HIT_LIGHT ? 1 : 2));
        if (e->health <= 0) {
            e->air = 0;
            plan_set(P_DEATH);
            return;
        }
    }
    if (is_lying()) return;
    toward = atan2f(from_x - e->pos[0], from_z - e->pos[2]);
    behind = fabsf(wrap_angle(toward - e->yaw)) > PI * 0.5f;
    if (e->air) {
        if (kind == HIT_SLAM) {
            e->hit_angle = -90.0f;
            e->hit_speed = SLAM_SPEED;
            plan_set(P_LAUNCHED);
        } else {
            plan_set(kind == HIT_HEAVY ? P_AIR_HIT_HEAVY : P_AIR_HIT);
        }
        return;
    }
    switch (kind) {
    case HIT_LIGHT:
    case HIT_AIR:
        plan_set(behind ? P_FLINCH_BACK : P_FLINCH_FRONT);
        break;
    case HIT_LAUNCH:
        plan_set(P_LAUNCH_UP);
        break;
    case HIT_HEAVY:
    case HIT_SLAM:
        e->hit_angle = HEAVY_ANGLE;
        e->hit_speed = HEAVY_SPEED;
        /* Square on to whoever hit it; the back version keeps its back to them. */
        e->yaw = behind ? toward + PI : toward;
        plan_set(behind ? P_LAUNCHED_BACK : P_LAUNCHED);
        break;
    }
}

void enemy_push_out(float *x, float *z, float r) {
    Enemy *selected = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].kind == ENEMY_GIGAPEDE) continue;
        const Enemy *o = &enemies[i];
        float dx, dz, d, min;
        e = &enemies[i];
        if (o->active && is_lying()) continue;
        if (!o->active) continue;
        dx = *x - o->pos[0]; dz = *z - o->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        min = r + ENEMY_RADIUS;
        if (d < min && d > 1e-3f && fabsf(o->pos[1] - o->floor_y) < 150.0f) {
            *x = o->pos[0] + dx / d * min;
            *z = o->pos[2] + dz / d * min;
        }
    }
    e = selected;
}

static void trace_row(int did_land) {
    TraceRow *t;
    if (!trace || trace_count >= TRACE_ROWS) return;
    t = &trace[trace_count++];
    t->tick = trace_tick; t->who = (short)(e - enemies); t->plan = e->plan; t->action = e->action;
    t->motion = (short)e->motion; t->landed = (short)did_land;
    t->x = e->pos[0]; t->y = e->pos[1] - e->floor_y; t->z = e->pos[2]; t->yaw = e->yaw; t->gauge = e->gauge; t->frame = e->frame;
}

void enemy_trace_start(void) {
    if (!trace) trace = malloc(sizeof(TraceRow) * TRACE_ROWS);
    trace_count = 0;
}

void enemy_trace_write(const char *path) {
    FILE *f;
    int i;
    if (!trace || !(f = fopen(path, "w"))) return;
    for (i = 0; i < trace_count; ++i) {
        const TraceRow *t = &trace[i];
        fprintf(f, "%d %d %d %d %d %d %.1f %.1f %.1f %.3f %.1f %.1f\n", t->tick, t->who, t->plan, t->action, t->motion,
                t->landed, t->x, t->y, t->z, t->yaw, t->gauge, t->frame);
    }
    fclose(f);
}

void enemy_set_ai_only(int on) { ai_only = on; }

static void land_attack(int heavy) {
    trace_row(heavy ? 2 : 1);
    landed = 1;
    landed_x = e->pos[0];
    landed_z = e->pos[2];
    landed_heavy = heavy;
}

/* Bodies, for one on the ground: other grounded enemies shove it, and it stops at Dante. */
static void keep_apart(float px, float py, float pz, float f) {
    float min = ENEMY_RADIUS * 2.0f, shoved = 0.0f, dx, dz, d2;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        if (o == e || !o->active || fabsf(o->pos[1] - e->pos[1]) > 100.0f) continue;
        dx = e->pos[0] - o->pos[0];
        if (dx > min || dx < -min) continue;
        dz = e->pos[2] - o->pos[2];
        d2 = dx * dx + dz * dz;
        if (d2 < min * min && d2 > 1e-3f) {
            float d = sqrtf(d2), move = min - d;
            e->pos[0] += dx / d * move;
            e->pos[2] += dz / d * move;
            if (move > shoved) shoved = move;
        }
    }
    e->pushed = shoved > PUSH_LEAPS * f;
    min = ENEMY_RADIUS + PLAYER_RADIUS;
    dx = e->pos[0] - px; dz = e->pos[2] - pz;
    d2 = dx * dx + dz * dz;
    if (d2 < min * min && d2 > 1e-3f && fabsf(py - e->pos[1]) < 150.0f) {
        float d = sqrtf(d2), move = (min - d) * ENEMY_YIELDS;
        e->pos[0] += dx / d * move;
        e->pos[2] += dz / d * move;
    }
}

static void evaluate_pose(void);

/* The live attack spheres against Dante. Bone 0 is the enemy's own space; others follow the pose. */
static void test_attack(float px, float py, float pz) {
    int k, posed = 0;
    float c = cosf(e->yaw), s = sinf(e->yaw);
    if (e->hit_done) return;
    for (k = 0; k < 2; ++k) {
        const EnemyAiCap *cap;
        float l[3], x, y, z, near_y;
        if (e->hit_left[k] <= 0.0f) continue;
        cap = &enemy_ai_caps[e->hit_cap[k]];
        if (cap->bone) {
            if (!posed) { evaluate_pose(); posed = 1; }
            {
                const float (*r)[3] = world_rot[cap->bone].m;
                const float *t = world_pos[cap->bone];
                l[0] = t[0] + r[0][0] * cap->x + r[0][1] * cap->y + r[0][2] * cap->z;
                l[1] = t[1] + r[1][0] * cap->x + r[1][1] * cap->y + r[1][2] * cap->z;
                l[2] = t[2] + r[2][0] * cap->x + r[2][1] * cap->y + r[2][2] * cap->z;
            }
        } else {
            l[0] = cap->x; l[1] = cap->y; l[2] = cap->z;
        }
        x = e->pos[0] + l[0] * c + l[2] * s;
        y = e->pos[1] + l[1];
        z = e->pos[2] + l[2] * c - l[0] * s;
        near_y = y < py + DANTE_LOW ? py + DANTE_LOW : y > py + DANTE_HIGH ? py + DANTE_HIGH : y;
        x -= px; y -= near_y; z -= pz;
        {
            float reach = cap->radius + DANTE_RADIUS;
            if (x * x + y * y + z * z < reach * reach) {
                e->hit_done = 1;
                land_attack(e->hit_type == 1);
                return;
            }
        }
    }
}

/* The order of the game's own per-frame function (FUN_00367670). */
static void update_one(float dt, float px, float py, float pz) {
    float f = dt * MOTION_FPS, root[3];
    int plan_was = e->plan, action_was = e->action;
    if (!e->active) return;
    dante_y = py;
    if (e->kind == ENEMY_GIGAPEDE) { giga_part_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_ENIGMA) { enigma_update(f, px, py, pz); return; }
    script_tick(f, root);
    {
        int use_root = action_update(px, pz, f);
        if (use_root) move_local(root[0], 0.0f, root[2]);
        plan_update(px, pz, f);
    }
    if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
    if (trace && (e->plan != plan_was || e->action != action_was)) trace_row(0);
    /* The floor, and falling onto it when not in a jump of its own. */
    {
        float g;
        int leaping = e->action == A_LEAP && e->step == 1;
        if (!e->air && !leaping) keep_apart(px, py, pz, f);
        else e->pushed = 0;
        if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
        if (e->air || leaping) {
            if (e->pos[1] < e->floor_y) e->pos[1] = e->floor_y;
        } else if (e->pos[1] > e->floor_y) {
            e->fall -= GRAVITY_STEP * f;
            e->pos[1] += e->fall * f;
            if (e->pos[1] <= e->floor_y) { e->pos[1] = e->floor_y; e->fall = 0.0f; }
        } else {
            e->pos[1] = e->floor_y;
            e->fall = 0.0f;
        }
    }
}

void enemy_update(float dt, float px, float py, float pz) {
    Enemy *selected = e;
    int i;
    ++trace_tick;
    if (attack_token > 0.0f) attack_token -= dt / (player_step_scale > 0.0f ? player_step_scale : 1.0f);
    for (i = 0; i < ENEMY_MAX; ++i) {
        e = &enemies[i];
        update_one(dt, px, py, pz);
    }
    arrows_update(dt * MOTION_FPS, px, py, pz);
    e = selected;
}

/* Sample motion -> local rotations/offsets -> world transforms. */
static void evaluate_pose(void) {
    static MotionSample a, b, over;
    M3 local;
    int i, k;
    motion_sample(motion_get(e->motion), e->frame, &a);
    if (e->overlay_motion >= 0) {
        motion_sample(motion_get(e->overlay_motion), e->overlay_frame, &over);
        for (i = e->overlay_lo; i <= e->overlay_hi && i < MOTION_MAX_BONES; ++i) {
            memcpy(a.rot[i], over.rot[i], sizeof(a.rot[i])); memcpy(a.has_rot[i], over.has_rot[i], sizeof(a.has_rot[i]));
            memcpy(a.trans[i], over.trans[i], sizeof(a.trans[i])); memcpy(a.has_trans[i], over.has_trans[i], sizeof(a.has_trans[i]));
        }
    }
    if (e->fade > 0.0f && e->prev_motion >= 0) motion_sample(motion_get(e->prev_motion), e->prev_frame, &b);
    const EnemyModel *m = &enemy_models[e->kind];
    for (i = 0; i < m->bone_count; ++i) {
        float r[3], t[3] = {m->bone_local[i].x, m->bone_local[i].y, m->bone_local[i].z};
        int p = m->bone_parent[i];
        for (k = 0; k < 3; ++k) {
            float ra = i < MOTION_MAX_BONES && a.has_rot[i][k] ? a.rot[i][k] : 0.0f;
            float ta = i < MOTION_MAX_BONES && a.has_trans[i][k] ? a.trans[i][k] : t[k];
            if (e->fade > 0.0f && e->prev_motion >= 0 && i < MOTION_MAX_BONES) {
                float rb = b.has_rot[i][k] ? b.rot[i][k] : ra;
                float tb = b.has_trans[i][k] ? b.trans[i][k] : ta;
                ra += (rb - ra) * e->fade;
                ta += (tb - ta) * e->fade;
            }
            r[k] = ra;
            t[k] = ta;
        }
        if (i == 0) t[0] = t[1] = t[2] = 0.0f;  /* root motion handled by the state machine */
        m_euler(&local, r[0], r[1], r[2]);
        if (p < 0) {
            world_rot[i] = local;
            memcpy(world_pos[i], t, sizeof(t));
        } else {
            m_mul(&world_rot[i], &world_rot[p], &local);
            for (k = 0; k < 3; ++k)
                world_pos[i][k] = world_pos[p][k] + world_rot[p].m[k][0] * t[0] + world_rot[p].m[k][1] * t[1] + world_rot[p].m[k][2] * t[2];
        }
    }
}

static void skin(void) {
    int i, k;
    const EnemyModel *m = &enemy_models[e->kind];
    for (i = 0; i < m->vertex_count; ++i) {
        const Dmc3Vec3 *s = &m->positions[i], *n = &m->normals[i];
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (k = 0; k < 3; ++k) {
            int w = m->weights[i][k], b;
            float wf, dx, dy, dz;
            const float (*r)[3];
            if (!w) continue;
            b = m->bones[i][k];
            wf = (float)w * (1.0f / 31.0f);
            r = world_rot[b].m;
            dx = s->x - m->bone_rest[b].x; dy = s->y - m->bone_rest[b].y; dz = s->z - m->bone_rest[b].z;
            px += wf * (r[0][0] * dx + r[0][1] * dy + r[0][2] * dz + world_pos[b][0]);
            py += wf * (r[1][0] * dx + r[1][1] * dy + r[1][2] * dz + world_pos[b][1]);
            pz += wf * (r[2][0] * dx + r[2][1] * dy + r[2][2] * dz + world_pos[b][2]);
            nx += wf * (r[0][0] * n->x + r[0][1] * n->y + r[0][2] * n->z);
            ny += wf * (r[1][0] * n->x + r[1][1] * n->y + r[1][2] * n->z);
            nz += wf * (r[2][0] * n->x + r[2][1] * n->y + r[2][2] * n->z);
        }
        e->vertices[i].x = px; e->vertices[i].y = py; e->vertices[i].z = pz;
        e->vertices[i].nx = nx; e->vertices[i].ny = ny; e->vertices[i].nz = nz;
    }
    sceKernelDcacheWritebackRange(e->vertices, sizeof(Vertex) * m->vertex_count);
}

#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

/* Each dummy's bones update on its own turn, spread out over the frames so they don't all
 * cost the same frame. A new motion (a hit, say) always shows at once. */
static void update_pose(float elapsed, float interval, int index, int count) {
    int moved = e->motion != e->posed_motion || e->frame != e->posed_frame || e->overlay_motion >= 0;
    if (!moved) return;
    e->anim_wait -= elapsed;
    if (interval > 0.0f && e->motion == e->posed_motion && e->anim_wait > 0.0f) return;
    e->anim_wait = interval + interval * (float)index / (float)(count > 0 ? count : 1);
    if (e->posed_motion >= 0 && e->motion == e->posed_motion) e->anim_wait = interval;
    e->posed_motion = e->motion;
    e->posed_frame = e->frame;
    evaluate_pose();
    skin();
    memcpy(e->hand_rot, world_rot[kind_hand[e->kind]].m, sizeof(e->hand_rot));
    memcpy(e->hand_pos, world_pos[kind_hand[e->kind]], sizeof(e->hand_pos));
    ++poses_done;
}

static void draw_one(void) {
    int i, bound = -1;
    if (e->hidden) return;
    const EnemyModel *m = &enemy_models[e->kind];
    const EnemyTextureSet *set = &enemy_texture_sets[m->texture_set];
    void *const *tex = textures[m->texture_set];
    if (!e->active) return;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {e->pos[0], e->pos[1], e->pos[2]};
        sceGumTranslate(&p);
        sceGumRotateY(e->yaw);
    }
    if (test_no_texture) sceGuDisable(GU_TEXTURE_2D); else sceGuEnable(GU_TEXTURE_2D);
    if (test_no_lighting) sceGuDisable(GU_LIGHTING); else sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);     /* swizzled at load */
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    for (i = 0; i < m->object_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if ((int)o->texture != bound) {
            bound = (int)o->texture;
            sceGuClutLoad(256 / 8, set->palettes[bound]);
            sceGuTexImage(0, set->textures[bound].width, set->textures[bound].height, set->textures[bound].width, tex[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[e->kind] + o->index_start, e->vertices);
    }
    if (e->kind != ENEMY_ENIGMA) {
        /* The weapon in its hand: a Seven Hell's scythe. The Enigma's bows are part of its body. */
        ScePspFMatrix4 mm;
        const float (*r)[3] = e->hand_rot;
        mm.x.x = r[0][0]; mm.x.y = r[1][0]; mm.x.z = r[2][0]; mm.x.w = 0.0f;
        mm.y.x = r[0][1]; mm.y.y = r[1][1]; mm.y.z = r[2][1]; mm.y.w = 0.0f;
        mm.z.x = r[0][2]; mm.z.y = r[1][2]; mm.z.z = r[2][2]; mm.z.w = 0.0f;
        mm.w.x = e->hand_pos[0]; mm.w.y = e->hand_pos[1]; mm.w.z = e->hand_pos[2]; mm.w.w = 1.0f;
        sceGumPushMatrix();
        sceGumMultMatrix(&mm);
        bound = m->weapon_texture;
        sceGuClutLoad(256 / 8, set->palettes[bound]);
        sceGuTexImage(0, set->textures[bound].width, set->textures[bound].height, set->textures[bound].width, tex[bound]);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, VTYPE, m->weapon_index_count, weapon_indices[e->kind], weapon_vertices[e->kind]);
        sceGumPopMatrix();
    }
}

void enemy_draw(void) {
    Enemy *selected = e;
    int i, rate = enemy_anim_rate(), count = enemy_count(), turn = 0;
    if (ai_only) return;
    /* Real time: the speed setting and pauses change how far the motions move, not how often
     * the PSP can afford to pose them. */
    unsigned int now = sceKernelGetSystemTimeLow();
    float elapsed = (float)(now - anim_last_time) / 1000000.0f;
    anim_last_time = now;
    for (i = 0; i < ENEMY_MAX; ++i) {
        e = &enemies[i];
        if (!e->active) continue;
        if (e->kind == ENEMY_GIGAPEDE) { if (e->step == 0) giga_draw(); continue; }
        update_pose(elapsed, rate > 0 ? 1.0f / (float)rate : 0.0f, turn++, count);
        draw_one();
    }
    arrows_draw();
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    e = selected;
}

/* ---- The Enigma (CEm013): the archer on the walls (build/hdref/enemy_specs.md, rank 0). It never walks:
 * it turns in place to face Dante, and each of its three pairs of bow arms (bones 5-15, 16-26, 27-37) has
 * its own cooldown. When one runs out and the attack token is free (the same 3 seconds as the Seven Hells'),
 * that pair shoots: one arrow, or a fan of three. Arrows home on Dante when he's near and are lobbed onto
 * his feet when he's far. Simplified: no hops, no ledge falls, no statue spawns; the token is taken per
 * volley as the game does. The aim lasts about two loops of the arms' motion, as decoded; not measured. */
#define ENIGMA_WAKE 9000.0f
#define ENIGMA_NEAR 1000.0f
#define ARROW_MAX 24
#define ARROW_LIFE 240.0f
#define ARROW_SPEED 35.0f
#define ARROW_TOP_SPEED 65.0f
#define ARROW_RADIUS 30.0f
/* type 0 an Enigma's arrow; 1 the Gigapede's homing sphere; 2 its orb that drops, then rolls. */
typedef struct { int active, homing, type; float pos[3], vel[3], life, lift, goal[2]; } Arrow;
static Arrow arrows[ARROW_MAX];
static int enigma_motion_idle, enigma_motion_flinch, enigma_motion_heavy, enigma_motion_death;
static int enigma_motion_aim, enigma_motion_release, enigma_motion_fan;
static const unsigned char arm_lo[3] = {5, 16, 27}, arm_hi[3] = {15, 26, 37}, arm_hand[3] = {9, 20, 31};
static const float arm_far_base[3] = {80.0f, 100.0f, 120.0f}, arm_near_base[3] = {230.0f, 200.0f, 250.0f};

static int enigma_init(void) {
    enigma_motion_idle = motion_find("ea01");      /* 374 frames, loops */
    enigma_motion_flinch = motion_find("eb00");    /* 100: light flinch */
    enigma_motion_heavy = motion_find("eb02");     /* 102: heavier */
    enigma_motion_death = motion_find("eb23");     /* 123 */
    enigma_motion_aim = motion_find("ec00");       /* 300: draw and aim, loops on 82 frames */
    enigma_motion_release = motion_find("ec01");   /* 301: release, 20 frames */
    enigma_motion_fan = motion_find("ec02");       /* 302: the fan */
    memset(arrows, 0, sizeof(arrows));
    return enigma_motion_idle >= 0;
}

static void enigma_play(int motion) {
    if (motion < 0) return;
    e->prev_motion = e->motion;
    e->prev_frame = e->frame;
    e->fade = e->motion >= 0 ? 1.0f : 0.0f;
    e->fade_rate = 1.0f / 10.0f;
    e->motion = motion;
    e->frame = 0.0f;
    e->done = 0;
}

static void enigma_spawn(void) {
    int k;
    e->plan = P_WATCH;
    e->action = A_NONE;
    e->motion = -1;
    enigma_play(enigma_motion_idle);
    e->fade = 0.0f;
    e->loop = 1;
    e->shoot_set = -1;
    /* The first volley uses the far base alone. */
    for (k = 0; k < 3; ++k) e->shoot_timer[k] = arm_far_base[k];
}

static void fire_arrow(float px, float py, float pz, float offset) {
    int i;
    float hand[3], c = cosf(e->yaw), s = sinf(e->yaw), dx, dy, dz, d;
    const float *h = e->hand_pos;   /* set by the last pose: kind_hand isn't per arm, so use the arm's own */
    (void)h;
    evaluate_pose();
    {
        const float *w = world_pos[arm_hand[e->shoot_set >= 0 ? e->shoot_set : 0]];
        hand[0] = e->pos[0] + w[0] * c + w[2] * s;
        hand[1] = e->pos[1] + w[1];
        hand[2] = e->pos[2] + w[2] * c - w[0] * s;
    }
    for (i = 0; i < ARROW_MAX && arrows[i].active; ++i) {}
    if (i >= ARROW_MAX) return;
    trace_row(5);   /* for tests: an arrow let go */
    dx = px - hand[0]; dy = py + 100.0f - hand[1]; dz = pz - hand[2];
    d = sqrtf(dx * dx + dz * dz);
    arrows[i].active = 1;
    arrows[i].type = 0;
    arrows[i].life = ARROW_LIFE;
    memcpy(arrows[i].pos, hand, sizeof(hand));
    if (d < ENIGMA_NEAR) {
        /* Straight at him, steering after him. */
        float l = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f;
        arrows[i].homing = 1;
        arrows[i].vel[0] = dx / l * ARROW_SPEED; arrows[i].vel[1] = dy / l * ARROW_SPEED; arrows[i].vel[2] = dz / l * ARROW_SPEED;
        arrows[i].lift = 0.2f;
    } else {
        /* Lobbed onto his feet, a little short or long, falling 0.1 a frame each frame. */
        float tx = px + dx / d * offset, tz = pz + dz / d * offset, time = (d + offset) / 30.0f;
        if (time < 20.0f) time = 20.0f;
        arrows[i].homing = 0;
        arrows[i].vel[0] = (tx - hand[0]) / time;
        arrows[i].vel[2] = (tz - hand[2]) / time;
        arrows[i].vel[1] = (py - hand[1] + 0.5f * 0.1f * time * time) / time;
    }
}

static void arrows_clear(int i) { arrows[i].active = 0; }

static void arrows_update(float f, float px, float py, float pz) {
    int i;
    for (i = 0; i < ARROW_MAX; ++i) {
        Arrow *a = &arrows[i];
        float g, dx, dy, dz;
        if (!a->active) continue;
        a->life -= f;
        if (a->life <= 0.0f) { a->active = 0; continue; }
        if (a->type == 1) {
            /* The Gigapede's sphere: accelerates at him (chest height plus a little), up to 30 a frame. */
            float tx = px - a->pos[0], ty = py + 150.0f - a->pos[1], tz = pz - a->pos[2];
            float tl = sqrtf(tx * tx + ty * ty + tz * tz) + 0.01f, v;
            a->vel[0] += tx / tl * f; a->vel[1] += ty / tl * f; a->vel[2] += tz / tl * f;
            v = sqrtf(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
            if (v > 30.0f) { float k = 30.0f / v; a->vel[0] *= k; a->vel[1] *= k; a->vel[2] *= k; }
        } else if (a->type == 2) {
            /* Falls 10 a frame to the floor's height, then rolls at 30 a frame toward where Dante was. */
            float g2;
            if (a->lift > 0.0f) {
                a->vel[0] = a->vel[2] = 0.0f; a->vel[1] = -10.0f;
                if (player_floor_fn && player_floor_fn(a->pos[0], a->pos[1], a->pos[2], 0.0f, &g2) && a->pos[1] < g2 + 60.0f) {
                    float dx2 = a->goal[0] - a->pos[0], dz2 = a->goal[1] - a->pos[2], l2 = sqrtf(dx2 * dx2 + dz2 * dz2) + 0.01f;
                    a->lift = 0.0f; a->pos[1] = g2 + 60.0f;
                    a->vel[0] = dx2 / l2 * 30.0f; a->vel[1] = 0.0f; a->vel[2] = dz2 / l2 * 30.0f;
                }
            }
        } else if (a->homing) {
            /* Turn toward his chest, and the small upward steer that fades. */
            float tx = px - a->pos[0], ty = py + 100.0f - a->pos[1], tz = pz - a->pos[2];
            float tl = sqrtf(tx * tx + ty * ty + tz * tz) + 0.01f, v;
            a->vel[0] += tx / tl * 2.0f * f; a->vel[1] += ty / tl * 2.0f * f + a->lift * f; a->vel[2] += tz / tl * 2.0f * f;
            a->lift -= 0.1f * f; if (a->lift < 0.0f) a->lift = 0.0f;
            v = sqrtf(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
            if (v > ARROW_TOP_SPEED) { float k = ARROW_TOP_SPEED / v; a->vel[0] *= k; a->vel[1] *= k; a->vel[2] *= k; }
        } else {
            a->vel[1] -= 0.1f * f;
        }
        a->pos[0] += a->vel[0] * f; a->pos[1] += a->vel[1] * f; a->pos[2] += a->vel[2] * f;
        /* Dante: his capsule. */
        dx = a->pos[0] - px; dz = a->pos[2] - pz;
        dy = a->pos[1] < py + DANTE_LOW ? a->pos[1] - (py + DANTE_LOW) : a->pos[1] > py + DANTE_HIGH ? a->pos[1] - (py + DANTE_HIGH) : 0.0f;
        {
            float r = a->type ? 50.0f : ARROW_RADIUS;
            if (dx * dx + dy * dy + dz * dz >= (r + DANTE_RADIUS) * (r + DANTE_RADIUS)) goto missed;
        }
        {
            trace_row(1);
            landed = 1;
            landed_x = a->pos[0] - a->vel[0];
            landed_z = a->pos[2] - a->vel[2];
            landed_heavy = 0;
            a->active = 0;
            continue;
        }
missed:
        /* The floor (the Gigapede's orbs keep going: they're on it, or come down to it). */
        if (a->type == 0 && player_floor_fn && player_floor_fn(a->pos[0], a->pos[1] + 50.0f, a->pos[2], 0.0f, &g) && a->pos[1] < g) a->active = 0;
    }
}

static void arrows_draw(void) {
    int i, n = 0;
    struct { unsigned int color; float x, y, z; } *v;
    for (i = 0; i < ARROW_MAX; ++i) n += arrows[i].active;
    if (!n) return;
    v = sceGuGetMemory(sizeof(*v) * 2 * (unsigned)n);
    n = 0;
    for (i = 0; i < ARROW_MAX; ++i) {
        const Arrow *a = &arrows[i];
        float l;
        if (!a->active) continue;
        l = sqrtf(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]) + 0.01f;
        if (a->type) {
            /* A glowing orb, drawn as a short vertical streak with a trail. */
            v[n].color = a->type == 1 ? 0xffff60c0u : 0xffffd060u; v[n].x = a->pos[0]; v[n].y = a->pos[1] + 50.0f; v[n].z = a->pos[2]; ++n;
            v[n].color = a->type == 1 ? 0xff801060u : 0xff806010u; v[n].x = a->pos[0] - a->vel[0] / l * 120.0f; v[n].y = a->pos[1] - 50.0f; v[n].z = a->pos[2] - a->vel[2] / l * 120.0f; ++n;
            continue;
        }
        v[n].color = 0xff60d0ffu; v[n].x = a->pos[0]; v[n].y = a->pos[1]; v[n].z = a->pos[2]; ++n;
        v[n].color = 0xff2060a0u; v[n].x = a->pos[0] - a->vel[0] / l * 90.0f; v[n].y = a->pos[1] - a->vel[1] / l * 90.0f; v[n].z = a->pos[2] - a->vel[2] / l * 90.0f; ++n;
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
}

static void enigma_update(float f, float px, float py, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz);
    int k, near = d < ENIGMA_NEAR;
    const Motion *m;
    if (e->plan == P_DEATH) {
        e->overlay_motion = -1;
        if (e->motion >= 0) {
            m = motion_get(e->motion);
            e->frame += f;
            if (e->frame >= m->end_frame) { e->frame = m->end_frame; e->timer += f; }
        }
        if (e->timer > 20.0f) e->active = 0;
        return;
    }
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    /* Body: idle, or a reaction playing out. */
    m = motion_get(e->motion);
    e->frame += f;
    if (e->motion == enigma_motion_idle) e->frame = motion_wrap(m, e->frame);
    else if (e->frame >= m->end_frame) enigma_play(enigma_motion_idle);
    if (d > ENIGMA_WAKE || !attacks_on) return;
    /* Turn in place. */
    turn(px, pz, 0x100, f);
    /* Shooters. */
    if (e->shoot_set < 0) {
        for (k = 0; k < 3; ++k) {
            e->shoot_timer[k] -= f;
            if (e->shoot_timer[k] > 0.0f) continue;
            e->shoot_timer[k] = (near ? arm_near_base[k] + 200.0f * ai_unit() : arm_far_base[k] + 100.0f * ai_unit());
            if (attack_token > 0.0f) continue;
            attack_token = ATTACK_TOKEN_SECONDS;
            e->shoot_set = (signed char)k;
            e->shoot_fan = ai_unit() * 100.0f >= (near ? 80.0f : 60.0f);
            e->shoot_phase = 0;
            e->shoot_t = 0.0f;
            e->overlay_motion = e->shoot_fan ? enigma_motion_fan : enigma_motion_aim;
            e->overlay_frame = 0.0f;
            e->overlay_lo = arm_lo[k];
            e->overlay_hi = arm_hi[k];
            break;
        }
        return;
    }
    e->shoot_t += f;
    if (e->overlay_motion >= 0) {
        const Motion *om = motion_get(e->overlay_motion);
        e->overlay_frame += f;
        if (e->shoot_phase == 0) e->overlay_frame = motion_wrap(om, e->overlay_frame);
    }
    switch (e->shoot_phase) {
    case 0:
        /* Aim for about two loops, then 20 frames, then let go. */
        if (e->shoot_t >= (e->shoot_fan ? 130.0f : 164.0f) + 20.0f) {
            e->shoot_phase = 1;
            e->shoot_t = 0.0f;
            e->overlay_motion = enigma_motion_release;
            e->overlay_frame = 0.0f;
            if (e->shoot_fan) {
                fire_arrow(px, py, pz, -200.0f);
                fire_arrow(px + cosf(e->yaw) * 150.0f, py, pz - sinf(e->yaw) * 150.0f, 0.0f);
                fire_arrow(px - cosf(e->yaw) * 150.0f, py, pz + sinf(e->yaw) * 150.0f, 200.0f);
            } else {
                static const float offsets[4] = {200.0f, -150.0f, 100.0f, 0.0f};
                fire_arrow(px, py, pz, offsets[ai_random() & 3u]);
            }
        }
        break;
    default:
        if (e->shoot_t >= 20.0f) { e->overlay_motion = -1; e->shoot_set = -1; }
        break;
    }
}

static void enigma_hit(HitKind kind, float from_x, float from_z) {
    (void)from_x; (void)from_z;
    ++hits;
    {
        float at[3] = {e->pos[0], e->pos[1] + 200.0f, e->pos[2]};
        fx_hit_spark(at);
    }
    if (e->plan == P_DEATH) return;
    if (mortal) {
        e->health = (signed char)(e->health - (kind == HIT_LIGHT ? 1 : 2));
        if (e->health <= 0) {
            e->plan = P_DEATH;
            e->timer = 0.0f;
            e->overlay_motion = -1;
            e->shoot_set = -1;
            enigma_play(enigma_motion_death);
            return;
        }
    }
    /* A light hit doesn't stop it shooting; a heavier one does. */
    if (kind == HIT_LIGHT || kind == HIT_AIR) enigma_play(enigma_motion_flinch);
    else { enigma_play(enigma_motion_heavy); e->overlay_motion = -1; e->shoot_set = -1; }
}

/* ---- The Gigapede (CEm023), Mission 4's boss (build/hdref/enemy_specs.md section C, rank 0). Its head
 * runs the game's routes of movement paths through room 111's walls, holes and ceiling, the 7 segments
 * trailing 500 cm apart along the same track. Each segment is an enemy slot, so Dante's attacks, lock-on and
 * the room's fight work as for any enemy; damage goes to one shared pool. Attacks at the routes' own path
 * frames: event 1 and 2, a homing sphere from each segment; 3 and 4, an orb that drops, then rolls at where
 * Dante was. Simplified: drawn rigid (its motions aren't played), no shells, no rolls or shake-off, no beams
 * or head orb. Health is the spec's 2800 against the Pride's 250. */
#include "gigapede_runtime.h"
#define GIGA_PARTS 8               /* the head and 7 segments */
#define GIGA_SPACING 500.0f
#define GIGA_HISTORY 512
#define GIGA_HEALTH (ENEMY_HEALTH * 2800 / 250)
typedef struct { signed char path; float speed; short at[2]; signed char event[2]; } GigaStep;
/* Rank 0 routes: 11 the entrance, then 1, 4, 7 by phase. Where a route has two choices by Dante's height,
 * these are the ones for him on the floor. */
static const GigaStep giga_route[4][4] = {
    {{14, 1.0f, {-1, -1}, {0, 0}}, {0, 1.0f, {-1, -1}, {0, 0}}, {-1, 0, {0}, {0}}},
    {{3, 1.0f, {2000, -1}, {1, 0}}, {1, 2.0f, {1200, -1}, {1, 0}}, {-1, 0, {0}, {0}}},
    {{11, 1.0f, {1200, 1700}, {3, 3}}, {9, 1.0f, {1100, 2200}, {2, 0}}, {-1, 0, {0}, {0}}},
    {{3, 1.0f, {1300, -1}, {2, 0}}, {6, 0.8f, {1550, -1}, {1, 0}}, {16, 1.0f, {1200, -1}, {2, 0}}, {-1, 0, {0}, {0}}},
};
static struct {
    int active, health, route, step, dying;
    float frame, rate, dead_time;
    float history[GIGA_HISTORY][3];
    int history_head, history_count;
    float pos[GIGA_PARTS][3], dir[GIGA_PARTS][3];
    unsigned char fired[2];
} giga;
static unsigned short *giga_head_idx, *giga_body_idx;
static Vertex *giga_head_v, *giga_body_v;
static void *giga_tex[GIGA_TEXTURE_COUNT];

static int path_enter(int p) { return p == 1 ? 200 : (p == 3 || p == 15 || p == 16 || p == 17) ? 300 : 100; }
static int path_margin(int p) { return p == 14 ? 0 : p == 15 ? 250 : p == 17 ? 300 : 100; }
static int path_frames(int p) { return (giga_path_start[p + 1] - giga_path_start[p] - 1) * GIGA_PATH_STEP; }
static void path_at(int p, float frame, float out[3]) {
    int n = giga_path_start[p + 1] - giga_path_start[p], i;
    float u = frame / (float)GIGA_PATH_STEP, k;
    const float (*pt)[3] = &giga_path_points[giga_path_start[p]];
    if (n <= 0) { out[0] = out[1] = out[2] = 0.0f; return; }
    if (u < 0.0f) u = 0.0f;
    i = (int)u;
    if (i >= n - 1) { memcpy(out, pt[n - 1], sizeof(float) * 3); return; }
    k = u - (float)i;
    out[0] = pt[i][0] + (pt[i + 1][0] - pt[i][0]) * k;
    out[1] = pt[i][1] + (pt[i + 1][1] - pt[i][1]) * k;
    out[2] = pt[i][2] + (pt[i + 1][2] - pt[i][2]) * k;
}

static void giga_init_mesh(Vertex **v, unsigned short **idx, const Dmc3Vec3 *p, const Dmc3Vec3 *n, const Dmc3Uv *uv, int vc,
                           const unsigned short *src, int ic) {
    int i;
    *v = memalign(16, sizeof(Vertex) * vc);
    *idx = memalign(16, sizeof(unsigned short) * ic);
    if (!*v || !*idx) return;
    for (i = 0; i < vc; ++i) {
        Vertex *o = &(*v)[i];
        o->u = uv[i].u; o->v = uv[i].v; o->nx = n[i].x; o->ny = n[i].y; o->nz = n[i].z; o->x = p[i].x; o->y = p[i].y; o->z = p[i].z;
    }
    memcpy(*idx, src, sizeof(unsigned short) * ic);
}

static int giga_init(void) {
    int i;
    giga_init_mesh(&giga_head_v, &giga_head_idx, giga_head_positions, giga_head_normals, giga_head_uvs, GIGA_HEAD_VERTEX_COUNT, giga_head_indices, GIGA_HEAD_INDEX_COUNT);
    giga_init_mesh(&giga_body_v, &giga_body_idx, giga_body_positions, giga_body_normals, giga_body_uvs, GIGA_BODY_VERTEX_COUNT, giga_body_indices, GIGA_BODY_INDEX_COUNT);
    if (!giga_head_v || !giga_body_v || !giga_head_idx || !giga_body_idx) return 0;
    for (i = 0; i < GIGA_TEXTURE_COUNT; ++i) {
        size_t bytes = (size_t)giga_textures[i].width * giga_textures[i].height;
        giga_tex[i] = memalign(16, bytes);
        if (!giga_tex[i]) return 0;
        memcpy(giga_tex[i], giga_textures[i].rgba, bytes);
        if (!swizzle_texture8(giga_tex[i], giga_textures[i].width, giga_textures[i].height)) return 0;
    }
    return 1;
}

static void giga_begin_step(void) {
    const GigaStep *s = &giga_route[giga.route][giga.step];
    giga.frame = (float)path_enter(s->path);
    giga.fired[0] = giga.fired[1] = 0;
}

/* A claimed slot per part: the head is the slot that was spawned. */
static void giga_spawn(void) {
    Enemy *head = e;
    int k, found = 1, i;
    float p[3];
    memset(&giga, 0, sizeof(giga));
    giga.active = 1;
    giga.health = GIGA_HEALTH;
    giga.route = 0;
    giga.step = 0;
    giga.rate = 1.4f;
    giga_begin_step();
    path_at(giga_route[0][0].path, giga.frame, p);
    for (i = 0; i < GIGA_HISTORY; ++i) memcpy(giga.history[i], p, sizeof(p));
    giga.history_count = GIGA_HISTORY;
    head->kind = ENEMY_GIGAPEDE; head->step = 0; head->active = 1; head->plan = P_WATCH; head->hidden = 1;
    for (k = 0; k < ENEMY_MAX && found < GIGA_PARTS; ++k) {
        Enemy *o = &enemies[k];
        if (o->active || o == head) continue;
        o->kind = ENEMY_GIGAPEDE; o->step = (unsigned char)found++; o->active = 1; o->plan = P_WATCH; o->hidden = 1;
        o->health = 1; o->overlay_motion = -1;
        memcpy(o->pos, p, sizeof(p));
    }
    memcpy(head->pos, p, sizeof(p));
}

static void giga_record(const float p[3]) {
    const float *last = giga.history[giga.history_head];
    float dx = p[0] - last[0], dy = p[1] - last[1], dz = p[2] - last[2];
    if (dx * dx + dy * dy + dz * dz < 25.0f * 25.0f) return;
    giga.history_head = (giga.history_head + 1) % GIGA_HISTORY;
    memcpy(giga.history[giga.history_head], p, sizeof(float) * 3);
}

/* The point this far behind the head along where it's been. */
static void giga_behind(float distance, float out[3]) {
    int i, at = giga.history_head;
    float left = distance;
    for (i = 0; i < GIGA_HISTORY - 1; ++i) {
        int prev = (at + GIGA_HISTORY - 1) % GIGA_HISTORY;
        const float *a = giga.history[at], *b = giga.history[prev];
        float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
        if (l >= left && l > 0.0f) {
            float k = left / l;
            out[0] = a[0] + dx * k; out[1] = a[1] + dy * k; out[2] = a[2] + dz * k;
            return;
        }
        left -= l;
        at = prev;
    }
    memcpy(out, giga.history[at], sizeof(float) * 3);
}

static void giga_orb(const float from[3], int kind, float px, float py, float pz);

static void giga_event(int ev, float px, float py, float pz) {
    int k;
    if (ev == 1 || ev == 2) for (k = 1; k < GIGA_PARTS; ++k) giga_orb(giga.pos[k], 0, px, py, pz);
    if (ev == 3 || ev == 4) for (k = 2; k < GIGA_PARTS; k += 2) giga_orb(giga.pos[k], 1, px, py, pz);
}

static void giga_update(float f, float px, float py, float pz) {
    const GigaStep *s;
    float target, p[3];
    int k, near = 0, i;
    if (!giga.active) return;
    if (giga.dying) {
        giga.dead_time += f;
        if (giga.dead_time > 90.0f) {
            for (i = 0; i < ENEMY_MAX; ++i) if (enemies[i].kind == ENEMY_GIGAPEDE) enemies[i].active = 0;
            giga.active = 0;
        }
        return;
    }
    s = &giga_route[giga.route][giga.step];
    /* Rate: the step's speed x 1.4 (x 1.8 in its last phase), doubled when no part is near Dante. */
    for (k = 0; k < GIGA_PARTS; ++k) {
        float dx = giga.pos[k][0] - px, dz = giga.pos[k][2] - pz;
        if (dx * dx + dz * dz < 1000.0f * 1000.0f) near = 1;
    }
    target = s->speed * (giga.health < GIGA_HEALTH * 500 / 2800 ? 1.8f : 1.4f) * (near ? 1.0f : 2.0f);
    if (s->path == 14) target = s->speed * 2.8f;
    giga.rate += (target - giga.rate) * 0.01f * f;
    giga.frame += giga.rate * f;
    for (k = 0; k < 2; ++k)
        if (!giga.fired[k] && s->at[k] >= 0 && giga.frame >= (float)s->at[k]) { giga.fired[k] = 1; if (s->event[k]) giga_event(s->event[k], px, py, pz); }
    if (giga.frame >= (float)(path_frames(s->path) - path_margin(s->path))) {
        ++giga.step;
        if (giga_route[giga.route][giga.step].path < 0) {
            /* The next route, by phase: under 2000 of 2800 the second, under 500 the third. */
            giga.route = giga.health < GIGA_HEALTH * 500 / 2800 ? 3 : giga.health < GIGA_HEALTH * 2000 / 2800 ? 2 : 1;
            giga.step = 0;
        }
        giga_begin_step();
        s = &giga_route[giga.route][giga.step];
    }
    path_at(s->path, giga.frame, p);
    giga_record(p);
    memcpy(giga.pos[0], p, sizeof(p));
    for (k = 1; k < GIGA_PARTS; ++k) giga_behind(GIGA_SPACING * (float)k, giga.pos[k]);
    for (k = 0; k < GIGA_PARTS; ++k) {
        float ahead[3], dx, dy, dz, l;
        if (k == 0) giga_behind(-0.0f, ahead), giga_behind(60.0f, ahead), dx = p[0] - ahead[0], dy = p[1] - ahead[1], dz = p[2] - ahead[2];
        else { dx = giga.pos[k - 1][0] - giga.pos[k][0]; dy = giga.pos[k - 1][1] - giga.pos[k][1]; dz = giga.pos[k - 1][2] - giga.pos[k][2]; }
        l = sqrtf(dx * dx + dy * dy + dz * dz);
        if (l > 1.0f) { giga.dir[k][0] = dx / l; giga.dir[k][1] = dy / l; giga.dir[k][2] = dz / l; }
    }
}

static void giga_part_update(float f, float px, float py, float pz) {
    if (e->step == 0) giga_update(f, px, py, pz);
    memcpy(e->pos, giga.pos[e->step < GIGA_PARTS ? e->step : 0], sizeof(e->pos));
    e->floor_y = e->pos[1];
    e->plan = giga.dying ? P_DEATH : P_WATCH;
}

static void giga_hit(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    ++hits;
    fx_hit_spark(at);
    if (!mortal || giga.dying) return;
    giga.health -= (kind == HIT_LIGHT || kind == HIT_AIR) ? 1 : 2;
    if (giga.health <= 0) { giga.dying = 1; giga.dead_time = 0.0f; }
}

static void giga_draw_part(const float pos[3], const float dir[3], int head) {
    ScePspFMatrix4 m;
    float fx = dir[0], fy = dir[1], fz = dir[2], rx, ry, rz, ux, uy, uz, l;
    int i, bound = -1;
    const Dmc3ModelObject *objects = head ? giga_head_objects : giga_body_objects;
    int count = head ? GIGA_HEAD_OBJECT_COUNT : GIGA_BODY_OBJECT_COUNT;
    /* Right = up x forward, then up = forward x right. */
    rx = fz; ry = 0.0f; rz = -fx; l = sqrtf(rx * rx + rz * rz);
    if (l < 0.01f) { rx = 1.0f; rz = 0.0f; l = 1.0f; }
    rx /= l; rz /= l;
    ux = fy * rz - fz * ry; uy = fz * rx - fx * rz; uz = fx * ry - fy * rx;
    m.x.x = rx; m.x.y = ry; m.x.z = rz; m.x.w = 0.0f;
    m.y.x = ux; m.y.y = uy; m.y.z = uz; m.y.w = 0.0f;
    m.z.x = fx; m.z.y = fy; m.z.z = fz; m.z.w = 0.0f;
    m.w.x = pos[0]; m.w.y = pos[1]; m.w.z = pos[2]; m.w.w = 1.0f;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
    for (i = 0; i < count; ++i) {
        const Dmc3ModelObject *o = &objects[i];
        if ((int)o->texture != bound) {
            bound = (int)o->texture;
            sceGuClutLoad(256 / 8, giga_palettes[bound]);
            sceGuTexImage(0, giga_textures[bound].width, giga_textures[bound].height, giga_textures[bound].width, giga_tex[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, (head ? giga_head_idx : giga_body_idx) + o->index_start, head ? giga_head_v : giga_body_v);
    }
}

static void giga_draw(void) {
    int k;
    if (!giga.active) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    for (k = GIGA_PARTS - 1; k >= 0; --k) {
        if (giga.dying && ((int)giga.dead_time / 4) % 2) continue;   /* flickers out */
        giga_draw_part(giga.pos[k], giga.dir[k], k == 0);
    }
}

static void giga_orb(const float from[3], int kind, float px, float py, float pz) {
    int i;
    (void)py;
    for (i = 0; i < ARROW_MAX && arrows[i].active; ++i) {}
    if (i >= ARROW_MAX) return;
    memset(&arrows[i], 0, sizeof(arrows[i]));
    arrows[i].active = 1;
    arrows[i].type = kind ? 2 : 1;
    arrows[i].life = 300.0f;
    memcpy(arrows[i].pos, from, sizeof(arrows[i].pos));
    arrows[i].lift = 1.0f;
    arrows[i].goal[0] = px;
    arrows[i].goal[1] = pz;
}
