/* Mission 20's ending street (room 012), docs/CREDITS_FIGHT.md.
 *
 * EventTbl20, room 12: on enter spawn_group 0 and op 84 [2, 0] (the HUD counter in its kill-count mode); then
 * op 8c [13620] (raw frames), op 7e [2] (the roll's end, the 100-kill flag), op 7e [3] (the freeze), wait 300,
 * op 7e [4], demo 59. The scene's set-up (FUN_00239b60) sees the ending flag (game+10) and builds the staff roll
 * (CStaffRoll, FUN_00517b70) in place of the cockpit, so the counter (FUN_00313a40, digits 3-5) is all the HUD there is.
 *
 * The roll: its clock starts at its first record's time (5460) and goes up one a frame; each record (0x5e0d00) makes a
 * page in slot index % 3 once its time is passed. A page (CUIDStaffRoll, 0x518430) fades in at 4 a frame to 0x40,
 * holds for its duration (-1: for good), and fades out at 4 a frame. The pages are pictures of the whole screen
 * (tools/prepare_staffroll.py: STAFF_nn.BIN). After op 7e [2] the roll fades the scene out (+0x4c, 2 a frame to 0x80,
 * drawn by 0x518110) and, with 100 kills, shows the card (id 4536) for good. */
#include "credits_fight.h"
#include "enemy.h"
#include "hud.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

#define END_WAIT 13620.0f    /* op 8c */
#define AFTER_FREEZE 300.0f  /* the wait between op 7e [3] and op 7e [4] */
#define ROLL_START 5460.0f   /* FUN_00517b70: the clock starts at record 0's time */
#define PAGE_ALPHA 0x40      /* a page's full alpha (0x518430); taken as fully drawn [inferred: the model's alpha x2] */
#define FADE_FULL 0x80       /* the roll's fade, 2 a frame (0x517c90 state 5) */
#define CARD 35              /* STAFF_35.BIN: id 4536, the 100-kill card */

/* CStaffRoll's records (0x5e0d00): when the page comes (on the roll's clock) and how long it holds. */
static const struct { float time, hold; } records[35] = {
    {5460, 300}, {5820, 360}, {6180, 360}, {6600, 360}, {6960, 360}, {7380, 360}, {7740, 360}, {8160, 360},
    {8520, 360}, {8880, 360}, {9240, 360}, {9720, 360}, {10080, 360}, {10440, 360}, {10800, 360}, {11220, 360},
    {11580, 360}, {12000, 360}, {12360, 360}, {12780, 360}, {13140, 360}, {13560, 360}, {13920, 360}, {14340, 360},
    {14700, 360}, {15060, 360}, {15420, 360}, {15840, 360}, {16260, 360}, {16620, 360}, {16980, 360}, {17400, 360},
    {17760, 360}, {18120, 360}, {18540, -1}};

#define TEX_W 512
#define TEX_H 448
typedef struct {
    unsigned char *tex;          /* 512 x 512 (the GE's power of two), the picture's 448 rows */
    unsigned int clut[256] __attribute__((aligned(16)));
    int loaded;                  /* the STAFF file in it, -1 none */
    int state;                   /* 0 none, 1 fading in, 2 holding, 3 fading out */
    int alpha;                   /* +0x6c8 */
    float hold;                  /* +0x6c0 */
} Page;

static char dir[256];
static Page pages[3];            /* the roll's slots, +0x3c */
static int active, kills, extended, stage;   /* stage 0 fighting, 1 the roll's end (7e 2 and 3), 2 over (7e 4) */
static int next_record, fade, card_shown;
static float street_clock, roll_clock, since_end, roll_acc;

void credits_fight_init(const char *game_dir) {
    snprintf(dir, sizeof dir, "%s", game_dir);
}

static void pages_free(void) {
    int i;
    for (i = 0; i < 3; ++i) { free(pages[i].tex); pages[i].tex = NULL; pages[i].loaded = -1; pages[i].state = 0; }
}

/* STAFF_nn.BIN into a slot; 0 if it isn't there (the build makes them from the disc). */
static int page_load(Page *p, int n) {
    char path[320];
    unsigned char head[8];
    SceUID fd;
    int ok;
    if (p->loaded == n) return 1;
    if (!p->tex && !(p->tex = memalign(16, TEX_W * 512))) return 0;
    snprintf(path, sizeof path, "%s/STAFF_%02d.BIN", dir, n);
    if ((fd = sceIoOpen(path, PSP_O_RDONLY, 0)) < 0) return 0;
    ok = sceIoRead(fd, head, 8) == 8 && memcmp(head, "STF1", 4) == 0 &&
         sceIoRead(fd, p->clut, sizeof p->clut) == (int)sizeof p->clut &&
         sceIoRead(fd, p->tex, TEX_W * TEX_H) == TEX_W * TEX_H;
    sceIoClose(fd);
    if (!ok) { p->loaded = -1; return 0; }
    memset(p->tex + TEX_W * TEX_H, 0, TEX_W * (512 - TEX_H));
    sceKernelDcacheWritebackRange(p->tex, TEX_W * 512);
    sceKernelDcacheWritebackRange(p->clut, sizeof p->clut);
    p->loaded = n;
    return 1;
}

static void page_show(Page *p, int n, float hold) {
    if (!page_load(p, n)) return;
    p->state = 1; p->alpha = 0; p->hold = hold;
}

void credits_fight_reset(void) {
    active = kills = extended = stage = 0;
    next_record = fade = card_shown = 0;
    street_clock = since_end = roll_acc = 0.0f;
    roll_clock = ROLL_START;
    pages_free();
}

void credits_fight_begin(void) {
    credits_fight_reset();
    active = 1;
}

int credits_fight_active(void) { return active; }
int credits_fight_kills(void) { return kills; }
float credits_fight_clock(void) { return street_clock; }
int credits_fight_extended(void) { return extended; }
int credits_fight_frozen(void) { return active && stage >= 1; }

void credits_fight_kill(int kind) {
    if (!active || stage >= 1 || kind == ENEMY_LADY) return;
    if (kills < 999) ++kills;   /* the counter shows at most 999 */
}

/* 0x518430, a frame at a time. */
static void page_step(Page *p) {
    switch (p->state) {
    case 1: if ((p->alpha += 4) >= PAGE_ALPHA) { p->alpha = PAGE_ALPHA; p->state = 2; } break;
    case 2: if (p->hold >= 0.0f && (p->hold -= 1.0f) < 0.0f) p->state = 3; break;
    case 3: if ((p->alpha -= 4) <= 0) { p->alpha = 0; p->state = 0; } break;
    }
}

static void roll_frame(void) {
    int i;
    roll_clock += 1.0f;
    /* 0x517c90 states 1-3: the next record's page, in slot index % 3 once its time is passed and the slot is free. */
    if (next_record < 35) {
        Page *p = &pages[next_record % 3];
        if (p->state == 0) {
            page_load(p, next_record);   /* ahead, as the roll loads the next file once it's made the last page */
            if (records[next_record].time < roll_clock) { page_show(p, next_record, records[next_record].hold); ++next_record; }
        }
    }
    if (stage >= 1) {
        /* States 5-7: the fade, then (with 100 kills) the card for good. */
        if (fade < FADE_FULL) fade += 2;
        else if (extended && !card_shown) {
            Page *p = &pages[next_record % 3];   /* a free slot: the roll's last page holds the one before */
            for (i = 0; i < 3 && pages[i].state != 0; ++i) ;
            if (i < 3) p = &pages[i];
            page_show(p, CARD, -1.0f);
            card_shown = 1;
        }
    }
    for (i = 0; i < 3; ++i) page_step(&pages[i]);
}

int credits_fight_update(float frames) {
    if (!active) return 0;
    street_clock += frames;
    /* The roll runs on whole frames, as the game's tasks do. */
    for (roll_acc += frames; roll_acc >= 1.0f; roll_acc -= 1.0f) roll_frame();
    if (stage == 0 && street_clock >= END_WAIT) {
        /* op 7e [2]: the counter against 100 (roll +0x57); op 7e [3] on the same frame: the freeze. */
        extended = kills >= 100;
        stage = 1;
        since_end = 0.0f;
    } else if (stage == 1) {
        since_end += frames;
        if (since_end >= AFTER_FREEZE) stage = 2;   /* op 7e [4]; demo 59 follows */
    }
    return stage == 2;
}

/* Tests: run the street to `frames` on its clock at once (the roll's pages with it; no draws). */
void credits_fight_skip(float frames) {
    while (active && street_clock + 1.0f <= frames) credits_fight_update(1.0f);
}

void credits_fight_test_kills(int n) { kills = n; }

void credits_fight_status(char *out, int size) {
    float at[3] = {0, 0, 0};
    int mode = -1, shots = 0, ally = enemy_ally_state(at, &mode, &shots);
    snprintf(out, (size_t)size, "credits on=%d clock=%.0f roll=%.0f page=%d kills=%d stage=%d extended=%d fade=%d card=%d ally=%d mode=0x%x shots=%d at=%.0f,%.0f,%.0f",
             active, street_clock, roll_clock, next_record, kills, stage, extended, fade, card_shown, ally, mode, shots, at[0], at[1], at[2]);
}

typedef struct { float u, v, x, y, z; } V2;
typedef struct { unsigned int color; float x, y, z; } C2;

void credits_fight_draw(void) {
    int i;
    if (!active) return;
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    if (fade > 0) {
        /* 0x518110: a 512 x 256 quad over the screen, its alpha +0x4c (black in the capture). */
        C2 *q = sceGuGetMemory(sizeof(C2) * 2);
        unsigned int a = (unsigned int)(fade >= FADE_FULL ? 255 : fade * 2);
        sceGuDisable(GU_TEXTURE_2D);
        q[0].color = q[1].color = a << 24;
        q[0].x = 0; q[0].y = 0; q[0].z = 0; q[1].x = 480; q[1].y = 272; q[1].z = 0;
        sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    for (i = 0; i < 3; ++i) {
        Page *p = &pages[i];
        V2 *v;
        if (p->state == 0 || p->loaded < 0) continue;
        /* The page's quad covers the whole 2D screen (512 x 384 units, UV rows 0-448): 4:3, centred, as the PCSX2
         * capture shows it on its 16:9 picture. */
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(256 / 8, p->clut);
        sceGuTexMode(GU_PSM_T8, 0, 0, 0);
        sceGuTexImage(0, TEX_W, 512, TEX_W, p->tex);
        sceGuTexFlush();
        sceGuColor(((unsigned int)(p->alpha * 255 / PAGE_ALPHA) << 24) | 0x00ffffffu);
        v = sceGuGetMemory(sizeof(V2) * 2);
        v[0].u = 0; v[0].v = 0; v[0].x = 240.0f - 272.0f * 2.0f / 3.0f; v[0].y = 0; v[0].z = 0;
        v[1].u = TEX_W; v[1].v = TEX_H; v[1].x = 240.0f + 272.0f * 2.0f / 3.0f; v[1].y = 272; v[1].z = 0;
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    }
    sceGuColor(0xffffffffu);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuDisable(GU_BLEND);
    hud_draw_kills(kills);
}
