/* What the Damned Queen (queen.c) and King (king.c) share with every chessman: CEm017's wait, landing, knocks and
 * castle (docs/CHESS_ROYALS_SPEC.md, "Common to both" and "Shared actions 10-13"). Pure logic: enemy.c's glue copies
 * the body in and out each frame, plays `motion`, and turns the events into sounds, effects and shakes. */
#pragma once

enum { ROYAL_WAIT = 0, ROYAL_MOVE = 2, ROYAL_ATTACK = 3, ROYAL_CASTLE = 11, ROYAL_KNOCK = 12, ROYAL_KNOCK_HEAVY = 13 };
/* The master's commands (+0x488, chess.h). */
enum { ROYAL_CMD_NONE = 0, ROYAL_CMD_RESPOND = 20, ROYAL_CMD_CASTLE = 30 };
/* Events, the native calls they stand for (FUN_00359da0 sounds come from bank 0x15 / 0x16 by EST). */
enum {
    ROYAL_EV_LAND = 1,         /* FUN_0040a2a0 shake (strength in `shake`) and sound 0x14 */
    ROYAL_EV_HIT_SHAKE = 2,    /* a hit's shake 5 (00408010) */
    ROYAL_EV_CASTLE = 4,       /* sound 0x2c, G0x390 at the feet */
    ROYAL_EV_CASTLE_DOWN = 8,  /* G0x1fa where it is set down */
    ROYAL_EV_KNOCK = 16        /* sound 6 / 8 by the hit's type, the hit's own sound */
};

typedef struct {
    /* in, each frame */
    float floor_y;          /* the floor under the piece */
    float frame;            /* the motion's frame (+0x544 vfunc +0x28) */
    int done;               /* the motion has ended (FUN_00255b70) */
    int wall;               /* the last move ran into a wall (+0xcac +8 & 0x30000) */
    float hp;               /* +0xea4 */
    int rank;               /* +0x3ab, 0-3 */
    /* in / out */
    float pos[3], vel[3], yaw;
    int action, sub;        /* +5, +6 */
    float timer;            /* +0x3b8 */
    float timer2;           /* +0x3b4 */
    float knock_yaw;        /* +0x486: from Dante, set by a hit */
    float dest[3];          /* +0x310, the castle's destination */
    int heavy_variant;      /* +0x3bc bit 0x100 */
    /* out */
    int motion;             /* the motion id the action plays (FUN_0040a190), -1 none */
    int restart;            /* restart it */
    unsigned events;
    float shake;
    int busy;               /* +0x110 bit 0x40000000, for the master */
} RoyalBody;

void royal_start(RoyalBody *b, int action);                  /* FUN_0040a140 */
void royal_motion(RoyalBody *b, int motion);                 /* FUN_0040a190 (-1 = the action's number) */
/* FUN_00408320's sub-states 0 / 1 for the Queen and King: returns 1 when the wait has run out. */
int royal_wait(RoyalBody *b, float f, float dante_distance, float base, float spread, float (*unit)(void));
/* The fall that ends every move: -3.2 a frame squared until 10 cm over the floor (FUN_0042eb00), then shake 5,
 * sound 0x14 and the wait. Returns 1 on landing. */
int royal_fall(RoyalBody *b, float f, float shake);
void royal_castle(RoyalBody *b, float f);                    /* action 11, 00408dd0 */
void royal_knock(RoyalBody *b, float f);                     /* actions 12 / 13, 004091f0 / 00409450 */
/* A hit on the piece (00407fe8): shake 5; while it waits, kind 3 is the light knock and kinds 4 / 7 the heavy one.
 * For the Queen and King every other kind does nothing. launch: native kind 3; heavy: kinds 4 / 7 [port mapping
 * inferred]. */
void royal_hit(RoyalBody *b, int launch, int heavy, float dante_x, float dante_z);
/* The 8-way heading toward (x, z) (FUN_0040a300), 0 = +z, 2 = +x. */
int royal_heading8(const RoyalBody *b, float x, float z);
/* 10 cm a frame along the heading, 7.07 / 7.07 on a diagonal (FUN_0040a340), times 1 + rank / 4. */
void royal_heading_velocity(RoyalBody *b, int dir);
