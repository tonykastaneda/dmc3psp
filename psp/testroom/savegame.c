/* Saves through the PSP's save utility (savegame.h). The dialog draws itself; these loops only clear the screen
 * under it, pump the utility and flip, as the SDK's samples do. */
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "savegame.h"

SaveGame savegame;
int savegame_auto;   /* tests: the utility's AUTOSAVE / AUTOLOAD on slot 0000, no dialog to answer */

#define GAME_NAME "DMC3PORTABLE"   /* the utility's gameName: 12 characters, the folder prefix in PSP/SAVEDATA */
#define DATA_FILE "DATA.BIN"
static const char *const TITLE = "Devil May Cry 3 Portable";
static char save_names[][20] = {"0000", "0001", "0002", "0003", "0004", "0005", "0006", "0007", ""};

static void *icon0;
static int icon0_size;
static unsigned int __attribute__((aligned(16))) list[1024];
static SaveGame io_buffer;

void savegame_clear(void) {
    memset(&savegame, 0, sizeof savegame);
    memcpy(savegame.magic, "D3PS", 4);
    savegame.version = SAVEGAME_VERSION;
    savegame.last_mission = 1;
    savegame.last_difficulty = 1;
    savegame.turbo = 1;
}

/* The save's icon: the EBOOT's own ICON0 (PBP header: "\0PBP", version, then the offsets of PARAM.SFO, ICON0,
 * ICON1, ...), read once. */
void savegame_init(const char *game_dir) {
    char path[320];
    unsigned int header[10];
    SceUID fd;
    savegame_clear();
    snprintf(path, sizeof path, "%sEBOOT.PBP", game_dir);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    if (sceIoRead(fd, header, sizeof header) == (int)sizeof header && header[0] == 0x50425000u && header[4] > header[3]) {
        icon0_size = (int)(header[4] - header[3]);
        icon0 = icon0_size < 256 * 1024 ? malloc((size_t)icon0_size) : NULL;
        if (icon0 && (sceIoLseek32(fd, (int)header[3], PSP_SEEK_SET) != (int)header[3] || sceIoRead(fd, icon0, icon0_size) != icon0_size)) {
            free(icon0); icon0 = NULL;
        }
    }
    sceIoClose(fd);
}

int savegame_missions_cleared(void) {
    unsigned int m = savegame.cleared[0] | savegame.cleared[1];
    int n = 0;
    for (; m; m &= m - 1) ++n;
    return n;
}

static void dialog_common(SceUtilitySavedataParam *p, PspUtilitySavedataMode mode) {
    int language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH, swap = PSP_UTILITY_ACCEPT_CROSS;
    memset(p, 0, sizeof *p);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &swap);   /* the X / O swap */
    p->base.size = sizeof *p;
    p->base.language = language;
    p->base.buttonSwap = swap;
    p->base.graphicsThread = 0x11;
    p->base.accessThread = 0x13;
    p->base.fontThread = 0x12;
    p->base.soundThread = 0x10;
    p->mode = mode;
    p->overwrite = 1;
    p->focus = mode == PSP_UTILITY_SAVEDATA_LISTSAVE ? PSP_UTILITY_SAVEDATA_FOCUS_LATEST : PSP_UTILITY_SAVEDATA_FOCUS_LATEST;
    strcpy(p->gameName, GAME_NAME);
    strcpy(p->saveName, "0000");
    p->saveNameList = save_names;
    strcpy(p->fileName, DATA_FILE);
    p->dataBuf = &io_buffer;
    p->dataBufSize = sizeof io_buffer;
    p->dataSize = sizeof io_buffer;
    strcpy(p->key, "");
}

/* Pump the dialog to the end; returns its result (0: done). */
static int run_dialog(SceUtilitySavedataParam *p) {
    int done = 0;
    if (sceUtilitySavedataInitStart(p) < 0) return -1;
    while (!done) {
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuFinish();
        sceGuSync(0, 0);
        switch (sceUtilitySavedataGetStatus()) {
        case PSP_UTILITY_DIALOG_VISIBLE: sceUtilitySavedataUpdate(1); break;
        case PSP_UTILITY_DIALOG_QUIT: sceUtilitySavedataShutdownStart(); break;
        case PSP_UTILITY_DIALOG_NONE: done = 1; break;
        default: break;
        }
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
    return p->base.result;
}

int savegame_save_dialog(void) {
    SceUtilitySavedataParam p;
    dialog_common(&p, savegame_auto ? PSP_UTILITY_SAVEDATA_AUTOSAVE : PSP_UTILITY_SAVEDATA_LISTSAVE);
    io_buffer = savegame;
    strncpy(p.sfoParam.title, TITLE, sizeof p.sfoParam.title - 1);
    snprintf(p.sfoParam.savedataTitle, sizeof p.sfoParam.savedataTitle, "Mission %d  %s", savegame.last_mission,
             savegame.last_difficulty ? "Normal" : "Easy");
    snprintf(p.sfoParam.detail, sizeof p.sfoParam.detail, "Missions cleared: %d\nRed Orbs: %d\nBloody Palace best: %d",
             savegame_missions_cleared(), savegame.red_orbs, savegame.bp_best);
    p.sfoParam.parentalLevel = 1;
    if (icon0) { p.icon0FileData.buf = icon0; p.icon0FileData.bufSize = (SceSize)icon0_size; p.icon0FileData.size = (SceSize)icon0_size; }
    return run_dialog(&p) == 0;
}

int savegame_load_dialog(void) {
    SceUtilitySavedataParam p;
    dialog_common(&p, savegame_auto ? PSP_UTILITY_SAVEDATA_AUTOLOAD : PSP_UTILITY_SAVEDATA_LISTLOAD);
    memset(&io_buffer, 0, sizeof io_buffer);
    if (run_dialog(&p) != 0) return 0;
    if (memcmp(io_buffer.magic, "D3PS", 4) || io_buffer.version != SAVEGAME_VERSION) return 0;
    savegame = io_buffer;
    return 1;
}
