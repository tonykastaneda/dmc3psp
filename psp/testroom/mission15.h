#ifndef DMC3PSP_TESTROOM_MISSION15_H
#define DMC3PSP_TESTROOM_MISSION15_H
/* Mission 15, "The gatecrasher": what EventTbl15 and its rooms' gimmicks add to the rooms main.c runs
 * (docs/GAME_FLOW.md). The coloured doors of 222, 202, 212, 211, 226 and 227 (STAY 600) lead where counter 4 says;
 * striking a flame portal (SWITCH 100/200 with STAY 650) in 202, 212, 226 or 227 turns it on by one, and 202's
 * works only once 200's has been struck (flag 2). Rooms are given by their numbers. */

void m15_reset(void);
void m15_enter(int room);
void m15_update(float dt);
/* A melee hit's frame. */
void m15_hit(float x, float y, float z, float yaw, float reach);
/* Leaving a room by one of its doors: returns 1 if Mission 15 sends it elsewhere. */
int m15_door_route(int room, int door, int *next_room, int *start);
void m15_status(char *out, int size);

#endif
