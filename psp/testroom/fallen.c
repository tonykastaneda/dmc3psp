/* The Fallen (CEm011, EST 29). Addresses are SLUS_213.61's; docs/FALLEN_SPEC.md has the decode and what's
 * approximated. Frames and distances keep the original 60 Hz / centimetre units; the PS2's 65536-to-a-turn angles
 * are converted where quoted. */
#include <math.h>
#include "fallen.h"

#define PI 3.14159265f
#define UNITS (2.0f * PI / 65536.0f)

/* Script ids (em011 [7]): 0:0 hover 001, 3:1 throw 301, 3:4 lunge 308, 3:2 slashes 303, 1:1 reaction 101,
 * 1:5 the fall 104, 0:5 rising 304, 2:0 appearing 200, 2:1 dying 202, 0:2 drift 002. */
const char *const fallen_motion_names[FALLEN_M_COUNT] = {
    "fa200", "fa001", "fa002", "fa101", "fa104", "fa304", "fa301", "fa308", "fa303", "fa202"
};
int fallen_loops(int motion) { return motion == FALLEN_M_HOVER || motion == FALLEN_M_DRIFT; }

/* The AI's states (+0x139a, FUN_00342ea0's switch). */
enum {
    S_APPEAR = 0x0e, S_THINK = 0x16, S_HOVER = 0x17, S_REACT = 0x18, S_GROUND = 0x1a, S_FALL = 0x1e, S_RISE = 0x1f,
    S_THROW = 0x20, S_SLASH_A = 0x22, S_SLASH_B = 0x23, S_SLASH_C = 0x24, S_LUNGE = 0x27, S_DEATH = 0x40
};

/* The scheduler's lists (config 0x5c3280, rank 0): flying (list 0, 0x5c2f90...) 0x27 80 / 0x20 20 (60 / 40 beyond
 * 1200); on the ground (list 1, 0x5c30b0...) 0x22 57, 0x23 21, 0x24 22. Bands 0x5c31f0: 300 400 600 800 900 1200. */
#define GAUGE_FULL 100.0f      /* 0x5c32a0 */
#define HOVER_RADIUS 320.0f    /* FUN_00344cc0 */
#define HOVER_PAUSE 5.0f       /* 0x40a00000 */
#define TURN_HOVER (0x400 * UNITS)   /* FUN_003441f0(.., 0x400) */
#define LUNGE_WIND 13.0f       /* FUN_00347470: 0x41500000 */
#define LUNGE_SPEED 20.0f      /* 0x41a00000, falling 1 a frame */
#define THROW_TURN (0x71c * UNITS)   /* FUN_00346bd0 */
#define ATTACK_REST 180.0f     /* FUN_003442c0 hands the manager 3.0 (seconds) */
/* [estimates] The class's damage handler isn't decoded: */
#define GUARD_BREAK 8.0f       /* front hits the closed wings take before he drops */
#define GROUND_TIME 600.0f     /* frames on the ground before he rises again */

static float wrap(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static void turn_to(FallenBrain *b, float x, float z, float rate, float f) {
    float d = wrap(atan2f(x - b->pos[0], z - b->pos[2]) - b->yaw);
    if (d > rate * f) d = rate * f;
    if (d < -rate * f) d = -rate * f;
    b->yaw = wrap(b->yaw + d);
}
static void forward(FallenBrain *b, float speed, float f) {
    b->pos[0] += sinf(b->yaw) * speed * f;
    b->pos[2] += cosf(b->yaw) * speed * f;
}
static void play(FallenBrain *b, int motion) {
    b->want = motion;
    b->restart = 1;
    b->done = 0;
    b->frame = 0.0f;
}
static void set_state(FallenBrain *b, int state) {
    b->state = state;
    b->timer = 0.0f;
    b->step = 0;
    switch (state) {
    case S_APPEAR: play(b, FALLEN_M_APPEAR); break;
    case S_HOVER: play(b, FALLEN_M_HOVER); break;
    case S_GROUND: play(b, FALLEN_M_DRIFT); break;
    case S_REACT: play(b, FALLEN_M_REACT); break;
    case S_FALL: play(b, FALLEN_M_FALL); break;
    case S_RISE: play(b, FALLEN_M_RISE); break;
    case S_THROW: play(b, FALLEN_M_THROW); break;
    case S_LUNGE: play(b, FALLEN_M_LUNGE); break;
    case S_SLASH_A: case S_SLASH_B: case S_SLASH_C: play(b, FALLEN_M_SLASHES); break;
    case S_DEATH: play(b, FALLEN_M_DEATH); break;
    default: break;
    }
}
int fallen_flying(const FallenBrain *b) { return b->mode != 1; }

void fallen_spawn(FallenBrain *b, int health) {
    b->health = health;
    b->land = b->spear = b->remove = 0;
    b->mode = 0;
    b->gauge = b->guard_hits = 0.0f;
    b->side = 0;
    b->bob = 0.0f;
    b->goal[0] = b->pos[0]; b->goal[1] = b->pos[1]; b->goal[2] = b->pos[2];
    set_state(b, S_APPEAR);   /* FUN_00342c70: state 0xe, the EST variant 0's action 0x1e (2:0, through the wall) */
}

/* FUN_00344cc0: a point 320 from Dante, 67.5-90 deg off the way he faces (0x3000 + rand 0x1000), alternating sides. */
static void hover_goal(FallenBrain *b, float px, float py, float pz, float (*unit)(void)) {
    float facing = atan2f(b->pos[0] - px, b->pos[2] - pz);   /* [approximation: from him, not his facing] */
    float off = (0x3000 + unit() * 0x1000) * UNITS;
    float a = facing + (b->side ? off : -off);
    b->goal[0] = px + sinf(a) * HOVER_RADIUS;
    b->goal[1] = py;
    b->goal[2] = pz + cosf(a) * HOVER_RADIUS;
}
/* The scheduler's gauge (config +0x18, 0x5c3230: 300 -> 15 a frame, 700 -> 10, 1500 -> 0.5) [the angle columns
 * aren't used]. */
static void fill_gauge(FallenBrain *b, float d, float f) {
    b->gauge += (d < 300.0f ? 15.0f : d < 700.0f ? 10.0f : d < 1500.0f ? 0.5f : 0.167f) * f;
}
static int pick(FallenBrain *b, float d, float (*unit)(void)) {
    float r = unit() * 100.0f;
    if (b->mode == 1) return r < 57.0f ? S_SLASH_A : r < 78.0f ? S_SLASH_B : S_SLASH_C;
    if (d > 1200.0f) return r < 60.0f ? S_LUNGE : S_THROW;
    if (d > 900.0f) return r < 70.0f ? S_LUNGE : S_THROW;
    if (d > 800.0f) return r < 75.0f ? S_LUNGE : S_THROW;
    return r < 80.0f ? S_LUNGE : S_THROW;
}
static void strike(FallenBrain *b, float px, float py, float pz, float reach, int heavy, int attacks) {
    float hx = b->pos[0] + sinf(b->yaw) * 120.0f - px, hz = b->pos[2] + cosf(b->yaw) * 120.0f - pz;
    if (attacks && hx * hx + hz * hz < reach * reach && fabsf(py + 100.0f - (b->pos[1] + 100.0f)) < 220.0f)
        b->land = heavy ? 2 : 1;
}

void fallen_think(FallenBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks, int *token) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz);
    b->land = b->spear = 0;
    b->timer += f;
    /* The hover's bob: +0x1348 turns 256 units a frame, y moves sin x 2 a frame. */
    if (b->mode != 1 && b->state != S_DEATH) {
        b->bob += 256.0f * UNITS * f;
        b->pos[1] += sinf(b->bob) * 2.0f * f;
    }
    switch (b->state) {
    case S_APPEAR:
        if (b->done) set_state(b, S_THINK);
        break;
    case S_THINK:
        /* 0x16: 0x17 (hover) in the air, 0x1a on the ground. */
        set_state(b, b->mode == 1 ? S_GROUND : S_HOVER);
        b->step = 1;
        break;
    case S_HOVER: {
        float gx, gy, gz, l;
        if (b->step == 0 || b->step == 1) { b->side ^= 1; hover_goal(b, px, py, pz, unit); b->step = 2; }
        gx = (b->goal[0] - b->pos[0]) / 30.0f * f;
        gz = (b->goal[2] - b->pos[2]) / 30.0f * f;
        gy = b->goal[1] - b->pos[1];
        gy = (gy > 0.0f ? gy / 30.0f : gy / 1000.0f) * f;   /* the PS2's y points down: quick to rise, slow to sink */
        l = sqrtf(gx * gx + gy * gy + gz * gz);
        if (l > 10.0f) { gx *= 10.0f / l; gy *= 10.0f / l; gz *= 10.0f / l; }
        b->pos[0] += gx; b->pos[1] += gy; b->pos[2] += gz;
        if (b->step == 2 && (b->goal[0] - b->pos[0]) * (b->goal[0] - b->pos[0]) + (b->goal[2] - b->pos[2]) * (b->goal[2] - b->pos[2]) < 40.0f * 40.0f) {
            b->step = 3; b->speed = HOVER_PAUSE;
        }
        if (b->step == 3 && (b->speed -= f) < 0.0f) b->step = 1;
        turn_to(b, px, pz, TURN_HOVER, f);
        fill_gauge(b, d, f);
        if (attacks && b->gauge > GAUGE_FULL && *token) {
            *token = 0;
            b->gauge = 0.0f;
            set_state(b, pick(b, d, unit));
        }
        break;
    }
    case S_GROUND:
        /* 0x1a (action 3): on the ground he closes in slowly [approximation: 3 a frame toward 320 from Dante]. */
        turn_to(b, px, pz, TURN_HOVER, f);
        if (d > HOVER_RADIUS) forward(b, 3.0f, f);
        fill_gauge(b, d, f);
        b->grounded_time += f;
        if (b->grounded_time > GROUND_TIME) { b->grounded_time = 0.0f; set_state(b, S_RISE); break; }
        if (attacks && b->gauge > GAUGE_FULL && *token) {
            *token = 0;
            b->gauge = 0.0f;
            set_state(b, pick(b, d, unit));
        }
        break;
    case S_LUNGE:
        /* Action 0x15 (3:4, motion 308): 13 frames turning 0x100 a frame, then a dash from 20 a frame slowing by 1;
         * attack record 4 on frames 35-45. */
        if (b->timer < LUNGE_WIND) { turn_to(b, px, pz, 0x100 * UNITS * 4.0f, f); b->speed = LUNGE_SPEED; }
        else if (b->speed > 0.0f) { forward(b, b->speed, f); b->speed -= f; }
        if (b->step == 0 && b->frame >= 35.0f) { b->step = 1; strike(b, px, py, pz, 200.0f, 1, attacks); }
        if (b->done) set_state(b, S_THINK);
        break;
    case S_THROW:
        /* Action 0xf (3:1, motion 301): turns 0x71c, and at frame 33 (attack record 0) the spear flies. */
        if (b->step == 0) { b->yaw = wrap(b->yaw + THROW_TURN); b->step = 1; }
        if (b->step == 1 && b->timer < 33.0f) turn_to(b, px, pz, 0.1f, f);
        if (b->step == 1 && b->frame >= 33.0f) { b->step = 2; if (attacks) b->spear = 1; }
        if (b->done) set_state(b, S_THINK);
        break;
    case S_SLASH_A: case S_SLASH_B: case S_SLASH_C:
        /* Actions 0x11 / 0x12 / 0x13 (3:2, motion 303): records 2, 2, 5 on frames 25, 60, 90. 0x12 and 0x13 step in
         * at 10 a frame [0x41200000]. */
        if (b->timer < 20.0f) turn_to(b, px, pz, 0.08f, f);
        if (b->state != S_SLASH_A && b->frame < 90.0f && d > 150.0f) forward(b, 10.0f, f);
        if (b->step == 0 && b->frame >= 25.0f) { b->step = 1; strike(b, px, py, pz, 220.0f, 0, attacks); }
        if (b->step == 1 && b->frame >= 60.0f) { b->step = 2; strike(b, px, py, pz, 220.0f, 0, attacks); }
        if (b->step == 2 && b->frame >= 90.0f) { b->step = 3; strike(b, px, py, pz, 240.0f, 1, attacks); }
        if (b->done) set_state(b, S_THINK);
        break;
    case S_REACT:
        if (b->done) set_state(b, S_THINK);
        break;
    case S_FALL:
        /* State 0x1e (action 0x1a, 1:5): down to the floor, mode 1 (effect 0x33f). */
        if (b->pos[1] > b->floor_y) b->pos[1] = fmaxf(b->floor_y, b->pos[1] - 15.0f * f);
        if (b->done && b->pos[1] <= b->floor_y + 1.0f) { b->mode = 1; b->grounded_time = 0.0f; set_state(b, S_THINK); }
        break;
    case S_RISE:
        /* State 0x1f (action 0xe, 0:5): mode 2, up for 120 frames, then mode 0. */
        b->mode = 2;
        b->pos[1] += 1.0f * f;
        if (b->timer > 120.0f) { b->mode = 0; b->guard_hits = 0.0f; set_state(b, S_THINK); }
        break;
    case S_DEATH:
        if (b->pos[1] > b->floor_y) b->pos[1] = fmaxf(b->floor_y, b->pos[1] - 10.0f * f);
        if (b->done || b->timer > 200.0f) b->remove = 1;
        break;
    }
    /* Never under the floor. In the air he keeps at least his own height over it. */
    if (b->pos[1] < b->floor_y) b->pos[1] = b->floor_y;
}

int fallen_hit(FallenBrain *b, int heavy, float fx, float fz, int mortal) {
    float tx = fx - b->pos[0], tz = fz - b->pos[2];
    int front = tx * sinf(b->yaw) + tz * cosf(b->yaw) > 0.0f;
    int damage = heavy ? 2 : 1;
    if (b->state == S_DEATH || b->state == S_APPEAR) return 0;
    /* In the air his wings close over the chest: from the front they take the hit [approximation]. */
    if (b->mode != 1 && front) {
        b->guard_hits += heavy ? 2.0f : 1.0f;
        damage = 0;
        if (b->guard_hits >= GUARD_BREAK) { set_state(b, S_FALL); return 0; }
    }
    if (mortal && damage) {
        b->health -= damage;
        if (b->health <= 0) { set_state(b, S_DEATH); return 1; }
    }
    if (damage && b->state != S_FALL && b->state != S_RISE &&
        (b->state == S_HOVER || b->state == S_GROUND || b->state == S_REACT || heavy)) set_state(b, S_REACT);
    return 0;
}
