/* The Game Over prompt and menu (gameover.h). Layout and timing are measured off PCSX2's screen (Mission 2's street,
 * the 100% save, three Yellow Orbs): the orb's face at the screen's middle, a fifth of its height, "REBIRTH YES NO"
 * under it, the orbs held at the bottom right; the menu's three rows left of the middle. The lettering is the port's
 * menu font (the menus' labels are, menu.c), not the game's own art for these words, which isn't located yet. The
 * Game Over object the game's task factory makes (FUN_0023ef80 case 9: vtable 0x6078d0, update FUN_00359120 ->
 * FUN_003591b0 / FUN_00359380) plays GameOver.sfd (the movie table 0x58e1f0, entry 3; 4-7 the other languages) and
 * loops it until it's told continue (1) or quit (-1); the port has no conversion of that movie, so End goes straight
 * to the menus [the movie isn't shown]. */
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>

#include "gameover.h"
#include "menu.h"

enum { GO_OFF, GO_FALL, GO_REBIRTH, GO_MENU };
enum { ROW_CONTINUE, ROW_RESTART, ROW_END, ROWS };
static const char *const row_text[ROWS] = {"CONTINUE", "RESTART THE MISSION", "END"};

static int phase, cursor, confirm, yes, orbs;
static float clock;

#define FALL_FRAMES 90.0f   /* the darkening and the orb's rise, before the prompt (PCSX2: about 1.5 s) */
#define BRIGHT 0xffffffffu
#define DIM 0xff707070u

void gameover_reset(void) { phase = GO_OFF; }
int gameover_active(void) { return phase != GO_OFF; }

void gameover_start(int yellow_orbs) {
    phase = GO_FALL;
    clock = 0.0f;
    orbs = yellow_orbs;
    cursor = ROW_CONTINUE;
    confirm = 0;
    yes = 0;
}

GameOverAction gameover_update(unsigned int pressed, float dt) {
    if (phase == GO_OFF) return GAMEOVER_NONE;
    clock += dt * 60.0f;
    if (phase == GO_FALL) {
        if (clock >= FALL_FRAMES) { phase = orbs > 0 ? GO_REBIRTH : GO_MENU; yes = 0; }
        return GAMEOVER_NONE;
    }
    if (phase == GO_REBIRTH) {
        if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) yes = !yes;
        if (pressed & PSP_CTRL_CROSS) {
            if (yes) { phase = GO_OFF; return GAMEOVER_REBIRTH; }
            phase = GO_MENU; confirm = 0; yes = 0;
        }
        return GAMEOVER_NONE;
    }
    if (confirm) {
        if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) yes = !yes;
        if (pressed & PSP_CTRL_CIRCLE) confirm = 0;
        else if (pressed & PSP_CTRL_CROSS) {
            confirm = 0;
            if (yes) {
                phase = GO_OFF;
                return cursor == ROW_CONTINUE ? GAMEOVER_CONTINUE : cursor == ROW_RESTART ? GAMEOVER_RESTART : GAMEOVER_END;
            }
        }
        return GAMEOVER_NONE;
    }
    if (pressed & PSP_CTRL_UP) cursor = (cursor + ROWS - 1) % ROWS;
    if (pressed & PSP_CTRL_DOWN) cursor = (cursor + 1) % ROWS;
    if (pressed & PSP_CTRL_CROSS) { confirm = 1; yes = 0; }
    return GAMEOVER_NONE;
}

void gameover_draw(void) {
    float k;
    if (phase == GO_OFF) return;
    k = phase == GO_FALL ? clock / FALL_FRAMES : 1.0f;
    if (k > 1.0f) k = 1.0f;
    menu_draw_wash((unsigned int)(k * 215.0f) << 24);   /* the scene stays faintly there (PCSX2: about 85% dark) */
    if (phase == GO_FALL) {
        /* The orb rises from the top into the middle, growing. */
        float t = k * k * (3.0f - 2.0f * k);
        menu_draw_yellow_orb(230.0f + 10.0f * t, 30.0f + 92.0f * t, 20.0f + 36.0f * t, BRIGHT);
        return;
    }
    if (phase == GO_REBIRTH) {
        char text[16];
        float x = 176.0f;
        menu_draw_yellow_orb(240.0f, 122.0f, 56.0f, BRIGHT);
        x += menu_font_draw(x, 156.0f, 18.0f, "REBIRTH", BRIGHT) + 14.0f;
        x += menu_font_draw(x, 156.0f, 18.0f, "YES", yes ? BRIGHT : DIM) + 10.0f;
        menu_font_draw(x, 156.0f, 18.0f, "NO", yes ? DIM : BRIGHT);
        menu_draw_yellow_orb(366.0f, 228.0f, 22.0f, BRIGHT);
        snprintf(text, sizeof text, "%02d", orbs > 99 ? 99 : orbs);
        menu_font_draw(380.0f, 219.0f, 18.0f, text, BRIGHT);
        return;
    }
    {
        int i;
        for (i = 0; i < ROWS; ++i) {
            float y = 100.0f + 17.0f * (float)i;
            float w = menu_font_draw(150.0f, y, 15.0f, row_text[i], i == cursor ? BRIGHT : DIM);
            if (confirm && i == cursor) {
                float x = 150.0f + w + 14.0f;
                x += menu_font_draw(x, y, 15.0f, "YES", yes ? BRIGHT : DIM) + 8.0f;
                menu_font_draw(x, y, 15.0f, "NO", yes ? DIM : BRIGHT);
            }
        }
    }
}
