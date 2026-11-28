/* The spike traps: CStageSetNeedle (set 100 in 125, 131 and 610; its "type" 0, 1 and 2). Vtable 0x60b420: update
 * FUN_003df400 runs FUN_003df6c0 while on (+0xace4, the event's gimmick_cmd0c; gimmick_cmd10 [100, 1] sets +0xace5,
 * which stops new rows while those out finish, FUN_003e0820 / 003e0840).
 *
 * 24 spike panels (the set's model 0: a board of spikes 10 m wide, its points toward +y), three to a row, 8 rows.
 * FUN_003df6c0 starts rows on a clock: type 0 one row of a random kind (0-3) in the first free even slot; types 1 and
 * 2 a pair, a ceiling or floor row (kind 0 / 1) and a wall row (2 / 3); then waits 0x5bbbf0[type x 5 + level]
 * frames. FUN_003dfa00 lays a row out: panel j at x = 0x5bbbc0[type] start + j x step, at the kind's place and turn
 * (the level's table at 0x5bbf50: y, z, its turn about x, which of y / z it moves along, its thrust, its draw-back),
 * panel j waiting j x (t0 + t1 + t2 + t3) / 3. FUN_003dfc20 runs each panel: 0 it draws back over t0 (0x5bbc30), 1 it
 * thrusts over t1 (0x5bbc50; at its end every other panel plays the room bank's 0xf, bank 0x20), 2 it holds for t2
 * (0x5bbc70), 3 it pulls in over t3 (0x5bbc90), then the row moves 3 steps down the corridor and starts again, until
 * it's past the end. Its hit (FUN_003e0600: attack 0 of 0x5bbb80, 1000) is a box at (-50, 0, 0) half (100, 400, 500)
 * in the panel's frame, live while it thrusts and holds.
 *
 * The level (+0xace7) is picked by a value the game reads through its object at game +4, +0x10, vfunc +0x1c, under
 * 250 / 330 / 500 / 850 (0x5bbf70; type 2 always level 4): that value isn't decoded, so the port takes the level from
 * the difficulty [inferred]. Rooms' triggers (main.c, the event scripts' areas):
 *   125  Mission 6's Trial of Skill: area 0 (x 9035-9235) starts them, area 1 (x 1310-1610) stops new rows.
 *   131  Mission 17's: area 0 by the door from 132 (x 185-1160) starts them, area 1 (x 8348-8648) stops them.
 *   610  Secret Mission 11, the same room: no script; they start as he comes in and stop at area 1. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>

#include "needles.h"
#include "sfx.h"
#include "stage.h"

#define NEEDLE_TAG 318   /* tools/gimmicks.py NEEDLE_TAG: the set's model, baked at the origin */
#define PANELS 24
#define ROWS 8

typedef struct {
    short room, type;
    float start0, start1;      /* the x range that sets them going (z within 7 m of the corridor: its doorway too) */
    float stop0, stop1;        /* the x range that stops new rows */
} Trap;
static const Trap traps[] = {
    {125, 0, 9035.0f, 9235.0f, 1310.0f, 1610.0f},
    {131, 1, 185.0f, 1160.0f, 8348.0f, 8648.0f},
    {610, 2, 0.0f, 1160.0f, 8349.0f, 8713.0f},
};
#define TRAP_COUNT ((int)(sizeof traps / sizeof traps[0]))

/* 0x5bbbc0: per type the first panel's x, the end, the step between panels. */
static const float lanes[3][3] = {{1200.0f, 8800.0f, 200.0f}, {8800.0f, 1200.0f, -200.0f}, {8800.0f, 1200.0f, -200.0f}};
/* 0x5bbbf0: frames between rows, [type][level]. */
static const float row_every[3][5] = {{110, 100, 110, 90, 70}, {120, 120, 150, 110, 100}, {100, 100, 100, 100, 100}};
/* 0x5bbc30 / 50 / 70 / 90: the phases' frames by level. */
static const float phase_frames[4][5] = {{10, 7, 15, 8, 5}, {10, 7, 10, 8, 5}, {15, 12, 15, 13, 10}, {5, 4, 5, 3, 3}};
/* 0x5bbf50: per level, the four kinds: y, z, turn about x (degrees), moves along y / z, thrust, draw-back. Level 2's
 * walls thrust 500, the others' 600. */
typedef struct { float y, z, turn; unsigned char along_y, along_z; float thrust, back; } Kind;
static const Kind kinds[2][4] = {
    {{1400, 2500, 180, 1, 0, -650, -50}, {-500, 2500, 0, 1, 0, 350, 50}, {500, 3500, -90, 0, 1, -600, -50}, {500, 1500, 90, 0, 1, 600, 50}},
    {{1400, 2500, 180, 1, 0, -650, -50}, {-500, 2500, 0, 1, 0, 350, 50}, {500, 3500, -90, 0, 1, -500, -50}, {500, 1500, 90, 0, 1, 500, 50}},
};
/* 0x5bbcb0: the panel's hit box (centre, half sizes) in its frame; level 3 and 4's are 420 high. */
#define HIT_CX (-50.0f)
#define HIT_HX 100.0f
#define HIT_HZ 500.0f

typedef struct { float x, y, z, wait, timer; int phase, struck; } Panel;
typedef struct { int on, kind, sounded; } Row;
static Panel panels[PANELS];
static Row rows[ROWS];
static const Trap *trap;
static int level = 1, running, stopping, done_room = -1, hazard, hits, sounds;
static float clock_, hazard_x, hazard_z;
static unsigned int rng = 0x2545f491u;

static unsigned int random_u32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static const Kind *kind_of(int k) { return &kinds[level == 2][k & 3]; }
static float frames_of(int phase) { return phase_frames[phase][level]; }

void needles_set_level(int difficulty) { level = difficulty < 0 ? 0 : difficulty > 4 ? 4 : difficulty; }

void needles_enter(int room, int rearm) {
    int i;
    trap = 0;
    running = stopping = 0; clock_ = 0.0f; hazard = 0;
    memset(panels, 0, sizeof panels);
    memset(rows, 0, sizeof rows);
    for (i = 0; i < TRAP_COUNT; ++i) if (traps[i].room == room) trap = &traps[i];
    if (trap) stage_set_group_mode(NEEDLE_TAG, 3);   /* drawn here, panel by panel */
    if (rearm) done_room = -1;
}

int needles_running(void) { return trap && running; }

/* FUN_003dfa00: row r of kind k. */
static void row_start(int r, int k) {
    const Kind *kd = kind_of(k);
    float cycle = frames_of(0) + frames_of(1) + frames_of(2) + frames_of(3);
    int j;
    rows[r].on = 1; rows[r].kind = k; rows[r].sounded = 0;
    for (j = 0; j < 3; ++j) {
        Panel *p = &panels[r * 3 + j];
        p->x = lanes[trap->type][0] + (float)j * lanes[trap->type][2];
        p->y = kd->y; p->z = kd->z;
        p->wait = (float)j * cycle / 3.0f;
        p->phase = 0; p->timer = frames_of(0); p->struck = 0;
    }
}

static void move(Panel *p, const Kind *kd, float by) {
    if (kd->along_y) p->y += by;
    if (kd->along_z) p->z += by;
}

/* Is (x, y, z) inside panel p's hit box (grown by r)? Its frame: at (x, y, z), turned about x. */
static int in_box(const Panel *p, const Kind *kd, float x, float y, float z, float r) {
    float a = -kd->turn * 0.0174532925f, dy = y - p->y, dz = z - p->z;
    float ly = dy * cosf(a) - dz * sinf(a), lz = dy * sinf(a) + dz * cosf(a);
    float hy = level >= 3 ? 420.0f : 400.0f;
    return fabsf(x - p->x - HIT_CX) < HIT_HX + r && fabsf(ly) < hy + r && fabsf(lz) < HIT_HZ + r;
}

void needles_update(float dt, float x, float y, float z, int on_ground) {
    float f = dt * 60.0f;
    int r;
    (void)on_ground;
    if (!trap) return;
    if (!running && done_room != trap->room && x > trap->start0 && x < trap->start1 && fabsf(z - 2500.0f) < 1300.0f) {
        running = 1; stopping = 0; clock_ = 0.0f;
    }
    if (running && !stopping && x > trap->stop0 && x < trap->stop1) { stopping = 1; done_room = trap->room; }
    if (!running) return;
    /* FUN_003df6c0: a new row on the clock. */
    if (!stopping && (clock_ -= f) < 0.0f) {
        int started = 0;
        for (r = 0; r < 4 && !started; ++r) {
            if (trap->type == 0) {
                if (!rows[r * 2].on) { row_start(r * 2, (int)(random_u32() & 3)); started = 1; }
            } else if (!rows[r * 2].on && !rows[r * 2 + 1].on) {
                row_start(r * 2, (int)(random_u32() & 1));
                row_start(r * 2 + 1, (int)(random_u32() & 1) + 2);
                started = 1;
            }
        }
        if (started) clock_ = row_every[trap->type][trap->type == 2 ? 4 : level];
    }
    /* FUN_003dfc20: each panel of each row out. */
    for (r = 0; r < ROWS; ++r) {
        int done = 0, j;
        const Kind *kd;
        if (!rows[r].on) continue;
        kd = kind_of(rows[r].kind);
        for (j = 0; j < 3; ++j) {
            Panel *p = &panels[r * 3 + j];
            if ((p->wait -= f) >= 0.0f) continue;
            switch (p->phase) {
            case 0:
                if ((p->timer -= f) < 0.0f) { p->phase = 1; p->timer = frames_of(1); }
                else move(p, kd, f * kd->back / frames_of(0));
                break;
            case 1:
                if ((p->timer -= f) < 0.0f) {
                    p->phase = 2; p->timer = frames_of(2);
                    if (!rows[r].sounded) { sfx_play_room(0xf, 0x20, 0.0f, 1.0f); ++sounds; }
                    rows[r].sounded = !rows[r].sounded;
                } else move(p, kd, f * kd->thrust / frames_of(1));
                break;
            case 2:
                if ((p->timer -= f) < 0.0f) { p->phase = 3; p->timer = frames_of(3); }
                break;
            case 3:
                if ((p->timer -= f) < 0.0f) {
                    const float *lane = lanes[trap->type];
                    p->x += lane[2] * 3.0f;
                    p->y = kd->y; p->z = kd->z;
                    p->struck = 0;
                    if (trap->type == 0 ? p->x > lane[1] : p->x < lane[1]) { p->x += lane[2] * 10.0f; p->phase = 4; }
                    else { p->phase = 0; p->timer = frames_of(0); }
                } else move(p, kd, -f * (kd->thrust + kd->back) / frames_of(3));
                break;
            default:
                ++done;
                break;
            }
            /* Its hit, while it thrusts and holds: Dante's body as three points up from his feet. */
            if ((p->phase == 1 || p->phase == 2) && !p->struck &&
                (in_box(p, kd, x, y + 20.0f, z, 40.0f) || in_box(p, kd, x, y + 90.0f, z, 40.0f) || in_box(p, kd, x, y + 170.0f, z, 40.0f))) {
                p->struck = 1; hazard = 1; ++hits;
                hazard_x = p->x - 200.0f * (lanes[trap->type][2] > 0.0f ? 1.0f : -1.0f); hazard_z = z;
            }
        }
        if (done == 3) rows[r].on = 0;
    }
    for (r = 0; r < ROWS && !rows[r].on; ++r) {}
    if (stopping && r == ROWS) running = 0;
}

int needles_hazard(float *from_x, float *from_z) {
    if (!hazard) return 0;
    hazard = 0;
    *from_x = hazard_x; *from_z = hazard_z;
    return 1;
}

void needles_status(char *out, int size) {
    int r, live = 0;
    for (r = 0; r < ROWS; ++r) live += rows[r].on;
    snprintf(out, size, "needles room=%d on=%d stopping=%d done=%d rows=%d hits=%d sounds=%d level=%d", trap ? trap->room : -1,
             running, stopping, done_room, live, hits, sounds, level);
}

/* Each panel out: the set's own model at (x, y, z), turned about x (FUN_003e04b0: Rx, then moved). */
void needles_draw(void) {
    int r, j;
    if (!trap) return;
    for (r = 0; r < ROWS; ++r) {
        const Kind *kd;
        if (!rows[r].on) continue;
        kd = kind_of(rows[r].kind);
        for (j = 0; j < 3; ++j) {
            const Panel *p = &panels[r * 3 + j];
            float a = kd->turn * 0.0174532925f, c = cosf(a), s = sinf(a);
            float m[12] = {1.0f, 0.0f, 0.0f, p->x, 0.0f, c, -s, p->y, 0.0f, s, c, p->z};
            if (p->phase == 4) continue;
            stage_draw_group_at(NEEDLE_TAG, m);
        }
    }
}
