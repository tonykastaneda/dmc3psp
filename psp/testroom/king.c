/* The Damned King's logic, FUN_004341a0 and its actions 0x4345b0 (the move) and 0x434890 (the attack), and his blast
 * object 0x43ebe0. docs/CHESS_ROYALS_SPEC.md "The King". */
#include "king.h"
#include <math.h>
#include <string.h>

/* Per rank (0-3). FUN_00409690's row at 0x5bfd30 (base + spread x a random 0-1); the move's rise and glide are both
 * 0x5c2c00's (frames, cm a frame). */
static const float wait_base[4] = {320.0f, 280.0f, 240.0f, 200.0f}, wait_spread = 60.0f;
static const float step_frames[4] = {20.0f, 15.0f, 15.0f, 10.0f}, rise_speed[4] = {2.0f, 2.5f, 2.5f, 4.0f};

void king_spawn(KingBrain *k, float hp, int rank) {
    RoyalBody keep = k->body;
    memset(k, 0, sizeof *k);
    k->body.pos[0] = keep.pos[0]; k->body.pos[1] = keep.pos[1]; k->body.pos[2] = keep.pos[2];
    k->body.yaw = keep.yaw; k->body.floor_y = keep.floor_y;
    k->body.hp = k->hp_before = hp; k->body.rank = rank & 3;
    k->body.motion = -1;
    k->near_timer = 120.0f;   /* [inferred] its value whenever Dante is out of reach */
    royal_motion(&k->body, 0);
}

static void king_move(KingBrain *k, float f, float dx, float dz, int respond) {
    RoyalBody *b = &k->body;
    switch (b->sub) {
    case 0:
        royal_motion(b, -1);
        b->timer = step_frames[b->rank];
        b->sub = 1;
        /* fall through */
    case 1:
        b->pos[1] += rise_speed[b->rank] * f;
        b->timer -= f;
        if (b->timer <= 0.0f) {
            royal_heading_velocity(b, royal_heading8(b, dx, dz));
            b->timer = step_frames[b->rank];
            b->sub = 2;
        }
        break;
    case 2:
        b->pos[0] += b->vel[0] * f; b->pos[2] += b->vel[2] * f;
        b->timer -= f;
        if (respond || b->timer <= 0.0f) {
            b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
            b->sub = 3;
        }
        break;
    case 3:
        royal_fall(b, f, 5.0f);
        break;
    }
}

static void blast_start(KingBrain *k) {
    KingBlast *x = &k->blast;
    x->active = 1; x->state = 0;
    x->pos[0] = k->body.pos[0]; x->pos[1] = k->body.pos[1]; x->pos[2] = k->body.pos[2];
    x->radius = 100.0f; x->height = 5.0f;
    x->hitting = 0;
}

static void blast_update(KingBrain *k, float f) {
    KingBlast *x = &k->blast;
    if (!x->active) return;
    x->hitting = 0;
    if (x->state == 0) {
        k->body.events |= KING_EV_CHARGE;
        x->time = 240.0f;
        x->state = 1;
    }
    if (x->state == 1) {
        x->pos[0] = k->body.pos[0]; x->pos[1] = k->body.pos[1]; x->pos[2] = k->body.pos[2];
        if (x->time < 180.0f) {
            k->body.events |= KING_EV_BLAST;
            x->radius = 1500.0f;
            x->height += 280.0f;
            x->state = 2;
        }
    }
    x->height += 1.0f;
    x->time -= f;
    if (x->time <= 0.0f) {
        x->active = 0;
        k->body.events |= KING_EV_BLAST_END;
        return;
    }
    x->hitting = x->time > 150.0f && x->time < 180.0f;
}

static void king_attack(KingBrain *k, float f) {
    RoyalBody *b = &k->body;
    if (b->sub == 0) {
        royal_motion(b, -1);
        b->timer2 = 0.0f;
        k->command = ROYAL_CMD_NONE;
        b->sub = 1;
    }
    if (b->sub == 1 && b->frame > 100.0f && b->frame < 102.0f) {
        blast_start(k);
        b->sub = 2;
    }
    if (b->done) royal_start(b, ROYAL_WAIT);
}

int king_think(KingBrain *k, float f, float dx, float dy, float dz, int command, float (*unit)(void)) {
    RoyalBody *b = &k->body;
    float ex = dx - b->pos[0], ez = dz - b->pos[2], d = sqrtf(ex * ex + ez * ez);
    int respond;
    (void)dy;
    b->events = 0; b->busy = 0;
    blast_update(k, f);
    if (b->hp <= 0.0f) { k->hp_before = b->hp; return command; }
    if (command == ROYAL_CMD_CASTLE) { royal_start(b, ROYAL_CASTLE); command = ROYAL_CMD_NONE; }
    if (command == ROYAL_CMD_RESPOND) { k->command = ROYAL_CMD_RESPOND; command = ROYAL_CMD_NONE; }
    /* 0x4341a0: hurt with Dante within 500, or Dante within 350 for 120 frames of a move */
    if (b->hp < k->hp_before) {
        if (d < 500.0f) { k->command = ROYAL_CMD_RESPOND; k->near_timer = 120.0f; }
    } else if (b->action == ROYAL_MOVE) {
        if (d < 350.0f) {
            k->near_timer -= f;
            if (k->near_timer <= 0.0f) { k->command = ROYAL_CMD_RESPOND; k->near_timer = 120.0f; }
        } else k->near_timer = 120.0f;
    }
    k->hp_before = b->hp;
    respond = k->command == ROYAL_CMD_RESPOND;
    if (respond) {
        if (b->action == ROYAL_WAIT) royal_start(b, ROYAL_ATTACK);
        else if (b->action == ROYAL_ATTACK) k->command = ROYAL_CMD_NONE;
    }
    switch (b->action) {
    case ROYAL_WAIT:
        if (royal_wait(b, f, d, wait_base[b->rank], wait_spread, unit)) royal_start(b, ROYAL_MOVE);
        break;
    case ROYAL_MOVE: king_move(k, f, dx, dz, respond); break;
    case ROYAL_ATTACK: king_attack(k, f); break;
    case ROYAL_CASTLE: royal_castle(b, f); break;
    case ROYAL_KNOCK: case ROYAL_KNOCK_HEAVY: royal_knock(b, f); break;
    default: royal_start(b, ROYAL_WAIT); break;
    }
    return command;
}

int king_blast_reaches(const KingBlast *x, float px, float py, float pz) {
    float ex = px - x->pos[0], ez = pz - x->pos[2];
    if (!x->active || !x->hitting) return 0;
    return ex * ex + ez * ez <= x->radius * x->radius && py >= x->pos[1] - 50.0f && py <= x->pos[1] + x->height;
}
