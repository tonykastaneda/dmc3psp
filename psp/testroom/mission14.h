#ifndef DMC3PSP_TESTROOM_MISSION14_H
#define DMC3PSP_TESTROOM_MISSION14_H
/* Mission 14, "Drive!": 237's combat adjudicator and 008's ending (EventTbl14, docs/GAME_FLOW.md). */

void m14_reset(void);
void m14_enter(int room);
int m14_door_shut(int room, int door);
int m14_update(float dt, float x, float y, float z);   /* 1: the mission's over */
void m14_hit(float x, float y, float z, float yaw, float reach, int heavy);
void m14_draw(void);
void m14_status(char *out, int size);

#endif
