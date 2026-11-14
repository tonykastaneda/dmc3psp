#ifndef DMC3PSP_TESTROOM_MISSION6_H
#define DMC3PSP_TESTROOM_MISSION6_H
/* Mission 6, "Family ties": the Mute Goddess and the Chamber of 3 Trials, from EventTbl06 (GDATA 244, decoded in
 * docs/GAME_FLOW.md). main.c runs the rooms, doors and fights; this says what the event script adds to them.
 * Rooms are given by their numbers (122 for ROOM_122.BIN), doors by their index in the room's door list. */

void m6_reset(int difficulty);
/* Into a room. Returns 1 if the room's fight is to run from the top even though it was cleared before (126's,
 * after each wrong doorway). */
int m6_enter(int room);
/* Leaving a room by one of its doors: the script may send it elsewhere (126's doorways). Returns 1 if it did. */
int m6_door_route(int room, int door, int *next_room, int *start);
int m6_door_shut(int room, int door);   /* locked by the script */
int m6_script_held(int room);           /* the room's fight isn't to start (yet) */
/* Each frame. prompt: set to 1 when O does something here. Returns 1 when the mission's over. */
int m6_update(float dt, unsigned int pressed, float x, float y, float z, int on_ground, int fight_on, int *prompt);
int m6_hazard(float *from_x, float *from_z);   /* the spike trap caught him this frame */
/* A melee hit's frame: the crest switches. heavy: 2 for a heavy hit. */
void m6_hit(float x, float y, float z, float yaw, float reach, int heavy);
void m6_draw(void);                     /* world space, no texture or lighting */
const char *m6_caption(float *alpha);   /* a line to show, or 0 */
void m6_status(char *out, int size);    /* for tests */

#endif
