#ifndef DMC3PSP_TESTROOM_GAMEOVER_H
#define DMC3PSP_TESTROOM_GAMEOVER_H

/* Dante's death as the game has it (docs/GAME_FLOW.md "Game Over"; PCSX2, Mission 2's street with the 100% save):
 * the screen darkens over him where he lies while a Yellow Orb rises, then "REBIRTH  YES  NO" with the orbs held
 * (bottom right) if he has any; YES spends one and he gets up there with his health back. NO, or no orb, brings
 * "CONTINUE / RESTART THE MISSION / END" over the same dark scene, each asking YES / NO (the cursor starts on NO).
 * Continue reloads the room from where he came in; Restart starts the mission again; End leaves for the menus. */
typedef enum { GAMEOVER_NONE, GAMEOVER_REBIRTH, GAMEOVER_CONTINUE, GAMEOVER_RESTART, GAMEOVER_END } GameOverAction;

void gameover_start(int yellow_orbs);
int gameover_active(void);
/* One frame of input (pressed: the buttons just down) over dt seconds; returns what was chosen, once. */
GameOverAction gameover_update(unsigned int pressed, float dt);
/* Inside an open display list, over the 3D scene and under nothing else. */
void gameover_draw(void);
void gameover_reset(void);

#endif
