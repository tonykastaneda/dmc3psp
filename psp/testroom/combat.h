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
    MOVE_COUNT
} MoveId;

typedef struct {
    MoveId move;
    int motion;            /* motion table index of the current move */
    float frame;
    MoveId queued;         /* follow-up requested inside the input window */
    float last_root[3];    /* root translation sampled last frame (for deltas) */
    int mash_presses;      /* triangle presses during the current move */
    float since_press;     /* seconds since the last triangle press */
    float sword_out;       /* seconds left holding the drawn sword after attacking */
    /* Cross-fade from the previous pose source (locomotion or prior move). */
    int fade_motion;       /* -1 = locomotion */
    float fade_frame, fade_weight;
} Combat;

int combat_init(void);     /* resolves rb-NN names; 0 if the moveset is missing */
void combat_reset(Combat *combat);
/* Consumes attack input, advances the current move and applies root motion to
 * the player. Call before player_update; player->attacking is set here. */
void combat_update(Combat *combat, Player *player, const PlayerInput *input, float dt);
const char *combat_move_name(MoveId move);
int combat_idle_motion(void);  /* sword-drawn idle (rb-00) or -1 */

#endif
