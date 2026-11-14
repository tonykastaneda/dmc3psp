#ifndef DMC3PSP_TESTROOM_MISSION17_H
#define DMC3PSP_TESTROOM_MISSION17_H
/* Mission 17, "Inner-demons": what EventTbl17 adds to the rooms main.c runs (docs/GAME_FLOW.md). Rooms by number. */

void m17_reset(void);
/* Leaving a room by one of its doors: returns 1 if Mission 17 sends it elsewhere. */
int m17_door_route(int room, int door, int *next_room, int *start);
/* Each frame: returns 1 (and where) when the script moves Dante on by itself (143's area 0: demo 47 -> 139). */
int m17_move(int room, float x, float y, float z, int boss_beaten, int *next_room, int *start);

#endif
