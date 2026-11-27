/* The Damned Queen (EST 39, CEm021 kind 0; em021.pac [2]-[6]), Mission 18's chess board (docs/CHESS_ROYALS_SPEC.md).
 * Her logic, 0x437510: after every wait she moves like a chess queen onto a line with Dante; the chess master's
 * command 20 starts her charge. */
#pragma once
#include "royal.h"

#define QUEEN_MAX_HP 1800.0f     /* param block 0x5c0030 +0x2c */
#define QUEEN_DAMAGE 1500.0f     /* hit record 0x5bfff0 +4 */

/* Events beyond royal.h's. */
enum {
    QUEEN_EV_WINDUP = 0x100,     /* G0x31b on her bone (+0x504 +0x104), G0x242 lane at her feet */
    QUEEN_EV_WINDUP_SOUND = 0x200, /* sound 0x37 */
    QUEEN_EV_CHARGE = 0x400,     /* G0x390 on her bone */
    QUEEN_EV_CHARGE_END = 0x800  /* the three effects stop */
};

typedef struct {
    RoyalBody body;
    float charge_yaw;            /* +0x480 */
    float speed, accel;          /* +0x68, +0x78 */
    float lane;                  /* G0x242's length: half the charge time left */
    int charges;                 /* +0x490 */
    int hitting;                 /* +0x110 bit 0: her body hits for QUEEN_DAMAGE */
} QueenBrain;

void queen_spawn(QueenBrain *q, float hp, int rank);
/* command: the master's +0x488 (chess.h); returns it, cleared if she took it. */
int queen_think(QueenBrain *q, float f, float dante_x, float dante_y, float dante_z, int command, float (*unit)(void));
