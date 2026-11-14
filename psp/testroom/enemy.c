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
#include "stage.h"
#include "vanguard.h"
#include "cerberus_boss.h"
#include "vergil_boss.h"
#include "leviathan_boss.h"

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
    /* The Blood-goyle's stone gauge and shots toward the next flinch, split cooldown and splits left, the
     * height it hovers over Dante and when that's re-rolled, and its dive's aim. */
    float goyle_stone, goyle_shots, goyle_split, goyle_lift, goyle_lift_wait, goyle_aim;
    signed char goyle_splits;
} Enemy;

static int hl_motion[HL_MOTIONS];
static Vertex *weapon_vertices[ENEMY_KIND_COUNT];
static unsigned short *indices[ENEMY_KIND_COUNT], *weapon_indices[ENEMY_KIND_COUNT];
static void *textures[ENEMY_TEXTURE_SET_COUNT][8];
/* Which bone holds the weapon, by kind. */
static const signed char kind_hand[ENEMY_KIND_COUNT] = {HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, ENIGMA_HAND_BONE, 0, 5, 7, 11, HAND_BONE};
static Enemy enemies[ENEMY_MAX];
static Enemy *e = &enemies[0];
static int hits, attacks_on;
static int landed;
static float landed_x, landed_z;
static int landed_heavy;
static float landed_damage = -1.0f;
static float attack_token;
static int mortal;
static int dummy_kind;
static int hit_weapon;   /* the melee weapon of the hits being dealt */
void enemy_set_hit_weapon(int melee) { hit_weapon = melee; }
void (*enemy_died_fn)(int kind, float x, float y, float z);
static void died(void) { if (enemy_died_fn) enemy_died_fn(e->kind, e->pos[0], e->pos[1], e->pos[2]); }
static float dante_y;   /* this frame's, for attacks that test his height in their action */
/* Each kind's decision table: the Seven Hells share the rest of their logic. */
static const EnemyAiClass *ai_class(void) {
    switch (e->kind) {
    case ENEMY_GLUTTONY: return &enemy_ai_classes[AI_CLASS_GLUTTONY];
    case ENEMY_SLOTH: return &enemy_ai_classes[AI_CLASS_SLOTH];
    case ENEMY_ENVY: return &enemy_ai_classes[AI_CLASS_ENVY];
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
static int goyle_init(void);
static void goyle_spawn(void);
static void goyle_update(float f, float px, float py, float pz);
static void goyle_hit(HitKind kind, float from_x, float from_z);
static void giga_note(const char *what, int a, float b);
static void goyle_bullet(const float from[3], const float to[3]);
static int enemy_giga_part(int k, float out[3]);
static void enemy_giga_struck(const float at[3], float damage);
#define GIGA_PARTS_PUBLIC 8
static int jester_init(void);
static void jester_spawn(void);
static void jester_update(float f, float px, float py, float pz);
static void jester_hit(HitKind kind);
static int pawn_init(void);
static void pawn_spawn(void);
static void pawn_update(float f, float px, float py, float pz);
static void pawn_hit(HitKind kind);
static int ar_init(void);
static void ar_rudra_spawn(void);
static void ar_spawn(void);
static void ar_update(float f, float px, float py, float pz);
static void ar_hit(HitKind kind);
static float ar_health_total(float *maximum);
static void goyle_turn_to(float px, float pz, float rate, float f);
static void giga_orb(const float from[3], int kind, float px, float py, float pz);
static void spawn_one(int kind, float x, float z);
static float ai_unit(void);
static float wrap_angle(float a);
static void land_attack(int heavy);
static void evaluate_pose(void);
static void trace_row(int did_land);
static void arrows_update(float f, float px, float py, float pz);
static void arrows_draw(void);
static void arrows_clear(int i);
static int giga_init(void);
static unsigned int ai_random(void);
static void giga_spawn(void);
static void giga_part_update(float f, float px, float py, float pz);
static void giga_hit(HitKind kind);
static void giga_draw(void);
float enemy_giga_health(void);
static int giga_part_out(int k);
static void giga_kill(void);
float enemy_giga_max_health(void);
#define ARROW_MAX_PUBLIC 24

int enemy_init(void) {
    int i, k;
    for (i = 0; i < HL_MOTIONS; ++i) {
        char name[6] = {'h', 'l', '-', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        hl_motion[i] = motion_find(name);
        if (hl_motion[i] < 0) return 0;
    }
    if (!enigma_init()) return 0;
    if (!goyle_init()) return 0;
    if (!jester_init()) return 0;
    if (!pawn_init()) return 0;
    if (!ar_init()) return 0;
    if (!giga_init()) return 0;
    if (!vanguard_init()) return 0;
    if (!cerberus_boss_init()) return 0;
    if (!vergil_boss_init()) return 0;
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
static int vergil_floor(float x, float y, float z, float *out) { return floor_under(x, y - 200.0f, z, out); }
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
    case ENEMY_ENVY: return ENEMY_HEALTH * 400 / 250;
    case ENEMY_ENIGMA: return ENEMY_HEALTH * 600 / 250;
    case ENEMY_GOYLE: return ENEMY_HEALTH * 300 / 250;
    case ENEMY_JESTER: return ENEMY_HEALTH * 1500 / 250;
    case ENEMY_PAWN: return ENEMY_HEALTH * 300 / 250;   /* pawn.inc's PAWN_HEALTH */
    case ENEMY_AGNI: case ENEMY_RUDRA: return ENEMY_HEALTH * 1200 / 250;
    default: return ENEMY_HEALTH;
    }
}


static void spawn_one(int kind, float x, float z) {
    const EnemyModel *m;
    int i;
    if (kind == ENEMY_GIGAPEDE) { giga_spawn(); return; }
    if (kind == ENEMY_LEVIATHAN) {
        if (!leviathan_boss_init()) return;
        e->kind=kind; e->active=1; e->plan=P_WATCH; e->hidden=0;
        leviathan_boss_spawn();
        leviathan_boss_target_position(e->pos);
        e->floor_y=0;
        return;
    }
    if (kind == ENEMY_CERBERUS) {
        e->kind = kind; e->active = 1; e->plan = P_WATCH; e->hidden = 0;
        e->pos[0] = 2500.0f; e->pos[1] = e->floor_y = 0.0f; e->pos[2] = 3250.0f;
        e->yaw = PI;
        cerberus_boss_spawn();
        return;
    }
    if (kind == ENEMY_VERGIL) {
        float y = 0.0f;
        if (!floor_under(x, spawn_search_y, z, &y)) y = 0.0f;
        e->kind = kind; e->active = 1; e->plan = P_WATCH; e->hidden = 0;
        e->pos[0] = x; e->pos[1] = e->floor_y = y; e->pos[2] = z;
        vergil_boss_set_floor(vergil_floor);
        vergil_boss_spawn(x, y, z);
        return;
    }
    if (kind == ENEMY_VANGUARD) {
        float y=0;
        floor_under(x,spawn_search_y,z,&y);
        e->kind=kind;e->pos[0]=x;e->pos[1]=y;e->pos[2]=z;e->floor_y=y;
        e->active=1;e->plan=P_WATCH;e->air=e->hidden=0;
        vanguard_spawn(x,y,z);return;
    }
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
    if (kind == ENEMY_GOYLE) { goyle_spawn(); return; }
    if (kind == ENEMY_JESTER) { jester_spawn(); return; }
    if (kind == ENEMY_PAWN) { pawn_spawn(); return; }
    if (kind == ENEMY_AGNI) { ar_spawn(); return; }
    if (kind == ENEMY_RUDRA) { ar_rudra_spawn(); return; }
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

void enemy_drop_below(float y) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind < ENEMY_KIND_COUNT && enemies[i].pos[1] < y) { enemies[i].active = 0; enemies[i].plan = P_DEATH; }
}

int enemy_alive_count(void) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) n += enemies[i].active && enemies[i].plan != P_DEATH;
    return n;
}

/* For tests: only the first `keep` living ones stay; the rest are removed outright. */
void enemy_trim(int keep) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].plan != P_DEATH && keep-- <= 0) enemies[i].active = 0;
}
void enemy_kill_all(void) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].active && enemies[i].plan != P_DEATH) {
            if (enemies[i].kind == ENEMY_VANGUARD) vanguard_kill();
            if (enemies[i].kind == ENEMY_CERBERUS) cerberus_boss_kill();
            if (enemies[i].kind == ENEMY_VERGIL) vergil_boss_kill();
            if (enemies[i].kind == ENEMY_LEVIATHAN) leviathan_boss_kill();
            Enemy *was = e;
            e = &enemies[i];
            if (e->kind != ENEMY_GIGAPEDE) died();   /* their orbs, as if Dante had done it */
            else giga_kill();                        /* its death run, and its orbs once */
            e = was;
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
void enemy_remove_defeated(void) {
    int i;
    for(i=0;i<ENEMY_MAX;++i)
        if(enemies[i].active && enemies[i].plan==P_DEATH)enemies[i].active=0;
    if(!enemy_alive_count())for(i=0;i<ARROW_MAX_PUBLIC;++i)arrows_clear(i);
}
void enemy_clear_minions(void) {
    Enemy *selected=e;int i;
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind<ENEMY_KIND_COUNT && enemies[i].plan!=P_DEATH) {
        e=&enemies[i];e->plan=P_DEATH;e->plan_step=0;e->air=0;died();
    }
    e=selected;
}

void enemy_set_count(int count, float x, float z) {
    int i;
    if (count == 0) for (i = 0; i < ARROW_MAX_PUBLIC; ++i) arrows_clear(i);
    for (i = 0; i < ENEMY_MAX; ++i) {
        e = &enemies[i];
        if (i >= count) {
            if(e->active && e->kind==ENEMY_LEVIATHAN)leviathan_boss_kill();
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
        if (o->active && o->plan != P_DEATH && !(o->kind == ENEMY_VANGUARD && vanguard_hidden()) &&
            !(o->kind == ENEMY_GIGAPEDE && giga_part_out(o->step)) &&
            !(o->kind == ENEMY_CERBERUS && cerberus_boss_dying()) &&
            !(o->kind == ENEMY_VERGIL && (vergil_boss_dying() || vergil_boss_hidden())) && d < best) { best = d; e = &enemies[i]; }
    }
    if (e->kind == ENEMY_CERBERUS) cerberus_boss_select_target(x, z);
    if (e->kind == ENEMY_LEVIATHAN) {
        leviathan_boss_select_target(x,z);
        leviathan_boss_target_position(e->pos);
    }
}

void enemy_set_attacks(int attacks) { attacks_on = attacks; }
void enemy_set_dummy_kind(int kind) { dummy_kind = kind == ENEMY_VANGUARD || kind == ENEMY_VERGIL || kind == ENEMY_LEVIATHAN || (kind >= 0 && kind < ENEMY_KIND_COUNT) ? kind : ENEMY_PRIDE; }
int enemy_kind(void) { return e->kind; }

int enemy_kind_for_class(int spawn_class) {
    /* EST spawn ids: a Seven Hell is 4 x its class number (0 Pride, 4 Gluttony, 8 Lust, 12 Sloth,
     * 16 Wrath, 20 Greed), and 31 is the Enigma. Anything not ported yet stands in as a Pride. */
    if (spawn_class >= 0 && spawn_class < 24) return spawn_class / 4;
    if (spawn_class == 31) return ENEMY_ENIGMA;
    if (spawn_class == 32) return ENEMY_GOYLE;
    if (spawn_class == 62) return ENEMY_JESTER;
    if (spawn_class == 35) return ENEMY_PAWN;   /* the Damned Pawn; 36-38 (the other chessmen) aren't in */
    if (spawn_class == 45) return ENEMY_AGNI;
    if (spawn_class == 41) return ENEMY_GIGAPEDE;
    if (spawn_class == 43) return ENEMY_CERBERUS;
    if (spawn_class == 58) return ENEMY_VERGIL;
    if (spawn_class == 26) return ENEMY_VANGUARD;
    if (spawn_class == 25) return ENEMY_ENVY;
    return ENEMY_PRIDE;
}
void enemy_set_anim_auto(int on) { anim_auto = on; }
void enemy_set_test(int no_texture, int no_lighting, float ring) {
    vanguard_render_test(no_texture,no_lighting);
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
float enemy_attack_damage(void) { return landed_damage; }
void enemy_clamp_player(float *z) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) if (enemies[i].active && enemies[i].kind == ENEMY_CERBERUS) {
        cerberus_boss_clamp_player(z);
        return;
    }
}

int enemy_active(void) { return e->active && e->plan != P_DEATH && !(e->kind == ENEMY_GIGAPEDE && giga_part_out(e->step)) && !(e->kind == ENEMY_VANGUARD && vanguard_hidden()) && !(e->kind == ENEMY_CERBERUS && cerberus_boss_dying()) && !(e->kind == ENEMY_VERGIL && (vergil_boss_dying() || vergil_boss_hidden())); }
void enemy_position(float out[3]) {
    if (e->kind == ENEMY_LEVIATHAN) { leviathan_boss_target_position(out); return; }
    if (e->kind == ENEMY_CERBERUS) cerberus_boss_target_position(out);
    else if (e->kind == ENEMY_VERGIL) vergil_boss_position(out);
    else memcpy(out, e->pos, sizeof(e->pos));
}
float enemy_height(void) { return e->pos[1] - e->floor_y; }
int enemy_hits_taken(void) { return hits; }
const char *enemy_boss_name(void) {
    int i;
    for (i=0;i<ENEMY_MAX;++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_LEVIATHAN) return "HEART OF LEVIATHAN";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_CERBERUS) return "CERBERUS";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_VERGIL) return "VERGIL";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_GIGAPEDE) return "GIGAPEDE";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_JESTER) return "JESTER";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && (enemies[i].kind == ENEMY_AGNI || enemies[i].kind == ENEMY_RUDRA)) return "AGNI & RUDRA";
    return "HELL VANGUARD";
}
int enemy_boss_health(float *health, float *maximum) {
    int lv;
    for(lv=0;lv<ENEMY_MAX;++lv) if(enemies[lv].active && enemies[lv].kind==ENEMY_LEVIATHAN) {
        *health=leviathan_boss_health();*maximum=leviathan_boss_max_health();return 1;
    }
    int i;
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind==ENEMY_VANGUARD) {
        *health=vanguard_health();*maximum=vanguard_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind==ENEMY_CERBERUS) {
        *health=cerberus_boss_health();*maximum=cerberus_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind==ENEMY_VERGIL) {
        *health=vergil_boss_health();*maximum=vergil_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && (enemies[i].kind==ENEMY_AGNI || enemies[i].kind==ENEMY_RUDRA) && enemies[i].plan!=P_DEATH) {
        *health=ar_health_total(maximum);return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind==ENEMY_JESTER && enemies[i].plan!=P_DEATH) {
        *health=enemies[i].health;*maximum=ENEMY_HEALTH * 1500 / 250;return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].kind==ENEMY_GIGAPEDE) {
        *health=enemy_giga_health();*maximum=enemy_giga_max_health();return 1;
    }
    return 0;
}
unsigned int enemy_ai_random(void) {return ai_random();}

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
        const EnemyAiOp *op = &(e->kind == ENEMY_ENVY ? envy_ai_ops : enemy_ai_ops)[e->script_at++];
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
    const short *scripts = e->kind == ENEMY_ENVY ? envy_ai_scripts : enemy_ai_scripts;
    if (scripts[n] < 0) { e->done = 1; return; }
    e->script_at = scripts[n];
    script_run();
}

/* The second list a state can name motions through; the game's PTR_DAT_00618c38. */
static void script_play_hit(int n) { script_play((e->kind==ENEMY_ENVY?envy_ai_hit_scripts:enemy_ai_hit_scripts)[n]); }

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
    if (e->kind == ENEMY_LEVIATHAN) {
        float damage=kind==HIT_PISTOL ? 8.0f : (kind==HIT_LIGHT || kind==HIT_AIR ? 42.0f : 84.0f);
        if (leviathan_boss_dying()) return;
        ++hits;
        if(leviathan_boss_hit(leviathan_boss_target(),damage,mortal)) {e->plan=P_DEATH;died();}
        return;
    }
    if (e->kind == ENEMY_CERBERUS) {
        ++hits;
        if (cerberus_boss_hit(kind, from_x, from_z, mortal)) { e->plan = P_DEATH; died(); }
        return;
    }
    if (e->kind == ENEMY_VERGIL) {
        ++hits;
        if (vergil_boss_hit(kind, from_x, from_z, mortal)) { e->plan = P_DEATH; died(); }
        return;
    }
    if (e->kind == ENEMY_VANGUARD && (vanguard_hidden() || vanguard_dying())) return;
    if (e->kind == ENEMY_GIGAPEDE) { giga_hit(kind); return; }
    if (e->kind == ENEMY_JESTER) { jester_hit(kind); return; }
    if (e->kind == ENEMY_PAWN) { pawn_hit(kind); return; }
    if (e->kind == ENEMY_AGNI || e->kind == ENEMY_RUDRA) { ar_hit(kind); return; }
    if (e->kind == ENEMY_GOYLE) { ++hits; { float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]}; fx_hit_spark(at); } goyle_hit(kind, from_x, from_z); return; }
    if(kind==HIT_PISTOL && e->kind!=ENEMY_VANGUARD)kind=HIT_LIGHT;
    ++hits;  /* infinite health: hits only cause reactions */
    {
        /* On the side facing the attacker, at chest height. */
        float d[2] = {from_x - e->pos[0], from_z - e->pos[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1]);
        float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
        if (len > 1.0f) { at[0] += d[0] / len * 40.0f; at[2] += d[1] / len * 40.0f; }
        fx_hit_spark(at);
    }
    if (e->kind == ENEMY_ENIGMA) { enigma_hit(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_GOYLE) { goyle_hit(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_VANGUARD) { if(vanguard_hit(kind,mortal)) {e->plan=P_DEATH;died();} return; }
    if (e->plan == P_DEATH) return;
    if (mortal) {
        e->health = (signed char)(e->health - (kind == HIT_LIGHT ? 1 : 2));
        if (e->health <= 0) {
            e->air = 0;
            plan_set(P_DEATH);
            died();
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
    case HIT_PISTOL: /* Normalized to HIT_LIGHT above for ordinary enemies. */
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

void enemy_push_out(float *x, float *z, float y, float r) {
    Enemy *selected = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].kind == ENEMY_GIGAPEDE) continue;
        if (enemies[i].kind == ENEMY_LEVIATHAN) continue;
        if (enemies[i].kind == ENEMY_VANGUARD && (vanguard_hidden() || vanguard_dying())) continue;
        if (enemies[i].kind == ENEMY_CERBERUS) {
            /* Its body is five posed push capsules (pack chunks 31/32), not one circle at its hips. */
            if (enemies[i].active) cerberus_boss_push_out(x, z, y, r);
            continue;
        }
        if (enemies[i].kind == ENEMY_VERGIL) {
            if (enemies[i].active) vergil_boss_push_out(x, z, y, r);
            continue;
        }
        const Enemy *o = &enemies[i];
        float dx, dz, d, min;
        e = &enemies[i];
        if (o->active && is_lying()) continue;
        if (!o->active) continue;
        dx = *x - o->pos[0]; dz = *z - o->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        min = r + ENEMY_RADIUS;
        /* Only bodies at his height: Dante over a Pride's head or the Vanguard mid-uppercut isn't shoved. */
        if (d < min && d > 1e-3f && fabsf(o->pos[1] - o->floor_y) < 150.0f && fabsf(o->pos[1] - y) < 170.0f) {
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
    landed_damage = -1.0f;
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
        cap = &(e->kind == ENEMY_ENVY ? envy_ai_caps : enemy_ai_caps)[e->hit_cap[k]];
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
    if (e->kind == ENEMY_LEVIATHAN) {
        float x,z,damage;
        leviathan_boss_update(f,px,py,pz,attacks_on);
        if(leviathan_boss_attack_landed(&x,&z,&damage)) {
            land_attack(1);landed_x=x;landed_z=z;landed_damage=damage;
        }
        leviathan_boss_target_position(e->pos);
        e->plan=leviathan_boss_dying()?P_DEATH:P_WATCH;
        return;
    }
    if (e->kind == ENEMY_CERBERUS) {
        float hit_x, hit_z, damage;
        int heavy;
        cerberus_boss_root_position(e->pos);
        e->floor_y = e->pos[1];
        e->plan = cerberus_boss_dying() ? P_DEATH : P_WATCH;
        cerberus_boss_update(f, px, py, pz, attacks_on);
        cerberus_boss_root_position(e->pos);
        if (cerberus_boss_attack_landed(&hit_x, &hit_z, &heavy, &damage)) {
            land_attack(heavy);
            landed_x = hit_x; landed_z = hit_z; landed_damage = damage;
        }
        return;
    }
    if (e->kind == ENEMY_VERGIL) {
        float hit_x, hit_z, damage;
        int heavy;
        e->plan = vergil_boss_dying() ? P_DEATH : P_WATCH;
        vergil_boss_update(f, px, py, pz, attacks_on);
        vergil_boss_position(e->pos);
        e->floor_y = e->pos[1];
        e->hidden = vergil_boss_hidden();
        if (vergil_boss_attack_landed(&hit_x, &hit_z, &heavy, &damage)) {
            land_attack(heavy);
            landed_x = hit_x; landed_z = hit_z; landed_damage = damage;
        }
        return;
    }
    if (e->kind == ENEMY_VANGUARD) {
        float ground;
        if(vanguard_update(f,px,py,pz,attacks_on,e->pos,&e->active))land_attack(1);
        if(floor_under(e->pos[0],e->pos[1],e->pos[2],&ground))e->floor_y=ground;
        else e->floor_y=e->pos[1];
        e->air=e->pos[1]>e->floor_y+5;e->hidden=vanguard_hidden();
        if(vanguard_dying())e->plan=P_DEATH;
        return;
    }
    if (e->kind == ENEMY_ENIGMA) { enigma_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_GOYLE) { goyle_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_JESTER) { jester_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_PAWN) { pawn_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_AGNI || e->kind == ENEMY_RUDRA) { ar_update(f, px, py, pz); return; }
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
        float was_x, was_z;
        e = &enemies[i];
        was_x = e->pos[0]; was_z = e->pos[2];
        update_one(dt, px, py, pz);
        /* The room's walls, as for Dante: swept along everything that moved it this frame (its walk, root
         * motion, knockback, being pushed by him or the others), in steps of at most 15 cm, so a launch or a
         * shove can't carry it through. A spawn or a jump past 6 m is a placement, not a move: checked where it
         * lands. The Gigapede flies its own paths. */
        if (e->active && e->kind < ENEMY_KIND_COUNT && stage_loaded()) {
            float mx = e->pos[0] - was_x, mz = e->pos[2] - was_z, cx = was_x, cz = was_z, l = sqrtf(mx * mx + mz * mz);
            int steps = l > 600.0f ? 1 : (int)(l / 15.0f) + 1, s;
            if (l > 600.0f) { cx = e->pos[0]; cz = e->pos[2]; mx = mz = 0.0f; }
            for (s = 1; s <= steps; ++s) {
                cx += mx / (float)steps; cz += mz / (float)steps;
                stage_collide(&cx, &cz, e->pos[1], ENEMY_RADIUS);
            }
            e->pos[0] = cx; e->pos[2] = cz;
        }
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
    if (e->kind == ENEMY_AGNI) sceGuColor(0xff8090ffu);        /* Agni: red */
    else if (e->kind == ENEMY_RUDRA) sceGuColor(0xffffa080u);  /* Rudra: blue */
    if (e->kind == ENEMY_GOYLE && (e->action == 15 || e->action == 16 || e->action == 17 || e->action == 19) && e->plan != P_DEATH) {
        /* Stone: grey. */
        sceGuDisable(GU_TEXTURE_2D);
        sceGuAmbientColor(0xff141618u);
        sceGuColor(0xff2a2d30u);
    }
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
    if (e->kind == ENEMY_GOYLE || e->kind == ENEMY_AGNI || e->kind == ENEMY_RUDRA) { sceGuAmbientColor(0xffffffffu); sceGuColor(0xffffffffu); sceGuEnable(GU_TEXTURE_2D); }
    if (e->kind != ENEMY_ENIGMA && e->kind != ENEMY_GOYLE) {
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
        if (e->kind == ENEMY_CERBERUS) { cerberus_boss_draw(); continue; }
        if (e->kind == ENEMY_VERGIL) { vergil_boss_draw(); continue; }
        if (e->kind == ENEMY_LEVIATHAN) { leviathan_boss_draw(); continue; }
        if (e->kind == ENEMY_VANGUARD) {vanguard_draw();continue;}
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
        } else if (a->type == 3) {
            /* Batted back: it homes on the nearest part of the Gigapede (accel 1.2, top 40) and hits it as five. */
            float best = 1.0e12f, t[3] = {0.0f, 0.0f, 0.0f}, tl, v;
            int k;
            for (k = 1; k < GIGA_PARTS_PUBLIC; ++k) {
                float q[3], d2;
                if (!enemy_giga_part(k, q)) continue;
                d2 = (q[0] - a->pos[0]) * (q[0] - a->pos[0]) + (q[1] - a->pos[1]) * (q[1] - a->pos[1]) + (q[2] - a->pos[2]) * (q[2] - a->pos[2]);
                if (d2 < best) { best = d2; t[0] = q[0]; t[1] = q[1]; t[2] = q[2]; }
            }
            if (best > 1.0e11f) { a->active = 0; continue; }
            if (best < 180.0f * 180.0f) { enemy_giga_struck(a->pos, 5.0f); a->active = 0; continue; }
            t[0] -= a->pos[0]; t[1] -= a->pos[1]; t[2] -= a->pos[2];
            tl = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) + 0.01f;
            a->vel[0] += t[0] / tl * 1.2f * f; a->vel[1] += t[1] / tl * 1.2f * f; a->vel[2] += t[2] / tl * 1.2f * f;
            v = sqrtf(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
            if (v > 40.0f) { float k2 = 40.0f / v; a->vel[0] *= k2; a->vel[1] *= k2; a->vel[2] *= k2; }
            a->pos[0] += a->vel[0] * f; a->pos[1] += a->vel[1] * f; a->pos[2] += a->vel[2] * f;
            continue;
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
        } else if (a->type == 4) {
            /* A Blood-goyle's bullet: straight. */
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
        if ((a->type == 0 || a->type == 4) && player_floor_fn && player_floor_fn(a->pos[0], a->pos[1] + 50.0f, a->pos[2], 0.0f, &g) && a->pos[1] < g) a->active = 0;
        if (a->type == 4 && stage_loaded()) {
            float cx = a->pos[0], cz = a->pos[2];
            if (stage_collide(&cx, &cz, a->pos[1] - 90.0f, 20.0f)) { fx_hit_spark(a->pos); a->active = 0; }   /* bursts on a wall */
        }
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
            v[n].color = a->type == 1 ? 0xffff60c0u : a->type == 3 ? 0xffffffffu : a->type == 4 ? 0xff2020ffu : 0xffffd060u; v[n].x = a->pos[0]; v[n].y = a->pos[1] + 50.0f; v[n].z = a->pos[2]; ++n;
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
            died();
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

/* ---- The Blood-goyle (CEm014, em014.pac), Mission 5 (docs/ENEMY_SPECS.md section D, rank 0). A flying
 * blood gargoyle. Its states are the game's (+5, dispatched by FUN_0039cb30) and motion N plays in state N.
 *   Flying: hovers 50-120 cm over Dante's feet (re-rolled every 200-400 frames), approaches at 12, 6 or 3 a
 *   frame by distance (over 2500, over 1000, nearer), backs off at 2, strafes. With the attack token it
 *   winds up (11), aims (12, a random +-50 degrees), and dives (14): 20 a frame building to 30, steering
 *   3 degrees a frame at him until it's past him.
 *   Sword: a flinch, or a split (8). It splits when its cooldown has run out (300 frames; it runs at one a
 *   frame with Dante within 1000 cm, slower beyond as 1e6 / d^2), fewer than 5 are out, splits are left (5,
 *   shared down the line) and a 75% roll passes. The new one (9) comes out of it with its health.
 *   Guns: each shot fills a stone gauge by twice its power; every 60 is a flinch and 120 more. Over 150
 *   (rank 0's 0x5bf244) it turns to stone (15): grey, it drops to the floor (16, 17) and stands as a statue
 *   while the gauge drains 1 a frame (0.2 as flesh). At 0 it comes back to life (18). Sword hits on the
 *   statue knock it up (19, vy 23, fall 0.7) and it shatters when its health is gone.
 * Simplified: the decision tables (0x5bf814) are a fixed choice; its blood shots (state 13) and the
 * dodge from aimed guns aren't in; damage to Dante is the default; three times the damage on stone is
 * inferred, not decoded. */
#define GOYLE_HEALTH (ENEMY_HEALTH * 300 / 250)
#define GOYLE_STONE_AT 150.0f
#define GOYLE_MAX_OUT 5
#define GOYLE_SPLITS 5
enum { G_HOVER = 0, G_STRAFE_L = 2, G_STRAFE_R = 3, G_APPROACH = 4, G_BACK = 5, G_FLINCH = 6, G_FLINCH_HEAVY = 7,
       G_SPLIT = 8, G_EMERGE = 9, G_WINDUP = 11, G_AIM = 12, G_SHOOT = 13, G_DIVE = 14, G_STONE = 15, G_STONE_FALL = 16,
       G_STATUE = 17, G_REVIVE = 18, G_STONE_HIT = 19, G_DEATH = 26, G_GATHER = 27 };
static int goyle_motion[28];

static int goyle_init(void) {
    int i;
    for (i = 0; i < 28; ++i) {
        char name[5] = {'g', 'b', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        goyle_motion[i] = motion_find(name);
        if (goyle_motion[i] < 0) return 0;
    }
    return 1;
}

static int goyle_count(void) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) n += enemies[i].active && enemies[i].kind == ENEMY_GOYLE && enemies[i].plan != P_DEATH;
    return n;
}

static void goyle_state(int s) {
    int m = goyle_motion[s < 28 ? s : 0];
    if (s == G_GATHER || s == G_SPLIT || s == G_EMERGE || s == G_STONE || s == G_STATUE || s == G_REVIVE || s == G_DEATH || s == G_DIVE)
        giga_note("goyle", s * 100 + (int)(e - enemies), e->goyle_stone);
    e->action = (unsigned char)s;
    e->step = 0;
    e->timer = 0.0f;
    if (m != e->motion) {
        e->prev_motion = e->motion;
        e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f;
        e->fade_rate = 1.0f / 8.0f;
        e->motion = m;
        e->frame = 0.0f;
    }
    e->done = 0;
}

static void goyle_spawn(void) {
    e->plan = P_WATCH;
    e->motion = -1;
    e->goyle_stone = e->goyle_shots = 0.0f;
    e->goyle_split = 300.0f;
    e->goyle_splits = GOYLE_SPLITS;
    e->vel[0] = e->vel[1] = e->vel[2] = 0.0f;
    e->goyle_lift = 50.0f + ai_unit() * 70.0f;
    e->goyle_lift_wait = 200.0f + ai_unit() * 200.0f;
    e->pos[1] += 150.0f;   /* they come in flying */
    goyle_state(G_HOVER);
    e->fade = 0.0f;
}

/* FUN_0039e660's tables (0x5bf814: per state, a list of {table, distance}; the first whose distance Dante's
 * within picks the table). Each row: a weight per distance band (under 350, under 700, beyond; 0x5bf240) and
 * the next state. A 0-100 roll walks the cumulative weights. 1000 stands for the data's 999.9 (the rest). */
typedef struct { unsigned char next; unsigned short w[3]; } GoyleRow;
static void goyle_split_off(void);
static const GoyleRow goyle_near[] = {{G_WINDUP,{10,10,15}},{G_STRAFE_L,{25,20,20}},{G_STRAFE_R,{25,20,20}},{G_BACK,{25,30,30}},{G_HOVER,{1000,1000,1000}}};
static const GoyleRow goyle_mid[] = {{G_WINDUP,{40,42,45}},{G_APPROACH,{10,30,30}},{G_STRAFE_L,{13,13,10}},{G_STRAFE_R,{13,13,10}},{G_BACK,{1000,1000,1000}}};
static const GoyleRow goyle_far[] = {{G_WINDUP,{10,15,20}},{G_APPROACH,{60,60,1000}},{G_HOVER,{1000,1000,1000}}};
/* After a flinch (states 6 and 7, 0x5bf7c0). */
static const GoyleRow goyle_hurt_near[] = {{G_WINDUP,{10,10,10}},{G_STRAFE_L,{30,30,40}},{G_STRAFE_R,{30,30,40}},{G_BACK,{1000,1000,1000}}};
static const GoyleRow goyle_hurt_mid[] = {{G_WINDUP,{10,15,20}},{G_STRAFE_L,{25,20,20}},{G_STRAFE_R,{25,20,20}},{G_APPROACH,{20,20,20}},{G_BACK,{5,10,10}},{G_HOVER,{1000,1000,1000}}};
static const GoyleRow goyle_hurt_far[] = {{G_WINDUP,{10,15,20}},{G_APPROACH,{1000,1000,1000}}};

static int goyle_in(int s0, int s1, int s2, int s3) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        if (o->active && o->kind == ENEMY_GOYLE && o->plan != P_DEATH && (o->action == s0 || o->action == s1 || o->action == s2 || o->action == s3)) ++n;
    }
    return n;
}

/* What to do next (FUN_0039e660). */
static void goyle_decide(float d) {
    const GoyleRow *t;
    int rows, i, band = d < 350.0f ? 0 : d < 700.0f ? 1 : 2, hurt = e->action == G_FLINCH || e->action == G_FLINCH_HEAVY;
    float roll, acc = 0.0f;
    /* Fewer out than its limit (+0x461) and splits left (+0x45c), none splitting now (flag 0x80000000): it
     * splits of its own accord (state 27, FUN_004324e0, then 8). */
    if (e->goyle_splits > 0 && goyle_count() < GOYLE_MAX_OUT && !goyle_in(G_GATHER, G_SPLIT, G_GATHER, G_GATHER)) {
        goyle_state(G_GATHER);
        return;
    }
    if (d < 400.0f) { t = hurt ? goyle_hurt_near : goyle_near; rows = hurt ? 4 : 5; }
    else if (d < 1000.0f) { t = hurt ? goyle_hurt_mid : goyle_mid; rows = hurt ? 6 : 5; }
    else { t = hurt ? goyle_hurt_far : goyle_far; rows = hurt ? 2 : 3; }
    roll = ai_unit() * 100.0f;
    for (i = 0; i < rows; ++i) {
        int next = t[i].next;
        acc += (float)t[i].w[band];
        if (acc <= roll) continue;
        if (next == e->action) { e->timer = 0.0f; return; }   /* the same state: carry on */
        if (next == G_WINDUP) {
            /* An attack needs fewer than 2 attacking (FUN_002fbdc0(mgr, 1)) and the attack token (3 s). Else the
             * next row. Under 350 the game also asks a check of its own (vfunc 0x1c8) that isn't decoded. */
            if (!attacks_on || attack_token > 0.0f || goyle_in(G_WINDUP, G_AIM, G_DIVE, G_SHOOT) >= 2) continue;
            attack_token = 3.0f;
            /* From further off it may shoot instead of diving [the port's choice; state 11's own pick isn't read]. */
            goyle_state(d > 500.0f && ai_unit() < 0.5f ? G_SHOOT : G_WINDUP);
            return;
        }
        goyle_state(next);
        return;
    }
    goyle_state(G_HOVER);
}

static void goyle_turn_to(float px, float pz, float rate, float f) {
    float want = atan2f(px - e->pos[0], pz - e->pos[2]), d = wrap_angle(want - e->yaw), t = rate * f;
    e->yaw = wrap_angle(e->yaw + (d > t ? t : d < -t ? -t : d));
}

static void goyle_update(float f, float px, float py, float pz) {
    const Motion *m;
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, s = sinf(e->yaw), c = cosf(e->yaw);
    int stone = e->action == G_STONE || e->action == G_STONE_FALL || e->action == G_STATUE || e->action == G_STONE_HIT;
    /* The stone gauge drains; so does the split cooldown, faster the nearer he is. */
    if (e->goyle_stone > 0.0f) { e->goyle_stone -= (stone ? 1.0f : 0.2f) * f; if (e->goyle_stone < 0.0f) e->goyle_stone = 0.0f; }
    if (e->goyle_shots > 0.0f) e->goyle_shots -= f;
    if (e->goyle_split > 0.0f) { float dd = d < 1000.0f ? 1000.0f : d; e->goyle_split -= 1.0e6f / (dd * dd) * f; }
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    m = motion_get(e->motion);
    e->frame += f;
    e->timer += f;
    if (e->frame >= m->end_frame) { e->done = 1; e->frame = (e->action == G_HOVER || e->action == G_APPROACH || e->action == G_BACK ||
                                                              e->action == G_STATUE || e->action == G_DIVE) ? motion_wrap(m, e->frame) : m->end_frame; }
    if (!floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) g = e->floor_y;
    e->floor_y = g;
    if (e->plan == P_DEATH) {
        /* A statue shatters at once; flesh bursts after its fall. */
        e->pos[1] -= 6.0f * f;
        if (e->pos[1] < g) e->pos[1] = g;
        if (e->timer > (e->goyle_stone > 0.0f ? 2.0f : 40.0f)) e->active = 0;
        return;
    }
    switch (e->action) {
    case G_HOVER: case G_APPROACH: case G_BACK: case G_STRAFE_L: case G_STRAFE_R: {
        float target = py + e->goyle_lift, step = (e->pos[1] - target) / 20.0f, speed = 0.0f, side = 0.0f;
        if (step > 3.0f) step = 3.0f;
        if (step < -1.0f) step = -1.0f;
        e->pos[1] -= step * f;
        if ((e->goyle_lift_wait -= f) < 0.0f) { e->goyle_lift = 50.0f + ai_unit() * 70.0f; e->goyle_lift_wait = 200.0f + ai_unit() * 200.0f; }
        goyle_turn_to(px, pz, 0.06f, f);
        if (e->action == G_APPROACH) speed = d > 2500.0f ? 12.0f : d > 1000.0f ? 6.0f : 3.0f;
        if (e->action == G_BACK) speed = -2.0f;
        if (e->action == G_STRAFE_L) side = 6.0f;
        if (e->action == G_STRAFE_R) side = -6.0f;
        if (e->action == G_APPROACH && d < 250.0f) speed = 0.0f;
        e->pos[0] += (s * speed + c * side) * f;
        e->pos[2] += (c * speed - s * side) * f;
        if (e->timer > (e->action == G_HOVER ? 70.0f : e->action == G_APPROACH ? 90.0f : 45.0f)) goyle_decide(d);
        break;
    }
    case G_FLINCH: case G_FLINCH_HEAVY:
        e->pos[0] -= s * e->vel[0] * f; e->pos[2] -= c * e->vel[0] * f;
        e->vel[0] *= 0.9f;
        if (e->done) goyle_decide(d);
        break;
    case G_GATHER:
        /* State 27 (FUN_004324e0): it gathers itself; at frame 149 the split effect (0x194), and at the motion's
         * end it splits (state 8). */
        goyle_turn_to(px, pz, 0.06f, f);
        if (!e->step && (e->frame >= 149.0f || e->done)) {
            float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
            e->step = 1;
            fx_explosion(at);
        }
        if (e->done) {
            --e->goyle_splits;
            e->goyle_split = 300.0f;
            e->vel[0] = 10.0f;
            goyle_state(G_SPLIT);
            goyle_split_off();
        }
        break;
    case G_SPLIT: case G_EMERGE:
        /* Thrown apart, frames 11-40 (FUN_00430330 / 00430480). */
        if (e->frame > 11.0f && e->frame < 40.0f) { e->pos[0] -= s * e->vel[0] * f; e->pos[2] -= c * e->vel[0] * f; }
        if (e->done) goyle_decide(d);
        break;
    case G_SHOOT:
        /* State 13: Shl00 (FUN_004404a0) emits, 27-32 frames into its life, 2 / 3 / 4 Shl01 blood bullets by
         * distance band (under 350, under 700, further; 0x5bf242) at Dante's place +-100 cm each way. They fly
         * straight and burst on a wall (FUN_00459eb0). Their speed and life aren't decoded: 25 a frame and 120. */
        goyle_turn_to(px, pz, 0.1f, f);
        if (!e->step && e->timer > 28.0f) {
            int k, n = d < 350.0f ? 2 : d < 700.0f ? 3 : 4;
            e->step = 1;
            for (k = 0; k < n; ++k) {
                float from[3] = {e->pos[0] + s * 60.0f, e->pos[1] + 150.0f, e->pos[2] + c * 60.0f};
                float to[3] = {px + ai_unit() * 200.0f - 100.0f, py + 100.0f + ai_unit() * 200.0f - 100.0f, pz + ai_unit() * 200.0f - 100.0f};
                goyle_bullet(from, to);
            }
        }
        if (e->done) goyle_state(G_HOVER);
        break;
    case G_WINDUP:
        goyle_turn_to(px, pz, 0.1f, f);
        if (e->done) {
            goyle_state(G_AIM);
            e->goyle_aim = (ai_unit() * 100.0f - 50.0f) / 360.0f * 6.283185f;
        }
        break;
    case G_AIM:
        goyle_turn_to(px, pz, 0.1f, f);
        e->pos[1] += 2.0f * f;   /* it rises into the dive */
        if (e->done) { e->yaw = wrap_angle(e->yaw + e->goyle_aim * 0.2f); e->vel[0] = 20.0f; goyle_state(G_DIVE); }
        break;
    case G_DIVE: {
        /* 20 a frame, building 0.5 a frame to 30; steering 3 degrees a frame while he's ahead; down onto him. */
        float want = atan2f(dx, dz), off = fabsf(wrap_angle(want - e->yaw)), ddy = py + 90.0f - e->pos[1];
        if (off < 1.4f) goyle_turn_to(px, pz, 0.052f, f);
        e->vel[0] += 0.5f * f; if (e->vel[0] > 30.0f) e->vel[0] = 30.0f;
        e->pos[0] += s * e->vel[0] * f; e->pos[2] += c * e->vel[0] * f;
        e->pos[1] += (ddy > 0.0f ? 1.0f : -1.0f) * fminf(fabsf(ddy), 6.0f * f);
        if (d < 110.0f && fabsf(py + 90.0f - e->pos[1]) < 150.0f && e->step == 0) {
            land_attack(0);
            e->step = 1;
        }
        if (e->timer > 70.0f || (e->step == 1 && e->timer > 20.0f) || e->pos[1] < g + 20.0f) {
            if (e->pos[1] < g + 20.0f) { float at[3] = {e->pos[0], g, e->pos[2]}; fx_dust(at); e->pos[1] = g + 20.0f; }
            goyle_state(G_HOVER);
        }
        break;
    }
    case G_STONE: case G_STONE_FALL: case G_STONE_HIT:
        /* Falls as stone (gravity 0.7 a frame); knocked up when struck. */
        e->vel[1] -= 0.7f * f;
        e->pos[1] += e->vel[1] * f;
        e->pos[0] -= s * e->vel[0] * f; e->pos[2] -= c * e->vel[0] * f;
        e->vel[0] *= 0.95f;
        if (e->pos[1] <= g) {
            float at[3] = {e->pos[0], g, e->pos[2]};
            e->pos[1] = g; e->vel[1] = 0.0f;
            fx_dust(at);
            goyle_state(G_STATUE);
        }
        break;
    case G_STATUE:
        e->pos[1] = g;
        if (e->goyle_stone <= 0.0f) { goyle_state(G_REVIVE); e->pos[1] = g; }
        break;
    case G_REVIVE:
        e->pos[1] += 3.0f * f;
        if (e->done) goyle_state(G_HOVER);
        break;
    default:
        goyle_state(G_HOVER);
        break;
    }
    if (e->pos[1] < g) e->pos[1] = g;
}

/* FUN_0042f090 / 0042fd40: with Dante aiming (his action 0x11) within 500 (300 while it approaches), a hovering
 * or approaching Blood-goyle dodges, left or right at 50/50. Taken here as his shots. */
void enemy_gun_aimed(float x, float z) {
    Enemy *was = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        Enemy *o = &enemies[i];
        float dx, dz, r;
        if (!o->active || o->kind != ENEMY_GOYLE || o->plan == P_DEATH) continue;
        if (o->action != 0 && o->action != 4) continue;   /* G_HOVER, G_APPROACH */
        r = o->action == 4 ? 300.0f : 500.0f;
        dx = o->pos[0] - x; dz = o->pos[2] - z;
        if (dx * dx + dz * dz > r * r || o->timer < 10.0f) continue;
        e = o;
        goyle_state(ai_unit() < 0.5f ? 2 : 3);
    }
    e = was;
}

static void goyle_bullet(const float from[3], const float to[3]) {
    int i;
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2], l = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f;
    for (i = 0; i < ARROW_MAX && arrows[i].active; ++i) {}
    if (i >= ARROW_MAX) return;
    memset(&arrows[i], 0, sizeof(arrows[i]));
    arrows[i].active = 1; arrows[i].type = 4; arrows[i].life = 120.0f;
    memcpy(arrows[i].pos, from, sizeof(arrows[i].pos));
    arrows[i].vel[0] = dx / l * 25.0f; arrows[i].vel[1] = dy / l * 25.0f; arrows[i].vel[2] = dz / l * 25.0f;
}

static void goyle_split_off(void) {
    Enemy *parent = e;
    int k;
    for (k = 0; k < ENEMY_MAX; ++k) {
        Enemy *o = &enemies[k];
        if (o->active) continue;
        e = o;
        spawn_one(ENEMY_GOYLE, parent->pos[0], parent->pos[2]);
        o->pos[1] = parent->pos[1];
        o->yaw = wrap_angle(parent->yaw + PI);
        o->health = parent->health;
        o->goyle_splits = parent->goyle_splits;
        o->goyle_split = 300.0f;
        o->vel[0] = 10.0f;
        goyle_state(G_EMERGE);
        break;
    }
    e = parent;
}

static void goyle_hit(HitKind kind, float from_x, float from_z) {
    int stone = e->action == G_STONE || e->action == G_STONE_FALL || e->action == G_STATUE || e->action == G_STONE_HIT;
    int gun = kind == HIT_PISTOL;
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    if (e->plan == P_DEATH) return;
    e->yaw = atan2f(from_x - e->pos[0], from_z - e->pos[2]);   /* knocked away from him */
    if (gun) {
        /* The stone gauge: twice the shot's power (a pistol shot's 5, inferred), capped at 1000; each 60 shot
         * at it is a flinch and 120 more. */
        e->goyle_stone += 10.0f; e->goyle_shots += 5.0f;
        if (e->goyle_shots > 60.0f) { e->goyle_stone += 120.0f; e->goyle_shots = 0.0f; if (!stone) { e->vel[0] = 4.0f; goyle_state(G_FLINCH_HEAVY); } }
        if (e->goyle_stone > 1000.0f) e->goyle_stone = 1000.0f;
        if (!stone && e->goyle_stone > GOYLE_STONE_AT) { e->vel[0] = 0.0f; e->vel[1] = 0.0f; goyle_state(G_STONE); }
        if (mortal && ai_unit() < 0.15f) --e->health;   /* bullets barely hurt it */
    } else if (stone) {
        /* Struck stone: up it goes, and it takes three times the damage. */
        e->vel[1] = 23.0f; e->vel[0] = kind == HIT_HEAVY ? 12.0f : 4.0f;
        goyle_state(G_STONE_HIT);
        if (mortal) e->health = (signed char)(e->health - (kind == HIT_LIGHT || kind == HIT_AIR ? 3 : 6));
    } else {
        if (mortal) e->health = (signed char)(e->health - (kind == HIT_LIGHT || kind == HIT_AIR ? 1 : 2));
        if (e->health > 0 && e->goyle_split <= 0.0f && e->goyle_splits > 0 && goyle_count() < GOYLE_MAX_OUT && ai_unit() < 0.75f) {
            --e->goyle_splits;
            e->goyle_split = 300.0f;
            e->vel[0] = 10.0f;
            goyle_state(G_SPLIT);
            fx_explosion(at);
            goyle_split_off();
        } else {
            e->vel[0] = kind == HIT_HEAVY || kind == HIT_LAUNCH ? 12.0f : 3.0f;
            goyle_state(kind == HIT_LIGHT || kind == HIT_AIR ? G_FLINCH : G_FLINCH_HEAVY);
        }
    }
    if (mortal && e->health <= 0) {
        if (stone) { fx_explosion(at); fx_wall_spark(at); }
        e->plan = P_DEATH;
        e->timer = 0.0f;
        goyle_state(G_DEATH);
        e->plan = P_DEATH;
        died();
    }
}

/* ---- The Jester (CEm044, SE; em037.pac, GDATA 140), Mission 5's boss in room 422 (docs/ENEMY_SPECS.md
 * section E). The game builds him like a player character, with moves (actions 0-0x15) an AI picks and
 * reactions (0x64-0x80), 383 functions in all (0x532000-0x556000): not ported. This is a stand-in fight on
 * his own model and motions: he walks in and swings his cane, lobs bombs that roll at Dante, teleports
 * behind him, and dances. Hits fill a stun gauge; stunned, he takes double damage. Which motion is which
 * was chosen by their shapes and lengths (inferred); his health is an estimate. The arena's centre,
 * (3500, 492, 3500), is from his set-up (FUN_005331d0). */
#define JESTER_HEALTH (ENEMY_HEALTH * 1500 / 250)
#define JESTER_STUN_HITS 12.0f
#define JESTER_ARENA 1400.0f   /* how far from the arena's centre he may go (its floor runs to 1550) */
enum { J_IDLE, J_WALK, J_CANE, J_CANE2, J_BOMB, J_VANISH, J_APPEAR, J_TAUNT, J_FLINCH, J_STUN, J_LAUNCHED, J_DEATH };
/* jsNN, in tools/prepare_skinned_model.py's order: [3] 0-2, [4] 3-20, [5] 21-29, [6] 30-33, [19] 34-40. */
static const signed char jester_motion_index[] = {21, 3, 8, 9, 10, 26, 26, 31, 5, 14, 4, 16};
static int jester_motion[sizeof(jester_motion_index)];

static int jester_init(void) {
    unsigned int i;
    for (i = 0; i < sizeof(jester_motion_index); ++i) {
        char name[5] = {'j', 's', (char)('0' + jester_motion_index[i] / 10), (char)('0' + jester_motion_index[i] % 10), 0};
        jester_motion[i] = motion_find(name);
        if (jester_motion[i] < 0) return 0;
    }
    return 1;
}

static void jester_state(int s) {
    int m = jester_motion[s];
    e->action = (unsigned char)s;
    e->timer = 0.0f;
    e->step = 0;
    e->done = 0;
    if (m != e->motion) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = m; e->frame = 0.0f;
    }
}

static void jester_puff(void) {
    int k;
    for (k = 0; k < 10; ++k) {
        float at[3] = {e->pos[0] + ai_unit() * 120.0f - 60.0f, e->pos[1] + ai_unit() * 180.0f, e->pos[2] + ai_unit() * 120.0f - 60.0f};
        float v[3] = {0.0f, 60.0f + ai_unit() * 60.0f, 0.0f};
        fx_mote(at, v, 90.0f, 20.0f, 0.5f, (k & 1) ? 0xffff60ffu : 0xffffd0ffu);
    }
}

static void jester_spawn(void) {
    e->plan = P_WATCH;
    e->motion = -1;
    e->goyle_stone = 0.0f;   /* his stun gauge */
    jester_state(J_TAUNT);
    e->fade = 0.0f;
}

static void jester_decide(float d) {
    float r = ai_unit();
    if (d > 260.0f && r < 0.45f) { jester_state(J_WALK); return; }
    if (d <= 260.0f && r < 0.55f) { jester_state(J_CANE); return; }
    r = ai_unit();
    jester_state(r < 0.4f ? J_BOMB : r < 0.75f ? J_VANISH : J_TAUNT);
}

static void jester_update(float f, float px, float py, float pz) {
    const Motion *m;
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, s, c;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->goyle_stone > 0.0f && e->action != J_STUN) { e->goyle_stone -= 0.02f * f; if (e->goyle_stone < 0.0f) e->goyle_stone = 0.0f; }
    m = motion_get(e->motion);
    e->frame += f;
    e->timer += f;
    if (e->frame >= m->end_frame) {
        e->done = 1;
        e->frame = (e->action == J_IDLE || e->action == J_WALK || e->action == J_TAUNT) ? motion_wrap(m, e->frame) : m->end_frame;
    }
    if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
    if (e->plan == P_DEATH) {
        if (e->timer > 150.0f) { jester_puff(); e->active = 0; }
        return;
    }
    s = sinf(e->yaw); c = cosf(e->yaw);
    switch (e->action) {
    case J_IDLE:
        goyle_turn_to(px, pz, 0.08f, f);
        if (e->timer > 50.0f) jester_decide(d);
        break;
    case J_WALK:
        goyle_turn_to(px, pz, 0.1f, f);
        e->pos[0] += s * 5.0f * f; e->pos[2] += c * 5.0f * f;
        if (d < 220.0f) jester_state(J_CANE);
        else if (e->timer > 120.0f) jester_decide(d);
        break;
    case J_CANE: case J_CANE2:
        if (e->frame < 25.0f) goyle_turn_to(px, pz, 0.12f, f);
        if (e->frame > 25.0f && e->frame < 45.0f) { e->pos[0] += s * 4.0f * f; e->pos[2] += c * 4.0f * f; }
        if (!e->step && e->frame > 35.0f) {
            float hx = e->pos[0] + s * 150.0f - px, hz = e->pos[2] + c * 150.0f - pz;
            e->step = 1;
            if (attacks_on && hx * hx + hz * hz < 170.0f * 170.0f && fabsf(py - e->pos[1]) < 200.0f) land_attack(e->action == J_CANE2);
        }
        if (e->done) { if (e->action == J_CANE && ai_unit() < 0.5f) jester_state(J_CANE2); else jester_state(J_IDLE); }
        break;
    case J_BOMB:
        goyle_turn_to(px, pz, 0.1f, f);
        if (!e->step && e->frame > 30.0f) {
            int k;
            e->step = 1;
            /* Three bombs: they drop by him, then roll at where Dante was (the Gigapede's rolling orb). */
            for (k = -1; k <= 1; ++k) {
                float at[3] = {e->pos[0] + s * 120.0f + c * 80.0f * (float)k, e->pos[1] + 170.0f, e->pos[2] + c * 120.0f - s * 80.0f * (float)k};
                giga_orb(at, 1, px + c * 150.0f * (float)k, py, pz - s * 150.0f * (float)k);
            }
        }
        if (e->done) jester_state(J_IDLE);
        break;
    case J_VANISH:
        if (e->timer < 2.0f && !e->step) { e->step = 1; jester_puff(); }
        if (e->timer > 20.0f) {
            /* Back behind Dante, in the arena. */
            float a = atan2f(dx, dz);
            e->pos[0] = px + sinf(a) * 320.0f; e->pos[2] = pz + cosf(a) * 320.0f;
            if (floor_under(e->pos[0], py + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
            else { e->pos[0] = 3500.0f; e->pos[1] = 492.0f; e->pos[2] = 3500.0f; }
            e->yaw = atan2f(px - e->pos[0], pz - e->pos[2]);
            jester_state(J_APPEAR);
            jester_puff();
        }
        break;
    case J_APPEAR:
        if (e->timer > 25.0f) jester_state(ai_unit() < 0.6f && d < 400.0f ? J_CANE : J_IDLE);
        break;
    case J_TAUNT:
        goyle_turn_to(px, pz, 0.05f, f);
        if (e->timer > 150.0f) jester_decide(d);
        break;
    case J_FLINCH:
        e->pos[0] -= s * e->vel[0] * f; e->pos[2] -= c * e->vel[0] * f; e->vel[0] *= 0.85f;
        if (e->done) jester_state(ai_unit() < 0.5f ? J_VANISH : J_IDLE);
        break;
    case J_STUN:
        if (e->timer > 200.0f) { e->goyle_stone = 0.0f; jester_state(J_VANISH); }
        break;
    case J_LAUNCHED:
        if (e->done) jester_state(J_IDLE);
        break;
    default:
        jester_state(J_IDLE);
    }
    /* His arena is a square platform, +-1550 cm about (3500, 3500) at 492, with nothing past its edge: he stays
     * on it (teleports and walks included), on its floor. */
    if (e->pos[0] < 3500.0f - JESTER_ARENA) e->pos[0] = 3500.0f - JESTER_ARENA;
    if (e->pos[0] > 3500.0f + JESTER_ARENA) e->pos[0] = 3500.0f + JESTER_ARENA;
    if (e->pos[2] < 3500.0f - JESTER_ARENA) e->pos[2] = 3500.0f - JESTER_ARENA;
    if (e->pos[2] > 3500.0f + JESTER_ARENA) e->pos[2] = 3500.0f + JESTER_ARENA;
    if (!floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) g = 492.0f;
    e->floor_y = g;
    if (e->action != J_VANISH) e->pos[1] = e->floor_y;
}

static void jester_hit(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
    int stunned = e->action == J_STUN;
    if (e->plan == P_DEATH || e->action == J_VANISH) return;
    ++hits;
    fx_hit_spark(at);
    if (mortal) {
        int dmg = kind == HIT_PISTOL ? (ai_unit() < 0.2f) : (kind == HIT_LIGHT || kind == HIT_AIR) ? 1 : 2;
        if (stunned) dmg *= 2;
        e->health = (signed char)(e->health - dmg);
        if (e->health <= 0) {
            e->plan = P_DEATH;
            jester_state(J_DEATH);
            e->plan = P_DEATH;
            died();
            return;
        }
    }
    if (stunned) return;
    e->goyle_stone += kind == HIT_PISTOL ? 0.2f : 1.0f;
    if (e->goyle_stone >= JESTER_STUN_HITS) { jester_state(J_STUN); return; }
    if (kind == HIT_LAUNCH) { jester_state(J_LAUNCHED); return; }
    if (kind != HIT_PISTOL && (e->action == J_IDLE || e->action == J_WALK || e->action == J_TAUNT || e->action == J_FLINCH || e->action == J_BOMB)) {
        e->vel[0] = kind == HIT_HEAVY ? 10.0f : 3.0f;
        jester_state(J_FLINCH);
    }
}

#include "pawn.inc"

/* ---- Agni & Rudra (class 27, EST 45; em026.pac, GDATA 131), Mission 5's boss in room 121 (docs/ENEMY_SPECS.md
 * section F). Class 27 (`FUN_00480bb0`, an object of 0x290 bytes) is a controller over the two brothers; their
 * own code isn't ported. A stand-in fight on their model, swords and motions: each walks in and slashes
 * (a three-hit combo, or an overhead), dashes in and out, leaps in with a jump slash, or charges his sword and
 * drives it in: Agni's sends a fire wave along the floor, Rudra's a whirlwind that chases Dante. Now and then
 * they team up: both dash through Dante from either side. When one falls the other fights on alone, faster.
 * Agni is red, Rudra blue (a tint: their textures are shared). Motions were chosen from their shapes and
 * root motion [inferred]; health is an estimate (the guide's boss bonus, 4000 orbs, and the two bars). */
#define AR_HEALTH (ENEMY_HEALTH * 1200 / 250)
enum { AR_IDLE, AR_WALK, AR_COMBO1, AR_COMBO2, AR_COMBO3, AR_OVERHEAD, AR_DASH_IN, AR_DASH_OUT, AR_LEAP, AR_CHARGE,
       AR_TEAM, AR_FLINCH, AR_KNOCK, AR_DEATH, AR_STATUE, AR_STATUE2, AR_STATE_COUNT };
/* arNN: their slots in [11]. */
static const signed char ar_motion_slot[AR_STATE_COUNT] = {0, 1, 36, 37, 38, 39, 3, 4, 7, 42, 2, 30, 44, 83, 71, 72};
static int ar_motion[AR_STATE_COUNT];

static int ar_init(void) {
    int i;
    for (i = 0; i < AR_STATE_COUNT; ++i) {
        char name[5] = {'a', 'r', (char)('0' + ar_motion_slot[i] / 10), (char)('0' + ar_motion_slot[i] % 10), 0};
        ar_motion[i] = motion_find(name);
        if (ar_motion[i] < 0) return 0;
    }
    return 1;
}

static Enemy *ar_brother(void) {
    int i, want = e->kind == ENEMY_AGNI ? ENEMY_RUDRA : ENEMY_AGNI;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == want && enemies[i].plan != P_DEATH) return &enemies[i];
    return 0;
}

static void ar_state(int s) {
    int m = ar_motion[s];
    e->action = (unsigned char)s;
    e->timer = 0.0f;
    e->step = 0;
    e->done = 0;
    e->prev_motion = e->motion; e->prev_frame = e->frame;
    e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 6.0f;
    e->motion = m; e->frame = 0.0f;
}

static void ar_rudra_spawn(void) { e->plan = P_WATCH; e->motion = -1; ar_state(AR_STATUE2); e->fade = 0.0f; }

/* The one spawn (class 27) brings both: Agni here, Rudra beside him. */
static void ar_spawn(void) {
    Enemy *agni = e;
    int k;
    agni->plan = P_WATCH; agni->motion = -1;
    agni->pos[0] += 180.0f;
    ar_state(AR_STATUE); agni->fade = 0.0f;
    for (k = 0; k < ENEMY_MAX; ++k) {
        Enemy *o = &enemies[k];
        if (o->active) continue;
        e = o;
        spawn_one(ENEMY_RUDRA, agni->pos[0] - 360.0f, agni->pos[2]);
        break;
    }
    e = agni;
}

static void ar_decide(float d) {
    float r = ai_unit();
    Enemy *bro = ar_brother();
    if (attacks_on && attack_token <= 0.0f) {
        attack_token = bro ? 3.0f : 1.8f;   /* alone, he presses harder */
        if (bro && r < 0.15f && bro->action <= AR_WALK) {
            /* Double team: both run through him. */
            Enemy *me = e;
            ar_state(AR_TEAM);
            e = bro; ar_state(AR_TEAM); e = me;
            return;
        }
        r = ai_unit();
        if (d > 700.0f) { ar_state(r < 0.5f ? AR_LEAP : AR_CHARGE); return; }
        if (d > 300.0f) { ar_state(r < 0.6f ? AR_DASH_IN : AR_CHARGE); return; }
        ar_state(r < 0.6f ? AR_COMBO1 : r < 0.85f ? AR_OVERHEAD : AR_DASH_OUT);
        return;
    }
    ar_state(d > 280.0f && r < 0.7f ? AR_WALK : AR_IDLE);
}

static void ar_strike(float reach, float radius, int heavy, float px, float py, float pz) {
    float s = sinf(e->yaw), c = cosf(e->yaw), hx = e->pos[0] + s * reach - px, hz = e->pos[2] + c * reach - pz;
    if (attacks_on && hx * hx + hz * hz < radius * radius && fabsf(py - e->pos[1]) < 250.0f) land_attack(heavy);
}

static void ar_update(float f, float px, float py, float pz) {
    const Motion *m;
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, s, c, speed = 0.0f;
    float rate = ar_brother() ? 1.0f : 1.25f;   /* alone, faster */
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    m = motion_get(e->motion);
    e->frame += f * rate;
    e->timer += f * rate;
    if (e->frame >= m->end_frame) {
        e->done = 1;
        e->frame = (e->action == AR_IDLE || e->action == AR_WALK) ? motion_wrap(m, e->frame) : m->end_frame;
    }
    if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
    e->pos[1] = e->floor_y;
    if (e->plan == P_DEATH) {
        if (e->timer > 200.0f) e->active = 0;
        return;
    }
    s = sinf(e->yaw); c = cosf(e->yaw);
    if (e->action == AR_STATUE || e->action == AR_STATUE2) {
        /* They stand as statues (their one-frame poses ar71, ar72) until he comes near, strikes one, or waits;
         * then both wake. The game plays a cutscene here. */
        Enemy *bro = ar_brother();
        if (d < 900.0f || e->timer > 600.0f || e->step || (bro && bro->action < AR_STATUE)) {
            float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]};
            fx_dust(at); fx_explosion(at);
            ar_state(AR_IDLE);
        }
        return;
    }
    switch (e->action) {
    case AR_IDLE:
        goyle_turn_to(px, pz, 0.06f, f);
        if (e->timer > 40.0f) ar_decide(d);
        break;
    case AR_WALK:
        goyle_turn_to(px, pz, 0.08f, f);
        speed = 1.3f;   /* the walk's own root: 128 cm in 101 frames */
        if (d < 260.0f || e->timer > 150.0f) ar_decide(d);
        break;
    case AR_COMBO1: case AR_COMBO2: case AR_COMBO3:
        if (e->frame < 15.0f) goyle_turn_to(px, pz, 0.12f, f);
        speed = 81.0f / 67.0f;
        if (!e->step && e->frame > 25.0f) { e->step = 1; ar_strike(170.0f, 190.0f, e->action == AR_COMBO3, px, py, pz); }
        if (e->done) {
            if (e->action != AR_COMBO3 && d < 330.0f && ai_unit() < 0.7f) ar_state(e->action + 1);
            else ar_state(AR_IDLE);
        }
        break;
    case AR_OVERHEAD:
        if (e->frame < 20.0f) goyle_turn_to(px, pz, 0.1f, f);
        if (!e->step && e->frame > 32.0f) {
            float at[3] = {e->pos[0] + s * 180.0f, e->floor_y, e->pos[2] + c * 180.0f};
            e->step = 1; ar_strike(180.0f, 170.0f, 1, px, py, pz); fx_dust(at);
        }
        if (e->done) ar_state(AR_IDLE);
        break;
    case AR_DASH_IN: case AR_DASH_OUT:
        if (e->frame < 4.0f) goyle_turn_to(px, pz, 0.3f, f);
        speed = e->action == AR_DASH_IN ? 9.7f : -9.3f;
        if (e->done) ar_state(e->action == AR_DASH_IN ? AR_COMBO1 : AR_IDLE);
        break;
    case AR_LEAP:
        if (e->frame < 30.0f) goyle_turn_to(px, pz, 0.1f, f);
        /* Carried over the gap to him while airborne. */
        if (e->frame > 20.0f && e->frame < 60.0f && d > 150.0f) speed = fminf(d / 40.0f, 25.0f);
        if (!e->step && e->frame > 62.0f) {
            float at[3] = {e->pos[0] + s * 120.0f, e->floor_y, e->pos[2] + c * 120.0f};
            e->step = 1; ar_strike(120.0f, 220.0f, 1, px, py, pz); fx_dust(at);
        }
        if (e->done) ar_state(AR_IDLE);
        break;
    case AR_CHARGE:
        if (e->frame < 120.0f) {
            goyle_turn_to(px, pz, 0.05f, f);
            if (((int)e->frame & 7) == 0) {
                /* The blade takes its element. */
                float at[3] = {e->hand_pos[0], e->hand_pos[1], e->hand_pos[2]}, v[3] = {0.0f, 40.0f, 0.0f};
                at[0] = e->pos[0] + at[0] * c + at[2] * s; at[1] += e->pos[1]; at[2] = e->pos[2] + at[2] * c - at[0] * 0.0f;
                fx_mote(at, v, 80.0f, 10.0f, 0.4f, e->kind == ENEMY_AGNI ? 0xff2080ffu : 0xffffd080u);
            }
        }
        if (!e->step && e->frame > 150.0f) {
            /* Driven in: a wave (Agni's fire) or a whirlwind (Rudra's wind) that runs at Dante. */
            float at[3] = {e->pos[0] + s * 160.0f, e->floor_y + 150.0f, e->pos[2] + c * 160.0f};
            e->step = 1;
            giga_orb(at, 1, px, py, pz);
            if (e->kind == ENEMY_RUDRA) { float at2[3] = {at[0] + c * 150.0f, at[1], at[2] - s * 150.0f}; giga_orb(at2, 0, px, py, pz); }
            fx_explosion(at);
        }
        if (e->done) ar_state(AR_IDLE);
        break;
    case AR_TEAM:
        /* Run straight through him, from where each stands. */
        if (e->frame < 10.0f) goyle_turn_to(px, pz, 0.3f, f);
        speed = 918.0f / 110.0f;
        if (!e->step && d < 160.0f) { e->step = 1; ar_strike(0.0f, 200.0f, 1, px, py, pz); }
        if (e->done) ar_state(AR_IDLE);
        break;
    case AR_FLINCH:
        speed = -e->vel[0]; e->vel[0] *= 0.85f;
        if (e->done) ar_state(ai_unit() < 0.4f ? AR_DASH_OUT : AR_IDLE);
        break;
    case AR_KNOCK:
        speed = -262.0f / 109.0f;
        if (e->done) ar_state(AR_IDLE);
        break;
    default:
        ar_state(AR_IDLE);
    }
    e->pos[0] += s * speed * f * rate; e->pos[2] += c * speed * f * rate;
}

static void ar_hit(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]};
    if (e->plan == P_DEATH) return;
    ++hits;
    fx_hit_spark(at);
    if (e->action == AR_STATUE || e->action == AR_STATUE2) { e->step = 1; return; }   /* struck awake */
    if (mortal) {
        int dmg = kind == HIT_PISTOL ? (ai_unit() < 0.2f) : (kind == HIT_LIGHT || kind == HIT_AIR) ? 1 : 2;
        /* Agni's fire is weak to Cerberus's ice (the guide); twice the damage is an estimate. */
        if (e->kind == ENEMY_AGNI && kind != HIT_PISTOL && hit_weapon == 1) dmg *= 2;
        e->health = (signed char)(e->health - dmg);
        if (e->health <= 0) {
            e->plan = P_DEATH;
            ar_state(AR_DEATH);
            e->plan = P_DEATH;
            died();
            return;
        }
    }
    /* They shrug off light hits mid-swing; heavy ones knock them back. */
    if (kind == HIT_HEAVY || kind == HIT_SLAM) { ar_state(AR_KNOCK); return; }
    if (kind != HIT_PISTOL && (e->action == AR_IDLE || e->action == AR_WALK)) { e->vel[0] = 3.0f; ar_state(AR_FLINCH); }
}

static float ar_health_total(float *maximum) {
    int i;
    float h = 0.0f;
    *maximum = (float)(AR_HEALTH * 2);
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && (enemies[i].kind == ENEMY_AGNI || enemies[i].kind == ENEMY_RUDRA) && enemies[i].plan != P_DEATH)
            h += (float)enemies[i].health;
    return h;
}

/* ---- The Gigapede (CEm023), Mission 4's boss (docs/ENEMY_SPECS.md section C, rank 0). Its head runs the
 * game's routes of movement paths through room 111's walls, holes and ceiling, the 7 segments trailing 500 cm
 * apart along the same track. Each part is an enemy slot, so Dante's attacks, lock-on and the room's fight
 * work as for any enemy; damage goes to one shared pool, the head's 2800.
 *
 * From the spec: the routes' events (homing spheres, the drop-then-roll orb, belly beams, the roll), segment
 * shells (4 hits at rank 0; broken, the plates go, the segment stops firing and takes more damage, and the
 * shell grows back once the segment has been through a hole), the shake-off when Dante stands on it (60
 * frames of sparks, then a full roll that runs down the body 4 frames a segment), the all-shells-broken rush,
 * and the death run out along path 13.
 * Simplified: drawn rigid and cut to a third of its triangles (tools/prepare_gigapede.py); the body hides its
 * top carapace for a broken shell rather than the game's staged plates; damage to Dante is the default. */
#include "gigapede_runtime.h"
#define GIGA_PARTS 8               /* the head and 7 segments */
#define GIGA_SPACING 500.0f
#define GIGA_HISTORY 512
#define GIGA_HEALTH (ENEMY_HEALTH * 2800.0f / 250.0f)
#define GIGA_SHELL 4               /* rank 0's shell hits, 0x5bba80 */
#define GIGA_SPARK_FRAMES 60.0f    /* rank 0's warning before the shake-off roll, 0x5baab0 */
#define GIGA_BEAMS 3
#define GIGA_TOP_BACK -560.0f      /* the carapace Dante can stand on, along a segment (cm from its front) */
#define GIGA_TOP_FRONT 60.0f
#define GIGA_TOP_HALF 170.0f
#define TWO_PI 6.2831853f
typedef struct { signed char path; float speed; short at[4]; signed char event[4]; } GigaStep;
/* Rank 0 routes (0x5ba9b0): 11 the entrance, then 1, 4, 7 by phase. Where a route has two choices by
 * Dante's height, these are the ones for him on the floor. Events: 1-2 homing spheres, 3-4 drop-then-roll
 * orbs, 9 belly beams, 10 a roll. */
static const GigaStep giga_route[4][5] = {
    {{14, 1.0f, {-1, -1, -1, -1}, {0}}, {0, 1.0f, {-1, -1, -1, -1}, {0}}, {-1, 0, {0}, {0}}},
    {{3, 1.0f, {2000, -1, -1, -1}, {1}}, {1, 2.0f, {1200, -1, -1, -1}, {1}}, {-1, 0, {0}, {0}}},
    {{11, 1.0f, {1200, 1700, -1, -1}, {3, 3}}, {9, 1.0f, {1100, 2200, -1, -1}, {2, 0}}, {-1, 0, {0}, {0}}},
    {{3, 1.0f, {1300, -1, -1, -1}, {2}}, {6, 0.8f, {800, 1500, 1550, -1}, {10, 9, 1}}, {16, 1.0f, {1200, -1, -1, -1}, {2}},
     {-1, 0, {0}, {0}}},
};
enum { GIGA_NORMAL, GIGA_SHAKE, GIGA_ROLL, GIGA_RUSH };
static struct {
    int active, route, step, dying, state, rider, ride_frames;
    float health, frame, rate, dead_time, state_time, throw_time, arc;
    float history[GIGA_HISTORY][3];
    int history_head;
    float pos[GIGA_PARTS][3], prev[GIGA_PARTS][3], dir[GIGA_PARTS][3], roll[GIGA_PARTS];
    int shell[GIGA_PARTS], out[GIGA_PARTS];   /* out: in the walls, not drawn and not a target */
    float regrow_at[GIGA_PARTS];
    unsigned char fired[4];
    struct { float life, hit_wait; int part; } beam[GIGA_BEAMS];
} giga;
static char giga_log_text[3072];
static int giga_log_len, giga_ticks;
static void giga_note(const char *what, int a, float b) {
    if (giga_log_len < (int)sizeof(giga_log_text) - 80)
        giga_log_len += snprintf(giga_log_text + giga_log_len, sizeof(giga_log_text) - giga_log_len,
                                 "giga t=%d %s %d %.1f hp=%.1f\n", giga_ticks, what, a, b, giga.health);
}
const char *enemy_giga_log(void) { return giga_log_text; }
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
    int k;
    giga.frame = (float)path_enter(s->path);
    memset(giga.fired, 0, sizeof(giga.fired));
    /* A new path starts in a hole: each broken shell grows back once its segment has followed it in. */
    for (k = 1; k < GIGA_PARTS; ++k)
        if (giga.shell[k] <= 0 && giga.regrow_at[k] <= 0.0f) giga.regrow_at[k] = giga.arc + GIGA_SPACING * (float)k;
}

/* A claimed slot per part: the head is the slot that was spawned. */
static void giga_spawn(void) {
    Enemy *head = e;
    int k, found = 1, i;
    float p[3];
    memset(&giga, 0, sizeof(giga));
    giga_log_len = 0; giga_log_text[0] = 0; giga_ticks = 0;
    giga.active = 1;
    giga.health = GIGA_HEALTH;
    giga.rate = 1.4f;
    giga.rider = -1;
    for (k = 0; k < GIGA_PARTS; ++k) giga.shell[k] = GIGA_SHELL;
    giga_begin_step();
    path_at(giga_route[0][0].path, giga.frame, p);
    for (i = 0; i < GIGA_HISTORY; ++i) memcpy(giga.history[i], p, sizeof(p));
    for (k = 0; k < GIGA_PARTS; ++k) { memcpy(giga.pos[k], p, sizeof(p)); memcpy(giga.prev[k], p, sizeof(p)); giga.dir[k][2] = 1.0f; }
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
    float dx = p[0] - last[0], dy = p[1] - last[1], dz = p[2] - last[2], d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < 25.0f * 25.0f) return;
    giga.arc += sqrtf(d2);
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

/* A part's frame: forward along the body, right and up rolled about it by the part's roll. */
static void giga_axes(int k, float r[3], float u[3]) {
    const float *f = giga.dir[k];
    float rx = f[2], rz = -f[0], l = sqrtf(rx * rx + rz * rz), ux, uy, uz, c = cosf(giga.roll[k]), s = sinf(giga.roll[k]);
    if (l < 0.01f) { rx = 1.0f; rz = 0.0f; l = 1.0f; }
    rx /= l; rz /= l;
    /* up = forward x right */
    ux = f[1] * rz; uy = f[2] * rx - f[0] * rz; uz = -f[1] * rx;
    r[0] = rx * c + ux * s; r[1] = uy * s; r[2] = rz * c + uz * s;
    u[0] = ux * c - rx * s; u[1] = uy * c; u[2] = uz * c - rz * s;
}

/* The belly (bone 0, under the middle of a segment). */
static void giga_belly(int k, float out[3]) {
    float r[3], u[3];
    giga_axes(k, r, u);
    out[0] = giga.pos[k][0] - giga.dir[k][0] * 300.0f - u[0] * 80.0f;
    out[1] = giga.pos[k][1] - giga.dir[k][1] * 300.0f - u[1] * 80.0f;
    out[2] = giga.pos[k][2] - giga.dir[k][2] * 300.0f - u[2] * 80.0f;
}

static void giga_orb(const float from[3], int kind, float px, float py, float pz);

static void giga_event(int ev, float px, float py, float pz) {
    int k;
    float at[3];
    /* Only segments with their shell fire (0x38bba0). */
    if (ev == 1 || ev == 2) for (k = 1; k < GIGA_PARTS; ++k) { if (giga.shell[k] > 0 && !giga.out[k]) { giga_belly(k, at); giga_orb(at, 0, px, py, pz); } }
    if (ev == 3 || ev == 4) for (k = 2; k < GIGA_PARTS; k += 2) { if (giga.shell[k] > 0 && !giga.out[k]) { giga_belly(k, at); giga_orb(at, 1, px, py, pz); } }
    if (ev == 8 || ev == 9)
        for (k = 0; k < GIGA_BEAMS; ++k) { giga.beam[k].life = 60.0f; giga.beam[k].hit_wait = 0.0f; giga.beam[k].part = 1 + (int)(ai_random() % (GIGA_PARTS - 1)); }
    giga_note("event", ev, giga.frame);
    if (ev == 10 && giga.state == GIGA_NORMAL) { giga.state = GIGA_ROLL; giga.state_time = 0.0f; }
}

/* The roll: each part turns per_frame radians a frame for `frames`, starting 4 frames after the one in
 * front, `wait` frames in. t: frames since it began. */
static void giga_roll_wave(float t, float wait, float per_frame, float frames) {
    int k;
    for (k = 0; k < GIGA_PARTS; ++k) {
        float local = t - wait - 4.0f * (float)k;
        if (local < 0.0f) local = 0.0f;
        if (local > frames) local = frames;
        giga.roll[k] = per_frame * local;
    }
}

static void giga_sparks(void) {
    int k;
    for (k = 0; k < GIGA_PARTS; ++k) {
        float at[3], v[3] = {0.0f, 60.0f, 0.0f};
        unsigned int r = fx_random_u32();
        at[0] = giga.pos[k][0] - giga.dir[k][0] * 300.0f + (float)((int)(r & 255) - 128);
        at[1] = giga.pos[k][1] + (float)((int)(r >> 8 & 127) - 30);
        at[2] = giga.pos[k][2] - giga.dir[k][2] * 300.0f + (float)((int)(r >> 16 & 255) - 128);
        fx_mote(at, v, 70.0f, 10.0f, 0.25f, (r >> 24 & 1) ? 0xffffa0ffu : 0xffff60c0u);
    }
}

static void giga_throw(void) {
    if (giga.rider < 0) return;
    giga_note("throw", giga.rider, giga.roll[giga.rider]);
    landed = 1;
    landed_x = giga.pos[giga.rider][0];
    landed_z = giga.pos[giga.rider][2];
    landed_heavy = 1;
    landed_damage = -1.0f;
    giga.throw_time = 40.0f;
    giga.rider = -1;
    giga.ride_frames = 0;
}

static void giga_update(float f, float px, float py, float pz) {
    const GigaStep *s;
    float target, p[3];
    int k, near = 0, broken = 0;
    if (!giga.active) return;
    giga_ticks += (int)(f + 0.5f);
    {
        static int last_state = -1, last_route = -1, last_step = -1;
        if (giga.state != last_state) { giga_note("state", giga.state, giga.state_time); last_state = giga.state; }
        if (giga.route != last_route || giga.step != last_step) { giga_note("path", giga_route[giga.route][giga.step].path, (float)giga.route); last_route = giga.route; last_step = giga.step; }
    }
    memcpy(giga.prev, giga.pos, sizeof(giga.prev));
    if (giga.throw_time > 0.0f) giga.throw_time -= f;
    for (k = 1; k < GIGA_PARTS; ++k) {
        if (giga.shell[k] <= 0) ++broken;
        if (giga.regrow_at[k] > 0.0f && giga.arc >= giga.regrow_at[k]) { giga_note("shell_regrow", k, giga.arc); giga.shell[k] = GIGA_SHELL; giga.regrow_at[k] = 0.0f; }
    }
    giga.state_time += f;
    if (giga.dying) {
        /* Route 10: path 13 at rate 4, out through its hole, flickering, then gone. */
        giga.dead_time += f;
        giga.frame += 4.0f * f;
        if (giga.dead_time > 150.0f || giga.frame >= (float)path_frames(13)) {
            for (k = 0; k < ENEMY_MAX; ++k) if (enemies[k].kind == ENEMY_GIGAPEDE) enemies[k].active = 0;
            giga.active = 0;
            return;
        }
        path_at(13, giga.frame, p);
        goto place;
    }
    /* The head's states (0x388690 and on). */
    switch (giga.state) {
    case GIGA_NORMAL:
        for (k = 0; k < GIGA_PARTS; ++k) {   /* unwinds any roll */
            float r = fmodf(giga.roll[k], TWO_PI);
            if (r > TWO_PI * 0.5f) r -= TWO_PI;
            giga.roll[k] = fabsf(r) < 0.05f * f ? 0.0f : r - (r > 0.0f ? 0.05f : -0.05f) * f;
        }
        if (giga.ride_frames > 0) { giga_note("rider", giga.rider, 0.0f); giga.state = GIGA_SHAKE; giga.state_time = 0.0f; }
        else if (broken == GIGA_PARTS - 1) { giga.state = GIGA_RUSH; giga.state_time = 0.0f; }
        break;
    case GIGA_SHAKE:
        /* 0x68: sparks on every slot, then one full turn at 0x400 a frame (64 frames). */
        if (giga.state_time < GIGA_SPARK_FRAMES) { if ((int)giga.state_time % 3 == 0) giga_sparks(); }
        giga_roll_wave(giga.state_time, GIGA_SPARK_FRAMES, TWO_PI / 64.0f, 64.0f);
        if (giga.state_time > GIGA_SPARK_FRAMES + 64.0f + 4.0f * GIGA_PARTS) { giga.state = GIGA_NORMAL; memset(giga.roll, 0, sizeof(giga.roll)); }
        break;
    case GIGA_ROLL:
        /* 0x69: waits 20, then 0x200 a frame for 64 frames: half a turn, unwound after. */
        giga_roll_wave(giga.state_time, 20.0f, TWO_PI / 128.0f, 64.0f);
        if (giga.state_time > 20.0f + 64.0f + 4.0f * GIGA_PARTS) giga.state = GIGA_NORMAL;
        break;
    case GIGA_RUSH:
        /* 0x6a: every shell broken. Waits 120, then rolls 0x200 a frame for 300 frames at path rate 5. */
        if (giga.state_time < 120.0f) { if ((int)giga.state_time % 4 == 0) giga_sparks(); }
        else giga_roll_wave(giga.state_time, 120.0f, TWO_PI / 128.0f, 300.0f);
        if (giga.state_time > 420.0f + 4.0f * GIGA_PARTS) {
            giga.state = GIGA_NORMAL;
            for (k = 1; k < GIGA_PARTS; ++k) if (giga.regrow_at[k] <= 0.0f) giga.regrow_at[k] = giga.arc + GIGA_SPACING * (float)k;
        }
        break;
    }
    /* Thrown off once his segment has turned past about a third. */
    if (giga.rider >= 0 && fabsf(sinf(giga.roll[giga.rider])) > 0.6f) giga_throw();
    s = &giga_route[giga.route][giga.step];
    /* Rate: the step's speed x 1.4 (x 1.8 in its last phase), 0.05 less a broken shell (not under 0.5),
     * doubled when no part is near Dante, easing 0.01 a frame. */
    for (k = 0; k < GIGA_PARTS; ++k) {
        float dx = giga.pos[k][0] - px, dz = giga.pos[k][2] - pz;
        if (dx * dx + dz * dz < 1000.0f * 1000.0f) near = 1;
    }
    target = s->speed * (giga.health < GIGA_HEALTH * 500.0f / 2800.0f ? 1.8f : 1.4f) - 0.05f * (float)broken;
    if (target < 0.5f) target = 0.5f;
    if (!near) target *= 2.0f;
    if (s->path == 14) target = s->speed * 2.8f;
    giga.rate += (target - giga.rate) * 0.01f * f;
    if (giga.state == GIGA_RUSH && giga.state_time >= 120.0f) giga.rate = 5.0f;
    giga.frame += giga.rate * f;
    for (k = 0; k < 4; ++k)
        if (!giga.fired[k] && s->at[k] >= 0 && giga.frame >= (float)s->at[k]) { giga.fired[k] = 1; if (s->event[k]) giga_event(s->event[k], px, py, pz); }
    if (giga.frame >= (float)(path_frames(s->path) - path_margin(s->path))) {
        ++giga.step;
        if (giga_route[giga.route][giga.step].path < 0) {
            /* The next route, by phase: under 2000 of 2800 the second, under 500 the third. */
            giga.route = giga.health < GIGA_HEALTH * 500.0f / 2800.0f ? 3 : giga.health < GIGA_HEALTH * 2000.0f / 2800.0f ? 2 : 1;
            giga.step = 0;
        }
        giga_begin_step();
        s = &giga_route[giga.route][giga.step];
    }
    path_at(s->path, giga.frame, p);
place:
    giga_record(p);
    memcpy(giga.pos[0], p, sizeof(p));
    for (k = 1; k < GIGA_PARTS; ++k) giga_behind(GIGA_SPACING * (float)k, giga.pos[k]);
    for (k = 0; k < GIGA_PARTS; ++k) {
        float ahead[3], dx, dy, dz, l;
        if (k == 0) { giga_behind(60.0f, ahead); dx = p[0] - ahead[0]; dy = p[1] - ahead[1]; dz = p[2] - ahead[2]; }
        else { dx = giga.pos[k - 1][0] - giga.pos[k][0]; dy = giga.pos[k - 1][1] - giga.pos[k][1]; dz = giga.pos[k - 1][2] - giga.pos[k][2]; }
        l = sqrtf(dx * dx + dy * dy + dz * dz);
        if (l > 1.0f) { giga.dir[k][0] = dx / l; giga.dir[k][1] = dy / l; giga.dir[k][2] = dz / l; }
    }
    /* In the walls: no walkable floor under it (the holes and the paths behind them are outside the room's
     * collision). The game's own flag (+0x78c4) isn't read. */
    for (k = 0; k < GIGA_PARTS; ++k) giga.out[k] = !stage_walkable_below(giga.pos[k][0] - giga.dir[k][0] * (k ? 300.0f : -200.0f),
                                                                          giga.pos[k][2] - giga.dir[k][2] * (k ? 300.0f : -200.0f));
    /* Belly beams (Shl05): straight down from the belly of a segment picked again every frame; they hit
     * every 10 frames, radius 100. */
    for (k = 0; k < GIGA_BEAMS; ++k) {
        float at[3], dx, dz;
        if (giga.beam[k].life <= 0.0f) continue;
        giga.beam[k].life -= f;
        giga.beam[k].part = 1 + (int)(ai_random() % (GIGA_PARTS - 1));
        if (giga.out[giga.beam[k].part]) continue;
        giga_belly(giga.beam[k].part, at);
        if (giga.beam[k].hit_wait > 0.0f) { giga.beam[k].hit_wait -= f; continue; }
        dx = px - at[0]; dz = pz - at[2];
        if (dx * dx + dz * dz < (100.0f + DANTE_RADIUS) * (100.0f + DANTE_RADIUS) && py < at[1]) {
            landed = 1; landed_x = at[0]; landed_z = at[2]; landed_heavy = 0; landed_damage = -1.0f;
            giga.beam[k].hit_wait = 10.0f;
        }
    }
}

/* Where a part stands for Dante's attacks and the lock-on: the point of its body nearest him. */
static void giga_part_update(float f, float px, float py, float pz) {
    int k = e->step < GIGA_PARTS ? e->step : 0;
    float back = GIGA_TOP_BACK, front = GIGA_TOP_FRONT, t;
    const float *b, *d;
    if (k == 0) { giga_update(f, px, py, pz); back = 0.0f; front = 450.0f; }   /* the head reaches forward */
    b = giga.pos[k]; d = giga.dir[k];
    t = (px - b[0]) * d[0] + (pz - b[2]) * d[2];
    if (t < back) t = back;
    if (t > front) t = front;
    e->pos[0] = b[0] + d[0] * t; e->pos[1] = b[1] + d[1] * t - 40.0f; e->pos[2] = b[2] + d[2] * t;
    e->floor_y = e->pos[1];
    e->plan = giga.dying ? P_DEATH : P_WATCH;
}

static void giga_hit(HitKind kind) {
    int k = e->step < GIGA_PARTS ? e->step : 0;
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    /* Ebony & Ivory chip it (a sixth of a sword hit, as with Cerberus) and don't crack a shell. */
    int pistol = kind == HIT_PISTOL;
    float damage = pistol ? 0.17f : (kind == HIT_LIGHT || kind == HIT_AIR) ? 1.0f : 2.0f;
    ++hits;
    fx_hit_spark(at);
    if (!mortal || giga.dying) return;
    /* The head takes the most (its block's 75 against the body's 30); a segment without its shell more than
     * one with. Shell: one off a hit; at 0 it shatters. */
    if (k == 0) damage *= 2.5f;
    else if (giga.shell[k] > 0) {
        if (!pistol && --giga.shell[k] == 0) { giga_note("shell_break", k, 0.0f); fx_explosion(at); fx_wall_spark(at); giga.regrow_at[k] = 0.0f; }
    } else damage *= 2.0f;
    giga.health -= damage;
    if (!pistol) giga_note("hit", k * 10 + giga.shell[k], damage);
    if (giga.health <= 0.0f) {
        giga.health = 0.0f;
        giga.dying = 1; giga.dead_time = 0.0f; giga.frame = 0.0f;
        giga_note("death", k, 0.0f);
        giga.state = GIGA_NORMAL; memset(giga.beam, 0, sizeof(giga.beam));
        giga_throw();
        died();
    }
}

static void giga_kill(void) {
    if (!giga.active || giga.dying) return;
    giga.health = 0.0f;
    giga.dying = 1; giga.dead_time = 0.0f; giga.frame = 0.0f;
    giga.state = GIGA_NORMAL; memset(giga.beam, 0, sizeof(giga.beam));
    giga_note("death", -1, 0.0f);
    giga_throw();
    died();
}

static void giga_draw_part(int k) {
    ScePspFMatrix4 m;
    float r[3], u[3];
    int i, bound = -1, head = k == 0;
    const Dmc3ModelObject *objects = head ? giga_head_objects : giga_body_objects;
    int count = head ? GIGA_HEAD_OBJECT_COUNT : GIGA_BODY_OBJECT_COUNT;
    giga_axes(k, r, u);
    m.x.x = r[0]; m.x.y = r[1]; m.x.z = r[2]; m.x.w = 0.0f;
    m.y.x = u[0]; m.y.y = u[1]; m.y.z = u[2]; m.y.w = 0.0f;
    m.z.x = giga.dir[k][0]; m.z.y = giga.dir[k][1]; m.z.z = giga.dir[k][2]; m.z.w = 0.0f;
    m.w.x = giga.pos[k][0]; m.w.y = giga.pos[k][1]; m.w.z = giga.pos[k][2]; m.w.w = 1.0f;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
    for (i = 0; i < count; ++i) {
        const Dmc3ModelObject *o = &objects[i];
        if (!head && giga_body_shell[i] && giga.shell[k] <= 0) continue;   /* the carapace is gone */
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
        if (giga.dying && giga.dead_time > 90.0f && ((int)giga.dead_time / 4) % 2) continue;   /* flickers out */
        if (giga.out[k] && (k == 0 || giga.out[k - 1]) && (k == GIGA_PARTS - 1 || giga.out[k + 1])) continue;   /* deep in a wall */
        giga_draw_part(k);
    }
    /* The belly beams: purple bolts to the floor. */
    for (k = 0; k < GIGA_BEAMS; ++k) {
        float top[3], bottom[3], g;
        if (giga.beam[k].life <= 0.0f || giga.out[giga.beam[k].part]) continue;
        giga_belly(giga.beam[k].part, top);
        bottom[0] = top[0]; bottom[2] = top[2];
        bottom[1] = player_floor_fn && player_floor_fn(top[0], top[1] - 50.0f, top[2], 0.0f, &g) ? g : top[1] - 3000.0f;
        fx_tracer(top, bottom, 0xffff70d0u, 60.0f);
        if (((int)giga.beam[k].life & 3) == 0) { float v[3] = {0.0f, 80.0f, 0.0f}; fx_mote(bottom, v, 120.0f, 20.0f, 0.2f, 0xffff90e0u); }
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

/* ---- Standing on it: the tops of upright segments are floor, and carry him. */
static float giga_top(int k, float x, float z, int *inside) {
    const float *b = giga.pos[k], *d = giga.dir[k];
    float h = sqrtf(d[0] * d[0] + d[2] * d[2]), fx, fz, dx = x - b[0], dz = z - b[2], along, side, r[3], u[3];
    *inside = 0;
    if (h < 0.5f) return 0.0f;
    giga_axes(k, r, u);
    if (u[1] < 0.8f) return 0.0f;   /* on a wall or the ceiling, or rolling */
    fx = d[0] / h; fz = d[2] / h;
    along = dx * fx + dz * fz; side = dx * fz - dz * fx;
    if (along < GIGA_TOP_BACK || along > GIGA_TOP_FRONT || fabsf(side) > GIGA_TOP_HALF) return 0.0f;
    *inside = 1;
    return b[1] + d[1] / h * along + giga_body_bounds[1][1];
}

int enemy_giga_floor(float x, float y, float z, float step, float *out) {
    int k, found = 0, in;
    if (!giga.active || giga.dying || giga.throw_time > 0.0f) return 0;
    for (k = 1; k < GIGA_PARTS; ++k) {
        float top = giga_top(k, x, z, &in);
        if (in && top <= y + step + 60.0f && (!found || top > *out)) { *out = top; found = 1; }
    }
    return found;
}

void enemy_giga_ride(float *x, float *y, float *z, int on_ground) {
    int k, in, best = -1;
    if (!giga.active || giga.dying) { giga.rider = -1; return; }
    if (on_ground) {
        for (k = 1; k < GIGA_PARTS; ++k) {
            float top = giga_top(k, *x, *z, &in);
            if (in && fabsf(top - *y) < 80.0f) { best = k; break; }
        }
    }
    giga.rider = best;
    giga.ride_frames = best >= 0 ? giga.ride_frames + 1 : 0;
    if (best < 0) return;
    *x += giga.pos[best][0] - giga.prev[best][0];
    *y += giga.pos[best][1] - giga.prev[best][1];
    *z += giga.pos[best][2] - giga.prev[best][2];
}

static int giga_part_out(int k) { return k >= 0 && k < GIGA_PARTS && giga.out[k]; }
static int enemy_giga_part(int k, float out[3]) {
    if (!giga.active || giga.dying || k < 0 || k >= GIGA_PARTS || giga.out[k]) return 0;
    out[0] = giga.pos[k][0] - giga.dir[k][0] * 250.0f; out[1] = giga.pos[k][1]; out[2] = giga.pos[k][2] - giga.dir[k][2] * 250.0f;
    return 1;
}
static void enemy_giga_struck(const float at[3], float damage) {
    int i;
    fx_explosion(at);
    if (!mortal || giga.dying || !giga.active) return;
    giga.health -= damage;
    giga_note("deflected", 0, damage);
    if (giga.health <= 0.0f) {
        for (i = 0; i < ENEMY_MAX; ++i) if (enemies[i].kind == ENEMY_GIGAPEDE && enemies[i].active && enemies[i].step == 0) { Enemy *was = e; e = &enemies[i]; giga_kill(); e = was; break; }
    }
}
void enemy_test_orb(float x, float y, float z, float yaw) {
    float at[3] = {x + sinf(yaw) * 700.0f, y + 250.0f, z + cosf(yaw) * 700.0f};
    giga_orb(at, 0, x, y, z);
}
/* Dante's sword meets its homing spheres: within reach in front of him, they're batted back (v x -0.5) to home on
 * the Gigapede. */
void enemy_deflect(float px, float py, float pz, float yaw, float reach) {
    int i;
    float s = sinf(yaw), c = cosf(yaw);
    for (i = 0; i < ARROW_MAX; ++i) {
        Arrow *a = &arrows[i];
        float dx, dz, d, facing;
        if (!a->active || a->type != 1) continue;
        dx = a->pos[0] - px; dz = a->pos[2] - pz; d = sqrtf(dx * dx + dz * dz);
        facing = d > 1.0f ? (dx * s + dz * c) / d : 1.0f;
        if (d < reach + 200.0f && facing > -0.3f && a->pos[1] > py - 50.0f && a->pos[1] < py + 350.0f) {
            a->type = 3; a->life = 300.0f;
            a->vel[0] *= -0.5f; a->vel[1] *= -0.5f; a->vel[2] *= -0.5f;
            giga_note("bat", i, 0.0f);
        }
    }
}
int enemy_giga_top_point(int k, float out[3]) {
    int in;
    if (!giga.active || giga.dying || k < 1 || k >= GIGA_PARTS || giga.out[k]) return 0;
    out[0] = giga.pos[k][0] - giga.dir[k][0] * 250.0f;
    out[2] = giga.pos[k][2] - giga.dir[k][2] * 250.0f;
    out[1] = giga_top(k, out[0], out[2], &in);
    return in;
}
int enemy_giga_focus(float out[3]) {
    int k, n = 0;
    out[0] = out[1] = out[2] = 0.0f;
    if (!giga.active) return 0;
    for (k = 0; k < GIGA_PARTS; ++k) {
        if (giga.out[k]) continue;
        out[0] += giga.pos[k][0]; out[1] += giga.pos[k][1]; out[2] += giga.pos[k][2]; ++n;
    }
    if (!n) return 0;
    out[0] /= (float)n; out[1] /= (float)n; out[2] /= (float)n;
    return 1;
}
float enemy_giga_health(void) { return giga.health; }
float enemy_giga_max_health(void) { return GIGA_HEALTH; }
