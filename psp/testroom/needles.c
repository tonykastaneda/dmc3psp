/* The spike traps: CStageSetNeedle (set 100 in 125, 131 and 610; its "type" is 0, 1 and 2). Its code (FUN_003df400
 * and the functions after it, about 4 KB, driving the needles' own models and motions) isn't decoded, so every trap
 * runs the stand-in first written for Mission 6 from the walkthrough: rows of spikes come at Dante down the corridor
 * in turn from the left wall (step right), the right wall (step left), the floor (jump) or the ceiling (stay on the
 * ground). A spike hurts as attack record 0 of table 0x5bbb80 (1000, reaction 0: light), which the needles' caps take
 * (FUN_003e0600, reached from the set's 003df400). Speed and spacing are estimates.
 *
 *   125  Mission 6's Trial of Skill: area 0 (x 9035-9235) starts them, area 1 (x 1310-1610) stops them; Dante walks
 *        toward -x to the Essence, the rows come toward +x.
 *   131  Mission 17's Trial of Skill (EventTbl17: area 0 by the door from 132 runs gimmick_cmd0c 100 with its camera,
 *        area 1 runs gimmick_cmd10 100 1 and sets flag 1, area 2 turns it off as the room's entry does). The port
 *        starts the rows at area 0 (x 185-1160) and stops them at area 1 (x 8348-8648); Dante walks toward +x to the
 *        red pod, the rows come toward -x.
 *   610  Secret Mission 11, the same room (st610 shares 131's starts and areas). No script: the rows start as he
 *        comes in (its start is by area 0) and stop at area 1, short of the Blue Orb at area 2. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>

#include "needles.h"
#include "stage.h"

typedef struct {
    short room;
    float mid, half;           /* the corridor's middle (z) and half width */
    float lo, hi;              /* the x range the rows run in */
    float start0, start1;      /* the x range that sets them going (z within 7 m of the corridor: its doorway) */
    float stop0, stop1;        /* the x range that stops them */
    float dir;                 /* +1: the rows travel toward +x */
    float speed, every;        /* cm a second; seconds between rows [estimates] */
} Trap;
static const Trap traps[] = {
    {125, 2500.0f, 450.0f, 1610.0f, 9300.0f, 9035.0f, 9235.0f, 1310.0f, 1610.0f, 1.0f, 700.0f, 1.6f},
    {131, 2491.0f, 600.0f, 900.0f, 8348.0f, 185.0f, 1160.0f, 8348.0f, 8648.0f, -1.0f, 700.0f, 1.6f},
    {610, 2491.0f, 600.0f, 900.0f, 8348.0f, 0.0f, 1160.0f, 8349.0f, 8713.0f, -1.0f, 700.0f, 1.6f},
};
#define TRAP_COUNT ((int)(sizeof traps / sizeof traps[0]))

#define NEEDLE_MAX 8
typedef struct { float x, floor; int kind, on, hit; } Needle;
static Needle needles[NEEDLE_MAX];
static const Trap *trap;
static int on, done_room = -1, seq, hazard, hits;
static float clock_, hazard_x, hazard_z;
static const unsigned char pattern[7] = {0, 1, 2, 0, 3, 1, 2};

void needles_enter(int room, int rearm) {
    int i;
    trap = 0;
    on = 0; clock_ = 0.0f; hazard = 0;
    memset(needles, 0, sizeof needles);
    for (i = 0; i < TRAP_COUNT; ++i) if (traps[i].room == room) trap = &traps[i];
    if (rearm) done_room = -1;
}

int needles_running(void) { return trap && on; }

void needles_update(float dt, float x, float y, float z, int on_ground) {
    int i;
    if (!trap) return;
    if (!on && done_room != trap->room && x > trap->start0 && x < trap->start1 && fabsf(z - trap->mid) < trap->half + 700.0f) on = 1;   /* the doorway too */
    if (on && x > trap->stop0 && x < trap->stop1) { on = 0; done_room = trap->room; }
    if (on && (clock_ -= dt) <= 0.0f) {
        clock_ = trap->every;
        for (i = 0; i < NEEDLE_MAX; ++i) if (!needles[i].on) break;
        if (i < NEEDLE_MAX) {
            Needle *n = &needles[i];
            float f;
            n->on = 1; n->hit = 0;
            /* 18 m ahead of him, where they come from, inside the corridor. */
            n->x = trap->dir > 0.0f ? fmaxf(x - 1800.0f, trap->lo) : fminf(x + 1800.0f, trap->hi);
            n->kind = pattern[seq++ % 7];
            n->floor = stage_floor(n->x, y + 200.0f, trap->mid, 400.0f, &f) ? f : y;
        }
    }
    for (i = 0; i < NEEDLE_MAX; ++i) {
        Needle *n = &needles[i];
        float past;
        int caught;
        if (!n->on) continue;
        n->x += trap->dir * trap->speed * dt;
        past = trap->dir * (n->x - x);   /* how far it's gone by him */
        if (past > 1500.0f || n->x > trap->hi + 1.0f || n->x < trap->lo - 1.0f || (!on && past > 200.0f)) { n->on = 0; continue; }
        if (n->hit || fabsf(n->x - x) > 40.0f) continue;
        caught = n->kind == 0 ? z < trap->mid : n->kind == 1 ? z >= trap->mid : n->kind == 2 ? on_ground : !on_ground;
        if (caught) { n->hit = 1; hazard = 1; ++hits; hazard_x = n->x - 200.0f * trap->dir; hazard_z = z; }
    }
}

int needles_hazard(float *from_x, float *from_z) {
    if (!hazard) return 0;
    hazard = 0;
    *from_x = hazard_x; *from_z = hazard_z;
    return 1;
}

void needles_status(char *out, int size) {
    int i, live = 0;
    for (i = 0; i < NEEDLE_MAX; ++i) live += needles[i].on;
    snprintf(out, size, "needles room=%d on=%d done=%d rows=%d hits=%d", trap ? trap->room : -1, on, done_room, live, hits);
}

typedef struct { unsigned int color; float x, y, z; } NeedleVertex;
/* An upright slab, 2 hx by 2 hz, from y0 to y1. */
static void slab(float x, float y0, float y1, float z, float hx, float hz, unsigned int top, unsigned int side) {
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    static const int q[6] = {0, 1, 2, 0, 2, 3};
    float px[4], pz[4];
    int i, n = 0;
    NeedleVertex *o = (NeedleVertex *)sceGuGetMemory(sizeof(NeedleVertex) * 30);
    for (i = 0; i < 4; ++i) { px[i] = x + cx[i] * hx; pz[i] = z + cz[i] * hz; }
    for (i = 0; i < 6; ++i) o[n++] = (NeedleVertex){top, px[q[i]], y1, pz[q[i]]};
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        o[n++] = (NeedleVertex){side, px[i], y1, pz[i]};
        o[n++] = (NeedleVertex){side, px[j], y1, pz[j]};
        o[n++] = (NeedleVertex){side, px[j], y0, pz[j]};
        o[n++] = (NeedleVertex){side, px[i], y1, pz[i]};
        o[n++] = (NeedleVertex){side, px[j], y0, pz[j]};
        o[n++] = (NeedleVertex){side, px[i], y0, pz[i]};
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, o);
}

void needles_draw(void) {
    int i, k;
    if (!trap) return;
    for (i = 0; i < NEEDLE_MAX; ++i) {
        /* A row of spikes: from one wall to the middle, low across the floor, or high across the ceiling. */
        const Needle *n = &needles[i];
        if (!n->on) continue;
        for (k = 0; k < 9; ++k) {
            float zz = trap->mid - trap->half + (k + 0.5f) * (trap->half * 2.0f / 9.0f), y0 = n->floor, y1 = n->floor + 320.0f;
            if (n->kind == 0 && zz >= trap->mid) continue;
            if (n->kind == 1 && zz < trap->mid) continue;
            if (n->kind == 2) y1 = n->floor + 70.0f;
            if (n->kind == 3) { y0 = n->floor + 230.0f; y1 = n->floor + 420.0f; }
            slab(n->x, y0, y1, zz, 7.0f, 10.0f, 0xffa0a8b0u, 0xff505860u);
        }
    }
}
