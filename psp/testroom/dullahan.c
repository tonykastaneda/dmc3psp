/* CEm012 (EST 30), native em012 mesh and clips. Frontal shield and a
 * slow approach/committed charge expose the rear core. The native decision
 * tables and swept attack caps remain approximations; see MISSION10_SPEC.md. */
#include <math.h>
#include "dullahan.h"

/* FUN_003cd860 selects 0:0 to track, 0:1 to charge, 3:0 to strike, and 2:1 on death. */
const char *const dullahan_motion_names[DULLAHAN_M_COUNT] = {"du600", "du601", "du900", "du700", "du800", "du700"};

enum { A_IDLE, A_RUN, A_CHARGE, A_STRIKE, A_FLINCH, A_KNOCK, A_DEATH };
static const int action_motion[] = {DULLAHAN_M_IDLE, DULLAHAN_M_IDLE, DULLAHAN_M_CHARGE, DULLAHAN_M_STRIKE,
                                    DULLAHAN_M_FLINCH, DULLAHAN_M_KNOCK, DULLAHAN_M_DEATH};

#define RUN_SPEED 3.0f
#define CHARGE_SPEED 13.0f      /* rank 0, 0x5b8e80 */
#define CHARGE_DECEL 0.17f      /* rank 0, 0x5b8ea0 */
#define CHARGE_RANGE 320.0f     /* port's distance gate */
#define HIT_REACH 200.0f

int dullahan_loops(int motion) { return motion == DULLAHAN_M_IDLE; }

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

/* Approximate the shield contact volume. Script 3:0 arms record 1 for five frames (600 damage, light reaction). */
static int strike_contact(DullahanBrain *b, float px, float py, float pz) {
    float x = b->pos[0] + sinf(b->yaw) * 120.0f - px, z = b->pos[2] + cosf(b->yaw) * 120.0f - pz;
    return x * x + z * z < HIT_REACH * HIT_REACH && fabsf(py - b->pos[1]) < 200.0f;
}

void dullahan_think(DullahanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz);
    b->land = 0;
    b->timer += f;
    switch (b->action) {
    case A_IDLE:
        turn_to(b, px, pz, 0.06f, f);
        if (b->timer > 30.0f + 40.0f * unit()) {
            act(b, d < CHARGE_RANGE || (d < 900.0f && unit() < 0.3f) ? A_CHARGE : A_RUN);
        }
        break;
    case A_RUN:
        turn_to(b, px, pz, 0.08f, f);
        forward(b, RUN_SPEED * 0.5f, f);
        if (d < CHARGE_RANGE) act(b, A_CHARGE);
        else if (b->timer > 150.0f) act(b, A_IDLE);
        break;
    case A_CHARGE:
        /* FUN_003ceb50: 25 frames tracking, then a straight charge with linear drag. */
        if (b->timer <= 25.0f) {
            turn_to(b, px, pz, 64.0f * (6.2831853f / 65536.0f), f);
            b->vel = CHARGE_SPEED;
        } else if (d < 250.0f || b->vel <= 0.4f) {
            act(b, A_STRIKE);
        } else {
            b->vel = fmaxf(0.0f, b->vel - CHARGE_DECEL * f);
            forward(b, b->vel, f);
        }
        break;
    case A_STRIKE:
        if (!b->step && b->frame >= 15.0f && b->frame < 20.0f &&
            attacks && strike_contact(b, px, py, pz)) {
            b->step = 1;
            b->land = 1;
            b->land_damage = 600.0f;
        }
        if (b->done) act(b, A_IDLE);
        break;
    case A_FLINCH:
        if (b->done) act(b, unit() < 0.5f ? A_CHARGE : A_IDLE);
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
    else if (b->action != A_STRIKE) act(b, A_FLINCH);
    return 0;
}
