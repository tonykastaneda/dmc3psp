/* Nevan (class 31, EST 49; em028.pac at GDATA 133), Mission 9's boss. Her own model and motions; her fight is a
 * stand-in from the walkthrough, as her class's code isn't decoded: she hovers and turns to Dante, glides at him,
 * sweeps her blade when he's close, sends thunder bats at him in a batch, and vanishes to reappear on his other
 * side. Hits fill a gauge that knocks her back. Not in yet: her lightning, the bats' own models, and the kiss she
 * tries near the end. Speeds, timings and the bats' damage are estimates; which motion is which was read from
 * their root paths (tools/prepare_skinned_model.py NEVAN_MOTIONS). */
#include <math.h>
#include "nevan_boss.h"
#include "fx.h"

const char *const nevan_motion_names[NEVAN_M_COUNT] = {"nv200", "nv201", "nv214", "nv240", "nv213", "nv245", "nv249"};

enum { N_HOVER, N_GLIDE, N_BLADE, N_BATS, N_FLINCH, N_KNOCK, N_DEATH, N_VANISH, N_APPEAR };
static const int action_motion[] = {NEVAN_M_HOVER, NEVAN_M_GLIDE, NEVAN_M_BLADE, NEVAN_M_BATS, NEVAN_M_FLINCH,
                                    NEVAN_M_KNOCK, NEVAN_M_DEATH, NEVAN_M_HOVER, NEVAN_M_HOVER};

#define GLIDE_SPEED 4.0f        /* cm a frame */
#define BLADE_SPEED 7.0f
#define BLADE_RANGE 360.0f
#define BAT_SPEED 9.0f
#define STAGE_RADIUS 1400.0f    /* how far from where she came in she may go */
#define KNOCK_HITS 10.0f

int nevan_loops(int motion) { return motion == NEVAN_M_HOVER || motion == NEVAN_M_GLIDE; }

static void act(NevanBrain *b, int a) {
    b->action = a;
    b->want = action_motion[a];
    b->restart = 1;
    b->timer = 0.0f;
    b->step = 0;
    b->done = 0;
    b->frame = 0.0f;
}

void nevan_spawn(NevanBrain *b, int health) {
    int i;
    b->health = health;
    b->land = b->remove = b->hidden = 0;
    b->vel = b->gauge = 0.0f;
    b->alpha = 1.0f;
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < NEVAN_BATS; ++i) b->bats[i].on = 0;
    act(b, N_HOVER);
}

static void turn_to(NevanBrain *b, float px, float pz, float rate, float f) {
    float want = atan2f(px - b->pos[0], pz - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > rate * f) d = rate * f;
    if (d < -rate * f) d = -rate * f;
    b->yaw += d;
}

static void forward(NevanBrain *b, float speed, float f) {
    b->pos[0] += sinf(b->yaw) * speed * f;
    b->pos[2] += cosf(b->yaw) * speed * f;
}

static void puff(const NevanBrain *b) {
    int k;
    for (k = 0; k < 14; ++k) {
        float a = (float)k * 0.45f;
        float at[3] = {b->pos[0] + sinf(a) * 60.0f, b->pos[1] + 40.0f + (float)(k % 5) * 35.0f, b->pos[2] + cosf(a) * 60.0f};
        float v[3] = {sinf(a) * 80.0f, 40.0f, cosf(a) * 80.0f};
        fx_mote(at, v, 80.0f, 20.0f, 0.6f, (k & 1) ? 0xffff3090u : 0xff60ffffu);
    }
}

static void release_bats(NevanBrain *b, float px, float py, float pz, float (*unit)(void)) {
    int i, n = 0;
    for (i = 0; i < NEVAN_BATS && n < 6; ++i) {
        NevanBat *t = &b->bats[i];
        float spread = ((float)n - 2.5f) * 0.25f, a = b->yaw + spread;
        if (t->on) continue;
        t->on = 1;
        t->life = 240.0f;
        t->pos[0] = b->pos[0] + sinf(a) * 80.0f;
        t->pos[1] = b->pos[1] + 120.0f + unit() * 80.0f;
        t->pos[2] = b->pos[2] + cosf(a) * 80.0f;
        t->vel[0] = sinf(a) * BAT_SPEED; t->vel[1] = 0.0f; t->vel[2] = cosf(a) * BAT_SPEED;
        (void)px; (void)py; (void)pz;
        ++n;
    }
}

/* The bats fly on, turning a little toward Dante; one that reaches him hurts and is gone. */
static void bats_update(NevanBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < NEVAN_BATS; ++i) {
        NevanBat *t = &b->bats[i];
        float dx, dy, dz, d;
        if (!t->on) continue;
        dx = px - t->pos[0]; dy = py + 100.0f - t->pos[1]; dz = pz - t->pos[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > 1.0f) {
            t->vel[0] += dx / d * 0.25f * f; t->vel[1] += dy / d * 0.25f * f; t->vel[2] += dz / d * 0.25f * f;
        }
        {
            float s = sqrtf(t->vel[0] * t->vel[0] + t->vel[1] * t->vel[1] + t->vel[2] * t->vel[2]);
            if (s > BAT_SPEED) { t->vel[0] *= BAT_SPEED / s; t->vel[1] *= BAT_SPEED / s; t->vel[2] *= BAT_SPEED / s; }
        }
        t->pos[0] += t->vel[0] * f; t->pos[1] += t->vel[1] * f; t->pos[2] += t->vel[2] * f;
        t->life -= f;
        if (d < 80.0f) {
            t->on = 0;
            if (attacks) b->land = 1;
            continue;
        }
        if (t->life <= 0.0f) t->on = 0;
    }
}

void nevan_think(NevanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz), r;
    b->land = 0;
    b->timer += f;
    bats_update(b, f, px, py, pz, attacks);
    if (b->gauge > 0.0f) b->gauge -= 0.01f * f;
    switch (b->action) {
    case N_HOVER:
        turn_to(b, px, pz, 0.05f, f);
        if (b->timer > 40.0f + 40.0f * unit()) {
            r = unit();
            if (d < BLADE_RANGE) act(b, N_BLADE);
            else if (r < 0.35f) act(b, N_BATS);
            else if (r < 0.7f) act(b, N_GLIDE);
            else act(b, N_VANISH);
        }
        break;
    case N_GLIDE:
        turn_to(b, px, pz, 0.06f, f);
        forward(b, GLIDE_SPEED, f);
        if (d < BLADE_RANGE) act(b, N_BLADE);
        else if (b->timer > 120.0f) act(b, N_HOVER);
        break;
    case N_BLADE:
        if (b->frame < 25.0f) turn_to(b, px, pz, 0.1f, f);
        if (b->frame > 25.0f && b->frame < 60.0f) forward(b, BLADE_SPEED, f);
        if (!b->step && b->frame > 50.0f) {
            float hx = b->pos[0] + sinf(b->yaw) * 140.0f - px, hz = b->pos[2] + cosf(b->yaw) * 140.0f - pz;
            b->step = 1;
            if (attacks && hx * hx + hz * hz < 220.0f * 220.0f && fabsf(py - b->pos[1]) < 250.0f) b->land = 2;
        }
        if (b->done) act(b, N_HOVER);
        break;
    case N_BATS:
        turn_to(b, px, pz, 0.05f, f);
        if (!b->step && b->frame > 60.0f) { b->step = 1; release_bats(b, px, py, pz, unit); }
        if (b->done) act(b, unit() < 0.5f ? N_VANISH : N_HOVER);
        break;
    case N_VANISH:
        if (!b->step) { b->step = 1; puff(b); b->hidden = 1; }
        if (b->timer > 30.0f) {
            /* Back on Dante's far side, 600 away, kept to her stage. */
            float a = atan2f(dx, dz) + (unit() - 0.5f) * 1.2f, hx, hz, l;
            b->pos[0] = px + sinf(a) * 600.0f; b->pos[2] = pz + cosf(a) * 600.0f;
            hx = b->pos[0] - b->home[0]; hz = b->pos[2] - b->home[2]; l = sqrtf(hx * hx + hz * hz);
            if (l > STAGE_RADIUS) { b->pos[0] = b->home[0] + hx / l * STAGE_RADIUS; b->pos[2] = b->home[2] + hz / l * STAGE_RADIUS; }
            b->yaw = atan2f(px - b->pos[0], pz - b->pos[2]);
            b->hidden = 0;
            act(b, N_APPEAR);
            puff(b);
        }
        break;
    case N_APPEAR:
        if (b->timer > 20.0f) act(b, d < BLADE_RANGE * 1.5f ? N_BLADE : N_HOVER);
        break;
    case N_FLINCH:
        if (b->done) act(b, unit() < 0.4f ? N_VANISH : N_HOVER);
        break;
    case N_KNOCK:
        forward(b, -b->vel, f);
        b->vel *= 0.94f;
        if (b->done) act(b, N_VANISH);
        break;
    case N_DEATH:
        if (b->timer > 200.0f) { if (!b->step) { b->step = 1; puff(b); } b->remove = b->timer > 230.0f; }
        break;
    }
    {
        float hx = b->pos[0] - b->home[0], hz = b->pos[2] - b->home[2], l = sqrtf(hx * hx + hz * hz);
        if (l > STAGE_RADIUS) { b->pos[0] = b->home[0] + hx / l * STAGE_RADIUS; b->pos[2] = b->home[2] + hz / l * STAGE_RADIUS; }
    }
}

int nevan_hit(NevanBrain *b, int heavy, float fx, float fz, int mortal) {
    if (b->action == N_DEATH || b->hidden) return 0;
    if (mortal) {
        b->health -= heavy ? 2 : 1;
        if (b->health <= 0) {
            int i;
            for (i = 0; i < NEVAN_BATS; ++i) b->bats[i].on = 0;
            act(b, N_DEATH);
            return 1;
        }
    }
    b->gauge += heavy ? 2.0f : 1.0f;
    if (b->gauge >= KNOCK_HITS) {
        b->gauge = 0.0f;
        b->yaw = atan2f(fx - b->pos[0], fz - b->pos[2]);
        b->vel = 260.0f / 112.0f * 2.0f;
        act(b, N_KNOCK);
    } else if (b->action == N_HOVER || b->action == N_GLIDE) {
        act(b, N_FLINCH);
    }
    return 0;
}

void nevan_draw_fx(const NevanBrain *b) {
    int i;
    for (i = 0; i < NEVAN_BATS; ++i) {
        const NevanBat *t = &b->bats[i];
        float still[3] = {0.0f, 0.0f, 0.0f};
        if (!t->on) continue;
        /* A bat: a dark core with a crackle of electricity round it. */
        fx_mote(t->pos, still, 40.0f, 30.0f, 0.04f, 0xff401030u);
        fx_mote(t->pos, still, 70.0f, 20.0f, 0.04f, 0x80ffa0e0u);
    }
}
