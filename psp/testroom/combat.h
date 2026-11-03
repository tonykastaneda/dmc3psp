#ifndef DMC3PSP_TESTROOM_COMBAT_H
#define DMC3PSP_TESTROOM_COMBAT_H

/* Rebellion moveset (Dante's GDATA slot 23 motions, "rb-NN").
 *
 * Move chaining was recovered from the data, not guessed from names: every
 * follow-up motion starts with its root (bone 0) translation equal to the
 * root position of its predecessor at the frame where the hand-off happens
 * (rb-03@18 -> rb-04, rb-04@37 -> rb-05, rb-06@66 -> rb-07, rb-07@137 and
 * rb-08@97 -> rb-09, rb-14@10 -> rb-15/rb-16). The button mapping is the
 * standard DMC3 scheme; which motion is which named move is an informed
 * reading (root travel, loop flags, poses) and still needs a player's eye. */

#include "player.h"

typedef enum {
    MOVE_NONE = -1,
    MOVE_COMBO1_1, MOVE_COMBO1_2, MOVE_COMBO1_3,  /* rb-03, rb-04, rb-05 */
    MOVE_COMBO2_2, MOVE_COMBO2_3,                 /* rb-06, rb-07 (pause combo) */
    MOVE_MILLION_STAB, MOVE_COMBO2_FINISH,        /* rb-08 (mash loop), rb-09 */
    MOVE_STINGER_DASH, MOVE_STINGER_HOLD, MOVE_STINGER_THRUST, /* rb-14, rb-15, rb-16 */
    MOVE_HIGH_TIME,                               /* rb-10 */
    MOVE_AERIAL_RAVE,                             /* rb-32 */
    MOVE_HELM_BREAKER,                            /* rb-29: air, R + forward + triangle */
    /* Swordmaster (circle, only in Swordmaster style). Identification of
     * these from the data is a best reading; see combat.c. */
    MOVE_SM_PROP,                                 /* rb-18: mash for Shredder */
    MOVE_SM_DANCE_START, MOVE_SM_DANCE,           /* rb-17 -> rb-19: Dance Macabre */
    MOVE_SM_CRAZY_DANCE,                          /* rb-31: mash circle in Dance Macabre */
    MOVE_SM_DRIVE,                                /* rb-01: R + back + circle */
    MOVE_SM_QUICK_DRIVE,                          /* rb-11: circle during a combo hit */
    /* Trickster (shared set, GDATA slot 33, "s33-NN"). */
    MOVE_TR_DASH,                                 /* s33-17: crouch, then a 4 m burst */
    MOVE_TR_SKY_STAR,                             /* s33-18: air dash */
    MOVE_TR_AIR_HIKE,                             /* s33-07: double jump (cross in the air) */
    MOVE_TR_WALL_HIKE,                            /* s33-08: circle into a wall */
    /* Royalguard (slot 34, "s34-NN"): hold circle. */
    MOVE_RG_BLOCK,                                /* s34-06: guard, loops while held */
    MOVE_RG_AIR_BLOCK,                            /* s34-08 */
    /* Lock-on + direction + cross (base motions). */
    MOVE_ROLL_LEFT, MOVE_ROLL_RIGHT,              /* 2-07 (+X = Dante's left), 2-08 */
    MOVE_BACKFLIP,                                /* 2-09: 394 cm back, root Y arc */
    MOVE_COUNT
} MoveId;

typedef struct {
    MoveId move;
    int motion;            /* motion table index of the current move */
    float frame;
    MoveId queued;         /* follow-up requested inside the input window */
    float last_root[3];    /* root translation sampled last frame (for deltas) */
    int mash_presses;      /* presses of the move's button during the current move */
    float since_melee;     /* seconds since the last triangle press */
    float since_style;     /* seconds since the last (Swordmaster) circle press */
    float sword_out;       /* seconds left holding the drawn sword after attacking */
    /* Cross-fade from the previous pose source (locomotion or prior move). */
    int fade_motion;       /* -1 = locomotion */
    float fade_frame, fade_weight;
    float next_hit;        /* frame of the move's next hit (-1 = done) */
    float base_y;          /* height at the start of a root-Y move */
    int hits_dealt;
} Combat;

int combat_init(void);     /* resolves rb-NN names; 0 if the moveset is missing */
void combat_reset(Combat *combat);
/* Consumes attack input, advances the current move and applies root motion to
 * the player. Call before player_update; player->attacking is set here. */
void combat_update(Combat *combat, Player *player, const PlayerInput *input, float dt);
const char *combat_move_name(MoveId move);
int combat_idle_motion(void);  /* sword-drawn idle (rb-00) or -1 */

#endif
