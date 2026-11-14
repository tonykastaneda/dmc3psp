#include "mission17.h"

/* 143's area 0 (EVE: four corners and a floor at 3101.6, 3000 high): with counter 0 still 0, demo 47, then
 * goto_room 139, start 0. */
static const float area0[4][2] = {{5099.4f, 4698.8f}, {4814.9f, 4984.6f}, {5433.8f, 5624.9f}, {5753.8f, 5356.8f}};

static int inside(const float q[4][2], float x, float z) {
    int k, pos = 0, neg = 0;
    for (k = 0; k < 4; ++k) {
        const float *a = q[k], *b = q[(k + 1) & 3];
        float c = (b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0]);
        if (c > 0.0f) pos = 1; else if (c < 0.0f) neg = 1;
    }
    return !(pos && neg);
}

void m17_reset(void) {}

/* 128, the God-cube Chamber: its door 2, into 129, is reached by climbing the cubes (STAY sets the port doesn't
 * move): Dante comes into 128 at start 2, by that door, instead of start 0 [approximation]. */
int m17_door_route(int room, int door, int *next_room, int *start) {
    if (room == 133 && door == 0) { *next_room = 128; *start = 2; return 1; }
    return 0;
}

int m17_move(int room, float x, float y, float z, int boss_beaten, int *next_room, int *start) {
    if (room == 143 && !boss_beaten && y > 3101.0f - 100.0f && inside(area0, x, z)) { *next_room = 139; *start = 0; return 1; }
    return 0;
}
