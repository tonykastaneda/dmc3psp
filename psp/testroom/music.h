#ifndef DMC3PSP_TESTROOM_MUSIC_H
#define DMC3PSP_TESTROOM_MUSIC_H
/* The missions' music (music.c, docs/MUSIC.md). */
void music_init(const char *game_dir);   /* reserves two channels and starts their threads */
void music_room(int mission, int room);  /* the room's on-enter BGM (event op 0x70) */
void music_update(int fight);            /* each frame: whether enemies are fighting him now */
void music_mute(int on);                 /* cutscenes and movies: silent */
void music_stop(void);
void music_menu(int track);              /* a menu's SDATA track (129 Option, 130 Result) */
#endif
