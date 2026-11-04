#ifndef DMC3PSP_TESTROOM_MENU_H
#define DMC3PSP_TESTROOM_MENU_H

/* The game's menus, from MENU.BIN (tools/prepare_menu.py). The flow follows its scenes:
 * CSceneStartMenu (title), CSceneMisSelect (mission select), CSceneMisStart (briefing).
 * See docs/GAME_FLOW.md. */

typedef struct {
    int mission;      /* 1 to 4 */
    int difficulty;   /* 0 Easy, 1 Normal */
    int turbo;
} MenuChoice;

typedef enum { PAUSE_NONE, PAUSE_RESUME, PAUSE_RESTART, PAUSE_QUIT, PAUSE_OPTIONS } PauseAction;

int menu_load(const char *path);      /* 1 on success */
int menu_loaded(void);

/* Runs the flow from the title to Mission Start, with its own frames. pad returns the buttons held,
 * and after_frame, if set, runs after each frame is shown; tests use both. */
typedef unsigned int (*MenuPad)(int frame);
typedef void (*MenuAfterFrame)(int frame);
void menu_run(MenuChoice *choice, MenuPad pad, MenuAfterFrame after_frame);
/* From Quit the Mission: back to Mission Select, skipping the title. */
void menu_run_from_mission_select(MenuChoice *choice, MenuPad pad, MenuAfterFrame after_frame);
void menu_show_loading(void);         /* one frame of Now Loading */
void menu_mission_clear(MenuPad pad, MenuAfterFrame after_frame);  /* until CROSS */
extern void *menu_draw_buffer;        /* the buffer being drawn: main's, kept in step across menu frames */
extern void *menu_draw_buffer;        /* the buffer being drawn: main's, kept in step across menu frames */

/* In game, inside an open display list. */
void pause_open(void);
PauseAction pause_update(unsigned int pressed);
void pause_draw(void);

#endif
