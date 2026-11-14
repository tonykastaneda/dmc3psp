/* Mission 18, "Invading hell", from EventTbl18 (GDATA 256).
 *
 * 403 Lost Souls Nirvana is the hub: its doors 0-8 lead to the bosses in 412-420. Coming back from room 412 + k, the
 * script lights cell k + 1 of the bingo board (BINGO set 888, flags 1-9) and disables door k. A line of lit cells
 * ("the next trial shall be revealed to the one who unites the light") opens the mirror, GATE 501 at (4150, 4125,
 * 1225), to 406, where demo 50 ends the mission.
 *
 * The board's own rule (CStageSetBingo) isn't decoded: the port reads it as a 3 x 3 board in door order (0 1 2 / 3 4 5
 * / 6 7 8), and any full row, column or diagonal opens the mirror. The mirror is a glow. */
#include <math.h>
#include <stdio.h>
#include "mission18.h"
#include "fx.h"
#include "enemy.h"
#include "geryon_boss.h"

static const float mirror_at[3] = {4150.0f, 4125.0f, 1225.0f};
static int room_now = -1;
static unsigned int beaten;   /* bit k: the boss behind 403's door k */
static float glow;

void m18_reset(void) { room_now = -1; beaten = 0; glow = 0.0f; }

void m18_enter(int room) {
    room_now = room;
    /* 419 is Leviathan's heartcore again (st302's layout): the heart isn't an EST group, so it's put where Mission 8
     * puts it (main.c, room 302), with the room's Envies (group 0) round it. */
    if (room == 419 && !(beaten >> 7 & 1)) enemy_add_kind(ENEMY_LEVIATHAN, 4978.0f, 2651.0f);
    /* 417 is 228's ground (its start 0 is 228's start 2): Geryon starts in his second phase there. */
    geryon_boss_start_on_ground(room == 417);
}

void m18_cleared(int room) {
    if (room >= 412 && room <= 420) beaten |= 1u << (room - 412);
}

static int bingo(void) {
    static const unsigned short lines[8] = {0007, 0070, 0700, 0111, 0222, 0444, 0421, 0124};
    int i;
    for (i = 0; i < 8; ++i) if ((beaten & lines[i]) == lines[i]) return 1;
    return 0;
}

int m18_door_shut(int room, int door) { return room == 403 && door >= 0 && door < 9 && (beaten >> door & 1); }

int m18_update(float x, float y, float z) {
    float dx = x - mirror_at[0], dz = z - mirror_at[2];
    glow += 1.0f;
    if (room_now != 403 || !bingo()) return 0;
    return dx * dx + dz * dz < 300.0f * 300.0f && fabsf(y - mirror_at[1]) < 500.0f;
}

void m18_draw(void) {
    float still[3] = {0.0f, 0.0f, 0.0f}, at[3] = {mirror_at[0], mirror_at[1] + 150.0f, mirror_at[2]};
    if (room_now != 403 || !bingo()) return;
    fx_mote(at, still, 260.0f + 30.0f * sinf(glow * 0.08f), 240.0f, 0.05f, 0xc0ffd0a0u);
}

void m18_status(char *out, int size) { snprintf(out, size, "beaten=%03x bingo=%d", beaten, bingo()); }
