#include "combat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combat_data.h"
#include "motion.h"
#include "enemy.h"
#include "leviathan_boss.h"
#include "stage.h"
#include "nfx.h"

#define FADE_SECONDS 0.10f
#define SWORD_OUT_SECONDS 1.6f
#define STINGER_HOLD_SECONDS (3.0f / MOTION_FPS)  /* FUN_00264fe0, maxed: HD Collection traces */
#define HIGH_TIME_HOLD_FRAME 20.0f                /* TRI still down here: the rising version */
#define MASH_GAP_SECONDS (13.0f / MOTION_FPS)     /* input patterns 16 / 40: gaps <= 13 frames */
#define MASH_PRESSES 6                            /* ... more than 6 presses */
#define CHARGE_SECONDS (90.0f / MOTION_FPS)       /* pattern 4: hold 90 frames, release */
#define SHORT_MASH_GAP_SECONDS (9.0f / MOTION_FPS) /* input pattern: Shotgun's mash CIR >2x, gaps <= 9 frames */
#define SHORT_MASH_PRESSES 2
#define MASH_KEEP_SECONDS 0.30f                   /* a press this recent keeps a mash loop going */
#define DANCE_STAB_PLAYS 2   /* action 207's handler (0x267000) replays 3:52 twice, then 3:53 */
#define MAX_DASHES 3          /* FUN_00245f10: dashes in a row < 3 at the top style level */
#define STARTUP_FRAMES 1.0f
/* The script's waits and the motion's end are seen a frame after the frame they name (FUN_00255160 resumes a
 * script once the motion has passed it). PS2, PS2_INPUT_LAG=1: the root moves from the frame after the request,
 * gates and the end come a frame later than that clock. */
#define SCRIPT_LAG 1.0f
#define TRICKSTER_LEVEL 2     /* player+0x3c75, 0 = Lv1: the full moveset is Lv3 */
#define SNAP_DASH 0.349f      /* 0xe39 = 20 deg: a dash this close to the target aims at it */
#define SNAP_SKY_STAR 0.175f  /* 0x71c = 10 deg (Sky Star) */
/* Air Trick (FUN_00226550, player config chunk 9): the warp lasts +0x290 = 10
 * frames and stops +0x298 = 120 cm short of the target, at most +0x294 =
 * 500 cm across and +0x29c = 500 cm up or down. It aims at the lock-on point
 * (player+0x3c40), which the test room doesn't have: the dummy's feet plus
 * AIR_TRICK_LOCK_HEIGHT, a Hell Pride's lock point on the PS2 (158.1 over feet at 13). */
#define AIR_TRICK_FRAMES 10.0f
#define AIR_TRICK_SHORT 120.0f
#define AIR_TRICK_MAX_XZ 500.0f
#define AIR_TRICK_MAX_Y 500.0f
#define AIR_TRICK_LOCK_HEIGHT 145.0f
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
#define GUN_FIRST_AIM_FRAMES_SHOTGUN 9.0f /* FUN_0026be70: the shotgun's */

static int gun_rows[5][6][5];      /* motion indices: idle, aim, shot, legs, legs aiming */

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
    float flag_g;          /* at flag 1 only the gravity changes to this (Aerial Rave: -1.5), 0 = no */
    float stop_below;      /* forward speed under this stops (FUN_002e3f60; Aerial Rave: 2) */
    int hover;             /* the air velocities wait, no movement or gravity, until flag 1 or the end (Nevan 6:34) */
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
    /* 3:16 is the first level's finish; the second level ends in 3:42. */
    [MOVE_STINGER_THRUST] = {S("3:42"), "STINGER", MOVE_NONE, 0, 0, 0, 280, HIT_HEAVY},
    [MOVE_HIGH_TIME] = {S("3:11"), "HIGH TIME", MOVE_NONE, 0, 0, 0, 230, HIT_LAUNCH},
    /* FUN_00265950: at its flag, 30 cm/frame up losing 1.35 (HD Collection traces). */
    [MOVE_HIGH_TIME_RISE] = {S("3:13"), "HIGH TIME", MOVE_NONE, 0, 0, 0, 230, HIT_LAUNCH, .flag_air = 1, .flag_v = {0.0f, 0.0f, 30.0f, -1.35f}},
    /* FUN_00264b90: vy and gravity from Rebellion's config (pl000 chunk 11, player+0x2844) +0x60/+0x64 at the
     * start, then at the flip (script window +0x24d5 = 1) +0x74/+0x78, capped at -70 (FUN_002e2560). That's
     * the variant under 500 cm above the floor (+0x6c); 3:40 / 3:41 with +0x7c.. / +0x84.. above aren't in. */
    [MOVE_HELM_BREAKER] = {S("3:10"), "HELM BREAKER", MOVE_NONE, 0, 0, 0, 260, HIT_SLAM,
                           .air = 1, .air_v = {0.0f, 0.0f, -2.0f, -0.5f}, .flag_air = 1, .flag_v = {0.0f, 0.0f, -27.5f, -2.5f}},
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
    /* Aerial Rave sinks slowly, each strike starting the sink again (HD Collection traces). */
    /* FUN_00268270 / 8400 / 8590 / 8720: speed, its change, vy and gravity from Rebellion's config (pl000 chunk 11)
     * +0xe0 / +0xf0 / +0x100 / +0x110; gravity -1.5 from the flip on (+0x24d5 = 1); speed under 2 stops. */
    [MOVE_SM_RAVE_1] = {S("13:7"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR, .air = 1, .air_v = {5.0f, -1.0f, -1.3f, -0.14f}, .flag_g = -1.5f, .stop_below = 2.0f},
    [MOVE_SM_RAVE_2] = {S("13:8"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR, .air = 1, .air_v = {5.0f, -1.0f, -1.2f, -0.14f}, .flag_g = -1.5f, .stop_below = 2.0f},
    [MOVE_SM_RAVE_3] = {S("13:9"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_AIR, .air = 1, .air_v = {5.0f, -1.0f, -1.2f, -0.14f}, .flag_g = -1.5f, .stop_below = 2.0f},
    [MOVE_SM_RAVE_4] = {S("13:10"), "AERIAL RAVE", MOVE_NONE, 0, 0, 0, 240, HIT_SLAM, .air = 1, .air_v = {5.0f, -1.0f, -1.2f, -0.14f}, .flag_g = -1.5f, .stop_below = 2.0f},
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
    /* At the script's flag it stops being root motion: forward at 10 cm/frame losing 0.38,
     * falling from rest at 0.95 a frame, until the floor (HD Collection traces). */
    [MOVE_TR_SKY_STAR] = {S("23:11"), "SKY STAR", MOVE_NONE, 0, 0, 0, 0, -1, .flag_air = 1, .flag_v = {10.0f, -0.38f, 0.0f, -0.95f}},
    [MOVE_TR_AIR_TRICK] = {S("23:15"), "AIR TRICK", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_AIR_TRICK_AIR] = {S("23:16"), "AIR TRICK", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_TR_WALL_HIKE] = {S("23:5"), "WALL HIKE", MOVE_TR_WALL_FLIP, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    [MOVE_TR_WALL_FLIP] = {S("23:6"), "WALL HIKE", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    /* FUN_00227fb0 states 4-6: along the wall on the motion's root (PCSX2: 4.1 cm/frame up for 43 frames), then
     * the kick motion, and at its flag (the arc moving 12 frames in on PCSX2) row 10 straight away from the wall. */
    [MOVE_TR_WALL_RUN_L] = {H("s43-07"), "WALL HIKE", MOVE_TR_WALL_KICK_L, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    [MOVE_TR_WALL_KICK_L] = {H("s43-08"), "WALL HIKE", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    [MOVE_TR_WALL_RUN_R] = {H("s43-09"), "WALL HIKE", MOVE_TR_WALL_KICK_R, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    [MOVE_TR_WALL_KICK_R] = {H("s43-10"), "WALL HIKE", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 0, 1},
    /* Hand-made (Air Hike's state and the Royalguard block states are not
     * decoded yet; these play stand-in motions). */
    /* The block states' scripts (FUN_00224850 24:0, FUN_00225a00 24:4), not yet their logic. */
    /* HD Collection traces: 24:0 for 70 frames, then the held pose, and 24:31 on release. */
    [MOVE_RG_BLOCK] = {S("24:0"), "ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_RG_HOLD] = {S("24:30"), "ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 0, 0, 1},
    [MOVE_RG_END] = {S("24:31"), "ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1},
    /* physics: the jump's arc carries on under it (PS2: 30 frames in the air in all, as a plain jump). */
    [MOVE_RG_AIR_BLOCK] = {S("24:5"), "AIR ROYAL BLOCK", MOVE_NONE, 0, 0, 0, 0, -1, 1, 0, 1},
    [MOVE_TAUNT] = {S("2:0"), "TAUNT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_DEVIL_ON] = {H("2-50"), "DEVIL TRIGGER", MOVE_NONE, 0, 0, 0, 0, -1},
    /* Ground motions seen on the HD Collection (state 44). Their roots say which way each throws
     * him: 3-00 back 72 cm, 3-02 back 201, while 3-01 and 3-03 go forward, for hits from behind.
     * He always turns to face the hit here, so these two. Which hit picks which is an estimate,
     * and so is the air one: 1:4 was seen once when Dante was knocked away in the air. */
    [MOVE_HIT] = {H("3-00"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_HIT_HEAVY] = {H("3-02"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_HIT_AIR] = {H("3-04"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1, .air = 1, .air_v = {0.0f, 0.0f, 0.0f, -1.5f}},
    [MOVE_HIT_ALT] = {H("3-15"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_HIT_BACK] = {H("3-01"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_HIT_HEAVY_BACK] = {H("3-03"), "HIT", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_DOWN] = {H("3-04"), "DOWN", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_DOWN_FAR] = {H("3-05"), "DOWN", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_GET_UP] = {H("3-07"), "GET UP", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_GET_UP_FAR] = {H("3-09"), "GET UP", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_RECOVER] = {H("3-06"), "RECOVER", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_POP] = {H("3-13"), "DOWN", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_POP_LAND] = {H("3-14"), "GET UP", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_QUICKSILVER] = {S("25:0"), "QUICKSILVER", MOVE_NONE, 0, 0, 0, 0, -1},
    [MOVE_DOPPELGANGER] = {H("2-58"), "DOPPELGANGER", MOVE_NONE, 0, 0, 0, 0, -1},
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
            gun_rows[g][r][4] = motion_find(row->aim_legs);
            for (m = 0; m < 5; ++m) if (gun_rows[g][r][m] < 0) return 0;
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

const WeaponPlace *combat_weapon_place(const Combat *c, const Player *player, int weapon, int second) {
    int state = 1;   /* sheathed */
    if (c->move != MOVE_NONE && (c->script_bank == 3 + weapon || c->script_bank == 13 + weapon)) {
        state = c->place_state;
    } else if (weapon >= 5 && weapon == player->gun_weapon && c->stance_active) {
        state = 40;  /* the gun stance scripts' state, in hand */
    } else if (weapon == 0 && c->sword_out > 0.0f && player->speed < 1.0f) {
        state = 2;   /* Rebellion's ready stance */
    }
    if (state < 0 || state >= WEAPON_PLACE_STATES) state = 1;
    if (second) return weapon == 2 ? &rudra_places[state] : &ivory_places[state];
    return &weapon_places[weapon][state];
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
    /* Still in the air: the fall carries on from the move's vertical speed (FUN_0021df00 is handed +0x64). */
    if (c->air && !player->on_ground) player->vy = c->air_v[2] * MOTION_FPS;
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
    while (c->next_event < sd->count && sd->events[c->next_event].frame + SCRIPT_LAG < c->frame) ++c->next_event;
}

/* Starts a move. A new action resets the gates (each script opens its own);
 * a continuation keeps the action. */
static void start_move(Combat *c, MoveId move, int action) {
    const Motion *m;
    /* A charge phase with the button already up goes straight on to its first tier: a tap
     * must not show the charge's pose for a frame. */
    if (moves[move].next_on == NEXT_CHARGE && moves[move].tiers[0] != MOVE_NONE) {
        int down = moves[move].btn == BTN_TRI ? c->held_tri : moves[move].btn == BTN_SQ ? c->held_sq : c->held_cir;
        if (!down) move = moves[move].tiers[0];
    }
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
    c->script_bank = c->script >= 0 ? atoi(script_data[c->script].key) : -1;
    c->place_state = 1;
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
    c->hover_done = 0;
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
    /* A press counts on its own frame only: the patterns' bits (+0x4a40, FUN_002532d0) and the plain presses
     * (+0x4a14, FUN_00245f10) are this frame's; a gate that buffers (1) keeps it as pending instead. PS2: a TRI
     * 3 frames before 13:9's gate 4 starts buffering gives no 4th Aerial Rave strike. */
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
static int stick_yaw(const Player *player, const PlayerInput *input, float *yaw);

/* FUN_0024c0b0: a Wall Hike needs a wall that goes on up: four rays from 100 cm above his feet,
 * 50 apart, all hitting it within reach and close enough to head-on (config +0xe0, +0xe8). */
#define WALL_HIKE_AIR_DEG 25.000000f
#define WALL_KICK_FLAG 12.0f   /* frames into 23:8 / 23:10 to the kick's first step (PCSX2: 23:10 at 100, row 10 moving at 112) */
static int wall_ahead(const Player *player, float yaw, float degrees) {
    float d[3] = {sinf(yaw), 0.0f, cosf(yaw)}, side[3] = {cosf(yaw), 0.0f, -sinf(yaw)};
    float limit = cosf(degrees * 0.0174533f);
    int i, k;
    if (!stage_loaded()) return 0;
    for (i = 0; i < 4; ++i) {
        float len = i == 0 ? 60.0f : 100.0f, t[2], along, dot;
        for (k = 0; k < 2; ++k) {
            float off = k ? 10.0f : -10.0f;
            float a[3] = {player->x + side[0] * off, player->y + 100.0f + 50.0f * (float)i, player->z + side[2] * off};
            float b[3] = {a[0] + d[0] * len, a[1], a[2] + d[2] * len};
            if (!stage_raycast(a, b, &t[k])) return 0;
        }
        /* The wall's slant across his path, from how far the two rays went. */
        along = (t[1] - t[0]) * len;
        dot = 20.0f / sqrtf(along * along + 400.0f);
        if (dot < limit) return 0;
    }
    return 1;
}

/* FUN_0024c280: the run along a wall. A ray along the stick from 100 cm up finds a wall within 100 cm that stands
 * near upright, the stick no more than 75 degrees off head-on (config +0xec); he turns along it, the side the stick
 * leans to: facing into the wall + 90 degrees is +0x3c6d 1 (23:9), - 90 is 0 (23:7). The ray pair's spread gives
 * the wall's direction. FUN_0024c570 then wants the wall to go on 300 cm the way he'll run: four rays at the wall
 * from 75, 150, 225 and 300 cm along it, all hitting (the loop of 4 over 300 / 4 and the raycasts are read from its
 * MIPS; the rays' length, 120 cm, from the constant beside them, is an inference). */
#define WALL_RUN_DEG 75.0f
static float wrap_pi(float a);
static int wall_side(const Player *player, float yaw, int *side, float *face) {
    float d[3] = {sinf(yaw), 0.0f, cosf(yaw)}, sd[3] = {cosf(yaw), 0.0f, -sinf(yaw)}, t[2], p[2][3], tx, tz, nx, nz, l, into;
    int k;
    if (!stage_loaded()) return 0;
    for (k = 0; k < 2; ++k) {
        float off = k ? 10.0f : -10.0f;
        float a[3] = {player->x + sd[0] * off, player->y + 100.0f, player->z + sd[2] * off};
        float b[3] = {a[0] + d[0] * 100.0f, a[1], a[2] + d[2] * 100.0f};
        if (!stage_raycast(a, b, &t[k])) return 0;
        p[k][0] = a[0] + d[0] * 100.0f * t[k]; p[k][2] = a[2] + d[2] * 100.0f * t[k];
    }
    tx = p[1][0] - p[0][0]; tz = p[1][2] - p[0][2];
    l = sqrtf(tx * tx + tz * tz);
    if (l < 1e-3f) return 0;
    nx = -tz / l; nz = tx / l;                       /* the wall's normal, made to face him */
    if (nx * d[0] + nz * d[2] > 0.0f) { nx = -nx; nz = -nz; }
    if (nx * d[0] + nz * d[2] > -cosf(WALL_RUN_DEG * 0.0174533f)) return 0;
    into = atan2f(-nx, -nz);
    if (wrap_pi(into - yaw) < 0.0f) { *side = 1; *face = wrap_pi(into + (float)M_PI * 0.5f); }
    else { *side = 0; *face = wrap_pi(into - (float)M_PI * 0.5f); }
    for (k = 1; k <= 4; ++k) {
        float hit, along = 75.0f * (float)k;
        float a[3] = {player->x + sinf(*face) * along, player->y + 100.0f, player->z + cosf(*face) * along};
        float b[3] = {a[0] + sinf(into) * 120.0f, a[1], a[2] + cosf(into) * 120.0f};
        if (!stage_raycast(a, b, &hit)) return 0;
    }
    return 1;
}

static int extra_check(const CommandData *cmd, const Player *player, const PlayerInput *input) {
    float h;
    if (cmd->check == 2) {
        float yaw = player->yaw;
        /* On the ground it's a dash at every real wall tried on the HD Collection, though the
         * code has a ground branch (+0xe0): something else must stop it there. */
        if (player->on_ground || !stick_pushed(input)) return 0;
        stick_yaw(player, input, &yaw);
        if (wall_ahead(player, yaw, WALL_HIKE_AIR_DEG)) return 1;
        {
            int side;
            float face;
            return wall_side(player, yaw, &side, &face);   /* FUN_0024b510 case 2: straight up first */
        }
    }
    if (cmd->check == 8) return 1;  /* FUN_0024b510 case 8: only Royal Release 194 needs more (the gauge) */
    h = command_min_height(cmd->check);
    return h >= 0.0f && height_above_floor(player) > h;
}

/* The next wall motion before this one has ended: they set his height from their root (base + root y), and the
 * next one's root starts where this one's would have finished, so the base is moved to keep him where he is. */
static void start_move(Combat *c, MoveId move, int action);
static void wall_next_early(Combat *c, Player *player, MoveId next) {
    float root[3];
    start_move(c, next, 0);
    motion_root(motion_get(c->motion), c->frame, root);
    c->base_y = player->y - (root[1] > 0.0f ? root[1] : 0.0f);
}

static int is_trickster(MoveId move) { return move >= MOVE_TR_DASH && move <= MOVE_TR_WALL_KICK_R; }

/* The command walker (FUN_002458f0 / FUN_00245f10) for Rebellion,
 * Swordmaster and Trickster: returns the action to start, or 0. */
static int walk_commands(Combat *c, const Player *player, const PlayerInput *input, unsigned int inputs) {
    const CommandData *cur = c->move != MOVE_NONE ? command_for(c->action) : NULL;
    int posture = player->on_ground ? POSTURE_GROUND : POSTURE_AIR, i;
    for (i = 0; i < COMMAND_DATA_COUNT; ++i) {
        const CommandData *cmd = &command_data[i];
        int pressed = (inputs >> cmd->input) & 1u, ok;
        if (!(cmd->posture & posture)) continue;
        if (cmd->style != 0xFF && cmd->style != game_style(player->style)) continue;
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
            /* No pending press here: on the PS2 a second dash pressed while gate 8 is only buffering (motion
             * frames 20-24 of 23:24) is dropped, not kept for when it opens; only one pressed within the last
             * frames counts (build/hdref/sweep_dash.py). */
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
    while (c->next_event < sd->count && sd->events[c->next_event].frame + SCRIPT_LAG <= c->frame) {
        const ScriptEvent *e = &sd->events[c->next_event++];
        if (e->kind == EV_GATE) c->gates[e->a & 0x0F] = e->b;
        else if (e->kind == EV_FLAG) c->flag = e->a;
        else if (e->kind == EV_PLACE) c->place_state = e->a;
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

void (*combat_hit_fn)(const Player *player, float reach, int kind);

static void deal_hit(Combat *c, const Player *player) {
    const MoveDef *d = &moves[c->move];
    int i;
    if (d->hit_kind < 0) return;
    if (combat_hit_fn) combat_hit_fn(player, d->reach, d->hit_kind);
    enemy_deflect(player->x, player->y, player->z, player->yaw, d->reach);
    enemy_set_hit_weapon(player->melee_weapon);
    for (i = 0; i < ENEMY_MAX; ++i) {
        float e[3], dx, dz, dist, facing, dy;
        enemy_select(i);
        if (!enemy_active()) continue;
        if(enemy_kind()==ENEMY_LEVIATHAN) {
            float a[3]={player->x,player->y+150,player->z};
            float b[3]={a[0]+sinf(player->yaw)*d->reach,a[1],a[2]+cosf(player->yaw)*d->reach};
            if(leviathan_boss_segment_hit(a,b)>=0) {
                enemy_hit((HitKind)d->hit_kind,player->x,player->z);
                ++c->hits_dealt;
            }
            continue;
        }
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
    legs = gun_rows[gun_index(player)][row][has_aim ? 4 : 3];
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
        /* Every shot is the row's level shot (column 3). The HD Collection's
         * 8:7 / 8:8 are the standing row's up and down shots (columns 1 and 5),
         * the main track FUN_00222e00 blends over the level one, not two guns. */
        if (d_is_alternating(c)) c->frame = (c->shot_count & 1) ? 0.0f : 15.0f;
        else c->frame = motion_get(c->motion)->loop_start;
        c->next_event = 0;
        c->since_fire = 0.0f;
        /* FUN_002688c0 (Ebony & Ivory) / FUN_0026be70 (shotgun): the first shot turns to the target before
         * firing; from outside the stance (state not 10-14) that takes at least 10 / 9 frames, target or
         * not. The shot's own length counts from when it fires (PS2: a shot from a stand runs 22 frames,
         * one in the stance 13). During the wait Ebony & Ivory hold the shot's first pose (FUN_002691b0
         * requests the shot with the wait as its blend); the shotgun raises it with the aim, 9:4 on the
         * PS2 (FUN_0026c350 column 0), then plays the shot. */
        c->shot_delay = 0.0f;
        if (c->shot_count == 1) {
            float least = gun_index(player) == 1 ? GUN_FIRST_AIM_FRAMES_SHOTGUN : GUN_FIRST_AIM_FRAMES;
            if (c->aim_target)
                c->shot_delay = fabsf(wrap_pi(atan2f(player->target_x - player->x, player->target_z - player->z) - player->yaw)) / GUN_AIM_TURN;
            if (!c->was_stance && c->shot_delay < least) c->shot_delay = least;
            if (c->shot_delay > 0.0f && c->shots_pending > 0) {
                --c->shots_pending;
                c->delayed_shot = 1;
                if (gun_index(player) == 1) { c->motion = row[1]; c->frame = motion_get(c->motion)->loop_start; }
            }
        }
    } else if (c->delayed_shot) {   /* counted down from the frame after the press, as the handler's phase 0 */
        c->shot_delay -= dt * MOTION_FPS;
        if (c->shot_delay <= 0.0f) {
            const int *row = gun_rows[gun_index(player)][c->gun_row];
            c->shots_pending += c->delayed_shot;
            c->delayed_shot = 0;
            c->since_fire = 0.0f;
            if (c->motion != row[2]) { c->motion = row[2]; c->frame = motion_get(c->motion)->loop_start; c->next_event = 0; }
        }
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
            else sector = d > 0.0f ? 2 : 3;   /* PS2: the stick toward yaw +90 is the left dash (23:2 / 23:26, motion 02) */
        }
        /* FUN_00225c90: the script is table 0x5911d0 {0,1,2,3, 24,25,26,27}[4 * (style level >= Lv3) + way];
         * at Lv3 a forward dash begun while a forward dash plays (motion 0x1700) is 23:17 instead. The style
         * level is player+0x3c75 (0 = Lv1); the full moveset is Lv3. */
        base = TRICKSTER_LEVEL >= 2 ? MOVE_TR_DASH2 : MOVE_TR_DASH;
        if (sector == 0 && TRICKSTER_LEVEL == 2 && (prev == MOVE_TR_DASH || prev == MOVE_TR_DASH2 || prev == MOVE_TR_DASH_CHAIN))
            base = MOVE_TR_DASH_CHAIN;
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
        int side;
        float face;
        if (!wall_ahead(player, sy, WALL_HIKE_AIR_DEG) && wall_side(player, sy, &side, &face)) {
            player->yaw = face;   /* along the wall */
            start_move(c, side ? MOVE_TR_WALL_RUN_R : MOVE_TR_WALL_RUN_L, 0);
        } else {
            if (pushed) player->yaw = sy;   /* into the wall */
            start_move(c, MOVE_TR_WALL_HIKE, 0);
        }
        c->base_y = player->y;
    }
    c->action = 0;   /* states, not actions: player+0x2998 = 0 */
}

/* Air Trick, after the vanish (FUN_00226550 states 1-2): fly toward the
 * lock-on point for AIR_TRICK_FRAMES, then drop (state 3). Returns 1 while
 * the move goes on. */
static int air_trick_update(Combat *c, Player *player, float dt) {
    float wait = c->move == MOVE_TR_AIR_TRICK_AIR ? AIR_TRICK_AIR_WAIT : motion_get(c->motion)->end_frame;
    if (c->air) return 1;   /* falling: the velocities in combat_update move him */
    if (c->warp_time <= 0.0f) {
        float dx, dz, dist, along, dy;
        if (c->frame < wait + SCRIPT_LAG) return 1;   /* the motion's end is seen a frame late, as everywhere */
        dx = player->target_x - player->x; dz = player->target_z - player->z;
        dist = sqrtf(dx * dx + dz * dz);
        dy = player->has_target ? player->target_y + AIR_TRICK_LOCK_HEIGHT - player->y : 0.0f;
        /* FUN_00226550 state 1: across = the 3D distance to the lock point (FUN_001324b0) less +0x298, at most
         * +0x294; up = the lock point's height difference, at most +0x29c either way. */
        along = player->has_target ? sqrtf(dist * dist + dy * dy) - AIR_TRICK_SHORT : 0.0f;
        if (along > AIR_TRICK_MAX_XZ) along = AIR_TRICK_MAX_XZ;
        if (along < 0.0f) along = 0.0f;
        if (dy > AIR_TRICK_MAX_Y) dy = AIR_TRICK_MAX_Y;
        if (dy < -AIR_TRICK_MAX_Y) dy = -AIR_TRICK_MAX_Y;
        c->warp_step[0] = dist > 1.0f ? dx / dist * along : 0.0f;
        c->warp_step[1] = dy;
        c->warp_step[2] = dist > 1.0f ? dz / dist * along : 0.0f;
        c->warp_time = AIR_TRICK_FRAMES / MOTION_FPS;
        player->on_ground = 0;
    }
    {
        /* Whole steps until the timer runs out, so Turbo overshoots a little, as on the real game. */
        float step = dt / (AIR_TRICK_FRAMES / MOTION_FPS);
        player->x += c->warp_step[0] * step;
        player->y += c->warp_step[1] * step;
        player->z += c->warp_step[2] * step;
        /* State 2: within +0x298 of the lock point the across speed stops (FUN_002e3f60). */
        if (player->has_target) {
            float lx = player->target_x - player->x, ly = player->target_y + AIR_TRICK_LOCK_HEIGHT - player->y, lz = player->target_z - player->z;
            if (lx * lx + ly * ly + lz * lz < AIR_TRICK_SHORT * AIR_TRICK_SHORT) c->warp_step[0] = c->warp_step[2] = 0.0f;
        }
        c->warp_time -= dt;
        if (c->warp_time <= 1.0e-4f) {   /* ten 60th steps exactly, as the PS2's +0x2870 countdown (not eleven by rounding) */
            /* State 9, HD Collection traces: 4 cm/frame forward and 8 up, losing 0.07 and 1. */
            static const float fall[4] = {4.0f, -0.07f, 8.0f, -1.0f};
            player->vy = 0.0f;
            memcpy(c->air_v, fall, sizeof(fall));
            c->air = 1;
        }
    }
    return 1;
}

/* Moves whose data isn't decoded: Royalguard, lock-on rolls. */
static void start_handmade(Combat *c, Player *player, const PlayerInput *input) {
    int guard = input->style_action && player->style == STYLE_ROYALGUARD;
    if (input->taunt && player->on_ground) {
        start_move(c, MOVE_TAUNT, 0);
    } else if (input->devil && !player->devil && player->on_ground) {
        start_move(c, MOVE_DEVIL_ON, 0);
    } else if (input->style_action && input->lock_on && player->on_ground && player->style == STYLE_QUICKSILVER) {
        /* R1 + CIR. Only turning it on has a motion (HD Collection). */
        player->quicksilver = !player->quicksilver;
        if (player->quicksilver) start_move(c, MOVE_QUICKSILVER, 0);
    } else if (input->style_action && input->lock_on && player->on_ground && player->style == STYLE_DOPPELGANGER) {
        player->doppelganger = !player->doppelganger;
        if (player->doppelganger) start_move(c, MOVE_DOPPELGANGER, 0);
    } else if (input->jump && player->locked_on && player->on_ground) {
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
            /* Forward and back are jumps (0:5, 0:9), on the jump's arc: player.c. */
            if (fwd < -0.6f || fwd > 0.6f) { player->flip_request = fwd > 0.0f ? 1 : -1; player->yaw = ref; }
            else if (side > 0.5f) start_move(c, MOVE_ROLL_RIGHT, 0);
            else if (side < -0.5f) start_move(c, MOVE_ROLL_LEFT, 0);
            if (c->move != MOVE_NONE) { c->base_y = player->y; player->yaw = ref; }
        }
    } else if (guard) {
        start_move(c, player->on_ground ? MOVE_RG_BLOCK : MOVE_RG_AIR_BLOCK, 0);
    }
    if (c->move != MOVE_NONE && moves[c->move].start_vy > 0.0f) {
        player->vy = moves[c->move].start_vy;
        player->on_ground = 0;
    }
}

/* FUN_00264fe0: Stinger's dash (3:15) from Rebellion's config (chunk 11): level 1 (action 7) +0x1c / +0x20,
 * 8 frames over 200 cm; level 2 (action 8) +0x28 / +0x2c, 16 frames over 560 cm. It stops early within
 * +0x34 = 130 cm of the target (one less than 200 cm above or below). */
static float stinger_dash(const Combat *c, int frames) {
    if (c->action == 7) return frames ? 8.0f : 200.0f;
    return frames ? 16.0f : 560.0f;
}

static int stinger_dash_over(const Combat *c, const Player *player) {
    /* +0x2870 counts down and the dash ends once it's below 0. */
    if (c->phase_time * MOTION_FPS > stinger_dash(c, 1)) return 1;
    if (player->has_target && fabsf(player->y - player->target_y) < 200.0f) {
        float dx = player->target_x - player->x, dz = player->target_z - player->z;
        if (dx * dx + dz * dz < 130.0f * 130.0f) return 1;
    }
    return 0;
}

/* ---- Knockdown (FUN_002e9a40), getting up (FUN_002ea540) and the quick recovery (FUN_002ea640) ----------
 * Launch by the attack's kind from the player config (chunk 9, +0x283c): vertical speed and gravity per frame
 * (FUN_002e3ef0) and the speed away from the attacker (FUN_002e3f50): kinds 0-2 +0x0 / +0x4 / +0x8, 3 +0xc..,
 * 4 +0x18.., others +0x24..; on landing that speed is scaled by +0x38 (+0x20c.. for 3-5) and he slides on
 * (FUN_002e35c0). Script 1:4 (kinds 0-3) / 1:5 (4 and up), window 0 byte 4 (+0x24d5) at motion frames: 1 holds
 * the pose until the floor (12 / 41), 2 ends the recovery press window (13 / 43), 3 recovers if a face button
 * was pressed before (or in Trickster; 20 / 50, script 1:6), 4 gets up (60 / 80, script 1:7 / 1:9). */
static const float knock_launch[4][3] = {{10.4f, -1.2f, 10.0f}, {12.0f, -1.2f, 15.0f}, {10.4f, -0.8f, 6.4f}, {12.4f, -0.8f, 12.0f}};
static const float knock_flags[2][4] = {{12.0f, 13.0f, 20.0f, 60.0f}, {41.0f, 43.0f, 50.0f, 80.0f}};
#define KNOCK_LAND_SPEED 0.3f
/* 1:7 / 1:9 / 1:6 open the cancel gates at these frames (window 1: gates 0, 5, 9, 10). */
#define GET_UP_FREE 65.0f
#define GET_UP_FAR_FREE 60.0f
#define RECOVER_FREE 70.0f

/* Kind 7 (FUN_002e9f50): he hangs config +0x3c = 40 frames, then flies as kinds 0-2; a press between flags 2 and
 * 3 recovers at flag 3, and flag 4 always ends in the quick recovery. Kind 6 (FUN_002ea390): straight up at
 * config +0x30 / +0x34 (23.5, -1.2) in 1:13, landing in 1:14. */
#define KNOCK_HANG_FRAMES 40.0f
#define POP_VY 23.5f
#define POP_G (-1.2f)
#define POP_LAND_FREE 70.0f

static void start_knockdown(Combat *c, Player *player, int kind) {
    int set = kind <= 2 || kind == 7 ? 0 : kind == 3 ? 1 : kind == 4 ? 2 : 3;
    c->kd_far = set >= 2;
    c->kd_pop = kind == 6;
    c->kd_launch = kind == 7;
    c->kd_hang = c->kd_launch ? KNOCK_HANG_FRAMES : 0.0f;
    start_move(c, c->kd_pop ? MOVE_POP : c->kd_far ? MOVE_DOWN_FAR : MOVE_DOWN, 0);
    c->kd_vy = c->kd_pop ? POP_VY : knock_launch[set][0];
    c->kd_g = c->kd_pop ? POP_G : knock_launch[set][1];
    c->kd_speed = c->kd_pop ? 0.0f : knock_launch[set][2];
    c->kd_landed = 0;
    c->kd_press = !c->kd_launch && player->style == STYLE_TRICKSTER;
    c->air_accum = 0.0f;
    c->dead_time = 0.0f;
    player->on_ground = 0;
    player->attacking = 1;
}

/* Returns 1 while it owns Dante this frame. */
static int knockdown_update(Combat *c, Player *player, const PlayerInput *input, float dt) {
    const float *fl = knock_flags[c->kd_far];
    const Motion *m = motion_get(c->motion);
    float back = player->yaw + (float)M_PI, f = dt * MOTION_FPS;
    int face = input->jump || input->melee || input->gun || input->style_action;
    float free_at = c->move == MOVE_GET_UP ? GET_UP_FREE : c->move == MOVE_GET_UP_FAR ? GET_UP_FAR_FREE
                  : c->move == MOVE_POP_LAND ? POP_LAND_FREE : RECOVER_FREE;
    if (c->move == MOVE_GET_UP || c->move == MOVE_GET_UP_FAR || c->move == MOVE_RECOVER || c->move == MOVE_POP_LAND) {
        /* From the gates' frame a move or the stick takes over; the rest plays out as a normal move. */
        if (c->frame >= free_at && (face || stick_pushed(input))) { end_move(c, player); return 0; }
        return 0;
    }
    if (c->move != MOVE_DOWN && c->move != MOVE_DOWN_FAR && c->move != MOVE_POP) return 0;
    player->attacking = 1;
    c->phase_time += dt;
    if (c->kd_hang > 0.0f) {   /* kind 7: held where he was hit */
        c->kd_hang -= f;
        c->frame = m->loop_start;
        return 1;
    }
    if (!c->kd_landed) {
        float floor_y = 0.0f, start_y = player->y, k = player_step_scale;
        c->frame += f;
        if (c->kd_pop) { if (c->frame > m->end_frame) c->frame = m->end_frame; }
        else if (c->frame > fl[0]) c->frame = fl[0];
        if (face && !c->kd_launch) c->kd_press = 1;
        player->on_ground = 0;
        c->air_accum += f / k;
        if (player_floor_fn && !player_floor_fn(player->x, start_y, player->z, 45.0f, &floor_y)) floor_y = -1.0e9f;
        while (c->air_accum >= 1.0f) {   /* FUN_002e3a60, one game step at a time, unless it meets the floor */
            if (c->kd_vy < 0.0f && player->y + c->kd_vy * k <= floor_y) { player->y = floor_y; c->air_accum = 0.0f; break; }
            c->air_accum -= 1.0f;
            player->x += sinf(back) * c->kd_speed * k;
            player->z += cosf(back) * c->kd_speed * k;
            player->y += c->kd_vy * k;
            c->kd_vy += c->kd_g * k;
            if (c->kd_vy < -70.0f) c->kd_vy = -70.0f;
        }
        if (player_floor_fn && !player_floor_fn(player->x, start_y, player->z, 45.0f, &floor_y)) floor_y = -1.0e9f;
        if (player->y < -5000.0f) { end_move(c, player); return 1; }
        if (c->kd_vy <= 0.0f && player->y <= floor_y) {
            player->y = floor_y;
            player->vy = 0.0f;
            player->on_ground = 1;
            c->kd_landed = 1;
            c->kd_speed *= KNOCK_LAND_SPEED;
            if (c->kd_pop && player->health > 0.0f) start_move(c, MOVE_POP_LAND, 0);
        }
        return 1;
    }
    if (c->kd_pop) {   /* dead from a pop: lie in its last pose */
        c->dead_time += f;
        return 1;
    }
    {
        float was = c->frame;
        player->on_ground = 1;
        c->frame += f;
        if (c->frame < fl[3]) {   /* the slide along the floor (FUN_002e35c0) */
            player->x += sinf(back) * c->kd_speed * f;
            player->z += cosf(back) * c->kd_speed * f;
        }
        if (player->health <= 0.0f) {
            /* Dead: no recovery and no getting up; he lies at the end of the motion. */
            if (c->frame > m->end_frame) c->frame = m->end_frame;
            c->dead_time += f;
            return 1;
        }
        if (c->kd_launch) {
            /* +0x2858 / +0x285c: only a press while the window is open (flags 2-3) counts; flag 4 recovers anyway. */
            if (c->frame >= fl[1] && c->frame < fl[2] && (face || player->style == STYLE_TRICKSTER)) c->kd_press = 1;
            if ((c->frame >= fl[2] && c->kd_press) || c->frame >= fl[3]) { start_move(c, MOVE_RECOVER, 0); return 1; }
        }
        if (was < fl[2] && c->frame >= fl[2] && c->kd_press) { start_move(c, MOVE_RECOVER, 0); return 1; }
        if (c->frame >= fl[3] || c->frame >= m->end_frame) {
            start_move(c, c->kd_far ? MOVE_GET_UP_FAR : MOVE_GET_UP, 0);
            return 1;
        }
    }
    return 1;
}

/* Twosome Time with R1 goes the stick's way against his facing (FUN_0026b0d0, table 0x595550): back 18:2,
 * left 18:3, right 18:1 (the generated actions 251 / 252 / 250); forward or neutral stays 18:0. */
static MoveId twosome_move(const Player *player, const PlayerInput *input, MoveId first) {
    float sy = player->yaw;
    if (player->locked_on && stick_yaw(player, input, &sy)) {
        float d = wrap_pi(sy - player->yaw);
        if (fabsf(d) >= 2.356f) return action_move(251);
        if (fabsf(d) > 0.785f) return action_move(d > 0.0f ? 252 : 250);
    }
    return first;
}

/* FUN_0026b1d0 / FUN_0026af60: a Twosome Time series picks its set at random (+0x3c6d: 18:0-3, or 18:4-7 from
 * table 0x595564), and its shots alternate the guns, playing frames 0-13 or 15-28; the first is from 15 either
 * way (+0x299f made even for set 0, odd stays for set 1). */
static void twosome_pose(Combat *c, int first) {
    const Motion *m;
    int idx, s;
    if (c->script < 0) return;
    if (first) { c->twosome_set = rand() & 1; c->twosome_shots = 0; }
    idx = atoi(script_data[c->script].key + 3);
    if (c->twosome_set && idx < 4) {
        char key[8];
        snprintf(key, sizeof(key), "18:%d", idx + 4);
        for (s = 0; s < SCRIPT_DATA_COUNT; ++s)
            if (!strcmp(script_data[s].key, key)) {
                int mo = motion_find(script_data[s].motion);
                if (mo >= 0) { c->script = s; c->motion = mo; }
                break;
            }
    }
    m = motion_get(c->motion);
    c->frame = m->loop_start + ((c->twosome_shots++ & 1) ? 0.0f : 15.0f);
    motion_root(m, c->frame, c->last_root);
    seek_events(c);
}

void combat_update(Combat *c, Player *player, const PlayerInput *input, float dt) {
    const Motion *m;
    const MoveDef *d;
    float root[3];
    unsigned int inputs = read_inputs(c, player, input, dt);
    int action, hits;

    c->held_tri = input->melee_held;
    c->held_sq = input->gun_held;
    c->held_cir = input->style_held;

    if (c->fade_weight > 0.0f) c->fade_weight -= dt / FADE_SECONDS;
    if (c->fade_weight < 0.0f) c->fade_weight = 0.0f;
    if (c->sword_out > 0.0f) c->sword_out -= dt;
    if (knockdown_update(c, player, input, dt)) return;

    /* Free cancels: gate 15 hands control back to movement, gate 8 to a jump. */
    if (c->move != MOVE_NONE && c->script >= 0) {
        int stick = input->stick_x * input->stick_x + input->stick_y * input->stick_y > 0.25f;
        /* In the air a jump only cancels if there's something to spend on it. */
        int can_jump = input->jump && (player->on_ground || (player->air_hikes > 0 && player->melee_weapon % 2 == 0) || player->enemy_close);
        if ((c->gates[15] == 2 && (stick || can_jump)) || (c->gates[8] == 2 && can_jump)) {
            end_move(c, player);
            return;
        }
    }

    action = walk_commands(c, player, input, inputs);
    /* One Sky Star an airtime (player+0x3c76). */
    if (action == 181 && player->air_stars <= 0) action = 0;
    if (action >= 180 && action <= 183) {
        if (action == 181) --player->air_stars;
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
            if (action == 134) first = twosome_move(player, input, first);
            {
                int again = c->action == 134, in_stance = c->stance_active;
                start_move(c, first, action);
                if (action == 134) twosome_pose(c, !again);
                /* FUN_0026b1d0: the first Twosome Time waits 10 frames from outside the gun stance, 3 in it,
                 * before it fires; the shot's 13 frames count from there (PS2: 22 frames from a stand). */
                if (action == 134 && !again && c->shots_pending >= moves[first].shot) {
                    c->shots_pending -= moves[first].shot;
                    c->delayed_shot = moves[first].shot;
                    c->shot_delay = in_stance ? 3.0f : GUN_FIRST_AIM_FRAMES;
                }
            }
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
    /* FUN_00224850 marks the release 24:31 interruptible (+0x2898 0x40000): on the PS2 the stick takes over at
     * once (24:31 one frame, then the strafe) and a CIR press blocks again (rg_lfm). */
    if (c->move == MOVE_RG_END && c->phase_time > 0.0f) {
        if (input->style_action && player->style == STYLE_ROYALGUARD && player->on_ground) { start_move(c, MOVE_RG_BLOCK, 0); return; }
        if (stick_pushed(input)) { end_move(c, player); return; }
    }
    if ((c->move == MOVE_RG_BLOCK || c->move == MOVE_RG_HOLD) && !input->style_held) {
        start_move(c, MOVE_RG_END, 0);
        return;
    }
    if (c->move == MOVE_RG_BLOCK && c->phase_time >= 70.0f / MOTION_FPS) {
        start_move(c, MOVE_RG_HOLD, 0);
        return;
    }
    if (d->hold_loop && !input->style_held) { end_move(c, player); return; }
    if (d->script && d->hit_kind >= 0 && c->move <= MOVE_SM_RAVE_4) c->sword_out = SWORD_OUT_SECONDS;
    /* Nevan's guitar stance (action 99) lasts until any input. */
    if (d->next_on == NEXT_STANCE && (stick_pushed(input) || input->jump || input->melee || input->style_action || input->gun)) {
        end_move(c, player);
        return;
    }
    if (d->layered) update_gun_layers(c, player, input, dt);
    else if (c->delayed_shot && c->phase_time > 0.0f) {   /* Twosome Time's wait, from the frame after */
        c->shot_delay -= dt * MOTION_FPS;
        if (c->shot_delay <= 0.0f) { c->shots_pending += c->delayed_shot; c->delayed_shot = 0; }
    }
    m = motion_get(c->motion);
    /* A new script starts counting STARTUP_FRAMES late, as on the PS2: its handler's state 0 requests the motion
     * and the script runs from the frame after (measured: the PS2's gates open two frames later than ours). */
    /* A first shot waiting to fire holds the shot's first pose (Ebony & Ivory, Twosome Time); the shotgun's
     * aim plays on. */
    if (c->phase_time >= STARTUP_FRAMES / MOTION_FPS &&
        !(c->delayed_shot && (!d->layered || c->motion == gun_rows[gun_index(player)][c->gun_row][2])))
        c->frame += dt * MOTION_FPS;
    c->phase_time += dt;

    /* Mash loops (Million Stab, Shredder, Crazy Dance) and held guards replay
     * the motion's loop range; the script's events replay with it. */
    if (m->looping && m->loop_to > m->loop_from && c->frame >= m->loop_to) {
        float since = d->mash == BTN_TRI ? c->since_tri : c->since_cir;
        /* Shredder runs a fixed 60 frames, mashing or not (HD Collection traces). */
        int shredder = c->move == MOVE_SM_SHREDDER;
        if (shredder ? c->phase_time < 60.0f / MOTION_FPS : (d->mash && since < MASH_KEEP_SECONDS) || d->hold_loop || c->move == MOVE_STINGER_HOLD) {
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
    if (d->flag_g != 0.0f && c->flag == 1 && c->air) c->air_v[3] = d->flag_g;
    if (d->hover && (c->flag == 1 || c->frame >= m->end_frame)) c->hover_done = 1;
    /* Beowulf's dive kick (FUN_00267d20): with a target the dive aims at it, the angle clamped to 40-75 deg
     * (config +0x55c / +0x560), speed 45 (+0x558); without one it's 45 deg (the generated flag velocities). */
    if (c->flag == 1 && c->air && c->script >= 0 && !strcmp(script_data[c->script].key, "7:12") && player->has_target) {
        float dx = player->target_x - player->x, dz = player->target_z - player->z;
        float a = atan2f(player->y - player->target_y, sqrtf(dx * dx + dz * dz));
        if (a < 0.698132f) a = 0.698132f;
        if (a > 1.308997f) a = 1.308997f;
        c->air_v[0] = 45.0f * cosf(a); c->air_v[1] = 0.0f; c->air_v[2] = -45.0f * sinf(a); c->air_v[3] = 0.0f;
    }
    if (d->next_on == NEXT_CHARGE) c->charge_frames += dt * MOTION_FPS;
    while (hits-- > 0) deal_hit(c, player);

    /* Ebony & Ivory in the air: every press after 13 frames fires again with a small kick up. */
    if (c->air && c->script >= 0 && !strcmp(script_data[c->script].key, "8:32")) {
        c->since_fire += dt * MOTION_FPS;
        if (input->gun && c->since_fire >= 13.0f) {
            ++c->shots_pending;
            c->since_fire = 0.0f;
            c->air_v[2] = 5.5f;
            c->air_v[3] = -1.2f;
        }
    }
    if (c->move == MOVE_HIGH_TIME && c->frame >= HIGH_TIME_HOLD_FRAME && input->melee_held) {
        start_move(c, MOVE_HIGH_TIME_RISE, 0);
        return;
    }
    {
        /* Moves the real game drives by speed, not by the motion (HD Collection traces). */
        float speed = c->move == MOVE_STINGER_HOLD ? stinger_dash(c, 0) / stinger_dash(c, 1) : 0.0f;
        if (c->script >= 0) {
            const char *key = script_data[c->script].key;
            if (!strcmp(key, "7:31")) speed = 43.0f;       /* Beowulf's Straight */
        }
        player->x += sinf(player->yaw) * speed * dt * MOTION_FPS;
        player->z += cosf(player->yaw) * speed * dt * MOTION_FPS;
    }

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
        /* Once its velocities take over (c->air after a flag) the motion only poses him. */
        int free_root = c->air && (d->flag_air || c->move == MOVE_TR_AIR_TRICK || c->move == MOVE_TR_AIR_TRICK_AIR);
        float dx = free_root ? 0.0f : root[0] - c->last_root[0], dz = free_root ? 0.0f : root[2] - c->last_root[2];
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
        float k = player_step_scale, floor_y = 0.0f, s = sinf(player->yaw), co = cosf(player->yaw);
        float frame_start_y = player->y;
        player->on_ground = 0;
        /* One real game step every 60th of a second, however long this frame was. */
        c->air_accum += dt * MOTION_FPS / k;
        if (d->hover && !c->hover_done) c->air_accum = 0.0f;   /* waiting: he stays where he is */
        /* FUN_00242200 checks the floor before the step (FUN_002e2560): a step that would go through it isn't
         * taken, he lands where he is (PS2 beo_cair: the dive kick lands 34 cm short of a full last step). */
        if (player_floor_fn && !player_floor_fn(player->x, frame_start_y, player->z, 45.0f, &floor_y)) floor_y = -1.0e9f;
        while (c->air_accum >= 1.0f) {
            if (c->air_v[2] < 0.0f && player->y + c->air_v[2] * k <= floor_y) { player->y = floor_y; c->air_accum = 0.0f; break; }
            c->air_accum -= 1.0f;
            player->x += s * c->air_v[0] * k;
            player->z += co * c->air_v[0] * k;
            player->y += c->air_v[2] * k;
            c->air_v[0] += c->air_v[1] * k;
            if (c->air_v[0] < 0.0f) c->air_v[0] = 0.0f;
            if (d->stop_below > 0.0f && c->air_v[0] < d->stop_below) c->air_v[0] = 0.0f;
            c->air_v[2] += c->air_v[3] * k;
            if (c->air_v[2] < -70.0f) c->air_v[2] = -70.0f;   /* FUN_002e2560's fall limit */
        }
        /* From where he started this frame: at 30 FPS a fast fall can pass well below the floor in one. */
        if (player_floor_fn && !player_floor_fn(player->x, frame_start_y, player->z, 45.0f, &floor_y)) floor_y = -1.0e9f;
        if (player->y < -5000.0f) {   /* nothing under him at all: player.c puts him back */
            c->air = 0;
            end_move(c, player);
            return;
        }
        if (c->air_v[2] <= 0.0f && player->y <= floor_y) {
            player->y = floor_y;
            player->vy = 0.0f;
            player->on_ground = 1;
            c->air = 0;
            if (d->next == MOVE_NONE) {
                /* State 8: the jump's landing frames, stopped. */
                int land = motion_find("2-04");
                if (land >= 0) { player->jump_motion = land; player->jump_frame = JUMP_LAND_FRAME; }
                player->land_stop = JUMP_LAND_STOP_FRAMES;
            }
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
        int at_end = c->frame >= m->end_frame + SCRIPT_LAG;
        int go = 0;
        float since = d->btn == BTN_TRI ? c->since_tri : d->btn == BTN_SQ ? c->since_sq : c->since_cir;
        int held = d->btn == BTN_TRI ? input->melee_held : d->btn == BTN_SQ ? input->gun_held : input->style_held;
        switch (d->next_on) {
        case NEXT_FLAG1:
            if (d->next != MOVE_NONE && moves[d->next].next_on == NEXT_CHARGE) {
                /* Into a charge only with its button still held at the flag (FUN_002794d0 state 1: +0x24d5 == 1
                 * and TRI held); a tap plays the whole swing, its root motion with it, then back to neutral. */
                int nb = moves[d->next].btn;
                int nheld = nb == BTN_TRI ? input->melee_held : nb == BTN_SQ ? input->gun_held : input->style_held;
                if (c->flag == 1 && nheld) go = 1;
                else if (at_end) {
                    /* The tap's own follow-on, as after a released charge (FUN_0027a1f0: 7:7 then 7:8). */
                    MoveId tier = moves[d->next].tiers[0];
                    MoveId after = tier != MOVE_NONE ? moves[tier].next : MOVE_NONE;
                    if (after != MOVE_NONE) start_move(c, after, 0);
                    else end_move(c, player);
                    return;
                }
            } else {
                go = c->flag == 1 || at_end;
            }
            break;
        case NEXT_FLAG2: go = c->flag == 2 || at_end; break;
        case NEXT_LAND:
            /* Airborne until the floor (above); hold the motion's last frame. A swing whose gravity turns at
             * its flag turns at its end at the latest (FUN_00276ac0: "end || +0x24d5 == 1"). */
            if (at_end && d->flag_g != 0.0f && c->air) c->air_v[3] = d->flag_g;
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
            if (!c->delayed_shot) c->phase_frames += dt * MOTION_FPS;   /* counted from when it fires */
            /* (Not the press that started this shot: that's the frame count's first frame.) */
            if (input->gun && c->phase_time * MOTION_FPS > 1.5f) c->shot_buffered = 1;
            if (c->shot_buffered && !c->delayed_shot && c->since_fire >= d->refire) { start_move(c, c->move, 0); return; }
            if (c->phase_frames >= d->frames) go = 1;
            break;
        case NEXT_TIMER:
        case NEXT_STANCE:
            if (!c->delayed_shot) c->phase_frames += dt * MOTION_FPS;
            /* FUN_0026b1d0: a CIR press in Twosome Time fires again from 7 frames after the shot (+0x2856),
             * an earlier one waits for then (+0x2858). */
            if (c->action == 134 && player->on_ground && player->style == STYLE_GUNSLINGER) {
                if (input->style_action && c->phase_time * MOTION_FPS > 1.5f) c->shot_buffered = 1;
                if (c->shot_buffered && !c->delayed_shot && c->phase_frames >= 7.0f) {
                    /* A new R1 sector (FUN_0026b0d0) turns first: 10 frames before that shot. */
                    MoveId was = c->move, next = twosome_move(player, input, action_move(134));
                    start_move(c, next, 0);
                    twosome_pose(c, 0);
                    if (next != was) {
                        c->shots_pending -= moves[next].shot;
                        c->delayed_shot = moves[next].shot;
                        c->shot_delay = GUN_FIRST_AIM_FRAMES;
                    }
                    return;
                }
            }
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

    /* The run along a wall (FUN_00227fb0 state 4): FUN_0024c880 looks for the wall beside him each frame, a ray
     * from 100 cm up 150 cm toward it; when it's gone he kicks off at once. The kick (state 5) leaves at the
     * motion's flag: row 10 away from the wall, his facing kept. */
    if (c->move == MOVE_TR_WALL_RUN_L || c->move == MOVE_TR_WALL_RUN_R) {
        float to = player->yaw + (c->move == MOVE_TR_WALL_RUN_R ? -0.5f : 0.5f) * (float)M_PI, hit;
        float a[3] = {player->x, player->y + 100.0f, player->z};
        float b[3] = {a[0] + sinf(to) * 150.0f, a[1], a[2] + cosf(to) * 150.0f};
        if (stage_loaded() && !stage_raycast(a, b, &hit)) { wall_next_early(c, player, d->next); return; }
    }
    /* Straight up (state 1), the same check ahead of him (FUN_0024c880 with +0x3c6d 2): once the wall has ended
     * under 100 cm up from his feet he goes to the flip at once, not on up past its top. The game also wants the
     * hit within 20 degrees of head-on and 300 cm clear overhead; the reach, 100 cm, is the straight check's. */
    if (c->move == MOVE_TR_WALL_HIKE && c->phase_time * MOTION_FPS > 2.0f) {
        float a[3] = {player->x, player->y + 100.0f, player->z}, hit;
        float b[3] = {a[0] + sinf(player->yaw) * 100.0f, a[1], a[2] + cosf(player->yaw) * 100.0f};
        if (stage_loaded() && !stage_raycast(a, b, &hit)) { wall_next_early(c, player, d->next); return; }
    }
    if ((c->move == MOVE_TR_WALL_KICK_L || c->move == MOVE_TR_WALL_KICK_R) && c->phase_time * MOTION_FPS >= WALL_KICK_FLAG) {
        float away = player->yaw + (c->move == MOVE_TR_WALL_KICK_R ? 0.5f : -0.5f) * (float)M_PI;
        float at[3] = {player->x + sinf(away) * 5.0f, player->y, player->z + cosf(away) * 5.0f};
        end_move(c, player);
        player_wall_kick(player, away);
        nfx_spawn_e(NFX_STATIC, 235, at, away, 0);   /* FUN_002fd0c0: E235 5 cm out along the wall's normal */
        return;
    }

    /* Continuations within the action, or the end of the move. */
    if (c->move == MOVE_STINGER_HOLD) {
        if (stinger_dash_over(c, player)) start_move(c, d->next, 0);
    } else if (d->next != MOVE_NONE && d->next_on == 1 && c->phase_time >= STINGER_HOLD_SECONDS) {
        start_move(c, d->next, 0);
    } else if (c->air && d->flag_air) {
        /* Still flying: the pose holds until the floor. */
        if (c->frame > m->end_frame) c->frame = m->end_frame;
    } else if (c->frame >= m->end_frame + SCRIPT_LAG) {
        ++c->plays;
        if (c->move == MOVE_SM_DANCE_STAB && c->plays < DANCE_STAB_PLAYS) start_move(c, c->move, 0);
        else if (d->next != MOVE_NONE) start_move(c, d->next, 0);
        else {
            int wall_kick = c->move == MOVE_TR_WALL_FLIP;
            end_move(c, player);
            if (wall_kick) {
                float away = player->yaw + (float)M_PI, at[3] = {player->x + sinf(away) * 5.0f, player->y, player->z + cosf(away) * 5.0f};
                player_wall_kick(player, away);
                nfx_spawn_e(NFX_STATIC, 235, at, away, 0);   /* FUN_00227fb0 state 2: FUN_002fd0c0's E235 */
            }
        }
    }
}

int combat_dante_can_die;

float combat_dead_frames(const Combat *c) { return c->dead_time; }

/* FUN_002e8d20 picks the reaction class from the attack record's kind (+0x18): kind 0 flinches (class 1), 1 heavy
 * flinch (2), 2-5 knockdown (3, 5), 6 the pop up (6), 7 the hang then knockdown (4); in the air every kind but 6
 * knocks him down, and so does a hit that takes his health to 0. */
int combat_player_hit_kind(Combat *c, Player *player, float from_x, float from_z, int kind, float damage) {
    float to_attacker;
    int front, down;
    if (c->move == MOVE_HIT || c->move == MOVE_HIT_HEAVY || c->move == MOVE_HIT_AIR ||
        c->move == MOVE_HIT_ALT || c->move == MOVE_HIT_BACK || c->move == MOVE_HIT_HEAVY_BACK ||
        c->move == MOVE_DOWN || c->move == MOVE_DOWN_FAR || c->move == MOVE_GET_UP || c->move == MOVE_GET_UP_FAR ||
        c->move == MOVE_RECOVER || c->move == MOVE_POP || c->move == MOVE_POP_LAND || player->health <= 0.0f) return 1;
    /* An estimate: the real block's rules (the gauge, the timing) aren't ported. */
    if (c->move == MOVE_RG_BLOCK || c->move == MOVE_RG_HOLD || c->move == MOVE_RG_AIR_BLOCK) return 0;
    if (damage < 0.0f) damage = kind >= 1 ? 800.0f : 700.0f; /* the Seven Hells' fallback records */
    player->health -= damage;
    if (player->health < 1.0f) player->health = combat_dante_can_die ? 0.0f : 1.0f;
    to_attacker = atan2f(from_x - player->x, from_z - player->z);
    front = fabsf(wrap_pi(to_attacker - player->yaw)) <= (float)M_PI * 0.5f;
    player->air_kind = AIR_NONE;
    player->jump_motion = -1;
    c->air = 0;
    c->base_y = player->y;
    down = (kind >= 2 && kind <= 7) || !player->on_ground || player->health <= 0.0f;
    if (kind == 6 && player->health <= 0.0f) kind = 0;   /* FUN_002e8d20: an emptying hit is class 3 */
    if (kind == 7 && !player->on_ground) kind = 0;       /* in the air kind 7 is class 3 too */
    if (down) {
        /* FUN_002e9a40 state 0: he faces the attacker and flies away from it. */
        player->yaw = to_attacker;
        start_knockdown(c, player, kind);
        return 1;
    }
    /* FUN_002e96a0 (light) / FUN_002e98d0 (heavy): hit from within 90 degrees of his facing (FUN_002e2be0) he
     * turns to the attacker and plays 1:0 or, half the time, 1:15 (heavy 1:2); from behind he turns away from it
     * and plays 1:1 (heavy 1:3). */
    player->yaw = front ? to_attacker : wrap_pi(to_attacker + (float)M_PI);
    start_move(c, kind >= 1 ? (front ? MOVE_HIT_HEAVY : MOVE_HIT_HEAVY_BACK)
                  : !front ? MOVE_HIT_BACK : (rand() % 10 < 5 ? MOVE_HIT_ALT : MOVE_HIT), 0);
    return 1;
}
int combat_player_hit_damage(Combat *c, Player *player, float from_x, float from_z, int heavy, float damage) {
    return combat_player_hit_kind(c, player, from_x, from_z, heavy ? 1 : 0, damage);
}
int combat_player_hit(Combat *c, Player *player, float from_x, float from_z, int heavy) {
    return combat_player_hit_damage(c, player, from_x, from_z, heavy, -1.0f);
}
const char *combat_script_key(int script) {
    return script >= 0 ? script_data[script].key : "-";
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
        if (cmd->style != 0xFF) {
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
