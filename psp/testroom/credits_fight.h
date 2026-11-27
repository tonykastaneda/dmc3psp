#ifndef DMC3PSP_TESTROOM_CREDITS_FIGHT_H
#define DMC3PSP_TESTROOM_CREDITS_FIGHT_H
/* Mission 20's ending street (room 012): the kill counter, the staff roll and the timed ending, after EventTbl20's
 * room 12 script and CStaffRoll (docs/CREDITS_FIGHT.md). main.c only calls these. */

void credits_fight_init(const char *game_dir);   /* where STAFF_nn.BIN are (tools/prepare_staffroll.py) */
/* Room 012's fog (LIG2's fog colour, fitted distances): 1 and sets sceGuFog and the clear colour while the street runs. */
int credits_fight_fog(unsigned int *clear);
void credits_fight_begin(void);          /* entering room 012 in Mission 20 (the room's on-enter block) */
void credits_fight_reset(void);           /* leaving the mission */
int credits_fight_active(void);
void credits_fight_kill(int kind);        /* an enemy died while the street runs */
int credits_fight_kills(void);            /* enemy manager +0x8a0 */
/* One update; frames at 60 a second. Returns 1 when the street is over (op 7e [4] and the demo after it). */
int credits_fight_update(float frames);
int credits_fight_frozen(void);           /* op 7e [3]: Dante and the enemies stop */
float credits_fight_clock(void);          /* frames since the room's on-enter block */
int credits_fight_extended(void);         /* op 7e [2] found 100 or more kills */
void credits_fight_skip(float frames);           /* tests: on to this frame of the street's clock */
void credits_fight_test_kills(int n);             /* tests: the counter at n */
void credits_fight_status(char *out, int size);   /* tests: a line for FOCUS_DONE.TXT */
void credits_fight_draw(void);           /* 2D, over the scene: the roll's pages, the fade, the counter */

#endif
