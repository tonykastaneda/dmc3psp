#include "combat.h"

#include <math.h>
#include <string.h>

#include "motion.h"
#include "enemy.h"

#define FADE_SECONDS 0.10f
#define SWORD_OUT_SECONDS 1.6f
#define MASH_WINDOW 0.30f      /* a press within this keeps Million Stab going */
#define STINGER_HOLD_FRAMES 14.0f

enum { BTN_MELEE, BTN_STYLE };

typedef struct {
    const char *motion;    /* rb-NN name */
    const char *name;      /* HUD label */
    int button;            /* which button drives this move's follow-ups / mash loop */
    float mash_loop_end;   /* >0: while mashing, frames past this wrap to the motion's loop start */
    int slam;              /* air move that drives Dante to the ground after frame 25 */
    /* Tap follow-up: a press before tap_until queues tap_next, which starts at
     * frame tap_switch (the data's hand-off frame). */
    float tap_until, tap_switch;
    MoveId tap_next;
    /* Late (paused) press in [late_from, late_until): switch immediately. */
    float late_from, late_until;
    MoveId late_next;
    /* Without input: continue into auto_next at auto_switch (or end at -1). */
    float auto_switch;
    MoveId auto_next;
    /* Appended (rows that omit them get 0): */
    int physics;           /* 0 = hover in the air while active, 1 = keep gravity */
    float start_vy;        /* vertical launch when the move starts (cm/s), 0 = none */
    int hold_loop;         /* loop [loop_start, end) while circle is held; release ends */
    /* Hit data (training dummy): first hit frame, repeat interval (0 = once),
     * reach in cm, reaction. Hit frames are set by eye from the swings. */
    float hit_frame, hit_repeat, reach;
    int hit_kind;          /* HitKind, or -1 for no hit */
    int root_y;            /* drive Dante's height from the motion's root Y */
} MoveDef;

#define M BTN_MELEE
#define S BTN_STYLE
#define NOHIT 0, 0, 0, -1
static const MoveDef moves[MOVE_COUNT] = {
    /*                   motion   label                 btn mash slam  tap: until switch next     late: from until next     auto: at next */
    [MOVE_COMBO1_1] = {"rb-03", "COMBO I - 1",          M, 0, 0, 18, 18, MOVE_COMBO1_2, 18, 55, MOVE_COMBO2_2, -1, MOVE_NONE},
    [MOVE_COMBO1_2] = {"rb-04", "COMBO I - 2",          M, 0, 0, 37, 37, MOVE_COMBO1_3, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_COMBO1_3] = {"rb-05", "COMBO I - 3",          M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_COMBO2_2] = {"rb-06", "COMBO II - 2",         M, 0, 0, 66, 66, MOVE_COMBO2_3, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_COMBO2_3] = {"rb-07", "COMBO II - 3",         M, 0, 0, 137, 137, MOVE_MILLION_STAB, 0, 0, MOVE_NONE, 137, MOVE_COMBO2_FINISH},
    [MOVE_MILLION_STAB] = {"rb-08", "MILLION STAB",     M, 96, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, 97, MOVE_COMBO2_FINISH},
    [MOVE_COMBO2_FINISH] = {"rb-09", "COMBO II - FINISH", M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_STINGER_DASH] = {"rb-14", "STINGER",          M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, 10, MOVE_STINGER_HOLD},
    [MOVE_STINGER_HOLD] = {"rb-15", "STINGER",          M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_STINGER_THRUST},
    [MOVE_STINGER_THRUST] = {"rb-16", "STINGER",        M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_HIGH_TIME] = {"rb-10", "HIGH TIME",           M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_AERIAL_RAVE] = {"rb-32", "AERIAL RAVE",       M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_HELM_BREAKER] = {"rb-29", "HELM BREAKER",     M, 0, 1, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    /* Swordmaster. rb-18 is stationary with the hips locked and the loop flag
     * set (Prop's spin; mashing keeps it going = Shredder). rb-17 hands off to
     * rb-19 at frame 20 (root continuity), a 215-frame flurry with a long
     * loop (Dance Macabre). rb-31 travels 430 cm and rises 150 (finisher).
     * rb-01 steps back and swings (Drive); rb-11 is a fast 186 cm dash slash
     * used as the combo cancel (Quick Drive). */
    [MOVE_SM_PROP] = {"rb-18", "PROP / SHREDDER",       S, 84, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_SM_DANCE_START] = {"rb-17", "DANCE MACABRE",  S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, 20, MOVE_SM_DANCE},
    [MOVE_SM_DANCE] = {"rb-19", "DANCE MACABRE",        S, 0, 0, 200, 200, MOVE_SM_CRAZY_DANCE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_SM_CRAZY_DANCE] = {"rb-31", "CRAZY DANCE",    S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_SM_DRIVE] = {"rb-01", "DRIVE",                S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_SM_QUICK_DRIVE] = {"rb-11", "QUICK DRIVE",    S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    /* Trickster (slot 33). Read from root motion: s33-17 crouches (hips 53)
     * then bursts 400 cm in ~15 frames; s33-18 is an airborne pose with 120 cm
     * of travel; s33-07 an in-air pose; s33-08 climbs (root y up 35). */
    [MOVE_TR_DASH] = {"s33-17", "DASH",                 S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_TR_SKY_STAR] = {"s33-18", "SKY STAR",         S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE},
    [MOVE_TR_AIR_HIKE] = {"s33-07", "AIR HIKE",         S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 1, 1150.0f},
    [MOVE_TR_WALL_HIKE] = {"s33-08", "WALL HIKE",       S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 1, 1450.0f},
    /* Royalguard (slot 34): s34-06 holds the guard (210 frames, loop from 131). */
    [MOVE_RG_BLOCK] = {"s34-06", "ROYAL BLOCK",         S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 0, 0, 1},
    [MOVE_RG_AIR_BLOCK] = {"s34-08", "AIR ROYAL BLOCK", S, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 0, 0, 1},
    /* 2-07 / 2-08 move 350 cm along +X / -X with the hips down to ~10 cm
     * (rolls); 2-09 moves 394 cm back with a 273 cm root-Y arc (backflip). */
    [MOVE_ROLL_LEFT] = {"2-07", "ROLL",                M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 0, 0, 0, NOHIT, 0},
    [MOVE_ROLL_RIGHT] = {"2-08", "ROLL",               M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 0, 0, 0, NOHIT, 0},
    [MOVE_BACKFLIP] = {"2-09", "BACKFLIP",             M, 0, 0, 0, 0, MOVE_NONE, 0, 0, MOVE_NONE, -1, MOVE_NONE, 0, 0, 0, NOHIT, 1},
};

/* Hit data per move (kept apart from the long table rows above):
 * {move, first hit frame, repeat interval, reach cm, HitKind}. */
static const struct { MoveId move; float frame, repeat, reach; int kind; } hit_table[] = {
    {MOVE_COMBO1_1, 12, 0, 230, HIT_LIGHT}, {MOVE_COMBO1_2, 12, 0, 230, HIT_LIGHT},
    {MOVE_COMBO1_3, 15, 0, 240, HIT_HEAVY}, {MOVE_COMBO2_2, 15, 0, 230, HIT_LIGHT},
    {MOVE_COMBO2_3, 20, 0, 230, HIT_LIGHT}, {MOVE_MILLION_STAB, 2, 5, 240, HIT_LIGHT},
    {MOVE_COMBO2_FINISH, 30, 0, 260, HIT_HEAVY}, {MOVE_STINGER_THRUST, 2, 0, 280, HIT_HEAVY},
    {MOVE_HIGH_TIME, 14, 0, 230, HIT_LAUNCH}, {MOVE_AERIAL_RAVE, 10, 18, 240, HIT_AIR},
    {MOVE_HELM_BREAKER, 30, 0, 260, HIT_SLAM}, {MOVE_SM_PROP, 6, 8, 230, HIT_LIGHT},
    {MOVE_SM_DANCE, 10, 12, 240, HIT_LIGHT}, {MOVE_SM_CRAZY_DANCE, 10, 10, 260, HIT_LIGHT},
    {MOVE_SM_DRIVE, 40, 0, 800, HIT_HEAVY}, {MOVE_SM_QUICK_DRIVE, 18, 0, 500, HIT_HEAVY},
};

static int hit_index(MoveId move) {
    unsigned int i;
    for (i = 0; i < sizeof(hit_table) / sizeof(hit_table[0]); ++i)
        if (hit_table[i].move == move) return (int)i;
    return -1;
}
#undef M
#undef S

static int motion_of[MOVE_COUNT];
static int idle_motion = -1;

static int find_motion(const char *name) {
    int i;
    for (i = 0; i < motion_count(); ++i)
        if (strcmp(motion_get(i)->name, name) == 0) return i;
    return -1;
}

int combat_init(void) {
    int m;
    for (m = 0; m < MOVE_COUNT; ++m) {
        motion_of[m] = find_motion(moves[m].motion);
        if (motion_of[m] < 0) return 0;
    }
    idle_motion = find_motion("rb-00");
    return 1;
}

int combat_idle_motion(void) { return idle_motion; }

const char *combat_move_name(MoveId move) {
    return move >= 0 && move < MOVE_COUNT ? moves[move].name : "";
}

void combat_reset(Combat *c) {
    memset(c, 0, sizeof(*c));
    c->move = MOVE_NONE;
    c->queued = MOVE_NONE;
    c->motion = -1;
    c->fade_motion = -1;
}

static void start_move(Combat *c, MoveId move, float frame);

static void end_move(Combat *c, Player *player) {
    c->fade_motion = c->motion;
    c->fade_frame = c->frame;
    c->fade_weight = 1.0f;
    c->move = MOVE_NONE;
    c->motion = -1;
    player->attacking = 0;
    player->free_fall = 0;
}

static void start_move(Combat *c, MoveId move, float frame) {
    /* Fade from whatever was showing (locomotion or the previous move). */
    c->fade_motion = c->move == MOVE_NONE ? -1 : c->motion;
    c->fade_frame = c->frame;
    c->fade_weight = 1.0f;
    c->move = move;
    c->motion = motion_of[move];
    c->frame = frame;
    c->queued = MOVE_NONE;
    c->mash_presses = 0;
    {
        int h = hit_index(move);
        c->next_hit = h >= 0 ? hit_table[h].frame : -1.0f;
    }
    motion_root(motion_get(c->motion), frame, c->last_root);
}

static void face_stick(Player *player, const PlayerInput *input, float camera_yaw) {
    /* DMC lets you aim the first hit with the stick. */
    float fx = sinf(camera_yaw), fz = cosf(camera_yaw), rx = -cosf(camera_yaw), rz = sinf(camera_yaw);
    float mx = fx * input->stick_y + rx * input->stick_x, mz = fz * input->stick_y + rz * input->stick_x;
    if (mx * mx + mz * mz > 0.04f) player->yaw = atan2f(mx, mz);
}

/* Stick direction relative to Dante's facing: +1 forward, -1 back, 0 neither. */
static int stick_relative(const Player *player, const PlayerInput *input, float camera_yaw) {
    float fx = sinf(camera_yaw), fz = cosf(camera_yaw), rx = -cosf(camera_yaw), rz = sinf(camera_yaw);
    float mx = fx * input->stick_y + rx * input->stick_x, mz = fz * input->stick_y + rz * input->stick_x;
    float len = sqrtf(mx * mx + mz * mz), dot;
    if (len < 0.5f) return 0;
    dot = (mx * sinf(player->yaw) + mz * cosf(player->yaw)) / len;
    return dot > 0.6f ? 1 : (dot < -0.6f ? -1 : 0);
}

static int is_combo_hit(MoveId move) {
    return move >= MOVE_COMBO1_1 && move <= MOVE_COMBO2_FINISH && move != MOVE_MILLION_STAB;
}

void combat_update(Combat *c, Player *player, const PlayerInput *input, float dt) {
    const Motion *m;
    float root[3];
    int tri = input->melee;
    /* Circle is Swordmaster's button; other styles' actions are not in yet. */
    int circ = input->style_action && player->style == STYLE_SWORDMASTER;
    int trick = input->style_action && player->style == STYLE_TRICKSTER;
    int guard = input->style_action && player->style == STYLE_ROYALGUARD;
    /* Trickster Air Hike: cross in the air, once per airtime. */
    int hike = input->jump && !player->on_ground && player->style == STYLE_TRICKSTER && !player->air_hiked;

    if (c->fade_weight > 0.0f) c->fade_weight -= dt / FADE_SECONDS;
    if (c->fade_weight < 0.0f) c->fade_weight = 0.0f;
    if (c->sword_out > 0.0f) c->sword_out -= dt;
    c->since_melee += dt;
    c->since_style += dt;
    if (tri) c->since_melee = 0.0f;
    if (circ) c->since_style = 0.0f;

    if (c->move == MOVE_NONE && input->jump && player->locked_on && player->on_ground) {
        /* Lock-on + direction + cross: roll left/right, backflip. */
        float fx = sinf(player->camera_yaw), fz = cosf(player->camera_yaw), rx = -cosf(player->camera_yaw), rz = sinf(player->camera_yaw);
        float mx = fx * input->stick_y + rx * input->stick_x, mz = fz * input->stick_y + rz * input->stick_x;
        float len = sqrtf(mx * mx + mz * mz);
        if (len > 0.5f) {
            float fwd = (mx * sinf(player->yaw) + mz * cosf(player->yaw)) / len;
            float side = (mx * -cosf(player->yaw) + mz * sinf(player->yaw)) / len;  /* + = Dante's right */
            if (fwd < -0.6f) start_move(c, MOVE_BACKFLIP, 0.0f);
            else if (side > 0.5f) start_move(c, MOVE_ROLL_RIGHT, 0.0f);
            else if (side < -0.5f) start_move(c, MOVE_ROLL_LEFT, 0.0f);
            if (c->move != MOVE_NONE) c->base_y = player->y;
        }
    }
    if (c->move == MOVE_NONE) {
        int dir = player->locked_on ? stick_relative(player, input, player->camera_yaw) : 0;
        player->attacking = 0;
        if (tri) {
            if (!player->on_ground) {
                start_move(c, dir > 0 ? MOVE_HELM_BREAKER : MOVE_AERIAL_RAVE, 0.0f);
            } else if (player->locked_on) {
                start_move(c, dir > 0 ? MOVE_STINGER_DASH : dir < 0 ? MOVE_HIGH_TIME : MOVE_COMBO1_1, 0.0f);
            } else {
                face_stick(player, input, player->camera_yaw);
                start_move(c, MOVE_COMBO1_1, 0.0f);
            }
        } else if (circ && player->on_ground) {
            if (!player->locked_on) face_stick(player, input, player->camera_yaw);
            start_move(c, dir > 0 ? MOVE_SM_DANCE_START : dir < 0 ? MOVE_SM_DRIVE : MOVE_SM_PROP, 0.0f);
        } else if (trick) {
            int pushing = input->stick_x * input->stick_x + input->stick_y * input->stick_y > 0.25f;
            face_stick(player, input, player->camera_yaw);
            if (player->on_ground && player->wall_contact && pushing) start_move(c, MOVE_TR_WALL_HIKE, 0.0f);
            else start_move(c, player->on_ground ? MOVE_TR_DASH : MOVE_TR_SKY_STAR, 0.0f);
        } else if (hike) {
            player->air_hiked = 1;
            start_move(c, MOVE_TR_AIR_HIKE, 0.0f);
        } else if (guard) {
            start_move(c, player->on_ground ? MOVE_RG_BLOCK : MOVE_RG_AIR_BLOCK, 0.0f);
        } else {
            return;
        }
        /* Launches (Air Hike, Wall Hike) happen as the move starts. */
        if (moves[c->move].start_vy > 0.0f) {
            player->vy = moves[c->move].start_vy;
            player->on_ground = 0;
        }
    } else {
        const MoveDef *d = &moves[c->move];
        int pressed = d->button == BTN_STYLE ? circ : tri;
        if (circ && is_combo_hit(c->move)) {
            start_move(c, MOVE_SM_QUICK_DRIVE, 0.0f);   /* Swordmaster combo cancel */
        } else if (pressed) {
            ++c->mash_presses;
            if (d->tap_next != MOVE_NONE && c->frame < d->tap_until) c->queued = d->tap_next;
            else if (d->late_next != MOVE_NONE && c->frame >= d->late_from && c->frame < d->late_until) {
                start_move(c, d->late_next, 0.0f);
            }
        }
    }

    /* High Time can be jump-cancelled once the launch has landed (DMC). */
    if (c->move == MOVE_HIGH_TIME && input->jump && c->frame > 20.0f && player->on_ground) {
        end_move(c, player);
        return;
    }
    player->attacking = 1;
    player->free_fall = moves[c->move].physics == 1;
    /* Free-fall moves end on landing; guards end when circle is released. */
    if (player->free_fall && player->on_ground && c->frame > 2.0f) { end_move(c, player); return; }
    if (moves[c->move].hold_loop && !input->style_held) { end_move(c, player); return; }
    /* Trickster and Royalguard moves don't draw the sword. */
    if (c->move < MOVE_TR_DASH) c->sword_out = SWORD_OUT_SECONDS;
    m = motion_get(c->motion);
    c->frame += dt * MOTION_FPS;

    /* Mash loops (Million Stab on triangle, Prop/Shredder on circle). */
    {
        const MoveDef *d = &moves[c->move];
        float since = d->button == BTN_STYLE ? c->since_style : c->since_melee;
        if (d->mash_loop_end > 0.0f && since < MASH_WINDOW && c->frame >= d->mash_loop_end &&
            m->loop_start < d->mash_loop_end) {
            c->frame = m->loop_start;
            motion_root(m, c->frame, c->last_root);
        }
        if (d->hold_loop && c->frame >= m->end_frame - 1.0f && m->loop_start < m->end_frame) {
            c->frame = m->loop_start;
            motion_root(m, c->frame, c->last_root);
        }
        /* Helm Breaker: after the flip, drive Dante down to the floor. */
        if (d->slam && c->frame >= 25.0f && player->y > 0.0f) {
            player->y -= 1800.0f * dt;
            if (player->y < 0.0f) player->y = 0.0f;
            if (player->y == 0.0f) player->on_ground = 1;
        }
    }
    /* Stinger: hold the lunge for a fixed distance before the thrust. */
    if (c->move == MOVE_STINGER_HOLD && c->frame >= STINGER_HOLD_FRAMES) {
        start_move(c, MOVE_STINGER_THRUST, 0.0f);
        m = motion_get(c->motion);
    }

    /* Root motion: bone 0 translation delta, in Dante's facing, moves the player. */
    motion_root(m, c->frame > m->end_frame ? m->end_frame : c->frame, root);
    {
        float dx = root[0] - c->last_root[0], dz = root[2] - c->last_root[2];
        float s = sinf(player->yaw), co = cosf(player->yaw);
        float limit = player_room_limit;
        player->x += co * dx + s * dz;
        player->z += -s * dx + co * dz;
        if (player->x > limit) player->x = limit;
        if (player->x < -limit) player->x = -limit;
        if (player->z > limit) player->z = limit;
        if (player->z < -limit) player->z = -limit;
        memcpy(c->last_root, root, sizeof(root));
        if (moves[c->move].root_y) {
            player->y = c->base_y + (root[1] > 0.0f ? root[1] : 0.0f);
            player->on_ground = root[1] <= 0.5f && c->frame > 5.0f;
        }
    }
    /* Hits on the training dummy: in reach, in front of Dante, at the right height. */
    if (c->next_hit >= 0.0f && c->frame >= c->next_hit && enemy_active()) {
        int h = hit_index(c->move);
        float e[3], dx, dz, dist, facing, dy;
        enemy_position(e);
        dx = e[0] - player->x; dz = e[2] - player->z;
        dist = sqrtf(dx * dx + dz * dz);
        facing = dist > 1.0f ? (dx * sinf(player->yaw) + dz * cosf(player->yaw)) / dist : 1.0f;
        dy = e[1] - player->y;
        if (h >= 0 && dist < hit_table[h].reach && facing > 0.2f && dy > -150.0f && dy < 200.0f) {
            enemy_hit((HitKind)hit_table[h].kind, player->x, player->z);
            ++c->hits_dealt;
        }
        c->next_hit = (h >= 0 && hit_table[h].repeat > 0.0f) ? c->next_hit + hit_table[h].repeat : -1.0f;
    }

    /* Hand-offs: queued follow-up, automatic continuation, or the end. */
    {
        const MoveDef *d = &moves[c->move];
        if (c->queued != MOVE_NONE && c->frame >= d->tap_switch) {
            MoveId next = c->queued;
            /* Million Stab only if the player is mashing; otherwise finish. */
            if (next == MOVE_MILLION_STAB && c->mash_presses < 3) next = MOVE_COMBO2_FINISH;
            start_move(c, next, 0.0f);
        } else if (d->auto_switch >= 0.0f && c->frame >= d->auto_switch && d->auto_next != MOVE_NONE) {
            start_move(c, d->auto_next, 0.0f);
        } else if (c->frame >= m->end_frame) {
            if (d->auto_next != MOVE_NONE && d->auto_switch < 0.0f) {
                start_move(c, d->auto_next, 0.0f);
            } else {
                end_move(c, player);
            }
        }
    }
}
