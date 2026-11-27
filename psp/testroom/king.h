/* The Damned King (EST 40, CEm021 kind 1; em021.pac [7]-[11]), Mission 18's chess board (docs/CHESS_ROYALS_SPEC.md).
 * His logic, 0x4341a0: after every wait he moves one square toward Dante; he attacks on the master's command 20, when
 * Dante stays near him as he moves, or when he is hurt with Dante near. The attack is a blast object (0x43ebe0). */
#pragma once
#include "royal.h"

#define KING_MAX_HP 2300.0f      /* param block 0x5bfc90 +0x2c */
#define KING_DAMAGE 2500.0f      /* hit record 0x5bfc50 +4 */

enum {
    KING_EV_CHARGE = 0x100,      /* the blast object starts: G0x36c follows the King, sound 0x3d */
    KING_EV_BLAST = 0x200,       /* sound 0x3f, G0x36c gives way to G0x233 */
    KING_EV_BLAST_END = 0x400    /* the object is gone */
};

/* The blast object (vtable 0x60df60, start 0x43ea30, update 0x43ebe0). */
typedef struct {
    int active, state;
    float time;                  /* +0x454: 240 down */
    float pos[3];                /* on the King until the blast */
    float radius, height;        /* +0x420 / +0x424 of its hit volume */
    int hitting;                 /* FUN_002946a0 runs: 150 < time < 180 */
} KingBlast;

typedef struct {
    RoyalBody body;
    float hp_before;             /* +0x3c4 [inferred: the HP before this frame's hits] */
    float near_timer;            /* +0x478 */
    int command;                 /* his own +0x488 response (20), besides the master's */
    KingBlast blast;
} KingBrain;

void king_spawn(KingBrain *k, float hp, int rank);
/* command: the master's +0x488 (chess.h); returns it, cleared if he took it. */
int king_think(KingBrain *k, float f, float dante_x, float dante_y, float dante_z, int command, float (*unit)(void));
/* Dante in the blast's volume (a cylinder round its foot, `height` tall) while it hits. */
int king_blast_reaches(const KingBlast *b, float x, float y, float z);
