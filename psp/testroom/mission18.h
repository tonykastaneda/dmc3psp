#ifndef DMC3PSP_TESTROOM_MISSION18_H
#define DMC3PSP_TESTROOM_MISSION18_H
/* Mission 18, "Invading hell": 403's bingo board and the mirror out (EventTbl18, docs/GAME_FLOW.md). */

void m18_reset(void);
void m18_enter(int room);
void m18_cleared(int room);                       /* a room's fight is over */
int m18_door_shut(int room, int door);
int m18_update(float x, float y, float z);        /* 1: into the mirror, the mission's over */
void m18_draw(void);
void m18_status(char *out, int size);

#endif
