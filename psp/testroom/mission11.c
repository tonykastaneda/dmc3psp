/* Mission 11, "Revenge", from EventTbl11 (GDATA 249): 214, the Altar of Evil. Two SLIDE sets on its balcony, 300 at
 * (1475, 900, 2600) and 301 at (3525, 900, 5200), are pushed along -z by melee from behind (the walkthrough: "attack
 * this block out from behind... until it is fitted in place of the hole") until they've gone their dist (1400 and
 * 4000), into the holes at z 1200. Each sets its flag (32, 33; counter 5). With both in, gimmick 200 opens the hidden
 * passage (the port hides STAY 200), door 1 (to 239) is enabled, and group 2 runs. Until then door 0 is locked and
 * door 1 disabled (block 1).
 *
 * Estimates: a hit pushes a block 300 x its zure (1.5, 1.0) over a quarter of a second; the blocks are drawn as
 * stone boxes (model 4 isn't converted) and don't block Dante. */
#include <math.h>
#include <stdio.h>
#include <pspgu.h>
#include <pspgum.h>
#include "mission11.h"
#include "fx.h"
#include "stage.h"

#define PASSAGE_TAG (128 + 200 - 100)   /* STAY 200, tools/gimmicks.py story_tag */
#define PUSH_FRAMES 15.0f

typedef struct { float x, y, z, start_z, dist, zure, push, moved; int fitted; } Block;
static const float block_place[2][5] = {{1475.0f, 900.0f, 2600.0f, 1400.0f, 1.5f}, {3525.0f, 900.0f, 5200.0f, 4000.0f, 1.0f}};
static Block blocks[2];
static int room_now = -1;

void m11_reset(void) {
    int i;
    for (i = 0; i < 2; ++i) {
        Block *b = &blocks[i];
        b->x = block_place[i][0]; b->y = block_place[i][1]; b->z = b->start_z = block_place[i][2];
        b->dist = block_place[i][3]; b->zure = block_place[i][4];
        b->push = b->moved = 0.0f; b->fitted = 0;
    }
    room_now = -1;
}

static int both_in(void) { return blocks[0].fitted && blocks[1].fitted; }

void m11_enter(int room) {
    room_now = room;
    if (room == 214 && both_in()) stage_hide_group(PASSAGE_TAG);
}

int m11_door_shut(int room, int door) {
    if (room != 214 || both_in()) return 0;
    return door == 0 || door == 1;
}

int m11_script_held(int room) { return room == 214 && !both_in(); }

void m11_update(float dt) {
    int i, was = both_in();
    float frames = dt * 60.0f;
    if (room_now != 214) return;
    for (i = 0; i < 2; ++i) {
        Block *b = &blocks[i];
        float step;
        if (b->fitted || b->push <= 0.0f) continue;
        step = fminf(b->push, 300.0f * b->zure / PUSH_FRAMES * frames);
        b->push -= step;
        b->moved += step;
        if (b->moved >= b->dist) {
            b->moved = b->dist; b->push = 0.0f; b->fitted = 1;
            {
                float at[3] = {b->x, b->y, b->start_z - b->dist};
                fx_dust(at);
            }
        }
        b->z = b->start_z - b->moved;
    }
    if (!was && both_in()) {
        float at[3] = {5600.0f, 300.0f, 1800.0f};
        stage_hide_group(PASSAGE_TAG);   /* gimmick_cmd2c [200, 1, 0]: the passage opens */
        fx_explosion(at);
    }
}

void m11_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    int i;
    if (room_now != 214) return;
    for (i = 0; i < 2; ++i) {
        Block *b = &blocks[i];
        float dx = b->x - x, dz = b->z - z, d = sqrtf(dx * dx + dz * dz);
        if (b->fitted || d > reach + 80.0f || fabsf(b->y - y) > 300.0f) continue;
        if (z < b->z) continue;                                   /* from behind: the +z side */
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        b->push += heavy ? 600.0f * b->zure / 2.0f : 300.0f * b->zure;
        {
            float at[3] = {b->x, b->y + 100.0f, b->z + 50.0f};
            fx_hit_spark(at);
        }
    }
}

typedef struct { unsigned int color; float x, y, z; } M11Vertex;
#define M11_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
static void box(float x, float y0, float y1, float z, float hx, float hz, unsigned int top, unsigned int side) {
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    static const int q[6] = {0, 1, 2, 0, 2, 3};
    int i, n = 0;
    M11Vertex *o = (M11Vertex *)sceGuGetMemory(sizeof(M11Vertex) * 30);
    for (i = 0; i < 6; ++i) o[n++] = (M11Vertex){top, x + cx[q[i]] * hx, y1, z + cz[q[i]] * hz};
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        float ax = x + cx[i] * hx, az = z + cz[i] * hz, bx = x + cx[j] * hx, bz = z + cz[j] * hz;
        o[n++] = (M11Vertex){side, ax, y1, az}; o[n++] = (M11Vertex){side, bx, y1, bz}; o[n++] = (M11Vertex){side, bx, y0, bz};
        o[n++] = (M11Vertex){side, ax, y1, az}; o[n++] = (M11Vertex){side, bx, y0, bz}; o[n++] = (M11Vertex){side, ax, y0, az};
    }
    sceGumDrawArray(GU_TRIANGLES, M11_VTYPE, n, 0, o);
}

void m11_draw(void) {
    int i;
    if (room_now != 214) return;
    for (i = 0; i < 2; ++i) box(blocks[i].x, blocks[i].y, blocks[i].y + 200.0f, blocks[i].z, 50.0f, 50.0f, 0xff606870u, 0xff404850u);
}

void m11_status(char *out, int size) {
    snprintf(out, size, "blocks=%.0f/%d %.0f/%d", blocks[0].moved, blocks[0].fitted, blocks[1].moved, blocks[1].fitted);
}
