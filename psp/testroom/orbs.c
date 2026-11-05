#include "orbs.h"

#include <pspgu.h>
#include <pspgum.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"

/* Not read from the game yet: how a burst splits, flies and homes. Estimates, made to look like it. */
#define ORB_MAX 96
#define ORB_PIECES 10          /* at most this many from one burst */
#define ORB_GRAVITY 1500.0f    /* cm/s^2 */
#define ORB_SETTLE 0.45f       /* seconds before they start for Dante */
#define ORB_PULL 2600.0f       /* cm/s^2 toward him */
#define ORB_TOP_SPEED 1400.0f  /* cm/s */
#define ORB_COLLECT 70.0f      /* cm from his chest */
#define ORB_SIZE 9.0f          /* cm, half a gem's height */

typedef struct { int active, value; float pos[3], vel[3], age, spin; } Orb;
typedef struct { unsigned int color; float x, y, z; } OrbVertex;

static Orb orbs[ORB_MAX];
static int *counter;
static float since_collected = 1.0e9f;

void orbs_set_counter(int *red_orbs) { counter = red_orbs; }
void orbs_clear(void) { memset(orbs, 0, sizeof(orbs)); }
float orbs_since_collected(void) { return since_collected; }

int orbs_for_enemy(int max_health, int difficulty) {
    float scale = difficulty == 0 ? 0.5f : 1.0f;
    int total = (int)((float)max_health * 5.0f * scale / 100.0f);
    return total > 0 ? total : 1;
}

static float unit_random(void) { return (float)(rand() & 0xffff) / 65535.0f; }

void orbs_burst(float x, float y, float z, int total) {
    int pieces, i, k;
    if (total <= 0) return;
    pieces = total < ORB_PIECES ? total : ORB_PIECES;
    for (k = 0; k < pieces; ++k) {
        float a = unit_random() * 6.2831853f, out = 120.0f + unit_random() * 160.0f;
        int value = total / pieces + (k < total % pieces ? 1 : 0);
        for (i = 0; i < ORB_MAX && orbs[i].active; ++i) {}
        if (i >= ORB_MAX) {
            /* No room: straight into the count. */
            if (counter) *counter += value;
            continue;
        }
        orbs[i].active = 1;
        orbs[i].value = value;
        orbs[i].pos[0] = x; orbs[i].pos[1] = y + 80.0f; orbs[i].pos[2] = z;
        orbs[i].vel[0] = sinf(a) * out; orbs[i].vel[1] = 380.0f + unit_random() * 260.0f; orbs[i].vel[2] = cosf(a) * out;
        orbs[i].age = 0.0f;
        orbs[i].spin = unit_random() * 6.2831853f;
    }
}

void orbs_update(float dt, float px, float py, float pz) {
    int i;
    since_collected += dt;
    for (i = 0; i < ORB_MAX; ++i) {
        Orb *o = &orbs[i];
        float g;
        if (!o->active) continue;
        o->age += dt;
        o->spin += dt * 5.0f;
        if (o->age < ORB_SETTLE) {
            /* Thrown out, falling, bouncing off the floor. */
            o->vel[1] -= ORB_GRAVITY * dt;
            o->pos[0] += o->vel[0] * dt; o->pos[1] += o->vel[1] * dt; o->pos[2] += o->vel[2] * dt;
            if (player_floor_fn && player_floor_fn(o->pos[0], o->pos[1] + 60.0f, o->pos[2], 0.0f, &g) && o->pos[1] < g + ORB_SIZE) {
                o->pos[1] = g + ORB_SIZE;
                o->vel[1] = -o->vel[1] * 0.4f;
                o->vel[0] *= 0.6f; o->vel[2] *= 0.6f;
            }
        } else {
            /* To his chest, faster and faster. */
            float dx = px - o->pos[0], dy = py + 100.0f - o->pos[1], dz = pz - o->pos[2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f, v;
            if (d < ORB_COLLECT) {
                if (counter) *counter += o->value;
                since_collected = 0.0f;
                o->active = 0;
                continue;
            }
            o->vel[0] += dx / d * ORB_PULL * dt; o->vel[1] += dy / d * ORB_PULL * dt; o->vel[2] += dz / d * ORB_PULL * dt;
            /* Mostly toward him: what's sideways dies away, so they don't orbit. */
            o->vel[0] *= 0.94f; o->vel[1] *= 0.94f; o->vel[2] *= 0.94f;
            v = sqrtf(o->vel[0] * o->vel[0] + o->vel[1] * o->vel[1] + o->vel[2] * o->vel[2]);
            if (v > ORB_TOP_SPEED) { float k = ORB_TOP_SPEED / v; o->vel[0] *= k; o->vel[1] *= k; o->vel[2] *= k; }
            o->pos[0] += o->vel[0] * dt; o->pos[1] += o->vel[1] * dt; o->pos[2] += o->vel[2] * dt;
        }
    }
}

/* Each a small red gem, two pyramids point to point, turning. */
void orbs_draw(void) {
    int i, n = 0;
    OrbVertex *v;
    for (i = 0; i < ORB_MAX; ++i) n += orbs[i].active;
    if (!n) return;
    v = sceGuGetMemory(sizeof(OrbVertex) * 24 * (unsigned)n);
    n = 0;
    for (i = 0; i < ORB_MAX; ++i) {
        const Orb *o = &orbs[i];
        float c, s, cx[4], cz[4], h = ORB_SIZE, w = ORB_SIZE * 0.75f;
        int k;
        if (!o->active) continue;
        c = cosf(o->spin); s = sinf(o->spin);
        for (k = 0; k < 4; ++k) {
            float a = (float)k * 1.5707963f;
            cx[k] = (cosf(a) * c - sinf(a) * s) * w;
            cz[k] = (cosf(a) * s + sinf(a) * c) * w;
        }
        for (k = 0; k < 4; ++k) {
            int j = (k + 1) % 4;
            unsigned int side = (k & 1) ? 0xff1010c0u : 0xff2828ffu;   /* ABGR: deep red, bright red */
            v[n++] = (OrbVertex){0xff8080ffu, o->pos[0], o->pos[1] + h, o->pos[2]};
            v[n++] = (OrbVertex){side, o->pos[0] + cx[k], o->pos[1], o->pos[2] + cz[k]};
            v[n++] = (OrbVertex){side, o->pos[0] + cx[j], o->pos[1], o->pos[2] + cz[j]};
            v[n++] = (OrbVertex){0xff000060u, o->pos[0], o->pos[1] - h, o->pos[2]};
            v[n++] = (OrbVertex){side, o->pos[0] + cx[j], o->pos[1], o->pos[2] + cz[j]};
            v[n++] = (OrbVertex){side, o->pos[0] + cx[k], o->pos[1], o->pos[2] + cz[k]};
        }
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    sceGuEnable(GU_TEXTURE_2D);
}
