#ifndef DMC3PSP_TESTROOM_MISSION19_H
#define DMC3PSP_TESTROOM_MISSION19_H
/* Mission 19, "Forces collide": what EventTbl19 adds to the rooms main.c runs (docs/GAME_FLOW.md). Rooms by number. */

/* Leaving a room by one of its doors: returns 1 if Mission 19 sends it elsewhere. */
int m19_door_route(int room, int door, int has_samsara, int *next_room, int *start);

#endif
