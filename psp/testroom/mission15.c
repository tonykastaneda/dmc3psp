#include "mission15.h"

#include "fx.h"

#include <math.h>
#include <stdio.h>

/* Counter 4 picks one pairing of the six coloured doors (EventTbl15's door_retarget switches, read together):
 *   0: 222-202, 212-211, 226-227      1: 222-227, 202-212, 211-226      2: 222-211, 202-226, 212-227
 * Each room's coloured door is one of its doors, and arriving through one puts Dante at that room's start by it. */
typedef struct { short room, door, start; } ColourDoor;
static const ColourDoor colour_doors[] = {
    {222, 0, 0}, {202, 2, 0}, {212, 3, 1}, {211, 3, 1}, {226, 1, 1}, {227, 0, 0}
};
static const short pairs[3][6][2] = {
    {{222, 202}, {202, 222}, {212, 211}, {211, 212}, {226, 227}, {227, 226}},
    {{222, 227}, {227, 222}, {202, 212}, {212, 202}, {211, 226}, {226, 211}},
    {{222, 211}, {211, 222}, {202, 226}, {226, 202}, {212, 227}, {227, 212}},
};
/* The flame portals: the SWITCH sets' places in the rooms' # GAME text. */
/* above: how far over it Dante may stand to strike it. 212's hangs below its 850 walkway, over a floor the port's
 * collision doesn't have, so it's struck from the walkway [approximation]. */
typedef struct { short room; float x, y, z, above; } Portal;
static const Portal portals[] = {
    {200, 1990.0f, 230.0f, 3220.0f, 150.0f}, {202, 3503.0f, 145.0f, 2913.0f, 150.0f}, {212, 3510.0f, 40.0f, 3280.0f, 900.0f},
    {226, 16168.5f, 2736.9f, 7506.4f, 150.0f}, {227, 7145.0f, 170.0f, -1425.0f, 150.0f}
};
#define PORTAL_R 80.0f
#define PORTAL_WAIT 120.0f   /* [estimate] the script's camera event (about 2 s) before another strike counts */

static int room_now, counter, lit_200, struck;
static float wait;

void m15_reset(void) { room_now = 0; counter = 0; lit_200 = 0; struck = 0; wait = 0.0f; }
void m15_enter(int room) { room_now = room; wait = 0.0f; }
void m15_update(float dt) { if (wait > 0.0f) wait -= dt * 60.0f; }
void m15_status(char *out, int size) {
    snprintf(out, size, "m15 room=%d counter=%d portal200=%d struck=%d", room_now, counter, lit_200, struck);
}

void m15_hit(float x, float y, float z, float yaw, float reach) {
    unsigned i;
    if (wait > 0.0f) return;
    for (i = 0; i < sizeof(portals) / sizeof(portals[0]); ++i) {
        const Portal *p = &portals[i];
        float dx = p->x - x, dz = p->z - z, d = sqrtf(dx * dx + dz * dz), dy = p->y - y;
        if (p->room != room_now || d > reach + PORTAL_R || dy < -p->above || dy > 300.0f) continue;
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        if (p->room == 202 && !lit_200) continue;   /* gimmick 300 stands in its place until flag 2 */
        {
            float at[3] = {p->x, p->y, p->z};
            fx_explosion(at);
        }
        ++struck;
        wait = PORTAL_WAIT;
        if (p->room == 200) lit_200 = 1;            /* 200: flag 2 */
        else counter = (counter + 1) % 3;           /* sw_counter 4: 0 -> 1 -> 2 -> 0 */
        return;
    }
}

int m15_door_route(int room, int door, int *next_room, int *start) {
    unsigned i, j;
    for (i = 0; i < sizeof(colour_doors) / sizeof(colour_doors[0]); ++i) {
        if (colour_doors[i].room != room || colour_doors[i].door != door) continue;
        for (j = 0; j < 6; ++j) if (pairs[counter][j][0] == room) {
            int to = pairs[counter][j][1];
            unsigned k;
            for (k = 0; k < sizeof(colour_doors) / sizeof(colour_doors[0]); ++k) if (colour_doors[k].room == to) {
                *next_room = to;
                *start = colour_doors[k].start;
                return 1;
            }
        }
    }
    return 0;
}
