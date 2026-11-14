#ifndef DMC3PSP_TESTROOM_MENU_H
#define DMC3PSP_TESTROOM_MENU_H

/* The game's menus, from MENU.BIN (tools/prepare_menu.py). The flow follows its scenes:
 * CSceneStartMenu (title), CSceneMisSelect (mission select), CSceneMisStart (briefing).
 * See docs/GAME_FLOW.md. */

/* Mission Select's Debug Room: the empty Mission 2 street with the training dummies. */
#define MENU_DEBUG_ROOM 99
typedef struct {
    int mission;      /* 1 to 4, or MENU_DEBUG_ROOM */
    int difficulty;   /* 0 Easy, 1 Normal */
    int turbo;
} MenuChoice;

typedef enum { PAUSE_NONE, PAUSE_RESUME, PAUSE_RESTART, PAUSE_QUIT, PAUSE_OPTIONS } PauseAction;

int menu_load(const char *path);      /* 1 on success */
int menu_loaded(void);
void menu_set_mission_mask(unsigned int mask); /* only generated mission slots are selectable */

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

/* The Divinity Statue's shop: the game's Customize screens. What's bought is kept here; for now it
 * doesn't change Dante (his whole moveset stays unlocked for testing). */
#define SHOP_ITEMS 8   /* Vital Star S, Vital Star L, Devil Star, Holy Water, Blue Orb, Purple Orb, Gold Orb, Yellow Orb */
typedef struct {
    int red_orbs;
    unsigned char owned[SHOP_ITEMS], bought[SHOP_ITEMS];
    unsigned char blue_fragments;
} ShopState;
/* Text for the screens' messages: the game's font isn't on the disc, so main draws it with its own. */
typedef void (*MenuText)(int x, int y, const char *text, unsigned int color);
extern MenuText menu_text;
/* START and SELECT together, anywhere: main's debug menu, run on its own frames. */
typedef void (*MenuDebug)(void);
extern MenuDebug menu_debug;
/* The menus' font, from MENU.BIN (tools/prepare_menu.py), for text written at run time: x, y the top left
 * in PSP pixels, height the line's. Usable inside any open display list; returns the width. */
float menu_font_draw(float x, float y, float height, const char *text, unsigned int color);
float menu_font_width(float height, const char *text);
void shop_open(ShopState *state);
int shop_update(unsigned int pressed);   /* 1 when the player leaves */
void shop_draw(void);                    /* inside an open display list */
/* As a menu of its own, with its own frames (the briefing's Customize). */
/* frame: where a test script's frame count is; returns where it ends. */
int shop_run(ShopState *state, MenuPad pad, MenuAfterFrame after_frame, int frame);
extern ShopState *menu_shop;             /* set to make the briefing's Customize open the shop */

/* In game, inside an open display list. */
void pause_open(void);
PauseAction pause_update(unsigned int pressed);
void pause_draw(void);

#endif
