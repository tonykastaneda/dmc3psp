/* Mission 20's ending street (room 012), docs/CREDITS_FIGHT.md.
 *
 * EventTbl20, room 12: on enter spawn_group 0 and op 84 [2, 0] (the HUD counter in its kill-count mode); then
 * op 8c [13620] (raw frames), op 7e [2] (the roll's end, the 100-kill flag), op 7e [3] (the freeze), wait 300,
 * op 7e [4], demo 59. The scene's set-up (FUN_00239b60) sees the ending flag (game+10) and builds the staff roll
 * in place of the cockpit, so the counter (FUN_00313a40, digits 3-5) is all the HUD there is. */
#include "credits_fight.h"
#include "enemy.h"
#include "hud.h"
#include <stdio.h>

#define END_WAIT 13620.0f    /* op 8c */
#define AFTER_FREEZE 300.0f  /* the wait between op 7e [3] and op 7e [4] */

static int active, kills, extended, stage;   /* stage 0 fighting, 1 the roll's end (7e 2 and 3), 2 over (7e 4) */
static float clock, since_end;

void credits_fight_reset(void) {
    active = kills = extended = stage = 0;
    clock = since_end = 0.0f;
}

void credits_fight_begin(void) {
    credits_fight_reset();
    active = 1;
}

int credits_fight_active(void) { return active; }
int credits_fight_kills(void) { return kills; }
float credits_fight_clock(void) { return clock; }
int credits_fight_extended(void) { return extended; }
int credits_fight_frozen(void) { return active && stage >= 1; }

void credits_fight_kill(int kind) {
    if (!active || stage >= 1 || kind == ENEMY_LADY) return;
    if (kills < 999) ++kills;   /* the counter shows at most 999 */
}

int credits_fight_update(float frames) {
    if (!active) return 0;
    clock += frames;
    if (stage == 0 && clock >= END_WAIT) {
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

void credits_fight_status(char *out, int size) {
    float at[3] = {0, 0, 0};
    int mode = -1, shots = 0, ally = enemy_ally_state(at, &mode, &shots);
    snprintf(out, (size_t)size, "credits on=%d clock=%.0f kills=%d stage=%d extended=%d ally=%d mode=0x%x shots=%d at=%.0f,%.0f,%.0f",
             active, clock, kills, stage, extended, ally, mode, shots, at[0], at[1], at[2]);
}

void credits_fight_draw(void) {
    if (!active) return;
    hud_draw_kills(kills);
}
