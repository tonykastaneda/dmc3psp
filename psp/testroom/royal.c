/* CEm017's shared actions for the Queen and King (docs/CHESS_ROYALS_SPEC.md). */
#include "royal.h"
#include <math.h>

#define ROYAL_PI 3.14159265f

void royal_start(RoyalBody *b, int action) {
    if (b->hp > 0.0f && b->action != action) { b->action = action; b->sub = 0; }
}

void royal_motion(RoyalBody *b, int motion) {
    if (motion < 0) motion = b->action;
    if (b->motion != motion) { b->motion = motion; b->restart = 1; }
}

int royal_wait(RoyalBody *b, float f, float d, float base, float spread, float (*unit)(void)) {
    if (b->sub == 0) {
        royal_motion(b, -1);
        b->timer = base + spread * unit();
        b->sub = 1;
    }
    if (d < 1000.0f) d = 1000.0f;
    b->timer -= 1000.0f / d * 3.0f * f;
    return b->timer <= 0.0f;
}

int royal_fall(RoyalBody *b, float f, float shake) {
    b->vel[1] -= 3.2f * f;
    b->pos[0] += b->vel[0] * f; b->pos[1] += b->vel[1] * f; b->pos[2] += b->vel[2] * f;
    if (b->pos[1] > b->floor_y + 10.0f) return 0;
    b->pos[1] = b->floor_y;
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
    b->events |= ROYAL_EV_LAND;
    b->shake = shake;
    royal_start(b, ROYAL_WAIT);
    return 1;
}

void royal_castle(RoyalBody *b, float f) {
    b->busy = 1;
    switch (b->sub) {
    case 0:
        b->events |= ROYAL_EV_CASTLE;
        b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
        royal_motion(b, -1);
        b->timer = 30.0f;
        b->sub = 1;
        /* fall through */
    case 1: {
        float dx = b->dest[0] - b->pos[0], dy = b->dest[1] - b->pos[1], dz = b->dest[2] - b->pos[2];
        /* 9 % of the way a frame (per native update, not scaled by the step) */
        b->pos[0] += dx * 0.09f; b->pos[1] += dy * 0.09f; b->pos[2] += dz * 0.09f;
        if (dx * dx + dz * dz > 1.0f) b->yaw = atan2f(dx, dz);
        b->timer -= f;
        if (b->timer <= 0.0f) b->sub = 2;
        break;
    }
    case 2:
        b->pos[0] = b->dest[0]; b->pos[1] = b->dest[1]; b->pos[2] = b->dest[2];
        b->events |= ROYAL_EV_CASTLE_DOWN;
        b->timer = 180.0f;
        b->sub = 3;
        /* fall through */
    case 3:
        b->vel[1] -= 3.2f * f;
        b->pos[0] += b->vel[0] * f; b->pos[1] += b->vel[1] * f; b->pos[2] += b->vel[2] * f;
        if (b->pos[1] <= b->floor_y + 10.0f) {
            b->pos[1] = b->floor_y; b->vel[1] = 0.0f;
            b->events |= ROYAL_EV_LAND; b->shake = 5.0f;
            b->sub = 4;
        }
        break;
    case 4:
        b->timer -= f;
        if (b->timer <= 0.0f) royal_start(b, ROYAL_WAIT);   /* the Rook alone goes on to its attack */
        break;
    }
}

/* +0x60 / +0x68 are the knock's velocity in the frame of +0x486 (x side, z forward): back is -z. */
static void knock_move(RoyalBody *b, float f, float back) {
    float s = sinf(b->knock_yaw), c = cosf(b->knock_yaw);
    b->pos[0] += s * back * f; b->pos[2] += c * back * f;
    b->pos[1] += b->vel[1] * f;
}

void royal_knock(RoyalBody *b, float f) {
    int heavy = b->action == ROYAL_KNOCK_HEAVY;
    /* vel[2] carries the back speed here (negative, toward 0) */
    if (b->sub == 0) {
        royal_motion(b, -1);
        if (heavy && b->heavy_variant) { b->vel[1] = 6.0f; b->vel[2] = -20.0f; }
        else { b->vel[1] = 20.0f; b->vel[2] = heavy ? -30.0f : -5.0f; }
        b->heavy_variant = 0;
        b->sub = 1;
    }
    if (b->sub == 2) {                          /* the light knock's 13-frame hang at the top */
        b->timer2 -= f;
        if (b->timer2 <= 0.0f) b->sub = 3;
        return;
    }
    b->vel[2] += (heavy ? 4.0f : 1.0f) * f;     /* FUN_0042e1e0 toward 0 */
    if (b->vel[2] > 0.0f) b->vel[2] = 0.0f;
    b->vel[1] -= 3.2f * f;
    knock_move(b, f, b->vel[2]);
    if (b->pos[1] <= b->floor_y + 10.0f) {
        b->pos[1] = b->floor_y;
        b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
        b->events |= ROYAL_EV_LAND; b->shake = 10.0f;
        royal_start(b, ROYAL_WAIT);
    } else if (!heavy && b->sub == 1 && b->vel[1] < 0.0f) {
        b->timer2 = 13.0f;
        b->sub = 2;
    }
}

void royal_hit(RoyalBody *b, int launch, int heavy, float dante_x, float dante_z) {
    if (b->hp <= 0.0f) return;
    b->events |= ROYAL_EV_HIT_SHAKE; b->shake = 5.0f;
    if (b->action != ROYAL_WAIT) return;
    b->knock_yaw = atan2f(b->pos[0] - dante_x, b->pos[2] - dante_z) + ROYAL_PI;   /* +0x486 faces Dante */
    if (launch) royal_start(b, ROYAL_KNOCK);
    else if (heavy) royal_start(b, ROYAL_KNOCK_HEAVY);
}

int royal_heading8(const RoyalBody *b, float x, float z) {
    float a = atan2f(x - b->pos[0], z - b->pos[2]);   /* 0 = +z, pi/2 = +x */
    int k = (int)floorf(a / (ROYAL_PI / 4.0f) + 0.5f);
    return k & 7;
}

void royal_heading_velocity(RoyalBody *b, int dir) {
    static const float v[8][2] = {{0, 10}, {7.07f, 7.07f}, {10, 0}, {7.07f, -7.07f}, {0, -10}, {-7.07f, -7.07f},
                                  {-10, 0}, {-7.07f, 7.07f}};
    float k = 1.0f + (float)b->rank * 0.25f;
    b->vel[0] = v[dir & 7][0] * k; b->vel[1] = 0.0f; b->vel[2] = v[dir & 7][1] * k;
}
