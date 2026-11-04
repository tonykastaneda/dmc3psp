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
    A_NONE = 14, A_WALK = 15, A_SWING = 16, A_SWING_WIDE = 17, A_LEAP = 21,
    A_STEP_LEFT = 22, A_STEP_RIGHT = 23, A_STEP_BACK = 24, A_TAUNT_A = 58, A_TAUNT_B = 59
};
enum {  /* Its plans. PLAN_STAND is ours: the debug menu's dummies that don't attack. */
    P_FLINCH_FRONT = 0, P_FLINCH_BACK = 1, P_LAUNCHED = 2, P_LAUNCHED_BACK = 3, P_LAUNCH_UP = 7, P_AIR_HIT_HEAVY = 8, P_AIR_HIT = 9,
    P_DECIDE = 23, P_WATCH = 24, P_SWING = 27, P_SWING_WIDE = 28, P_LEAP = 32, P_STEP_LEFT = 33, P_STEP_RIGHT = 34,
    P_STEP_BACK = 35, P_TAUNT_A = 51, P_TAUNT_B = 52, PLAN_STAND = 200
};
enum { OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO };

/* How long after one enemy is let attack before another may. The game passes 3.0 to a manager this
 * port hasn't read (FUN_00369180). Measured on the HD Collection in Turbo: 49 swings from 4 enemies,
 * never under 3.0 real seconds apart, so it doesn't scale with game speed. */
#define ATTACK_TOKEN_SECONDS 3.0f
/* The game's value comes from a mission object not read yet. 0 gives what Mission 2 on Easy shows
 * on the HD Collection: plans 27, 32, 51 and 52 only. */
#define AI_RANK 0.0f
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
#define HAND_BONE 9                 /* -X hand holds the scythe */
#define MAX_BONES 24

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } Vertex;
typedef struct { float m[3][3]; } M3;

typedef struct {
    int active;
    float pos[3], yaw, floor_y, frame, fall;
    int motion;
    /* Cross-fade between motions, over as many frames as the script says. */
    int prev_motion;
    float prev_frame, fade, fade_rate;
    /* The layers. */
    unsigned char plan, plan_step, action, step;
    unsigned char air, loop, done, stop_finish, pushed;
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
static Vertex *weapon_vertices;
static unsigned short *indices, *weapon_indices;
static void *textures[ENEMY_TEXTURE_COUNT];
static Enemy enemies[ENEMY_MAX];
static Enemy *e = &enemies[0];
static int hits, attacks_on;
static int landed;
static float landed_x, landed_z;
static int landed_heavy;
static float attack_token;
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

int enemy_init(void) {
    int i, k;
    for (i = 0; i < HL_MOTIONS; ++i) {
        char name[6] = {'h', 'l', '-', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        hl_motion[i] = motion_find(name);
        if (hl_motion[i] < 0) return 0;
    }
    indices = memalign(16, sizeof(unsigned short) * ENEMY_INDEX_COUNT);
    weapon_vertices = memalign(16, sizeof(Vertex) * ENEMY_WEAPON_VERTEX_COUNT);
    weapon_indices = memalign(16, sizeof(unsigned short) * ENEMY_WEAPON_INDEX_COUNT);
    if (!indices || !weapon_vertices || !weapon_indices) return 0;
    memcpy(indices, enemy_indices, sizeof(unsigned short) * ENEMY_INDEX_COUNT);
    memcpy(weapon_indices, enemy_weapon_indices, sizeof(unsigned short) * ENEMY_WEAPON_INDEX_COUNT);
    for (k = 0; k < ENEMY_MAX; ++k) enemies[k].motion = enemies[k].prev_motion = -1;
    for (i = 0; i < ENEMY_WEAPON_VERTEX_COUNT; ++i) {
        Vertex *v = &weapon_vertices[i];
        v->u = enemy_weapon_uvs[i].u; v->v = enemy_weapon_uvs[i].v;
        v->nx = enemy_weapon_normals[i].x; v->ny = enemy_weapon_normals[i].y; v->nz = enemy_weapon_normals[i].z;
        v->x = enemy_weapon_positions[i].x; v->y = enemy_weapon_positions[i].y; v->z = enemy_weapon_positions[i].z;
    }
    for (i = 0; i < ENEMY_TEXTURE_COUNT; ++i) {
        size_t bytes = (size_t)enemy_textures[i].width * enemy_textures[i].height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], enemy_textures[i].rgba, bytes);
        if (!swizzle_texture8(textures[i], enemy_textures[i].width, enemy_textures[i].height)) return 0;
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

static void spawn_one(float x, float z) {
    /* Each dummy's own skinned copy, the first time that slot is used. */
    if (!e->vertices) {
        int i;
        e->vertices = memalign(16, sizeof(Vertex) * ENEMY_VERTEX_COUNT);
        if (!e->vertices) return;
        for (i = 0; i < ENEMY_VERTEX_COUNT; ++i) { e->vertices[i].u = enemy_uvs[i].u; e->vertices[i].v = enemy_uvs[i].v; }
    }
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
    script_play(0);
    e->fade = 0.0f;
    e->posed_motion = -1;
    e->active = 1;
}

void enemy_spawn(float x, float z) {
    int i;
    for (i = 1; i < ENEMY_MAX; ++i) enemies[i].active = 0;
    e = &enemies[0];
    spawn_one(x, z);
}

int enemy_count(void) {
    int i, n = 0;
    for (i = 0; i < ENEMY_MAX; ++i) n += enemies[i].active;
    return n;
}

void enemy_set_count(int count, float x, float z) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        e = &enemies[i];
        if (i >= count) {
            e->active = 0;
        } else if (!e->active) {
            float a = 0.785f * (float)i + 0.6f, r = spawn_ring + 60.0f * (float)(i / 8);
            spawn_one(x + sinf(a) * r, z + cosf(a) * r);
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
    default: break;
    }
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
    while (band < ENEMY_AI_BANDS - 1 && distance >= enemy_ai_bands[band]) ++band;
    while (column < 6 && enemy_ai_times[column] <= AI_RANK) ++column;
    row = &enemy_ai_rows[attack ? 0 : 1][band][column];
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
        for (i = 0; i < ENEMY_AI_ZONES; ++i) {
            if (distance < enemy_ai_zones[i][0] && fabsf(d) < enemy_ai_zones[i][1] * PI / 180.0f) {
                e->gauge += enemy_ai_zones[i][2] * f;
                break;
            }
        }
        while (column < 6 && enemy_ai_times[column] <= AI_RANK) ++column;
        if (e->gauge > enemy_ai_thresholds[column < 6 ? column : 5]) decide(distance);
        break;
    }
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
    e = selected;
}

/* Sample motion -> local rotations/offsets -> world transforms. */
static void evaluate_pose(void) {
    static MotionSample a, b;
    M3 local;
    int i, k;
    motion_sample(motion_get(e->motion), e->frame, &a);
    if (e->fade > 0.0f && e->prev_motion >= 0) motion_sample(motion_get(e->prev_motion), e->prev_frame, &b);
    for (i = 0; i < ENEMY_BONE_COUNT; ++i) {
        float r[3], t[3] = {enemy_bone_local[i].x, enemy_bone_local[i].y, enemy_bone_local[i].z};
        int p = enemy_bone_parent[i];
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
    for (i = 0; i < ENEMY_VERTEX_COUNT; ++i) {
        const Dmc3Vec3 *s = &enemy_positions[i], *n = &enemy_normals[i];
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (k = 0; k < 3; ++k) {
            int w = enemy_weights[i][k], b;
            float wf, dx, dy, dz;
            const float (*r)[3];
            if (!w) continue;
            b = enemy_bones[i][k];
            wf = (float)w * (1.0f / 31.0f);
            r = world_rot[b].m;
            dx = s->x - enemy_bone_rest[b].x; dy = s->y - enemy_bone_rest[b].y; dz = s->z - enemy_bone_rest[b].z;
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
    sceKernelDcacheWritebackRange(e->vertices, sizeof(Vertex) * ENEMY_VERTEX_COUNT);
}

#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

/* Each dummy's bones update on its own turn, spread out over the frames so they don't all
 * cost the same frame. A new motion (a hit, say) always shows at once. */
static void update_pose(float elapsed, float interval, int index, int count) {
    int moved = e->motion != e->posed_motion || e->frame != e->posed_frame;
    if (!moved) return;
    e->anim_wait -= elapsed;
    if (interval > 0.0f && e->motion == e->posed_motion && e->anim_wait > 0.0f) return;
    e->anim_wait = interval + interval * (float)index / (float)(count > 0 ? count : 1);
    if (e->posed_motion >= 0 && e->motion == e->posed_motion) e->anim_wait = interval;
    e->posed_motion = e->motion;
    e->posed_frame = e->frame;
    evaluate_pose();
    skin();
    memcpy(e->hand_rot, world_rot[HAND_BONE].m, sizeof(e->hand_rot));
    memcpy(e->hand_pos, world_pos[HAND_BONE], sizeof(e->hand_pos));
    ++poses_done;
}

static void draw_one(void) {
    int i, bound = -1;
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
    for (i = 0; i < ENEMY_OBJECT_COUNT; ++i) {
        const Dmc3ModelObject *o = &enemy_objects[i];
        if ((int)o->texture != bound) {
            bound = (int)o->texture;
            sceGuClutLoad(256 / 8, enemy_palettes[bound]);
            sceGuTexImage(0, enemy_textures[bound].width, enemy_textures[bound].height, enemy_textures[bound].width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices + o->index_start, e->vertices);
    }
    {
        /* Scythe in the -X hand. */
        ScePspFMatrix4 mm;
        const float (*r)[3] = e->hand_rot;
        mm.x.x = r[0][0]; mm.x.y = r[1][0]; mm.x.z = r[2][0]; mm.x.w = 0.0f;
        mm.y.x = r[0][1]; mm.y.y = r[1][1]; mm.y.z = r[2][1]; mm.y.w = 0.0f;
        mm.z.x = r[0][2]; mm.z.y = r[1][2]; mm.z.z = r[2][2]; mm.z.w = 0.0f;
        mm.w.x = e->hand_pos[0]; mm.w.y = e->hand_pos[1]; mm.w.z = e->hand_pos[2]; mm.w.w = 1.0f;
        sceGumPushMatrix();
        sceGumMultMatrix(&mm);
        bound = ENEMY_WEAPON_TEXTURE;
        sceGuClutLoad(256 / 8, enemy_palettes[bound]);
        sceGuTexImage(0, enemy_textures[bound].width, enemy_textures[bound].height, enemy_textures[bound].width, textures[bound]);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, VTYPE, ENEMY_WEAPON_INDEX_COUNT, weapon_indices, weapon_vertices);
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
        update_pose(elapsed, rate > 0 ? 1.0f / (float)rate : 0.0f, turn++, count);
        draw_one();
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    e = selected;
}
