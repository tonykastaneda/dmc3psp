#include "bullets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "enemy.h"
#include "cerberus_boss.h"
#include "leviathan_boss.h"
#include "fx.h"
#include "nfx.h"
#include "stage.h"

#define MAX_BULLETS 48
#define ENEMY_RADIUS 70.0f
#define ENEMY_HEIGHT 220.0f
#define PROJECTILE_MAX_SECONDS 2.5f
/* The Shotgun's shell (FUN_002ee930): 6 / 8 / 10 pellets by its level (0x5a2348; level 0 here [the gun's level isn't
 * carried]): one straight to 2000 cm along the aim, the rest on a ring round that point, evenly spaced from a random
 * angle, each 250-500 cm out across the aim (500-1000 for its shell type 2). */
#define SHOTGUN_PELLETS 6
#define SHOTGUN_REACH 2000.0f

typedef struct {
    int active, kind, moving;
    float a[3], b[3];   /* tracer from a to b, or the projectile's tail and head */
    float velocity[3];  /* cm/s */
    float life;
    unsigned int color;
    int fx;             /* Artemis's shot: its V0xd3 riding it */
} Bullet;

static Bullet bullets[MAX_BULLETS];
static int shots_fired, hits_landed;

/* Range from the handlers: E&I FUN_002698d0, shotgun FUN_0026c740, Spiral FUN_0026f060.
 * Artemis and Kalina Ann use the E&I range and an estimated speed. */
static const struct { float range, speed; unsigned int color; HitKind hit; } guns[BULLET_KIND_COUNT] = {
    [BULLET_EBONY_IVORY] = {2000.0f, 0.0f, 0xff60e0ffu, HIT_LIGHT},
    [BULLET_SHOTGUN] = {1000.0f, 0.0f, 0xff50c0ffu, HIT_HEAVY},
    [BULLET_ARTEMIS] = {2000.0f, 3000.0f, 0xffff80ffu, HIT_LIGHT},
    [BULLET_SPIRAL] = {1500.0f, 0.0f, 0xffffffa0u, HIT_HEAVY},
    [BULLET_KALINA_ANN] = {2000.0f, 1800.0f, 0xff4080ffu, HIT_HEAVY},
};

void bullets_reset(void) {
    memset(bullets, 0, sizeof(bullets));
    shots_fired = hits_landed = 0;
}

int bullets_shots(void) { return shots_fired; }
int bullets_hits(void) { return hits_landed; }

static Bullet *new_bullet(void) {
    int i;
    for (i = 0; i < MAX_BULLETS; ++i)
        if (!bullets[i].active) return &bullets[i];
    return &bullets[0];
}

/* Does the segment a-b pass through the dummy's upright capsule? Returns the
 * fraction along it, or -1. */
static int shot_kind_now;   /* the E&I shot in flight's PS2 kind (bullets_set_shot_kind) */
static float one_enemy_segment_hit(const float a[3], const float b[3]) {
    float e[3], dx, dz, len2, t, cx, cz, y;
    if (!enemy_active()) return -1.0f;
    if (enemy_kind() == ENEMY_CERBERUS) return cerberus_boss_segment_hit(a, b);
    if (enemy_kind() == ENEMY_LEVIATHAN) return leviathan_boss_segment_hit(a, b);
    enemy_position(e);
    dx = b[0] - a[0];
    dz = b[2] - a[2];
    len2 = dx * dx + dz * dz;
    t = len2 > 0.001f ? ((e[0] - a[0]) * dx + (e[2] - a[2]) * dz) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    cx = a[0] + dx * t - e[0];
    cz = a[2] + dz * t - e[2];
    y = a[1] + (b[1] - a[1]) * t - e[1];
    if (cx * cx + cz * cz > ENEMY_RADIUS * ENEMY_RADIUS || y < -20.0f || y > ENEMY_HEIGHT) return -1.0f;
    return t;
}

/* The first dummy along the segment, left selected for enemy_hit. */
static float enemy_segment_hit(const float a[3], const float b[3]) {
    int i, first = -1;
    float best = -1.0f;
    for (i = 0; i < ENEMY_MAX; ++i) {
        float t;
        enemy_select(i);
        t = one_enemy_segment_hit(a, b);
        if (t >= 0.0f && (best < 0.0f || t < best)) { best = t; first = i; }
    }
    if (first >= 0) enemy_select(first);
    return best;
}

/* One instant shot: stops at a wall, hurts the dummy if it comes first. */
int bullets_through_walls;
static int hitscan(int kind, const float from[3], const float to[3]) {
    float end[3], wall = 1.0f, t;
    int hit = 0, k;
    if (stage_loaded() && !bullets_through_walls) {
        float s;
        if (stage_raycast(from, to, &s)) wall = s;
    }
    for (k = 0; k < 3; ++k) end[k] = from[k] + (to[k] - from[k]) * wall;
    t = enemy_segment_hit(from, end);
    if (t >= 0.0f) {
        hit = 1;
        for (k = 0; k < 3; ++k) end[k] = from[k] + (end[k] - from[k]) * t;
    }
    if (hit || wall < 1.0f) {
        /* Where a shot lands: V0xa (its shell, 002efaa0; V0x32 for a charged one, not told apart here), from the E&I
         * pack, the Shotgun's own for its pellets; Spiral's keeps the port's spark. */
        int bank = kind == BULLET_SHOTGUN ? NFX_GUN_SHOTGUN : kind == BULLET_EBONY_IVORY ? NFX_GUN_EI : -1;
        int charged = kind == BULLET_EBONY_IVORY && (shot_kind_now == 5 || shot_kind_now >= 7);   /* the shell's flag 1 */
        if (bank < 0 || !nfx_spawn(bank, charged ? 0x32 : 0xa, end, atan2f(to[0] - from[0], to[2] - from[2]), 0)) fx_wall_spark(end);
    }
    if (kind == BULLET_SPIRAL) fx_tracer(from, end, guns[kind].color, 6.0f);
    return hit;
}

static void bullets_fire_one(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]);
void bullets_fire(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]) {
    enemy_gun_aimed(muzzle[0], muzzle[2]);   /* Blood-goyles near him may dodge (their state 0's check) */
    bullets_fire_one(kind, muzzle, yaw, has_target, target);
}
/* The next E&I shot's kind (FUN_002698d0 / 0026a520: 0 ordinary, 5 the charge shot's, 7-9 Gunslinger's rapid fire
 * upgraded), set by main.c's charge state; its flash, impact and damage (the shell's record, 0x5a5a30 + kind * 0x40:
 * kinds 7-9 share record 7). */
static int next_shot_kind;
extern float enemy_bullet_damage;
void bullets_set_shot_kind(int kind) { next_shot_kind = kind; }
static void bullets_fire_one(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]) {
    float to[3], d[3], len;
    shot_kind_now = kind == BULLET_EBONY_IVORY ? next_shot_kind : 0;
    next_shot_kind = 0;
    int k;
    if (kind < 0 || kind >= BULLET_KIND_COUNT) return;
    ++shots_fired;
    /* Aim: the lock-on point, else straight ahead at muzzle height. */
    if (has_target) {
        memcpy(to, target, sizeof(to));
    } else {
        to[0] = muzzle[0] + sinf(yaw) * guns[kind].range;
        to[1] = muzzle[1];
        to[2] = muzzle[2] + cosf(yaw) * guns[kind].range;
    }
    for (k = 0; k < 3; ++k) d[k] = to[k] - muzzle[k];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1.0f) return;
    for (k = 0; k < 3; ++k) d[k] /= len;
    /* The gun's own flash (FUN_002fbf20, by the shot's kind, from the gun's effect pack GDATA 97 + w as nfx banks
     * 14-18): E&I V0x22, the Shotgun V0x23, Spiral V0x228, Kalina Ann V0x21b, on the muzzle along the shot. Artemis's
     * shot (CPl000Shl10) carries its own (V0xd3, below). The charge levels' (V0x129 / V0x145, V0x137 / 0x14b / 0x158)
     * and the ground-level shots' dust (V0x7b / V0x15c) aren't told apart by the port's guns. */
    {
        static const short flash[BULLET_KIND_COUNT][2] = {
            [BULLET_EBONY_IVORY] = {NFX_GUN_EI, 0x22}, [BULLET_SHOTGUN] = {NFX_GUN_SHOTGUN, 0x23},
            [BULLET_ARTEMIS] = {-1, 0}, [BULLET_SPIRAL] = {NFX_GUN_SPIRAL, 0x228}, [BULLET_KALINA_ANN] = {NFX_GUN_KALINA, 0x21b}};
        int v = flash[kind][1];
        if (kind == BULLET_EBONY_IVORY) v = shot_kind_now == 5 ? 0x129 : shot_kind_now >= 7 ? 0x145 : 0x22;
        if (flash[kind][0] < 0 || !nfx_spawn(flash[kind][0], v, muzzle, atan2f(d[0], d[2]), 0))
            fx_muzzle(muzzle, d, guns[kind].color);
        /* FUN_002fbf20: the charge shot's (kinds 1 / 4 there) also V0x15c, the shared pack's. */
        if (kind == BULLET_EBONY_IVORY && shot_kind_now == 5) nfx_spawn(NFX_STATIC, 0x15c, muzzle, atan2f(d[0], d[2]), 0);
    }
    /* Shots run the gun's whole range, past the aim point. */
    for (k = 0; k < 3; ++k) to[k] = muzzle[k] + d[k] * guns[kind].range;
    if (guns[kind].speed > 0.0f) {
        Bullet *b = new_bullet();
        memset(b, 0, sizeof(*b));
        b->active = 1;
        b->kind = kind;
        b->moving = 1;
        b->color = guns[kind].color;
        b->life = PROJECTILE_MAX_SECONDS;
        memcpy(b->a, muzzle, sizeof(b->a));
        memcpy(b->b, muzzle, sizeof(b->b));
        for (k = 0; k < 3; ++k) b->velocity[k] = d[k] * guns[kind].speed;
        return;
    }
    if (kind == BULLET_SHOTGUN) {
        int p, any = 0;
        float aim[3] = {to[0] - muzzle[0], to[1] - muzzle[1], to[2] - muzzle[2]}, len, x[3], y[3], up[3] = {0.0f, 1.0f, 0.0f};
        float centre[3], start = (float)(rand() % 6283) / 1000.0f;
        len = sqrtf(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
        if (len < 1.0f) { aim[0] = d[0]; aim[1] = d[1]; aim[2] = d[2]; len = 1.0f; }
        for (p = 0; p < 3; ++p) { aim[p] /= len; centre[p] = muzzle[p] + aim[p] * SHOTGUN_REACH; }
        /* FUN_00133890's frame round the aim (up the world's y): x = up x aim, y = aim x x. */
        x[0] = up[1] * aim[2] - up[2] * aim[1]; x[1] = up[2] * aim[0] - up[0] * aim[2]; x[2] = up[0] * aim[1] - up[1] * aim[0];
        len = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); if (len < 1e-4f) { x[0] = 1.0f; x[1] = x[2] = 0.0f; len = 1.0f; }
        for (p = 0; p < 3; ++p) x[p] /= len;
        y[0] = aim[1] * x[2] - aim[2] * x[1]; y[1] = aim[2] * x[0] - aim[0] * x[2]; y[2] = aim[0] * x[1] - aim[1] * x[0];
        any |= hitscan(kind, muzzle, centre);
        for (p = 0; p < SHOTGUN_PELLETS - 1; ++p) {
            float a = start + 6.283185f / (float)(SHOTGUN_PELLETS - 1) * (float)p, r = 250.0f + (float)(rand() % 1000) * 0.25f;
            float pellet[3];
            int k2;
            for (k2 = 0; k2 < 3; ++k2) pellet[k2] = centre[k2] + r * (x[k2] * cosf(a) + y[k2] * sinf(a));
            any |= hitscan(kind, muzzle, pellet);
        }
        if (any) { enemy_hit(guns[kind].hit, muzzle[0], muzzle[2]); ++hits_landed; }
        return;
    }
    if (hitscan(kind, muzzle, to)) {
        static const float record_damage[10] = {10, 10, 16, 15, 10, 30, 50, 12, 12, 12};   /* 0x5a5a30 +4 */
        if (kind == BULLET_EBONY_IVORY) enemy_bullet_damage = record_damage[shot_kind_now];
        enemy_hit(kind==BULLET_EBONY_IVORY?HIT_PISTOL:guns[kind].hit, muzzle[0], muzzle[2]); ++hits_landed;
        enemy_bullet_damage = 10.0f;
    }
}

void bullets_update(float dt) {
    int i, k;
    for (i = 0; i < MAX_BULLETS; ++i) {
        Bullet *b = &bullets[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0.0f) { b->active = 0; if (b->fx) { nfx_kill(b->fx); b->fx = 0; } continue; }
        if (b->moving) {
            float next[3], s, t;
            int done = 0;
            for (k = 0; k < 3; ++k) next[k] = b->b[k] + b->velocity[k] * dt;
            t = enemy_segment_hit(b->b, next);
            if (t >= 0.0f) {
                enemy_hit(guns[b->kind].hit, b->b[0], b->b[2]);
                ++hits_landed;
                done = 1;
            } else if (stage_loaded() && stage_raycast(b->b, next, &s)) {
                done = 1;
            }
            /* A short streak behind the head. */
            for (k = 0; k < 3; ++k) {
                b->a[k] = next[k] - b->velocity[k] * 0.03f;
                b->b[k] = next[k];
            }
            if (b->kind == BULLET_ARTEMIS) {
                float yaw = atan2f(b->velocity[0], b->velocity[2]);
                if (!nfx_alive(b->fx)) b->fx = nfx_spawn(NFX_GUN_ARTEMIS, 0xd3, b->b, yaw, 1);
                else nfx_move(b->fx, b->b, yaw);
                if (!b->fx) fx_projectile(b->a, b->b, b->color);
            } else fx_projectile(b->a, b->b, b->color);
            if (done) {
                b->active = 0;
                if (b->fx) { nfx_kill(b->fx); b->fx = 0; }
                /* Kalina Ann's rocket bursts in V0x21f (its pack's; the one Lady's rocket plays) [inferred: its shell's
                 * code isn't traced] */
                if (b->kind == BULLET_KALINA_ANN) { if (!nfx_spawn(NFX_GUN_KALINA, 0x21f, next, 0.0f, 0)) fx_explosion(next); }
                else fx_wall_spark(next);
            }
        }
    }
}
