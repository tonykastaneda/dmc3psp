#ifndef DMC3PSP_TESTROOM_NEEDLES_H
#define DMC3PSP_TESTROOM_NEEDLES_H
/* The spike traps (CStageSetNeedle, set 100) of 125 (Mission 6's Trial of Skill), 131 (Mission 17's) and 610 (Secret
 * Mission 11), as FUN_003df400 and the functions after it run them: rows of three spiked panels (the set's own model)
 * thrust from the ceiling, the floor or a wall and step down the corridor. See needles.c. Rooms are given by number. */

/* Into a room: its trap, if it has one. rearm: the trap may start again even if it ran before (Mission 6's 125 is
 * rearmed until its Essence is taken). */
void needles_enter(int room, int rearm);
/* The trap's level (0-4; the port's difficulty stands in for the value the game picks it by). */
void needles_set_level(int difficulty);
void needles_update(float dt, float x, float y, float z, int on_ground);
int needles_hazard(float *from_x, float *from_z);   /* a panel caught him this frame: where it came from */
void needles_draw(void);
int needles_running(void);
void needles_status(char *out, int size);            /* for tests */

#endif
