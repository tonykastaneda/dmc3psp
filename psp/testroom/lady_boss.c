/* Lady (class 37, EST 55; em034.pac at GDATA 138), Mission 16's boss, from her class's code (CEm037, 0x443140-
 * 0x450000; docs/ENEMY_SPECS.md J), rank 0. Her brain runs in two levels, a mode (+0x47c0, FUN_00444c90) and an
 * action in it (+0x47c2, FUN_0044afc0, which also names the action's motion: PAC [2]-[6] by range). What's ported:
 * - From her walk (modes below 0x18, FUN_00445210): to the guns (0x18) while she's had fewer than 3 bursts (+0x47cc);
 *   to a new spot (0x28) if her magazine (+0x47e0) is empty or she last did 0x1a; else 0x1a.
 * - The gun (0x18, FUN_004455d0), by Dante's distance: under 300 she escapes (0x1d); under 500 she moves (0x28);
 *   under 800 the second gun (0x19) while bursts are under 3; beyond, she shoots (0x34, 0x35 / 0x36 with Dante above
 *   or below by 15 degrees, FUN_0044ad50) when he's within 45 degrees of her facing (0x61a500-0x61a504), else she
 *   turns (0x31); she turns at 1820 a frame (10 degrees, 0x61a4f4).
 * - Her magazine holds 50 (0x61a514); a shot takes one (FUN_0044d3e0); empty, she reloads (mode 0x2d, action 0x2b).
 *   After 3 bursts she pauses (mode 0x2c, action 0x2a), and the count starts again.
 * - Her health is 4000 (0x5cbe40 +0x2c); her attack records are at 0x5cbc40 (80, 500, 1500, 1000, 500, 30, 500, 50).
 * The walk, waypoint rings, special gate, grapple and ledge states below are decoded from the same class.
 * The grapple uses its hook and skinned cable, the held launcher frame, and the captured firing direction.
 * Remaining stand-ins (weapon choices/events, empty motion fallbacks and flinches) are listed in StandIns.md. */
#include <math.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include "lady_boss.h"
#include "fx.h"
#include "stage.h"
#include "lady_wire_runtime.h"

/* Action a's motion (FUN_0044afc0): 0-0x0e PAC [2] slot a, 0x0f-0x29 [3] a-0x0f, 0x2a-0x2d [4] a-0x2a, 0x2e-0x52
 * [5] a-0x2e, 0x53 on [6] a-0x53. The idle, strafes, grenade, Kalina Ann, escape and flinches are the stand-in's
 * picks from before (their actions aren't placed). */
const char *const lady_motion_names[LADY_M_COUNT] = {
    "ld200", "ld210", "ld211",
    "ld502", "ld503", "ld506", "ld507", "ld508",   /* actions 0x30, 0x31, 0x34-0x36 */
    "ld516", "ld517", "ld520", "ld521", "ld522",   /* 0x3e, 0x3f, 0x42-0x44 */
    "ld523", "ld530",                              /* 0x45 her walk, 0x4c her run */
    "ld400", "ld401",                              /* 0x2a the pause, 0x2b the reload */
    "ld610", "ld630", "ld304", "ld641", "ld644",
    /* 0x85; 0x5c (its slot, [6] 9, is empty: the gun's draw stands in), 0x56-0x59; 0x72, 0x73 */
    "ld650", "ld502", "ld603", "ld604", "ld605", "ld606", "ld631", "ld632",
    /* 0x1d's grapple 0x5e, 0x5f, 0x61, 0x62; mode 0x1e's 0x8f, 0x0c, 0x0e */
    "ld611", "ld612", "ld614", "ld615", "ld660", "ld212", "ld614"};   /* 0x0e's slot, [2] 14, is empty: 0x61's landing */

enum { L_IDLE, L_GUN, L_GUN2, L_TURN, L_SHOOT, L_SHOOT2, L_MOVE, L_PAUSE, L_RELOAD, L_GRENADE, L_KALINA, L_ESCAPE,
       L_FLINCH, L_KNOCK, L_DOWN, L_POSE, L_BIG, L_VOLLEY, L_LEDGE };
static const unsigned char action_motion[] = {LADY_M_WALK, LADY_M_GUN, LADY_M_GUN2, LADY_M_TURN, LADY_M_SHOT,
    LADY_M_SHOT2, LADY_M_RUN, LADY_M_PAUSE, LADY_M_RELOAD, LADY_M_GRENADE, LADY_M_KALINA, LADY_M_GRENADE,
    LADY_M_FLINCH, LADY_M_KNOCK, LADY_M_KNOCK, LADY_M_POSE, LADY_M_AIM, LADY_M_KALINA, LADY_M_LEDGE_SHOT};

/* Her waypoints (FUN_00448df0 / FUN_00448920 / FUN_00445590): two rings of 16 about the room's middle (0x5cc350), the
 * floor's (0x5cc1d0, radius about 1270, y 1) and the raised middle's (0x5cc290, about 680, y 101). She takes the nearer
 * ring's nearest point, then the next one on round the side she's on (FUN_004494b0), jittered by +-150 (the floor's,
 * 0x61a4ec) or +-100 (0x61a4f0). */
static const float ring_floor[16][2] = {{2500, 1126}, {2014, 1226}, {1599, 1499}, {1326, 1914}, {1226, 2400}, {1326, 2886},
    {1599, 3301}, {2014, 3574}, {2500, 3674}, {2986, 3574}, {3401, 3301}, {3674, 2886}, {3774, 2400}, {3674, 1914},
    {3401, 1499}, {2986, 1226}};
static const float ring_middle[16][2] = {{2500, 1723}, {2242, 1777}, {2022, 1922}, {1877, 2142}, {1823, 2400}, {1877, 2658},
    {2022, 2878}, {2242, 3023}, {2500, 3077}, {2758, 3023}, {2978, 2878}, {3123, 2658}, {3177, 2400}, {3123, 2142},
    {2978, 1922}, {2758, 1777}};
/* The five ledges she grapples onto (0x5cc360, y 501). */
static const float ledges[5][2] = {{1940, 1629}, {1594, 2694}, {2500, 3353}, {3406, 2694}, {3060, 1629}};
#define MIDDLE_X 2500.0f
#define MIDDLE_Z 2400.0f
/* Her attack gate (+0x130, FUN_0032b450 / FUN_0032b5a0; its block 0x5cc100): a gauge filling 1 a frame with Dante within
 * 1500 and 30 degrees of her facing, else 2 within 300; it opens past 100 (difficulty under 300). */
#define SPECIAL_GATE 100.0f

static void waypoint_pick(LadyBrain *b, float (*unit)(void)) {
    int r, i, best[2] = {0, 0};
    float bd[2] = {1e12f, 1e12f};
    for (r = 0; r < 2; ++r)
        for (i = 0; i < 16; ++i) {
            const float *p = r ? ring_floor[i] : ring_middle[i];
            float dx = p[0] - b->pos[0], dz = p[1] - b->pos[2], d = dx * dx + dz * dz;
            if (d < bd[r]) { bd[r] = d; best[r] = i; }
        }
    b->ring = bd[1] < bd[0];
    /* FUN_004494b0: which way round the middle she's going (her last spot, +0x150, against now: the cross product's
     * sign); FUN_00445590: the next point that way. Her last spot is taken as her last target. */
    {
        float ax = b->to[0] - MIDDLE_X, az = b->to[2] - MIDDLE_Z, bx = b->pos[0] - MIDDLE_X, bz = b->pos[2] - MIDDLE_Z;
        b->side = az * bx - ax * bz < 0.0f;
    }
    b->spot = best[b->ring];
    b->spot = b->side ? (b->spot > 0 ? b->spot - 1 : 15) : (b->spot < 15 ? b->spot + 1 : 0);
    {
        const float *p = b->ring ? ring_floor[b->spot] : ring_middle[b->spot];
        float jitter = b->ring ? 300.0f : 200.0f;
        b->to[0] = p[0] + jitter * (unit() - 0.5f);
        b->to[2] = p[1] + jitter * (unit() - 0.5f);
    }
}

static void waypoint_next(LadyBrain *b, float (*unit)(void)) {
    b->spot = b->side ? (b->spot > 0 ? b->spot - 1 : 15) : (b->spot < 15 ? b->spot + 1 : 0);
    {
        const float *p = b->ring ? ring_floor[b->spot] : ring_middle[b->spot];
        float jitter = b->ring ? 300.0f : 200.0f;
        b->to[0] = p[0] + jitter * (unit() - 0.5f);
        b->to[2] = p[1] + jitter * (unit() - 0.5f);
    }
}

/* FUN_00359c00 toward a point. */
static void face_point(LadyBrain *b, const float to[3], float rate, float f) {
    float want = atan2f(to[0] - b->pos[0], to[2] - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    b->yaw += d > rate * f ? rate * f : d < -rate * f ? -rate * f : d;
}

#define DEG 0.0174532925f
#define TURN (1820.0f / 65536.0f * 6.2831853f)   /* 0x61a4f4 a frame */
#define MAGAZINE 50
#define BURSTS 3
#define WALK_SPEED (323.0f / 38.0f)
#define ESCAPE_SPEED (704.0f / 106.0f)
#define ROOM_RADIUS 1500.0f
#define KNOCK_HITS 10.0f
#define BULLET_SPEED 30.0f
#define ROCKET_SPEED 16.0f
static const float record_damage[4] = {80.0f, 500.0f, 1000.0f, 1500.0f};   /* pistols, second gun, grenade, rocket */

int lady_loops(int motion) { return motion == LADY_M_IDLE || motion == LADY_M_WALK || motion == LADY_M_RUN; }

/* Another motion within the action. */
static void play(LadyBrain *b, int m) { b->want = m; b->restart = 1; b->done = 0; b->frame = 0.0f; }

static void act(LadyBrain *b, int a) {
    b->action = a;
    b->want = action_motion[a];
    b->restart = 1;
    b->timer = 0.0f;
    b->step = 0;
    b->done = 0;
    b->frame = 0.0f;
}

void lady_spawn(LadyBrain *b, int health) {
    int i;
    b->health = b->max_health = health;
    b->to[0] = b->pos[0]; b->to[1] = b->pos[1]; b->to[2] = b->pos[2];
    b->ring = 1; b->spot = 0; b->side = 0; b->volleys = 0;
    b->token = 0.0f;
    b->air = b->ledge = 0; b->speed = b->clock = 0.0f;
    b->wire = 0;
    b->land = 0;
    b->gauge = 0.0f;
    b->magazine = MAGAZINE;
    b->bursts = 0;
    b->last_mode = -1;
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < LADY_SHOTS; ++i) b->shots[i].on = 0;
    b->ally = b->has_target = 0;
    act(b, L_IDLE);
}

/* FUN_00359c00: toward Dante by at most `rate` a frame. Returns how far off she still is. */
static float face(LadyBrain *b, float px, float pz, float rate, float f) {
    float want = atan2f(px - b->pos[0], pz - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > rate * f) { b->yaw += rate * f; return d - rate * f; }
    if (d < -rate * f) { b->yaw -= rate * f; return d + rate * f; }
    b->yaw += d;
    return 0.0f;
}

static void move(LadyBrain *b, float fwd, float side, float f) {
    float s = sinf(b->yaw), c = cosf(b->yaw);
    b->pos[0] += (s * fwd + c * side) * f;
    b->pos[2] += (c * fwd - s * side) * f;
}

static void fire(LadyBrain *b, int kind, float px, float py, float pz, float spread) {
    int i;
    for (i = 0; i < LADY_SHOTS; ++i) {
        LadyShot *s = &b->shots[i];
        float dx, dy, dz, d;
        if (s->on) continue;
        s->on = 1;
        s->kind = kind;
        s->pos[0] = b->pos[0] + sinf(b->yaw) * 60.0f;
        s->pos[1] = b->pos[1] + 120.0f;
        s->pos[2] = b->pos[2] + cosf(b->yaw) * 60.0f;
        dx = px - s->pos[0] + spread * cosf(b->yaw); dy = py + 100.0f - s->pos[1]; dz = pz - s->pos[2] - spread * sinf(b->yaw);
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 1.0f) d = 1.0f;
        if (kind == 1) {
            float t = 50.0f;
            s->vel[0] = dx / t; s->vel[2] = dz / t; s->vel[1] = 0.5f * 0.6f * t;
            s->life = 110.0f;
        } else {
            float speed = kind == 2 ? ROCKET_SPEED : BULLET_SPEED;
            s->vel[0] = dx / d * speed; s->vel[1] = dy / d * speed; s->vel[2] = dz / d * speed;
            s->life = kind == 2 ? 140.0f : 60.0f;
        }
        return;
    }
}

static void landed(LadyBrain *b, int heavy, float damage) { b->land = heavy ? 2 : 1; b->land_damage = damage; }

static void shots_update(LadyBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < LADY_SHOTS; ++i) {
        LadyShot *s = &b->shots[i];
        float dx, dy, dz;
        if (!s->on) continue;
        if (s->kind == 1) {
            if (s->pos[1] > b->floor_y + 10.0f || s->vel[1] > 0.0f) {
                s->vel[1] -= 0.6f * f;
                s->pos[0] += s->vel[0] * f; s->pos[1] += s->vel[1] * f; s->pos[2] += s->vel[2] * f;
                if (s->pos[1] < b->floor_y + 10.0f) { s->pos[1] = b->floor_y + 10.0f; s->vel[1] = 0.0f; }
            }
            if ((s->life -= f) <= 0.0f) {
                dx = px - s->pos[0]; dz = pz - s->pos[2];
                fx_explosion(s->pos);
                if (attacks && dx * dx + dz * dz < 250.0f * 250.0f && fabsf(py - s->pos[1]) < 250.0f) landed(b, 1, record_damage[2]);
                s->on = 0;
            }
            continue;
        }
        s->pos[0] += s->vel[0] * f; s->pos[1] += s->vel[1] * f; s->pos[2] += s->vel[2] * f;
        s->life -= f;
        dx = px - s->pos[0]; dy = py + 100.0f - s->pos[1]; dz = pz - s->pos[2];
        if (dx * dx + dz * dz < (s->kind == 2 ? 120.0f * 120.0f : 60.0f * 60.0f) && fabsf(dy) < 140.0f) {
            if (s->kind == 2) fx_explosion(s->pos);
            if (attacks) landed(b, s->kind != 0, s->kind == 2 ? record_damage[3] : s->kind == 3 ? record_damage[1] : record_damage[0]);
            s->on = 0;
            continue;
        }
        if (s->life <= 0.0f) { if (s->kind == 2) fx_explosion(s->pos); s->on = 0; }
    }
}

/* The modes' choice: FUN_00445210 from her walk, FUN_004455d0 / FUN_00445a20 between bursts. */
static void decide(LadyBrain *b, float d, float off, int from_walk) {
    if (!from_walk) {
        if (d < 300.0f) { b->last_mode = 0x1d; act(b, L_ESCAPE); return; }
        if (d < 500.0f) { b->last_mode = 0x28; act(b, L_MOVE); return; }
    }
    if (b->bursts < BURSTS) {
        if (from_walk) { b->last_mode = 0x18; act(b, L_GUN); return; }
        /* Under 800 the second gun (mode 0x19: drawn 0x3e, fired 0x42-0x44), beyond it the pistols (0x34-0x36); either
         * fires only with him within 45 degrees of her facing, else she turns (0x31 / 0x3f). */
        if (d < 800.0f && b->action != L_GUN2 && b->last_mode != 0x19) { b->last_mode = 0x19; act(b, L_GUN2); return; }
        if (fabsf(off) > 45.0f * DEG) { act(b, L_TURN); return; }
        act(b, d < 800.0f ? L_SHOOT2 : L_SHOOT);
        return;
    }
    if (b->magazine < 1 || b->last_mode == 0x1a) { b->last_mode = 0x28; act(b, L_MOVE); return; }
    b->last_mode = 0x1a;
    act(b, L_POSE);   /* FUN_00445e30: action 0x85, turning to him till it's played */
}

/* FUN_004492a0, when her special gate opens: by her health's thirds (FUN_00449230), full 0x1b (the big shot) if he's
 * in sight, the middle third 0x1d (her escape), the last 0x1c (her volley). 1 if she took one. */
static int special(LadyBrain *b, int sight) {
    float h = (float)b->health / (float)(b->max_health > 0 ? b->max_health : 1);
    if (b->token <= SPECIAL_GATE) return 0;
    if (h >= 0.66f) {
        if (!sight) return 0;
        b->last_mode = 0x1b; act(b, L_BIG);
    } else if (h >= 0.33f) {
        b->last_mode = 0x1d; act(b, L_ESCAPE);
    } else {
        b->last_mode = 0x1c; act(b, L_VOLLEY);
    }
    b->token = 0.0f;   /* FUN_0032b610 */
    return 1;
}

/* 0044d080's held Kalina Ann: T(-8.4,-1,-1.3), Rz(pi), attached to body bone 9
 * (5c9520). 0044b780 transforms its local +X as a direction. 00446330 fires
 * from 5c9610 (87.8,0,4.28); 0049f020 attaches the cable to its second bone,
 * model [20] bone 2 at (65.2,0,4.3). Body matrices are in her own space. */
void lady_wire_pose(LadyBrain *b, const float rot[3][3], const float pos[3]) {
    static const float muzzle[3] = {-8.4f-87.8f,-1.0f,-1.3f+4.28f};
    static const float attach[3] = {-8.4f-65.2f,-1.0f,-1.3f+4.3f};
    float sn = sinf(b->yaw), cs = cosf(b->yaw), m[3], a[3], d[3];
    int i;
    for (i = 0; i < 3; ++i) {
        m[i] = pos[i] + rot[i][0]*muzzle[0] + rot[i][1]*muzzle[1] + rot[i][2]*muzzle[2];
        a[i] = pos[i] + rot[i][0]*attach[0] + rot[i][1]*attach[1] + rot[i][2]*attach[2];
        d[i] = -rot[i][0];
    }
    b->wire_muzzle[0] = b->pos[0] + cs*m[0] + sn*m[2];
    b->wire_muzzle[1] = b->pos[1] + m[1];
    b->wire_muzzle[2] = b->pos[2] + cs*m[2] - sn*m[0];
    b->wire_attach[0] = b->pos[0] + cs*a[0] + sn*a[2];
    b->wire_attach[1] = b->pos[1] + a[1];
    b->wire_attach[2] = b->pos[2] + cs*a[2] - sn*a[0];
    /* The firing direction is captured once; it must not follow her climb pose. */
    if (!b->wire) {
        b->wire_dir[0] = cs*d[0] + sn*d[2];
        b->wire_dir[1] = d[1];
        b->wire_dir[2] = cs*d[2] - sn*d[0];
    }
}

static void wire_update(LadyBrain *b, float frames) {
    /* 0049ec80: the hook advances by the passed velocity until a shell-hit
     * collision marks it caught; 0049edf0 leaves it at that collision. */
    while (frames > 0.0f && b->wire == 1) {
        float f = fminf(frames, 1.0f), next[3], t;
        int i;
        for (i = 0; i < 3; ++i) next[i] = b->wire_pos[i] + b->wire_dir[i]*50.0f*f;
        if (stage_loaded() && stage_raycast(b->wire_pos, next, &t)) {
            for (i = 0; i < 3; ++i) b->wire_pos[i] += (next[i]-b->wire_pos[i])*t;
            b->wire = 2;
        } else memcpy(b->wire_pos, next, sizeof next);
        frames -= f;
    }
}

/* ---- The ally (Mission 20's ending street): variant 1 sends every mode of FUN_00444c90 to 0x30, and her modes are
 * 0x2f-0x33 (FUN_004480f0, 00448350, 00448650, 00448710, 00448010). She aims at FUN_0044c8b0's pick (the caller's
 * target: the nearest enemy to her), shoots every 20 frames (0x61a568), the rocket once her counter (+0x4838) reaches
 * 100 (0x61a570), and regroups behind Dante. Her shots fly at the target and only it can take them [stand-in]. */
#define ALLY_FIRE 20.0f           /* 0x61a568 */
#define ALLY_ROCKET 100.0f        /* 0x61a570 */
#define ALLY_ROCKET_STEP 10.0f    /* what fills +0x4838 isn't decoded: a tenth a burst [estimate] */
#define ALLY_REGROUP 600.0f       /* 0x61a468; Dante between 300 and 600 (0x61a46c / 0x61a470, 0x61a45c / 0x61a460) */
#define ALLY_SPOT_NEAR 100.0f     /* 0x61a464 */
#define ALLY_RUN_TURN (5461.0f / 65536.0f * 6.2831853f)   /* FUN_004480f0's 5461 a frame */
#define ALLY_BURST_SHOTS 3        /* 0x85's shots [estimate: its events aren't read] */
enum { A_REGROUP = 0x2f, A_AIM = 0x30, A_SHOOT = 0x31, A_ROCKET = 0x32, A_WAIT = 0x33 };

void lady_make_ally(LadyBrain *b) {
    b->ally = 1;
    b->mode = A_AIM; b->step = 0;
    b->shot_clock = b->rocket = b->regroup = 0.0f;
}

static void ally_mode(LadyBrain *b, int m) { b->mode = m; b->step = 0; }

/* Like shots_update, against her target: what reaches it hurts it (the caller applies ally_hit). */
static void ally_shots(LadyBrain *b, float f) {
    int i;
    for (i = 0; i < LADY_SHOTS; ++i) {
        LadyShot *s = &b->shots[i];
        float dx, dy, dz;
        if (!s->on) continue;
        s->pos[0] += s->vel[0] * f; s->pos[1] += s->vel[1] * f; s->pos[2] += s->vel[2] * f;
        s->life -= f;
        dx = b->target[0] - s->pos[0]; dy = b->target[1] + 100.0f - s->pos[1]; dz = b->target[2] - s->pos[2];
        if (b->has_target && dx * dx + dz * dz < (s->kind == 2 ? 120.0f * 120.0f : 60.0f * 60.0f) && fabsf(dy) < 140.0f) {
            if (s->kind == 2) fx_explosion(s->pos);
            if (b->ally_hit < 2) { b->ally_hit = s->kind == 2 ? 2 : 1; b->ally_hit_damage = s->kind == 2 ? record_damage[3] : record_damage[0]; }
            s->on = 0;
            continue;
        }
        if (s->life <= 0.0f) { if (s->kind == 2) fx_explosion(s->pos); s->on = 0; }
    }
}

static void ally_think(LadyBrain *b, float f, float px, float py, float pz) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz);
    float *t = b->target;
    b->land = 0; b->ally_hit = 0;
    b->timer += f;
    b->regroup += f;
    ally_shots(b, f);
    switch (b->mode) {
    case A_AIM:   /* FUN_00448350 */
        if (b->step == 0) { play(b, LADY_M_WALK); b->step = 1; }   /* action 0x45 */
        if (!b->has_target) { ally_mode(b, A_WAIT); break; }
        {
            float off = face(b, t[0], t[2], TURN, f);
            /* Within her cone (0x61a500: +-45 degrees, reach 10000): action 0x49, her aim (its slot isn't one of the
             * port's motions: the drawn guns, 0x30's, stand in). */
            if (b->step == 1 && fabsf(off) < 45.0f * DEG) { play(b, LADY_M_GUN); b->step = 2; }
        }
        if (b->step == 2) {
            b->shot_clock += f;
            if (b->shot_clock >= ALLY_FIRE) {
                b->shot_clock = 0.0f;
                ally_mode(b, b->rocket >= ALLY_ROCKET ? A_ROCKET : A_SHOOT);
            } else if (b->regroup > ALLY_REGROUP && d > 300.0f && d < 600.0f) ally_mode(b, A_REGROUP);
        }
        break;
    case A_SHOOT:   /* FUN_00448650: action 0x85, turning to the target, till it's played */
        if (b->step == 0) { play(b, LADY_M_POSE); b->step = 1; b->volleys = 0; }
        if (b->has_target) face(b, t[0], t[2], TURN, f);
        if (b->volleys < ALLY_BURST_SHOTS && b->frame >= 6.0f + 6.0f * b->volleys) {   /* [estimate] */
            if (b->has_target) fire(b, 0, t[0], t[1], t[2], 0.0f);
            ++b->volleys;
        }
        if (b->done) { b->rocket += ALLY_ROCKET_STEP; ally_mode(b, A_AIM); }
        break;
    case A_ROCKET:   /* FUN_00448710: action 0x56 (0x57 / 0x58 by the target's height, FUN_0044ad40) */
        if (b->step == 0) {
            float up = atan2f(t[1] - b->pos[1], sqrtf((t[0] - b->pos[0]) * (t[0] - b->pos[0]) + (t[2] - b->pos[2]) * (t[2] - b->pos[2])));
            play(b, LADY_M_BIG + (up > 15.0f * DEG ? 1 : up < -15.0f * DEG ? 2 : 0));
            b->step = 1;
        }
        if (b->has_target) face(b, t[0], t[2], TURN, f);
        if (b->step == 1 && b->frame > 20.0f) {   /* the shot's frame, as her boss's [estimate] */
            float at[3] = {b->pos[0] + sinf(b->yaw) * 80.0f, b->pos[1] + 130.0f, b->pos[2] + cosf(b->yaw) * 80.0f};
            fx_explosion(at);
            if (b->has_target) fire(b, 2, t[0], t[1], t[2], 0.0f);
            b->step = 2;
        }
        if (b->done && b->step >= 2) { b->rocket = 0.0f; ally_mode(b, A_AIM); }
        break;
    case A_WAIT:   /* FUN_00448010: the guns drawn (action 0x30); a target -> 0x30; Dante 300-600 away -> 0x2f */
        if (b->step == 0) { play(b, LADY_M_GUN); b->step = 1; }
        if (b->has_target) ally_mode(b, A_AIM);
        else if (d > 300.0f && d < 600.0f) ally_mode(b, A_REGROUP);
        break;
    case A_REGROUP:   /* FUN_004480f0: run to 100 behind Dante (his yaw), then turn her back to him */
        if (b->step == 0) { play(b, LADY_M_RUN); b->step = 1; }
        if (b->step == 1) {
            float spot[3] = {px - sinf(b->dante_yaw) * 100.0f, py, pz - cosf(b->dante_yaw) * 100.0f};
            float sx = spot[0] - b->pos[0], sz = spot[2] - b->pos[2];
            face_point(b, spot, ALLY_RUN_TURN, f);
            move(b, 530.0f / 30.0f * 0.5f, 0.0f, f);   /* her run, as her boss's [estimate] */
            if (sx * sx + sz * sz < ALLY_SPOT_NEAR * ALLY_SPOT_NEAR) { play(b, LADY_M_GUN); b->step = 2; }
        } else {
            float away[3] = {2.0f * b->pos[0] - px, py, 2.0f * b->pos[2] - pz};
            float want = atan2f(away[0] - b->pos[0], away[2] - b->pos[2]) - b->yaw;
            while (want > 3.14159265f) want -= 6.2831853f;
            while (want < -3.14159265f) want += 6.2831853f;
            face_point(b, away, ALLY_RUN_TURN, f);
            if (fabsf(want) < ALLY_RUN_TURN * f + 0.001f) { b->regroup = 0.0f; ally_mode(b, A_AIM); }
        }
        break;
    default: ally_mode(b, A_AIM); break;
    }
}

void lady_think(LadyBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks, int sight) {
    if (b->ally) { ally_think(b, f, px, py, pz); return; }
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz), off;
    b->land = 0;
    b->timer += f;
    wire_update(b, f);
    shots_update(b, f, px, py, pz, attacks);
    if (b->gauge > 0.0f) b->gauge -= 0.01f * f;
    off = atan2f(dx, dz) - b->yaw;
    while (off > 3.14159265f) off -= 6.2831853f;
    while (off < -3.14159265f) off += 6.2831853f;
    /* The gate's rows, the first that holds him (FUN_0032b450), ticked only in her walk, guns and run (modes below
     * 0x18, 0x18, 0x19 and 0x28: FUN_00445210 / 004455d0 / 00445a20 / 00447850). */
    if (b->action > L_MOVE) ;
    else if (d < 1500.0f && fabsf(off) < 30.0f * DEG) b->token += 1.0f * f;
    else if (d < 300.0f) b->token += 2.0f * f;
    switch (b->action) {
    case L_IDLE:
        /* Her walk (FUN_00445210): toward her next waypoint (picked as she starts), at 1 degree a frame. In sight:
         * her special if its gate is open, else the guns (bursts under 3), a new spot (magazine empty or after 0x1a)
         * or 0x1a. Out of sight: reload when empty, pause after 3 bursts, else on round her ring. */
        if (b->step == 0) { waypoint_pick(b, unit); b->step = 1; }
        if (b->timer > 10.0f && special(b, sight)) break;
        if (sight && b->timer > 10.0f) { decide(b, d, off, 1); break; }
        if (!sight) {
            if (b->bursts >= BURSTS) { act(b, L_PAUSE); break; }
            if (b->magazine < 1) { act(b, L_RELOAD); break; }
        }
        face_point(b, b->to, 182.0f / 65536.0f * 6.2831853f, f);
        move(b, WALK_SPEED, 0.0f, f);
        {
            float tx = b->to[0] - b->pos[0], tz = b->to[2] - b->pos[2];
            if (tx * tx + tz * tz <= 300.0f * 300.0f) waypoint_next(b, unit);
        }
        break;
    case L_POSE:
        face(b, px, pz, TURN, f);   /* 0x61a4f8: 1820 a frame */
        if (b->done) act(b, L_IDLE);
        break;
    case L_BIG:
        /* FUN_00445f00: 0x5c, then the big shot (0x56, or 0x57 / 0x58 with him above / below by 15 degrees), then 0x59,
         * turning to him throughout. What the shot is [inferred]: Kalina Ann's rocket, on its motion's event. */
        face(b, px, pz, TURN, f);
        if (b->done) {
            if (b->step == 0) {
                float up = atan2f(py - b->pos[1], d);
                b->step = 1; b->done = 0; b->frame = 0.0f; b->restart = 1;
                b->want = LADY_M_BIG + (up > 15.0f * DEG ? 1 : up < -15.0f * DEG ? 2 : 0);
            } else if (b->step == 1) {
                b->step = 3; b->done = 0; b->frame = 0.0f; b->restart = 1; b->want = LADY_M_BIG_END;
            } else act(b, L_IDLE);
        }
        if (b->step == 1 && b->frame > 20.0f) {   /* the shot's frame [estimate] */
            float at[3] = {b->pos[0] + sinf(b->yaw) * 80.0f, b->pos[1] + 130.0f, b->pos[2] + cosf(b->yaw) * 80.0f};
            b->step = 2;
            fx_explosion(at);
            fire(b, 2, px, py, pz, 0.0f);
        }
        break;
    case L_VOLLEY:
        /* FUN_004460c0: 0x71, then 0x72 twice (0x5cc140), then 0x73, turning toward her next waypoint at 0x61a4f4. What
         * each 0x72 sends [inferred]: a grenade at him. */
        if (b->step == 0) { waypoint_pick(b, unit); b->volleys = 2; b->step = 1; }
        face_point(b, b->to, TURN, f);
        if (b->done) {
            if (b->volleys > 0) {
                --b->volleys;
                b->done = 0; b->frame = 0.0f; b->restart = 1; b->want = LADY_M_VOLLEY;
                fire(b, 1, px, py, pz, 0.0f);
            } else if (b->want != LADY_M_VOLLEY_END) {
                b->done = 0; b->frame = 0.0f; b->restart = 1; b->want = LADY_M_VOLLEY_END;
            } else act(b, L_IDLE);
        }
        break;
    case L_GUN: case L_GUN2: case L_TURN:
        /* Drawing the guns, or turning on him (0x30, 0x3e, 0x31). */
        face(b, px, pz, TURN, f);
        if (b->done || b->timer > 40.0f) decide(b, d, 0.0f, 0);
        break;
    case L_SHOOT: case L_SHOOT2: {
        /* A burst: 3 shots from the pistols, one from the second gun [estimates], each taking one from the magazine. */
        int shots = b->action == L_SHOOT ? 3 : 1;
        if (b->timer <= f) {
            /* FUN_0044ad50: Dante above or below her by 15 degrees, the up or down shot (+1, +2). */
            float up = atan2f(py - b->pos[1], d);
            b->want = (b->action == L_SHOOT ? LADY_M_SHOT : LADY_M_SHOT2) + (up > 15.0f * DEG ? 1 : up < -15.0f * DEG ? 2 : 0);
        }
        face(b, px, pz, TURN, f);
        while (b->step < shots && b->magazine > 0 && b->frame > 10.0f + 10.0f * (float)b->step) {
            ++b->step;
            --b->magazine;
            fire(b, b->action == L_SHOOT ? 0 : 3, px, py, pz, 0.0f);
        }
        if (b->done || b->timer > 90.0f) { ++b->bursts; act(b, L_IDLE); }
        break;
    }
    case L_MOVE:
        /* 0x28 (FUN_00447850): running for her next waypoint (turning 1820 a frame); within 300 the next. She runs on
         * while he's in sight and either she's had 3 bursts or he's within 800 (0x5cc128); else back to her walk. */
        if (b->step == 0) { waypoint_pick(b, unit); b->step = 1; }
        if (!sight || !(b->bursts >= BURSTS || d < 800.0f)) { act(b, L_IDLE); break; }
        face_point(b, b->to, TURN, f);
        move(b, 530.0f / 30.0f * 0.5f, 0.0f, f);   /* her run's root [estimate: ld530 isn't read for distance] */
        {
            float tx = b->to[0] - b->pos[0], tz = b->to[2] - b->pos[2];
            if (tx * tx + tz * tz <= 300.0f * 300.0f) waypoint_next(b, unit);
        }
        break;
    case L_PAUSE:
        if (b->timer > 32.0f || b->done) { b->bursts = 0; act(b, L_IDLE); }   /* 0x61a510 */
        break;
    case L_RELOAD:
        if (b->done) { b->magazine = MAGAZINE; b->bursts = 0; act(b, L_IDLE); }
        break;
    case L_GRENADE:
        face(b, px, pz, TURN, f);
        if (!b->step && b->frame > 25.0f) { b->step = 1; fire(b, 1, px, py, pz, 0.0f); }
        if (b->done) act(b, L_IDLE);
        break;
    case L_KALINA:
        face(b, px, pz, b->frame < 60.0f ? TURN : 0.0f, f);
        if (!b->step && b->frame > 80.0f) {
            float at[3] = {b->pos[0] + sinf(b->yaw) * 80.0f, b->pos[1] + 130.0f, b->pos[2] + cosf(b->yaw) * 80.0f};
            b->step = 1;
            fx_explosion(at);
            fire(b, 2, px, py, pz, 0.0f);
        }
        if (b->done) act(b, L_IDLE);
        break;
    case L_ESCAPE:
        /* FUN_00446330 (mode 0x1d), her grapple: 0x5d while she turns (1820) to her next waypoint; facing it, 0x5e
         * fires the wire; then 0x5f (0x60 above rank 0), rising at 20 a frame, 1 more each frame (0x61a524 /
         * 0x61a528), for up to 120 (0x61a534). At y 1200 (0x61a530) she lets go over a ledge (0x5cc360, y 501) in
         * her health's middle third (FUN_00449230 = 1), the one beside Dante's nearest that's nearer him, else over
         * her next waypoint, and drops (0x62) 20 a frame (0x61a52c) to land (0x61); on a ledge, mode 0x1e, else her
         * walk. Grenades as she lets go only above rank 1 (0x5cc150). */
        if (b->step == 0) { waypoint_pick(b, unit); b->ledge = 0; b->step = 1; }
        if (b->step == 1) {
            float want = atan2f(b->to[0] - b->pos[0], b->to[2] - b->pos[2]) - b->yaw;
            while (want > 3.14159265f) want -= 6.2831853f;
            while (want < -3.14159265f) want += 6.2831853f;
            face_point(b, b->to, TURN, f);
            if (b->done && fabsf(want) < TURN * f + 0.001f) {
                memcpy(b->wire_pos, b->wire_muzzle, sizeof b->wire_pos);
                b->wire = 1;
                play(b, LADY_M_WIRE); b->step = 2;
            }
        } else if (b->step == 2) {
            if (b->done) { play(b, LADY_M_ZIP); b->speed = 20.0f; b->clock = 120.0f; b->air = 1; b->step = 3; }
        } else if (b->step == 3) {
            b->speed += 1.0f * f;
            b->pos[0] += b->wire_dir[0] * b->speed * f;
            b->pos[1] += b->wire_dir[1] * b->speed * f;
            b->pos[2] += b->wire_dir[2] * b->speed * f;
            if (b->pos[1] >= 1200.0f) {
                float h = (float)b->health / (float)(b->max_health > 0 ? b->max_health : 1);
                if (h >= 0.33f && h < 0.66f) {
                    /* FUN_00449120: the ledge nearest Dante [inferred: by distance]; then whichever of its two
                     * neighbours is nearer him (+0x47c8 bit 0 clear). */
                    int i, n = 0, a, c;
                    float best = 1e12f, da, dc;
                    for (i = 0; i < 5; ++i) {
                        float lx = ledges[i][0] - px, lz = ledges[i][1] - pz;
                        if (lx * lx + lz * lz < best) { best = lx * lx + lz * lz; n = i; }
                    }
                    a = n ? n - 1 : 4; c = n == 4 ? 0 : n + 1;
                    da = (ledges[a][0] - px) * (ledges[a][0] - px) + (ledges[a][1] - pz) * (ledges[a][1] - pz);
                    dc = (ledges[c][0] - px) * (ledges[c][0] - px) + (ledges[c][1] - pz) * (ledges[c][1] - pz);
                    n = da < dc ? a : c;
                    b->pos[0] = ledges[n][0]; b->pos[2] = ledges[n][1];
                    b->ledge = 1;
                } else {
                    waypoint_pick(b, unit);
                    b->pos[0] = b->to[0]; b->pos[2] = b->to[2];
                }
                play(b, LADY_M_ZIP_FALL); b->step = 4;
                /* 0049f500 collapses the cable to the relocated hook on release. */
                memcpy(b->wire_pos, b->pos, sizeof b->wire_pos);
                b->wire = 2;
            } else if (b->wire && b->pos[1] > b->wire_pos[1]) { play(b, LADY_M_ZIP_FALL); b->step = 4; }
            else if ((b->clock -= f) < 0.0f) { play(b, LADY_M_ZIP_FALL); b->step = 4; }
            else if (b->done) play(b, LADY_M_ZIP);
        } else if (b->step == 4) {
            float floor = b->floor_y;   /* stage_floor includes the authored STAY crosspiece under her */
            b->pos[1] -= 20.0f * f;
            if (b->pos[1] <= floor) {
                b->pos[1] = floor; b->ledge = floor > 491.0f;
                b->air = b->ledge ? 3 : 0; b->wire = 0;
                play(b, LADY_M_ZIP_LAND); b->step = 5;
            }
        } else if (b->done) {
            if (b->pos[1] > 501.0f - 10.0f) { b->last_mode = 0x1e; act(b, L_LEDGE); }
            else act(b, L_IDLE);
        }
        break;
    case L_LEDGE:
        /* FUN_00446f60 (mode 0x1e): on her ledge, 0x8f 0x5cc160[rank] times (3), turning to him at 1820 (0x61a4f8);
         * then the hop down (0x0c) facing the middle if he's further out than she is, else away from it, falling 28 a
         * frame (+0x4824 = -28) to the floor, the landing (0x0e), then her walk. What 0x8f fires [inferred]: one
         * pistol shot. */
        if (b->step == 0) { b->volleys = 3; b->clock = 0.0f; b->step = 1; }
        if (b->step == 1) {
            face(b, px, pz, TURN, f);
            if (b->clock == 0.0f && b->frame > 10.0f) { b->clock = 1.0f; fire(b, 0, px, py, pz, 0.0f); }
            if (b->done) {
                if (--b->volleys < 1) {
                    float mx = b->pos[0] - MIDDLE_X, mz = b->pos[2] - MIDDLE_Z, ox = px - MIDDLE_X, oz = pz - MIDDLE_Z;
                    /* FUN_00359c80(middle, her): from her to the middle (it's how she turns to a target). */
                    b->yaw = atan2f(-mx, -mz) + (mx * mx + mz * mz < ox * ox + oz * oz ? 0.0f : 3.14159265f);
                    play(b, LADY_M_HOP); b->air = 2; b->ledge = 0; b->step = 2;
                } else { play(b, LADY_M_LEDGE_SHOT); b->clock = 0.0f; }
            }
        } else if (b->step == 2) {
            if (b->done) b->step = 3;   /* off the ledge by the hop's root travel */
        } else if (b->step == 3) {
            b->pos[1] -= 28.0f * f;
            if (b->pos[1] <= b->floor_y) { b->pos[1] = b->floor_y; b->air = 0; play(b, LADY_M_HOP_LAND); b->step = 4; }
        } else if (b->done) act(b, L_IDLE);
        break;
    case L_FLINCH: case L_KNOCK:
        if (b->action == L_KNOCK) move(b, -593.0f / 37.0f * 0.5f, 0.0f, f);
        if (b->done) act(b, L_IDLE);
        break;
    case L_DOWN:
        break;
    }
    {
        /* Kept within the room about its middle, outside her floor ring [the room's walls, approximated]. */
        float hx = b->pos[0] - MIDDLE_X, hz = b->pos[2] - MIDDLE_Z, l = sqrtf(hx * hx + hz * hz);
        if (l > ROOM_RADIUS) { b->pos[0] = MIDDLE_X + hx / l * ROOM_RADIUS; b->pos[2] = MIDDLE_Z + hz / l * ROOM_RADIUS; }
    }
}

int lady_hit(LadyBrain *b, int heavy, int mortal) {
    if (b->action == L_DOWN) return 0;
    if (mortal) {
        b->health -= heavy ? 2 : 1;
        if (b->health <= 0) {
            int i;
            for (i = 0; i < LADY_SHOTS; ++i) b->shots[i].on = 0;
            act(b, L_DOWN);
            b->air = 0;
            b->wire = 0;
            return 1;
        }
    }
    b->gauge += heavy ? 2.0f : 1.0f;
    if (b->air || b->action == L_ESCAPE || b->action == L_LEDGE) return 0;   /* on the wire or a ledge: no flinch [the port's] */
    if (b->gauge >= KNOCK_HITS) { b->gauge = 0.0f; act(b, L_KNOCK); }
    else if (b->action == L_IDLE || b->action == L_TURN || b->action == L_MOVE) act(b, L_FLINCH);
    return 0;
}

void lady_draw_fx(const LadyBrain *b) {
    int i;
    for (i = 0; i < LADY_SHOTS; ++i) {
        const LadyShot *s = &b->shots[i];
        float still[3] = {0.0f, 0.0f, 0.0f};
        if (!s->on) continue;
        if (s->kind == 0 || s->kind == 3) fx_mote(s->pos, still, s->kind ? 26.0f : 18.0f, 12.0f, 0.04f, 0xff60e0ffu);
        else if (s->kind == 1) {
            int blink = ((int)s->life / (s->life < 30.0f ? 3 : 8)) & 1;
            fx_mote(s->pos, still, 24.0f, 22.0f, 0.04f, 0xff304050u);
            if (blink) fx_mote(s->pos, still, 40.0f, 10.0f, 0.04f, 0xff2020ffu);
        } else fx_mote(s->pos, still, 60.0f, 40.0f, 0.04f, 0xff40a0ffu);
    }
}

/* 0049f020: five cable bones at quarters of the span, their X axes scaled by
 * the first segment's length / 70. Preserve the original skin weights. */
void lady_draw_wire(const LadyBrain *b) {
    float axis[3][3], delta[3], length, segment;
    int model, batch, i, j, k;
    if (!b->wire) return;
    for (k = 0; k < 3; ++k) delta[k] = b->wire_pos[k] - b->wire_attach[k];
    length = sqrtf(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    if (length < 0.01f) return;
    for (k = 0; k < 3; ++k) axis[0][k] = delta[k]/length;
    axis[2][0] = -axis[0][2]; axis[2][1] = 0; axis[2][2] = axis[0][0];
    segment = sqrtf(axis[2][0]*axis[2][0]+axis[2][2]*axis[2][2]);
    if (segment < 0.001f) { axis[2][0] = 0; axis[2][2] = 1; }
    else { axis[2][0] /= segment; axis[2][2] /= segment; }
    axis[1][0] = axis[2][1]*axis[0][2]-axis[2][2]*axis[0][1];
    axis[1][1] = axis[2][2]*axis[0][0]-axis[2][0]*axis[0][2];
    axis[1][2] = axis[2][0]*axis[0][1]-axis[2][1]*axis[0][0];
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    sceGuEnable(GU_TEXTURE_2D); sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_BLEND); sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0x20, 0xff);
    sceGuDepthMask(GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0); sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    segment = length * 0.25f;
    for (model = 0; model < 2; ++model) {
        const LadyWireModel *m = &lady_wire_models[model];
        for (batch = 0; batch < m->count; ++batch) {
            const LadyWireBatch *part = &m->batches[batch];
            LadyWireVertex *vertices = sceGuGetMemory(part->count * sizeof(*vertices));
            for (i = 0; i < part->count; ++i) {
                const LadyWireSkin *skin = &part->skin[i];
                const float *v = &skin->v.x;
                float out[3] = {0,0,0};
                vertices[i] = skin->v;
                for (j = 0; j < 3; ++j) {
                    int bone = skin->bones[j];
                    float w = skin->weights[j]/31.0f, local[3];
                    if (!skin->weights[j]) continue;
                    for (k = 0; k < 3; ++k) local[k] = v[k]-m->rest[bone][k];
                    if (model) local[0] *= segment/70.0f;
                    for (k = 0; k < 3; ++k) {
                        float base = model ? b->wire_attach[k]+delta[k]*(float)bone*0.25f : b->wire_pos[k];
                        out[k] += w*(base+axis[0][k]*local[0]+axis[1][k]*local[1]+axis[2][k]*local[2]);
                    }
                }
                vertices[i].x = out[0]; vertices[i].y = out[1]; vertices[i].z = out[2];
            }
            sceGuClutLoad(32, part->palette);
            sceGuTexImage(0, part->width, part->height, part->width, part->pixels);
            sceGuTexFlush();
            sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                            part->count, 0, vertices);
        }
    }
    sceGuEnable(GU_CULL_FACE); sceGuEnable(GU_BLEND); sceGuEnable(GU_LIGHTING);
}
