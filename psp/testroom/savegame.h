#ifndef DMC3PSP_TESTROOM_SAVEGAME_H
#define DMC3PSP_TESTROOM_SAVEGAME_H

/* Progress on the memory stick through the PSP's own save utility (sceUtilitySavedata): its list of slots, with the
 * port's icon (the EBOOT's ICON0), a title and a line of detail, as the game's save screen lists its slots. What's
 * kept is what the game's save struct keeps that the port has (docs/SHOP.md "Save / session struct"): missions
 * cleared per difficulty, Red Orbs, the shop's items held and times bought, Blue Orb fragments, the Devil Arms and
 * styles got, and the Bloody Palace's best level. */
typedef struct {
    char magic[4];               /* "D3PS" */
    unsigned int version;        /* SAVEGAME_VERSION */
    unsigned int cleared[2];     /* Easy, Normal: bit m-1 for mission m */
    int red_orbs;
    unsigned char owned[8], bought[8];
    unsigned char blue_fragments, last_mission, last_difficulty, turbo;
    unsigned int got;            /* player_owned bits got in play (player.h OWN_*) */
    int bp_best;
    unsigned int reserved[8];
} SaveGame;
#define SAVEGAME_VERSION 1u

/* The save in memory, kept up to date by the game (mission clears, purchases, the Palace) and written when the
 * player saves. */
extern SaveGame savegame;
extern int savegame_auto;   /* tests: no dialog (AUTOSAVE / AUTOLOAD, slot 0000) */
void savegame_init(const char *game_dir);   /* where the EBOOT is (its ICON0 is the save's icon) */
void savegame_clear(void);                  /* a new game */
/* The utility's dialogs, run on their own frames: 1 if a save was written / read (the latter into savegame). */
int savegame_save_dialog(void);
int savegame_load_dialog(void);
int savegame_missions_cleared(void);        /* missions cleared on any difficulty */

#endif
