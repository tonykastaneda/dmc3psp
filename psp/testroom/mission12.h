#ifndef DMC3PSP_TESTROOM_MISSION12_H
#define DMC3PSP_TESTROOM_MISSION12_H
/* Mission 12, "Hunting the Hunter": what EventTbl12 adds to the rooms main.c runs (docs/GAME_FLOW.md). The
 * carriage ride (216) ends in 239 once its three waves are down, and Geryon's bridge (228) gives way to the
 * ground below at his 80%. Rooms are given by their numbers. */

/* A room's fight is over: returns 1 if Mission 12 moves Dante on by itself (the ride's end). */
int m12_room_done(int room, int *next_room, int *start);
/* Each frame in 228: returns 1 (and his place, x y z yaw-degrees) when Dante drops to the ground fight. */
int m12_warp(int room, float out[4]);

#endif
