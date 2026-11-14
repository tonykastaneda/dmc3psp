/* CEm012 (EST 30), native em012 mesh and clips. Frontal shield and a
 * slow approach/committed charge expose the rear core. The native decision
 * tables and swept attack caps remain approximations; see MISSION10_SPEC.md. */
#include <math.h>
#include "dullahan.h"

const char *const dullahan_motion_names[DULLAHAN_M_COUNT] = {"du600", "du601", "du800", "du800", "du700", "du700", "du900"};

enum { A_IDLE, A_RUN, A_LUNGE, A_POUNCE, A_FLINCH, A_KNOCK, A_DEATH };
static const int action_motion[] = {DULLAHAN_M_IDLE, DULLAHAN_M_RUN, DULLAHAN_M_LUNGE, DULLAHAN_M_POUNCE,
                                    DULLAHAN_M_FLINCH, DULLAHAN_M_KNOCK, DULLAHAN_M_DEATH};

#define RUN_SPEED 3.0f
#define LUNGE_SPEED 18.0f
#define POUNCE_SPEED 18.0f
#define LUNGE_RANGE 320.0f
#define HIT_REACH 200.0f

int dullahan_loops(int motion) { return motion == DULLAHAN_M_IDLE || motion == DULLAHAN_M_RUN; }

static void act(DullahanBrain *b, int a) {
    b->action = a;
    b->want = action_motion[a];
    b->timer = 0.0f;
    b->step = 0;
    b->done = 0;
    b->frame = 0.0f;
    b->restart = 1;
}

void dullahan_spawn(DullahanBrain *b, int health) {
    b->health = health;
    b->land = b->remove = 0;
    b->vel = 0.0f;
    act(b, A_IDLE);
}

static void turn_to(DullahanBrain *b, float px, float pz, float rate, float f) {
    float want = atan2f(px - b->pos[0], pz - b->pos[2]), d = want - b->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > rate * f) d = rate * f;
    if (d < -rate * f) d = -rate * f;
    b->yaw += d;
}

static void forward(DullahanBrain *b, float speed, float f) {
    b->pos[0] += sinf(b->yaw) * speed * f;
    b->pos[2] += cosf(b->yaw) * speed * f;
}

/* Approximate swept shield contact in front of the body. */
static void charge_contact(DullahanBrain *b, float px, float py, float pz, int heavy, int attacks) {
    float hx = b->pos[0] + sinf(b->yaw) * 120.0f - px, hz = b->pos[2] + cosf(b->yaw) * 120.0f - pz;
    if (attacks && hx * hx + hz * hz < HIT_REACH * HIT_REACH && fabsf(py - b->pos[1]) < 200.0f) b->land = heavy ? 2 : 1;
}

void dullahan_think(DullahanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz);
    b->land = 0;
    b->timer += f;
    switch (b->action) {
    case A_IDLE:
        turn_to(b, px, pz, 0.06f, f);
        if (b->timer > 30.0f + 40.0f * unit()) {
            if (d < LUNGE_RANGE) act(b, A_LUNGE);
            else if (d < 900.0f && unit() < 0.3f) act(b, A_POUNCE);
            else act(b, A_RUN);
        }
        break;
    case A_RUN:
        turn_to(b, px, pz, 0.08f, f);
        forward(b, RUN_SPEED * 0.5f, f);
        if (d < LUNGE_RANGE) act(b, A_LUNGE);
        else if (b->timer > 150.0f) act(b, A_IDLE);
        break;
    case A_LUNGE:
        if (b->frame < 30.0f) turn_to(b, px, pz, 0.02f, f);
        if(b->frame>=30 && b->frame<75)forward(b, LUNGE_SPEED, f);
        if (!b->step && b->frame > 45.0f) { b->step = 1; charge_contact(b, px, py, pz, 0, attacks); }
        if (b->done) act(b, A_IDLE);
        break;
    case A_POUNCE:
        if (b->frame < 20.0f) turn_to(b, px, pz, 0.02f, f);
        if(b->frame>=30 && b->frame<75)forward(b, POUNCE_SPEED, f);
        if (!b->step && b->frame > 70.0f) { b->step = 1; charge_contact(b, px, py, pz, 1, attacks); }
        if (b->done) act(b, A_IDLE);
        break;
    case A_FLINCH:
        if (b->done) act(b, unit() < 0.5f ? A_LUNGE : A_IDLE);
        break;
    case A_KNOCK:
        forward(b, -b->vel, f);
        b->vel *= 0.92f;
        if (b->done) act(b, A_IDLE);
        break;
    case A_DEATH:
        if (b->timer > 150.0f) b->remove = 1;
        break;
    }
}

int dullahan_hit(DullahanBrain *b, int heavy, float fx, float fz, int mortal) {
    {
        float dx=fx-b->pos[0],dz=fz-b->pos[2];
        if(dx*sinf(b->yaw)+dz*cosf(b->yaw)>0)return 0;
    }

    if (b->action == A_DEATH) return 0;
    if (mortal) {
        b->health -= heavy ? 2 : 1;
        if (b->health <= 0) { act(b, A_DEATH); return 1; }
    }
    b->yaw = atan2f(fx - b->pos[0], fz - b->pos[2]);
    if (heavy) { act(b, A_KNOCK); b->vel = 210.0f / 65.0f * 2.0f; }
    else if (b->action != A_POUNCE) act(b, A_FLINCH);
    return 0;
}
