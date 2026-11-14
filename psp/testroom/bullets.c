#include "bullets.h"

#include <math.h>
#include <string.h>

#include "enemy.h"
#include "cerberus_boss.h"
#include "leviathan_boss.h"
#include "fx.h"
#include "stage.h"

#define MAX_BULLETS 48
#define ENEMY_RADIUS 70.0f
#define ENEMY_HEIGHT 220.0f
#define PROJECTILE_MAX_SECONDS 2.5f
#define SHOTGUN_PELLETS 5
#define SHOTGUN_SPREAD 0.10f      /* radians between pellets: estimate, not decoded */

typedef struct {
    int active, kind, moving;
    float a[3], b[3];   /* tracer from a to b, or the projectile's tail and head */
    float velocity[3];  /* cm/s */
    float life;
    unsigned int color;
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
static int hitscan(int kind, const float from[3], const float to[3]) {
    float end[3], wall = 1.0f, t;
    int hit = 0, k;
    if (stage_loaded()) {
        float s;
        if (stage_raycast(from, to, &s)) wall = s;
    }
    for (k = 0; k < 3; ++k) end[k] = from[k] + (to[k] - from[k]) * wall;
    t = enemy_segment_hit(from, end);
    if (t >= 0.0f) {
        hit = 1;
        for (k = 0; k < 3; ++k) end[k] = from[k] + (end[k] - from[k]) * t;
    } else if (wall < 1.0f) {
        fx_wall_spark(end);
    }
    if (kind == BULLET_SPIRAL) fx_tracer(from, end, guns[kind].color, 6.0f);
    return hit;
}

static void bullets_fire_one(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]);
void bullets_fire(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]) {
    enemy_gun_aimed(muzzle[0], muzzle[2]);   /* Blood-goyles near him may dodge (their state 0's check) */
    bullets_fire_one(kind, muzzle, yaw, has_target, target);
}
static void bullets_fire_one(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]) {
    float to[3], d[3], len;
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
    fx_muzzle(muzzle, d, guns[kind].color);
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
        float base = atan2f(d[0], d[2]);
        for (p = 0; p < SHOTGUN_PELLETS; ++p) {
            float a = base + (float)(p - SHOTGUN_PELLETS / 2) * SHOTGUN_SPREAD;
            float pellet[3] = {muzzle[0] + sinf(a) * guns[kind].range, to[1], muzzle[2] + cosf(a) * guns[kind].range};
            any |= hitscan(kind, muzzle, pellet);
        }
        if (any) { enemy_hit(guns[kind].hit, muzzle[0], muzzle[2]); ++hits_landed; }
        return;
    }
    if (hitscan(kind, muzzle, to)) { enemy_hit(kind==BULLET_EBONY_IVORY?HIT_PISTOL:guns[kind].hit, muzzle[0], muzzle[2]); ++hits_landed; }
}

void bullets_update(float dt) {
    int i, k;
    for (i = 0; i < MAX_BULLETS; ++i) {
        Bullet *b = &bullets[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0.0f) { b->active = 0; continue; }
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
            fx_projectile(b->a, b->b, b->color);
            if (done) {
                b->active = 0;
                if (b->kind == BULLET_KALINA_ANN) fx_explosion(next);
                else fx_wall_spark(next);
            }
        }
    }
}
