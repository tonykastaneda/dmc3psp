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
 * Stand-ins: what mode 0x1a is (here she throws a grenade or fires Kalina Ann by turns), the escape (0x1d: a long
 * leap back with a grenade left), where she moves to (0x28: her spots 0x5cc1d0 / 0x5cc290 aren't read: she steps
 * to keep 500-800 from him), the shots in a burst (3) and which record each weapon uses (the pistols 80, the second
 * gun 500, a grenade 1000, the rocket 1500 [inferred]), her line of sight (always clear), her flinches. */
#include <math.h>
#include "lady_boss.h"
#include "fx.h"

/* Action a's motion (FUN_0044afc0): 0-0x0e PAC [2] slot a, 0x0f-0x29 [3] a-0x0f, 0x2a-0x2d [4] a-0x2a, 0x2e-0x52
 * [5] a-0x2e, 0x53 on [6] a-0x53. The idle, strafes, grenade, Kalina Ann, escape and flinches are the stand-in's
 * picks from before (their actions aren't placed). */
const char *const lady_motion_names[LADY_M_COUNT] = {
    "ld200", "ld210", "ld211",
    "ld502", "ld503", "ld506", "ld507", "ld508",   /* actions 0x30, 0x31, 0x34-0x36 */
    "ld516", "ld517", "ld520", "ld521", "ld522",   /* 0x3e, 0x3f, 0x42-0x44 */
    "ld523", "ld530",                              /* 0x45 her walk, 0x4c her run */
    "ld400", "ld401",                              /* 0x2a the pause, 0x2b the reload */
    "ld610", "ld630", "ld304", "ld641", "ld644"};

enum { L_IDLE, L_GUN, L_GUN2, L_TURN, L_SHOOT, L_SHOOT2, L_MOVE, L_PAUSE, L_RELOAD, L_GRENADE, L_KALINA, L_ESCAPE,
       L_FLINCH, L_KNOCK, L_DOWN };
static const unsigned char action_motion[] = {LADY_M_IDLE, LADY_M_GUN, LADY_M_GUN2, LADY_M_TURN, LADY_M_SHOT,
    LADY_M_SHOT2, LADY_M_WALK, LADY_M_PAUSE, LADY_M_RELOAD, LADY_M_GRENADE, LADY_M_KALINA, LADY_M_ESCAPE,
    LADY_M_FLINCH, LADY_M_KNOCK, LADY_M_KNOCK};

#define DEG 0.0174532925f
#define TURN (1820.0f / 65536.0f * 6.2831853f)   /* 0x61a4f4 a frame */
#define MAGAZINE 50
#define BURSTS 3
#define WALK_SPEED (323.0f / 38.0f)
#define ESCAPE_SPEED (704.0f / 106.0f)
#define ROOM_RADIUS 1300.0f
#define KNOCK_HITS 10.0f
#define BULLET_SPEED 30.0f
#define ROCKET_SPEED 16.0f
static const float record_damage[4] = {80.0f, 500.0f, 1000.0f, 1500.0f};   /* pistols, second gun, grenade, rocket */

int lady_loops(int motion) { return motion == LADY_M_IDLE || motion == LADY_M_WALK || motion == LADY_M_RUN; }

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
    b->health = health;
    b->land = 0;
    b->gauge = 0.0f;
    b->magazine = MAGAZINE;
    b->bursts = 0;
    b->last_mode = -1;
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < LADY_SHOTS; ++i) b->shots[i].on = 0;
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
    if (b->magazine < 1 || b->last_mode == 0x1a) { b->last_mode = 0x28; act(b, b->magazine < 1 ? L_RELOAD : L_PAUSE); return; }
    b->last_mode = 0x1a;
    {
        static int turn;   /* stand-in for mode 0x1a: a grenade and Kalina Ann by turns */
        act(b, (turn ^= 1) ? L_GRENADE : L_KALINA);
    }
}

void lady_think(LadyBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz), off;
    (void)unit;
    b->land = 0;
    b->timer += f;
    shots_update(b, f, px, py, pz, attacks);
    if (b->gauge > 0.0f) b->gauge -= 0.01f * f;
    off = atan2f(dx, dz) - b->yaw;
    while (off > 3.14159265f) off -= 6.2831853f;
    while (off < -3.14159265f) off += 6.2831853f;
    switch (b->action) {
    case L_IDLE:
        face(b, px, pz, TURN, f);
        if (b->timer > 20.0f) decide(b, d, off, 1);
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
        /* 0x28: to another spot; here, keeping 500-800 from him [stand-in]. */
        face(b, px, pz, TURN, f);
        move(b, d < 650.0f ? -WALK_SPEED * 0.6f : WALK_SPEED * 0.6f, 0.0f, f);
        if (b->timer > 60.0f) act(b, L_IDLE);
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
        face(b, px, pz, TURN, f);
        move(b, -ESCAPE_SPEED, 0.0f, f);
        if (!b->step && b->frame > 50.0f) { b->step = 1; fire(b, 1, px, py, pz, 0.0f); }
        if (b->done) act(b, L_IDLE);
        break;
    case L_FLINCH: case L_KNOCK:
        if (b->action == L_KNOCK) move(b, -593.0f / 37.0f * 0.5f, 0.0f, f);
        if (b->done) act(b, L_IDLE);
        break;
    case L_DOWN:
        break;
    }
    {
        float hx = b->pos[0] - b->home[0], hz = b->pos[2] - b->home[2], l = sqrtf(hx * hx + hz * hz);
        if (l > ROOM_RADIUS) { b->pos[0] = b->home[0] + hx / l * ROOM_RADIUS; b->pos[2] = b->home[2] + hz / l * ROOM_RADIUS; }
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
            return 1;
        }
    }
    b->gauge += heavy ? 2.0f : 1.0f;
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
