/* Mission 14, "Drive!", from EventTbl14 (GDATA 252).
 *
 * 237 Lair of Judgement Ruins: door 0 (to 235) is disabled until flag 32, which COMBO set 200 (a combat adjudicator,
 *     weapon "fight", rank C) sets when it's beaten; COLLISION 201 walls the door off behind it. The walkthrough: only
 *     Beowulf's Hyper Fist moves it. The port has no style ranks on it: ADJ_HITS melee hits break it (a heavy hit
 *     counts two), whatever the weapon. It's drawn as a stone block (the COMBO model isn't converted).
 * 008 13th Avenue: its area 0 (x 602-5202, z 1686-2286) plays demo 326, which clears the mission. */
#include <math.h>
#include <stdio.h>
#include <pspgu.h>
#include <pspgum.h>
#include "mission14.h"
#include "fx.h"
#include "stage.h"

#define ADJ_TAG (128 + 201 - 100)   /* COLLISION 201, tools/gen_rooms.py STORY_WALLS */
#define ADJ_HITS 20
static const float adj_at[3] = {70.0f, -100.0f, 4950.0f};
static int room_now = -1, adj_hits, adj_broken;

void m14_reset(void) { room_now = -1; adj_hits = 0; adj_broken = 0; }

void m14_enter(int room) {
    room_now = room;
    if (room == 237 && adj_broken) stage_hide_group(ADJ_TAG);
}

int m14_door_shut(int room, int door) { return room == 237 && door == 0 && !adj_broken; }

int m14_update(float dt, float x, float y, float z) {
    (void)dt;
    return room_now == 8 && x > 602.0f && x < 5202.0f && z > 1686.0f && z < 2286.0f && y > 400.0f && y < 5500.0f;
}

void m14_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    float dx = adj_at[0] - x, dz = adj_at[2] - z, d = sqrtf(dx * dx + dz * dz);
    if (room_now != 237 || adj_broken || d > reach + 100.0f || fabsf(adj_at[1] - y) > 300.0f) return;
    if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) return;
    adj_hits += heavy ? 2 : 1;
    {
        float at[3] = {adj_at[0], adj_at[1] + 150.0f, adj_at[2]};
        fx_hit_spark(at);
        if (adj_hits >= ADJ_HITS) {
            adj_broken = 1;
            stage_hide_group(ADJ_TAG);
            fx_explosion(at);
        }
    }
}

typedef struct { unsigned int color; float x, y, z; } M14Vertex;
#define M14_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
void m14_draw(void) {
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    static const int q[6] = {0, 1, 2, 0, 2, 3};
    float x = adj_at[0] + 60.0f, z = adj_at[2], y0 = adj_at[1], y1 = adj_at[1] + 260.0f, h = 70.0f;
    unsigned int top = 0xff607080u, side = 0xff404858u;
    int i, n = 0;
    M14Vertex *o;
    if (room_now != 237 || adj_broken) return;
    o = (M14Vertex *)sceGuGetMemory(sizeof(M14Vertex) * 30);
    for (i = 0; i < 6; ++i) o[n++] = (M14Vertex){top, x + cx[q[i]] * h, y1, z + cz[q[i]] * h};
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        float ax = x + cx[i] * h, az = z + cz[i] * h, bx = x + cx[j] * h, bz = z + cz[j] * h;
        o[n++] = (M14Vertex){side, ax, y1, az}; o[n++] = (M14Vertex){side, bx, y1, bz}; o[n++] = (M14Vertex){side, bx, y0, bz};
        o[n++] = (M14Vertex){side, ax, y1, az}; o[n++] = (M14Vertex){side, bx, y0, bz}; o[n++] = (M14Vertex){side, ax, y0, az};
    }
    sceGumDrawArray(GU_TRIANGLES, M14_VTYPE, n, 0, o);
}

void m14_status(char *out, int size) { snprintf(out, size, "adj=%d/%d", adj_hits, adj_broken); }
