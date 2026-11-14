#ifndef DMC3PSP_TESTROOM_MISSION7_H
#define DMC3PSP_TESTROOM_MISSION7_H
/* Mission 7, "A chance meeting": what EventTbl07 adds to the rooms main.c runs (docs/GAME_FLOW.md). Now the
 * Cursed Skull Chamber (105): its crest switch (100, flag 35) wakes the hanging ball (HANG 200, flags 36/37), and
 * breaking the ball's shell shows the Crystal Skull (item 45). Rooms are given by their numbers. */

void m7_reset(void);
void m7_enter(int room);
void m7_update(float dt);
/* A melee hit's frame. heavy: 1 for a heavy hit. */
void m7_hit(float x, float y, float z, float yaw, float reach, int heavy);
int m7_item_shown(int item);   /* 0 while the script still hides it */
void m7_push_out(float *x, float *z, float y, float r);   /* the hanging ball is solid */
void m7_draw(void);            /* world space, no texture or lighting */
void m7_status(char *out, int size);

#endif
