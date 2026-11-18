#ifndef DMC3PSP_TESTROOM_SFX_H
#define DMC3PSP_TESTROOM_SFX_H
/* The game's sound effects (sfx.c, docs/MUSIC.md). Ids are the system bank's (the game's type 0). */
enum { SFX_BANK_SYS, SFX_BANK_DANTE, SFX_BANKS };
int sfx_init(const char *game_dir);
void sfx_play(int id, float pan, float volume);   /* pan -1 left .. 1 right, added to its own */
void sfx_volume(float v);
void sfx_play_bank(int bank, int id, float pan, float volume);
/* Dante's motion sound events passed this frame: from < 0 when the motion just began. */
void sfx_motion(int motion, float from, float to);
#endif
