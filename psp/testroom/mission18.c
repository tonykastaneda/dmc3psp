/* Mission 18, "Invading hell", from EventTbl18 (GDATA 256).
 *
 * 403 Lost Souls Nirvana is the hub: its doors 0-8 lead to the bosses in 412-420. Coming back from room 412 + k, the
 * script lights cell k + 1 of the bingo board (BINGO set 888, flags 1-9) and disables door k. A line of lit cells
 * ("the next trial shall be revealed to the one who unites the light") opens the mirror, GATE 501 at (4150, 4125,
 * 1225), to 406, where demo 50 ends the mission.
 *
 * The board's rule is CStageSetBingo's (bingo() below). The mirror is a glow. */
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

/* CStageSetBingo (vtable 0x613d90; FUN_004ed320 / 004ed8b0): cell c (1-9) is lit by flag c; twelve links (0x5dfd00)
 * each join two cells and light once both are; the mirror's flag is set (sound 0x97) when every link of one of seven
 * patterns (0x5dfdc0: a count, then link numbers) is lit. */
static int bingo(void) {
    static const unsigned char links[12][2] = {{4, 6}, {3, 6}, {3, 8}, {9, 8}, {2, 9}, {2, 7}, {4, 7}, {4, 5}, {3, 5},
                                               {1, 3}, {1, 5}, {1, 2}};
    static const signed char patterns[7][8] = {{4, 0, 1, 7, 8}, {3, 8, 9, 10}, {5, 2, 3, 4, 9, 11}, {5, 5, 6, 7, 10, 11},
                                               {7, 0, 1, 2, 3, 4, 5, 6}, {6, 0, 1, 5, 6, 9, 11}, {7, 2, 3, 4, 5, 6, 7, 8}};
    unsigned int lit = 0;
    int i, k;
    for (i = 0; i < 12; ++i)
        if ((beaten >> (links[i][0] - 1) & 1) && (beaten >> (links[i][1] - 1) & 1)) lit |= 1u << i;
    for (i = 0; i < 7; ++i) {
        for (k = 1; k <= patterns[i][0] && (lit >> patterns[i][k] & 1); ++k) {}
        if (k > patterns[i][0]) return 1;
    }
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
