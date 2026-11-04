#include "menu.h"

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <psputils.h>

#include <malloc.h>
#include <string.h>

#include "menu_runtime.h"

#define SCR_W 480
#define SCR_H 272
/* The art fills +-256 x +-192 in the game's screen units, at 4:3. Here heights fit the screen and the
 * layout spreads to its width: an item's centre moves out, but the item keeps its true proportions. */
#define UNIT ((float)MENU_ART_H / 384.0f)
#define SPREAD (((float)SCR_W / UNIT / 2.0f) / 256.0f)
#define ART_LEFT 0
#define FADE_FRAMES 15
#define DIM 0xff707070
#define BRIGHT 0xffffffff
#define LOCKED 0x60ffffff
#define MENU_MISSIONS 4

typedef struct {
    char magic[4];
    unsigned int images, atlases, groups, quads;
    unsigned int image_table, atlas_table, group_table, quad_table;
} Header;
typedef struct { unsigned int width, height, palette, pixels; } Atlas;
typedef struct { unsigned int first, count; } Group;
typedef struct { unsigned int atlas; float u0, v0, u1, v1, x0, y0, x1, y1; } Quad;
typedef struct { float u, v; unsigned int color; float x, y, z; } Vertex;

/* Quads by position in their group, from the disc's layout (--dump in prepare_menu.py). */
enum { TITLE_NEW, TITLE_LOAD, TITLE_OPTIONS, TITLE_ITEMS };
enum { LABEL_NORMAL = 1, LABEL_EASY = 12 };
enum { TAG_TURBO = 0 };
enum { ROWS_SHOWN = 2 };
/* The briefing strip holds the bar and both labels on one texture and one quad; the labels' places
 * are read off the HD Collection. */
static const float BRIEFING_BAR[8] = {0.0f, 0.0f, 1.0f, 0.494f, -269.0f, -203.0f, 265.0f, -117.0f};
static const float BRIEFING_START[8] = {0.02f, 0.58f, 0.75f, 0.76f, -16.0f, -170.0f, 120.0f, -136.0f};
static const float BRIEFING_CUSTOMIZE[8] = {0.02f, 0.78f, 0.56f, 0.94f, 133.0f, -170.0f, 233.0f, -136.0f};
enum { PAUSE_BACK_LEFT, PAUSE_BACK_RIGHT, PAUSE_STATUS, PAUSE_SAVE, PAUSE_QUIT_ITEM, PAUSE_RESTART_ITEM, PAUSE_EXIT, PAUSE_OPTIONS_ITEM, PAUSE_YES, PAUSE_NO };
static const int pause_order[] = {PAUSE_STATUS, PAUSE_SAVE, PAUSE_QUIT_ITEM, PAUSE_RESTART_ITEM, PAUSE_OPTIONS_ITEM, PAUSE_EXIT};
#define PAUSE_ROWS 6

static unsigned char *blob;
static const Header *header;
static unsigned int __attribute__((aligned(16))) list[32768];
static int pause_cursor, pause_confirm, pause_yes;
void *menu_draw_buffer;
void *menu_draw_buffer;

int menu_loaded(void) { return header != 0; }

int menu_load(const char *path) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    if (fd < 0) return 0;
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    blob = memalign(16, (size_t)size);
    if (!blob) { sceIoClose(fd); return 0; }
    got = sceIoRead(fd, blob, (unsigned int)size);
    sceIoClose(fd);
    if (got != size || memcmp(blob, "MNU1", 4) != 0) {
        free(blob);
        blob = 0;
        return 0;
    }
    sceKernelDcacheWritebackRange(blob, (unsigned int)size);
    header = (const Header *)blob;
    return 1;
}

static const Quad *group_quad(int group, int index) {
    const Group *g = (const Group *)(blob + header->group_table) + group;
    return (const Quad *)(blob + header->quad_table) + g->first + index;
}

/* 2D drawing takes texture coordinates in pixels. */
static float texture_w = 1.0f, texture_h = 1.0f;

static void bind_atlas(unsigned int index) {
    const Atlas *a = (const Atlas *)(blob + header->atlas_table) + index;
    texture_w = (float)a->width;
    texture_h = (float)a->height;
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(256 / 8, blob + a->palette);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, (int)a->width, (int)a->height, (int)a->width, blob + a->pixels);
    sceGuTexFlush();
}

static void sprite(float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, unsigned int color) {
    Vertex *v = sceGuGetMemory(sizeof(Vertex) * 2);
    v[0].u = u0 * texture_w; v[0].v = v0 * texture_h; v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0.0f;
    v[1].u = u1 * texture_w; v[1].v = v1 * texture_h; v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

/* Game screen units (Y up, origin at the centre) to PSP pixels. flip mirrors the texture. */
static void unit_quad(float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, unsigned int color, int flip) {
    float centre = (x0 + x1) * 0.5f * SPREAD, half = (x1 - x0) * 0.5f;
    sprite(flip ? u1 : u0, v0, flip ? u0 : u1, v1,
           SCR_W / 2.0f + (centre - half) * UNIT, SCR_H / 2.0f - y1 * UNIT,
           SCR_W / 2.0f + (centre + half) * UNIT, SCR_H / 2.0f - y0 * UNIT, color);
}

/* For bands and backdrops: right across the screen. */
static void span_quad(float u0, float v0, float u1, float v1, float y0, float y1, unsigned int color) {
    sprite(u0, v0, u1, v1, 0.0f, SCR_H / 2.0f - y1 * UNIT, (float)SCR_W, SCR_H / 2.0f - y0 * UNIT, color);
}

static void draw_quad_offset(int group, int index, unsigned int color, float dx, float dy, int flip) {
    const Quad *q = group_quad(group, index);
    bind_atlas(q->atlas);
    unit_quad(q->u0, q->v0, q->u1, q->v1, q->x0 + dx, q->y0 + dy, q->x1 + dx, q->y1 + dy, color, flip);
}

static void draw_quad(int group, int index, unsigned int color) { draw_quad_offset(group, index, color, 0.0f, 0.0f, 0); }

/* The briefing strip's pieces are all on one atlas: the quad gives which. */
static void draw_briefing_piece(const float *p, unsigned int color) {
    bind_atlas(group_quad(GROUP_BRIEFING, 0)->atlas);
    if (p == BRIEFING_BAR) span_quad(p[0], p[1], p[2], p[3], p[5], p[7], color);
    else unit_quad(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], color, 0);
}

static void draw_image(int index) {
    const unsigned int *table = (const unsigned int *)(blob + header->image_table);
    sceGuTexMode(GU_PSM_5650, 0, 0, 0);
    sceGuTexImage(0, 512, 512, MENU_IMAGE_W, blob + table[index]);
    sceGuTexFlush();
    texture_w = texture_h = 1.0f;
    sprite(0.0f, 0.0f, (float)MENU_ART_W, (float)MENU_ART_H,
           (float)ART_LEFT, 0.0f, (float)(ART_LEFT + MENU_ART_W), (float)MENU_ART_H, BRIGHT);
}

static void begin_2d(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

/* Back to what the game's own drawing assumes. */
static void end_2d(void) {
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
}

static void fade_overlay(float amount) {
    if (amount <= 0.0f) return;
    sceGuDisable(GU_TEXTURE_2D);
    sprite(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, SCR_W, SCR_H, (unsigned int)(amount * 255.0f) << 24);
    sceGuEnable(GU_TEXTURE_2D);
}

typedef enum { SCREEN_TITLE, SCREEN_MISSIONS, SCREEN_BRIEFING, SCREEN_DONE } Screen;

static void draw_title(int cursor) {
    int i;
    draw_image(IMG_TITLE);
    for (i = 0; i < TITLE_ITEMS; ++i)
        draw_quad(GROUP_TITLE_OPTIONS, i, i == TITLE_OPTIONS ? LOCKED : i == cursor ? BRIGHT : DIM);
}

/* Easy and Normal only, for now. Row 0's arrows and digit sit one row-height above row 1's. */
static void draw_missions(const MenuChoice *c, int row) {
    static const int labels[ROWS_SHOWN] = {LABEL_EASY, LABEL_NORMAL};
    const float row_height = 26.7f;
    int i;
    draw_image(IMG_MISSION);
    for (i = 0; i < ROWS_SHOWN; ++i) {
        unsigned int color = i == row ? BRIGHT : DIM;
        draw_quad(GROUP_MISSION_ROWS, i, BRIGHT);
        draw_quad(GROUP_MISSION_LABELS, labels[i], color);
        if (i == row) {
            draw_quad_offset(GROUP_MISSION_ARROWS, i * 2, BRIGHT, 0.0f, 0.0f, 0);
            draw_quad_offset(GROUP_MISSION_ARROWS, i * 2 + 1, BRIGHT, 0.0f, 0.0f, 1);
        }
        draw_quad_offset(GROUP_MISSION_DIGITS, c->mission - 1, color, 0.0f, -row_height * (float)i, 0);
    }
    if (c->turbo) draw_quad(GROUP_MISSION_TAGS, TAG_TURBO, BRIGHT);
}

static void draw_briefing(const MenuChoice *c, int cursor) {
    draw_image(IMG_BANNER_1 + c->mission - 1);
    draw_briefing_piece(BRIEFING_BAR, BRIGHT);
    draw_briefing_piece(BRIEFING_START, cursor == 0 ? BRIGHT : DIM);
    draw_briefing_piece(BRIEFING_CUSTOMIZE, LOCKED);
}

static void flow(MenuChoice *c, Screen screen, MenuPad pad, MenuAfterFrame after_frame) {
    static SceCtrlData data;
    unsigned int held = 0xffffffffu;   /* a button down when this starts isn't a press */
    int frame = 0, title_cursor = TITLE_NEW, row = c->difficulty, fade = FADE_FRAMES, leaving = 0;
    Screen next = screen;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    while (screen != SCREEN_DONE) {
        unsigned int now, pressed;
        if (pad) {
            now = pad(frame);
        } else {
            sceCtrlReadBufferPositive(&data, 1);
            now = data.Buttons;
        }
        pressed = now & ~held;
        held = now;
        if (frame % 300 == 0) scePowerTick(PSP_POWER_TICK_ALL);
        if (!leaving && fade == 0) {
            switch (screen) {
            case SCREEN_TITLE:
                if (pressed & PSP_CTRL_LEFT) title_cursor = title_cursor == TITLE_NEW ? TITLE_LOAD : TITLE_NEW;
                if (pressed & PSP_CTRL_RIGHT) title_cursor = title_cursor == TITLE_LOAD ? TITLE_NEW : TITLE_LOAD;
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) {
                    /* No saves yet: both lead to Mission Select. New Game starts at the first mission. */
                    if (title_cursor == TITLE_NEW) c->mission = 1;
                    next = SCREEN_MISSIONS;
                    leaving = 1;
                }
                break;
            case SCREEN_MISSIONS:
                if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) row = !row;
                if (pressed & PSP_CTRL_LEFT) c->mission = c->mission > 1 ? c->mission - 1 : MENU_MISSIONS;
                if (pressed & PSP_CTRL_RIGHT) c->mission = c->mission < MENU_MISSIONS ? c->mission + 1 : 1;
                if (pressed & PSP_CTRL_SQUARE) c->turbo = !c->turbo;
                if (pressed & PSP_CTRL_CROSS) { c->difficulty = row; next = SCREEN_BRIEFING; leaving = 1; }
                if (pressed & PSP_CTRL_CIRCLE) { next = SCREEN_TITLE; leaving = 1; }
                break;
            case SCREEN_BRIEFING:
                if (pressed & PSP_CTRL_CROSS) { next = SCREEN_DONE; leaving = 1; }
                if (pressed & PSP_CTRL_CIRCLE) { next = SCREEN_MISSIONS; leaving = 1; }
                break;
            default:
                break;
            }
        }
        if (leaving) {
            if (++fade >= FADE_FRAMES) { screen = next; leaving = 0; }
        } else if (fade > 0) {
            --fade;
        }
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        begin_2d();
        switch (screen) {
        case SCREEN_TITLE: draw_title(title_cursor); break;
        case SCREEN_MISSIONS: draw_missions(c, row); break;
        case SCREEN_BRIEFING: draw_briefing(c, 0); break;
        default: break;
        }
        fade_overlay((float)fade / (float)FADE_FRAMES);
        end_2d();
        sceGuFinish();
        sceGuSync(0, 0);
        if (after_frame) after_frame(frame);
        sceDisplayWaitVblankStart();
        menu_draw_buffer = sceGuSwapBuffers();
        ++frame;
    }
}

void menu_run(MenuChoice *choice, MenuPad pad, MenuAfterFrame after_frame) { flow(choice, SCREEN_TITLE, pad, after_frame); }

void menu_run_from_mission_select(MenuChoice *choice, MenuPad pad, MenuAfterFrame after_frame) { flow(choice, SCREEN_MISSIONS, pad, after_frame); }

void menu_mission_clear(MenuPad pad, MenuAfterFrame after_frame) {
    static SceCtrlData data;
    unsigned int held = 0xffffffffu;
    int frame = 0, fade = FADE_FRAMES, leaving = 0;
    for (;;) {
        unsigned int now, pressed;
        if (pad) now = pad(frame);
        else { sceCtrlReadBufferPositive(&data, 1); now = data.Buttons; }
        pressed = now & ~held;
        held = now;
        if (!leaving && fade == 0 && (pressed & PSP_CTRL_CROSS)) leaving = 1;
        if (leaving) { if (++fade >= FADE_FRAMES) break; }
        else if (fade > 0) --fade;
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        begin_2d();
        draw_image(IMG_CLEAR);
        fade_overlay((float)fade / (float)FADE_FRAMES);
        end_2d();
        sceGuFinish();
        sceGuSync(0, 0);
        if (after_frame) after_frame(frame);
        sceDisplayWaitVblankStart();
        menu_draw_buffer = sceGuSwapBuffers();
        ++frame;
    }
}

void menu_show_loading(void) {
    const Atlas *a = (const Atlas *)(blob + header->atlas_table) + ATLAS_NOW_LOADING;
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    begin_2d();
    bind_atlas(ATLAS_NOW_LOADING);
    /* The logo is the top half of its picture, whose bottom holds effect sprites. */
    sprite(0.0f, 0.0f, 1.0f, 0.45f, SCR_W - (float)a->width - 16.0f, SCR_H - 74.0f, SCR_W - 16.0f, SCR_H - 16.0f, BRIGHT);
    end_2d();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    menu_draw_buffer = sceGuSwapBuffers();
}

void pause_open(void) {
    pause_cursor = 0;
    pause_confirm = 0;
    pause_yes = 0;
}

static int pause_selectable(int row) { return pause_order[row] != PAUSE_STATUS && pause_order[row] != PAUSE_SAVE; }

PauseAction pause_update(unsigned int pressed) {
    if (pause_confirm) {
        if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) pause_yes = !pause_yes;
        if (pressed & PSP_CTRL_CIRCLE) pause_confirm = 0;
        if (pressed & PSP_CTRL_CROSS) {
            int item = pause_order[pause_cursor];
            pause_confirm = 0;
            if (pause_yes) return item == PAUSE_QUIT_ITEM ? PAUSE_QUIT : PAUSE_RESTART;
        }
        return PAUSE_NONE;
    }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) return PAUSE_RESUME;
    if (pressed & PSP_CTRL_UP) pause_cursor = (pause_cursor + PAUSE_ROWS - 1) % PAUSE_ROWS;
    if (pressed & PSP_CTRL_DOWN) pause_cursor = (pause_cursor + 1) % PAUSE_ROWS;
    if ((pressed & PSP_CTRL_CROSS) && pause_selectable(pause_cursor)) {
        switch (pause_order[pause_cursor]) {
        case PAUSE_QUIT_ITEM:
        case PAUSE_RESTART_ITEM:
            pause_confirm = 1;
            pause_yes = 0;
            break;
        case PAUSE_OPTIONS_ITEM: return PAUSE_OPTIONS;
        default: return PAUSE_RESUME;
        }
    }
    return PAUSE_NONE;
}

void pause_draw(void) {
    int i;
    if (!header) return;
    begin_2d();
    sceGuDisable(GU_TEXTURE_2D);
    sprite(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, SCR_W, SCR_H, 0xa0000000);
    sceGuEnable(GU_TEXTURE_2D);
    {
        /* The two halves of the backdrop, meeting in the middle of the screen. */
        const Quad *l = group_quad(GROUP_PAUSE, PAUSE_BACK_LEFT), *r = group_quad(GROUP_PAUSE, PAUSE_BACK_RIGHT);
        bind_atlas(l->atlas);
        sprite(l->u0, l->v0, l->u1, l->v1, 0.0f, SCR_H / 2.0f - l->y1 * UNIT, SCR_W / 2.0f, SCR_H / 2.0f - l->y0 * UNIT, 0xc0ffffff);
        sprite(r->u0, r->v0, r->u1, r->v1, SCR_W / 2.0f, SCR_H / 2.0f - r->y1 * UNIT, (float)SCR_W, SCR_H / 2.0f - r->y0 * UNIT, 0xc0ffffff);
    }
    if (pause_confirm) {
        draw_quad(GROUP_PAUSE, pause_order[pause_cursor], BRIGHT);
        draw_quad_offset(GROUP_PAUSE, PAUSE_YES, pause_yes ? BRIGHT : DIM, 0.0f, -pause_confirm * 40.0f, 0);
        draw_quad_offset(GROUP_PAUSE, PAUSE_NO, pause_yes ? DIM : BRIGHT, 40.0f, -pause_confirm * 40.0f, 0);
    } else {
        for (i = 0; i < PAUSE_ROWS; ++i)
            draw_quad(GROUP_PAUSE, pause_order[i], i == pause_cursor ? BRIGHT : pause_selectable(i) ? DIM : LOCKED);
    }
    end_2d();
    sceGuEnable(GU_DEPTH_TEST);
}
