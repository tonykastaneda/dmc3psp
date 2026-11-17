#ifndef DMC3PSP_TESTROOM_MISSION19_H
#define DMC3PSP_TESTROOM_MISSION19_H
/* Mission 19, "Forces collide": what EventTbl19 adds to the rooms main.c runs (docs/GAME_FLOW.md). Rooms by number. */

/* Leaving a room by one of its doors: returns 1 if Mission 19 sends it elsewhere. */
int m19_door_route(int room, int door, int has_samsara, int *next_room, int *start);

/* 404's hourglass (mission19.c): stopped = the room's fight is over or the Samsara is taken. */
void m19_enter(int room, int stopped);
/* Each frame in Mission 19: returns 1 when the sand runs out (the Abyss come back). */
int m19_update(float dt, int cleared);
int m19_clock_text(char *out, int size);   /* the timer, while it runs */
void m19_status(char *out, int size);
/* 405's mirrors: one of Dante's blows (mission19.c). */
void m19_test_mirror(int k);   /* tests: light mirror k and hold it */
void m19_mirror_hit(float x, float y, float z, float yaw, float reach, int heavy);

#endif
