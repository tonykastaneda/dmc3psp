#include "combat.h"

#include <math.h>
#include <string.h>

#include "combat_data.h"
#include "motion.h"
#include "enemy.h"

#define FADE_SECONDS 0.10f
#define SWORD_OUT_SECONDS 1.6f
#define STINGER_HOLD_SECONDS (14.0f / MOTION_FPS) /* lunge length before the thrust (not decoded) */
#define MASH_GAP_SECONDS (13.0f / MOTION_FPS)     /* input patterns 16 / 40: gaps <= 13 frames */
#define MASH_PRESSES 6                            /* ... more than 6 presses */
#define CHARGE_SECONDS (90.0f / MOTION_FPS)       /* pattern 4: hold 90 frames, release */
#define MASH_KEEP_SECONDS 0.30f                   /* a press this recent keeps a mash loop going */
#define DANCE_STAB_PLAYS 2   /* action 207's handler (0x267000) replays 3:52 twice, then 3:53 */
#define BUFFER_SECONDS 0.15f                      /* a press stays usable this long (test room choice) */
#define MAX_DASHES 3          /* FUN_00245f10: dashes in a row < 3 at the top style level */
#define SNAP_DASH 0.349f      /* 0xe39 = 20 deg: a dash this close to the target aims at it */
#define SNAP_SKY_STAR 0.175f  /* 0x71c = 10 deg (Sky Star) */
/* Air Trick (FUN_00226550, player config chunk 9): the warp lasts +0x290 = 10
 * frames and stops +0x298 = 120 cm short of the target, at most +0x294 =
 * 500 cm across and +0x29c = 500 cm up or down. It aims at the lock-on point
 * (player+0x3c40), which the test room doesn't have: the dummy's feet plus
 * AIR_TRICK_LOCK_HEIGHT (estimate). */
#define AIR_TRICK_FRAMES 10.0f
#define AIR_TRICK_SHORT 120.0f
#define AIR_TRICK_MAX_XZ 500.0f
#define AIR_TRICK_MAX_Y 500.0f
#define AIR_TRICK_LOCK_HEIGHT 200.0f
#define AIR_TRICK_AIR_WAIT 8.0f   /* from the air the warp starts at frame 8 */

enum { BTN_NONE, BTN_TRI, BTN_CIR };
#define STYLE_ANY 0xFF

typedef struct {
    const char *script;    /* "bank:index" in combat_data.h; NULL = hand-made move */
    const char *motion;    /* hand-made moves only */
    const char *name;      /* HUD label */
    MoveId next;           /* continuation (part of the same action) */
    int next_on;           /* 0 at motion end, 1 after hold time (Stinger) */
    int mash;              /* loop the motion's loop range while this button is mashed */
    int slam;              /* air move that drives Dante to the floor after frame 25 */
    float reach;           /* training-dummy reach in cm (hit spheres not decoded yet) */
    int hit_kind;          /* HitKind, -1 = never hits */
    /* Hand-made moves: */
    int physics;           /* 1 = keep gravity (Air Hike, Wall Hike) */
    float start_vy;        /* vertical launch when the move starts (cm/s) */
    int hold_loop;         /* loop while circle is held; release ends */
    int root_y;            /* drive Dante's height from the motion's root Y */
} MoveDef;

#define S(key) key, NULL
#define H(mot) NULL, mot
static const MoveDef moves[MOVE_COUNT] = {
    [MOVE_COMBO1_1] = {S("3:3"), "COMBO I", MOVE_NONE, 0, 0, 0, 230, HIT_LIGHT},
    [MOVE_COMBO1_2] = {S("3:4"), "COMBO I", MOVE_NONE, 0, 0, 0, 230, HIT_LIGHT},
    [MOVE_COMBO1_3] = {S("3:5"), "COMBO I", MOVE_NONE, 0, 0, 0, 240, HIT_HEAVY},
    [MOVE_COMBO2_2] = {S("3:6"), "COMBO II", MOVE_NONE, 0, 0, 0, 230, HIT_LIGHT},
    [MOVE_COMBO2_3] = {S("3:7"), "COMBO II", MOVE_NONE, 0, 0, 0, 230, HIT_HEAVY},
    [MOVE_MILLION_STAB] = {S("3:8"), "MILLION STAB", MOVE_COMBO2_FINISH, 0, BTN_TRI, 0, 240, HIT_LIGHT},
    [MOVE_COMBO2_FINISH] = {S("3:9"), "MILLION STAB", MOVE_NONE, 0, 0, 0, 260, HIT_HEAVY},
    [MOVE_STINGER_DASH] = {S("3:14"), "STINGER", MOVE_STINGER_HOLD, 0, 0, 0, 280, HIT_HEAVY},
    [MOVE_STINGER_HOLD] = {S("3:15"), "STINGER", MOVE_STINGER_THRUST, 1, 0, 0, 280, HIT_HEAVY},
    [MOVE_STINGER_THRUST] = {S("3:16"), "STINGER", MOVE_NONE, 0, 0, 0, 280, HIT_HEAVY},
    [MOVE_HIGH_TIME] = {S("3:11"), "HIGH TIME", MOVE_NONE, 0, 0, 0, 230, HIT_LAUNCH},
    [MOVE_HELM_BREAKER] = {S("3:10"), "HELM BREAKER", MOVE_NONE, 0, 0, 1, 260, HIT_SLAM},
    [MOVE_ROUND_TRIP] = {S("3:17"), "ROUND TRIP", MOVE_ROUND_TRIP_CATCH, 0, 0, 0, 500, HIT_LIGHT},
    [MOVE_ROUND_TRIP_CATCH] = {S("3:19"), "ROUND TRIP", MOVE_NONE, 0, 0, 0, 500, HIT_LIGHT},
    [MOVE_SM_SWORD_PIERCE] = {S("13:0"), "SWORD PIERCE", MOVE_NONE, 0, 0, 0, 600, HIT_HEAVY},
    [MOVE_SM_PROP] = {S("13:19"), "PROP", MOVE_NONE, 0, 0, 0, 230, HIT_LIGHT},
    [MOVE_SM_SHREDDER] = {S("13:12"), "SHREDDER", MOVE_SM_SHREDDER_END, 0, BTN_CIR, 0, 230, HIT_LIGHT},
    [MOVE_SM_SHREDDER_END] = {S("13:13"), "SHREDDER", MOVE_NONE, 0, 0, 0, 230, HIT_HEAVY},
    [MOVE_SM_DANCE_START] = {S("13:17"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_1] = {S("3:46"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_2] = {S("3:47"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_3] = {S("3:48"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_4] = {S("3:49"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_5] = {S("3:50"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_6] = {S("3:51"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_STAB] = {S("3:52"), "DANCE MACABRE", MOVE_SM_DANCE_FINISH, 0, 0, 0, 240, HIT_LIGHT},
    [MOVE_SM_DANCE_FINISH] = {S("3:53"), "DANCE MACABRE", MOVE_NONE, 0, 0, 0, 260, HIT_HEAVY},
    [MOVE_SM_CRAZY_DANCE] = {S("13:18"), "CRAZY DANCE", MOVE_SM_CRAZY_FINISH, 0, BTN_CIR, 0, 260, HIT_LIGHT},
    [MOVE_SM_CRAZY_FINISH] = {S("13:20"), "CRAZY DANCE", MOVE_NONE, 0, 0, 0, 280, HIT_HEAVY},
    [MOVE_SM_RAVE_1] = {S("13:7"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR},
    [MOVE_SM_RAVE_2] = {S("13:8"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR},
    [MOVE_SM_RAVE_3] = {S("13:9"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR},
    [MOVE_SM_RAVE_4] = {S("13:10"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_SLAM},
    /* Trickster: no attack; root motion carries Dante (dash 400 cm, Sky Star
     * 580 cm, Wall Hike 462 cm up). */
    [MOVE_TR_DASH] = {S("23:0"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH_BACK] = {S("23:1"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH_LEFT] = {S("23:2"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH_RIGHT] = {S("23:3"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH2] = {S("23:24"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH2_BACK] = {S("23:25"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH2_LEFT] = {S("23:26"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH2_RIGHT] = {S("23:27"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_DASH_CHAIN] = {S("23:17"), "DASH", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_SKY_STAR] = {S("23:11"), "SKY STAR", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_AIR_TRICK] = {S("23:15"), "AIR TRICK", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_AIR_TRICK_AIR] = {S("23:16"), "AIR TRICK", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_WALL_HIKE] = {S("23:5"), "WALL HIKE", MOVE_TR_WALL_FLIP, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    [MOVE_TR_WALL_FLIP] = {S("23:6"), "WALL HIKE", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    /* Hand-made (Air Hike's state and the Royalguard block states are not
     * decoded yet; these play stand-in motions). */
    [MOVE_TR_AIR_HIKE] = {H("2-04"), "AIR HIKE", MOVE_NONE, 0, 0, 0, 0, -1, 1, 1150.0f},
    [MOVE_RG_BLOCK] = {H("s34-06"), "ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_RG_AIR_BLOCK] = {H("s34-08"), "AIR ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_ROLL_LEFT] = {H("2-07"), "ROLL", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_ROLL_RIGHT] = {H("2-08"), "ROLL", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_BACKFLIP] = {H("2-09"), "BACKFLIP", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
};
#undef S
#undef H

/* Command action id -> first move (the action handlers, FUN_002625d0). */
static MoveId action_move(int action) {
    switch (action) {
    case 1: return MOVE_COMBO1_1;
    case 2: return MOVE_COMBO1_2;
    case 3: return MOVE_COMBO1_3;
    case 4: return MOVE_COMBO2_2;
    case 5: return MOVE_COMBO2_3;
    case 6: return MOVE_HELM_BREAKER;
    case 7: case 8: return MOVE_STINGER_DASH;
    case 10: return MOVE_HIGH_TIME;
    case 13: return MOVE_ROUND_TRIP;
    case 14: return MOVE_MILLION_STAB;
    case 15: return MOVE_SM_SWORD_PIERCE;
    case 16: return MOVE_SM_PROP;
    case 19: return MOVE_SM_RAVE_1;
    case 20: return MOVE_SM_RAVE_2;
    case 21: return MOVE_SM_RAVE_3;
    case 22: return MOVE_SM_RAVE_4;
    case 24: return MOVE_SM_SHREDDER;
    case 200: return MOVE_SM_DANCE_START;
    case 201: case 202: case 203: case 204: case 205: case 206:
        return (MoveId)(MOVE_SM_DANCE_1 + (action - 201));
    case 207: return MOVE_SM_DANCE_STAB;
    case 208: return MOVE_SM_CRAZY_DANCE;
    /* Trickster: start_trickster picks the script (direction, chain, posture). */
    case 180: return MOVE_TR_DASH;
    case 181: return MOVE_TR_SKY_STAR;
    case 182: return MOVE_TR_AIR_TRICK;
    case 183: return MOVE_TR_WALL_HIKE;
    default: return MOVE_NONE;
    }
}

static int motion_of[MOVE_COUNT];
static int script_of[MOVE_COUNT];
static int idle_motion = -1;

int combat_init(void) {
    int m, s;
    for (m = 0; m < MOVE_COUNT; ++m) {
        const char *name = moves[m].motion;
        script_of[m] = -1;
        if (moves[m].script) {
            for (s = 0; s < SCRIPT_DATA_COUNT; ++s)
                if (strcmp(script_data[s].key, moves[m].script) == 0) script_of[m] = s;
            if (script_of[m] < 0) return 0;
            name = script_data[script_of[m]].motion;
        }
        motion_of[m] = motion_find(name);
        if (motion_of[m] < 0) return 0;
    }
    idle_motion = motion_find("rb-00");
    return 1;
}

int combat_idle_motion(void) { return idle_motion; }

const char *combat_move_name(MoveId move) {
    return move >= 0 && move < MOVE_COUNT ? moves[move].name : "";
}

void combat_reset(Combat *c) {
    memset(c, 0, sizeof(*c));
    c->move = MOVE_NONE;
    c->motion = -1;
    c->script = -1;
    c->fade_motion = -1;
}

static void end_move(Combat *c, Player *player) {
    c->fade_motion = c->motion;
    c->fade_frame = c->frame;
    c->fade_weight = 1.0f;
    c->move = MOVE_NONE;
    c->action = 0;
    c->motion = -1;
    c->script = -1;
    c->pending = 0;
    c->active = 0;
    memset(c->gates, 0, sizeof(c->gates));
    c->dashes = 0;        /* FUN_0021f500 (back to neutral) clears the dash count */
    c->warp_time = 0.0f;
    player->attacking = 0;
    player->free_fall = 0;
}

static void seek_events(Combat *c) {
    const ScriptData *sd;
    c->next_event = 0;
    if (c->script < 0) return;
    sd = &script_data[c->script];
    while (c->next_event < sd->count && sd->events[c->next_event].frame < c->frame) ++c->next_event;
}

/* Starts a move. A new action resets the gates (each script opens its own);
 * a continuation keeps the action. */
static void start_move(Combat *c, MoveId move, int action) {
    const Motion *m;
    if (move != c->move || action) c->plays = 0;
    c->fade_motion = c->move == MOVE_NONE ? -1 : c->motion;
    c->fade_frame = c->frame;
    c->fade_weight = 1.0f;
    c->move = move;
    if (action) c->action = action;
    c->motion = motion_of[move];
    c->script = script_of[move];
    m = motion_get(c->motion);
    /* Scripts start at their op-1 frame, or the motion's own first frame. */
    c->frame = c->script >= 0 && script_data[c->script].start >= 0 ? (float)script_data[c->script].start
                                                                    : m->loop_start;
    c->pending = 0;
    c->active = 0;
    c->phase_time = 0.0f;
    memset(c->gates, 0, sizeof(c->gates));
    /* The script's first segments run at once even when they wait for frames
     * before the start frame (the game only waits for frames still ahead). */
    c->next_event = 0;
    motion_root(m, c->frame, c->last_root);
}

/* Height above the floor under Dante (FUN_0024b510: y - floor y). */
static float height_above_floor(const Player *player) {
    float floor_y = 0.0f;
    if (player_floor_fn && !player_floor_fn(player->x, player->y, player->z, 0.0f, &floor_y)) floor_y = 0.0f;
    return player->y - floor_y;
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
    float len = sqrtf(mx * mx + mz * mz), dot, ref = player->yaw;
    if (len < 0.5f) return 0;
    /* Locked on to something: forward / back are toward / away from it (the
     * target-relative sectors of the input patterns), not Dante's facing,
     * which may still be turning toward the target. */
    if (player->locked_on && player->has_target)
        ref = atan2f(player->target_x - player->x, player->target_z - player->z);
    dot = (mx * sinf(ref) + mz * cosf(ref)) / len;
    return dot > 0.6f ? 1 : (dot < -0.6f ? -1 : 0);
}

/* Which command inputs (CommandInput bits) happened this frame. */
static unsigned int read_inputs(Combat *c, const Player *player, const PlayerInput *input, float dt) {
    unsigned int in = 0;
    int dir = player->locked_on ? stick_relative(player, input, player->camera_yaw) : 0;
    int tri = input->melee, cir = input->style_action;
    c->since_tri += dt;
    c->since_cir += dt;
    if (tri) { c->tri_mash = c->since_tri <= MASH_GAP_SECONDS ? c->tri_mash + 1 : 1; c->since_tri = 0.0f; }
    if (cir) { c->cir_mash = c->since_cir <= MASH_GAP_SECONDS ? c->cir_mash + 1 : 1; c->since_cir = 0.0f; }
    if (tri || cir) c->last_dir = dir;
    if (tri) in |= 1u << IN_TRI;
    if (cir) in |= 1u << IN_CIR;
    if (tri && input->lock_on && dir > 0) in |= 1u << IN_LOCK_FWD_TRI;
    if (tri && input->lock_on && dir < 0) in |= 1u << IN_LOCK_BACK_TRI;
    if (cir && input->lock_on && dir > 0) in |= 1u << IN_LOCK_FWD_CIR;
    if (cir && input->lock_on && dir < 0) in |= 1u << IN_LOCK_BACK_CIR;
    if (cir && input->lock_on && player->has_target && dir > 0) in |= 1u << IN_LOCK_TARGET_CIR;
    if (tri && c->tri_mash > MASH_PRESSES) in |= 1u << IN_MASH_TRI;
    if (cir && c->cir_mash > MASH_PRESSES) in |= 1u << IN_MASH_CIR;
    if (input->melee_held) c->tri_held += dt;
    else {
        if (c->tri_held >= CHARGE_SECONDS) in |= 1u << IN_CHARGE_TRI;
        c->tri_held = 0.0f;
    }
    /* Keep a press alive for a few frames so a tap just before a gate opens
     * still counts (the game's pattern history plays this role). */
    c->buffer_time -= dt;
    if (in) { c->buffered = in; c->buffer_time = BUFFER_SECONDS; }
    else if (c->buffer_time > 0.0f) in = c->buffered;
    return in;
}

static const CommandData *command_for(int action) {
    int i;
    for (i = 0; i < COMMAND_DATA_COUNT; ++i)
        if (command_data[i].action == action) return &command_data[i];
    return NULL;
}

/* FUN_0024bc40: every required gate open (1 = buffer only), an optional one
 * satisfies all; "after" needs the current action. 0 fail, 1 buffer, 2 go. */
static int conditions(const Combat *c, const CommandData *cmd) {
    int need = 0, have = 0, buffered = 0, i, afters = 0, after_ok = 0;
    for (i = 0; i < 5; ++i) {
        int g = cmd->gates[i], state;
        if (g == 0xFF) continue;
        state = c->gates[g & 0x0F];
        if (!(g & 0x80)) ++need;
        if (state) {
            have = (g & 0x80) ? 99 : have + 1;
            if (state == 1) buffered = 1;
        }
    }
    for (i = 0; i < 3; ++i) {
        if (!cmd->after[i]) continue;
        ++afters;
        if (cmd->after[i] == c->action) after_ok = 1;
    }
    if (have < need || (afters && !after_ok)) return 0;
    return buffered ? 1 : 2;
}

/* Test room style -> the game's style id (command table byte 0xd, player+0x3c6c). */
static int game_style(Style style) {
    switch (style) {
    case STYLE_SWORDMASTER: return 0;
    case STYLE_GUNSLINGER: return 1;
    case STYLE_TRICKSTER: return 2;
    case STYLE_ROYALGUARD: return 3;
    default: return -1;
    }
}

static int stick_pushed(const PlayerInput *input) {
    return input->stick_x * input->stick_x + input->stick_y * input->stick_y > 0.25f;
}

/* FUN_0024b510: the command's extra check. Height ones (Helm Breaker > 120 cm,
 * Aerial Rave > 80 cm) come from the player config; case 2 (Wall Hike) needs
 * the stick pushed into a wall. The rest are not decoded and fail. */
static int extra_check(const CommandData *cmd, const Player *player, const PlayerInput *input) {
    float h;
    if (cmd->check == 2) return player->wall_contact && stick_pushed(input);
    h = command_min_height(cmd->check);
    return h >= 0.0f && height_above_floor(player) > h;
}

static int is_trickster(MoveId move) { return move >= MOVE_TR_DASH && move <= MOVE_TR_WALL_FLIP; }

/* The command walker (FUN_002458f0 / FUN_00245f10) for Rebellion,
 * Swordmaster and Trickster: returns the action to start, or 0. */
static int walk_commands(Combat *c, const Player *player, const PlayerInput *input, unsigned int inputs) {
    const CommandData *cur = c->move != MOVE_NONE ? command_for(c->action) : NULL;
    int posture = player->on_ground ? POSTURE_GROUND : POSTURE_AIR, i;
    for (i = 0; i < COMMAND_DATA_COUNT; ++i) {
        const CommandData *cmd = &command_data[i];
        int pressed = (inputs >> cmd->input) & 1u, ok;
        if (!(cmd->posture & posture)) continue;
        if (cmd->type == 3 && cmd->style != game_style(player->style)) continue;
        if (!pressed && c->pending != cmd->action) continue;
        if (action_move(cmd->action) == MOVE_NONE) continue;
        if ((cmd->flags & 0x400) && !extra_check(cmd, player, input)) continue;
        if (cmd->action == 180 && c->dashes >= MAX_DASHES) continue;
        if (c->move == MOVE_NONE) {
            if (cmd->flags & 0x100) return cmd->action;
            continue;
        }
        if (!cur) {
            /* Trickster states clear the current action (player+0x2998 = 0),
             * so the walker only needs the cancel flag and the gates. Other
             * hand-made moves don't cancel. */
            if (!is_trickster(c->move) || !(cmd->flags & 0x200)) continue;
            ok = conditions(c, cmd);
            if (ok == 2) return cmd->action;
            if (ok == 1 && !c->pending) c->pending = cmd->action;
            continue;
        }
        if (!(cmd->flags & 0x200) || cmd->action == c->action) continue;
        /* Category order: a same-type follow-up must rank above the current move. */
        if (!(cur->type == 2 || cur->type != cmd->type || cur->category < cmd->category ||
              ((cmd->flags & 0x100) && cur->category == 9))) continue;
        ok = conditions(c, cmd);
        if (ok == 2) return cmd->action;
        if (ok == 1 && !c->pending) c->pending = cmd->action;
    }
    return 0;
}

static int script_has_active(const ScriptData *sd) {
    int i;
    for (i = 0; i < sd->count; ++i)
        if (sd->events[i].kind == EV_ACTIVE && sd->events[i].a) return 1;
    return 0;
}

/* Applies the script events up to the current frame; returns hits to deal. */
static int run_events(Combat *c) {
    const ScriptData *sd;
    int hits = 0;
    if (c->script < 0) return 0;
    sd = &script_data[c->script];
    while (c->next_event < sd->count && sd->events[c->next_event].frame <= c->frame) {
        const ScriptEvent *e = &sd->events[c->next_event++];
        if (e->kind == EV_GATE) c->gates[e->a & 0x0F] = e->b;
        else if (e->kind == EV_ACTIVE) {
            if (e->a && !c->active) ++hits;
            c->active = e->a;
        } else if (e->kind == EV_HIT && (c->active || !script_has_active(sd))) {
            /* Window 0 byte 2 0x2n re-arms the attack: a new hit inside an
             * active window, or each stab of a move without one (Million Stab). */
            ++hits;
        }
    }
    return hits;
}

static void deal_hit(Combat *c, const Player *player) {
    const MoveDef *d = &moves[c->move];
    float e[3], dx, dz, dist, facing, dy;
    if (d->hit_kind < 0 || !enemy_active()) return;
    enemy_position(e);
    dx = e[0] - player->x; dz = e[2] - player->z;
    dist = sqrtf(dx * dx + dz * dz);
    facing = dist > 1.0f ? (dx * sinf(player->yaw) + dz * cosf(player->yaw)) / dist : 1.0f;
    dy = e[1] - player->y;
    if (dist < d->reach && facing > 0.2f && dy > -150.0f && dy < 200.0f) {
        enemy_hit((HitKind)d->hit_kind, player->x, player->z);
        ++c->hits_dealt;
    }
}

static float wrap_pi(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

/* Stick direction in world space (yaw), or 0 if the stick is neutral. */
static int stick_yaw(const Player *player, const PlayerInput *input, float *yaw) {
    float fx = sinf(player->camera_yaw), fz = cosf(player->camera_yaw);
    float rx = -cosf(player->camera_yaw), rz = sinf(player->camera_yaw);
    float mx = fx * input->stick_y + rx * input->stick_x, mz = fz * input->stick_y + rz * input->stick_x;
    if (mx * mx + mz * mz < 0.25f) return 0;
    *yaw = atan2f(mx, mz);
    return 1;
}

/* Snap to the lock-on target when the aim is within `snap` of it. */
static void snap_to_target(Player *player, float snap) {
    float to;
    if (!player->locked_on || !player->has_target) return;
    to = atan2f(player->target_x - player->x, player->target_z - player->z);
    if (fabsf(wrap_pi(to - player->yaw)) < snap) player->yaw = to;
}

/* Trickster states (FUN_00225c90 dash, FUN_002261b0 Sky Star, FUN_00226550
 * Air Trick, FUN_00227fb0 Wall Hike): pick the script and the facing. */
static void start_trickster(Combat *c, Player *player, const PlayerInput *input, int action) {
    float sy = player->yaw;
    int pushed = stick_yaw(player, input, &sy);
    MoveId prev = c->move;
    if (action == 180) {
        /* FUN_00254180: with R1 held the stick picks fwd / back / left /
         * right against Dante's facing (45 deg sectors, config +0xbc);
         * otherwise (or neutral) he dashes the way the stick points. */
        int sector = 0;   /* 0 fwd, 1 back, 2 left, 3 right */
        MoveId base;
        ++c->dashes;
        if (input->lock_on && pushed) {
            float d = wrap_pi(sy - player->yaw);
            if (fabsf(d) <= 0.785f) sector = 0;
            else if (fabsf(d) >= 2.356f) sector = 1;
            else sector = d > 0.0f ? 3 : 2;   /* yaw grows toward Dante's right */
        }
        base = c->dashes >= 2 ? MOVE_TR_DASH2 : MOVE_TR_DASH;
        if (sector == 0 && c->dashes == 2 && prev == MOVE_TR_DASH) base = MOVE_TR_DASH_CHAIN;
        else base = (MoveId)(base + sector);
        if (sector == 0) {
            if (pushed) player->yaw = sy;
            snap_to_target(player, SNAP_DASH);
        } else if (sector == 1) {
            player->yaw = wrap_pi(sy + (float)M_PI);   /* face away, dash backward */
        }
        start_move(c, base, 0);
    } else if (action == 181) {
        if (pushed) player->yaw = sy;
        snap_to_target(player, SNAP_SKY_STAR);
        start_move(c, MOVE_TR_SKY_STAR, 0);
    } else if (action == 182) {
        start_move(c, player->on_ground ? MOVE_TR_AIR_TRICK : MOVE_TR_AIR_TRICK_AIR, 0);
        c->warp_time = 0.0f;
        if (player->has_target)
            player->yaw = atan2f(player->target_x - player->x, player->target_z - player->z);
    } else {
        if (pushed) player->yaw = sy;   /* into the wall */
        start_move(c, MOVE_TR_WALL_HIKE, 0);
        c->base_y = player->y;
    }
    c->action = 0;   /* states, not actions: player+0x2998 = 0 */
}

/* Air Trick, after the vanish (FUN_00226550 states 1-2): fly toward the
 * lock-on point for AIR_TRICK_FRAMES, then drop (state 3). Returns 1 while
 * the move goes on. */
static int air_trick_update(Combat *c, Player *player, float dt) {
    float wait = c->move == MOVE_TR_AIR_TRICK_AIR ? AIR_TRICK_AIR_WAIT : motion_get(c->motion)->end_frame;
    if (c->warp_time <= 0.0f) {
        float dx, dz, dist, along, dy;
        if (c->frame < wait) return 1;
        dx = player->target_x - player->x; dz = player->target_z - player->z;
        dist = sqrtf(dx * dx + dz * dz);
        along = player->has_target ? dist - AIR_TRICK_SHORT : 0.0f;
        if (along > AIR_TRICK_MAX_XZ) along = AIR_TRICK_MAX_XZ;
        if (along < 0.0f) along = 0.0f;
        dy = player->has_target ? player->target_y + AIR_TRICK_LOCK_HEIGHT - player->y : 0.0f;
        if (dy > AIR_TRICK_MAX_Y) dy = AIR_TRICK_MAX_Y;
        if (dy < -AIR_TRICK_MAX_Y) dy = -AIR_TRICK_MAX_Y;
        c->warp_step[0] = dist > 1.0f ? dx / dist * along : 0.0f;
        c->warp_step[1] = dy;
        c->warp_step[2] = dist > 1.0f ? dz / dist * along : 0.0f;
        c->warp_time = AIR_TRICK_FRAMES / MOTION_FPS;
        player->on_ground = 0;
    }
    {
        float step = dt / (AIR_TRICK_FRAMES / MOTION_FPS);
        float left = c->warp_time;
        if (step * (AIR_TRICK_FRAMES / MOTION_FPS) > left) step = left / (AIR_TRICK_FRAMES / MOTION_FPS);
        player->x += c->warp_step[0] * step;
        player->y += c->warp_step[1] * step;
        player->z += c->warp_step[2] * step;
        c->warp_time -= dt;
        if (c->warp_time <= 0.0f) {
            player->vy = 0.0f;
            return 0;   /* drop from here */
        }
    }
    return 1;
}

/* Moves whose data isn't decoded: Air Hike, Royalguard, lock-on rolls. */
static void start_handmade(Combat *c, Player *player, const PlayerInput *input) {
    int guard = input->style_action && player->style == STYLE_ROYALGUARD;
    int hike = input->jump && !player->on_ground && player->style == STYLE_TRICKSTER && !player->air_hiked;
    if (input->jump && player->locked_on && player->on_ground) {
        float fx = sinf(player->camera_yaw), fz = cosf(player->camera_yaw);
        float rx = -cosf(player->camera_yaw), rz = sinf(player->camera_yaw);
        float mx = fx * input->stick_y + rx * input->stick_x, mz = fz * input->stick_y + rz * input->stick_x;
        float len = sqrtf(mx * mx + mz * mz);
        if (len > 0.5f) {
            float fwd = (mx * sinf(player->yaw) + mz * cosf(player->yaw)) / len;
            float side = (mx * -cosf(player->yaw) + mz * sinf(player->yaw)) / len;  /* + = Dante's right */
            if (fwd < -0.6f) start_move(c, MOVE_BACKFLIP, 0);
            else if (side > 0.5f) start_move(c, MOVE_ROLL_RIGHT, 0);
            else if (side < -0.5f) start_move(c, MOVE_ROLL_LEFT, 0);
            if (c->move != MOVE_NONE) c->base_y = player->y;
        }
    } else if (hike) {
        player->air_hiked = 1;
        start_move(c, MOVE_TR_AIR_HIKE, 0);
    } else if (guard) {
        start_move(c, player->on_ground ? MOVE_RG_BLOCK : MOVE_RG_AIR_BLOCK, 0);
    }
    if (c->move != MOVE_NONE && moves[c->move].start_vy > 0.0f) {
        player->vy = moves[c->move].start_vy;
        player->on_ground = 0;
    }
}

void combat_update(Combat *c, Player *player, const PlayerInput *input, float dt) {
    const Motion *m;
    const MoveDef *d;
    float root[3];
    unsigned int inputs = read_inputs(c, player, input, dt);
    int action, hits;

    if (c->fade_weight > 0.0f) c->fade_weight -= dt / FADE_SECONDS;
    if (c->fade_weight < 0.0f) c->fade_weight = 0.0f;
    if (c->sword_out > 0.0f) c->sword_out -= dt;

    /* Free cancels: gate 15 hands control back to movement, gate 8 to a jump. */
    if (c->move != MOVE_NONE && c->script >= 0) {
        int stick = input->stick_x * input->stick_x + input->stick_y * input->stick_y > 0.25f;
        if ((c->gates[15] == 2 && (stick || input->jump)) || (c->gates[8] == 2 && input->jump)) {
            end_move(c, player);
            return;
        }
    }

    action = walk_commands(c, player, input, inputs);
    if (action >= 180 && action <= 183) {
        c->buffer_time = 0.0f;
        start_trickster(c, player, input, action);
    } else if (action) {
        c->buffer_time = 0.0f;
        if (c->move == MOVE_NONE && player->on_ground && !player->locked_on) face_stick(player, input, player->camera_yaw);
        if (is_trickster(c->move)) c->dashes = 0;
        start_move(c, action_move(action), action);
    } else if (c->move == MOVE_NONE) {
        start_handmade(c, player, input);
        if (c->move == MOVE_NONE) { player->attacking = 0; return; }
    }

    d = &moves[c->move];
    player->attacking = 1;
    player->free_fall = d->physics == 1;
    if (player->free_fall && player->on_ground && c->phase_time > 2.0f / MOTION_FPS) { end_move(c, player); return; }
    if (d->hold_loop && !input->style_held) { end_move(c, player); return; }
    if (d->script && d->hit_kind >= 0) c->sword_out = SWORD_OUT_SECONDS;
    m = motion_get(c->motion);
    c->frame += dt * MOTION_FPS;
    c->phase_time += dt;

    /* Mash loops (Million Stab, Shredder, Crazy Dance) and held guards replay
     * the motion's loop range; the script's events replay with it. */
    if (m->looping && m->loop_to > m->loop_from && c->frame >= m->loop_to) {
        float since = d->mash == BTN_TRI ? c->since_tri : c->since_cir;
        if ((d->mash && since < MASH_KEEP_SECONDS) || d->hold_loop || c->move == MOVE_STINGER_HOLD) {
            c->frame = m->loop_from;
            motion_root(m, c->frame, c->last_root);
            seek_events(c);
        }
    } else if (d->hold_loop && c->frame >= m->end_frame - 1.0f) {
        c->frame = m->loop_start;   /* stand-in guard motion without a loop range */
        motion_root(m, c->frame, c->last_root);
    }
    hits = run_events(c);
    while (hits-- > 0) deal_hit(c, player);

    /* Helm Breaker: after the flip, drive Dante down to the floor. */
    if (d->slam && c->frame >= 25.0f && player->y > 0.0f) {
        player->y -= 1800.0f * dt;
        if (player->y < 0.0f) player->y = 0.0f;
        if (player->y == 0.0f) player->on_ground = 1;
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
        if (d->root_y) {
            player->y = c->base_y + (root[1] > 0.0f ? root[1] : 0.0f);
            player->on_ground = root[1] <= 0.5f && c->phase_time > 5.0f / MOTION_FPS;
        }
    }

    /* Air Trick: vanish, warp to the target, then drop. */
    if (c->move == MOVE_TR_AIR_TRICK || c->move == MOVE_TR_AIR_TRICK_AIR) {
        if (!air_trick_update(c, player, dt)) end_move(c, player);
        return;
    }

    /* Continuations within the action, or the end of the move. */
    if (d->next != MOVE_NONE && d->next_on == 1 && c->phase_time >= STINGER_HOLD_SECONDS) {
        start_move(c, d->next, 0);
    } else if (c->frame >= m->end_frame) {
        ++c->plays;
        if (c->move == MOVE_SM_DANCE_STAB && c->plays < DANCE_STAB_PLAYS) start_move(c, c->move, 0);
        else if (d->next != MOVE_NONE) start_move(c, d->next, 0);
        else end_move(c, player);
    }
}
