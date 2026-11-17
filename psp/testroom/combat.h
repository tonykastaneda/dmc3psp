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
    MOVE_HIGH_TIME_RISE,                          /* still holding TRI: he goes up after it (3:13) */
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
    MOVE_TR_WALL_RUN_L, MOVE_TR_WALL_KICK_L,      /* 183 along the wall: 23:7 -> 23:8 (+0x3c6d 0) */
    MOVE_TR_WALL_RUN_R, MOVE_TR_WALL_KICK_R,      /* 23:9 -> 23:10 (+0x3c6d 1) */
    /* Not decoded yet: hand-made. */
    MOVE_RG_BLOCK, MOVE_RG_HOLD, MOVE_RG_END, MOVE_RG_AIR_BLOCK,
    MOVE_TAUNT,                                   /* SELECT: script 2:0 */
    MOVE_QUICKSILVER, MOVE_DOPPELGANGER,          /* R1 + CIR in those styles: 25:0, 26:0 */
    MOVE_DEVIL_ON,                                /* turning Devil Trigger on: 0:50 */
    MOVE_HIT, MOVE_HIT_HEAVY, MOVE_HIT_AIR,       /* Dante hit: 3-00, 3-02, 3-04 */
    MOVE_HIT_ALT, MOVE_HIT_BACK, MOVE_HIT_HEAVY_BACK, /* 3-15 (the front's other half), 3-01, 3-03 */
    MOVE_DOWN, MOVE_DOWN_FAR,                     /* knockdown: 1:4 / 1:26 (3-04), 1:5 / 1:27 (3-05) */
    MOVE_GET_UP, MOVE_GET_UP_FAR, MOVE_RECOVER,   /* 1:7 (3-07), 1:9 (3-09), quick recovery 1:6 (3-06) */
    MOVE_POP, MOVE_POP_LAND,                      /* kind 6: 1:13 (3-13) up and down, 1:14 (3-14) landing */
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
    float carry_speed;     /* forward speed this frame before the move (FUN_00131b50(+0x170)), for keep_min */
    int jump_moving;       /* this airtime's jump had the stick (+0x2904 != 4) */
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
    float sq_last_hold;   /* seconds SQ was held before its last release (Artemis: a charge skips the aim) */
    float warp_time, warp_step[3];  /* Air Trick warp (FUN_00226550 state 1-2) */
    int held_tri, held_sq, held_cir;
    float air_accum;
    int hover_done;        /* a hover phase's wait is over */
    /* Knockdown (FUN_002e9a40): launch speeds, landed yet, a recovery press seen (+0x2868), the far set. */
    float kd_vy, kd_g, kd_speed, dead_time;
    int kd_landed, kd_press, kd_far;
    float kd_hang;         /* kind 7: frames hanging before the flight (config +0x3c) */
    int kd_launch, kd_pop; /* kind 7 (FUN_002e9f50) / kind 6 (FUN_002ea390) */
    int twosome_set, twosome_shots;   /* Twosome Time's set (+0x3c6d) and shots in the series */
    /* Held by an enemy (his action 0x2e, FUN_0021f2e0): the mash toward breaking free (FUN_00254a20, +0x4a4c /
     * +0x4a4e) and the stick's last direction (+0x4a4a). */
    int held, held_mash, held_need, held_dir, held_free;
    float fade_seconds;    /* this move's blend in from the last pose (0: a cut) */
} Combat;

int combat_init(void);     /* resolves motions and scripts; 0 if the moveset is missing */
void combat_reset(Combat *combat);
/* Consumes attack input, advances the current move and applies root motion to
 * the player. Call before player_update; player->attacking is set here. */
const char *combat_script_key(int script);
/* An enemy's attack reached Dante, from this point. Returns 0 if he blocked it. */
int combat_player_hit(Combat *c, Player *player, float from_x, float from_z, int heavy);
int combat_player_hit_damage(Combat *c, Player *player, float from_x, float from_z, int heavy, float damage);
/* The same with the attack record's kind (+0x18; FUN_002e8d20): 0 flinch, 1 heavy flinch, 2-5 knockdown
 * (4 and up the far one), 7 launch; any hit in the air knocks him down. */
int combat_player_hit_kind(Combat *c, Player *player, float from_x, float from_z, int kind, float damage);
/* An enemy takes hold of him (FUN_0021f2e0: his action 0x2e); need is the mash to break free (FUN_00254a10).
 * While held he can't move or attack; combat_hold_broken says he's mashed free (and lets go); combat_release ends it
 * from the holder's side. */
void combat_hold(Combat *c, Player *player, int need);
int combat_held(const Combat *c);
int combat_hold_broken(Combat *c, Player *player);
void combat_release(Combat *c, Player *player);
/* Set by the game in missions (off in test scenes): health can reach 0, and he stays down. */
extern int combat_dante_can_die;
/* Frames he has lain dead, 0 while alive. */
float combat_dead_frames(const Combat *c);
void combat_update(Combat *combat, Player *player, const PlayerInput *input, float dt);
const char *combat_move_name(MoveId move);
/* Takes the shots fired since the last call; 0 if none. */
int combat_take_shots(Combat *combat);
int combat_move_is_gun(MoveId move);
/* Where a weapon (0-4 melee, 5-9 guns) is now (FUN_002954b0, FUN_00293550): the placement
 * state if it's in use, else sheathed. second: the other of a pair: Ivory, or Rudra's record. */
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

/* Each time one of Dante's attacks lands its hit frame: where he is, its reach, the HitKind. The missions use
 * it for breakable props. */
extern void (*combat_hit_fn)(const Player *player, float reach, int kind);

#endif
