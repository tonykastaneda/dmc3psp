#ifndef DMC3PSP_TESTROOM_SFX_H
#define DMC3PSP_TESTROOM_SFX_H
/* The game's sound effects (sfx.c, docs/MUSIC.md). Ids are the system bank's (the game's type 0). */
enum { SFX_BANK_SYS, SFX_BANK_DANTE, SFX_BANK_WEAPON0, SFX_BANKS = SFX_BANK_WEAPON0 + 10 };
int sfx_init(const char *game_dir);
void sfx_play(int id, float pan, float volume);   /* pan -1 left .. 1 right, added to its own */
void sfx_volume(float v);
void sfx_play_bank(int bank, int id, float pan, float volume);
/* Dante's motion sound events passed this frame: from < 0 when the motion just began. */
void sfx_motion(int motion, float from, float to);
/* The room's footstep bank (by room number, -1 none) and the material of the floor under Dante (0: no steps). */
void sfx_room(int room);
void sfx_floor(int material);
void sfx_step_stats(int out[3]);   /* tests: steps played, the last one's material, steps on no material */
#endif
