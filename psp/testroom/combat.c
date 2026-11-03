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
#define SHORT_MASH_GAP_SECONDS (9.0f / MOTION_FPS) /* input pattern: Shotgun's mash CIR >2x, gaps <= 9 frames */
#define SHORT_MASH_PRESSES 2
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

enum { BTN_NONE, BTN_TRI, BTN_CIR, BTN_SQ };
enum { NEXT_END, NEXT_TIME, NEXT_FLAG1, NEXT_FLAG2, NEXT_LAND, NEXT_MASH, NEXT_CHARGE, NEXT_TIMER, NEXT_STANCE, NEXT_SHOT };
#define GUN_WALK_SPEED 3.5f        /* cm per frame: movement table +4 */
#define GUN_WALK_TURN 0.19635f     /* 0x800 per frame: the travel direction's turn limit */
#define GUN_AIM_TURN 0.41888f      /* player config +0xc0 = 24 degrees per frame */
#define GUN_LEGS_BLEND_FRAMES 10.0f /* FUN_002243e0: FUN_00222e00(10, ...) on a new row */
#define GUN_STANCE_AFTER_SHOT 10.0f /* weapon config +0x588 */
#define GUN_STANCE_AUTO_AIM 50.0f  /* FUN_00246d40: a neutral-stick shot that found a target */
#define GUN_FIRST_AIM_FRAMES 10.0f /* FUN_002688c0: the first shot from outside the stance */

static int gun_rows[5][6][4];      /* motion indices: idle, aim, shot, legs */

#define CHARGE_TIER_FRAMES 30.0f   /* weapon config +0x504: charge below this is the plain release */
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
    /* Generated phases (tools/moveset_phases.py): */
    float start;           /* motion frame to start at; <= 0 = the script's */
    int gun;               /* a gun move: Rebellion stays on the back, shots reach far */
    int btn;               /* button a mash / charge phase watches */
    MoveId loop_move;      /* NEXT_MASH: played while mashing */
    MoveId tiers[3];       /* NEXT_CHARGE: by charge time */
    float frames;          /* NEXT_TIMER / NEXT_STANCE: length */
    int air, flag_air;     /* set the velocities below at the start / at flag 1 */
    float air_v[4], flag_v[4];
    MoveId lock_move;      /* played instead when locked on to a target */
    int shot;              /* shots fired as this phase starts */
    float auto_fire;       /* frames between further shots during the phase */
    float refire;          /* NEXT_SHOT: frames before another press fires again */
    int layered;           /* upper-body shot: the legs walk under it */
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
    /* The block states' scripts (FUN_00224850 24:0, FUN_00225a00 24:4), not yet their logic. */
    [MOVE_RG_BLOCK] = {S("24:0"), "ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_RG_AIR_BLOCK] = {S("24:4"), "AIR ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_ROLL_LEFT] = {H("2-07"), "ROLL", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_ROLL_RIGHT] = {H("2-08"), "ROLL", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_BACKFLIP] = {H("2-09"), "BACKFLIP", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
#define GEN_MOVE_TABLE
#include "combat_moves_table.h"
#undef GEN_MOVE_TABLE
};
#undef S
#undef H
#include "combat_moves_table.h"

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
    default: return gen_action_move(action);
    }
}

static int motion_of[MOVE_COUNT];
static int script_of[MOVE_COUNT];
static int idle_motion = -1;

int combat_init(void) {
    int m, s, g, r;
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
    for (g = 0; g < 5; ++g) {
        for (r = 0; r < 6; ++r) {
            const GunStanceRow *row = &gun_stance_rows[g][r];
            gun_rows[g][r][0] = motion_find(row->idle);
            gun_rows[g][r][1] = motion_find(row->aim);
            gun_rows[g][r][2] = motion_find(row->shot);
            gun_rows[g][r][3] = motion_find(row->legs);
            for (m = 0; m < 4; ++m) if (gun_rows[g][r][m] < 0) return 0;
        }
    }
    return 1;
}

int combat_idle_motion(void) { return idle_motion; }

static int d_is_alternating(const Combat *c) { return moves[c->move].next_on == NEXT_SHOT; }

void combat_stance_motion(const Combat *c, int *upper, float *upper_frame) {
    *upper = c->stance_upper;
    *upper_frame = c->stance_frame;
}

int combat_move_is_gun(MoveId move) { return move >= 0 && move < MOVE_COUNT && moves[move].gun; }

int combat_take_shots(Combat *c) {
    int shots = c->shots_pending;
    c->shots_pending = 0;
    return shots;
}

const char *combat_move_name(MoveId move) {
    return move >= 0 && move < MOVE_COUNT ? moves[move].name : "";
}

void combat_reset(Combat *c) {
    memset(c, 0, sizeof(*c));
    c->move = MOVE_NONE;
    c->motion = -1;
    c->script = -1;
    c->fade_motion = -1;
    c->lower_motion = -1;
    c->lower_fade_motion = -1;
    c->stance_upper = -1;
}

static void end_move(Combat *c, Player *player) {
    c->last_move_for_end = c->move;
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
    c->air = 0;
    /* Shots hand over to the stance; anything else leaves the legs to it too. */
    if (moves[c->last_move_for_end].layered) c->stance_timer = c->stance_timer > GUN_STANCE_AFTER_SHOT ? c->stance_timer : GUN_STANCE_AFTER_SHOT;
    c->delayed_shot = 0;
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
    /* Blend from what's on screen: the last move, the gun stance, or plain locomotion. */
    if (c->move != MOVE_NONE) {
        c->fade_motion = c->motion;
        c->fade_frame = c->frame;
    } else if (c->stance_active && c->stance_upper >= 0) {
        c->fade_motion = c->stance_upper;
        c->fade_frame = c->stance_frame;
    } else {
        c->fade_motion = -1;
        c->fade_frame = 0.0f;
    }
    c->fade_weight = 1.0f;
    c->move = move;
    if (action) c->action = action;
    c->motion = motion_of[move];
    c->script = script_of[move];
    m = motion_get(c->motion);
    /* Scripts start at their op-1 frame, or the motion's own first frame. */
    c->frame = c->script >= 0 && script_data[c->script].start >= 0 ? (float)script_data[c->script].start
                                                                    : m->loop_start;
    if (moves[move].start > 0.0f) c->frame = moves[move].start;
    c->charge_frames = 0.0f;
    c->charge_looped = 0;
    c->phase_frames = 0.0f;
    c->shots_pending += moves[move].shot;
    c->auto_timer = moves[move].auto_fire;
    c->shot_buffered = 0;
    if (moves[move].layered) c->upper_pending = 1;
    else if (!moves[move].gun || !c->stance_active) c->lower_motion = -1;
    if (!moves[move].layered) c->stance_active = 0;
    if (moves[move].air) {
        memcpy(c->air_v, moves[move].air_v, sizeof(c->air_v));
        c->air = 1;
    }
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
    if (input->gun) {
        c->sq_mash = c->since_sq <= MASH_GAP_SECONDS ? c->sq_mash + 1 : 1;
        c->since_sq = 0.0f;
        in |= 1u << IN_SQ;
        if (c->sq_mash > MASH_PRESSES) in |= 1u << IN_MASH_SQ;
    }
    c->since_sq += dt;
    if (cir) {
        c->cir_short_mash = c->since_cir_short <= SHORT_MASH_GAP_SECONDS ? c->cir_short_mash + 1 : 1;
        c->since_cir_short = 0.0f;
        if (c->cir_short_mash > SHORT_MASH_PRESSES) in |= 1u << IN_MASH_CIR_SHORT;
    }
    c->since_cir_short += dt;
    /* Patterns on release: charge 90 frames (guns), and Artemis' 0-frame charge. */
    if (input->gun_held) c->sq_held += dt;
    else {
        if (c->sq_held > 0.0f) in |= 1u << IN_RELEASE_SQ;
        if (c->sq_held >= CHARGE_SECONDS) in |= 1u << IN_CHARGE_SQ;
        c->sq_held = 0.0f;
    }
    if (input->style_held) c->cir_held += dt;
    else {
        if (c->cir_held > 0.0f) in |= 1u << IN_RELEASE_CIR;
        c->cir_held = 0.0f;
    }
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
    if (cmd->check == 8) return 1;  /* FUN_0024b510 case 8: only Royal Release 194 needs more (the gauge) */
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
        if (cmd->weapon != 0xFF && cmd->weapon != player->melee_weapon && cmd->weapon != player->gun_weapon) continue;
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
        else if (e->kind == EV_FLAG) c->flag = e->a;
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

static int d_is_alternating(const Combat *c);

/* ---- Gun stance (states 12 / 13, FUN_00223c30 and FUN_002243e0) ------------
 * R1 draws the gun (FUN_002436e0 -> FUN_00243a60): Dante faces the target, or
 * holds his facing with none, and walks at the table's walk speed with the legs
 * for his direction against the aim. */
static int gun_index(const Player *player) { return player->gun_weapon - 5; }

static void gun_walk(Combat *c, Player *player, const PlayerInput *input, float dt) {
    float frames = dt * MOTION_FPS, stick = player->yaw, aim;
    int has_aim = player->has_target && (player->locked_on || c->aim_target), row = 5, pushed = stick_yaw(player, input, &stick);
    int legs;
    aim = has_aim ? atan2f(player->target_x - player->x, player->target_z - player->z) : player->yaw;
    if (has_aim) {
        float d = wrap_pi(aim - player->yaw), t = GUN_AIM_TURN * frames;
        player->yaw = wrap_pi(player->yaw + (d > t ? t : d < -t ? -t : d));
    }
    if (pushed) {
        if (!c->walking) c->walk_dir = stick;      /* state 12 starts toward the stick */
        else {
            float d = wrap_pi(stick - c->walk_dir), t = GUN_WALK_TURN * frames;
            c->walk_dir = wrap_pi(c->walk_dir + (d > t ? t : d < -t ? -t : d));
        }
        c->walking = 1;
        {
            /* FUN_00223e80: 0x2000 and 0x6000 of a 0x10000 circle. */
            float d = wrap_pi(aim - c->walk_dir);
            if (fabsf(d) <= 0.7854f) row = 0;
            else if (fabsf(d) >= 2.3562f) row = 2;
            else row = d < 0.0f ? 1 : 4;
        }
        player->x += sinf(c->walk_dir) * GUN_WALK_SPEED * frames;
        player->z += cosf(c->walk_dir) * GUN_WALK_SPEED * frames;
    } else {
        c->walking = 0;
    }
    c->gun_row = row;
    legs = gun_rows[gun_index(player)][row][3];
    if (legs != c->lower_motion) {
        if (c->lower_motion >= 0) {
            c->lower_fade_motion = c->lower_motion;
            c->lower_fade_frame = c->lower_frame;
            c->lower_fade_weight = 1.0f;
        }
        c->lower_motion = legs;
        c->lower_frame = motion_get(legs)->loop_start;
    }
    c->lower_frame = motion_wrap(motion_get(legs), c->lower_frame + frames);
    if (c->lower_fade_weight > 0.0f) {
        c->lower_fade_weight -= frames / GUN_LEGS_BLEND_FRAMES;
        c->lower_fade_frame = motion_wrap(motion_get(c->lower_fade_motion), c->lower_fade_frame + frames);
    }
}

/* With no move playing: is the stance on, and what do the arms do? */
static void gun_stance_step(Combat *c, Player *player, const PlayerInput *input, float dt) {
    int on = player->on_ground && (input->lock_on || c->stance_timer > 0.0f);
    int was = c->stance_active;
    if (c->stance_timer > 0.0f) c->stance_timer -= dt * MOTION_FPS;
    if (!on) {
        if (was) {
            /* Back to the walk / run / idle states. */
            c->fade_motion = c->lower_motion;
            c->fade_frame = c->lower_frame;
            c->fade_weight = 1.0f;
            c->lower_motion = -1;
            c->lower_fade_weight = 0.0f;
        }
        c->stance_active = 0;
        c->shot_count = 0;     /* FUN_0021d280 clears player+0x299f back in neutral */
        c->aim_target = 0;
        player->gun_stance = 0;
        return;
    }
    if (!was) {
        /* Coming from a move, end_move already set the blend from that move. */
        if (c->fade_weight <= 0.0f) {
            c->fade_motion = -1;
            c->fade_frame = 0.0f;
            c->fade_weight = 1.0f;
        }
        c->walking = 0;
        c->sword_out = 0.0f;
    }
    c->stance_active = 1;
    player->gun_stance = 1;
    gun_walk(c, player, input, dt);
    {
        const int *row = gun_rows[gun_index(player)][c->gun_row];
        int upper = player->has_target && (player->locked_on || c->aim_target) ? row[1] : row[0];
        if (upper != c->stance_upper) { c->stance_upper = upper; c->stance_frame = motion_get(upper)->loop_start; }
        c->stance_frame = motion_wrap(motion_get(upper), c->stance_frame + dt * MOTION_FPS);
    }
}

/* Upper-body shots run over the same walk. */
static void update_gun_layers(Combat *c, Player *player, const PlayerInput *input, float dt) {
    gun_walk(c, player, input, dt);
    if (c->upper_pending) {
        const int *row = gun_rows[gun_index(player)][c->gun_row];
        float stick;
        int neutral = !stick_yaw(player, input, &stick);
        c->upper_pending = 0;
        c->was_stance = c->stance_active;
        ++c->shot_count;
        c->motion = row[2];
        /* FUN_00246d40: a shot with the stick neutral finds a target by itself. */
        c->aim_target = player->has_target && (player->locked_on || neutral);
        if (c->aim_target && !player->locked_on) c->stance_timer = GUN_STANCE_AUTO_AIM;
        /* Odd shots play the motion from its start, even ones from 15: the two guns. */
        if (d_is_alternating(c)) c->frame = (c->shot_count & 1) ? 0.0f : 15.0f;
        else c->frame = motion_get(c->motion)->loop_start;
        c->next_event = 0;
        c->since_fire = 0.0f;
        /* FUN_002688c0: the first shot turns to the target before firing; from
         * outside the stance that takes at least 10 frames. */
        c->shot_delay = 0.0f;
        if (c->aim_target && c->shot_count == 1) {
            float d = fabsf(wrap_pi(atan2f(player->target_x - player->x, player->target_z - player->z) - player->yaw));
            c->shot_delay = d / GUN_AIM_TURN;
            if (!c->was_stance && c->shot_delay < GUN_FIRST_AIM_FRAMES) c->shot_delay = GUN_FIRST_AIM_FRAMES;
            if (c->shot_delay > 0.0f && c->shots_pending > 0) { --c->shots_pending; c->delayed_shot = 1; }
        }
    }
    if (c->delayed_shot) {
        c->shot_delay -= dt * MOTION_FPS;
        if (c->shot_delay <= 0.0f) { c->delayed_shot = 0; ++c->shots_pending; c->since_fire = 0.0f; }
    } else {
        c->since_fire += dt * MOTION_FPS;
    }
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
            /* Against the target when there is one (FUN_00253650), since lock-on
             * doesn't hold Dante's facing on it; the roll then starts facing it. */
            float ref = player->has_target ? atan2f(player->target_x - player->x, player->target_z - player->z) : player->yaw;
            float fwd = (mx * sinf(ref) + mz * cosf(ref)) / len;
            float side = (mx * -cosf(ref) + mz * sinf(ref)) / len;  /* + = Dante's right */
            if (fwd < -0.6f) start_move(c, MOVE_BACKFLIP, 0);
            else if (side > 0.5f) start_move(c, MOVE_ROLL_RIGHT, 0);
            else if (side < -0.5f) start_move(c, MOVE_ROLL_LEFT, 0);
            if (c->move != MOVE_NONE) { c->base_y = player->y; player->yaw = ref; }
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
        if (c->move == MOVE_NONE && player->on_ground && !player->locked_on && !moves[action_move(action)].layered) face_stick(player, input, player->camera_yaw);
        /* Locked on, attacks go at the target even if Dante was running elsewhere. */
        else if (c->move == MOVE_NONE && player->locked_on && player->has_target)
            player->yaw = atan2f(player->target_x - player->x, player->target_z - player->z);
        if (is_trickster(c->move)) c->dashes = 0;
        {
            MoveId first = action_move(action);
            if (player->locked_on && player->has_target && moves[first].lock_move != MOVE_NONE && moves[first].lock_move)
                first = moves[first].lock_move;
            start_move(c, first, action);
        }
    } else if (c->move == MOVE_NONE) {
        start_handmade(c, player, input);
        if (c->move == MOVE_NONE) {
            player->attacking = 0;
            gun_stance_step(c, player, input, dt);
            return;
        }
    }

    d = &moves[c->move];
    player->attacking = 1;
    player->free_fall = d->physics == 1;
    if (player->free_fall && player->on_ground && c->phase_time > 2.0f / MOTION_FPS) { end_move(c, player); return; }
    if (d->hold_loop && !input->style_held) { end_move(c, player); return; }
    if (d->script && d->hit_kind >= 0 && c->move <= MOVE_SM_RAVE_4) c->sword_out = SWORD_OUT_SECONDS;
    /* Nevan's guitar stance (action 99) lasts until any input. */
    if (d->next_on == NEXT_STANCE && (stick_pushed(input) || input->jump || input->melee || input->style_action || input->gun)) {
        end_move(c, player);
        return;
    }
    if (d->layered) update_gun_layers(c, player, input, dt);
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
    c->flag = 0;
    hits = run_events(c);
    if (d->flag_air && c->flag == 1) {
        memcpy(c->air_v, d->flag_v, sizeof(c->air_v));
        c->air = 1;
    }
    if (d->next_on == NEXT_CHARGE) c->charge_frames += dt * MOTION_FPS;
    while (hits-- > 0) deal_hit(c, player);

    /* Helm Breaker: after the flip, drive Dante down to the floor. */
    if (d->slam && c->frame >= 25.0f && player->y > 0.0f) {
        player->y -= 1800.0f * dt;
        if (player->y < 0.0f) player->y = 0.0f;
        if (player->y == 0.0f) player->on_ground = 1;
    }

    /* Root motion: bone 0 translation delta, in Dante's facing, moves the player. */
    if (d->layered) memcpy(root, c->last_root, sizeof(root));
    else
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

    /* Velocity-driven moves (FUN_002e2560): forward speed, vertical speed and
     * gravity per frame, until Dante reaches the floor. */
    if (c->air) {
        float frames = dt * MOTION_FPS, floor_y = 0.0f, s = sinf(player->yaw), co = cosf(player->yaw);
        player->on_ground = 0;
        player->x += s * c->air_v[0] * frames;
        player->z += co * c->air_v[0] * frames;
        player->y += c->air_v[2] * frames;
        c->air_v[0] += c->air_v[1] * frames;
        if (c->air_v[0] < 0.0f) c->air_v[0] = 0.0f;
        c->air_v[2] += c->air_v[3] * frames;
        if (c->air_v[2] < -70.0f) c->air_v[2] = -70.0f;   /* FUN_002e2560's fall limit */
        if (player_floor_fn && !player_floor_fn(player->x, player->y, player->z, 45.0f, &floor_y)) floor_y = -1.0e9f;
        if (c->air_v[2] <= 0.0f && player->y <= floor_y) {
            player->y = floor_y;
            player->vy = 0.0f;
            player->on_ground = 1;
            c->air = 0;
            if (d->next != MOVE_NONE) start_move(c, d->next, 0);
            else end_move(c, player);
            return;
        }
    }

    /* Guns that keep firing through a phase. */
    if (d->auto_fire > 0.0f) {
        c->auto_timer -= dt * MOTION_FPS;
        if (c->auto_timer <= 0.0f) { c->auto_timer += d->auto_fire; ++c->shots_pending; }
    }

    /* Generated phases: what moves on to the next script. */
    if (d->next_on >= NEXT_FLAG1) {
        int at_end = c->frame >= m->end_frame;
        int go = 0;
        float since = d->btn == BTN_TRI ? c->since_tri : d->btn == BTN_SQ ? c->since_sq : c->since_cir;
        int held = d->btn == BTN_TRI ? input->melee_held : d->btn == BTN_SQ ? input->gun_held : input->style_held;
        switch (d->next_on) {
        case NEXT_FLAG1: go = c->flag == 1 || at_end; break;
        case NEXT_FLAG2: go = c->flag == 2 || at_end; break;
        case NEXT_LAND:
            /* Airborne until the floor (above); hold the motion's last frame. */
            if (at_end) {
                if (!c->air && player->on_ground) go = 1;
                else c->frame = m->end_frame;
            }
            break;
        case NEXT_MASH:
            if (at_end) {
                if (since < MASH_KEEP_SECONDS && d->loop_move != MOVE_NONE) { start_move(c, d->loop_move, 0); return; }
                go = 1;
            }
            break;
        case NEXT_CHARGE:
            if (!held) {
                MoveId t = d->tiers[c->charge_frames < CHARGE_TIER_FRAMES ? 0 : c->charge_looped ? 2 : 1];
                if (t != MOVE_NONE) start_move(c, t, 0);
                else end_move(c, player);
                return;
            }
            if (at_end) {
                c->charge_looped = 1;
                c->frame = d->start > 0.0f ? d->start : m->loop_start;
                motion_root(m, c->frame, c->last_root);
                seek_events(c);
            }
            break;
        case NEXT_SHOT:
            /* FUN_002688c0: a press from `refire` frames on fires again, and the
             * shot ends `frames` frames in. */
            c->phase_frames += dt * MOTION_FPS;
            /* (Not the press that started this shot: that's the frame count's first frame.) */
            if (input->gun && c->phase_frames > 1.5f) c->shot_buffered = 1;
            if (c->shot_buffered && !c->delayed_shot && c->since_fire >= d->refire) { start_move(c, c->move, 0); return; }
            if (c->phase_frames >= d->frames) go = 1;
            break;
        case NEXT_TIMER:
        case NEXT_STANCE:
            c->phase_frames += dt * MOTION_FPS;
            if (c->phase_frames >= d->frames) go = 1;
            else if (at_end) {
                c->frame = m->loop_start;
                motion_root(m, c->frame, c->last_root);
                seek_events(c);
            }
            break;
        default: break;
        }
        if (go) {
            if (d->next != MOVE_NONE) start_move(c, d->next, 0);
            else end_move(c, player);
        }
        return;
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

int combat_selftest(int *total, int *failed_action, int *fallbacks) {
    static Combat c;
    static Player p;
    PlayerInput in;
    int finished = 0;
    unsigned int i;
    int (*saved_floor)(float, float, float, float, float *) = player_floor_fn;
    player_floor_fn = 0;   /* flat floor at 0, whatever the stage */
    memset(&in, 0, sizeof(in));
    in.style = -1;
    *total = (int)sizeof(gen_actions);
    *failed_action = -1;
    *fallbacks = 0;
    for (i = 0; i < sizeof(gen_actions); ++i) {
        int action = gen_actions[i], frame;
        combat_reset(&c);
        memset(&p, 0, sizeof(p));
        p.gun_weapon = 5;
        p.on_ground = 1;
        /* Air moves start in the air; ground ones on the floor. */
        if (moves[action_move(action)].air) { p.y = 400.0f; p.on_ground = 0; }
        start_move(&c, action_move(action), action);
        for (frame = 0; frame < 6000 && c.move != MOVE_NONE; ++frame) {
            MoveId before = c.move;
            const MoveDef *d = &moves[before];
            int waited_flag = d->next_on == NEXT_FLAG1 || d->next_on == NEXT_FLAG2;
            combat_update(&c, &p, &in, 1.0f / 60.0f);
            if (waited_flag && c.move != before && !c.flag) ++*fallbacks;
        }
        if (c.move == MOVE_NONE) ++finished;
        else if (*failed_action < 0) *failed_action = action;
    }
    player_floor_fn = saved_floor;
    return finished;
}

int combat_selftest_inputs(int *total, int *first_failed_action) {
    static Combat c;
    static Player p;
    int ok = 0, i;
    int (*saved_floor)(float, float, float, float, float *) = player_floor_fn;
    player_floor_fn = 0;
    *total = 0;
    *first_failed_action = -1;
    for (i = 0; i < COMMAND_DATA_COUNT; ++i) {
        const CommandData *cmd = &command_data[i];
        PlayerInput in;
        int release = cmd->input == IN_RELEASE_SQ || cmd->input == IN_RELEASE_CIR;
        if (!(cmd->flags & 0x100) || cmd->after[0]) continue;       /* neutral commands only */
        if (cmd->input != IN_TRI && cmd->input != IN_CIR && cmd->input != IN_SQ && !release) continue;
        if ((cmd->flags & 0x400) && cmd->check != 8 && command_min_height(cmd->check) < 0.0f) continue; /* walls */
        ++*total;
        combat_reset(&c);
        memset(&p, 0, sizeof(p));
        p.melee_weapon = 0;
        p.gun_weapon = 5;
        if (cmd->weapon < 5) p.melee_weapon = cmd->weapon;
        else if (cmd->weapon < 10) p.gun_weapon = cmd->weapon;
        if (cmd->type == 3) {
            static const Style styles[4] = {STYLE_SWORDMASTER, STYLE_GUNSLINGER, STYLE_TRICKSTER, STYLE_ROYALGUARD};
            p.style = styles[cmd->style & 3];
        }
        p.on_ground = cmd->posture != POSTURE_AIR;
        p.y = p.on_ground ? 0.0f : 400.0f;
        memset(&in, 0, sizeof(in));
        in.style = -1;
        if (release) {
            /* Artemis: fires on release of a hold. */
            if (cmd->input == IN_RELEASE_SQ) in.gun_held = 1; else in.style_held = 1;
            combat_update(&c, &p, &in, 1.0f / 60.0f);
            if (c.move != MOVE_NONE) { /* a press row took it first */ }
            memset(&in, 0, sizeof(in));
            in.style = -1;
        } else if (cmd->input == IN_TRI) in.melee = 1;
        else if (cmd->input == IN_CIR) in.style_action = 1;
        else in.gun = 1;
        if (c.move == MOVE_NONE) combat_update(&c, &p, &in, 1.0f / 60.0f);
        if (c.move != MOVE_NONE) ++ok;
        else if (*first_failed_action < 0) *first_failed_action = cmd->action;
    }
    player_floor_fn = saved_floor;
    return ok;
}
