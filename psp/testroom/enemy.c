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

#include "cloth.h"
#include "motion.h"
#include "player.h"
#include "stage.h"
#include "vanguard.h"
#include "cerberus_boss.h"
#include "vergil_boss.h"
#include "geryon_boss.h"
#include "doppelganger_boss.h"
#include "arkham_boss.h"
#include "leviathan_boss.h"

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "swizzle.h"
#include "model_runtime.h"   /* Dmc3Vec3 / Dmc3Uv / Dmc3ModelObject / Dmc3Texture types */
#include "enemy_runtime.h"
#include "enemy_ai_runtime.h"
#include "agni_rudra_scripts.h"
#include "agni_rudra.h"
#include "nfx.h"
#include "beowulf_scripts.h"
#include "soul_eater_scripts.h"
#include "arachne_scripts.h"
#include "jester.h"

/* The real logic of CEm000 / CComEm000, the Seven Hell at this model: see docs/ENEMY_AI.md.
 * Three layers, as on the disc:
 *   plan    what it means to do. Most just run one action until it ends, then go back to deciding.
 *   action  a state with its own update: which motions, turning, jumps, reactions.
 *   script  the motion data's own list of what plays and when attacks are live.
 * Angles in the comments are the game's, 65536 to the circle; frames are 1/60 s. */
#define HL_MOTIONS 71
#define AB_MOTIONS 10   /* the Abyss's own, PAC [36] */
#define ANGLE(u) ((float)(u) * 6.2831853f / 65536.0f)
#define PI 3.1415927f
#define GRAVITY_STEP 0.54f

enum {  /* The actions this enemy uses, by the game's numbers. */
    A_FLINCH_FRONT = 0, A_FLINCH_BACK = 1, A_LAUNCHED = 2, A_LAUNCHED_BACK = 3, A_LAUNCH_UP = 7, A_AIR_HIT_HEAVY = 8, A_AIR_HIT = 12,
    A_NONE = 14, A_WALK = 15, A_DEATH = 26, A_SWING = 16, A_SWING_WIDE = 17, A_LEAP = 21,
    A_STEP_LEFT = 22, A_STEP_RIGHT = 23, A_STEP_BACK = 24, A_TAUNT_A = 58, A_TAUNT_B = 59,
    /* The Sloth's and the Gluttony's (build/hdref/enemy_specs.md): warps to 900, 600 and 300 cm from Dante,
     * the warp strike, the Gluttony's wind-up and its spew. */
    A_WARP_FAR = 41, A_WARP_MID = 42, A_WARP_NEAR = 43, A_WARP_STRIKE = 57, A_WIND_UP = 56, A_SPEW = 31,
    /* The Abyss's (its AI clone's handlers, vtable 0x612220): 35 a taunt (FUN_004b5470), 74 its sweep (FUN_004bafe0),
     * 75 its sink and rise (FUN_004bb2d0). */
    A_AB_TAUNT = 35, A_AB_SWEEP = 74, A_AB_SINK = 75
};
enum {  /* Its plans. PLAN_STAND is ours: the debug menu's dummies that don't attack. */
    P_FLINCH_FRONT = 0, P_FLINCH_BACK = 1, P_LAUNCHED = 2, P_LAUNCHED_BACK = 3, P_LAUNCH_UP = 7, P_AIR_HIT_HEAVY = 8, P_AIR_HIT = 9,
    P_DEATH = 20, P_DECIDE = 23, P_WATCH = 24, P_SWING = 27, P_SWING_WIDE = 28, P_LEAP = 32, P_STEP_LEFT = 33, P_STEP_RIGHT = 34,
    P_STEP_BACK = 35, P_TAUNT_A = 51, P_TAUNT_B = 52, PLAN_STAND = 200,
    P_SPEW = 37, P_SPEW_WOUND = 45, P_WARP_STRIKE = 46, P_WARP_FAR = 47, P_WARP_MID = 48, P_WARP_NEAR = 49,
    /* The Abyss's own picks (0x5ce910's rows): plans 40, 58, 59 run actions 35, 74, 75 (FUN_004abfc0, FUN_004ac980,
     * FUN_004aca20). */
    P_AB_TAUNT = 40, P_AB_SWEEP = 58, P_AB_SINK = 59
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
/* Bosses' health from their classes' parameter blocks (+0x2c, the value FUN_002f0560 starts the health with). */
#define NEVAN_MAX 7000
#define BEOWULF_MAX 10000
#define LADY_MAX 4000
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
    float tilt;                     /* about its own X after its yaw: the Arachne on a wall */
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
    short health;                   /* units of a light hit (42); bosses run past 127 */
    short script_at, stop;
    unsigned char flags[5];         /* the script player's flag set 0 (+0x5d), set by its op3s (FUN_00255640) */
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
    float ar_health;                /* Agni & Rudra: 7000 each; the Jester: 5000 */
    unsigned short ar_seen;         /* the last of Dante's moves he reacted to */
    unsigned char encounter;        /* the Jester: which of his three fights (+0x481e, the spawn record's op 8) */
    unsigned char j_second, j_pending, j_up;   /* the Jester's second set (+0x481d), its threshold crossed (+0x4d30),
                                                  up off the floor (+0x4818) */
    unsigned char j_hops; float j_g;   /* the Jester's hops left (+0x4d18) and their gravity (+0x4810) */
    unsigned char mirror_bound;     /* 405's Abyss (their spawn records' op 8 (1, 1)): only the mirrors kill them */
    /* Its parts' chains (Nevan's hair and dress): each extra bone's state, whether they hang yet, and the 60 fps
     * frames of game time not stepped yet. */
    ClothJoint *cloth;
    unsigned char cloth_ready;
    float cloth_frames;
    float veil;                     /* 0 opaque; above, drawn blended with alpha 1 - veil (Nevan's fades) */
    /* The Soul Eater (CEm016): its sight gauge (+0x4bc: 30 out of Dante's view, down while he looks), how shown it is
     * (+0x4c4, 0-30), its hover over him (+0x4b4) and when that's re-rolled (+0x4b8), its local velocity (+0x60), its
     * teleport's point (+0x3b0), its charge count (+0x448), whether a hit came while it couldn't react (+0x4d2). */
    float se_sight, se_show, se_lift, se_lift_wait, se_vel[3], se_to[3];
    signed char se_charges;
    unsigned char se_hit_pending;
} Enemy;

static float body_radius(const Enemy *a) { return a->kind == ENEMY_VANGUARD ? 50.0f : ENEMY_RADIUS; }
static int body_solid(const Enemy *a) {
    return a->active && !a->hidden && a->plan != P_DEATH &&
        (a->kind < ENEMY_KIND_COUNT || a->kind == ENEMY_VANGUARD);
}

static int hl_motion[HL_MOTIONS];
static Vertex *weapon_vertices[ENEMY_KIND_COUNT];
static unsigned short *indices[ENEMY_KIND_COUNT], *weapon_indices[ENEMY_KIND_COUNT];
static void *textures[ENEMY_TEXTURE_SET_COUNT][8];
/* Which bone holds the weapon, by kind. */
static const signed char kind_hand[ENEMY_KIND_COUNT] = {HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, HAND_BONE, ENIGMA_HAND_BONE, 0, 5, 7, 11, HAND_BONE};
static Enemy enemies[ENEMY_MAX];
static float torso_offset[ENEMY_MAX][3];
static int torso_kind(int kind) { return kind <= ENEMY_GREED || kind == ENEMY_ENVY || kind == ENEMY_ABYSS; }
static Enemy *e = &enemies[0];
static int hits, attacks_on;
static int landed;
static float landed_x, landed_z;
static int landed_heavy;
static float landed_damage = -1.0f;
static int landed_kind = -1;   /* the attack record's kind (+0x18), -1 unknown */
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
    case ENEMY_ABYSS: return &enemy_ai_classes[AI_CLASS_ABYSS];
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
static void scrolls_update(float frames);
static float enemy_wind[2];   /* pushes Dante this frame (enemy_push_out) */
/* A hold on Dante (enemy.h): taken this frame, its need, how it ended, where from, the health drained, his escape. */
static int hold_new, hold_need, hold_end, hold_broken;
static Enemy *hold_owner;   /* who holds him */
static float hold_from[2], hold_drain;
static void giga_note(const char *what, int a, float b);
/* Dante's motion while held, from the holder's pack (FUN_0021f2e0's bank, FUN_0021f3a0's index): Nevan's kiss 0x14
 * (her [3] slot 20, 003b8810), the Soul Eater's held loop 0xc (its [9] slot 12, 0049dac0 state 4). The Arachne's
 * (its [15], 003f8080) isn't wired. */
static const char *hold_motion;
static void hold_take(int need) {
    hold_new = 1; hold_need = need; hold_broken = 0; hold_owner = e;
    hold_motion = e->kind == ENEMY_NEVAN ? "nvh-20" : e->kind == ENEMY_SOUL ? "seh-12" : 0;
    giga_note("hold", need, (float)e->kind);
}
const char *enemy_hold_motion(void) { return hold_motion; }
static void hold_let_go(int how) { if (hold_owner == e) { hold_end = how; hold_from[0] = e->pos[0]; hold_from[1] = e->pos[2]; hold_owner = 0; } }
static int hold_freed(void) { int r = hold_owner == e && hold_broken; if (r) { hold_broken = 0; hold_owner = 0; } return r; }
int enemy_hold_take(int *need) { if (!hold_new) return 0; hold_new = 0; *need = hold_need; return 1; }
float enemy_take_hold_drain(void) { float d = hold_drain; hold_drain = 0.0f; return d; }
int enemy_hold_end(float *from_x, float *from_z) {
    int r;
    if (hold_owner && !hold_owner->active) { hold_owner = 0; hold_end = 1; }   /* its holder's gone */
    r = hold_end; hold_end = 0; *from_x = hold_from[0]; *from_z = hold_from[1]; return r; }
void enemy_hold_broken(void) { hold_broken = 1; }

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
void enemy_test_note(const char *what, int a, float b) { giga_note(what, a, b); }
static int goyle_bullet(const float from[3], const float to[3]);
static int enemy_giga_part(int k, float out[3]);
static void enemy_giga_struck(const float at[3], float damage);
#define GIGA_PARTS_PUBLIC 8
static int jester_init(void);
static void jester_spawn(void);
static void jester_update(float f, float px, float py, float pz);
static void jester_hit(HitKind kind);
static JesterProjectiles jester_world;   /* his emitters and what they throw (jester.c) */
static void bishop_spawn_glue(void);
static void bishop_update_glue(float f,float px,float py,float pz);
static void bishop_hit_glue(HitKind kind,float from_x,float from_z);
static void dullahan_spawn_glue(void);
static void fallen_spawn_glue(void);
static void fallen_update_glue(float f, float px, float py, float pz);
static void fallen_hit_glue(HitKind kind, float from_x, float from_z);
static void dullahan_update_glue(float f,float px,float py,float pz);
static void dullahan_hit_glue(HitKind kind,float from_x,float from_z);
static void arachne_spawn_glue(void);
static void nevan_spawn_glue(void);
static void beowulf_spawn_glue(void);
static void lady_spawn_glue(void);
static void lady_update_glue(float f, float px, float py, float pz);
static void lady_hit_glue(HitKind kind);
static void beowulf_update_glue(float f, float px, float py, float pz);
static void beowulf_hit_glue(HitKind kind, float from_x, float from_z);
static void soul_spawn_glue(void);
static void soul_update_glue(float f, float px, float py, float pz);
static void soul_hit_glue(HitKind kind);
static void nevan_update_glue(float f, float px, float py, float pz);
static void nevan_hit_glue(HitKind kind, float from_x, float from_z);
static void arachne_update_glue(float f, float px, float py, float pz);
static void arachne_hit_glue(HitKind kind, float from_x, float from_z);
static int pawn_init(void);
static void greed_tick(int slot, float f, float px, float pz);
static void coffins_update(float f);
static void coffins_reset(void);
static void pawn_spawn(void);
static void pawn_update(float f, float px, float py, float pz);
static void pawn_hit(HitKind kind);
static int ar_init(void);
static void ar_rudra_spawn(void);
static void ar_spawn(void);
static void ar_update(float f, float px, float py, float pz);
static void ar_hit(HitKind kind, float hit_from_x, float hit_from_z);
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
static void giga_clear(void);
static void giga_part_update(float f, float px, float py, float pz);
static void giga_hit(HitKind kind);
static void giga_draw(void);
float enemy_giga_health(void);
static int giga_part_out(int k);
static int giga_part_crossing(int k, const float centre[3]);
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
    if (!geryon_boss_init()) return 0;
    if (!doppelganger_boss_init()) return 0;
    if (!arkham_boss_init()) return 0;
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

static void body_wall(float *x, float *z, float y, float radius) {
    int k;
    /* The stage query has a bounded triangle pass. At corners, another plane
     * can reintroduce a contact already visited in that pass. */
    for (k = 0; k < 4; ++k) {
        float ox = *x, oz = *z;
        stage_collide(x, z, y, radius);
        if (fabsf(*x-ox)+fabsf(*z-oz) < 0.01f) break;
    }
}

/* Repair only invalid EST placements. Search locally, rather than creating a
 * floor at zero when a prop or a collision hole covers the authored point. */
static void spawn_site(float *x, float *z) {
    float ground, cx = *x, cz = *z;
    int ring, k;
    if (!stage_loaded()) return;
    if (floor_under(cx, spawn_search_y, cz, &ground)) {
        body_wall(&cx, &cz, ground, ENEMY_RADIUS);
        if (floor_under(cx, spawn_search_y, cz, &ground)) { *x = cx; *z = cz; return; }
    }
    for (ring = 1; ring <= 16; ++ring) {
        for (k = 0; k < 32; ++k) {
            float a = (float)k * (2*PI/32);
            cx = *x+sinf(a)*(float)(ring*64); cz = *z+cosf(a)*(float)(ring*64);
            if (!floor_under(cx, spawn_search_y, cz, &ground)) continue;
            body_wall(&cx, &cz, ground, ENEMY_RADIUS);
            if (!floor_under(cx, spawn_search_y, cz, &ground)) continue;
            *x = cx; *z = cz;
            return;
        }
    }
    /* Leave genuinely unsupported stage data visible to the audit. */
}

static void script_play(int n);

/* How much each takes, in units of a light hit. Estimates: the game's health and damage values aren't
 * read yet. On the HD Collection a maxed Dante kills a Pride in about three to five hits on Easy. */
static int kind_health(int kind) {
    /* In proportion to the parameter blocks' health (inferred): Pride 250, Gluttony 700, Sloth 1200,
     * Enigma 600 (build/hdref/enemy_specs.md). */
    switch (kind) {
    case ENEMY_GLUTTONY: return ENEMY_HEALTH * 700 / 250;
    case ENEMY_LUST: return ENEMY_HEALTH * 750 / 250;    /* 0x5ac100 +0x2c (class 2, FUN_002c5d90) */
    case ENEMY_GREED: return ENEMY_HEALTH * 1000 / 250;  /* 0x5b0080 +0x2c (class 5, FUN_00356538) */
    case ENEMY_WRATH: return ENEMY_HEALTH * 500 / 250;   /* 0x5b10f0 +0x2c (class 4, FUN_00354560) */
    case ENEMY_DULLAHAN: return ENEMY_HEALTH * 300 / 250;   /* 0x5ccad0 +0x2c (class 12, FUN_003cc200) */
    case ENEMY_SLOTH: return ENEMY_HEALTH * 1200 / 250;
    case ENEMY_ENVY: return ENEMY_HEALTH * 400 / 250;
    case ENEMY_ABYSS: return ENEMY_HEALTH * 1500 / 250;   /* its block 0x5ce060 +0x2c (0x61a678 +4) */
    case ENEMY_ENIGMA: return ENEMY_HEALTH * 600 / 250;
    case ENEMY_GOYLE: return ENEMY_HEALTH * 600 / 250;   /* 0x5bf4c0 +0x2c (class 14, FUN_0039c608) */
    case ENEMY_JESTER: return ENEMY_HEALTH * 1500 / 250;
    case ENEMY_ARACHNE: return ENEMY_HEALTH * 1400 / 250;  /* its parameter block 0x5c3870 +0x2c (FUN_003f2f10 copies it) */
    case ENEMY_NEVAN: return ENEMY_HEALTH * NEVAN_MAX / 250;   /* 0x5bc300 +0x2c (class 31, FUN_003b356c) */
    case ENEMY_PAWN: return ENEMY_HEALTH * 300 / 250;   /* pawn.inc's PAWN_HEALTH */
    case ENEMY_FALLEN: return ENEMY_HEALTH * 1300 / 250;   /* 0x5c32a0 +0x2c (class 11, copied by FUN_00342450) */
    case ENEMY_BEOWULF: return ENEMY_HEALTH * BEOWULF_MAX / 250;   /* 0x5b84e0 +0x2c (class 33, FUN_002d95a4) */
    case ENEMY_SOUL: return (ENEMY_HEALTH * 200 + 125) / 250;   /* 0x5c1440 +0x2c (class 16, FUN_0049ad90) */
    case ENEMY_LADY: return ENEMY_HEALTH * LADY_MAX / 250;   /* 0x5cbe40 +0x2c (class 37, FUN_00444a8c) */
    case ENEMY_AGNI: case ENEMY_RUDRA: return ENEMY_HEALTH * 1200 / 250;
    default: return ENEMY_HEALTH;
    }
}


static Enemy *last_spawned;   /* for the room script's values (enemy_set_spawn_param) */
static void spawn_one(int kind, float x, float z) {
    const EnemyModel *m;
    int i;
    last_spawned = e;
    e->mirror_bound = 0;
    if (hold_owner == e) hold_let_go(1);   /* a slot taken again lets go of him */
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
    if (kind == ENEMY_DOPPELGANGER) {
        float y = 0.0f;
        if (!floor_under(x, spawn_search_y, z, &y)) y = 0.0f;
        e->kind = kind; e->active = 1; e->plan = P_WATCH; e->hidden = 0;
        e->pos[0] = x; e->pos[1] = e->floor_y = y; e->pos[2] = z;
        doppelganger_boss_set_floor(vergil_floor);
        doppelganger_boss_spawn(x, y, z);
        return;
    }
    if (kind == ENEMY_ARKHAM) {
        float y = 0.0f;
        if (!floor_under(x, spawn_search_y, z, &y)) y = 0.0f;
        e->kind = kind; e->active = 1; e->plan = P_WATCH; e->hidden = 0;
        e->pos[0] = x; e->pos[1] = e->floor_y = y; e->pos[2] = z;
        arkham_boss_set_floor(vergil_floor);
        arkham_boss_spawn(x, y, z);
        return;
    }
    if (kind == ENEMY_GERYON) {
        e->kind = kind; e->active = 1; e->plan = P_WATCH; e->hidden = 0;
        geryon_boss_set_floor(vergil_floor);
        geryon_boss_spawn(x, 0.0f, z);
        geryon_boss_position(e->pos);
        e->floor_y = e->pos[1];
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
        vanguard_select((int)(e-enemies));vanguard_spawn(x,y,z);return;
    }
    if (kind < 0 || kind >= ENEMY_KIND_COUNT) kind = ENEMY_PRIDE;
    spawn_site(&x, &z);
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
    e->posed_frame = -1.0f; /* Force a bind pose even when this kind has no idle clip. */
    e->overlay_motion = -1;
    e->hidden = 0;
    e->cloth_ready = 0;
    e->cloth_frames = 0.0f;
    e->veil = 0.0f;
    e->active = 1;
    if (kind == ENEMY_ENIGMA) { enigma_spawn(); return; }
    if (kind == ENEMY_GOYLE) { goyle_spawn(); return; }
    if (kind == ENEMY_JESTER) { jester_spawn(); return; }
    if (kind == ENEMY_BISHOP) {bishop_spawn_glue();return;}
    if (kind == ENEMY_DULLAHAN) { dullahan_spawn_glue(); return; }
    if (kind == ENEMY_FALLEN) { fallen_spawn_glue(); return; }
    if (kind == ENEMY_ARACHNE) { arachne_spawn_glue(); return; }
    if (kind == ENEMY_NEVAN) { nevan_spawn_glue(); return; }
    if (kind == ENEMY_PAWN) { pawn_spawn(); return; }
    if (kind == ENEMY_BEOWULF) { beowulf_spawn_glue(); return; }
    if (kind == ENEMY_SOUL) { soul_spawn_glue(); return; }
    if (kind == ENEMY_LADY) { lady_spawn_glue(); return; }
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

/* A spawn record's op 8 (index, value), for the enemy it just made: the Jester's encounter (+0x481e: health
 * 5000 / 6500 / 10000, 0x5e2f60). */
void enemy_set_spawn_param(int index, int value) {
    Enemy *a = last_spawned;
    if (a && a->active && index == 1 && a->kind == ENEMY_ABYSS) { a->mirror_bound = (unsigned char)(value != 0); return; }
    if (!a || !a->active || index != 0 || a->kind != ENEMY_JESTER || value < 0 || value > 2) return;
    a->encounter = (unsigned char)value;
    a->ar_health = jester_max_health(value);
}

int enemy_add(float x, float z) { return enemy_add_kind(ENEMY_PRIDE, x, z); }

/* An EST class that spawns as another kind's stand-in keeps its own decoded health (its parameter block +0x2c):
 * the Abyss (EST 24-27, class 6, 0x5ce060 via FUN_0041d748) 1500; the Soul Eater (EST 34, class 16, 0x5c1440 via
 * FUN_0049b3a8) 200. Others: the kind's own. */
int enemy_add_class(int spawn_class, float x, float z) {
    Enemy *selected = e;
    int i, kind = enemy_kind_for_class(spawn_class), hp = 0;
    if (spawn_class >= 24 && spawn_class <= 27 && kind == ENEMY_PRIDE) hp = 1500;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active) {
            e = &enemies[i];
            spawn_one(kind, x, z);
            if (hp && e->active) e->health = (short)(ENEMY_HEALTH * hp / 250);
            e = selected;
            return 1;
        }
    }
    return 0;
}

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

/* Secret Mission 9 (secret_missions.c): those in or by a wagon's box ride it, carried dx along and kept on its deck. */
void enemy_ride(float x0, float x1, float z0, float z1, float dx, float deck_y) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        Enemy *r = &enemies[i];
        if (!r->active || r->pos[0] < x0 - 300.0f || r->pos[0] > x1 + 300.0f || r->pos[2] < z0 - 300.0f || r->pos[2] > z1 + 300.0f) continue;
        r->pos[0] += dx;
        if (r->pos[0] < x0) r->pos[0] = x0;
        if (r->pos[0] > x1) r->pos[0] = x1;
        if (r->pos[2] < z0) r->pos[2] = z0;
        if (r->pos[2] > z1) r->pos[2] = z1;
        r->pos[1] = deck_y;
    }
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

int enemy_alive_positions(float out[][3], int max) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX && n < max; ++i)
        if (enemies[i].active && enemies[i].plan != P_DEATH) { out[n][0] = enemies[i].pos[0]; out[n][1] = enemies[i].pos[1]; out[n][2] = enemies[i].pos[2]; ++n; }
    return n;
}

/* For tests: only the first `keep` living ones stay; the rest are removed outright. */
void enemy_trim(int keep) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].plan != P_DEATH && keep-- <= 0) enemies[i].active = 0;
}
static void plan_set(int p);
static void died(void);
/* 405's mirror set (CStageSetMirror): a blow on its lit mirror throws every enemy in the room (FUN_0035a610, vtable
 * +0xe4); with two broken, it kills them (FUN_0035a720, +0xd8, each frame). */
void enemy_mirror_blow(float from_x, float from_z) {
    Enemy *was = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active || enemies[i].plan == P_DEATH) continue;
        e = &enemies[i];
        enemy_hit(HIT_HEAVY, from_x, from_z);
    }
    e = was;
}
void enemy_mirror_kill(void) {
    Enemy *was = e;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active || enemies[i].plan == P_DEATH || !enemies[i].mirror_bound) continue;
        e = &enemies[i];
        e->health = 0; e->air = 0;
        plan_set(P_DEATH);
        died();
    }
    e = was;
}

void enemy_kill_all(void) {
    int i;
    coffins_reset();
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].active && enemies[i].plan != P_DEATH) {
            if (enemies[i].kind == ENEMY_VANGUARD) { vanguard_select(i); vanguard_kill(); }
            if (enemies[i].kind == ENEMY_CERBERUS) cerberus_boss_kill();
            if (enemies[i].kind == ENEMY_VERGIL) vergil_boss_kill();
            if (enemies[i].kind == ENEMY_GERYON) geryon_boss_kill();
            if (enemies[i].kind == ENEMY_DOPPELGANGER) doppelganger_boss_kill();
            if (enemies[i].kind == ENEMY_ARKHAM) arkham_boss_kill();
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
        e=&enemies[i];e->plan=P_DEATH;e->plan_step=0;e->air=0;died();e->active=0;
    }
    e=selected;
}

void enemy_set_count(int count, float x, float z) {
    int i;
    if (count == 0) for (i = 0; i < ARROW_MAX_PUBLIC; ++i) arrows_clear(i);
    if (count == 0) { coffins_reset(); giga_clear(); }
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
        if (o->active && o->plan != P_DEATH && !(o->kind == ENEMY_VANGUARD && o->hidden) &&
            !(o->kind == ENEMY_SOUL && o->veil > 0.97f) &&   /* unseen, it can't be locked on (0x80000000) */
            !(o->kind == ENEMY_GIGAPEDE && giga_part_out(o->step)) &&
            !(o->kind == ENEMY_CERBERUS && cerberus_boss_dying()) &&
            !(o->kind == ENEMY_VERGIL && (vergil_boss_dying() || vergil_boss_hidden())) &&
            !(o->kind == ENEMY_GERYON && (geryon_boss_dying() || geryon_boss_hidden())) &&
            !(o->kind == ENEMY_DOPPELGANGER && (doppelganger_boss_dying() || doppelganger_boss_hidden())) &&
            !(o->kind == ENEMY_ARKHAM && arkham_boss_dying()) && d < best) { best = d; e = &enemies[i]; }
    }
    if (e->kind == ENEMY_CERBERUS) cerberus_boss_select_target(x, z);
    if (e->kind == ENEMY_LEVIATHAN) {
        leviathan_boss_select_target(x,z);
        leviathan_boss_target_position(e->pos);
    }
}

void enemy_set_attacks(int attacks) { attacks_on = attacks; }
void enemy_set_dummy_kind(int kind) { dummy_kind = kind == ENEMY_VANGUARD || kind == ENEMY_VERGIL || kind == ENEMY_GERYON || kind == ENEMY_DOPPELGANGER || kind == ENEMY_ARKHAM || kind == ENEMY_LEVIATHAN || (kind >= 0 && kind < ENEMY_KIND_COUNT) ? kind : ENEMY_PRIDE; }
int enemy_kind(void) { return e->kind; }

int enemy_kind_for_class(int spawn_class) {
    /* EST spawn ids: a Seven Hell is 4 x its class number (0 Pride, 4 Gluttony, 8 Lust, 12 Sloth,
     * 16 Wrath, 20 Greed), and 31 is the Enigma. Anything not ported yet stands in as a Pride. */
    if (spawn_class >= 0 && spawn_class < 24) return spawn_class / 4;
    if (spawn_class == 37)return ENEMY_BISHOP;
    if (spawn_class == 30) return ENEMY_DULLAHAN;
    if (spawn_class == 29) return ENEMY_FALLEN;
    if (spawn_class == 31) return ENEMY_ENIGMA;
    if (spawn_class == 32) return ENEMY_GOYLE;
    if (spawn_class == 62) return ENEMY_JESTER;
    if (spawn_class == 35) return ENEMY_PAWN;
    if (spawn_class >= 36 && spawn_class <= 38) return ENEMY_PAWN;   /* the other chessmen (Missions 14, 16): the Pawn stands in */   /* the Damned Pawn; 36-38 (the other chessmen) aren't in */
    if (spawn_class == 45) return ENEMY_AGNI;
    if (spawn_class == 41) return ENEMY_GIGAPEDE;
    if (spawn_class == 43) return ENEMY_CERBERUS;
    if (spawn_class == 58) return ENEMY_VERGIL;
    if (spawn_class == 50) return ENEMY_GERYON;
    if (spawn_class == 52) return ENEMY_DOPPELGANGER;
    if (spawn_class == 53) return ENEMY_ARKHAM;
    if (spawn_class == 26) return ENEMY_VANGUARD;
    if (spawn_class == 25) return ENEMY_ENVY;
    if (spawn_class == 24) return ENEMY_ABYSS;
    if (spawn_class == 28) return ENEMY_ARACHNE;
    if (spawn_class == 34) return ENEMY_SOUL;
    if (spawn_class == 49) return ENEMY_NEVAN;
    if (spawn_class == 51) return ENEMY_BEOWULF;
    if (spawn_class == 55) return ENEMY_LADY;
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

int enemy_active(void) { if (e->kind == ENEMY_VANGUARD) vanguard_select((int)(e-enemies)); return e->active && e->plan != P_DEATH && !(e->kind == ENEMY_GIGAPEDE && giga_part_out(e->step)) && !(e->kind == ENEMY_VANGUARD && vanguard_hidden()) && !(e->kind == ENEMY_CERBERUS && cerberus_boss_dying()) && !(e->kind == ENEMY_VERGIL && (vergil_boss_dying() || vergil_boss_hidden())) && !(e->kind == ENEMY_GERYON && (geryon_boss_dying() || geryon_boss_hidden())) && !(e->kind == ENEMY_DOPPELGANGER && (doppelganger_boss_dying() || doppelganger_boss_hidden())) && !(e->kind == ENEMY_ARKHAM && arkham_boss_dying()); }
void enemy_position(float out[3]) {
    if (e->kind == ENEMY_LEVIATHAN) { leviathan_boss_target_position(out); return; }
    if (e->kind == ENEMY_CERBERUS) cerberus_boss_target_position(out);
    else if (e->kind == ENEMY_VERGIL) vergil_boss_position(out);
    else if (e->kind == ENEMY_GERYON) geryon_boss_target_position(out);
    else if (e->kind == ENEMY_DOPPELGANGER) doppelganger_boss_target_position(out);
    else if (e->kind == ENEMY_ARKHAM) arkham_boss_target_position(out);
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
        if (enemies[i].active && enemies[i].kind == ENEMY_GERYON) return "GERYON";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_DOPPELGANGER) return "DOPPELGANGER";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_ARKHAM) return "ARKHAM";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_GIGAPEDE) return "GIGAPEDE";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_JESTER) return "JESTER";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_NEVAN) return "NEVAN";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_BEOWULF) return "BEOWULF";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == ENEMY_LADY) return "LADY";
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && (enemies[i].kind == ENEMY_AGNI || enemies[i].kind == ENEMY_RUDRA)) return "AGNI & RUDRA";
    return "HELL VANGUARD";
}
int enemy_boss_health(float *health, float *maximum) {
    int lv;
    for(lv=0;lv<ENEMY_MAX;++lv) if(enemies[lv].active && enemies[lv].plan!=P_DEATH && enemies[lv].kind==ENEMY_LEVIATHAN) {
        *health=leviathan_boss_health();*maximum=leviathan_boss_max_health();return 1;
    }
    int i;
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_VANGUARD) {
        vanguard_select(i);*health=vanguard_health();*maximum=vanguard_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_CERBERUS && !cerberus_boss_dying()) {
        *health=cerberus_boss_health();*maximum=cerberus_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_VERGIL) {
        *health=vergil_boss_health();*maximum=vergil_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_GERYON) {
        *health=geryon_boss_health();*maximum=geryon_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_DOPPELGANGER) {
        *health=doppelganger_boss_health();*maximum=doppelganger_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_ARKHAM) {
        *health=arkham_boss_health();*maximum=arkham_boss_max_health();return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && (enemies[i].kind==ENEMY_AGNI || enemies[i].kind==ENEMY_RUDRA)) {
        *health=ar_health_total(maximum);return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_JESTER) {
        *health=enemies[i].ar_health;*maximum=jester_max_health(enemies[i].encounter);return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_LADY) {
        *health=enemies[i].health;*maximum=ENEMY_HEALTH * LADY_MAX / 250;return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_BEOWULF) {
        *health=enemies[i].health;*maximum=ENEMY_HEALTH * BEOWULF_MAX / 250;return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_NEVAN) {
        *health=enemies[i].health;*maximum=ENEMY_HEALTH * NEVAN_MAX / 250;return 1;
    }
    for(i=0;i<ENEMY_MAX;++i)if(enemies[i].active && enemies[i].plan!=P_DEATH && enemies[i].kind==ENEMY_GIGAPEDE) {
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

static int is_ar(void) { return e->kind == ENEMY_AGNI || e->kind == ENEMY_RUDRA; }
/* A boss with its own script bank (its pack's scripts, caps and attack records, and its own motions): Agni & Rudra
 * (tools/prepare_agni_rudra.py), Beowulf (tools/prepare_beowulf.py). The Seven Hells' kinds return 0. */
typedef struct { const EnemyAiOp *ops; const short *starts; int scripts; const EnemyAiCap *caps; int caps_count;
                 const ArAttack *attacks; int attack_count; const int *motions; int motion_count; } ScriptBank;
static int ar_motion_map[AR_SCRIPTS];
#define BW_MOTIONS 43
static int bw_motion_map[BW_MOTIONS];
#define SE_MOTIONS 14
static int se_motion_map[SE_MOTIONS];
#define AN_MOTION_IDS 312   /* the Arachne's script motion ids, g * 100 + n */
static int an_motion_map[AN_MOTION_IDS];
static const ScriptBank *script_bank(void) {
    static const ScriptBank agni = {ar_ops_0, ar_starts_0, AR_SCRIPTS, ar_caps, AR_CAPS, ar_attacks, 22, ar_motion_map, AR_SCRIPTS};
    static const ScriptBank rudra = {ar_ops_1, ar_starts_1, AR_SCRIPTS, ar_caps, AR_CAPS, ar_attacks, 22, ar_motion_map, AR_SCRIPTS};
    static const ScriptBank beowulf = {bw_ops, bw_starts, BW_SCRIPTS, bw_caps, BW_CAPS, bw_attacks, BW_ATTACKS, bw_motion_map, BW_MOTIONS};
    static const ScriptBank soul = {se_ops, se_starts, SE_SCRIPTS, se_caps, SE_CAPS, se_attacks, SE_ATTACKS, se_motion_map, SE_MOTIONS};
    static const ScriptBank arachne = {an_ops, an_starts, AN_SCRIPTS, an_caps, AN_CAPS, an_attacks, AN_ATTACKS, an_motion_map, AN_MOTION_IDS};
    return e->kind == ENEMY_AGNI ? &agni : e->kind == ENEMY_RUDRA ? &rudra : e->kind == ENEMY_BEOWULF ? &beowulf :
           e->kind == ENEMY_SOUL ? &soul : e->kind == ENEMY_ARACHNE ? &arachne : 0;
}
static int script_event;   /* the last script event the runner met this frame, + 1 (0: none) */
static const EnemyAiOp *script_ops(void) { const ScriptBank *k = script_bank(); return k ? k->ops : e->kind == ENEMY_ENVY ? envy_ai_ops : e->kind == ENEMY_ABYSS ? abyss_ai_ops : enemy_ai_ops; }

static void script_play_op(const EnemyAiOp *op) {
    int m, blend = op->c & 0xff;
    if (script_bank()) {
        const ScriptBank *k = script_bank();
        m = op->a < k->motion_count ? k->motions[op->a] : -1;
        if (m < 0) return;   /* an empty or unexported motion slot */
    } else {
        if (e->kind == ENEMY_ABYSS && op->a >= 100 && op->a < 100 + AB_MOTIONS) {
            /* The Abyss's own motions, its scripts' ids 100+ (PAC [36]: "abNN"). */
            static int ab_motion[AB_MOTIONS] = {-2};
            if (ab_motion[0] == -2) {
                int k;
                for (k = 0; k < AB_MOTIONS; ++k) {
                    char name[5] = {'a', 'b', (char)('0' + k / 10), (char)('0' + k % 10), 0};
                    ab_motion[k] = motion_find(name);
                }
            }
            m = ab_motion[op->a - 100];
            if (m < 0) return;
        } else {
            if (op->a >= HL_MOTIONS) return;  /* a motion from a pack the port doesn't export */
            m = hl_motion[op->a];
        }
    }
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
        const EnemyAiOp *op = &script_ops()[e->script_at++];
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
        case 4:   /* a script event; op3 copies its 5 bytes into flag set 0 (FUN_00255640) */
            script_event = op->a + 1;
            if ((op->c & 0xF) == 3) {
                e->flags[0] = (unsigned char)op->a; e->flags[1] = (unsigned char)op->b;
                e->flags[2] = (unsigned char)((op->c >> 4) & 0xF); e->flags[3] = (unsigned char)((op->c >> 8) & 0xF);
                e->flags[4] = (unsigned char)((op->c >> 12) & 0xF);
            }
            break;
        default: e->script_at = op->b; break;
        }
    }
}

static void script_play(int n) {
    e->hit_left[0] = e->hit_left[1] = 0.0f;
    e->done = 0;
    memset(e->flags, 0, sizeof e->flags);
    {
        const ScriptBank *k = script_bank();
        int start = k ? (n >= 0 && n < k->scripts ? k->starts[n] : -1) : (e->kind == ENEMY_ENVY ? envy_ai_scripts : e->kind == ENEMY_ABYSS ? abyss_ai_scripts : enemy_ai_scripts)[n];
        if (start < 0) { e->done = 1; return; }
        e->script_at = start;
    }
    script_run();
}

/* The second list a state can name motions through; the game's PTR_DAT_00618c38. */
static void script_play_hit(int n) { script_play((e->kind==ENEMY_ENVY?envy_ai_hit_scripts:e->kind==ENEMY_ABYSS?abyss_ai_hit_scripts:enemy_ai_hit_scripts)[n]); }

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
    case A_AB_TAUNT: script_play(38 + (int)(ai_random() & 1u)); break;   /* FUN_004b5470: 38 or 39 at random */
    case A_AB_SWEEP: script_play(102); break;                            /* FUN_004bafe0 */
    case A_AB_SINK: script_play(101); e->timer = 0.0f; break;            /* FUN_004bb2d0 */
    case A_SPEW: script_play(52); e->timer = 0.0f; break;        /* hl-52 */
    default: break;
    }
    if (a != A_WARP_FAR && a != A_WARP_MID && a != A_WARP_NEAR && a != A_WARP_STRIKE) e->hidden = 0;
}

/* The Sloth reappears this far from Dante at a random angle, on the floor under him, facing him. */
static void warp_to(float px, float pz, float r) {
    float a = (float)(ai_random() & 0xffffu) * (2.0f * PI / 65536.0f), g;
    int k;
    /* Keep the native radius and first random heading. Invalid headings must not
     * materialize outside Love Planet (or across a wall on another storey).
     * Like Vanguard's placement adaptation, try the remaining headings before
     * retaining the last valid position. Never invent floor at Dante's height. */
    for (k = 0; k < 32; ++k) {
        float angle = a + (float)k * (2.0f * PI / 32.0f);
        float x = px + sinf(angle) * r, z = pz + cosf(angle) * r;
        float cx = x, cz = z, t;
        float from[3] = {px, dante_y + 100.0f, pz}, to[3];
        if (!floor_under(x, dante_y + 200.0f, z, &g) || fabsf(g-dante_y) > 400.0f) continue;
        if (stage_loaded()) {
            stage_collide(&cx, &cz, g, ENEMY_RADIUS);
            if (hypotf(cx-x, cz-z) > 0.5f) continue;
            to[0] = x; to[1] = g + 100.0f; to[2] = z;
            if (stage_raycast(from, to, &t) && t < 0.99f) continue;
        }
        e->pos[0] = x; e->pos[1] = e->floor_y = g; e->pos[2] = z;
        break;
    }
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
    case A_AB_TAUNT:
        /* FUN_004b5570: turning to him at most 0x100 a frame. */
        turn(px, pz, 0x100, f);
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_AB_SWEEP:
        /* FUN_004bb030: turning 2 degrees a frame over its motion's frames 0-30 (0x5a835c-0x5a8364). */
        if (e->frame <= 30.0f) turn(px, pz, 364, f);
        if (e->done) action_enter(A_NONE);
        return 1;
    case A_AB_SINK:
        /* FUN_004bb3a0: its sink (101) played out, it's under, 300 cm from him at a random angle, facing him; 50
         * frames later it rises there (100). [Hidden while under: the game doesn't hide it, it's below the floor.] */
        if (e->step == 0) {
            if (e->done) { warp_to(px, pz, 300.0f); e->hidden = 1; e->timer = 0.0f; e->step = 1; }
        } else if (e->step == 1) {
            e->timer += f;
            if (e->timer >= 50.0f) { e->hidden = 0; script_play(100); e->step = 2; }
        } else if (e->done) action_enter(A_NONE);
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

static void plan_set(int p) {
    if (e->kind == ENEMY_ABYSS && p != e->plan && p != P_DECIDE && p != P_WATCH) giga_note("abyss", p, (float)(e - enemies));   /* for tests */
    e->plan = (unsigned char)p; e->plan_step = 0;
}

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
    case P_AB_TAUNT: plan_run_action(A_AB_TAUNT); break;
    case P_AB_SWEEP: plan_run_action(A_AB_SWEEP); break;
    case P_AB_SINK: plan_run_action(A_AB_SINK); break;
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
    if (e->kind == ENEMY_VANGUARD) vanguard_select((int)(e-enemies));
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
    if (e->kind == ENEMY_GERYON) {
        ++hits;
        if (geryon_boss_hit(kind, from_x, from_z, mortal)) { e->plan = P_DEATH; died(); }
        return;
    }
    if (e->kind == ENEMY_DOPPELGANGER) {
        ++hits;
        if (doppelganger_boss_hit(kind, from_x, from_z, mortal)) { e->plan = P_DEATH; died(); }
        return;
    }
    if (e->kind == ENEMY_ARKHAM) {
        ++hits;
        if (arkham_boss_hit(kind, from_x, from_z, mortal)) { e->plan = P_DEATH; died(); }
        return;
    }
    if (e->kind == ENEMY_VANGUARD && (vanguard_hidden() || vanguard_dying())) return;
    if (e->kind == ENEMY_GIGAPEDE) { giga_hit(kind); return; }
    if (e->kind == ENEMY_JESTER) { jester_hit(kind); return; }
    if (e->kind == ENEMY_BISHOP) {++hits;bishop_hit_glue(kind,from_x,from_z);return;}
    if (e->kind == ENEMY_DULLAHAN) { ++hits; dullahan_hit_glue(kind,from_x,from_z); return; }
    if (e->kind == ENEMY_FALLEN) { ++hits; fallen_hit_glue(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_ARACHNE) { ++hits; arachne_hit_glue(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_NEVAN) { ++hits; nevan_hit_glue(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_PAWN) { pawn_hit(kind); return; }
    if (e->kind == ENEMY_BEOWULF) { ++hits; beowulf_hit_glue(kind, from_x, from_z); return; }
    if (e->kind == ENEMY_SOUL) { ++hits; soul_hit_glue(kind); return; }
    if (e->kind == ENEMY_LADY) { ++hits; lady_hit_glue(kind); return; }
    if (e->kind == ENEMY_AGNI || e->kind == ENEMY_RUDRA) { ar_hit(kind, from_x, from_z); return; }
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
    if (mortal && !e->mirror_bound) {
        e->health = (short)(e->health - (kind == HIT_LIGHT ? 1 : 2));
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
    /* A wind on Dante this frame (Beowulf's volley), applied with the bodies' shoves so it meets the walls too. */
    *x += enemy_wind[0]; *z += enemy_wind[1];
    enemy_wind[0] = enemy_wind[1] = 0.0f;
    for (i = 0; i < ENEMY_MAX; ++i) {
        if (enemies[i].kind == ENEMY_VANGUARD) vanguard_select(i);
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
        if (enemies[i].kind == ENEMY_GERYON) {
            if (enemies[i].active) geryon_boss_push_out(x, z, y, r);
            continue;
        }
        if (enemies[i].kind == ENEMY_DOPPELGANGER) {
            if (enemies[i].active) doppelganger_boss_push_out(x, z, y, r);
            continue;
        }
        if (enemies[i].kind == ENEMY_ARKHAM) {
            if (enemies[i].active) arkham_boss_push_out(x, z, y, r);
            continue;
        }
        const Enemy *o = &enemies[i];
        float dx, dz, d, min;
        e = &enemies[i];
        if (o->active && is_lying()) continue;
        if (!o->active || o->plan == P_DEATH) continue;
        dx = *x - o->pos[0]; dz = *z - o->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        min = r + body_radius(o);
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

static FILE *audit_file;
static unsigned int audit_frames, audit_bodies, audit_violations;
static unsigned int audit_expected, audit_room_expected;
static int audit_mission = -1, audit_room = -1;
static unsigned int audit_room_frames, audit_room_bodies, audit_room_violations;
static int audit_script_at = -1, audit_script_total = -1;

void enemy_audit_script(int at, int total) { audit_script_at=at; audit_script_total=total; }

static void audit_room_summary(void) {
    if (audit_file && audit_mission >= 0)
        fprintf(audit_file,"# room_summary mission=%d room=%d frames=%u bodies=%u violations=%u expected=%u script_at=%d script_total=%d\n",
                audit_mission,audit_room,audit_room_frames,audit_room_bodies,audit_room_violations,audit_room_expected,
                audit_script_at,audit_script_total);
}

static int body_airborne(const Enemy *a) {
    if (a->kind == ENEMY_GOYLE || a->kind == ENEMY_FALLEN || a->kind == ENEMY_NEVAN) return 1;
    /* pawn.inc's PW_RISE/PW_FALL use their own hop physics, not Enemy.air. */
    if (a->kind == ENEMY_PAWN) return a->action == 1 || a->action == 2 ||
        (a->plan == P_DEATH && a->pos[1] > a->floor_y + .5f);
    return a->air || (a->action == A_LEAP && a->step == 1);
}

void enemy_audit_start(const char *path) {
    if (audit_file) fclose(audit_file);
    audit_file = fopen(path, "w");
    audit_frames = audit_bodies = audit_violations = 0;
    audit_expected = audit_room_expected = 0;
    audit_mission = audit_room = -1;
    audit_room_frames = audit_room_bodies = audit_room_violations = 0;
    if (audit_file) fprintf(audit_file, "mission,room,frame,slot,kind,other,violation,depth,x,y,z\n");
}

static void audit_violation(int mission, int room, int frame, int slot, int other, const char *what, float depth) {
    const Enemy *a = &enemies[slot];
    int expected = a->kind == ENEMY_GIGAPEDE &&
        (!strcmp(what,"wall") || !strcmp(what,"below_floor")) && giga_part_crossing(a->step,a->pos);
    /* The upright common probe can graze a wall above a rolling insect's
     * actual bounds. The native oriented box was tested and is clear. Keep
     * that diagnostic distinct from an authored geometry intersection. */
    int proxy_clear=a->kind==ENEMY_GIGAPEDE && !strcmp(what,"wall") && !expected;
    if(proxy_clear)expected=1;
    if(expected) { ++audit_expected; ++audit_room_expected; }
    else { ++audit_violations; ++audit_room_violations; }
    fprintf(audit_file, "%d,%d,%d,%d,%d,%d,%s%s,%.3f,%.3f,%.3f,%.3f\n",
            mission, room, frame, slot, a->kind, other, expected ? "expected_" : "", proxy_clear?"proxy_wall":what, depth, a->pos[0], a->pos[1], a->pos[2]);
}

/* Query each authored boss push shape on a copy, so auditing never moves Dante. */
static float audit_boss_overlap(int kind, float x, float y, float z, float radius) {
    float cx = x, cz = z;
    switch (kind) {
    case ENEMY_CERBERUS: cerberus_boss_push_out(&cx, &cz, y, radius); break;
    case ENEMY_VERGIL: vergil_boss_push_out(&cx, &cz, y, radius); break;
    case ENEMY_GERYON: geryon_boss_push_out(&cx, &cz, y, radius); break;
    case ENEMY_DOPPELGANGER: doppelganger_boss_push_out(&cx, &cz, y, radius); break;
    case ENEMY_ARKHAM: arkham_boss_push_out(&cx, &cz, y, radius); break;
    default: return -1;
    }
    return sqrtf((cx-x)*(cx-x)+(cz-z)*(cz-z));
}

void enemy_audit_frame(int mission, int room, int frame, float px, float py, float pz) {
    int i, j;
    if (!audit_file || !stage_loaded()) return;
    if (mission != audit_mission || room != audit_room) {
        audit_room_summary();
        audit_mission = mission; audit_room = room;
        audit_room_frames = audit_room_bodies = audit_room_violations = 0;
        audit_room_expected = 0;
        audit_script_at = audit_script_total = -1;
    }
    ++audit_frames;
    ++audit_room_frames;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *a = &enemies[i];
        float x, z, g, depth;
        /* Leviathan's fixed organs sit in the room's scenery outside the
         * walkable floor envelope; these are native bone hurt anchors. */
        const StageInfo *s = a->kind == ENEMY_LEVIATHAN ? stage_info() : stage_collision_info();
        if (!a->active || (a->hidden && a->kind != ENEMY_GIGAPEDE)) continue;
        /* Segments draw through giga_draw, not the skinned actor's hidden flag. */
        if (a->kind == ENEMY_GIGAPEDE && giga_part_out(a->step)) continue;
        ++audit_bodies;
        ++audit_room_bodies;
        /* Gigapede's authored path intentionally crosses walls; its in-room segments are tested below. */
        if (a->kind == ENEMY_GIGAPEDE && !stage_walkable_below(a->pos[0], a->pos[2])) continue;
        x = a->pos[0]; z = a->pos[2];
        stage_collide(&x, &z, a->pos[1], body_radius(a));
        depth = sqrtf((x-a->pos[0])*(x-a->pos[0]) + (z-a->pos[2])*(z-a->pos[2]));
        if (depth > 0.5f) audit_violation(mission, room, frame, i, -1, "wall", depth);
        depth = fmaxf(fmaxf(s->min[0]-a->pos[0], a->pos[0]-s->max[0]),
                      fmaxf(s->min[2]-a->pos[2], a->pos[2]-s->max[2]));
        if (depth > 0.5f) audit_violation(mission, room, frame, i, -1, "bounds", depth);
        if (a->kind == ENEMY_GIGAPEDE ?
            stage_floor(a->pos[0],a->pos[1]+200,a->pos[2],0,&g) :
            floor_under(a->pos[0], a->pos[1], a->pos[2], &g)) {
            if (g-a->pos[1] > 0.5f) audit_violation(mission, room, frame, i, -1, "below_floor", g-a->pos[1]);
            /* Only flag unsupported stationary ground actors; flying, launches and authored leaps are legal. */
            if (a->kind < ENEMY_KIND_COUNT && !body_airborne(a) && a->fall == 0 &&
                a->pos[1]-g > 5.0f)
                audit_violation(mission, room, frame, i, -1, "floating", a->pos[1]-g);
        } else audit_violation(mission, room, frame, i, -1, "no_floor", 0);
        if (a->plan == P_DEATH) continue; /* Corpses retain world checks, but are not live body contacts. */
        /* Special bosses have posed push shapes rather than this ordinary cylinder. */
        depth = audit_boss_overlap(a->kind, px, py, pz, PLAYER_RADIUS);
        if (depth > 0.5f) audit_violation(mission, room, frame, i, -1, "dante", depth);
        if (a->kind == ENEMY_GIGAPEDE || a->kind == ENEMY_LEVIATHAN) continue;
        for (j = i+1; j < ENEMY_MAX; ++j) {
            const Enemy *b = &enemies[j];
            float dx = a->pos[0]-b->pos[0], dz = a->pos[2]-b->pos[2];
            if (!b->active || b->hidden || b->plan == P_DEATH || b->kind == ENEMY_GIGAPEDE || b->kind == ENEMY_LEVIATHAN) continue;
            if(torso_kind(a->kind) && torso_kind(b->kind)) {
                float tx=dx+torso_offset[i][0]-torso_offset[j][0];
                float ty=a->pos[1]-b->pos[1]+torso_offset[i][1]-torso_offset[j][1];
                float tz=dz+torso_offset[i][2]-torso_offset[j][2];
                depth=body_radius(a)+body_radius(b)-sqrtf(tx*tx+ty*ty+tz*tz);
                if(depth>.5f)audit_violation(mission,room,frame,i,j,"torso_enemy",depth);
            }
            depth = audit_boss_overlap(a->kind, b->pos[0], b->pos[1], b->pos[2], ENEMY_RADIUS);
            if (depth < 0) depth = audit_boss_overlap(b->kind, a->pos[0], a->pos[1], a->pos[2], ENEMY_RADIUS);
            if (depth < 0) {
                if (fabsf(a->pos[1]-b->pos[1]) >= 170) continue;
                depth = body_radius(a)+body_radius(b)-sqrtf(dx*dx+dz*dz);
            }
            if (depth > 0.5f) audit_violation(mission, room, frame, i, j, "enemy", depth);
        }
        if (audit_boss_overlap(a->kind, px, py, pz, PLAYER_RADIUS) < 0 && fabsf(py-a->pos[1]) < 170) {
            float dx = px-a->pos[0], dz = pz-a->pos[2];
            depth = body_radius(a)+PLAYER_RADIUS-sqrtf(dx*dx+dz*dz);
            if (depth > 0.5f) audit_violation(mission, room, frame, i, -1, "dante", depth);
        }
        if(torso_kind(a->kind)) {
            float tx=a->pos[0]+torso_offset[i][0],ty=a->pos[1]+torso_offset[i][1],tz=a->pos[2]+torso_offset[i][2];
            float near_y=fmaxf(py+DANTE_LOW,fminf(py+DANTE_HIGH,ty));
            depth=ENEMY_RADIUS+PLAYER_RADIUS-sqrtf((tx-px)*(tx-px)+(tz-pz)*(tz-pz)+(ty-near_y)*(ty-near_y));
            if(depth>.5f)audit_violation(mission,room,frame,i,-1,"torso_dante",depth);
        }
    }
    fflush(audit_file);
}

void enemy_audit_finish(void) {
    if (!audit_file) return;
    audit_room_summary();
    fprintf(audit_file, "# frames=%u bodies=%u violations=%u expected=%u\n", audit_frames, audit_bodies, audit_violations,audit_expected);
    fclose(audit_file);
    audit_file = NULL;
}

static void land_attack(int heavy) {
    trace_row(heavy ? 2 : 1);
    landed = 1;
    landed_x = e->pos[0];
    landed_z = e->pos[2];
    landed_heavy = heavy;
    landed_damage = -1.0f;
    landed_kind = -1;
}

int enemy_attack_kind(int heavy) { return landed_kind >= 0 ? landed_kind : heavy ? 1 : 0; }

/* Bodies, for one on the ground: other grounded enemies shove it, and it stops at Dante. */
static void keep_apart(float px, float py, float pz, float f) {
    float min = ENEMY_RADIUS * 2.0f, shoved = 0.0f, dx, dz, d2;
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        if (o == e || !o->active || o->plan == P_DEATH || fabsf(o->pos[1] - e->pos[1]) > 100.0f) continue;
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
        cap = script_bank() ? &script_bank()->caps[e->hit_cap[k] < script_bank()->caps_count ? e->hit_cap[k] : 0] : &(e->kind == ENEMY_ENVY ? envy_ai_caps : e->kind == ENEMY_ABYSS ? abyss_ai_caps : enemy_ai_caps)[e->hit_cap[k]];
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
                if (script_bank()) {
                    /* The attack record (Agni & Rudra 0x5cf6f0, Beowulf 0x5b7fa0): its damage, and a heavy reaction
                     * from 500. */
                    const ScriptBank *bk = script_bank();
                    const ArAttack *r = &bk->attacks[e->hit_type < bk->attack_count ? e->hit_type : 0];
                    land_attack(r->reaction >= 500.0f);
                    landed_damage = r->damage;
                    landed_kind = r->kind;
                } else if ((e->kind == ENEMY_PRIDE || e->kind == ENEMY_ENVY) && e->hit_type < 2) {
                    /* OP6 selects an attack record, not a heavy flag.
                     * Pride *0x618c28 = 0x5a7600; Envy *0x61a6b0 =
                     * 0x5ce960. Both tables' records 0/1 are kind 0,
                     * damage 700/800, reaction 350/400 (both light). */
                    land_attack(0);
                    landed_damage = e->hit_type ? 800.0f : 700.0f;
                    landed_kind = 0;
                } else land_attack(e->hit_type == 1);
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
            landed_kind = cerberus_boss_attack_kind();
        }
        return;
    }
    if (e->kind == ENEMY_DOPPELGANGER) {
        float hit_x, hit_z, damage;
        int heavy;
        e->plan = doppelganger_boss_dying() ? P_DEATH : P_WATCH;
        doppelganger_boss_update(f, px, py, pz, attacks_on);
        doppelganger_boss_position(e->pos);
        e->floor_y = e->pos[1];
        e->hidden = doppelganger_boss_hidden();
        if (doppelganger_boss_attack_landed(&hit_x, &hit_z, &heavy, &damage)) {
            land_attack(heavy);
            landed_x = hit_x; landed_z = hit_z; landed_damage = damage;
        }
        return;
    }
    if (e->kind == ENEMY_ARKHAM) {
        float hit_x, hit_z, damage;
        int heavy;
        e->plan = arkham_boss_dying() ? P_DEATH : P_WATCH;
        arkham_boss_update(f, px, py, pz, attacks_on);
        arkham_boss_position(e->pos);
        e->floor_y = e->pos[1];
        e->hidden = arkham_boss_hidden();
        if (arkham_boss_attack_landed(&hit_x, &hit_z, &heavy, &damage)) {
            land_attack(heavy);
            landed_x = hit_x; landed_z = hit_z; landed_damage = damage;
        }
        return;
    }
    if (e->kind == ENEMY_GERYON) {
        float hit_x, hit_z, damage;
        int heavy;
        e->plan = geryon_boss_dying() ? P_DEATH : P_WATCH;
        geryon_boss_update(f, px, py, pz, attacks_on);
        geryon_boss_position(e->pos);
        e->floor_y = e->pos[1];
        e->hidden = geryon_boss_hidden();
        if (geryon_boss_attack_landed(&hit_x, &hit_z, &heavy, &damage)) {
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
            landed_kind = vergil_boss_attack_kind();
        }
        return;
    }
    if (e->kind == ENEMY_VANGUARD) {
        float ground;
        vanguard_select((int)(e-enemies));
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
    if(e->kind==ENEMY_BISHOP){bishop_update_glue(f,px,py,pz);return;}
    if (e->kind == ENEMY_DULLAHAN) { dullahan_update_glue(f,px,py,pz); return; }
    if (e->kind == ENEMY_FALLEN) { fallen_update_glue(f, px, py, pz); return; }
    if (e->kind == ENEMY_ARACHNE) { arachne_update_glue(f, px, py, pz); return; }
    if (e->kind == ENEMY_NEVAN) { nevan_update_glue(f, px, py, pz); return; }
    if (e->kind == ENEMY_PAWN) { pawn_update(f, px, py, pz); return; }
    if (e->kind == ENEMY_BEOWULF) { beowulf_update_glue(f, px, py, pz); return; }
    if (e->kind == ENEMY_SOUL) { soul_update_glue(f, px, py, pz); return; }
    if (e->kind == ENEMY_LADY) { lady_update_glue(f, px, py, pz); return; }
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
        if (!e->air && !leaping && e->plan != P_DEATH) keep_apart(px, py, pz, f);
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

/* All ordinary kinds share this final collision pass, including flying bodies,
 * root-motion lunges and reactions. It runs after their individual brains.
 * The boss modules with independent positions keep their own authored solvers. */
static void body_move(Enemy *a, float x, float y, float z) {
    float radius = body_radius(a);
    float dx = x-a->pos[0], dz = z-a->pos[2], dy = y-a->pos[1];
    float cx = a->pos[0], cz = a->pos[2], old_y = a->pos[1], ground;
    int grounded = !body_airborne(a) && fabsf(y-a->floor_y) < 0.5f;
    float original[3], original_floor = a->floor_y;
    int steps = (int)(fmaxf(hypotf(dx, dz), fabsf(dy)) / 15.0f) + 1, s;
    memcpy(original,a->pos,sizeof(original));
    /* Long placements (Sloth warp) have already selected a valid landing. */
    if (steps > 256) { cx = x; cz = z; dx = dz = 0; steps = 1; }
    for (s = 1; s <= steps; ++s) {
        float nx = cx+dx/steps, nz = cz+dz/steps;
        float sy = old_y+dy*(float)s/steps;
        if (stage_loaded()) {
            body_wall(&nx, &nz, sy, radius);
            /* Arena wall footprints also constrain launches and flying actors.
             * Otherwise a launch clears a low door barrier and lands trapped
             * in its sealed pocket (Trial of Wisdom's north doorway). */
            if (body_airborne(a) && sy > a->floor_y+5)
                body_wall(&nx,&nz,a->floor_y,radius);
            /* Open collision edges are often lower than a launched enemy.
             * Keep its footprint over the arena, even above those edge walls. */
            if (!floor_under(nx, fmaxf(old_y,y), nz, &ground) &&
                floor_under(cx, fmaxf(old_y,y), cz, &ground)) break;
        }
        cx = nx; cz = nz;
    }
    a->pos[0] = cx; a->pos[1] = y; a->pos[2] = cz;
    if (floor_under(cx, y, cz, &ground)) {
        a->floor_y = ground;
        if (y < ground || grounded) a->pos[1] = ground;
    }
    if (stage_loaded()) {
        int k;
        for (k = 0; k < 3; ++k) {
            float before = a->pos[1];
            body_wall(&a->pos[0], &a->pos[2], a->pos[1],radius);
            if (body_airborne(a) && a->pos[1] > a->floor_y+5)
                body_wall(&a->pos[0],&a->pos[2],a->floor_y,radius);
            if (floor_under(a->pos[0],a->pos[1],a->pos[2],&ground)) {
                a->floor_y = ground;
                if (a->pos[1] < ground || grounded) a->pos[1] = ground;
            }
            if (fabsf(a->pos[1]-before) < 0.01f) break;
        }
        /* A narrow intersection can defeat the bounded wall projector. Keep
         * the last clear point instead of committing an unresolved contact. */
        {
            float tx = a->pos[0], tz = a->pos[2];
            stage_collide(&tx,&tz,a->pos[1],radius);
            if (hypotf(tx-a->pos[0],tz-a->pos[2]) > 0.1f) {
                tx = original[0]; tz = original[2];
                stage_collide(&tx,&tz,original[1],radius);
                if (hypotf(tx-original[0],tz-original[2]) < 0.1f) {
                    memcpy(a->pos,original,sizeof(original)); a->floor_y = original_floor;
                }
            }
        }
    }
}

static int torso_circle(const Enemy *a,const Enemy *b,float px,float py,float pz,float *x,float *z,float *radius) {
    int i=(int)(a-enemies),j=b?(int)(b-enemies):0;
    float ay,by,dy,r;
    if(!torso_kind(a->kind) || (b && !torso_kind(b->kind)))return 0;
    ay=a->pos[1]+torso_offset[i][1];
    by=b?b->pos[1]+torso_offset[j][1]:fmaxf(py+DANTE_LOW,fminf(py+DANTE_HIGH,ay));
    dy=ay-by;r=body_radius(a)+(b?body_radius(b):PLAYER_RADIUS);
    if(fabsf(dy)>=r)return 0;
    *x=(b?b->pos[0]+torso_offset[j][0]:px)-torso_offset[i][0];
    *z=(b?b->pos[2]+torso_offset[j][2]:pz)-torso_offset[i][2];
    *radius=sqrtf(r*r-dy*dy);
    return 1;
}

static float body_overlap_score(const Enemy *a, float px, float py, float pz) {
    float score = 0, depth;
    float tx,tz,tr;
    int i;
    if (fabsf(a->pos[1]-py) < 170) {
        depth = body_radius(a)+PLAYER_RADIUS-hypotf(a->pos[0]-px,a->pos[2]-pz);
        if (depth > 0) score += depth*depth;
    }
    if(torso_circle(a,0,px,py,pz,&tx,&tz,&tr)) {
        depth=tr-hypotf(a->pos[0]-tx,a->pos[2]-tz);
        if(depth>0)score+=depth*depth;
    }
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *b = &enemies[i];
        if (b == a || !body_solid(b)) continue;
        depth = (body_radius(a)+body_radius(b))-hypotf(a->pos[0]-b->pos[0],a->pos[2]-b->pos[2]);
        if (depth > 0 && fabsf(a->pos[1]-b->pos[1]) < 170) score += depth*depth;
        if(torso_circle(a,b,0,0,0,&tx,&tz,&tr)) {
            depth=tr-hypotf(a->pos[0]-tx,a->pos[2]-tz);
            if(depth>0)score+=depth*depth;
        }
    }
    return score;
}

/* A radial push into a wall cannot separate a body pinned between that wall
 * and another actor. Try nearby points on the contact circle, sweeping each
 * candidate through the stage and accepting only a lower total penetration. */
static void body_escape(Enemy *a, float px, float py, float pz) {
    float cx = px, cz = pz, radius = body_radius(a)+PLAYER_RADIUS;
    float worst = fabsf(a->pos[1]-py) < 170 ? radius-hypotf(a->pos[0]-px,a->pos[2]-pz) : 0;
    float original[3], best[3], original_floor = a->floor_y, best_floor = a->floor_y;
    float best_score = body_overlap_score(a,px,py,pz), best_move = 0, angle;
    float tx,tz,tr,depth;
    int i, k, ring;
    if (best_score <= 0.25f) return;
    if(torso_circle(a,0,px,py,pz,&tx,&tz,&tr) && (depth=tr-hypotf(a->pos[0]-tx,a->pos[2]-tz))>worst)
        {worst=depth;cx=tx;cz=tz;radius=tr;}
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *b = &enemies[i];
        float depth;
        if (b == a || !body_solid(b)) continue;
        depth = (body_radius(a)+body_radius(b))-hypotf(a->pos[0]-b->pos[0],a->pos[2]-b->pos[2]);
        if (fabsf(a->pos[1]-b->pos[1]) < 170 && depth > worst) { worst = depth; cx = b->pos[0]; cz = b->pos[2]; radius = (body_radius(a)+body_radius(b)); }
        if(torso_circle(a,b,0,0,0,&tx,&tz,&tr) && (depth=tr-hypotf(a->pos[0]-tx,a->pos[2]-tz))>worst)
            {worst=depth;cx=tx;cz=tz;radius=tr;}
    }
    if (worst <= 0.5f) return;
    memcpy(original,a->pos,sizeof(original)); memcpy(best,original,sizeof(best));
    angle = atan2f(a->pos[0]-cx,a->pos[2]-cz);
    /* The outer rings let a landing body go around a doorway's shoulder.
     * Staying within one body diameter can leave every candidate blocked by
     * that same corner (M18's room 420). Every route remains wall-swept. */
    for (ring = 0; ring < 8; ++ring) for (k = 0; k < 32; ++k) {
        float turn = (float)((k+1)/2)*(PI/16)*(k&1 ? 1 : -1);
        float score, moved;
        memcpy(a->pos,original,sizeof(original)); a->floor_y = original_floor;
        float contact_radius = radius+.02f+(float)(ring*ring*4);
        body_move(a,cx+sinf(angle+turn)*contact_radius,original[1],cz+cosf(angle+turn)*contact_radius);
        score = body_overlap_score(a,px,py,pz);
        moved = hypotf(a->pos[0]-original[0],a->pos[2]-original[2]);
        if (score < best_score-0.001f || (score <= best_score && moved < best_move)) {
            best_score = score; best_move = moved; memcpy(best,a->pos,sizeof(best)); best_floor = a->floor_y;
        }
        if (best_score < 0.0001f) { ring = 8; break; }
    }
    memcpy(a->pos,best,sizeof(best)); a->floor_y = best_floor;
}

static void resolve_bodies(float px, float py, float pz) {
    int pass, i, j;
    /* Revisit contacts after every enemy has moved: the old one-sided pass
     * could undo a previous actor's separation, especially in mid-air. */
    for (pass = 0; pass < 24; ++pass) {
        int contacts = 0;
        for (i = 0; i < ENEMY_MAX; ++i) {
            Enemy *a = &enemies[i];
            float dx, dz, distance, depth;
            if (!body_solid(a)) continue;
            for (j = i+1; j < ENEMY_MAX; ++j) {
                Enemy *b = &enemies[j];
                float tx,tz,tr;
                if (!body_solid(b)) continue;
                dx = a->pos[0]-b->pos[0]; dz = a->pos[2]-b->pos[2];
                distance = hypotf(dx, dz); depth = (body_radius(a)+body_radius(b))-distance;
                if (depth > 0.01f && fabsf(a->pos[1]-b->pos[1]) < 170) {
                if (distance < 0.001f) { dx = 1; dz = 0; distance = 1; }
                dx /= distance; dz /= distance;
                body_move(a, a->pos[0]+dx*(depth*.5f+.01f), a->pos[1], a->pos[2]+dz*(depth*.5f+.01f));
                body_move(b, b->pos[0]-dx*(depth*.5f+.01f), b->pos[1], b->pos[2]-dz*(depth*.5f+.01f));
                /* Give the unblocked body the part its pinned neighbour could
                 * not take, instead of repeatedly losing half the correction. */
                dx = b->pos[0]-a->pos[0]; dz = b->pos[2]-a->pos[2]; distance = hypotf(dx,dz);
                depth = (body_radius(a)+body_radius(b))-distance;
                if (depth > 0.01f && distance > 0.001f)
                    body_move(b,b->pos[0]+dx/distance*(depth+.01f),b->pos[1],b->pos[2]+dz/distance*(depth+.01f));
                ++contacts;
                }
                if(torso_circle(a,b,0,0,0,&tx,&tz,&tr)) {
                    dx=a->pos[0]-tx;dz=a->pos[2]-tz;distance=hypotf(dx,dz);depth=tr-distance;
                    if(depth>.01f) {
                        if(distance<.001f){dx=a->pos[0]-b->pos[0];dz=a->pos[2]-b->pos[2];distance=hypotf(dx,dz);}
                        if(distance<.001f){dx=1;dz=0;distance=1;}
                        dx/=distance;dz/=distance;
                        body_move(a,a->pos[0]+dx*(depth*.5f+.01f),a->pos[1],a->pos[2]+dz*(depth*.5f+.01f));
                        body_move(b,b->pos[0]-dx*(depth*.5f+.01f),b->pos[1],b->pos[2]-dz*(depth*.5f+.01f));
                        ++contacts;
                    }
                }
            }
            if (fabsf(a->pos[1]-py) < 170) {
            dx = a->pos[0]-px; dz = a->pos[2]-pz;
            distance = hypotf(dx, dz); depth = body_radius(a)+PLAYER_RADIUS-distance;
            if (depth > 0.01f) {
                if (distance < 0.001f) { dx = 1; dz = 0; distance = 1; }
                body_move(a, a->pos[0]+dx/distance*(depth+.01f), a->pos[1], a->pos[2]+dz/distance*(depth+.01f));
                ++contacts;
            }
            }
            {
                float tx,tz,tr;
                if(torso_circle(a,0,px,py,pz,&tx,&tz,&tr)) {
                    dx=a->pos[0]-tx;dz=a->pos[2]-tz;distance=hypotf(dx,dz);depth=tr-distance;
                    if(depth>.01f) {
                        if(distance<.001f){dx=a->pos[0]-px;dz=a->pos[2]-pz;distance=hypotf(dx,dz);}
                        if(distance<.001f){dx=1;dz=0;distance=1;}
                        body_move(a,a->pos[0]+dx/distance*(depth+.01f),a->pos[1],a->pos[2]+dz/distance*(depth+.01f));
                        ++contacts;
                    }
                }
            }
        }
        if (!contacts) break;
        if (pass == 7 || pass == 15 || pass == 23) {
            for (i = 0; i < ENEMY_MAX; ++i) {
                Enemy *a = &enemies[i];
                if (body_solid(a)) body_escape(a,px,py,pz);
            }
        }
    }
}

void enemy_finish_collisions(float px, float py, float pz) {
    int i;
    Enemy *selected=e;
    for(i=0;i<ENEMY_MAX;++i)if(body_solid(&enemies[i]) && torso_kind(enemies[i].kind)) {
        float c,s;
        e=&enemies[i];evaluate_pose();c=cosf(e->yaw);s=sinf(e->yaw);
        torso_offset[i][0]=world_pos[3][0]*c+world_pos[3][2]*s;
        torso_offset[i][1]=world_pos[3][1];
        torso_offset[i][2]=world_pos[3][2]*c-world_pos[3][0]*s;
    }
    e=selected;
    resolve_bodies(px, py, pz);
    for (i=0;i<ENEMY_MAX;++i) if(enemies[i].active && enemies[i].kind==ENEMY_VANGUARD) {
        vanguard_select(i);vanguard_contact_position(enemies[i].pos,enemies[i].floor_y);
    }
}

void enemy_update(float dt, float px, float py, float pz) {
    Enemy *selected = e;
    int i;
    ++trace_tick;
    if (attack_token > 0.0f) attack_token -= dt / (player_step_scale > 0.0f ? player_step_scale : 1.0f);
    for (i = 0; i < ENEMY_MAX; ++i) {
        float was_x, was_y, was_z;
        e = &enemies[i];
        was_x = e->pos[0]; was_y = e->pos[1]; was_z = e->pos[2];
        update_one(dt, px, py, pz);
        if (e->active && e->kind < ENEMY_KIND_COUNT && enemy_models[e->kind].cloth_count) {
            e->cloth_frames += dt * 60.0f;   /* the chains step at the game's 60 fps (FUN_003b38e0, once a frame) */
            if (e->cloth_frames > 4.0f) e->cloth_frames = 4.0f;
        }
        if (e->active && e->kind == ENEMY_GREED) greed_tick(i, dt * MOTION_FPS, px, pz);
        /* The room's walls, as for Dante: swept along everything that moved it this frame (its walk, root
         * motion, knockback, being pushed by him or the others), in steps of at most 15 cm, so a launch or a
         * shove can't carry it through. A spawn or a jump past 6 m is a placement, not a move: checked where it
         * lands. The Gigapede flies its own paths. */
        if (e->active && e->kind < ENEMY_KIND_COUNT && stage_loaded()) {
            float x = e->pos[0], y = e->pos[1], z = e->pos[2];
            /* A warp is placement, not travel through the intervening walls. */
            if (hypotf(x-was_x,z-was_z) > 600) { was_x = x; was_y = y; was_z = z; }
            e->pos[0] = was_x; e->pos[1] = was_y; e->pos[2] = was_z;
            body_move(e, x, y, z);
        }
    }
    arrows_update(dt * MOTION_FPS, px, py, pz);
    coffins_update(dt * MOTION_FPS);
    scrolls_update(dt * 60.0f);
    e = selected;
}

/* Each kind's .TSC scrolls (FUN_0019ddb0 a frame): the phase and MinimumUV's drift per axis, and the offsets drawn. */
#define SCROLL_MAX 4
static float scroll_phase[ENEMY_KIND_COUNT][SCROLL_MAX][2], scroll_drift[ENEMY_KIND_COUNT][SCROLL_MAX][2];
static float scroll_uv[ENEMY_KIND_COUNT][SCROLL_MAX][2];

static float wrap01(float x) { while (x > 1.0f) x -= 1.0f; while (x < 0.0f) x += 1.0f; return x; }

/* frames: 60 fps frames of game time (the owner's speed, +0x1b0). Type 1 (FUN_0019e740 / FUN_0019e360): the
 * phase moves frames / TimeUV a frame along DirUV, and is the offset. Type 3 (FUN_0019df50 / FUN_0019e840): the
 * same phase eased, (cos((1 - phase) * pi) + 1) / 2, plus MinimumUV's drift. An axis with no time or direction
 * stays at 0. */
static void scrolls_update(float frames) {
    int k, s, a;
    for (k = 0; k < ENEMY_KIND_COUNT; ++k) {
        const EnemyModel *m = &enemy_models[k];
        for (s = 0; s < m->scroll_count && s < SCROLL_MAX; ++s) {
            const EnemyScroll *sc = &m->scrolls[s];
            for (a = 0; a < 2; ++a) {
                float time = a ? sc->time_v : sc->time_u, minimum = a ? sc->minimum_v : sc->minimum_u;
                int dir = a ? sc->dir_v : sc->dir_u;
                float *phase = &scroll_phase[k][s][a], *drift = &scroll_drift[k][s][a];
                if (time == 0.0f || dir == 0) { scroll_uv[k][s][a] = 0.0f; continue; }
                *phase = wrap01(*phase + frames / time * (float)dir);
                if (sc->type == 3) {
                    float v = (cosf((1.0f - *phase) * PI) + 1.0f) * 0.5f;
                    if (minimum != 0.0f) { *drift = wrap01(*drift + minimum * (float)dir * frames); v += *drift; }
                    scroll_uv[k][s][a] = v;
                } else scroll_uv[k][s][a] = *phase;
            }
        }
    }
}

/* A part's bone at its rest offset from bone `from` (its link or its parent), turned with it. */
static void hang_from(int i, int from) {
    const EnemyModel *m = &enemy_models[e->kind];
    float d[3] = {m->bone_rest[i].x - m->bone_rest[from].x, m->bone_rest[i].y - m->bone_rest[from].y,
                  m->bone_rest[i].z - m->bone_rest[from].z};
    int k;
    world_rot[i] = world_rot[from];
    for (k = 0; k < 3; ++k)
        world_pos[i][k] = world_pos[from][k] + world_rot[from].m[k][0] * d[0] + world_rot[from].m[k][1] * d[1] + world_rot[from].m[k][2] * d[2];
}

/* Which of a kind's bones a chain runs (built on first use). */
static unsigned char bone_free[ENEMY_KIND_COUNT][ENEMY_MAX_BONES], bone_free_built[ENEMY_KIND_COUNT];

/* The parts' bones after the body's (FUN_003b38e0's frame): those linked to a body bone take its matrix; the chains
 * step (cloth.c, at 60 fps of game time) from where their parents are now; the rest ride on their parents. */
static void pose_parts(void) {
    const EnemyModel *m = &enemy_models[e->kind];
    const int body = m->body_bone_count;
    unsigned char *is_free = bone_free[e->kind];
    int i, c, b, k, steps;
    if (m->bone_count == body && !m->cloth_count) return;
    if (!bone_free_built[e->kind]) {
        for (c = 0; c < m->cloth_count; ++c)
            for (b = 0; b < m->cloths[c].bone_count; ++b) is_free[m->cloths[c].bones[b]] = 1;
        bone_free_built[e->kind] = 1;
    }
    /* Linked and rigid first, so every chain's anchors are where the body is now. */
    for (i = body; i < m->bone_count; ++i) {
        int link = m->bone_link[i], p = m->bone_parent[i];
        if (link >= 0) hang_from(i, link);
        else if (p >= 0) hang_from(i, p);
        else { m_identity(&world_rot[i]); world_pos[i][0] = world_pos[i][1] = world_pos[i][2] = 0.0f; }
    }
    if (!m->cloth_count) return;
    if (!e->cloth) {
        /* One joint a bone (a chain may run a part's bones or the body's own: the Soul Eater's wings). */
        e->cloth = memalign(16, sizeof(ClothJoint) * ENEMY_MAX_BONES);
        if (!e->cloth) return;
        e->cloth_ready = 0;
    }
    if (!e->cloth_ready) {
        for (i = 0; i < m->bone_count; ++i) {
            ClothJoint *j = &e->cloth[i];
            memcpy(j->pos, world_pos[i], sizeof(j->pos));
            memset(j->vel, 0, sizeof(j->vel));
            memcpy(&j->rot, &world_rot[i], sizeof(j->rot));
        }
        e->cloth_ready = 1;
        e->cloth_frames = 0.0f;
    }
    steps = (int)e->cloth_frames;
    e->cloth_frames -= (float)steps;
    while (steps-- > 0) {
        for (c = 0; c < m->cloth_count; ++c) {
            const EnemyCloth *cl = &m->cloths[c];
            ClothParams params = {cl->gravity, cl->spring, cl->max_speed, cl->stiffness, cl->axis, cl->limit};
            ClothCapsule caps[4];
            int n = cl->capsule_count < 4 ? cl->capsule_count : 4;
            for (k = 0; k < n; ++k) {
                const EnemyClothCapsule *s = &cl->capsules[k];
                const float (*r)[3] = world_rot[s->bone].m;
                const float *o = world_pos[s->bone];
                const float a[3] = {s->a.x, s->a.y, s->a.z}, bb[3] = {s->b.x, s->b.y, s->b.z};
                int q;
                for (q = 0; q < 3; ++q) {
                    caps[k].a[q] = o[q] + r[q][0] * a[0] + r[q][1] * a[1] + r[q][2] * a[2];
                    caps[k].b[q] = o[q] + r[q][0] * bb[0] + r[q][1] * bb[1] + r[q][2] * bb[2];
                }
                caps[k].radius = s->radius;
            }
            for (b = 0; b < cl->bone_count; ++b) {
                int bone = cl->bones[b], p = m->bone_parent[bone];
                const float rest[3] = {m->bone_rest[bone].x - m->bone_rest[p].x, m->bone_rest[bone].y - m->bone_rest[p].y,
                                       m->bone_rest[bone].z - m->bone_rest[p].z};
                const float *pp = is_free[p] ? e->cloth[p].pos : world_pos[p];
                const ClothM3 *pr = is_free[p] ? &e->cloth[p].rot : (const ClothM3 *)&world_rot[p];
                cloth_step(&e->cloth[bone], pp, pr, rest, &params, caps, n);
            }
        }
    }
    /* The chains' bones where they hang, and whatever of a part's rides on them. */
    for (i = 0; i < m->bone_count; ++i) {
        int p = m->bone_parent[i];
        if (is_free[i]) {
            memcpy(world_pos[i], e->cloth[i].pos, sizeof(world_pos[i]));
            memcpy(&world_rot[i], &e->cloth[i].rot, sizeof(world_rot[i]));
        } else if (i >= body && m->bone_link[i] < 0 && p >= body) hang_from(i, p);
    }
}

/* Sample motion -> local rotations/offsets -> world transforms. */
static void evaluate_pose(void) {
    static MotionSample a, b, over;
    M3 local;
    int i, k;
    {
        const Motion *motion = motion_get(e->motion);
        /* Bishop's stationary idle intentionally has no motion channels. A
         * reused slot must still skin its bind pose, not sample NULL. */
        if (motion) motion_sample(motion, e->frame, &a);
        else memset(&a,0,sizeof(a));
    }
    if (e->overlay_motion >= 0) {
        motion_sample(motion_get(e->overlay_motion), e->overlay_frame, &over);
        for (i = e->overlay_lo; i <= e->overlay_hi && i < MOTION_MAX_BONES; ++i) {
            memcpy(a.rot[i], over.rot[i], sizeof(a.rot[i])); memcpy(a.has_rot[i], over.has_rot[i], sizeof(a.has_rot[i]));
            memcpy(a.trans[i], over.trans[i], sizeof(a.trans[i])); memcpy(a.has_trans[i], over.has_trans[i], sizeof(a.has_trans[i]));
        }
    }
    if (e->fade > 0.0f && e->prev_motion >= 0) motion_sample(motion_get(e->prev_motion), e->prev_frame, &b);
    const EnemyModel *m = &enemy_models[e->kind];
    for (i = 0; i < m->body_bone_count; ++i) {
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
    pose_parts();
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
    int i, bound = -1, translucent, blend_was = 0;
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
        if (e->tilt != 0.0f) sceGumRotateX(e->tilt);
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
    translucent = e->veil > 0.001f;
    if (translucent) {
        float a = 1.0f - (e->veil > 1.0f ? 1.0f : e->veil);
        blend_was = sceGuGetStatus(GU_BLEND);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuColor(((unsigned int)(a * 255.0f) << 24) | 0x00ffffffu);
    }
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
        if (m->object_scroll[i] >= 0) {
            /* A .TSC scroll (FUN_0019daf0): the texture slides, wrapping. */
            const float *uv = scroll_uv[e->kind][m->object_scroll[i]];
            sceGuTexWrap(GU_REPEAT, GU_REPEAT);
            sceGuTexOffset(uv[0], uv[1]);
            sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[e->kind] + o->index_start, e->vertices);
            sceGuTexOffset(0.0f, 0.0f);
            sceGuTexWrap(GU_CLAMP, GU_CLAMP);
            continue;
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
    if (translucent) {
        sceGuColor(0xffffffffu);
        if (!blend_was) sceGuDisable(GU_BLEND);
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
        if (e->kind == ENEMY_GERYON) { geryon_boss_draw(); continue; }
        if (e->kind == ENEMY_DOPPELGANGER) { doppelganger_boss_draw(); continue; }
        if (e->kind == ENEMY_ARKHAM) { arkham_boss_draw(); continue; }
        if (e->kind == ENEMY_LEVIATHAN) { leviathan_boss_draw(); continue; }
        if (e->kind == ENEMY_VANGUARD) {vanguard_select((int)(e-enemies));vanguard_draw();continue;}
        update_pose(elapsed, rate > 0 ? 1.0f / (float)rate : 0.0f, turn++, count);
        draw_one();
    }
    arrows_draw();
    jester_projectiles_draw(&jester_world);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    e = selected;
}

/* The soft floor shadow Dante has (fx_draw_shadow), under every enemy and boss: sized to each, fading as it rises
 * (gone 5 m up, more for the big ones). Not under the Gigapede (it crawls the walls and ceiling) or the Leviathan
 * (Dante's inside it). The sizes are the port's, fitted by eye to the models' footprints [estimates]. */
static float shadow_radius(int kind) {
    switch (kind) {
    case ENEMY_ARACHNE: return 150.0f;
    case ENEMY_BEOWULF: return 120.0f;
    case ENEMY_AGNI: case ENEMY_RUDRA: return 60.0f;
    case ENEMY_WRATH: case ENEMY_FALLEN: return 60.0f;
    case ENEMY_PAWN: case ENEMY_BISHOP: case ENEMY_DULLAHAN: case ENEMY_ENIGMA: case ENEMY_GREED: return 52.0f;
    case ENEMY_LADY: return 38.0f;
    case ENEMY_VANGUARD: return 75.0f;
    case ENEMY_CERBERUS: return 330.0f;
    case ENEMY_GERYON: return 260.0f;
    case ENEMY_ARKHAM: return 90.0f;
    case ENEMY_VERGIL: case ENEMY_DOPPELGANGER: return 42.0f;
    default: return 46.0f;
    }
}
#define SHADOW_MAX 48
void enemy_draw_shadows(void) {
    float s[SHADOW_MAX][5], p[3], g;
    int i, n = 0;
    if (ai_only) return;
    for (i = 0; i < ENEMY_MAX && n < SHADOW_MAX; ++i) {
        const Enemy *a = &enemies[i];
        float r, h, k;
        if (!a->active || a->hidden) continue;
        switch (a->kind) {
        case ENEMY_GIGAPEDE: case ENEMY_LEVIATHAN: continue;
        case ENEMY_CERBERUS: if (cerberus_boss_dying()) continue; cerberus_boss_root_position(p); break;
        case ENEMY_VERGIL: if (vergil_boss_hidden()) continue; vergil_boss_position(p); break;
        case ENEMY_GERYON: if (geryon_boss_hidden()) continue; geryon_boss_position(p); break;
        case ENEMY_DOPPELGANGER: if (doppelganger_boss_hidden()) continue; doppelganger_boss_position(p); break;
        case ENEMY_ARKHAM: if (arkham_boss_hidden()) continue; arkham_boss_position(p); break;
        case ENEMY_VANGUARD: vanguard_select(i); if (vanguard_hidden()) continue;   /* its slot keeps its feet */
            p[0] = a->pos[0]; p[1] = a->pos[1]; p[2] = a->pos[2]; break;
        default: p[0] = a->pos[0]; p[1] = a->pos[1]; p[2] = a->pos[2]; break;
        }
        if (!floor_under(p[0], p[1] - 140.0f, p[2], &g)) continue;
        { float shown; if (stage_shown_floor(p[0], g, p[2], &shown)) g = shown; }   /* on what's drawn */
        r = shadow_radius(a->kind);
        h = p[1] - g;
        k = 1.0f - h / (500.0f + 2.0f * r);
        if (h < -30.0f || k <= 0.0f) continue;
        if (k > 1.0f) k = 1.0f;
        s[n][0] = p[0]; s[n][1] = g; s[n][2] = p[2]; s[n][3] = r; s[n][4] = 170.0f * k;
        ++n;
    }
    fx_draw_shadows(&s[0][0], n);
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
typedef struct { int active, homing, type; float pos[3], vel[3], life, lift, goal[2], damage; } Arrow;   /* damage 0: the fallback */
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
    arrows[i].damage = 0.0f;
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
            landed_damage = a->damage > 0.0f ? a->damage : -1.0f;   /* not the last hit's */
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
    if (mortal && !e->mirror_bound) {
        e->health = (short)(e->health - (kind == HIT_LIGHT ? 1 : 2));
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

static int goyle_bullet(const float from[3], const float to[3]) {
    int i;
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2], l = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f;
    for (i = 0; i < ARROW_MAX && arrows[i].active; ++i) {}
    if (i >= ARROW_MAX) return -1;
    memset(&arrows[i], 0, sizeof(arrows[i]));
    arrows[i].active = 1; arrows[i].type = 4; arrows[i].life = 120.0f;
    memcpy(arrows[i].pos, from, sizeof(arrows[i].pos));
    arrows[i].vel[0] = dx / l * 25.0f; arrows[i].vel[1] = dy / l * 25.0f; arrows[i].vel[2] = dz / l * 25.0f;
    return i;
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
        if (mortal) e->health = (short)(e->health - (kind == HIT_LIGHT || kind == HIT_AIR ? 3 : 6));
    } else {
        if (mortal) e->health = (short)(e->health - (kind == HIT_LIGHT || kind == HIT_AIR ? 1 : 2));
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
 * behind him, and dances. His defence and stun meter are the game's (jester_hit). Which motion is which
 * was chosen by their shapes and lengths (inferred); his health is an estimate. The arena's centre,
 * (3500, 492, 3500), is from his set-up (FUN_005331d0). */
#define JESTER_HEALTH (ENEMY_HEALTH * 1500 / 250)
#define JESTER_STUN_AT 249.0f       /* the stun meter's threshold (0x5e2f30) */
#define JESTER_STUN_FRAMES 500.0f   /* the stun's length (0x5e2ed0) */
#define JESTER_ARENA 1400.0f   /* how far from the arena's centre he may go (its floor runs to 1550) */
enum { J_IDLE, J_WALK, J_CANE, J_CANE2, J_BOMB, J_VANISH, J_APPEAR, J_TAUNT, J_FLINCH, J_STUN, J_LAUNCHED, J_DEATH,
       J_ATK_TP, J_ATK0, J_ATK1, J_ATK2,
       /* His second set (+0x481d): 0x79 up out of the middle (states 0x32-0x34), 0x7b the fall (0x35), 0x7f the charge
        * (0x3e-0x40), 0x80 the hops (0x41-0x43), and their recovery. */
       J_RISE, J_FALL, J_CHARGE, J_HOPS, J_RECOVER };
static int jester_last_attack = -1, jester_last_throw = -1, jester_last_five = -1;
static int ar_force;   /* tests (defined with Agni & Rudra's) */
#define JESTER_FLOOR 492.0f
/* jsNN, in tools/prepare_skinned_model.py's order: [3] 0-2, [4] 3-20, [5] 21-29, [6] 30-33, [19] 34-40. */
/* The flinch (js06), the stun (js10) and the blade's vanish (js12) are the reactions' own (0x370 calls in
 * FUN_00535470, FUN_00535e60, FUN_00536110). */
static const signed char jester_motion_index[] = {21, 3, 8, 9, 10, 12, 26, 31, 6, 10, 4, 16, 32, 33, 30, 31,
                                                  25, 1, 1, 29, 3};   /* FUN_0053bd90's 0x19, 1, 1, 0x1d, 3 */
static int jester_motion[sizeof(jester_motion_index)];
/* His idle (0x66, FUN_00535140): one of 0x5e2e10's five, js21, js24, js23, js27, js28 (0x5e2db0 maps them), picked at
 * random but never the last again (the next instead); js27 / js28 walk him by their root (FUN_00535310). */
static const signed char jester_idle_index[5] = {21, 24, 23, 27, 28};
static int jester_idle_motion[5], jester_idle_last = -1, jester_idle_pick;

static int jester_init(void) {
    unsigned int i;
    for (i = 0; i < sizeof(jester_motion_index); ++i) {
        char name[5] = {'j', 's', (char)('0' + jester_motion_index[i] / 10), (char)('0' + jester_motion_index[i] % 10), 0};
        jester_motion[i] = motion_find(name);
        if (jester_motion[i] < 0) return 0;
    }
    for (i = 0; i < 5; ++i) {
        char name[5] = {'j', 's', (char)('0' + jester_idle_index[i] / 10), (char)('0' + jester_idle_index[i] % 10), 0};
        jester_idle_motion[i] = motion_find(name);
        if (jester_idle_motion[i] < 0) return 0;
    }
    return 1;
}

static void jester_state(int s) {
    int m = jester_motion[s];
    if (s == J_IDLE) {   /* 0x64 then 0x66: the next idle */
        int k = (int)(ai_unit() * 5.0f) % 5;
        if (k == jester_idle_last) k = (k + 1) % 5;
        jester_idle_last = jester_idle_pick = k;
        m = jester_idle_motion[k];
    }
    /* The attack's end (however it ends) destroys its emitter (+0x4cec-+0x4cf4); what it threw finishes its life. */
    if (e->action >= J_ATK0 && e->action <= J_ATK2 && (s < J_ATK0 || s > J_ATK2)) jester_projectiles_stop(&jester_world);
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
    e->health = 1;
    e->encounter = 0;   /* his first unless the spawn record says (enemy_set_spawn_param) */
    e->j_second = e->j_pending = e->j_up = 0;
    e->ar_health = jester_max_health(0);
    e->wake = 0.0f;
    jester_projectiles_reset(&jester_world);
    jester_state(J_TAUNT);
    e->fade = 0.0f;
}

/* His sphere in the second set (00548fb0: 160 cm (0x5e3238), 150 up), its record's damage on Dante, as a knock-down
 * (kind 3). 1 if it hit. */
static int jester_sphere(float px, float py, float pz, float damage) {
    float dx = px - e->pos[0], dy = py + 100.0f - (e->pos[1] + 150.0f), dz = pz - e->pos[2];
    if (!attacks_on || dx * dx + dy * dy + dz * dz > (160.0f + DANTE_RADIUS) * (160.0f + DANTE_RADIUS)) return 0;
    land_attack(1);
    landed_x = e->pos[0]; landed_z = e->pos[2]; landed_damage = damage;
    return 1;
}

/* One of 0x80's hops (state 0x42): T = 45 / 2 frames (0x5e3064 + 0x5e3068 x a random, halved), H = 500 (0x5e306c);
 * up at 2H / T, gravity -2H / T^2, along toward Dante (at most 500 away, 0x5e3074) over 2T. */
static void jester_hop(float px, float pz) {
    float tt = 45.0f / 2.0f, h = 500.0f, dx = px - e->pos[0], dz = pz - e->pos[2], l = sqrtf(dx * dx + dz * dz);
    if (l > 500.0f) { dx *= 500.0f / l; dz *= 500.0f / l; }
    e->vel[0] = dx / (tt * 2.0f); e->vel[2] = dz / (tt * 2.0f);
    e->vel[1] = 2.0f * h / tt; e->j_g = -2.0f * h / (tt * tt);
    e->pos[1] = JESTER_FLOOR + 0.01f;
}

/* Between attacks: his idle (0x64, FUN_00534d10, at once 0x66). */
static void jester_decide(float d) {
    (void)d;
    jester_state(J_IDLE);
}

static void jester_update(float f, float px, float py, float pz) {
    const Motion *m;
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, s, c;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    m = motion_get(e->motion);
    e->frame += f;
    e->timer += f;
    if (e->frame >= m->end_frame) {
        e->done = 1;
        e->frame = (e->action == J_WALK || e->action == J_TAUNT) ? motion_wrap(m, e->frame) : m->end_frame;
    }
    if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
    if (e->plan == P_DEATH) {
        jester_projectiles_reset(&jester_world);
        if (e->timer > 150.0f) { jester_puff(); e->active = 0; }
        return;
    }
    if (ar_force == 0x79 && e->encounter && !e->j_second && e->action != J_RISE) {   /* tests: "arforce frame 121" */
        ar_force = -1;
        e->ar_health = e->encounter == 1 ? 2400.0f : 4400.0f;
        e->j_pending = 1;
        jester_projectiles_stop(&jester_world);
        jester_state(J_RISE);
    }
    jester_projectiles_update(&jester_world, f, e->pos[0], e->pos[1], e->pos[2], e->yaw, px, py, pz, attacks_on, ai_unit);
    {
        float hx, hz, damage;
        int heavy;
        if (jester_projectiles_landed(&jester_world, &hx, &hz, &damage, &heavy)) {
            float at[3] = {px, py + 100.0f, pz};
            land_attack(heavy);
            landed_x = hx; landed_z = hz; landed_damage = damage;
            fx_explosion(at);
            giga_note("jester shot landed", (int)damage, heavy);
        }
    }
    s = sinf(e->yaw); c = cosf(e->yaw);
    if (e->action == J_IDLE || e->action == J_WALK || e->action == J_TAUNT || e->action == J_FLINCH) {
        /* FUN_00534490: his idle time reaches the wait, and he picks an attack (FUN_0053e960). */
        e->wake += f;
        if (e->wake >= JESTER_ATTACK_WAIT) {
            int five = -1;
            e->wake = 0.0f;
            if (e->j_second) {
                /* FUN_00534490 with +0x481d: on the floor (+0x4818 clear) the fall, 0x7b; else FUN_0053ea40's
                 * no-repeat pick of five: 0x75, 0x76, 0x77, 0x7f, 0x80. */
                if (!e->j_up) five = 5;
                else {
                    five = (int)(ai_unit() * 5.0f) % 5;
                    if (five == jester_last_five) five = (five + 1) % 5;
                    jester_last_five = five;
                }
            }
            giga_note("jester attack", five >= 0 ? 10 + five : jester_last_attack, e->ar_health);
            if (five == 5) { jester_state(J_FALL); jester_puff(); }
            else if (five == 3) jester_state(J_CHARGE);
            else if (five == 4) jester_state(J_HOPS);
            else {
                e->shoot_set = (signed char)(five >= 0 ? five : jester_pick(&jester_last_attack, ai_unit));
                jester_state(J_ATK_TP);
                jester_puff();
            }
        }
    }
    switch (e->action) {
    case J_ATK_TP:
        /* Each attack (0x75 / 0x76 / 0x77) begins by vanishing to a spot 100-1000 cm from Dante (FUN_00536230 row 4,
         * motion js32), facing him. */
        if (e->timer > 20.0f && !e->step) {
            e->step = 1;
            jester_reappear(4, px, pz, &e->pos[0], &e->pos[2], ai_unit);
            if (floor_under(e->pos[0], py + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
            e->yaw = atan2f(px - e->pos[0], pz - e->pos[2]);
            jester_puff();
        }
        if (e->done || e->timer > 60.0f) jester_state(J_ATK0 + (e->shoot_set > 0 ? e->shoot_set : 0));
        break;
    case J_ATK0: case J_ATK1: case J_ATK2:
        /* The throw: js33, js30 or js31 by attack. What he throws is picked the same no-repeat way (FUN_0053e720,
         * +0x4d6c) and made as an emitter (FUN_0053e800, jester.c): kind 0 the ball, 1 the traps, 2 the four-arm cross,
         * at AI rank 0. It lives until the attack ends, which waits for the emitter's own cycle. */
        if (e->frame < 25.0f) goyle_turn_to(px, pz, 0.12f, f);
        if (!e->step && e->frame > 30.0f) {
            int kind = jester_pick(&jester_last_throw, ai_unit);
            e->step = 1;
            jester_projectiles_begin(&jester_world, kind, 0, e->pos[0], e->pos[1], e->pos[2], px, pz);
            giga_note("jester throw", kind, e->ar_health);
        }
        if (e->done && e->step && e->timer >= jester_projectiles_minimum(&jester_world)) {
            jester_projectiles_stop(&jester_world);
            e->j_up = 0;   /* his attacks end on the floor (FUN_005373c0, state 0x2e, clears +0x4818) [inferred] */
            jester_state(J_IDLE);
        }
        break;
    case J_RISE:
        /* 0x79 (FUN_00539470 → state 0x32, FUN_00539540): at the arena's middle, out of sight 60 frames; then from
         * y -220 facing Dante (js25), up 6 a frame to the floor (0x33, FUN_005397c0), and his second set is on. */
        if (e->step == 0) {
            e->hidden = 1;
            if (e->timer >= 60.0f) {
                e->hidden = 0; e->step = 1;
                e->pos[0] = 3500.0f; e->pos[1] = -220.0f; e->pos[2] = 3500.0f;
                e->yaw = atan2f(px - e->pos[0], pz - e->pos[2]);
                e->frame = 0.0f; e->done = 0;
                jester_puff();
            } else e->frame = 0.0f;
        } else {
            e->pos[1] += 6.0f * f;
            if (e->pos[1] > JESTER_FLOOR) e->pos[1] = JESTER_FLOOR;
            if (e->done) { e->j_second = 1; e->j_up = 1; e->j_pending = 0; jester_state(J_IDLE); }
        }
        break;
    case J_FALL:
        /* 0x7b (FUN_00539a40, state 0x35 FUN_00539c80): up at y 2492 over where he was for 60 frames (0x5e2fc0), then
         * falling (1.11 a frame more, +0x4810) onto a spot 50-550 from Dante (FUN_00536540 row 1) in 60 frames;
         * falling, his sphere (00548fb0 kind 2: 160 cm, record 7, 1000) hurts. Landed, idle (0x36). */
        if (e->step == 0) {
            float tx, tz;
            jester_reappear(1, px, pz, &tx, &tz, ai_unit);
            e->vel[0] = (tx - e->pos[0]) / 60.0f; e->vel[2] = (tz - e->pos[2]) / 60.0f; e->vel[1] = 0.0f;
            e->pos[1] = 2492.0f;
            e->step = 1; e->j_up = 1;
        } else if (e->step == 1) {
            if (e->timer >= 60.0f) e->step = 2;
        } else {
            e->pos[1] += e->vel[1] * f - 1.1111f * 0.5f * f * f;
            e->vel[1] -= 1.1111f * f;
            e->pos[0] += e->vel[0] * f; e->pos[2] += e->vel[2] * f;
            jester_sphere(px, py, pz, 1000.0f);
            if (e->pos[1] < JESTER_FLOOR) { e->pos[1] = JESTER_FLOOR; jester_puff(); jester_state(J_IDLE); }
        }
        break;
    case J_CHARGE:
        /* 0x7f (FUN_0053a550; states 0x3e-0x40): 120 frames (0x5e3020) turning to him 5 degrees a frame, then 300
         * (0x5e302c) charging along his facing at 45 a frame (0x5e3024), turned back toward the middle within 35
         * degrees at random (0x5e3028) at the arena's edge (+0x4d1d) [the edge as the port's arena clamp]; his
         * sphere (kind 1: 160 cm, record 6, 1000) is live, resting 30 frames after a hit. Then 60 frames (js03). */
        if (e->step == 0) {
            goyle_turn_to(px, pz, 5.0f * PI / 180.0f, f);
            if (e->timer >= 120.0f) { e->step = 1; e->timer = 0.0f; e->vel[0] = sinf(e->yaw) * 45.0f; e->vel[2] = cosf(e->yaw) * 45.0f; e->vel[1] = 0.0f; }
        } else {
            float ox = e->pos[0] - 3500.0f, oz = e->pos[2] - 3500.0f;
            e->pos[0] += e->vel[0] * f; e->pos[2] += e->vel[2] * f;
            if (fabsf(ox) > JESTER_ARENA - 50.0f || fabsf(oz) > JESTER_ARENA - 50.0f) {
                if (e->vel[0] * ox + e->vel[2] * oz > 0.0f) {
                    float a = atan2f(-ox, -oz) + (ai_unit() * 2.0f - 1.0f) * 35.0f * PI / 180.0f;
                    e->vel[0] = sinf(a) * 45.0f; e->vel[2] = cosf(a) * 45.0f;
                }
            }
            e->yaw = atan2f(e->vel[0], e->vel[2]);
            if (e->vel[1] > 0.0f) e->vel[1] -= f;   /* resting after a hit */
            else if (jester_sphere(px, py, pz, 1000.0f)) e->vel[1] = 30.0f;
            if (e->timer >= 300.0f) jester_state(J_RECOVER);
        }
        break;
    case J_HOPS:
        /* 0x80 (FUN_0053afb0; states 0x41-0x43): 120 frames (0x5e30c0) spinning 10 degrees a frame (js29), then 5 hops
         * (0x5e3060, rank 0) at Dante: each 45 frames (0x5e3064), 500 high (0x5e306c), at most 500 along
         * (0x5e3074); falling, his sphere (kind 2, record 7, 1000) is live. Then 60 frames. */
        e->yaw += 10.0f * PI / 180.0f * f;
        if (e->step == 0) {
            if (e->timer >= 120.0f) { e->step = 1; e->j_hops = 5; jester_hop(px, pz); }
        } else {
            e->pos[1] += e->vel[1] * f + e->j_g * 0.5f * f * f;
            e->vel[1] += e->j_g * f;
            e->pos[0] += e->vel[0] * f; e->pos[2] += e->vel[2] * f;
            if (e->vel[1] < 0.0f) jester_sphere(px, py, pz, 1000.0f);
            if (e->pos[1] < JESTER_FLOOR) {
                e->pos[1] = JESTER_FLOOR;
                jester_puff();
                if (--e->j_hops > 0) jester_hop(px, pz); else jester_state(J_RECOVER);
            }
        }
        break;
    case J_RECOVER:
        if (e->timer >= 60.0f || e->done) jester_state(J_IDLE);
        break;
    case J_IDLE:
        /* FUN_00535310: his first 60 frames turn him to Dante (FUN_0053ed60(10) [its rate: inferred]); js27 / js28
         * carry him by their root; at the motion's end, 0x64 again (or 0x7b, the fall, in his second set off the
         * floor: taken by the attack choice here). */
        if (e->timer < 60.0f) goyle_turn_to(px, pz, 0.08f, f);
        if (jester_idle_pick >= 3) {
            float a[3], b[3], from = e->frame - f < 0.0f ? 0.0f : e->frame - f;
            motion_root(m, from, a);
            motion_root(m, e->frame, b);
            e->pos[0] += (b[0] - a[0]) * c + (b[2] - a[2]) * s;
            e->pos[2] += (b[2] - a[2]) * c - (b[0] - a[0]) * s;
        }
        if (e->done) jester_state(J_IDLE);
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
            /* Back near Dante, in the arena: FUN_00536230 row 1 (50-550 cm about him, kept within 1000 of the
             * centre). */
            jester_reappear(1, px, pz, &e->pos[0], &e->pos[2], ai_unit);
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
        if (e->done) jester_state(J_IDLE);
        break;
    case J_STUN:
        if (e->timer > JESTER_STUN_FRAMES) jester_state(J_VANISH);
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
    if (e->action != J_VANISH && e->action != J_RISE && e->action != J_FALL && e->action != J_HOPS) e->pos[1] = e->floor_y;
}

/* FUN_0053d0d0 (his hits), FUN_0053eb20 (which of the parameter rows 0x5e3a60 the hit meets) and the reactions
 * (FUN_00535470 a shot, FUN_00536110 a blade, FUN_00535e60 the stun). The rows' +0x20 scales the damage: 1 (in
 * hundredths) as he is, 100 while stunned (+0x1e2 = 3). The stun meter (+0x4cc8) takes 50 a blade hit (0x5e2f14,
 * rank 0) and 1.75 times the shot's own value a bullet (0x5e2f10; that value, +0x4d54, isn't read: the port's
 * bullet's 8 [estimate]); past 249 (0x5e2f30) his next reaction is the stun, 500 frames (0x5e2ed0), the meter
 * emptied. During his attacks (actions 0x15-0x2c, 0x3e-0x43) hits don't stop him. */
static int jester_attacking(void) { return e->action == J_ATK_TP || (e->action >= J_ATK0 && e->action <= J_ATK2) || e->action >= J_RISE; }
static void jester_hit(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
    int stunned = e->action == J_STUN, gun = kind == HIT_PISTOL;
    if (e->plan == P_DEATH || e->action == J_VANISH || e->hidden) return;
    ++hits;
    fx_hit_spark(at);
    if (mortal) {
        /* The port's hit values (as the Vanguard's: 42 light, 84 heavy, 8 a bullet), scaled as the row says. */
        float dmg = gun ? 8.0f : (kind == HIT_LIGHT || kind == HIT_AIR) ? 42.0f : 84.0f;
        dmg *= stunned ? 1.0f : 0.01f;
        e->ar_health -= dmg;
        if (e->ar_health <= 0.0f) {
            e->ar_health = 0.0f;
            e->plan = P_DEATH;
            jester_state(J_DEATH);
            e->plan = P_DEATH;
            died();
            return;
        }
    }
    /* FUN_00534490: in a later encounter, below its threshold (0x5e2f64: 2500, 4500), +0x4d30; his next reaction is
     * then 0x79 (FUN_0053bfc0), up out of the middle into his second set. */
    if (e->encounter && !e->j_second && e->ar_health < (e->encounter == 1 ? 2500.0f : 4500.0f)) e->j_pending = 1;
    if (stunned || jester_attacking()) return;
    if (e->j_pending) { jester_puff(); jester_state(J_RISE); return; }
    e->goyle_stone += gun ? 1.75f * 8.0f : 50.0f;
    if (e->goyle_stone > JESTER_STUN_AT) { e->goyle_stone = 0.0f; jester_state(J_STUN); return; }
    if (gun) { e->vel[0] = 3.0f; jester_state(J_FLINCH); return; }   /* 0x67-0x6a: js06, on the spot (row 0) */
    /* 0x6d: a blade's hit sends him off (row 1, 50-550 cm about Dante) unless his attack is due. */
    if (e->wake < JESTER_ATTACK_WAIT) jester_state(J_VANISH);
}

#include "pawn.inc"
#include "greed.inc"

/* ---- Agni & Rudra (class 27, EST 45; em026.pac, GDATA 131), Mission 5's boss in room 121 (docs/ENEMY_SPECS.md
 * section F). Each brother runs the game's states (agni_rudra.c: the brain, FUN_003acfb0 / 003ace70, and what
 * each state plays); his motion scripts are em026 [12] (Agni) and [13] (Rudra), run here by the Seven Hells'
 * script runner with their own caps and attack records (tools/prepare_agni_rudra.py). Health 7000 each (their
 * parameter blocks' +0x2c), against Dante's hits as for the Vanguard's 3000. Agni is red, Rudra blue (a tint:
 * their textures are shared). Before the fight they stand as statues (ar71, ar72): the game plays a cutscene. */
#define AR_HEALTH_MAX 7000.0f
#define AR_STATUE_STATE 100
static int ar_statue_motion[2];
static ArBrain ar_brains[2];
static float ar_formation_time;            /* the pair's controller: frames left on this formation */
static int ar_formation = 5;
static int ar_dante_action;     /* Dante's current move, the command table's action id (FUN_0021e920), 0 none */
static float ar_dante_yaw;
static int ar_force = -1;   /* tests: the next state either brother takes from idle */
void enemy_ar_force(int state) { ar_force = state; }
void enemy_dante_attack(int action, int gun) { ar_dante_action = gun ? 0 : action; }
void enemy_dante_yaw(float yaw) { ar_dante_yaw = yaw; }

/* FUN_003afd30: the sector Dante stands in about his facing (65536 a circle): 0 within 22.5 degrees in front, 2 / 4
 * to 90 degrees either side, 3 / 5 to 157.5, 1 behind. Odd: he's beyond the side line. */
static int ar_sector(float a) {
    float deg = a * (180.0f / PI);
    if (deg >= -22.5f && deg <= 22.5f) return 0;
    if (deg > 22.5f && deg <= 90.0f) return 2;
    if (deg < -22.5f && deg >= -90.0f) return 4;
    if (deg > 90.0f && deg <= 157.5f) return 3;
    if (deg < -90.0f && deg >= -157.5f) return 5;
    return 1;
}

/* FUN_003aeec0, for Dante (character 0): a brother reacts to a new move of Dante's (+0x2ec3 keeps the last), never
 * the survivor with both swords. Stinger (7, 8) within 650 with Dante in front and facing him (Dante's yaw less his,
 * less 90 degrees, under 180); 38 / 39 within 650 in front; 92 and 4 within 350 in front. Other moves: none (the
 * branch on +0x2ec2 never runs: it stays 0xff). */
static int ar_reacts(const ArBrain *b, int action, float d, int sector) {
    float rel;
    if (b->set == 2 || (sector & 1)) return 0;
    switch (action) {
    case 7: case 8:
        rel = wrap_angle(ar_dante_yaw - e->yaw - PI * 0.5f);
        return d <= 650.0f && rel >= 0.0f;
    case 38: case 39: return d <= 650.0f;
    case 92: case 4: return d <= 350.0f;
    default: return 0;
    }
}

static int ar_init(void) {
    int i;
    for (i = 0; i < AR_SCRIPTS; ++i) {
        char name[5] = {'a', 'r', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        ar_motion_map[i] = motion_find(name);
    }
    ar_statue_motion[0] = motion_find("ar71");
    ar_statue_motion[1] = motion_find("ar72");
    return ar_motion_map[0] >= 0 && ar_statue_motion[0] >= 0 && ar_statue_motion[1] >= 0;
}

static Enemy *ar_brother(void) {
    int i, want = e->kind == ENEMY_AGNI ? ENEMY_RUDRA : ENEMY_AGNI;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].kind == want && enemies[i].plan != P_DEATH) return &enemies[i];
    return 0;
}
static ArBrain *ar_brain(void) { return &ar_brains[e->kind == ENEMY_AGNI ? 0 : 1]; }

/* The state's next script, or done when it has no more. */
static void ar_phase_start(const ArStateInfo *in) {
    int n = e->step < 6 ? in->scripts[e->step] : -1;
    e->wake = 0.0f;
    if (n < 0) { e->done = 1; return; }
    script_play(n);
}

static void ar_go(int s) {
    const ArStateInfo *in = ar_state_info(s);
    giga_note(e->kind == ENEMY_AGNI ? "agni" : "rudra", s, e->ar_health);
    e->action = (unsigned char)s;
    e->step = 0;
    e->timer = 0.0f;
    e->vel[0] = e->vel[1] = 0.0f;
    if (in->dash >= 0) {
        float start, top, hold, slow;
        int side;
        ar_dash(in->dash, &start, &top, &hold, &slow, &side);
        e->vel[0] = start; e->vel[1] = hold;
    }
    ar_phase_start(in);
}

/* State 64, the pair's opening (FUN_003ab250; its steps are the game's +5 values). From the pedestals: a turn
 * (script 26), take-off (2), the rise (3) and the air (4) at him, speeding up 3 a frame to 16 (FUN_003b0090's
 * +0x78 / +0x2e08) for up to 420 frames or until within 100; a turn (26); script 0 while he waits (300 frames at
 * most) for his brother to reach the same wait; 60 frames × his place in the pair (+0x2ee0: Agni 0, Rudra 1 is an
 * inference); then the second leap (2, 3, 4) for 300 frames, which within 350 lands (91 + swords, FUN_003a3e30) and
 * within 1000 turns into state 65; 20 frames on, slowing 0.75 a frame, and when the landing's done, idle. */
#define AR_OPENING 64
static void ar_play(int n) { e->wake = 0.0f; script_play(n); }
static void ar_opening(float f, float px, float pz, float d, const float *root, Enemy *bro, int set) {
    const float A = 2.0f * PI / 65536.0f;
    int s = e->step;
    if ((s >= 0xc && s <= 0xd) || (s >= 0x34 && s <= 0x36)) { e->vel[0] += 3.0f * f; if (e->vel[0] > 16.0f) e->vel[0] = 16.0f; }
    else if (s == 0x37) { e->vel[0] -= 0.75f * f; if (e->vel[0] < 0.0f) e->vel[0] = 0.0f; }
    move_local(root[0], 0.0f, root[2] + e->vel[0] * f);
    switch (s) {
    case 0:
        if (d <= 100.0f) { e->step = 0x16; break; }   /* near him: no first leap */
        /* fall through */
    case 1: case 2:
        goyle_turn_to(px, pz, 0x888 * A, f);
        if (e->done) { ar_play(2); e->step = 0xb; }
        break;
    case 0xb:
        if (e->done) { ar_play(3); e->step = 0xc; }
        break;
    case 0xc:
        goyle_turn_to(px, pz, 0x800 * A, f);
        if (e->done) { ar_play(4); e->vel[1] = 420.0f; e->step = 0xd; }
        break;
    case 0xd:
        goyle_turn_to(px, pz, 0x800 * A, f);
        if ((e->vel[1] -= f) <= 0.0f || d <= 100.0f) { e->vel[0] = 0.0f; ar_play(26); e->step = 0x16; }
        break;
    case 0x16:
        goyle_turn_to(px, pz, 0x888 * A, f);
        if (e->done) { ar_play(0); e->vel[1] = 300.0f; e->step = 0x1f; }
        break;
    case 0x1f:
        goyle_turn_to(px, pz, 0x100 * A, f);
        if ((e->vel[1] -= f) <= 0.0f || !bro) { ar_go(AR_IDLE); break; }
        if ((bro->action == AR_OPENING && bro->step > 0x1d) || (bro->action != AR_OPENING && bro->action != AR_STATUE_STATE)) {
            e->vel[1] = 60.0f * (e->kind == ENEMY_AGNI ? 0.0f : 1.0f);
            e->step = 0x29;
        }
        break;
    case 0x29:
        goyle_turn_to(px, pz, 0x100 * A, f);
        if ((e->vel[1] -= f) <= 0.0f) { ar_play(2); e->step = 0x33; }
        break;
    case 0x33:
        if (e->done) { ar_play(3); e->step = 0x34; }
        break;
    case 0x34:
        goyle_turn_to(px, pz, 0x800 * A, f);
        if (e->done) { ar_play(4); e->vel[1] = 300.0f; e->step = 0x35; }
        break;
    case 0x35:
        goyle_turn_to(px, pz, 0x800 * A, f);
        if ((e->vel[1] -= f) <= 0.0f || d <= 350.0f) { ar_play(91 + set); e->step = 0x36; }
        else if (d <= 1000.0f) ar_go(65);
        break;
    case 0x36:
        if (e->wake >= 20.0f) e->step = 0x37;
        break;
    default:
        if (e->done) ar_go(AR_IDLE);
        break;
    }
    if (e->action == AR_OPENING && e->step != s) giga_note(e->kind == ENEMY_AGNI ? "agni_open" : "rudra_open", e->step, d);
}

static void ar_statue(int which) {
    e->plan = P_WATCH;
    e->action = AR_STATUE_STATE;
    e->motion = ar_statue_motion[which]; e->frame = 0.0f; e->fade = 0.0f;
    e->done = 0; e->step = 0; e->timer = 0.0f;
    e->hit_left[0] = e->hit_left[1] = 0.0f;
    e->health = 1;
    e->ar_health = AR_HEALTH_MAX;
}

static void ar_rudra_spawn(void) { ar_statue(1); }

/* The one spawn (class 27) brings both: Agni here, Rudra beside him. */
static void ar_spawn(void) {
    Enemy *agni = e;
    int k;
    agni->pos[0] += 180.0f;
    ar_statue(0);
    for (k = 0; k < ENEMY_MAX; ++k) {
        Enemy *o = &enemies[k];
        if (o->active) continue;
        e = o;
        spawn_one(ENEMY_RUDRA, agni->pos[0] - 360.0f, agni->pos[2]);
        break;
    }
    e = agni;
}

static void ar_update(float f, float px, float py, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, root[3];
    Enemy *bro = ar_brother();
    ArBrain *b = ar_brain();
    const ArStateInfo *in;
    ArSense s;
    if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
    e->pos[1] = e->floor_y;
    if (e->action == AR_STATUE_STATE) {
        /* They stand as statues until he comes near, strikes one, or waits; then both wake. */
        e->timer += f;
        if (d < 900.0f || e->timer > 600.0f || e->step || (bro && bro->action != AR_STATUE_STATE)) {
            float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]};
            fx_dust(at); fx_explosion(at);
            ar_brain_reset(b, e->kind == ENEMY_AGNI ? 0 : 1);
            ar_formation_time = 0.0f;
            ar_go(AR_OPENING);   /* +0x2de7: the pair's opening first (FUN_003aedd0) */
        }
        return;
    }
    script_tick(f, root);
    e->wake += f;
    e->timer += f;
    if (e->plan == P_DEATH) {
        if (e->done && e->timer > 200.0f) e->active = 0;
        return;
    }
    if (e->action == AR_OPENING) {
        ar_opening(f, px, pz, d, root, bro, b->set);
        keep_apart(px, py, pz, f);
        return;
    }
    /* The survivor takes up his brother's sword (+0x2f40 = 2). */
    if (!bro && b->set != 2) b->set = 2;
    s.distance = d; s.above = py - e->pos[1];
    s.health_fraction = e->ar_health / AR_HEALTH_MAX;
    s.brother_alive = bro != 0;
    s.brother_state = bro ? bro->action : -1;
    s.brother_attacking = bro && bro->action >= 42 && bro->action <= 60;
    s.flank = 0;
    if (bro) {
        /* FUN_003acae0: the angle at Dante between the two; 0x1400 and 0x4800 of 65536 a circle. */
        float da = wrap_angle(atan2f(e->pos[0] - px, e->pos[2] - pz) - atan2f(bro->pos[0] - px, bro->pos[2] - pz));
        float off = PI - fabsf(da);
        s.flank = off < 0x1400 * (2.0f * PI / 65536.0f) ? 0 : off < 0x4800 * (2.0f * PI / 65536.0f) ? (da > 0.0f ? 1 : 3) : (da > 0.0f ? 2 : 4);
        if (e->kind == ENEMY_AGNI && (ar_formation_time -= f) <= 0.0f) {
            /* The pair's controller: the next formation every 1800 frames, the healthier brother first. */
            ar_formation_time = 1800.0f;
            ar_formation = ar_formation == 5 ? 6 : 5;
            if (e->ar_health >= bro->ar_health) ar_pair_formation(&ar_brains[0], &ar_brains[1], ar_formation);
            else ar_pair_formation(&ar_brains[1], &ar_brains[0], ar_formation);
        }
    }
    ar_brain_tick(b, &s, f);
    in = ar_state_info(e->action);
    if (in->brain) {
        int sector = ar_sector(wrap_angle(atan2f(dx, dz) - e->yaw));
        /* FUN_003af2c0: Dante beyond his side line: he turns to him (state 0, ar26) before anything else. */
        if (sector & 1) { ar_go(0); return; }
        /* FUN_003aeec0: a new move of Dante's that he answers: Agni hops back (21), Rudra guards (23). */
        if (ar_dante_action == 0) e->ar_seen = 0;
        else if (e->ar_seen != (unsigned short)ar_dante_action) {
            e->ar_seen = (unsigned short)ar_dante_action;
            if (ar_reacts(b, ar_dante_action, d, sector)) { ar_go(b->set == 0 ? 21 : 23); return; }
        }
    }
    if (in->turn) goyle_turn_to(px, pz, (float)in->turn * (2.0f * PI / 65536.0f), f);
    if (in->dash >= 0) {
        /* FUN_003b0450's dash: up to its top speed, held, then slowing. */
        float start, top, hold, slow, v;
        int side;
        ar_dash(in->dash, &start, &top, &hold, &slow, &side);
        if (e->step <= 1) { e->vel[0] += (top - start) / 8.0f * f; if (fabsf(e->vel[0]) > fabsf(top)) e->vel[0] = top; }
        else if (e->vel[1] > 0.0f) e->vel[1] -= f;
        else { e->vel[0] -= slow * f; if (e->vel[0] * top < 0.0f) e->vel[0] = 0.0f; }
        v = e->vel[0] * f;
        if (side) move_local(v, 0.0f, 0.0f); else move_local(0.0f, 0.0f, v);
    } else {
        move_local(root[0], 0.0f, root[2]);
    }
    keep_apart(px, py, pz, f);
    if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
    {
        float hx = e->hand_pos[0], hz = e->hand_pos[2], sn = sinf(e->yaw), cs = cosf(e->yaw);
        float hand[3] = {e->pos[0] + hx * cs + hz * sn, e->pos[1] + e->hand_pos[1], e->pos[2] + hz * cs - hx * sn};
        if (e->action >= 52 && e->action <= 54 && e->step == 3 && e->frame >= 165.0f && e->vel[1] == 0.0f) {
            /* The slam's burst where the blade meets the floor (FUN_003a8200 phase 0x1f): the game plays V785 (Agni's
             * sword) / V786 (Rudra's) from em026 [18], whose E part (E0) shows nothing on its own in the port's effect
             * runtime and the rest is P particles (not ported). A stand-in burst, hurting within 300 cm as attack record
             * 12 [estimate]. */
            float at[3] = {e->pos[0] + sn * 150.0f, e->floor_y + 40.0f, e->pos[2] + cs * 150.0f};
            float ddx = px - at[0], ddz = pz - at[2];
            e->vel[1] = 1.0f;
            fx_explosion(at); fx_dust(at);
            if (attacks_on && ddx * ddx + ddz * ddz < 300.0f * 300.0f && fabsf(py - e->floor_y) < 200.0f) {
                land_attack(1);
                landed_damage = ar_attacks[12].damage;
            }
        }
        /* The charged blade (FUN_003ac280): V588 on Agni's sword, V589 on Rudra's, looping on it while it lasts (the
         * survivor's: the one his last charge was for). It rides his hand, not the sword's own matrix [estimate]. */
        if (b->charged > 0.0f && e->plan != P_DEATH) {
            if (!nfx_alive(b->charge_fx)) {
                int sword = b->set == 2 ? b->charge_sword : b->set;
                b->charge_fx = nfx_spawn(NFX_AGNI_RUDRA, sword ? 589 : 588, hand, e->yaw, 1);
            } else nfx_move(b->charge_fx, hand, e->yaw);
        } else if (b->charge_fx) {
            nfx_kill(b->charge_fx);
            b->charge_fx = 0;
        }
    }
    /* A script that loops (a fall, a hold) gives way after 150 frames: what it waits on isn't followed. */
    if (e->done || (e->loop && !in->brain && e->wake > 150.0f)) {
        ++e->step;
        if (e->step < 6 && in->scripts[e->step] >= 0) ar_phase_start(in);
        else {
            if (e->action == 50 || e->action == 51) { b->charge_sword = e->action == 51; ar_brain_charged(b); }
            if (e->action >= 22 && e->action <= 26 && e->action != 23 && e->action != 25) ar_brain_counter(b);
            if (e->action == AR_DEATH) return;
            ar_go(in->next >= 0 ? in->next : AR_IDLE);
            return;
        }
    }
    if (in->brain && ar_force >= 0) { int st = ar_force; ar_force = -1; ar_go(st); return; }
    if (in->brain) {
        int next = ar_brain_decide(b, &s, e->action);
        if (next >= 0) ar_go(next);
    }
}

static void ar_hit(HitKind kind, float hit_from_x, float hit_from_z) {
    float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]}, damage;
    if (e->plan == P_DEATH) return;
    ++hits;
    fx_hit_spark(at);
    if (e->action == AR_STATUE_STATE) { e->step = 1; return; }   /* struck awake */
    if ((e->action == 21 || e->action == 23 || e->action == 25) && e->step == 0 && kind != HIT_PISTOL) {
        /* Guarding (+0x1e4 = 2): the blow is turned, and he holds on to counter (22, 24, 26). */
        ar_go(e->action + 1);
        return;
    }
    if (mortal) {
        /* As the Vanguard's: 42 a light hit, 84 a heavy one, a bullet 8 [estimate]. Agni's fire is weak to
         * Cerberus's ice (the guide): twice the damage is an estimate. */
        damage = kind == HIT_PISTOL ? 8.0f : (kind == HIT_LIGHT || kind == HIT_AIR) ? 42.0f : 84.0f;
        if (e->kind == ENEMY_AGNI && kind != HIT_PISTOL && hit_weapon == 1) damage *= 2.0f;
        e->ar_health -= damage;
        if (e->ar_health <= 0.0f) {
            e->ar_health = 0.0f;
            ar_go(AR_DEATH);
            e->plan = P_DEATH;
            e->timer = 0.0f;
            died();
            return;
        }
    }
    /* A blow that throws (FUN_003a2ab0 / FUN_003a2c00, vtable +0xe4 / +0xe0: hit types beyond 0-2) knocks him
     * back, state 37 (ar50) struck from in front, 38 (ar51) from behind. Which of Dante's hits are those types is
     * inferred: the port's heavy, slam and launch. */
    if ((kind == HIT_HEAVY || kind == HIT_SLAM || kind == HIT_LAUNCH) && ar_state_info(e->action)->brain) {
        float dx = hit_from_x - e->pos[0], dz = hit_from_z - e->pos[2];
        ar_go(ar_sector(wrap_angle(atan2f(dx, dz) - e->yaw)) & 1 ? 38 : 37);
    }
}

static float ar_health_total(float *maximum) {
    int i;
    float h = 0.0f;
    *maximum = AR_HEALTH_MAX * 2.0f;
    for (i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && (enemies[i].kind == ENEMY_AGNI || enemies[i].kind == ENEMY_RUDRA) && enemies[i].plan != P_DEATH)
            h += enemies[i].ar_health;
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
static void giga_clear(void) { giga.active = 0; giga.rider = -1; giga.ride_frames = 0; }
static char giga_log_text[12288];
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
static int giga_part_crossing(int k, const float centre[3]) {
    static const float heights[]={0,5,65,120};
    float from[3],to[3],hit,back=k?GIGA_TOP_BACK:0,front=k?GIGA_TOP_FRONT:450;
    float corners[8][3],right[3],up[3];
    const float (*bounds)[3]=k?giga_body_bounds:giga_head_bounds;
    int j,h,axis;
    if(giga_part_out(k) || giga_part_out(k-1) || giga_part_out(k+1))return 1;
    if(k<0 || k>=GIGA_PARTS)return 0;
    for(h=0;h<4;++h) {
        for(j=0;j<3;++j){from[j]=giga.pos[k][j]+giga.dir[k][j]*back;to[j]=giga.pos[k][j]+giga.dir[k][j]*front;}
        from[1]+=heights[h];to[1]+=heights[h];
        if(stage_raycast(from,to,&hit))return 1;
    }
    /* A centre ray misses the first and last frames of an authored crossing:
     * the carapace reaches a wall before its centre line does. Check the
     * transformed bounds from the native mesh, rather than widening a ray
     * by an arbitrary distance or exempting every Gigapede wall contact. */
    giga_axes(k,right,up);
    /* A short boundary triangle can lie wholly inside the bounds, missing
     * all twelve outer edges. Cross-sections through the checked body point
     * also identify that grazing entry/exit (M4, frame 297). */
    for(axis=0;axis<2;++axis)for(h=0;h<4;++h) {
        const float *cross=axis?up:right;
        for(j=0;j<3;++j) {
            from[j]=centre[j]+cross[j]*bounds[0][axis];
            to[j]=centre[j]+cross[j]*bounds[1][axis];
        }
        from[1]+=heights[h]+40;to[1]+=heights[h]+40;
        if(stage_raycast(from,to,&hit))return 1;
    }
    for(h=0;h<8;++h)for(j=0;j<3;++j)
        corners[h][j]=giga.pos[k][j]+right[j]*bounds[h&1][0]+
            up[j]*bounds[(h>>1)&1][1]+giga.dir[k][j]*bounds[(h>>2)&1][2];
    for(h=0;h<8;++h)for(j=0;j<3;++j)if(!(h&(1<<j)))
        if(stage_raycast(corners[h],corners[h|(1<<j)],&hit))return 1;
    {
        float axes[3][3];
        memcpy(axes[0],right,sizeof(right));memcpy(axes[1],up,sizeof(up));
        memcpy(axes[2],giga.dir[k],sizeof(axes[2]));
        /* Small boundary faces can be wholly enclosed by the rolling mesh
         * bounds without crossing an edge or a centre ray. */
        return stage_intersects_bounds(giga.pos[k],axes,bounds);
    }
}
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

/* ---- The Arachne (class 10, CEm010; em010.pac at GDATA 121), Mission 9 (docs/ENEMY_SPECS.md H), at rank 0.
 * Built like Dante: actions (+0x3430, entered by FUN_003f6eb0) set states (+0x342c, run by FUN_003f7710), each playing
 * a motion index (FUN_003f6460) whose script (0x5bd0d0: group, n; em010 [16], motion PACs [7]-[10]) runs here on the
 * shared script runner with its caps and records (tools/prepare_arachne.py). Its decision (FUN_003f9650): with its
 * attack gate (+0x130, block 0x5c6bb0: past 150; 2 a frame within 500 and 30 degrees, 1 within 800 and 30, 0.5 within
 * 2000 and 10) shut, a move from its zone's table (Dante under 400 / 700 / beyond: 0x5c3c90 / 0x5c3dd0 / 0x5c3f20); open,
 * an attack from 0x5c4f10 with the shared token, else 0x5c5cc0 (by zone, and health band: all alike at rank 0), turning
 * to him first (0x6c) if he's behind. On a wall (after 0x70) it decides from 0x5c6200 / 0x5c6740: the web shot (0x79),
 * a crawl (0x73), a drop onto him (0x7b) or a leap down (0x72). A hit makes it react 40% of the time (0x5c6aa0): 0x10.
 * Root motion is x1.5 (FUN_00402ba0: +0x1fc9 isn't 1 for this, the 1400-health block [inferred]). */
#define AN_TURN(deg_rate) (deg_rate)   /* its turn rates are radians a frame (FUN_00402a30 -> FUN_00105760) */
#define AN_WEBS 8
typedef struct { int on, owner, holding; float pos[3], vel[3], life, hold; } AnWeb;
static AnWeb an_webs[AN_WEBS];
static float an_gate[ENEMY_MAX], an_wall[ENEMY_MAX][3], an_target[ENEMY_MAX][3];
static unsigned char an_loops[ENEMY_MAX], an_on_wall[ENEMY_MAX], an_tries[ENEMY_MAX], an_shot[ENEMY_MAX];
static float an_hit_frames[ENEMY_MAX], an_hit_damage[ENEMY_MAX];   /* a hit its state armed (FUN_00294480) */
/* Its state code arms its hits on script event 4 (FUN_00294480(set, 9, frames, bone)): a sphere of 64 (+0x2258)
 * at a front bone [placed 100 ahead, 60 up: the bones (+0x818 / +0x8c8) aren't mapped]. */
static void an_arm(int record, float frames) { an_hit_frames[e - enemies] = frames; an_hit_damage[e - enemies] = an_attacks[record].damage; }
static void an_hits(float px, float py, float pz, float f) {
    int me = (int)(e - enemies);
    float at[3], dx, dy, dz;
    if (an_hit_frames[me] <= 0.0f) return;
    an_hit_frames[me] -= f;
    at[0] = e->pos[0] + sinf(e->yaw) * 100.0f; at[1] = e->pos[1] + 60.0f; at[2] = e->pos[2] + cosf(e->yaw) * 100.0f;
    dx = px - at[0]; dy = py + 100.0f - at[1]; dz = pz - at[2];
    if (attacks_on && dx * dx + dy * dy + dz * dz < (64.0f + DANTE_RADIUS + 60.0f) * (64.0f + DANTE_RADIUS + 60.0f)) {
        land_attack(0); landed_damage = an_hit_damage[me]; landed_x = e->pos[0]; landed_z = e->pos[2];
        an_hit_frames[me] = 0.0f;
    }
}
typedef struct { unsigned char action, weight; } AnPick;
/* Rank 0's weights (the tables' first column). */
static const AnPick an_move[3][8] = {
    {{0x65, 8}, {0x6b, 10}, {0x67, 30}, {0x68, 30}, {0x70, 20}, {0x88, 2}},
    {{0x6f, 5}, {0x65, 8}, {0x66, 25}, {0x6b, 5}, {0x67, 20}, {0x68, 20}, {0x70, 15}, {0x88, 2}},
    {{0x6f, 15}, {0x65, 8}, {0x66, 35}, {0x67, 20}, {0x68, 20}, {0x88, 2}}};
static const AnPick an_attack[3][6] = {{{0x70, 10}, {0x77, 60}, {0x7a, 30}},
    {{0x71, 5}, {0x70, 20}, {0x6d, 25}, {0x7a, 50}},
    {{0x6e, 30}, {0x66, 15}, {0x71, 10}, {0x6d, 25}, {0x7a, 20}}};
static const AnPick an_no_token[3][6] = {{{0x6f, 30}, {0x65, 30}, {0x6b, 20}, {0x70, 20}},
    {{0x6f, 30}, {0x65, 30}, {0x66, 10}, {0x72, 10}, {0x70, 20}},
    {{0x65, 10}, {0x6e, 40}, {0x66, 5}, {0x71, 30}, {0x70, 15}}};
static const AnPick an_wall_attack[3][4] = {{{0x73, 25}, {0x79, 75}}, {{0x73, 25}, {0x79, 60}, {0x7b, 15}},
                                            {{0x72, 55}, {0x7b, 45}}};

static int an_init(void) {
    static const unsigned char counts[4] = {14, 19, 7, 12};
    int g, n, found = 0;
    for (g = 0; g < 4; ++g)
        for (n = 0; n < counts[g]; ++n) {
            char name[8];
            snprintf(name, sizeof name, "an%d%02d", 7 + g, n);
            an_motion_map[g * 100 + n] = motion_find(name);
            found += an_motion_map[g * 100 + n] >= 0;
        }
    return found > 0;
}
static int an_pick(const AnPick *t, int n) {
    int i, total = 0, r;
    for (i = 0; i < n && t[i].action; ++i) total += t[i].weight;
    if (total <= 0) return 0x65;
    r = (int)(ai_unit() * (float)total);
    for (i = 0; i < n && t[i].action; ++i) { if (r < t[i].weight) return t[i].action; r -= t[i].weight; }
    return t[0].action;
}
static void an_play(int index) { script_play(an_motion_script[index]); }
/* FUN_00402a30: toward Dante at this rate a frame, unless he's within 15 degrees of its facing. */
static void an_turn(float px, float pz, float rate, float f) {
    float rel = wrap_angle(atan2f(px - e->pos[0], pz - e->pos[2]) - e->yaw);
    if (fabsf(rel) < 15.0f * PI / 180.0f) return;
    e->yaw += (rel > 0.0f ? rate : -rate) * f;
}
static void an_go(int action, float px, float py, float pz);
static void an_decide(float px, float py, float pz);

static void an_webs_launch(float px, float py, float pz) {
    /* 0x79's four shots (004f4430(i, 4) -> FUN_004f46d0): from its mouth, each at a point 150 from Dante round a ring
     * (i / 4 of a turn), falling 0.8 a frame from rest (20 frames when the point's higher), at most 800 along. */
    int i, k = 0;
    float from[3] = {e->pos[0] + sinf(e->yaw) * 80.0f, e->pos[1] + 60.0f, e->pos[2] + cosf(e->yaw) * 80.0f};
    if (an_on_wall[e - enemies]) for (i = 0; i < 3; ++i) from[i] = e->pos[i] + an_wall[e - enemies][i] * 60.0f;
    for (i = 0; i < AN_WEBS && k < 4; ++i) {
        AnWeb *w = &an_webs[i];
        float a = (float)k / 4.0f * 2.0f * PI, tx = px + sinf(a) * 150.0f, tz = pz + cosf(a) * 150.0f, t, dx, dz, l;
        if (w->on) continue;
        w->on = 1; w->owner = (int)(e - enemies); w->holding = 0; w->life = 1000.0f; w->hold = 0.0f;
        w->pos[0] = from[0]; w->pos[1] = from[1]; w->pos[2] = from[2];
        t = from[1] < py ? 20.0f : sqrtf((from[1] - py) * 1.6f) / 0.8f;
        if (t < 1.0f) t = 1.0f;
        dx = tx - from[0]; dz = tz - from[2]; l = sqrtf(dx * dx + dz * dz);
        if (l > 800.0f) { dx *= 800.0f / l; dz *= 800.0f / l; }
        w->vel[0] = dx / t; w->vel[1] = 0.0f; w->vel[2] = dz / t;
        ++k;
    }
}
/* The shots (FUN_004f4a60): falling 0.8 a frame, a 100 cm sphere (0x310), 1000 frames; on Dante, he's held
 * (FUN_0021f2e0) for up to 300 frames (+0x224), his struggle 35 (0x5c6b50 [inferred]). */
static void an_webs_update(float f, float px, float py, float pz) {
    int i, me = (int)(e - enemies);
    for (i = 0; i < AN_WEBS; ++i) {
        AnWeb *w = &an_webs[i];
        float dx, dy, dz;
        if (!w->on || w->owner != me) continue;
        if (w->holding) {
            w->pos[0] = px; w->pos[1] = py + 40.0f; w->pos[2] = pz;
            if (hold_freed() || (w->hold -= f) < 0.0f) { hold_let_go(1); w->on = 0; }
            else { float v[3] = {0.0f, 0.0f, 0.0f}; fx_mote(w->pos, v, 90.0f, 90.0f, 0.05f, 0xc0f0f0f0u); }
            continue;
        }
        w->pos[0] += w->vel[0] * f; w->pos[1] += w->vel[1] * f; w->pos[2] += w->vel[2] * f;
        w->vel[1] -= 0.8f * f;
        dx = px - w->pos[0]; dy = py + 100.0f - w->pos[1]; dz = pz - w->pos[2];
        if (attacks_on && !hold_owner && dx * dx + dy * dy + dz * dz < (100.0f + DANTE_RADIUS) * (100.0f + DANTE_RADIUS)) {
            hold_take(35);
            w->holding = 1; w->hold = 300.0f;
            continue;
        }
        if ((w->life -= f) < 0.0f || w->pos[1] < e->floor_y - 50.0f) { w->on = 0; continue; }
        { float v[3] = {0.0f, 0.0f, 0.0f}; fx_mote(w->pos, v, 40.0f, 40.0f, 0.05f, 0xc0f0f0f0u); }   /* its look (effects 0x29e / 0x2a3 aren't converted) */
    }
}

/* FUN_003fa0a0 (0x70): a ray up and ahead (0, 300-800, 1000 in its frame) for a wall; found, it leaps there (5). */
static int an_find_wall(float *at, float *normal) {
    float s = sinf(e->yaw), c = cosf(e->yaw), up = 300.0f + ai_unit() * 500.0f;
    float from[3] = {e->pos[0], e->pos[1] + 50.0f, e->pos[2]}, to[3] = {from[0] + s * 1000.0f, from[1] + up, from[2] + c * 1000.0f}, t;
    int k;
    if (!stage_loaded() || !stage_raycast(from, to, &t)) return 0;
    for (k = 0; k < 3; ++k) at[k] = from[k] + (to[k] - from[k]) * t;
    /* The surface's normal isn't returned by the stage ray: the way back along it, flattened (a wall). */
    normal[0] = -s; normal[1] = 0.0f; normal[2] = -c;
    for (k = 0; k < 3; ++k) at[k] += normal[k] * 30.0f;
    return t * 1000.0f > 400.0f;
}
/* A leap (FUN_003fb240): up at 25-32, falling 1.6 a frame, toward a point (at most 1000 along). */
static void an_leap_to(float tx, float ty, float tz) {
    float v = 25.0f + ai_unit() * 7.0f, dy = e->pos[1] - ty, disc = v * v + 3.2f * dy, t, dx, dz, l;
    if (disc < 0.0f) disc = 0.0f;
    t = (v + sqrtf(disc)) / 1.6f;
    if (t < 1.0f) t = 1.0f;
    dx = tx - e->pos[0]; dz = tz - e->pos[2]; l = sqrtf(dx * dx + dz * dz);
    if (l > 1000.0f) { dx *= 1000.0f / l; dz *= 1000.0f / l; }
    e->vel[0] = dx / t; e->vel[1] = v; e->vel[2] = dz / t; e->acc[1] = -1.6f;
}

static void an_go(int action, float px, float py, float pz) {
    int me = (int)(e - enemies);
    giga_note("arachne", action, (float)e->health);
    e->action = (unsigned char)action;
    e->step = 0; e->timer = 0.0f; e->wake = 0.0f;
    e->vel[0] = e->vel[1] = e->vel[2] = 0.0f; e->acc[1] = 0.0f;
    switch (action) {
    case 0x65: an_play(0); e->gauge = 45.0f + (ai_unit() * 2.0f - 1.0f) * 10.0f; break;   /* 0x5c6a10 / 0x5c6a30 */
    case 0x66: an_play(2); an_loops[me] = 2; break;     /* 0x5c6790's pick (always 1 at rank 0) + 1 */
    case 0x67: an_play(0xd); an_loops[me] = 2; break;
    case 0x68: an_play(0xc); an_loops[me] = 2; break;
    case 0x6b: an_play(3); an_loops[me] = 1; break;
    case 0x6c: an_play(9); break;
    case 0x6d: {   /* 0x5c68e0: 5 / 10 / 15 frames, 20 / 30 / 50% */
        float r = ai_unit() * 100.0f;
        an_play(0); e->gauge = r < 20.0f ? 5.0f : r < 50.0f ? 10.0f : 15.0f;
        break;
    }
    case 0x6e: an_play(1); e->gauge = 10.0f; break;      /* 0x619a40 / 0x619a44 */
    case 0x6f: an_play(1); e->gauge = 30.0f; e->vel[0] = ai_unit() < 0.5f ? 1.0f : -1.0f; break;
    case 0x70: {
        float at[3], n[3];
        if (an_find_wall(at, n)) {
            an_play(5);
            an_target[me][0] = at[0]; an_target[me][1] = at[1]; an_target[me][2] = at[2];
            an_wall[me][0] = n[0]; an_wall[me][1] = n[1]; an_wall[me][2] = n[2];
            an_leap_to(at[0], at[1], at[2]);
            e->step = 1;
        } else if (++an_tries[me] < 3) { an_play(3); e->step = 9; }   /* 6: a short run, then it tries again */
        else { an_tries[me] = 0; an_go(0x77, px, py, pz); }
        break;
    }
    case 0x71: case 0x72:
        an_play(5);
        an_leap_to(px, e->floor_y, pz);
        if (an_on_wall[me]) { an_on_wall[me] = 0; e->tilt = 0.0f; }
        break;
    case 0x73: an_play(0); e->gauge = 45.0f; break;
    case 0x77: an_play(0x28); break;                  /* 0x5c6840: no wait at rank 0; then 0x4c */
    case 0x79: an_play(0x2f); an_shot[me] = 0; break;
    case 0x7a: {   /* 0x5bd530: its speed by distance, 0 at 0 to 12.5 at 700 */
        float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz);
        an_play(0x29);
        e->vel[2] = (d < 700.0f ? d : 700.0f) / 700.0f * 12.5f;
        break;
    }
    case 0x7b: {   /* FUN_00402510: off the wall onto Dante, falling 2 a frame (vy so it lands where he is) */
        float dy = e->pos[1] - py, t = (40.0f + sqrtf(1600.0f + 4.0f * (dy > 0.0f ? dy : 0.0f))) / 2.0f, dx = px - e->pos[0], dz = pz - e->pos[2];
        an_play(0x2d);
        if (t < 1.0f) t = 1.0f;
        e->vel[0] = dx / t; e->vel[1] = 40.0f; e->vel[2] = dz / t; e->acc[1] = -2.0f;
        an_on_wall[me] = 0; e->tilt = 0.0f;
        break;
    }
    case 0x88: an_play(0x23); break;
    case 0x10: break;   /* set by an_hit */
    case 0x14: an_play(0x1e); break;
    default: an_play(0); break;
    }
}

static void an_decide(float px, float py, float pz) {
    int me = (int)(e - enemies), zone;
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), rel;
    zone = d < 400.0f ? 0 : d < 700.0f ? 1 : 2;
    rel = wrap_angle(atan2f(dx, dz) - e->yaw);
    if (an_on_wall[me]) {
        if (an_gate[me] > 150.0f && attack_token <= 0.0f && attacks_on) {
            attack_token = ATTACK_TOKEN_SECONDS;
            an_gate[me] = 0.0f;
            an_go(an_pick(an_wall_attack[zone], 4), px, py, pz);
        } else an_go(0x72, px, py, pz);   /* 0x5c6740: the leap down, whatever the zone */
        return;
    }
    if (an_gate[me] <= 150.0f || !attacks_on) { an_go(an_pick(an_move[zone], 8), px, py, pz); return; }
    if (fabsf(rel) > PI * 0.75f) { an_go(0x6c, px, py, pz); return; }   /* behind: the turn first (dot < -0.707) */
    an_gate[me] = 0.0f;
    if (attack_token <= 0.0f) { attack_token = ATTACK_TOKEN_SECONDS; an_go(an_pick(an_attack[zone], 6), px, py, pz); }
    else an_go(an_pick(an_no_token[zone], 6), px, py, pz);
}

static void arachne_spawn_glue(void) {
    static int ready = -1;
    int me = (int)(e - enemies);
    float g;
    if (ready < 0) ready = an_init();
    e->motion = -1;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    e->plan = P_WATCH;
    e->tilt = 0.0f;
    an_gate[me] = 0.0f; an_on_wall[me] = 0; an_tries[me] = 0;
    an_play(0);
    e->action = 0x65; e->gauge = 45.0f;
    e->fade = 0.0f;
}

static void arachne_update_glue(float f, float px, float py, float pz) {
    int me = (int)(e - enemies), airborne;
    float root[3], g, dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), rel = wrap_angle(atan2f(dx, dz) - e->yaw);
    script_tick(f, root);
    an_webs_update(f, px, py, pz);
    e->timer += f;
    if (e->plan == P_DEATH) {
        if (e->done && e->timer > 180.0f) e->active = 0;
        return;
    }
    /* Its gate (FUN_00402fc0): the first row that holds him. */
    if (d < 500.0f && fabsf(rel) < 30.0f * PI / 180.0f) an_gate[me] += 2.0f * f;
    else if (d < 800.0f && fabsf(rel) < 30.0f * PI / 180.0f) an_gate[me] += 1.0f * f;
    else if (d < 2000.0f && fabsf(rel) < 10.0f * PI / 180.0f) an_gate[me] += 0.5f * f;
    airborne = e->acc[1] != 0.0f;
    if (!an_on_wall[me] && !airborne) move_local(root[0] * 1.5f, 0.0f, root[2] * 1.5f);   /* FUN_00402ba0 */
    switch (e->action) {
    case 0x65: case 0x73:
        if (!an_on_wall[me]) an_turn(px, pz, 0.01f, f);
        if ((e->gauge -= f) < 0.0f || an_gate[me] > 150.0f) an_decide(px, py, pz);
        break;
    case 0x66: case 0x67: case 0x68: case 0x6b: {
        static const float rate[4] = {0.04f, 0.15f, 0.15f, 0.03f};
        int k = e->action == 0x66 ? 0 : e->action == 0x67 ? 1 : e->action == 0x68 ? 2 : 3;
        an_turn(px, pz, rate[k], f);
        if (k == 1 || k == 2) {   /* the strafes (FUN_003fbe20): 15 sideways (0x5c6820) */
            float side = k == 1 ? -15.0f : 15.0f;
            e->pos[0] -= cosf(e->yaw) * side * f; e->pos[2] += sinf(e->yaw) * side * f;
        }
        if (e->done) {
            if (--an_loops[me] > 0) an_play(e->action == 0x66 ? 2 : e->action == 0x67 ? 0xd : e->action == 0x68 ? 0xc : 3);
            else { e->action = 0x09; e->gauge = 20.0f; an_play(0); }   /* state 9: 20 frames (0x5c67a0) */
        }
        break;
    }
    case 0x6c:
        an_turn(px, pz, 0.1f, f);
        if (e->done) { e->action = 0x09; e->gauge = 20.0f; an_play(0); }
        break;
    case 0x09:
        if ((e->gauge -= f) < 0.0f) an_decide(px, py, pz);
        break;
    case 0x6d:
        an_turn(px, pz, 0.01f, f);
        if (e->step == 0 && (e->gauge -= f) < 0.0f) {
            /* FUN_003fcf80 -> 0x14 (motion 0x2a), then off along its web line at him (0x15, FUN_003f7d80): here a
             * leap at him [the web line's swing isn't ported]. */
            e->step = 1; an_play(0x2a);
        } else if (e->step == 1 && e->done) { e->step = 2; an_play(5); an_leap_to(px, py, pz); }
        break;
    case 0x6e:
        if (e->gauge > 10.0f) an_turn(px, pz, 0.1f, f);
        if ((e->gauge -= f) < 0.0f) { e->action = 0x1a; e->gauge = 20.0f; }   /* 0x5c6778 */
        break;
    case 0x1a:
        if ((e->gauge -= f) < 0.0f) an_decide(px, py, pz);
        break;
    case 0x6f:
        e->pos[0] += cosf(e->yaw) * e->vel[0] * 8.0f * f; e->pos[2] -= sinf(e->yaw) * e->vel[0] * 8.0f * f;   /* [estimate] */
        if ((e->gauge -= f) < 0.0f) an_decide(px, py, pz);
        break;
    case 0x70:
        if (e->step == 9) { if (e->done) an_go(0x70, px, py, pz); break; }
        break;
    case 0x77:
        an_turn(px, pz, 0.04f, f);
        /* FUN_004016c0: record 0 (600), 4 frames, as its script opens flag 4 [taken as its first event] */
        if (script_event && !e->step) { e->step = 1; an_arm(0, 4.0f); }
        if (e->done) an_decide(px, py, pz);
        break;
    case 0x79:
        if (!an_shot[me] && script_event) { an_shot[me] = 1; an_webs_launch(px, py, pz); }
        if (e->done) an_go(0x7b, px, py, pz);   /* FUN_00401cc0: done, it drops onto him */
        break;
    case 0x7a:
        an_turn(px, pz, 0.2f, f);
        if (e->step == 0) { move_local(0.0f, 0.0f, e->vel[2] * f); if (e->done) { e->step = 1; an_play(0x30); } }
        else {
            /* FUN_00402200: records 5 (600) and 8 (400), 30 frames, on flag 4 [its first event] */
            if (script_event && e->step == 1) { e->step = 2; an_arm(5, 30.0f); }
            if (e->done) an_decide(px, py, pz);
        }
        break;
    case 0x88:
        if (e->done) an_decide(px, py, pz);
        break;
    case 0x10:
        e->pos[0] -= sinf(e->yaw) * e->vel[2] * f; e->pos[2] -= cosf(e->yaw) * e->vel[2] * f;
        e->vel[2] *= 0.9f;
        if (e->done) an_decide(px, py, pz);
        break;
    default:
        if (e->done && !airborne) an_decide(px, py, pz);
        break;
    }
    if (airborne) {
        int k;
        for (k = 0; k < 3; ++k) e->pos[k] += e->vel[k] * f;
        e->vel[1] += e->acc[1] * f;
        if (e->action == 0x70 && e->step == 1) {
            /* Onto the wall when it reaches the spot (or starts to fall short of it). */
            float tx = an_target[me][0] - e->pos[0], tz = an_target[me][2] - e->pos[2];
            if (tx * tx + tz * tz < 60.0f * 60.0f || (e->vel[1] < 0.0f && e->pos[1] < an_target[me][1])) {
                e->pos[0] = an_target[me][0]; e->pos[1] = an_target[me][1]; e->pos[2] = an_target[me][2];
                e->acc[1] = 0.0f; e->vel[0] = e->vel[1] = e->vel[2] = 0.0f;
                an_on_wall[me] = 1;
                e->yaw = atan2f(-an_wall[me][0], -an_wall[me][2]);
                e->tilt = -PI * 0.5f;
                an_go(0x73, px, py, pz);
            }
        } else if (e->vel[1] < 0.0f && floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g) && e->pos[1] <= g) {
            e->pos[1] = e->floor_y = g; e->acc[1] = 0.0f; e->vel[0] = e->vel[1] = e->vel[2] = 0.0f;
            if (e->action == 0x7b) {   /* 0x58: the landing (motion 0x32), its hit by the script */
                float at[3] = {e->pos[0], e->pos[1] + 30.0f, e->pos[2]};
                fx_dust(at);
                e->action = 0x58; an_play(0x32);
            } else an_decide(px, py, pz);
        }
    } else if (!an_on_wall[me] && floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    if (e->action == 0x58 && e->done) an_decide(px, py, pz);
    an_hits(px, py, pz, f);
    if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
}

static void arachne_hit_glue(HitKind kind, float from_x, float from_z) {
    int me = (int)(e - enemies), heavy = !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL);
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    fx_hit_spark(at);
    if (e->plan == P_DEATH) return;
    if (mortal) {
        e->health -= heavy ? 2 : 1;
        if (e->health <= 0) {
            e->health = 0; e->plan = P_DEATH;
            an_on_wall[me] = 0; e->tilt = 0.0f; e->acc[1] = 0.0f;
            an_go(0x14, 0.0f, 0.0f, 0.0f);
            e->timer = 0.0f;
            died();
            return;
        }
    }
    /* FUN_003f53e0: 40% of hits (0x5c6aa0) make it react (0x10); on a wall it falls off [the port's: 0x7d isn't read]. */
    if (ai_unit() * 100.0f >= 40.0f) return;
    if (an_on_wall[me]) { an_on_wall[me] = 0; e->tilt = 0.0f; e->vel[0] = e->vel[2] = 0.0f; e->vel[1] = 0.0f; e->acc[1] = -1.6f; e->action = 0x10; an_play(6); return; }
    if (e->acc[1] != 0.0f) return;
    /* FUN_003fe0b0: heavy (over 100 damage) -> motion 10 or 11 at random, knocked back 23; else 6, back 7. */
    e->action = 0x10; e->step = 0; e->timer = 0.0f;
    if (heavy) { an_play(ai_unit() < 0.5f ? 10 : 11); e->vel[2] = 23.0f; }
    else { an_play(6); e->vel[2] = 7.0f; }
    {
        float a = atan2f(from_x - e->pos[0], from_z - e->pos[2]);
        e->yaw = a;
    }
}

/* ---- Nevan (nevan_boss.c decides; here her body). Her health is the Enemy's own, for the boss bar. */
#include "nevan_boss.h"
static NevanBrain nevan_brain;
static int nevan_motion_id[NEVAN_M_COUNT] = {-2};
static void nevan_play(int m, int restart) {
    int id;
    if (nevan_motion_id[0] == -2) { int k; for (k = 0; k < NEVAN_M_COUNT; ++k) nevan_motion_id[k] = motion_find(nevan_motion_names[k]); }
    id = nevan_motion_id[m];
    if (id < 0) return;
    if (id != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = id; e->frame = 0.0f; e->done = 0;
    }
}
static void nevan_spawn_glue(void) {
    NevanBrain *b = &nevan_brain;
    float g;
    e->motion = -1;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->floor_y = g; else e->floor_y = e->pos[1];
    b->pos[0] = e->pos[0]; b->pos[1] = e->floor_y; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    nevan_spawn(b, e->health);
    nevan_play(b->want, 1);
    b->restart = 0;
    e->fade = 0.0f;
}
static void nevan_update_glue(float f, float px, float py, float pz) {
    NevanBrain *b = &nevan_brain;
    const Motion *m;
    float g;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) {
            e->done = 1;
            e->frame = nevan_loops(b->want) ? motion_wrap(m, e->frame) : m->end_frame;
        }
    }
    b->pos[0] = e->pos[0]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    b->frame = e->frame; b->done = e->done;
    b->held_free = hold_freed();
    if (ar_force >= 0 && !b->hidden && b->action <= 6) { nevan_force(b, ar_force); ar_force = -1; }   /* tests: "arforce" */
    {
        int before = b->action;
        nevan_think(b, f, px, py, pz, ai_unit, attacks_on && e->plan != P_DEATH);
        /* The kiss's hold on Dante: taken, drained, let go or thrown (main's side: combat_hold). */
        if (b->hold) { hold_take(b->hold); b->hold = 0; }
        hold_drain += b->drain; b->drain = 0.0f;
        if (b->release) { e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2]; hold_let_go(b->release); b->release = 0; }
        if (b->action != before) giga_note("nevan", b->action, b->pos[0] * 10000.0f + b->pos[2]);
    }
    e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    if (b->placed) {
        /* A teleport: the floor under where it put her (FUN_003ba790), or that height itself. */
        e->floor_y = floor_under(e->pos[0], b->pos[1] + 1.0f, e->pos[2], &g) ? g : b->pos[1];
        b->placed = 0;
    } else if (floor_under(e->pos[0], e->floor_y + 300.0f, e->pos[2], &g)) e->floor_y = g;
    e->pos[1] = e->floor_y;   /* her own motions carry her height (the stand-in's 140 cm lift is gone) */
    b->pos[1] = e->pos[1];
    e->hidden = (unsigned char)b->hidden;
    e->veil = 1.0f - b->alpha;   /* her fades (FUN_003b5870's +0x5161), blended */
    if (b->restart || nevan_motion_id[b->want] != e->motion) { nevan_play(b->want, b->restart); b->restart = 0; }
    if (b->land) { land_attack(b->land == 2); landed_damage = b->land_damage; }
    nevan_draw_fx(b);
    if (b->remove) e->active = 0;
}
static void nevan_hit_glue(HitKind kind, float from_x, float from_z) {
    NevanBrain *b = &nevan_brain;
    float at[3] = {e->pos[0], e->pos[1] + 120.0f, e->pos[2]};
    if (b->hidden || e->plan == P_DEATH) return;
    fx_hit_spark(at);
    if (nevan_hit(b, kind == HIT_SLAM || kind == HIT_LAUNCH ? 2 : !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), from_x, from_z, mortal)) {
        e->plan = P_DEATH;
        died();
    }
    e->health = (short)b->health;
    nevan_play(b->want, b->restart);
    b->restart = 0;
}

/* Dullahan state and shield logic live in dullahan.c. */
#include "dullahan.h"
static DullahanBrain dullahan_brain[ENEMY_MAX];
static int dullahan_motion_id[DULLAHAN_M_COUNT] = {-2};
static void dullahan_play(int m, int restart) {
    int id;
    if (dullahan_motion_id[0] == -2) { int k; for (k = 0; k < DULLAHAN_M_COUNT; ++k) dullahan_motion_id[k] = motion_find(dullahan_motion_names[k]); }
    id = dullahan_motion_id[m];
    if (id < 0) return;
    if (id != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = id; e->frame = 0.0f; e->done = 0;
    }
}
static void dullahan_spawn_glue(void) {
    DullahanBrain *b = &dullahan_brain[e - enemies];
    e->motion = -1;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    dullahan_spawn(b, e->health);
    giga_note("dullahan spawn health", e->health, 0.0f);   /* tests */
    dullahan_play(b->want, 1);
    b->restart = 0;
    e->fade = 0.0f;
}
static void dullahan_update_glue(float f, float px, float py, float pz) {
    DullahanBrain *b = &dullahan_brain[e - enemies];
    const Motion *m;
    float g;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) {
            e->done = 1;
            e->frame = dullahan_loops(b->want) ? motion_wrap(m, e->frame) : m->end_frame;
        }
    }
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    b->frame = e->frame; b->done = e->done;
    dullahan_think(b, f, px, py, pz, ai_unit, attacks_on && e->plan != P_DEATH);
    e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    if (floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    if (b->restart || dullahan_motion_id[b->want] != e->motion) { dullahan_play(b->want, b->restart); b->restart = 0; }
    if (b->land) { land_attack(b->land == 2); landed_damage = b->land_damage; giga_note("dullahan landed", b->land, (int)b->land_damage); }
    if (b->remove) e->active = 0;
}
static void dullahan_hit_glue(HitKind kind, float from_x, float from_z) {
    DullahanBrain *b = &dullahan_brain[e - enemies];
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    fx_hit_spark(at);
    if (e->plan == P_DEATH) return;
    if (dullahan_hit(b, !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), from_x, from_z, mortal)) {
        e->plan = P_DEATH;
        died();
    }
    dullahan_play(b->want, b->restart);
    b->restart = 0;
}

/* Separate Bishop caster brain. */
#include "bishop.h"
static BishopBrain bishop_brain[ENEMY_MAX];
static int bishop_motion_id[BISHOP_M_COUNT] = {-2};
static void bishop_play(int m, int restart) {
    int id;
    if (bishop_motion_id[0] == -2) { int k; for (k = 0; k < BISHOP_M_COUNT; ++k) bishop_motion_id[k] = motion_find(bishop_motion_names[k]); }
    id = bishop_motion_id[m];
    if (id < 0) return;
    if (id != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = id; e->frame = 0.0f; e->done = 0;
    }
}
static void bishop_spawn_glue(void) {
    BishopBrain *b = &bishop_brain[e - enemies];
    e->motion = -1;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    bishop_spawn(b, e->health);
    bishop_play(b->want, 1);
    b->restart = 0;
    e->fade = 0.0f;
}
static void bishop_update_glue(float f, float px, float py, float pz) {
    BishopBrain *b = &bishop_brain[e - enemies];
    const Motion *m;
    float g;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) {
            e->done = 1;
            e->frame = bishop_loops(b->want) ? motion_wrap(m, e->frame) : m->end_frame;
        }
    }
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    b->frame = e->frame; b->done = e->done;
    bishop_think(b, f, px, py, pz, ai_unit, attacks_on && e->plan != P_DEATH);
    e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    if (floor_under(e->pos[0], e->pos[1] + 100.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    if (b->restart || bishop_motion_id[b->want] != e->motion) { bishop_play(b->want, b->restart); b->restart = 0; }
    if(b->cast){float from[3]={b->pos[0],b->pos[1]+180,b->pos[2]},to[3]={b->target[0],b->target[1]+30,b->target[2]};
        if(b->cast==1)fx_guard_flash(to);else {fx_tracer(from,to,0xff6070ffu,8);fx_explosion(to);}}
    if (b->land) {land_attack(1);landed_damage=700;}
    if (b->remove) e->active = 0;
}
static void bishop_hit_glue(HitKind kind, float from_x, float from_z) {
    BishopBrain *b = &bishop_brain[e - enemies];
    float at[3] = {e->pos[0], e->pos[1] + 60.0f, e->pos[2]};
    fx_hit_spark(at);
    if (e->plan == P_DEATH) return;
    if (bishop_hit(b, !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), from_x, from_z, mortal)) {
        e->plan = P_DEATH;
        died();
    }
    bishop_play(b->want, b->restart);
    b->restart = 0;
}


/* ---- Beowulf (class 33, CEm033; em030.pac at GDATA 135), Mission 11's boss in 217 (docs/ENEMY_SPECS.md I), at rank 0.
 * His actions (+0x37da, dispatched by FUN_002d99f0) each play a script of his bank (em030 [8], groups 0, 1, 3;
 * FUN_002da3d0), run here by the shared script runner with his caps and attack records (tools/prepare_beowulf.py), so
 * his motions, hit windows and damage are the game's. From idle he picks by sector (FUN_002da860, tables 0x5b8c50 by
 * phase: the first sector Dante stands in names a weighted list of actions), never the action he last took again
 * (FUN_002da7b0). His phase (+0x38d0) steps up below 73% and 47% of his health (0x619718), with a roar (action 0x16).
 * Health 10000 (0x5b84e0 +0x2c). His gate, movement and hit reactions are the game's (below); his feathers' flight is too (record 8). */
#define BW_SCRIPT_OF(group, script) ((group) * 16 + (script))
static const signed char bw_action_script[28] = {
    BW_SCRIPT_OF(0, 0), BW_SCRIPT_OF(0, 1), BW_SCRIPT_OF(0, 7), BW_SCRIPT_OF(0, 8), BW_SCRIPT_OF(0, 9),
    BW_SCRIPT_OF(1, 0), BW_SCRIPT_OF(1, 1), BW_SCRIPT_OF(1, 2), BW_SCRIPT_OF(1, 3), BW_SCRIPT_OF(1, 6), BW_SCRIPT_OF(1, 9),
    BW_SCRIPT_OF(0, 1),   /* 11: FUN_002da800(0, ...) unless coming from 1, the walk [its script number isn't read] */
    BW_SCRIPT_OF(3, 0), BW_SCRIPT_OF(3, 1), BW_SCRIPT_OF(3, 2), BW_SCRIPT_OF(3, 3), BW_SCRIPT_OF(3, 4), BW_SCRIPT_OF(3, 5),
    BW_SCRIPT_OF(3, 6), BW_SCRIPT_OF(3, 7), BW_SCRIPT_OF(3, 8), BW_SCRIPT_OF(3, 9), BW_SCRIPT_OF(1, 0), BW_SCRIPT_OF(0, 7),
    BW_SCRIPT_OF(3, 11), BW_SCRIPT_OF(3, 12), BW_SCRIPT_OF(3, 13), BW_SCRIPT_OF(3, 14)};
enum { BW_IDLE = 0, BW_RUN = 2, BW_STAGGER = 8, BW_DEATH = 9, BW_WALK = 11, BW_ROAR = 0x16 };
typedef struct { unsigned char action, weight; } BwPick;
typedef struct { float angle, half, reach; BwPick picks[5]; } BwSector;   /* degrees; half 0: any side; reach 0: any */
static const BwSector bw_sectors[3][3] = {
    {{0.0f, 120.0f, 300.0f, {{16, 70}, {11, 30}}}, {180.0f, 120.0f, 400.0f, {{17, 70}, {11, 30}}}, {0.0f, 0.0f, 0.0f, {{11, 100}}}},
    {{0.0f, 0.0f, 550.0f, {{19, 70}, {2, 30}}}, {0.0f, 0.0f, 0.0f, {{2, 100}}}, {0.0f, 0.0f, -1.0f, {{0, 0}}}},
    {{0.0f, 0.0f, 400.0f, {{11, 35}, {19, 30}, {27, 35}}}, {0.0f, 0.0f, 600.0f, {{2, 15}, {19, 20}, {27, 25}, {18, 20}, {15, 20}}},
     {0.0f, 0.0f, 0.0f, {{2, 20}, {15, 50}, {27, 30}}}}};
static const float bw_phase_at[2] = {0.73f, 0.47f};
#define BW_FEATHERS 12
static struct { int on, state, index, homing, turn; float pos[3], dir[3], speed, wait, home, life, born; } bw_feathers[BW_FEATHERS];
static int bw_phase, bw_last, bw_gauge;
static float bw_shots;   /* his shot tally (+0x38d4) */
static int bw_shot_once;             /* his first shot hit (+0x3903) */
static float bw_gate;   /* his attack gate (+0x130; block 0x5b8900): opens past 300 (difficulty under 369) */
static float bw_wind;
#define BW_TURN_FAST (1274.0f / 65536.0f * 6.2831853f)   /* 0x619680 */
#define BW_TURN_MID (637.0f / 65536.0f * 6.2831853f)     /* 0x619688 */
#define BW_TURN_SLOW (218.0f / 65536.0f * 6.2831853f)    /* 0x619698 */

static int bw_init(void) {
    int i, found = 0;
    for (i = 0; i < BW_MOTIONS; ++i) {
        char name[5] = {'b', 'w', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        bw_motion_map[i] = motion_find(name);
        found += bw_motion_map[i] >= 0;
    }
    return found > 0;
}

static void bw_go(int action) {
    giga_note("beowulf", action, (float)e->health);
    e->action = (unsigned char)action;
    e->timer = 0.0f;
    e->wake = 0.0f;
    e->step = 0;
    script_play(bw_action_script[action]);
}

static void beowulf_spawn_glue(void) {
    float g;
    static int ready = -1;
    if (ready < 0) ready = bw_init();
    e->motion = -1;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    e->plan = P_WATCH;
    bw_phase = 0; bw_last = -1; bw_gauge = 0; bw_gate = 0.0f; bw_shots = 0.0f; bw_shot_once = 0;
    memset(bw_feathers, 0, sizeof bw_feathers);
    bw_go(BW_IDLE);
    e->fade = 0.0f;
}

/* FUN_002da860 / FUN_00359f20 / FUN_002da7b0: the first sector Dante stands in, a weighted pick, not the last again. */
static int bw_choose(float d, float rel_deg) {
    int s, k, total = 0, pick = BW_IDLE;
    for (s = 0; s < 3; ++s) {
        const BwSector *z = &bw_sectors[bw_phase][s];
        float off = rel_deg - z->angle;
        if (z->reach < 0.0f) break;
        while (off > 180.0f) off -= 360.0f;
        while (off < -180.0f) off += 360.0f;
        if (z->reach > 0.0f && d > z->reach) continue;
        if (z->half > 0.0f && fabsf(off) > z->half) continue;
        for (k = 0; k < 5 && z->picks[k].weight; ++k) total += z->picks[k].weight;
        if (!total) continue;
        {
            int r = (int)(ai_unit() * (float)total), acc = 0;
            for (k = 0; k < 5 && z->picks[k].weight; ++k) { acc += z->picks[k].weight; if (r < acc) { pick = z->picks[k].action; break; } }
            if (pick == bw_last)
                for (k = 0; k < 5 && z->picks[k].weight; ++k) if (z->picks[k].action != bw_last) { pick = z->picks[k].action; break; }
        }
        break;
    }
    return pick;
}

/* The feathers (CEm025's feather, vtable 0x60c750: FUN_0040cef0 made by FUN_002dcf00 at his wing bones, slots 6-12 then
 * 14-16 of his bone list; set up by FUN_0040d160, run by FUN_0040d380). Each waits its index x 3 frames (FUN_0040d130),
 * then drifts out from his body at 0x5b8610's speed 2, steering 0xb4 a frame toward Dante (FUN_0025a7d0); once he lets
 * them go [his next event, or his volley's end: inferred], its index x 10 frames (0x619910), then 0x5b8630's flight:
 * 40 a frame, steering 0x258 a frame for 80 frames or until within 300 of him, gone after 120. Record 8 (360) on a
 * touch (its spheres 20 / 30). */
static float bw_wing_released;
static int bw_wing_bone(int bone, float out[3]) {
    float l[3], c = cosf(e->yaw), s = sinf(e->yaw);
    if (bone >= enemy_models[e->kind].bone_count) return 0;
    evaluate_pose();
    l[0] = world_pos[bone][0]; l[1] = world_pos[bone][1]; l[2] = world_pos[bone][2];
    out[0] = e->pos[0] + l[0] * c + l[2] * s;
    out[1] = e->pos[1] + l[1];
    out[2] = e->pos[2] + l[2] * c - l[0] * s;
    return 1;
}

/* Turn dir toward want by up to a radians (FUN_0025a9d0's steer). */
static void bw_steer(float dir[3], const float want[3], float a) {
    float d = dir[0] * want[0] + dir[1] * want[1] + dir[2] * want[2], ang, perp[3], l;
    int k;
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    ang = acosf(d);
    if (ang <= a) { for (k = 0; k < 3; ++k) dir[k] = want[k]; return; }
    for (k = 0; k < 3; ++k) perp[k] = want[k] - dir[k] * d;
    l = sqrtf(perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2]);
    if (l < 1e-5f) return;
    for (k = 0; k < 3; ++k) dir[k] = dir[k] * cosf(a) + perp[k] / l * sinf(a);
}

static void bw_feathers_update(float f, float px, float py, float pz) {
    int i, k;
    for (i = 0; i < BW_FEATHERS; ++i) {
        float want[3], dist, dx, dy, dz, near_y, r;
        if (!bw_feathers[i].on) continue;
        if (bw_feathers[i].state == 0) {   /* FUN_0040d130 */
            if ((bw_feathers[i].wait -= f) < 0.0f) { bw_feathers[i].state = 1; bw_feathers[i].speed = 2.0f; bw_feathers[i].turn = 0xb4;
                bw_feathers[i].home = 300.0f; bw_feathers[i].life = 1000.0f; bw_feathers[i].homing = 1; }
            continue;
        }
        if ((bw_feathers[i].life -= f) < 0.0f) { bw_feathers[i].on = 0; continue; }
        if (bw_feathers[i].state == 1 && (bw_wing_released > bw_feathers[i].born || e->action != 15 || e->flags[0] != 10)) {
            bw_feathers[i].state = 2; bw_feathers[i].wait = (float)bw_feathers[i].index * 10.0f;
        } else if (bw_feathers[i].state == 2 && (bw_feathers[i].wait -= f) < 0.0f) {
            bw_feathers[i].state = 3; bw_feathers[i].speed = 40.0f; bw_feathers[i].turn = 0x258;   /* 0x5b8630 */
            bw_feathers[i].home = 80.0f; bw_feathers[i].life = 120.0f;
        }
        want[0] = px - bw_feathers[i].pos[0]; want[1] = py + 100.0f - bw_feathers[i].pos[1]; want[2] = pz - bw_feathers[i].pos[2];
        dist = sqrtf(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
        bw_feathers[i].home -= f;
        if (bw_feathers[i].homing && (bw_feathers[i].home <= 0.0f || dist <= 300.0f)) bw_feathers[i].homing = 0;   /* FUN_0025ad70 */
        if (bw_feathers[i].homing && dist > 1.0f) {
            for (k = 0; k < 3; ++k) want[k] /= dist;
            bw_steer(bw_feathers[i].dir, want, (float)bw_feathers[i].turn * (2.0f * PI / 65536.0f) * f);
        }
        for (k = 0; k < 3; ++k) bw_feathers[i].pos[k] += bw_feathers[i].dir[k] * bw_feathers[i].speed * f;
        if (bw_feathers[i].pos[1] < e->floor_y) { bw_feathers[i].on = 0; continue; }
        near_y = bw_feathers[i].pos[1] < py + DANTE_LOW ? py + DANTE_LOW : bw_feathers[i].pos[1] > py + DANTE_HIGH ? py + DANTE_HIGH : bw_feathers[i].pos[1];
        dx = bw_feathers[i].pos[0] - px; dy = bw_feathers[i].pos[1] - near_y; dz = bw_feathers[i].pos[2] - pz;
        r = 30.0f + DANTE_RADIUS;
        if (attacks_on && dx * dx + dy * dy + dz * dz < r * r) {
            bw_feathers[i].on = 0;
            land_attack(0);
            landed_damage = bw_attacks[8].damage;
            landed_x = bw_feathers[i].pos[0]; landed_z = bw_feathers[i].pos[2];
            continue;
        }
        {
            float still[3] = {0.0f, 0.0f, 0.0f};
            fx_mote(bw_feathers[i].pos, still, 50.0f, 6.0f, 0.1f, 0xffc0f0ffu);
        }
    }
}

/* FUN_0040cef0(bone, index, him): at his bone, heading out from his body. */
static void bw_feather(int bone, int index) {
    int i, k;
    for (i = 0; i < BW_FEATHERS; ++i) {
        float l;
        if (bw_feathers[i].on) continue;
        if (!bw_wing_bone(bone, bw_feathers[i].pos)) return;
        bw_feathers[i].on = 1;
        bw_feathers[i].state = 0;
        bw_feathers[i].index = index;
        bw_feathers[i].wait = (float)index * 3.0f;
        bw_feathers[i].born = bw_wing_released;
        bw_feathers[i].dir[0] = bw_feathers[i].pos[0] - e->pos[0];
        bw_feathers[i].dir[1] = bw_feathers[i].pos[1] - (e->pos[1] + 200.0f);
        bw_feathers[i].dir[2] = bw_feathers[i].pos[2] - e->pos[2];
        l = sqrtf(bw_feathers[i].dir[0] * bw_feathers[i].dir[0] + bw_feathers[i].dir[1] * bw_feathers[i].dir[1] + bw_feathers[i].dir[2] * bw_feathers[i].dir[2]);
        if (l < 1.0f) { bw_feathers[i].dir[0] = 0.0f; bw_feathers[i].dir[1] = 1.0f; bw_feathers[i].dir[2] = 0.0f; }
        else for (k = 0; k < 3; ++k) bw_feathers[i].dir[k] /= l;
        return;
    }
}
static void beowulf_update_glue(float f, float px, float py, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), g, root[3], rel;
    int a = e->action;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    script_event = 0;
    script_tick(f, root);
    e->timer += f;
    e->wake += f;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->floor_y = g;
    e->pos[1] = e->floor_y;
    bw_feathers_update(f, px, py, pz);
    if (e->plan == P_DEATH) {
        if (e->done && e->timer > 330.0f) e->active = 0;
        return;
    }
    rel = wrap_angle(atan2f(dx, dz) - e->yaw) * (180.0f / PI);
    /* His attack gate (FUN_0032b450, block 0x5b8900's rows, the first that holds him): 20 a frame within 500, else 5
     * within 3000 and 30 degrees of his facing; ticked in his idle and action 1 (FUN_002da960 / 002daa00). */
    if (a > 1) ;
    else if (d < 500.0f) bw_gate += 20.0f * f;
    else if (d < 3000.0f && fabsf(rel) < 30.0f) bw_gate += 5.0f * f;
    /* His phase: below 73% and 47% of his health, with a roar (action 0x16), once each. */
    if (bw_phase < 2 && a != BW_ROAR && (float)e->health < (float)(ENEMY_HEALTH * BEOWULF_MAX / 250) * bw_phase_at[bw_phase]) {
        ++bw_phase;
        bw_go(BW_ROAR);
        return;
    }
    switch (a) {
    case 0: case 1:
        if (ar_force >= 0) { int st = ar_force; ar_force = -1; bw_go(st); return; }   /* tests: "arforce frame state" */
        /* 1 (FUN_002daa00) turns to him at 0x619680 (1274 a frame) and lasts at least 300 frames (0x619674). */
        if (a == 1) {
            goyle_turn_to(px, pz, BW_TURN_FAST, f);
            if (e->done && e->timer >= 300.0f) { bw_go(BW_IDLE); return; }
        } else goyle_turn_to(px, pz, 0.04f, f);
        if (bw_gate > 300.0f) { int next = bw_choose(d, rel); if (next != BW_IDLE) { bw_last = next; bw_gate = 0.0f; bw_go(next); return; } }
        break;
    case BW_WALK:
        /* FUN_002dab50: turning 1274 a frame; Dante within 550 and 100 degrees of his facing (0x5b8c64) takes him into
         * 0x18, else after 300 frames (0x619674) he stops. */
        goyle_turn_to(px, pz, BW_TURN_FAST, f);
        move_local(root[0], 0.0f, root[2]);
        if (d < 550.0f && fabsf(rel) < 100.0f) { bw_go(0x18); return; }
        if (e->timer > 300.0f) { bw_go(BW_IDLE); return; }
        break;
    case BW_RUN:
        /* FUN_002dade0: turning 637 a frame (0x619688) for 30 frames (0x619690), then 218 (0x619698); after 180
         * (0x619694), or Dante beyond 3000 (0x5b8cb4), it stops (3) [at its next stride's event in the game]. */
        goyle_turn_to(px, pz, e->timer < 30.0f ? BW_TURN_MID : BW_TURN_SLOW, f);
        move_local(root[0], 0.0f, root[2]);
        if (e->timer > 210.0f || d > 3000.0f) { bw_go(3); return; }
        break;
    case 0x17:
        goyle_turn_to(px, pz, BW_TURN_MID, f);
        move_local(root[0], 0.0f, root[2]);
        if (d < 300.0f || e->timer > 240.0f) { bw_go(BW_IDLE); return; }
        break;
    default:
        if (e->timer < 20.0f) goyle_turn_to(px, pz, 0.06f, f);
        move_local(root[0], 0.0f, root[2]);
        if (a == 15) {
            /* FUN_002dcf00 reads his body player's flags: flag 1 lets 0x5b8650's + 1 feathers go from wing bones 6 on
             * (7 at rank 0), then again 0x5b8660's + 1 from 14 (3); flag 4 a wind that pushes Dante away 4.7 a frame
             * for 190 frames. A feather waiting to fly goes at flag 1, or when flag 0 isn't 10 (his volley over). */
            if (e->flags[1] == 1) {
                int k, n = e->step ? 3 : 7, first = e->step ? 14 : 6;
                e->flags[1] = 0;
                bw_wing_released += 1.0f;
                for (k = 0; k < n; ++k) bw_feather(first + k, k);
                e->step = 1;
            }
            if (e->flags[4] == 1) { e->flags[4] = 0; bw_wind = 190.0f; }
        }
        if (a == BW_STAGGER && e->done) { bw_go(bw_phase ? 1 : BW_IDLE); return; }   /* 0x619670[phase]: 0, 1, 1 */
        if (e->done) { bw_go(BW_IDLE); return; }
        break;
    }
    if (bw_wind > 0.0f) {
        float l = d > 1.0f ? d : 1.0f;
        bw_wind -= f;
        enemy_wind[0] += dx / l * 4.7f * f; enemy_wind[1] += dz / l * 4.7f * f;
    }
    keep_apart(px, py, pz, f);
    if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
}

static void beowulf_hit_glue(HitKind kind, float from_x, float from_z) {
    float at[3] = {e->pos[0], e->pos[1] + 200.0f, e->pos[2]};
    int heavy = !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL);
    if (e->plan == P_DEATH) return;
    fx_hit_spark(at);
    if (mortal) {
        e->health -= heavy ? 2 : 1;
        if (e->health <= 0) {
            e->health = 0;
            e->plan = P_DEATH;
            bw_go(BW_DEATH);
            e->timer = 0.0f;
            died();
            return;
        }
    }
    /* FUN_002df8e0, his hits (super armour otherwise). Not while he's in 5-7 already, nor dying:
     * - a knock-down record (kind 4) of over 600 sets his blade tally (+0x38d8) to its limit, 100000 (0x61966c): action 5
     *   [the port's heavy, slam and launch taken as that record];
     * - shots add to +0x38d4 (their damage), and past 1500 (0x619668) action 6 unless he's in his last phase; his first
     *   shot hit plays 10 (+0x3903), later ones half the time (0x619724);
     * - a blade from within 40 degrees in front staggers him (8) 30 / 20 / 10% of the time by phase (0x5b8ce0). */
    if (e->action >= 5 && e->action <= 7) return;
    if (kind == HIT_PISTOL) {
        bw_shots += 10000.0f / 240.0f;   /* a port bullet's share of his health, in his units */
        if (bw_shots >= 1500.0f && bw_phase != 2) { bw_shots = 0.0f; bw_go(6); }
        else if (!bw_shot_once) { bw_shot_once = 1; bw_go(10); }
        else if (ai_unit() * 100.0f < 50.0f && e->action <= 2) bw_go(10);
        return;
    }
    if (kind == HIT_HEAVY || kind == HIT_SLAM || kind == HIT_LAUNCH) { bw_go(5); return; }
    if (e->action != BW_STAGGER) {
        float rel = wrap_angle(atan2f(from_x - e->pos[0], from_z - e->pos[2]) - e->yaw);
        static const float chance[3] = {30.0f, 20.0f, 10.0f};
        if (fabsf(rel) < 0x1c72 * (2.0f * PI / 65536.0f) && ai_unit() * 100.0f < chance[bw_phase]) bw_go(BW_STAGGER);
    }
}

/* ---- The Soul Eater (class 16, CEm016; EST 34): Missions 11 and 15. From its class's code (docs/ENEMY_SPECS.md H):
 * per frame FUN_0049b7b0, its actions FUN_004325d0-FUN_00433fd0 (+5), each playing its own script (FUN_0049cdf0;
 * tools/prepare_soul_eater.py), timed by its action table (FUN_0049ce50), turning by it (FUN_0049cf80), deciding by its
 * one list (FUN_0049dea0: at rank 0 in Dante's view it hovers (0), else it comes for him (1)).
 * Its trick: while he looks its way (FUN_00359b40, his facing) its sight gauge runs down (at half speed at rank 0) and
 * it fades out (+0x4c4 down); out of his view the gauge is 30 and it shows (alpha 4 a frame of it, to 120 of 128). It
 * hovers 20-80 over him (+0x4b4, re-rolled every 50-200 frames), moving a twentieth of the difference a frame (down at
 * most 3, up at most 1) in its hovering actions (0x10000000).
 * Its bite holds Dante (FUN_0021f2e0, combat_hold) and its hold (7) drains his Devil Trigger (FUN_0021eae0: 20 a frame,
 * 1000 a rune [inferred]) until he struggles 60 points free (8) or its time's up (9, the burst).
 * Stand-ins: he isn't lifted or turned with it (FUN_0049dac0's states: up 15 a frame less 0.64, for 18 frames); the
 * struggle's count isn't reset as the hold starts (FUN_00254a10 in 7); the rank-0 teleport (4) reappears ahead of his
 * facing, not the camera's; 16 (a dodge when he does move 0x11 near it) isn't triggered. */
enum { SE_HOVER = 0, SE_COME = 1, SE_KNOCK = 2, SE_KNOCK2 = 3, SE_BLINK = 4, SE_CLOSE = 5, SE_BITE = 6, SE_HOLD = 7,
       SE_FREED = 8, SE_BURST = 9, SE_SPIN = 12, SE_ARRIVE = 13, SE_LATCH = 14, SE_SHOWN = 15, SE_DEATH = 30 };
static float se_dante_yaw, se_dt_drain;
void enemy_set_dante_yaw(float yaw) { se_dante_yaw = yaw; }
float enemy_take_dt_drain(void) { float d = se_dt_drain; se_dt_drain = 0.0f; return d; }

static int se_init(void) {
    int i, found = 0;
    for (i = 0; i < SE_MOTIONS; ++i) {
        char name[5] = {'s', 'e', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        se_motion_map[i] = motion_find(name);
        found += se_motion_map[i] >= 0;
    }
    return found > 0;
}

/* FUN_0049ce50: this action's time, rank 0. */
static float se_time(int a) { return se_actions[a].time[0] + se_actions[a].rand[0] * ai_unit(); }

/* FUN_0049cdb0 / FUN_0049cdf0: a new action, its own script. */
static void se_go(int a) {
    giga_note("soul", a, (float)e->health);
    e->action = (unsigned char)a;
    e->step = 0;
    e->wake = 0.0f;
    e->timer = 0.0f;
    script_play(a == SE_DEATH ? 2 : a == SE_ARRIVE ? 13 : a);
}

/* FUN_00359b40: is it within his facing's cone (+-60 degrees) [the cone's width is read as 0x5555 either way]. */
static int se_seen(float px, float pz) {
    float d = wrap_angle(atan2f(e->pos[0] - px, e->pos[2] - pz) - se_dante_yaw);
    return fabsf(d) < PI / 3.0f;
}

/* FUN_0049dea0 at rank 0: in his view, hover (0); out of it, come for him (1). */
static void se_decide(float px, float pz) {
    int next = se_seen(px, pz) ? SE_HOVER : SE_COME;
    if (next != e->action) se_go(next);
    else e->timer = se_time(next), e->step = 1;
}

/* FUN_0042e0c0: its local velocity along its facing. */
static void se_move(float f) { move_local(e->se_vel[0] * f, e->se_vel[1] * f, e->se_vel[2] * f); }

static void soul_spawn_glue(void) {
    float g;
    static int ready = -1;
    if (ready < 0) ready = se_init();
    e->motion = -1;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->floor_y = g;
    e->pos[1] = e->floor_y + 100.0f;
    e->plan = P_WATCH;
    e->se_sight = 30.0f; e->se_show = 0.0f; e->se_lift = 50.0f; e->se_lift_wait = 0.0f;
    e->se_vel[0] = e->se_vel[1] = e->se_vel[2] = 0.0f;
    e->se_charges = 0; e->se_hit_pending = 0;
    e->veil = 1.0f;
    se_go(SE_ARRIVE);   /* FUN_0049ad90, spawn type 0: action 13 */
    e->fade = 0.0f;
}

static void soul_update_glue(float f, float px, float py, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz), root[3];
    int a = e->action, hover = 0, steady = 0, held = 0;
    float turn = (float)se_actions[a < SE_SCRIPTS ? a : 0].turn[0] * (PI / 180.0f);
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    script_event = 0;
    script_tick(f, root);
    e->timer -= f;
    if (a == SE_DEATH) {
        /* FUN_0049c2a0: script 2, spinning 0x300 a frame, falling, fading over 30 frames, then gone. */
        e->yaw = wrap_angle(e->yaw + 0.0736f * f);
        e->se_vel[1] -= 0.5f * f;
        se_move(f);
        e->veil = 1.0f - (e->timer > 0.0f ? e->timer : 0.0f) * 4.0f / 128.0f;
        if (e->timer <= 0.0f) e->active = 0;
        return;
    }
    if (e->se_hit_pending && (a == SE_HOVER || a == SE_COME)) { e->se_hit_pending = 0; se_go(SE_SHOWN); a = SE_SHOWN; }
    switch (a) {
    case SE_HOVER:
        hover = 1;
        if (e->step == 0) { e->timer = se_time(a); e->step = 1; e->wake = 0.0f; }
        goyle_turn_to(px, pz, turn, f);
        if (e->timer <= 0.0f) {
            if ((e->wake += 1.0f) > 2.0f) { se_go(SE_BLINK); return; }
            se_decide(px, pz);
        }
        break;
    case SE_COME:
        hover = 1;
        if (e->step == 0) {
            /* FUN_00432730: 12 a frame beyond 2000, 8 beyond 1000, else 4. */
            e->se_vel[0] = e->se_vel[1] = 0.0f;
            e->se_vel[2] = d > 2000.0f ? 12.0f : d > 1000.0f ? 8.0f : 4.0f;
            e->timer = se_time(a); e->step = 1;
        }
        goyle_turn_to(px, pz, turn, f);
        se_move(f);
        if (d < 200.0f) { se_go(SE_CLOSE); return; }
        if (e->timer <= 0.0f) se_decide(px, pz);
        break;
    case SE_KNOCK: case SE_KNOCK2:
        /* FUN_00432930: back at 20, easing to 0 by 2 a frame; then the blink. */
        steady = 1;
        if (e->step == 0) { e->se_vel[0] = e->se_vel[1] = 0.0f; e->se_vel[2] = -20.0f; e->step = 1; }
        e->se_vel[2] += 2.0f * f; if (e->se_vel[2] > 0.0f) e->se_vel[2] = 0.0f;
        se_move(f);
        if (e->done) { se_go(SE_BLINK); return; }
        break;
    case SE_BLINK:
        /* FUN_00432ad0: out over 10 frames, then (FUN_0049d6e0) a point 400-900 cm from him at his floor + 100,
         * half the way there a frame for 10 frames, then back over 30, and a new decision. */
        steady = 1;
        if (e->step == 0) { e->timer = 10.0f; e->step = 1; }
        if (e->step == 1) {
            e->veil = 1.0f - (e->timer > 0.0f ? e->timer : 0.0f) / 10.0f;
            if (e->timer <= 0.0f) {
                float ang = se_dante_yaw + (ai_unit() - 0.5f) * (PI / 4.0f), r = 400.0f + 500.0f * ai_unit(), g;
                e->se_to[0] = px + sinf(ang) * r; e->se_to[2] = pz + cosf(ang) * r;
                e->se_to[1] = (floor_under(px, py + 100.0f, pz, &g) ? g : py) + 100.0f;
                e->timer = 10.0f; e->step = 2; e->veil = 1.0f;
            }
        } else if (e->step == 2) {
            int k;
            for (k = 0; k < 3; ++k) e->pos[k] += (e->se_to[k] - e->pos[k]) * 0.5f;
            if (e->timer <= 0.0f) { for (k = 0; k < 3; ++k) e->pos[k] = e->se_to[k]; e->timer = 0.0f; e->step = 3; }
        } else {
            goyle_turn_to(px, pz, turn, f);
            e->veil = 1.0f + e->timer / 30.0f;   /* timer runs from 0 down: in over 30 */
            if (e->timer <= -30.0f) { e->veil = 0.0f; e->se_show = 0.0f; se_decide(px, pz); }
        }
        return;   /* its own alpha while it blinks */
    case SE_CLOSE:
        /* FUN_00432e30: under 100 it backs off, rising 3 a frame, for 60 frames; beyond 500 back to 1; else, its
         * time up, the bite. */
        hover = 1;
        if (e->step == 0) { e->se_vel[0] = e->se_vel[1] = e->se_vel[2] = 0.0f; e->timer = se_time(a); e->step = 1; }
        if (e->step == 1) {
            goyle_turn_to(px, pz, turn, f);
            if (d < 100.0f) { e->timer = 60.0f; e->se_vel[2] = -12.0f; e->step = 2; }
            else if (d > 500.0f) { se_go(SE_COME); return; }
            else if (e->timer <= 0.0f) { se_go(SE_BITE); return; }
        } else {
            e->se_lift += 3.0f * f;
            goyle_turn_to(px, pz, -turn, f);
            se_move(f);
            if (e->timer <= 0.0f) { se_decide(px, pz); return; }
        }
        break;
    case SE_BITE: {
        /* FUN_00433000: it dives (its hover 6 a frame lower); 23 frames in, its mouth within 150 of his middle takes
         * him (FUN_0021f2e0 → 14, the latch); its script done, 12. */
        steady = 1;
        e->se_lift -= 6.0f * f;
        e->pos[1] = py + e->se_lift > e->floor_y ? py + e->se_lift : e->floor_y;
        if (e->step == 0) { e->timer = 23.0f; e->step = 1; }
        if (e->step == 1 && e->timer <= 0.0f) {
            float mx = e->pos[0] + sinf(e->yaw) * 60.0f - px, my = e->pos[1] + 120.0f - (py + 100.0f), mz = e->pos[2] + cosf(e->yaw) * 60.0f - pz;
            e->step = 2;
            /* Held (not if he's held already: FUN_0021f2e0 refuses): 60 struggle points free him (FUN_004331b0's FUN_00254a10). */
            if (attacks_on && !hold_owner && mx * mx + my * my + mz * mz < 150.0f * 150.0f) { hold_take(60); se_go(SE_LATCH); return; }
        }
        if (e->done) { se_go(SE_SPIN); return; }
        break;
    }
    case SE_LATCH:
        /* FUN_00433930: on his head, its script, then the hold. */
        held = 1;
        if (hold_freed()) { se_go(SE_FREED); return; }
        if (e->done) { se_go(SE_HOLD); return; }
        break;
    case SE_HOLD:
        /* FUN_004331b0: draining his Devil Trigger 20 a frame for its time (240 + 60 at rank 0); freed, 8; its time up,
         * 9, the burst. */
        held = 1;
        if (hold_freed()) { se_go(SE_FREED); return; }   /* FUN_00254a20: past 60, 8 */
        if (e->step == 0) { e->timer = se_time(a); e->step = 1; }
        se_dt_drain += 20.0f * f;
        if (e->timer <= 0.0f) { se_go(SE_BURST); return; }
        break;
    case SE_FREED:
        /* FUN_00433310: thrown off, its script, then hovering. */
        steady = 1;
        if (e->done) { se_go(SE_HOVER); return; }
        break;
    case SE_BURST:
        /* FUN_004333c0: its script's record 1 (3000) on him, gone 30 frames later. */
        held = 1;
        if (e->step == 0) { e->timer = 30.0f; e->step = 1; hold_let_go(1); }
        if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
        if (e->timer <= 0.0f) { e->plan = P_DEATH; e->active = 0; }
        break;
    case SE_SPIN:
        /* FUN_004336e0: its charges spent (none at rank 0), back to hovering when its script's done. */
        steady = 1;
        if (e->done) { se_go(SE_HOVER); return; }
        break;
    case SE_ARRIVE:
        /* FUN_004337f0: in over 100 frames to half shown, turning to him; at 120 a decision. */
        steady = 1;
        goyle_turn_to(px, pz, turn, f);
        e->veil = 1.0f - (-e->timer) * 0.64f / 100.0f;
        if (-e->timer >= 120.0f) { e->veil = 0.0f; e->se_show = 15.0f; se_decide(px, pz); }
        return;
    case SE_SHOWN:
        /* FUN_00433a70: a hit shows it: after 0-80 frames fully shown, its script at 1.5 speed, then the blink. */
        if (e->step == 0) { e->timer = 80.0f * ai_unit(); e->step = 1; }
        if (e->step == 1) { if (e->timer <= 0.0f) { e->se_sight = 30.0f; e->se_show = 30.0f; e->step = 2; script_play(15); } }
        else { goyle_turn_to(px, pz, turn, f); if (e->done) { se_go(SE_BLINK); return; } }
        hover = 1;
        break;
    default:
        if (e->done) se_go(SE_HOVER);
        break;
    }
    if (held) {
        /* FUN_0049dd80: on his head, 5 ahead of it. */
        e->pos[0] = px + sinf(se_dante_yaw) * 5.0f; e->pos[1] = py + 120.0f; e->pos[2] = pz + cosf(se_dante_yaw) * 5.0f;
        e->yaw = se_dante_yaw;
        e->veil = 0.0f;
        return;
    }
    /* Its hover (0x10000000) and its re-rolled height. */
    if ((e->se_lift_wait -= f) < 0.0f) { e->se_lift_wait = 50.0f + 150.0f * ai_unit(); e->se_lift = 20.0f + 60.0f * ai_unit(); }
    if (hover) {
        float k = (e->pos[1] - (py + e->se_lift)) / 20.0f;
        if (k > 3.0f) k = 3.0f;
        if (k < -1.0f) k = -1.0f;
        e->pos[1] -= k * f;
    }
    (void)steady;
    /* Its sight gauge and how shown it is (FUN_0049b7b0), rank 0's halves. */
    if (se_seen(px, pz)) { e->se_sight -= 0.5f * f; if (e->se_sight < 0.0f) e->se_sight = 0.0f; }
    else e->se_sight = 30.0f;
    if (e->se_sight >= 30.0f) { e->se_show += f; if (e->se_show > 30.0f) e->se_show = 30.0f; }
    else { e->se_show -= 0.5f * f; if (e->se_show < 0.0f) e->se_show = 0.0f; }
    e->veil = 1.0f - e->se_show * 4.0f / 128.0f;
    keep_apart(px, py, pz, f);
    if (e->hit_left[0] > 0.0f || e->hit_left[1] > 0.0f) test_attack(px, py, pz);
}

static void soul_hit_glue(HitKind kind) {
    float at[3] = {e->pos[0], e->pos[1] + 130.0f, e->pos[2]};
    int heavy = !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL);
    if (e->plan == P_DEATH || e->action == SE_BURST) return;
    fx_hit_spark(at);
    e->se_show = 30.0f; e->se_sight = 30.0f;
    if (mortal) {
        e->health -= heavy ? 2 : 1;
        if (e->health <= 0) {
            e->health = 0;
            e->plan = P_DEATH;
            se_go(SE_DEATH);
            hold_let_go(1);
            e->timer = 30.0f;
            e->se_vel[0] = e->se_vel[1] = e->se_vel[2] = 0.0f;
            died();
            return;
        }
    }
    if (heavy && e->action != SE_BLINK) { se_go(SE_KNOCK); return; }
    e->se_hit_pending = 1;   /* FUN_0049b7b0: a hit while hovering or coming shows it (15) */
}

/* ---- The Fallen (fallen.c decides; here his body, his spear and the shared attack turn). */
#include "fallen.h"
extern int testroom_frame;
static FallenBrain fallen_brain[ENEMY_MAX];
static int fallen_motion_id[FALLEN_M_COUNT] = {-2};
static float fallen_rest;   /* FUN_003442c0: the enemy manager lets one attack, then rests 3 s */
static void fallen_play(int m, int restart) {
    int id;
    if (fallen_motion_id[0] == -2) { int k; for (k = 0; k < FALLEN_M_COUNT; ++k) fallen_motion_id[k] = motion_find(fallen_motion_names[k]); }
    id = fallen_motion_id[m];
    if (id < 0) return;
    if (id != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 8.0f;
        e->motion = id; e->frame = 0.0f; e->done = 0;
    }
}
static void fallen_spawn_glue(void) {
    FallenBrain *b = &fallen_brain[e - enemies];
    e->motion = -1;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1] + 100.0f; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    b->floor_y = e->floor_y;
    fallen_spawn(b, e->health);
    fallen_play(b->want, 1);
    b->restart = 0;
    e->fade = 0.0f;
}
static void fallen_update_glue(float f, float px, float py, float pz) {
    FallenBrain *b = &fallen_brain[e - enemies];
    const Motion *m;
    float g;
    int token = fallen_rest <= 0.0f, had = token;
    {
        int k;   /* the clock runs once a frame: on the first Fallen's turn */
        for (k = 0; k < ENEMY_MAX && !(enemies[k].active && enemies[k].kind == ENEMY_FALLEN); ++k) {}
        if (&enemies[k] == e && fallen_rest > 0.0f) fallen_rest -= f;
    }
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) {
            e->done = 1;
            e->frame = fallen_loops(b->want) ? motion_wrap(m, e->frame) : m->end_frame;
        }
    }
    if (floor_under(b->pos[0], b->pos[1] + 100.0f, b->pos[2], &g)) b->floor_y = g;
    b->frame = e->frame; b->done = e->done;
    fallen_think(b, f, px, py, pz, ai_unit, attacks_on && e->plan != P_DEATH, &token);
    if (had && !token) fallen_rest = 180.0f;   /* this one attacked */
    {
        static int last_state[ENEMY_MAX];   /* tests: each change of state, in VERGIL_LOG.TXT */
        int k = (int)(e - enemies);
        if (last_state[k] != b->state || b->land || b->spear || (testroom_frame % 60) == 0) {
            char line[128];
            snprintf(line, sizeof(line), "fallen %d state %#x mode %d land %d spear %d hp %d gauge %.0f rest %.0f atk %d", k, b->state, b->mode, b->land, b->spear, b->health, b->gauge, fallen_rest, attacks_on);
            vergil_boss_note(line, b->pos[0], b->pos[1], b->pos[2], py);
            last_state[k] = b->state;
        }
    }
    e->pos[0] = b->pos[0]; e->pos[1] = b->pos[1]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    e->floor_y = b->floor_y;
    e->air = fallen_flying(b);
    if (b->restart || fallen_motion_id[b->want] != e->motion) { fallen_play(b->want, b->restart); b->restart = 0; }
    if (b->land) { land_attack(b->land == 2); landed_damage = b->land_damage; }   /* its attack record's damage */
    if (b->spear) {
        /* Its spear (action 0xf's record 0), straight at his chest [approximation: the shell isn't decoded]. */
        float from[3] = {e->pos[0] + sinf(e->yaw) * 60.0f, e->pos[1] + 180.0f, e->pos[2] + cosf(e->yaw) * 60.0f};
        float to[3] = {px, py + 100.0f, pz};
        { int k = goyle_bullet(from, to); if (k >= 0) arrows[k].damage = 900.0f; }   /* record 0 (0x5c2dd0): 900 */
    }
    if (b->remove) e->active = 0;
}
static void fallen_hit_glue(HitKind kind, float from_x, float from_z) {
    FallenBrain *b = &fallen_brain[e - enemies];
    float at[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]};
    fx_hit_spark(at);
    if (e->plan == P_DEATH) return;
    if (fallen_hit(b, !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), from_x, from_z, mortal)) {
        e->plan = P_DEATH;
        died();
    }
    fallen_play(b->want, b->restart);
    b->restart = 0;
}

/* ---- Lady (lady_boss.c decides; here her body). Her health is the Enemy's own, for the boss bar. */
#include "lady_boss.h"
static LadyBrain lady_brain;
static int lady_motion_id[LADY_M_COUNT] = {-2};
static void lady_play(int m, int restart) {
    int id;
    if (lady_motion_id[0] == -2) { int k; for (k = 0; k < LADY_M_COUNT; ++k) lady_motion_id[k] = motion_find(lady_motion_names[k]); }
    id = lady_motion_id[m];
    if (id < 0) return;
    if (id != e->motion || restart) {
        e->prev_motion = e->motion; e->prev_frame = e->frame;
        e->fade = e->motion >= 0 ? 1.0f : 0.0f; e->fade_rate = 1.0f / 10.0f;
        e->motion = id; e->frame = 0.0f; e->done = 0;
    }
}
static void lady_spawn_glue(void) {
    LadyBrain *b = &lady_brain;
    float g;
    e->motion = -1;
    if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    lady_spawn(b, e->health);
    lady_play(b->want, 1);
    b->restart = 0;
    e->fade = 0.0f;
}
static void lady_update_glue(float f, float px, float py, float pz) {
    LadyBrain *b = &lady_brain;
    const Motion *m;
    float g;
    if (e->fade > 0.0f) { e->fade -= e->fade_rate * f; if (e->fade < 0.0f) e->fade = 0.0f; }
    if (e->motion >= 0) {
        float before = e->frame;
        m = motion_get(e->motion);
        e->frame += f;
        if (e->frame >= m->end_frame) {
            e->done = 1;
            e->frame = lady_loops(b->want) ? motion_wrap(m, e->frame) : m->end_frame;
        }
        if (b->air == 2 && e->frame > before) {
            /* The hop from a ledge: her motion's root travel moves her (the grapple's moves are the brain's). */
            float a[3], c[3], sn = sinf(e->yaw), cs = cosf(e->yaw);
            motion_root(m, before, a); motion_root(m, e->frame, c);
            e->pos[0] += (c[0] - a[0]) * cs + (c[2] - a[2]) * sn;
            e->pos[2] += (c[2] - a[2]) * cs - (c[0] - a[0]) * sn;
        }
    }
    if (floor_under(e->pos[0], e->pos[1] + 50.0f, e->pos[2], &g)) b->floor_y = g;
    b->pos[0] = e->pos[0]; b->pos[1] = e->pos[1]; b->pos[2] = e->pos[2]; b->yaw = e->yaw;
    b->frame = e->frame; b->done = e->done;
    {
        int before = b->action, before_step = b->step;
        {
            /* FUN_00449450: a ray from her (head height) to him; a wall or floor between means he's out of sight. */
            float from[3] = {e->pos[0], e->pos[1] + 150.0f, e->pos[2]}, to[3] = {px, py + 100.0f, pz}, t;
            int sight = !stage_loaded() || !stage_raycast(from, to, &t);
            lady_think(b, f, px, py, pz, ai_unit, attacks_on && e->plan != P_DEATH, sight);
        }
        if (b->action != before) giga_note("lady", b->action, (float)b->magazine);
        else if (b->step != before_step && b->air + (b->action == 11) + (b->action == 18) > 0) { giga_note("lady_step", b->step * 1000 + b->health, b->pos[0]); giga_note("lady_z", b->step, b->pos[2]); }
    }
    e->pos[0] = b->pos[0]; e->pos[2] = b->pos[2]; e->yaw = b->yaw;
    /* Off the floor, the bodies' settle pass treats her as airborne, over the floor below her now. */
    e->air = b->air != 0;
    if (b->air) {
        e->pos[1] = b->pos[1];
        if (floor_under(e->pos[0], e->pos[1] + 50.0f, e->pos[2], &g)) b->floor_y = g;   /* where she is now */
        e->floor_y = b->floor_y;
    }
    else if (floor_under(e->pos[0], e->pos[1] + 300.0f, e->pos[2], &g)) e->pos[1] = e->floor_y = g;
    if (b->restart || lady_motion_id[b->want] != e->motion) { lady_play(b->want, b->restart); b->restart = 0; }
    if (b->land) { land_attack(b->land == 2); landed_damage = b->land_damage; }   /* her attack record's damage */
    lady_draw_fx(b);
    if (e->plan == P_DEATH) { e->timer += f; if (e->timer > 300.0f) e->active = 0; }
}
static void lady_hit_glue(HitKind kind) {
    LadyBrain *b = &lady_brain;
    float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
    if (e->plan == P_DEATH) return;
    fx_hit_spark(at);
    if (lady_hit(b, !(kind == HIT_LIGHT || kind == HIT_AIR || kind == HIT_PISTOL), mortal)) {
        e->plan = P_DEATH;
        e->timer = 0.0f;
        died();
    }
    e->health = (short)b->health;
    lady_play(b->want, b->restart);
    b->restart = 0;
}

/* ---- The Jester's projectiles, drawn (their logic is jester.c's): motes, as the game's effects aren't ported.
 * The ball glows in his hand and in flight; a trap shows a ring where it lies, and bursts as it springs (55-65);
 * the cross's four arms are columns of light swept round where he stood. */
int jester_projectiles_init(void) { return 1; }
void jester_projectiles_draw(const JesterProjectiles *w) {
    static const float still[3] = {0.0f, 0.0f, 0.0f};
    int i, k;
    for (i = 0; i < JESTER_SHOTS; ++i) {
        const JesterShot *p = &w->shots[i];
        if (!p->active) continue;
        if (p->kind == 0) {
            fx_mote(p->pos, still, p->phase == 2 ? 420.0f : 170.0f, p->phase == 2 ? 520.0f : 150.0f, 0.06f,
                    p->phase == 2 ? 0xff2080ffu : 0xff40e0ffu);
        } else {
            float armed = p->age > 55.0f && p->age <= 65.0f;
            for (k = 0; k < 8; ++k) {
                float a = (float)k * 0.7853982f, at[3] = {p->pos[0] + sinf(a) * 75.0f, p->pos[1] + (armed ? 150.0f : 5.0f), p->pos[2] + cosf(a) * 75.0f};
                fx_mote(at, still, armed ? 160.0f : 70.0f, armed ? 90.0f : 70.0f, 0.06f, armed ? 0xff2040ffu : 0xffa0f0ffu);
            }
        }
    }
    if (w->emitting && w->kind == 2) {
        for (i = 0; i < 4; ++i) {
            float a = w->angle + (float)i * 1.5707963f;
            for (k = 1; k <= 6; ++k) {
                float r = (float)k * 200.0f, at[3] = {w->cross[0] + sinf(a) * r, w->cross[1] + 90.0f, w->cross[2] + cosf(a) * r};
                fx_mote(at, still, 220.0f, 220.0f, 0.06f, 0xfff060e0u);
            }
        }
    }
}
