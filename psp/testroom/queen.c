/* The Damned Queen's logic, FUN_00437510 and its actions 0x437640 (the move) and 0x437ab0 (the charge).
 * docs/CHESS_ROYALS_SPEC.md "The Queen". */
#include "queen.h"
#include <math.h>
#include <string.h>

#define QUEEN_PI 3.14159265f

/* Per rank (0-3). FUN_00409690's rows at 0x5c00d0: base + spread x a random 0-1. */
static const float wait_base[4] = {120.0f, 80.0f, 70.0f, 60.0f}, wait_spread = 60.0f;
static const float glide_base = 100.0f, glide_spread = 50.0f;      /* action 2 */
static const float charge_base = 50.0f, charge_spread = 50.0f;     /* action 3 */
static const float turn_deg[4] = {2.0f, 3.0f, 3.0f, 4.0f};          /* +0x20 of the action 3 row */
/* The rise before the move and the charge: 0x5c2ce0 / 0x5c2d00, (frames, cm a frame). */
static const float rise_frames[4] = {20.0f, 15.0f, 15.0f, 10.0f}, rise_speed[4] = {2.0f, 2.5f, 2.5f, 4.0f};

void queen_spawn(QueenBrain *q, float hp, int rank) {
    RoyalBody keep = q->body;
    memset(q, 0, sizeof *q);
    q->body.pos[0] = keep.pos[0]; q->body.pos[1] = keep.pos[1]; q->body.pos[2] = keep.pos[2];
    q->body.yaw = keep.yaw; q->body.floor_y = keep.floor_y;
    q->body.hp = hp; q->body.rank = rank & 3;
    q->body.motion = -1;
    royal_motion(&q->body, 0);
}

/* The four points on a line with Dante (FUN_0040a480 / 0040a4c0): two rook points, two bishop points. */
static void line_points(const RoyalBody *b, float dx, float dz, float pts[4][2]) {
    float x = b->pos[0], z = b->pos[2], d_self = z - x, d_dante = dz - dx;
    pts[0][0] = dx; pts[0][1] = z;
    pts[1][0] = x; pts[1][1] = dz;
    pts[2][0] = ((dx + dz) - d_self) * 0.5f; pts[2][1] = pts[2][0] + d_self;
    pts[3][0] = ((x + z) - d_dante) * 0.5f; pts[3][1] = pts[3][0] + d_dante;
}

static float wrap(float a) {
    while (a > QUEEN_PI) a -= 2.0f * QUEEN_PI;
    while (a < -QUEEN_PI) a += 2.0f * QUEEN_PI;
    return a;
}

static void queen_move(QueenBrain *q, float f, float dx, float dz, int *command, float (*unit)(void)) {
    RoyalBody *b = &q->body;
    switch (b->sub) {
    case 0:
        royal_motion(b, -1);
        b->timer = rise_frames[b->rank];
        b->sub = 1;
        /* fall through */
    case 1:
        b->pos[1] += rise_speed[b->rank] * f;
        b->timer -= f;
        if (b->timer <= 0.0f) {
            float pts[4][2], r = unit() * 100.0f;
            int pick = r < 25.0f ? 0 : r < 50.0f ? 1 : r < 75.0f ? 2 : 3;
            line_points(b, dx, dz, pts);
            royal_heading_velocity(b, royal_heading8(b, pts[pick][0], pts[pick][1]));
            b->timer = glide_base + glide_spread * unit();
            b->sub = 2;
        }
        break;
    case 2:
        b->pos[0] += b->vel[0] * f; b->pos[2] += b->vel[2] * f;
        b->timer -= f;
        if (*command == ROYAL_CMD_RESPOND || b->timer <= 0.0f) {
            b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
            b->sub = 3;
        }
        break;
    case 3:
        royal_fall(b, f, 5.0f);
        break;
    }
}

static void queen_charge(QueenBrain *q, float f, float dx, float dz, float (*unit)(void)) {
    RoyalBody *b = &q->body;
    q->hitting = 1;
    switch (b->sub) {
    case 0:
        royal_motion(b, 0);
        b->timer = rise_frames[b->rank];
        q->charges = 3;
        b->sub = 1;
        /* fall through */
    case 1:
        b->pos[1] += rise_speed[b->rank] * f;
        b->timer -= f;
        if (b->timer <= 0.0f) b->sub = 2;
        break;
    case 2: {
        float pts[4][2], best = 1e12f;
        int k, near = 0;
        royal_motion(b, 0);
        line_points(b, dx, dz, pts);
        for (k = 0; k < 4; ++k) {
            /* FUN_0042e500: the point's distance to Dante */
            float ex = pts[k][0] - dx, ez = pts[k][1] - dz, d = ex * ex + ez * ez;
            if (d < best) { best = d; near = k; }
        }
        q->charge_yaw = (float)royal_heading8(b, pts[near][0], pts[near][1]) * (QUEEN_PI / 4.0f);
        q->speed = 40.0f;
        q->accel = (float)b->rank * 2.0f + 5.0f;
        b->sub = 3;
    }
        /* fall through */
    case 3: {
        float diff = wrap(q->charge_yaw - b->yaw), step = turn_deg[b->rank] * f * (QUEEN_PI / 180.0f);
        if (fabsf(diff) <= step) {
            b->yaw = q->charge_yaw;
            b->events |= QUEEN_EV_WINDUP;
            royal_motion(b, 3);
            b->sub = 4;
        } else b->yaw = wrap(b->yaw + (diff > 0.0f ? step : -step));
        break;
    }
    case 4:
        b->events |= QUEEN_EV_WINDUP_SOUND;
        b->timer = 60.0f - (float)b->rank * 3.0f;
        b->sub = 5;
        /* fall through */
    case 5:
        b->timer -= f;
        if (b->timer <= 0.0f) {
            b->events |= QUEEN_EV_CHARGE;
            royal_motion(b, 7);
            b->timer = charge_base + charge_spread * unit();
            b->wall = 0;
            b->sub = 6;
        }
        break;
    case 6:
        q->accel -= f * 0.3f;
        if (q->accel < -1.0f) q->accel = -1.0f;
        q->speed += q->accel * f;
        if (q->speed > 50.0f) q->speed = 50.0f;
        if (q->speed < 35.0f) q->speed = 35.0f;
        b->vel[0] = sinf(q->charge_yaw) * q->speed; b->vel[1] = 0.0f; b->vel[2] = cosf(q->charge_yaw) * q->speed;
        b->pos[0] += b->vel[0] * f; b->pos[2] += b->vel[2] * f;
        q->lane = b->timer * 0.5f;
        if (b->wall) b->timer = 0.01f;
        b->timer -= f;
        if (b->timer <= 0.0f) {
            float r;
            b->events |= QUEEN_EV_CHARGE_END;
            royal_motion(b, 8);
            r = unit() * 100.0f;
            /* 004381a0: again only on a roll under 75 with +0x490 already 0; it is 3 from the start */
            if (r < 75.0f && q->charges-- == 0) b->sub = 7;
            else { b->vel[0] = b->vel[1] = b->vel[2] = 0.0f; b->sub = 9; }
        }
        break;
    case 7:
        b->timer = 60.0f;
        b->sub = 8;
        /* fall through */
    case 8:
        b->timer -= f;
        if (b->timer <= 0.0f) b->sub = 2;
        break;
    case 9:
        royal_fall(b, f, 5.0f);
        break;
    }
}

int queen_think(QueenBrain *q, float f, float dx, float dy, float dz, int command, float (*unit)(void)) {
    RoyalBody *b = &q->body;
    float ex = dx - b->pos[0], ez = dz - b->pos[2], d = sqrtf(ex * ex + ez * ez);
    (void)dy;
    b->events = 0; b->busy = 0; q->hitting = 0;
    if (b->hp <= 0.0f) return command;
    /* 0x406830: the castle, from any action */
    if (command == ROYAL_CMD_CASTLE) { royal_start(b, ROYAL_CASTLE); command = ROYAL_CMD_NONE; }
    /* 0x437510: the response */
    if (command == ROYAL_CMD_RESPOND) {
        if (b->action == ROYAL_WAIT) royal_start(b, ROYAL_ATTACK);
        else if (b->action == ROYAL_ATTACK) command = ROYAL_CMD_NONE;
    }
    switch (b->action) {
    case ROYAL_WAIT:
        /* 00408320 -> 004095f0 -> 0x437330: her only decision row always picks the move */
        if (royal_wait(b, f, d, wait_base[b->rank], wait_spread, unit)) royal_start(b, ROYAL_MOVE);
        break;
    case ROYAL_MOVE: queen_move(q, f, dx, dz, &command, unit); break;
    case ROYAL_ATTACK:
        command = command == ROYAL_CMD_RESPOND ? ROYAL_CMD_NONE : command;   /* 0x437ab0 clears +0x488 */
        queen_charge(q, f, dx, dz, unit);
        break;
    case ROYAL_CASTLE: royal_castle(b, f); break;
    case ROYAL_KNOCK: case ROYAL_KNOCK_HEAVY: royal_knock(b, f); break;
    default: royal_start(b, ROYAL_WAIT); break;
    }
    return command;
}
