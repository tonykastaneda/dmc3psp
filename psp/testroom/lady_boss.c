/* Lady (class 37, EST 55; em034.pac at GDATA 138), Mission 16's boss. Her own model and motions; her fight is a stand-in
 * from the walkthrough, as her class's code isn't decoded: she keeps her distance, strafing and shooting her guns,
 * lobs grenades that beep and burst after a moment, fires Kalina Ann's rocket, and when Dante closes in she escapes
 * with a long backward leap (the repel gun). Hits fill a gauge that knocks her back. Speeds are her motions' root
 * travel (tools/prepare_skinned_model.py LADY_MOTIONS); timings, damage and her health are estimates. Bullets,
 * grenades and the rocket are glows, not models; her guns aren't drawn. */
#include <math.h>
#include "lady_boss.h"
#include "fx.h"

const char *const lady_motion_names[LADY_M_COUNT] = {
    "ld200", "ld202", "ld210", "ld211", "ld209", "ld502", "ld610", "ld630", "ld304", "ld641", "ld644", "ld401"};

enum { L_IDLE, L_RUN, L_STRAFE_R, L_STRAFE_L, L_BACK, L_SHOOT, L_GRENADE, L_KALINA, L_ESCAPE, L_FLINCH, L_KNOCK, L_DOWN };

#define RUN_SPEED (323.0f / 38.0f)
#define STRAFE_SPEED (467.0f / 55.0f)
#define BACK_SPEED (412.0f / 64.0f)
#define ESCAPE_SPEED (704.0f / 106.0f)
#define CLOSE 350.0f            /* too close: she backs off or escapes */
#define KEEP 900.0f             /* the distance she likes */
#define ROOM_RADIUS 1300.0f
#define KNOCK_HITS 10.0f
#define BULLET_SPEED 30.0f
#define ROCKET_SPEED 16.0f

int lady_loops(int motion) { return motion == LADY_M_IDLE || motion == LADY_M_RUN; }

static void act(LadyBrain *b, int a) {
    b->action = a;
    b->want = a;   /* actions and motions are in the same order */
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
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < LADY_SHOTS; ++i) b->shots[i].on = 0;
    act(b, L_IDLE);
}

static void face(LadyBrain *b, float px, float pz, float rate, float f) {
    float want = atan2f(px - b->pos[0], pz - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > rate * f) d = rate * f;
    if (d < -rate * f) d = -rate * f;
    b->yaw += d;
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
            /* A grenade: lobbed to land by him in about a second, then it beeps for one more. */
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
            s->life -= f;
            if (s->life <= 0.0f) {
                /* It bursts: anyone within 250 is hurt. */
                dx = px - s->pos[0]; dz = pz - s->pos[2];
                fx_explosion(s->pos);
                if (attacks && dx * dx + dz * dz < 250.0f * 250.0f && fabsf(py - s->pos[1]) < 250.0f) b->land = 2;
                s->on = 0;
            }
            continue;
        }
        s->pos[0] += s->vel[0] * f; s->pos[1] += s->vel[1] * f; s->pos[2] += s->vel[2] * f;
        s->life -= f;
        dx = px - s->pos[0]; dy = py + 100.0f - s->pos[1]; dz = pz - s->pos[2];
        if (dx * dx + dz * dz < (s->kind == 2 ? 120.0f * 120.0f : 60.0f * 60.0f) && fabsf(dy) < 140.0f) {
            if (s->kind == 2) fx_explosion(s->pos);
            if (attacks) b->land = s->kind == 2 ? 2 : 1;
            s->on = 0;
            continue;
        }
        if (s->life <= 0.0f) { if (s->kind == 2) fx_explosion(s->pos); s->on = 0; }
    }
}

void lady_think(LadyBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz), r;
    b->land = 0;
    b->timer += f;
    shots_update(b, f, px, py, pz, attacks);
    if (b->gauge > 0.0f) b->gauge -= 0.01f * f;
    switch (b->action) {
    case L_IDLE:
        face(b, px, pz, 0.08f, f);
        if (b->timer > 20.0f + 25.0f * unit()) {
            r = unit();
            if (d < CLOSE) act(b, r < 0.5f ? L_ESCAPE : L_BACK);
            else if (d > KEEP * 1.6f) act(b, r < 0.5f ? L_RUN : L_KALINA);
            else act(b, r < 0.35f ? L_SHOOT : r < 0.55f ? L_STRAFE_R : r < 0.75f ? L_STRAFE_L : r < 0.9f ? L_GRENADE : L_KALINA);
        }
        break;
    case L_RUN:
        face(b, px, pz, 0.1f, f);
        move(b, RUN_SPEED * 0.6f, 0.0f, f);
        if (d < KEEP || b->timer > 80.0f) act(b, L_SHOOT);
        break;
    case L_STRAFE_R: case L_STRAFE_L:
        /* Sidestepping round him, shooting as she goes. */
        face(b, px, pz, 0.1f, f);
        move(b, 0.0f, (b->action == L_STRAFE_R ? 1.0f : -1.0f) * STRAFE_SPEED, f);
        if (b->frame > 10.0f && ((int)b->timer % 14) == 0) fire(b, 0, px, py, pz, (unit() - 0.5f) * 120.0f);
        if (b->done) act(b, L_IDLE);
        break;
    case L_BACK:
        face(b, px, pz, 0.1f, f);
        move(b, -BACK_SPEED, 0.0f, f);
        if (b->done) act(b, L_SHOOT);
        break;
    case L_SHOOT:
        face(b, px, pz, 0.1f, f);
        if (b->frame > 15.0f && b->frame < 70.0f && b->step < 4 && b->frame > 15.0f + 12.0f * (float)b->step) {
            ++b->step;
            fire(b, 0, px, py, pz, (unit() - 0.5f) * 100.0f);
        }
        if (b->done) act(b, L_IDLE);
        break;
    case L_GRENADE:
        face(b, px, pz, 0.08f, f);
        if (!b->step && b->frame > 25.0f) { b->step = 1; fire(b, 1, px, py, pz, 0.0f); }
        if (b->done) act(b, L_IDLE);
        break;
    case L_KALINA:
        face(b, px, pz, b->frame < 60.0f ? 0.06f : 0.0f, f);
        if (!b->step && b->frame > 80.0f) {
            float at[3] = {b->pos[0] + sinf(b->yaw) * 80.0f, b->pos[1] + 130.0f, b->pos[2] + cosf(b->yaw) * 80.0f};
            b->step = 1;
            fx_explosion(at);   /* the launch flash */
            fire(b, 2, px, py, pz, 0.0f);
        }
        if (b->done) act(b, L_IDLE);
        break;
    case L_ESCAPE:
        face(b, px, pz, 0.12f, f);
        move(b, -ESCAPE_SPEED, 0.0f, f);
        if (!b->step && b->frame > 50.0f) { b->step = 1; fire(b, 1, px, py, pz, 0.0f); }   /* a grenade left behind */
        if (b->done) act(b, L_SHOOT);
        break;
    case L_FLINCH: case L_KNOCK:
        if (b->action == L_KNOCK) move(b, -593.0f / 37.0f * 0.5f, 0.0f, f);
        if (b->done) act(b, unit() < 0.6f ? L_ESCAPE : L_IDLE);
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
    else if (b->action == L_IDLE || b->action == L_SHOOT || b->action == L_RUN) act(b, L_FLINCH);
    return 0;
}

void lady_draw_fx(const LadyBrain *b) {
    int i;
    for (i = 0; i < LADY_SHOTS; ++i) {
        const LadyShot *s = &b->shots[i];
        float still[3] = {0.0f, 0.0f, 0.0f};
        if (!s->on) continue;
        if (s->kind == 0) fx_mote(s->pos, still, 18.0f, 12.0f, 0.04f, 0xff60e0ffu);
        else if (s->kind == 1) {
            /* The grenade, its light blinking faster as it's about to go. */
            int blink = ((int)s->life / (s->life < 30.0f ? 3 : 8)) & 1;
            fx_mote(s->pos, still, 24.0f, 22.0f, 0.04f, 0xff304050u);
            if (blink) fx_mote(s->pos, still, 40.0f, 10.0f, 0.04f, 0xff2020ffu);
        } else fx_mote(s->pos, still, 60.0f, 40.0f, 0.04f, 0xff40a0ffu);
    }
}
