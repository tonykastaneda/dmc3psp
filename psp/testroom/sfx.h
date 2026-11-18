#ifndef DMC3PSP_TESTROOM_SFX_H
#define DMC3PSP_TESTROOM_SFX_H
/* The game's sound effects (sfx.c, docs/MUSIC.md). Ids are the system bank's (the game's type 0). */
int sfx_init(const char *game_dir);
void sfx_play(int id, float pan, float volume);   /* pan -1 left .. 1 right, added to its own */
void sfx_volume(float v);
#endif
