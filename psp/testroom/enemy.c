#include "enemy.h"

#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <psputils.h>
#include <pspthreadman.h>

#include <malloc.h>
#include <math.h>
#include <string.h>

#include "motion.h"
#include "player.h"

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "swizzle.h"
#include "model_runtime.h"   /* Dmc3Vec3 / Dmc3Uv / Dmc3ModelObject / Dmc3Texture types */
#include "enemy_runtime.h"

/* Motions (Seven Hells shared set), chosen from the data:
 *   hl-00 10-frame loop, hips steady          -> idle
 *   hl-37 33 frames, pushed back 21 cm         -> flinch (light hit)
 *   hl-11 knocked back 95 cm (then hl-13)      -> knock-back (heavy hit)
 *   hl-22 root rises 2.5 m in 41 frames        -> launched
 *   hl-23 looped, held in the air              -> airborne
 *   hl-24 19 frames, small pop                 -> air hit
 *   hl-55 looped, hips at 17 (lying)           -> down
 *   hl-20 hips 17 -> 119 over 60 frames        -> get up */
#define M_IDLE "hl-00"
#define M_FLINCH "hl-37"
#define M_KNOCK "hl-11"
#define M_KNOCK_RECOVER "hl-13"
#define M_LAUNCH "hl-22"
#define M_AIR "hl-23"
#define M_AIR_HIT "hl-24"
#define M_DOWN "hl-55"
#define M_GETUP "hl-20"
#define M_WALK "hl-15"
#define M_SWING "hl-16"
#define M_JUMP "hl-21"

#define ENEMY_GRAVITY 900.0f       /* floatier than Dante: juggles hang */
#define LAUNCH_HANG 0.45f          /* seconds held at the top of a launch */
#define ENEMY_RADIUS 45.0f
#define AIR_POP_SPEED 380.0f
#define LAUNCH_HEIGHT 250.0f
#define DOWN_SECONDS 1.0f
#define HAND_BONE 9                 /* -X hand holds the scythe */
#define MAX_BONES 24

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } Vertex;
typedef struct { float m[3][3]; } M3;

/* Read off the HD Collection, per 60 fps frame: a dummy swings from about 90 cm away with hl-16 and
 * connects near frame 63, or leaps at Dante with hl-21 for about 40 frames, peaking about 440 cm up.
 * The real AI isn't ported: walk speed, reaches, how often they leap, and leaps hurting him are guesses. */
#define WALK_SPEED 2.0f
#define SWING_FROM 100.0f
#define SWING_REACH 170.0f
#define SWING_HIT_FRAME 63.0f
#define JUMP_FROM_MIN 150.0f
#define JUMP_FROM_MAX 320.0f
#define JUMP_FRAMES 40.0f
#define JUMP_PEAK 440.0f
#define JUMP_PAST 90.0f
#define JUMP_REACH 110.0f
/* One comes in at a time; the rest wait about this far off, as they do on the real game. */
#define WAIT_DISTANCE 220.0f

typedef struct {
    int active;
    EnemyState state;
    float pos[3], yaw, vy, floor_y, state_time, frame;
    int motion;
    float launch_base;
    /* Short cross-fade between motions. */
    int prev_motion;
    float prev_frame, fade;
    float ai_wait, jump_step[2], jump_time;
    int ai_hit_done, approaching;
    /* Each its own: the display list reads them after the next dummy is skinned, and they
     * stay as they are on the frames this one's pose isn't updated. */
    Vertex *vertices;
    float hand_rot[3][3], hand_pos[3];
    float anim_wait, posed_frame;
    int posed_motion;
} Enemy;

static int m_idle, m_flinch, m_knock, m_knock_recover, m_launch, m_air, m_air_hit, m_down, m_getup, m_walk, m_swing, m_jump;
static Vertex *weapon_vertices;
static unsigned short *indices, *weapon_indices;
static void *textures[ENEMY_TEXTURE_COUNT];
static Enemy enemies[ENEMY_MAX];
static Enemy *e = &enemies[0];
static int hits, attacks_on;
static int landed;
static float landed_x, landed_z;
static int landed_heavy;
static unsigned int ai_seed = 12345u;
static int anim_auto = 1, poses_done;
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

static void set_motion(int m, float f) {
    if (m == e->motion) return;
    e->prev_motion = e->motion;
    e->prev_frame = e->frame;
    e->fade = e->prev_motion >= 0 ? 1.0f : 0.0f;
    e->motion = m;
    e->frame = f;
}

int enemy_init(void) {
    int i, k;
    m_idle = motion_find(M_IDLE); m_flinch = motion_find(M_FLINCH); m_knock = motion_find(M_KNOCK);
    m_knock_recover = motion_find(M_KNOCK_RECOVER); m_launch = motion_find(M_LAUNCH); m_air = motion_find(M_AIR);
    m_air_hit = motion_find(M_AIR_HIT); m_down = motion_find(M_DOWN); m_getup = motion_find(M_GETUP);
    m_walk = motion_find(M_WALK); m_swing = motion_find(M_SWING); m_jump = motion_find(M_JUMP);
    if (m_idle < 0 || m_flinch < 0 || m_knock < 0 || m_knock_recover < 0 || m_launch < 0 || m_air < 0 ||
        m_air_hit < 0 || m_down < 0 || m_getup < 0 || m_walk < 0 || m_swing < 0 || m_jump < 0) return 0;
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
        size_t bytes = (size_t)enemy_textures[i].width * enemy_textures[i].height * 4;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], enemy_textures[i].rgba, bytes);
        if (!swizzle_texture32(textures[i], enemy_textures[i].width, enemy_textures[i].height)) return 0;
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
    e->vy = 0.0f;
    e->state = ENEMY_IDLE;
    e->state_time = 0.0f;
    e->motion = -1;
    set_motion(m_idle, 0.0f);
    e->fade = 0.0f;
    e->ai_wait = 0.5f + 0.5f * (float)(e - enemies);
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
            float a = 0.785f * (float)i + 0.6f, r = 320.0f + 60.0f * (float)(i / 8);
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
EnemyState enemy_state(void) { return e->state; }
void enemy_position(float out[3]) { memcpy(out, e->pos, sizeof(e->pos)); }
float enemy_height(void) { return e->pos[1] - e->floor_y; }
int enemy_hits_taken(void) { return hits; }

const char *enemy_state_name(void) {
    static const char *names[] = {"IDLE", "FLINCH", "KNOCKBACK", "LAUNCHED", "AIRBORNE", "DOWN", "GET UP", "WALK", "SWING", "JUMP"};
    return names[e->state];
}

static void enter(EnemyState s, int m) {
    e->state = s;
    e->state_time = 0.0f;
    set_motion(m, 0.0f);
}

void enemy_hit(HitKind kind, float from_x, float from_z) {
    if (!e->active) return;
    ++hits;  /* infinite health: hits only cause reactions */
    {
        /* On the side facing the attacker, at chest height. */
        float d[2] = {from_x - e->pos[0], from_z - e->pos[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1]);
        float at[3] = {e->pos[0], e->pos[1] + 110.0f, e->pos[2]};
        if (len > 1.0f) { at[0] += d[0] / len * 40.0f; at[2] += d[1] / len * 40.0f; }
        fx_hit_spark(at);
    }
    /* Face the attacker. */
    e->yaw = atan2f(from_x - e->pos[0], from_z - e->pos[2]);
    if (e->state == ENEMY_LAUNCHED || e->state == ENEMY_AIRBORNE) kind = kind == HIT_SLAM ? HIT_SLAM : HIT_AIR;
    switch (kind) {
    case HIT_LIGHT: if (e->state != ENEMY_DOWN && e->state != ENEMY_GETUP) enter(ENEMY_FLINCH, m_flinch); e->frame = 0.0f; break;
    case HIT_HEAVY: enter(ENEMY_KNOCKBACK, m_knock); e->frame = 0.0f; break;
    case HIT_LAUNCH:
        e->launch_base = e->floor_y;
        enter(ENEMY_LAUNCHED, m_launch);
        e->frame = 0.0f;
        break;
    case HIT_AIR:
        /* Juggle: pop up and hang. */
        e->vy = AIR_POP_SPEED;
        enter(ENEMY_AIRBORNE, m_air_hit);
        e->frame = 0.0f;
        break;
    case HIT_SLAM:
        e->vy = -1800.0f;
        enter(ENEMY_AIRBORNE, m_air_hit);
        break;
    }
}

void enemy_push_out(float *x, float *z, float r) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        float dx, dz, d, min;
        if (!o->active || o->state == ENEMY_DOWN) continue;
        dx = *x - o->pos[0]; dz = *z - o->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        min = r + ENEMY_RADIUS;
        if (d < min && d > 1e-3f && fabsf(o->pos[1] - o->floor_y) < 150.0f) {
            *x = o->pos[0] + dx / d * min;
            *z = o->pos[2] + dz / d * min;
        }
    }
}

static float ai_rand(void) {
    ai_seed = ai_seed * 1103515245u + 12345u;
    return (float)((ai_seed >> 16) & 0x7fff) / 32767.0f;
}

static void land_attack(int heavy) {
    landed = 1;
    landed_x = e->pos[0];
    landed_z = e->pos[2];
    landed_heavy = heavy;
}

/* Keep dummies from standing inside each other. */
static void keep_apart(void) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        Enemy *o = &enemies[i];
        float dx, dz, d;
        if (o == e || !o->active) continue;
        dx = e->pos[0] - o->pos[0]; dz = e->pos[2] - o->pos[2];
        d = sqrtf(dx * dx + dz * dz);
        if (d < ENEMY_RADIUS * 2.0f && d > 1e-3f) {
            e->pos[0] = o->pos[0] + dx / d * ENEMY_RADIUS * 2.0f;
            e->pos[2] = o->pos[2] + dz / d * ENEMY_RADIUS * 2.0f;
        }
    }
}

static int another_is_coming_in(void) {
    int i;
    for (i = 0; i < ENEMY_MAX; ++i) {
        const Enemy *o = &enemies[i];
        if (o == e || !o->active) continue;
        if (o->state == ENEMY_SWING || o->state == ENEMY_JUMP || (o->state == ENEMY_WALK && o->approaching)) return 1;
    }
    return 0;
}

/* Attack mode: what an idle or walking dummy does next. */
static void ai_think(float dt, float px, float pz) {
    float dx = px - e->pos[0], dz = pz - e->pos[2], d = sqrtf(dx * dx + dz * dz);
    int blocked = another_is_coming_in();
    if (blocked) {
        /* Close the distance only as far as the waiting ring. */
        e->approaching = 0;
        e->yaw = atan2f(dx, dz);
        if (e->state == ENEMY_WALK && d <= WAIT_DISTANCE) enter(ENEMY_IDLE, m_idle);
        else if (e->state == ENEMY_IDLE && d > WAIT_DISTANCE + 40.0f) enter(ENEMY_WALK, m_walk);
        if (e->state == ENEMY_WALK) {
            float step = WALK_SPEED * dt * MOTION_FPS;
            e->pos[0] += dx / d * step;
            e->pos[2] += dz / d * step;
        }
        return;
    }
    if (e->state == ENEMY_WALK) {
        float step = WALK_SPEED * dt * MOTION_FPS;
        e->yaw = atan2f(dx, dz);
        if (d > 1.0f) { e->pos[0] += dx / d * step; e->pos[2] += dz / d * step; }
    }
    e->ai_wait -= dt;
    if (e->ai_wait > 0.0f && e->state == ENEMY_IDLE) return;
    if (d <= SWING_FROM) {
        e->yaw = atan2f(dx, dz);
        e->ai_hit_done = 0;
        enter(ENEMY_SWING, m_swing);
    } else if (d >= JUMP_FROM_MIN && d <= JUMP_FROM_MAX && e->ai_wait <= 0.0f && ai_rand() < 0.35f) {
        float way = d + JUMP_PAST;
        e->yaw = atan2f(dx, dz);
        e->jump_step[0] = dx / d * way / JUMP_FRAMES;
        e->jump_step[1] = dz / d * way / JUMP_FRAMES;
        e->jump_time = 0.0f;
        e->ai_hit_done = 0;
        enter(ENEMY_JUMP, m_jump);
    } else if (e->state == ENEMY_IDLE) {
        e->approaching = 1;
        enter(ENEMY_WALK, m_walk);
    }
    if (e->ai_wait <= 0.0f) e->ai_wait = 1.0f;  /* the next chance to leap */
}

static void update_one(float dt, float px, float py, float pz) {
    const Motion *m;
    float root[3];
    if (!e->active) return;
    e->state_time += dt;
    if (e->fade > 0.0f) e->fade -= dt / 0.12f;
    if (e->fade < 0.0f) e->fade = 0.0f;
    e->frame += dt * MOTION_FPS;
    m = motion_get(e->motion);

    switch (e->state) {
    case ENEMY_IDLE:
        e->frame = motion_wrap(m, e->frame);
        if (attacks_on) ai_think(dt, px, pz);
        break;
    case ENEMY_WALK:
        e->frame = motion_wrap(m, e->frame);
        if (attacks_on) ai_think(dt, px, pz);
        else enter(ENEMY_IDLE, m_idle);
        break;
    case ENEMY_SWING:
        if (!e->ai_hit_done && e->frame >= SWING_HIT_FRAME) {
            float dx = px - e->pos[0], dz = pz - e->pos[2];
            e->ai_hit_done = 1;
            if (dx * dx + dz * dz < SWING_REACH * SWING_REACH && fabsf(py - e->pos[1]) < 200.0f) land_attack(0);
        }
        if (e->frame >= m->end_frame) { e->ai_wait = 0.8f + ai_rand() * 1.2f; enter(ENEMY_IDLE, m_idle); }
        break;
    case ENEMY_JUMP: {
        /* A leap on a parabola, peaking halfway. */
        float frames = dt * MOTION_FPS, t;
        e->jump_time += frames;
        t = e->jump_time / JUMP_FRAMES;
        if (t > 1.0f) t = 1.0f;
        e->pos[0] += e->jump_step[0] * frames;
        e->pos[2] += e->jump_step[1] * frames;
        e->pos[1] = e->floor_y + JUMP_PEAK * 4.0f * t * (1.0f - t);
        if (e->frame > m->end_frame - 1.0f) e->frame = m->end_frame - 1.0f;
        if (t >= 1.0f) {
            float dx = px - e->pos[0], dz = pz - e->pos[2];
            e->pos[1] = e->floor_y;
            if (dx * dx + dz * dz < JUMP_REACH * JUMP_REACH && fabsf(py - e->pos[1]) < 150.0f) land_attack(1);
            e->ai_wait = 0.8f + ai_rand() * 1.2f;
            enter(ENEMY_IDLE, m_idle);
        }
        break;
    }
    case ENEMY_FLINCH:
        if (e->frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    case ENEMY_KNOCKBACK:
        /* Slide back along the hit direction, then recover. */
        if (e->motion == m_knock) {
            float push = 140.0f * dt;
            e->pos[0] -= sinf(e->yaw) * push; e->pos[2] -= cosf(e->yaw) * push;
            if (e->frame >= m->end_frame) set_motion(m_knock_recover, 0.0f);
        } else if (e->frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    case ENEMY_LAUNCHED:
        /* Height from the launch motion's root Y (2.5 m), then hang. */
        motion_root(m, e->frame < m->end_frame ? e->frame : m->end_frame, root);
        e->pos[1] = e->launch_base + root[1] * (LAUNCH_HEIGHT / 252.0f);
        if (e->frame >= m->end_frame) { e->vy = 0.0f; enter(ENEMY_AIRBORNE, m_air); }
        break;
    case ENEMY_AIRBORNE:
        if (e->motion == m_air_hit && e->frame >= m->end_frame) set_motion(m_air, 0.0f);
        if (e->motion == m_air) e->frame = motion_wrap(m, e->frame);
        if (e->state_time > LAUNCH_HANG || e->vy != 0.0f) e->vy -= ENEMY_GRAVITY * dt;
        e->pos[1] += e->vy * dt;
        if (e->pos[1] <= e->floor_y) { e->pos[1] = e->floor_y; e->vy = 0.0f; enter(ENEMY_DOWN, m_down); }
        break;
    case ENEMY_DOWN:
        e->frame = motion_wrap(m, e->frame);
        if (e->state_time > DOWN_SECONDS) enter(ENEMY_GETUP, m_getup);
        break;
    case ENEMY_GETUP:
        if (e->frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    }
    /* Track the floor while grounded (knock-back slides). */
    if (e->state != ENEMY_LAUNCHED && e->state != ENEMY_AIRBORNE && e->state != ENEMY_JUMP) {
        float g;
        keep_apart();
        if (floor_under(e->pos[0], e->pos[1], e->pos[2], &g)) e->floor_y = g;
        e->pos[1] = e->floor_y;
    }
}

void enemy_update(float dt, float px, float py, float pz) {
    Enemy *selected = e;
    int i;
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
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 1);   /* swizzled at load */
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    for (i = 0; i < ENEMY_OBJECT_COUNT; ++i) {
        const Dmc3ModelObject *o = &enemy_objects[i];
        if ((int)o->texture != bound) {
            bound = (int)o->texture;
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
        sceGuTexImage(0, enemy_textures[bound].width, enemy_textures[bound].height, enemy_textures[bound].width, textures[bound]);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, VTYPE, ENEMY_WEAPON_INDEX_COUNT, weapon_indices, weapon_vertices);
        sceGumPopMatrix();
    }
}

void enemy_draw(void) {
    Enemy *selected = e;
    int i, rate = enemy_anim_rate(), count = enemy_count(), turn = 0;
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
    e = selected;
}
