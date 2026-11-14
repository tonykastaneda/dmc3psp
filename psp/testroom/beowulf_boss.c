/* Beowulf (class 33, EST 51; em030.pac at GDATA 135), Mission 11's boss. His own model and motions; his fight is a
 * stand-in from the walkthrough's list of his moves, as his class's code isn't decoded: a three-punch combo when
 * Dante's close, a charge on all fours, a leaping rush that lands hard, a stomp that throws a stone pillar rolling at
 * Dante, and a volley of glowing feathers after his body flashes. He has super armour: hits fill a gauge that
 * staggers him. Speeds are his motions' root travel (tools/prepare_skinned_model.py BEOWULF_MOTIONS); timings and
 * damage are estimates. The pillars aren't models, and can't be broken for orbs. */
#include <math.h>
#include "beowulf_boss.h"
#include "fx.h"

const char *const beowulf_motion_names[BEOWULF_M_COUNT] = {
    "bw00", "bw01", "bw07", "bw30", "bw31", "bw32", "bw39", "bw36", "bw37", "bw38", "bw35", "bw16"};

enum { B_IDLE, B_WALK, B_CHARGE, B_PUNCH1, B_PUNCH2, B_PUNCH3, B_STOMP, B_RUSH, B_LASER, B_FLINCH, B_KNOCK, B_DEATH };

#define WALK_SPEED (1257.0f / 149.0f)
#define CHARGE_SPEED (1673.0f / 149.0f)
#define RUSH_SPEED (1354.0f / 240.0f)
#define PUNCH_RANGE 420.0f
#define CHAMBER_RADIUS 1500.0f
#define STAGGER_HITS 14.0f
#define PILLAR_SPEED 11.0f
#define FEATHER_SPEED 14.0f

int beowulf_loops(int motion) { return motion == BEOWULF_M_IDLE || motion == BEOWULF_M_WALK || motion == BEOWULF_M_CHARGE; }

static void act(BeowulfBrain *b, int a) {
    b->action = a;
    b->want = a;   /* the actions and motions are in the same order */
    b->restart = 1;
    b->timer = 0.0f;
    b->step = 0;
    b->done = 0;
    b->frame = 0.0f;
}

void beowulf_spawn(BeowulfBrain *b, int health) {
    int i;
    b->health = health;
    b->land = 0;
    b->gauge = 0.0f;
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < BEOWULF_SHOTS; ++i) b->shots[i].on = 0;
    act(b, B_IDLE);
}

static void turn_to(BeowulfBrain *b, float px, float pz, float rate, float f) {
    float want = atan2f(px - b->pos[0], pz - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > rate * f) d = rate * f;
    if (d < -rate * f) d = -rate * f;
    b->yaw += d;
}

static void forward(BeowulfBrain *b, float speed, float f) {
    b->pos[0] += sinf(b->yaw) * speed * f;
    b->pos[2] += cosf(b->yaw) * speed * f;
}

/* A blow landing reach in front of him, radius r. */
static void strike(BeowulfBrain *b, float px, float py, float pz, float reach, float r, int heavy, int attacks) {
    float hx = b->pos[0] + sinf(b->yaw) * reach - px, hz = b->pos[2] + cosf(b->yaw) * reach - pz;
    if (attacks && hx * hx + hz * hz < r * r && fabsf(py - b->pos[1]) < 300.0f) b->land = heavy ? 2 : 1;
}

static void shoot(BeowulfBrain *b, int kind, float px, float py, float pz) {
    int i;
    for (i = 0; i < BEOWULF_SHOTS; ++i) {
        BeowulfShot *s = &b->shots[i];
        float dx, dy, dz, d, speed = kind ? FEATHER_SPEED : PILLAR_SPEED;
        if (s->on) continue;
        s->on = 1;
        s->kind = kind;
        s->life = kind ? 150.0f : 200.0f;
        s->pos[0] = b->pos[0] + sinf(b->yaw) * 250.0f;
        s->pos[1] = b->pos[1] + (kind ? 350.0f : 80.0f);
        s->pos[2] = b->pos[2] + cosf(b->yaw) * 250.0f;
        dx = px - s->pos[0]; dy = kind ? py + 100.0f - s->pos[1] : 0.0f; dz = pz - s->pos[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 1.0f) d = 1.0f;
        s->vel[0] = dx / d * speed; s->vel[1] = dy / d * speed; s->vel[2] = dz / d * speed;
        return;
    }
}

static void shots_update(BeowulfBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < BEOWULF_SHOTS; ++i) {
        BeowulfShot *s = &b->shots[i];
        float dx, dy, dz, r;
        if (!s->on) continue;
        s->pos[0] += s->vel[0] * f; s->pos[1] += s->vel[1] * f; s->pos[2] += s->vel[2] * f;
        s->life -= f;
        dx = px - s->pos[0]; dy = py + 90.0f - s->pos[1]; dz = pz - s->pos[2];
        r = s->kind ? 70.0f : 140.0f;
        if (dx * dx + dz * dz < r * r && fabsf(dy) < (s->kind ? 120.0f : 200.0f)) {
            s->on = 0;
            if (attacks) b->land = s->kind ? 1 : 2;
            continue;
        }
        if (s->life <= 0.0f) s->on = 0;
    }
}

void beowulf_think(BeowulfBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz), r;
    b->land = 0;
    b->timer += f;
    shots_update(b, f, px, py, pz, attacks);
    if (b->gauge > 0.0f) b->gauge -= 0.01f * f;
    switch (b->action) {
    case B_IDLE:
        turn_to(b, px, pz, 0.04f, f);
        if (b->timer > 40.0f + 30.0f * unit()) {
            r = unit();
            if (d < PUNCH_RANGE) act(b, r < 0.75f ? B_PUNCH1 : B_STOMP);
            else if (d > 900.0f) act(b, r < 0.4f ? B_CHARGE : r < 0.7f ? B_RUSH : B_LASER);
            else act(b, r < 0.35f ? B_STOMP : r < 0.6f ? B_LASER : B_WALK);
        }
        break;
    case B_WALK:
        turn_to(b, px, pz, 0.05f, f);
        forward(b, WALK_SPEED, f);
        if (d < PUNCH_RANGE) act(b, B_PUNCH1);
        else if (b->timer > 150.0f) act(b, B_IDLE);
        break;
    case B_CHARGE:
        /* On all fours, straight through where Dante was. */
        if (b->timer < 20.0f) turn_to(b, px, pz, 0.08f, f);
        forward(b, CHARGE_SPEED, f);
        if (!b->step && d < 260.0f) { b->step = 1; strike(b, px, py, pz, 0.0f, 300.0f, 1, attacks); }
        if (b->timer > 110.0f) act(b, B_IDLE);
        break;
    case B_PUNCH1: case B_PUNCH2: case B_PUNCH3: {
        float len = b->action == B_PUNCH1 ? 96.0f : b->action == B_PUNCH2 ? 164.0f : 124.0f;
        float travel = b->action == B_PUNCH1 ? 290.0f : b->action == B_PUNCH2 ? 341.0f : 206.0f;
        if (b->frame < len * 0.3f) turn_to(b, px, pz, 0.08f, f);
        forward(b, travel / len, f);
        if (!b->step && b->frame > len * 0.45f) {
            b->step = 1;
            strike(b, px, py, pz, 260.0f, 240.0f, b->action == B_PUNCH3, attacks);
        }
        if (b->done) {
            if (b->action != B_PUNCH3 && d < PUNCH_RANGE * 1.3f && unit() < 0.7f) act(b, b->action + 1);
            else act(b, B_IDLE);
        }
        break;
    }
    case B_STOMP:
        turn_to(b, px, pz, 0.05f, f);
        if (!b->step && b->frame > 90.0f) {
            float at[3] = {b->pos[0] + sinf(b->yaw) * 250.0f, b->pos[1], b->pos[2] + cosf(b->yaw) * 250.0f};
            b->step = 1;
            fx_dust(at);
            strike(b, px, py, pz, 200.0f, 260.0f, 1, attacks);
            shoot(b, 0, px, py, pz);   /* the pillar, punched at him */
        }
        if (b->done) act(b, B_IDLE);
        break;
    case B_RUSH:
        if (b->frame < 30.0f) turn_to(b, px, pz, 0.08f, f);
        forward(b, RUSH_SPEED, f);
        if (!b->step && b->frame > 180.0f) {
            float at[3] = {b->pos[0], b->pos[1], b->pos[2]};
            b->step = 1;
            fx_dust(at);
            strike(b, px, py, pz, 100.0f, 380.0f, 1, attacks);
        }
        if (b->done) act(b, B_IDLE);
        break;
    case B_LASER:
        turn_to(b, px, pz, 0.04f, f);
        if (b->frame < 110.0f && ((int)b->timer % 6) == 0) {
            /* His body flashes before the feathers fly. */
            float at[3] = {b->pos[0], b->pos[1] + 300.0f, b->pos[2]}, up[3] = {0.0f, 30.0f, 0.0f};
            fx_mote(at, up, 260.0f, 80.0f, 0.3f, 0x80a0ffffu);
        }
        if (b->frame >= 110.0f && b->frame < 230.0f && b->step < 6 && b->frame > 110.0f + 20.0f * (float)b->step) {
            ++b->step;
            shoot(b, 1, px, py, pz);
        }
        if (b->done) act(b, B_IDLE);
        break;
    case B_FLINCH: case B_KNOCK:
        if (b->action == B_KNOCK) forward(b, -209.0f / 151.0f, f);
        if (b->done) act(b, B_IDLE);
        break;
    case B_DEATH:
        break;
    }
    {
        float hx = b->pos[0] - b->home[0], hz = b->pos[2] - b->home[2], l = sqrtf(hx * hx + hz * hz);
        if (l > CHAMBER_RADIUS) { b->pos[0] = b->home[0] + hx / l * CHAMBER_RADIUS; b->pos[2] = b->home[2] + hz / l * CHAMBER_RADIUS; }
    }
}

int beowulf_hit(BeowulfBrain *b, int heavy, int mortal) {
    if (b->action == B_DEATH) return 0;
    if (mortal) {
        b->health -= heavy ? 2 : 1;
        if (b->health <= 0) {
            int i;
            for (i = 0; i < BEOWULF_SHOTS; ++i) b->shots[i].on = 0;
            act(b, B_DEATH);
            return 1;
        }
    }
    b->gauge += heavy ? 2.0f : 1.0f;
    if (b->gauge >= STAGGER_HITS) {
        b->gauge = 0.0f;
        act(b, heavy ? B_KNOCK : B_FLINCH);
    }
    return 0;
}

void beowulf_draw_fx(const BeowulfBrain *b) {
    int i;
    for (i = 0; i < BEOWULF_SHOTS; ++i) {
        const BeowulfShot *s = &b->shots[i];
        float still[3] = {0.0f, 0.0f, 0.0f};
        if (!s->on) continue;
        if (s->kind) fx_mote(s->pos, still, 60.0f, 30.0f, 0.04f, 0xffc0ffffu);   /* a feather of light */
        else {
            /* The pillar: a grey mass rolling along the floor, kicking up dust. */
            float top[3] = {s->pos[0], s->pos[1] + 120.0f, s->pos[2]};
            fx_mote(s->pos, still, 200.0f, 190.0f, 0.04f, 0xff707880u);
            fx_mote(top, still, 170.0f, 160.0f, 0.04f, 0xff808890u);
        }
    }
}
