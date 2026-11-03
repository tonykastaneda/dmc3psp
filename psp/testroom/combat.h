#ifndef DMC3PSP_TESTROOM_COMBAT_H
#define DMC3PSP_TESTROOM_COMBAT_H

/* Rebellion and Swordmaster-Rebellion, driven by the game's own data
 * (docs/MOVESET.md, docs/MOVESET_RE.md; tables in combat_data.h):
 *
 *   - which move a button press starts, and when it may cancel the current
 *     one, is DMC3's command walker (FUN_002458f0) over the command table:
 *     input, posture, style, category order, "after action N", and cancel
 *     gates;
 *   - the gates (player+0x24dd[16]) and the attack-active frames come from
 *     each move's motion script timeline, in the motion's own frames;
 *   - multi-part moves (Stinger, Million Stab, Shredder, Crazy Dance, Round
 *     Trip) chain their scripts the way the action handlers request them.
 *
 * Trickster (actions 180-183) goes through the same command rows; its player
 * states (dash 0x16, Sky Star 0x17, Air Trick 0x18, Wall Hike) pick scripts
 * as their handlers do. Royalguard, Air Hike and the lock-on rolls are not
 * decoded yet and keep their hand-made starts. */

#include "player.h"
#include "combat_moves.h"

/* A weapon's place on Dante in one placement state (FUN_00293910). mode: 0 fixed to the
 * bone, 255 hidden, else a weapon motion plays. owner: nonzero, the bone is the coat's. */
typedef struct { unsigned char mode, owner, bone; float pos[3], rot[3]; } WeaponPlace;

typedef enum {
    MOVE_NONE = -1,
    /* Rebellion (GDATA 23, "rb-NN") */
    MOVE_COMBO1_1, MOVE_COMBO1_2, MOVE_COMBO1_3,  /* actions 1, 2, 3 */
    MOVE_COMBO2_2, MOVE_COMBO2_3,                 /* 4 (after the pause), 5 */
    MOVE_MILLION_STAB, MOVE_COMBO2_FINISH,        /* 14 (mash) -> rb-09 */
    MOVE_STINGER_DASH, MOVE_STINGER_HOLD, MOVE_STINGER_THRUST, /* 7/8: R + fwd + TRI */
    MOVE_HIGH_TIME,                               /* 10: R + back + TRI */
    MOVE_HELM_BREAKER,                            /* 6: TRI in the air */
    MOVE_ROUND_TRIP, MOVE_ROUND_TRIP_CATCH,       /* 13: charge TRI, release */
    /* Swordmaster - Rebellion (GDATA 33, "s33-NN"; dance steps reuse rb-) */
    MOVE_SM_SWORD_PIERCE,                         /* 15: R + fwd + CIR */
    MOVE_SM_PROP,                                 /* 16: CIR */
    MOVE_SM_SHREDDER, MOVE_SM_SHREDDER_END,       /* 24: CIR after Prop */
    MOVE_SM_DANCE_START,                          /* 200: R + back + CIR */
    MOVE_SM_DANCE_1, MOVE_SM_DANCE_2, MOVE_SM_DANCE_3, /* 201-206: CIR each */
    MOVE_SM_DANCE_4, MOVE_SM_DANCE_5, MOVE_SM_DANCE_6,
    MOVE_SM_DANCE_STAB, MOVE_SM_DANCE_FINISH,     /* 207 */
    MOVE_SM_CRAZY_DANCE, MOVE_SM_CRAZY_FINISH,    /* 208: mash CIR */
    MOVE_SM_RAVE_1, MOVE_SM_RAVE_2, MOVE_SM_RAVE_3, MOVE_SM_RAVE_4, /* 19-22: CIR in the air */
    /* Trickster (GDATA 43, "s43-NN"; player states 0x16-0x18, FUN_00225c90 ...) */
    MOVE_TR_DASH, MOVE_TR_DASH_BACK, MOVE_TR_DASH_LEFT, MOVE_TR_DASH_RIGHT,     /* 180: 23:0-3 */
    MOVE_TR_DASH2, MOVE_TR_DASH2_BACK, MOVE_TR_DASH2_LEFT, MOVE_TR_DASH2_RIGHT, /* chained: 23:24-27 */
    MOVE_TR_DASH_CHAIN,                           /* 2nd fwd dash after a fwd dash: 23:17 */
    MOVE_TR_SKY_STAR,                             /* 181: 23:11 */
    MOVE_TR_AIR_TRICK, MOVE_TR_AIR_TRICK_AIR,     /* 182: 23:15 / 23:16 */
    MOVE_TR_WALL_HIKE, MOVE_TR_WALL_FLIP,         /* 183: 23:5 -> 23:6 */
    /* Not decoded yet: hand-made. */
    MOVE_TR_AIR_HIKE,
    MOVE_RG_BLOCK, MOVE_RG_AIR_BLOCK,
    MOVE_ROLL_LEFT, MOVE_ROLL_RIGHT, MOVE_BACKFLIP,
    /* Every other weapon: phases generated from tools/moveset_phases.py. */
    GEN_MOVE_IDS
    MOVE_COUNT
} MoveId;

#define COMBAT_GATES 16

typedef struct {
    MoveId move;
    int action;            /* command-table action id of the current move (0 = none) */
    int motion;            /* motion table index of the current move */
    float frame;           /* motion frame (absolute, as the scripts address it) */
    int script;            /* script_data index, -1 for hand-made moves */
    int next_event;        /* next script event to apply */
    unsigned char gates[COMBAT_GATES]; /* 0 closed, 1 buffer, 2 open */
    int pending;           /* action buffered while its gate was 1 */
    int active;            /* attack-active window (window 0 byte 1 bit 0x80) */
    float last_root[3];    /* root translation sampled last frame (for deltas) */
    float phase_time;      /* seconds in the current move */
    int plays;             /* times the current move's motion has completed */
    /* Input recognition (the game's patterns 4, 16, 40: charge and mash). */
    int tri_mash, cir_mash;          /* presses in the current mash run */
    float since_tri, since_cir;      /* seconds since the last press */
    float tri_held;                  /* seconds TRI has been held */
    unsigned int buffered;           /* inputs of the last press, kept briefly */
    int last_dir;                    /* stick vs facing at the last press (+1 fwd, -1 back) */
    float buffer_time;
    float sword_out;       /* seconds left holding the drawn sword after attacking */
    /* Cross-fade from the previous pose source (locomotion or prior move). */
    int fade_motion;       /* -1 = locomotion */
    float fade_frame, fade_weight;
    float base_y;          /* height at the start of a root-Y move */
    int hits_dealt;
    int dashes;            /* Trickster dashes in a row (player+0x3c75; reset in neutral) */
    int flag;              /* script window 0 byte 4 this frame (player+0x24d5, one-shot) */
    int air;               /* the move owns Dante's height */
    float air_v[4];        /* speed, speed accel, vy, gravity, per 60 fps frame */
    float charge_frames;   /* frames in the current charge phase */
    float phase_frames;    /* frames in a timed phase */
    float since_cir_short;
    int shots_pending;     /* shots for main to fire this frame (it knows the hand's position) */
    int shot_buffered;     /* square pressed during a shot: fires again at the refire frame */
    int shot_hand;         /* alternates Ebony and Ivory */
    float auto_timer;
    /* Gun stance (player+0x28a6, FUN_002436e0: R1 held, or just after a shot). */
    int stance_active;
    float stance_timer;    /* player+0x3c60: frames the stance outlasts R1 */
    int stance_upper;      /* whole-pose or arms motion while no shot plays */
    float stance_frame;
    float walk_dir;        /* player+0x28dc: direction of travel */
    int walking;
    int shot_count;        /* player+0x299f: gun shots in a row */
    float since_fire;      /* frames since the last shot left the gun */
    float shot_delay;      /* frames before a shot fires, turning to the target */
    int aim_target;        /* shots go at the target, not straight ahead */
    int was_stance;        /* the stance was on when this shot started */
    /* Legs under the stance and under upper-body shots (-1 = none). */
    int lower_motion;
    float lower_frame;
    int lower_fade_motion;
    float lower_fade_frame, lower_fade_weight;
    int upper_pending;     /* a new shot: set it up on the next update */
    int gun_row;           /* player+0x3b9f */
    int place_state;       /* the current script's weapon placement (player+0x24d2) */
    int script_bank;       /* its bank, which says which weapon it belongs to */
    int delayed_shot;      /* a shot waiting out shot_delay */
    MoveId last_move_for_end;
    /* Restored: charge, input recognition and Air Trick state. */
    int charge_looped;     /* the charge motion has played through (player+0x2856) */
    int sq_mash, cir_short_mash;
    float since_sq, sq_held, cir_held;
    float warp_time, warp_step[3];  /* Air Trick warp (FUN_00226550 state 1-2) */
} Combat;

int combat_init(void);     /* resolves motions and scripts; 0 if the moveset is missing */
void combat_reset(Combat *combat);
/* Consumes attack input, advances the current move and applies root motion to
 * the player. Call before player_update; player->attacking is set here. */
void combat_update(Combat *combat, Player *player, const PlayerInput *input, float dt);
const char *combat_move_name(MoveId move);
/* Takes the shots fired since the last call; 0 if none. */
int combat_take_shots(Combat *combat);
int combat_move_is_gun(MoveId move);
/* Where a weapon (0-4 melee, 5-9 guns) is now (FUN_002954b0, FUN_00293550): the placement
 * state if it's in use, else sheathed. second: Ivory, the gun's second model. */
const WeaponPlace *combat_weapon_place(const Combat *combat, const Player *player, int weapon, int second);
/* The stance's motions: whichever drives the pose while no move plays. */
void combat_stance_motion(const Combat *combat, int *upper, float *upper_frame);
int combat_idle_motion(void);  /* sword-drawn idle (rb-00) or -1 */
/* Runs every generated action to its end with no input, off screen; returns how
 * many finished. *failed_action = the first that hung, *fallbacks = phases that
 * waited on a script flag the script never set. */
int combat_selftest(int *total, int *failed_action, int *fallbacks);
/* Feeds each neutral command's button through combat_update with its weapon, style
 * and posture set, as the pad does; returns how many started a move of that weapon. */
int combat_selftest_inputs(int *total, int *first_failed_action);

#endif
