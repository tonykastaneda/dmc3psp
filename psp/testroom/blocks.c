/* Secret Mission 12's blocks (st611, "Final Ascension"): the room's STAY and DUMMY sets with a hit box, models
 * 0-32. Each moving one plays its own looping motion from the room's model pack (PNST slot 10N+1: a MOT of 1200
 * frames on one bone; decoded by tools/gimmicks.py set_motion, written by tools/gen_rooms.py write_blocks into a
 * local generated header): lifts, slides, spins and blocks swinging round the tower's middle. prepare_stage.py
 * bakes them into ROOM_611.BIN tagged 160 + their set number, and this poses those batches each frame.
 *
 * What's the port's: their tops are floors (the hit box's top, turned with the block's yaw only; a block spinning
 * about x or z keeps a flat top 150 above its middle) and the one he stands on carries him, but their sides
 * don't stop him. The motion runs one game frame a frame from 0 when he comes in; whether the game keeps the
 * clock running between visits isn't known. The sets' own code (CStageSetStay / CStageSetDummy) isn't decoded:
 * the motions are the data's. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "blocks.h"
#include "stage.h"
#include "blocks_runtime.h"

#define LOOP_FRAMES 1200.0f
static int on;
static float clock_;
static float now_c[BLOCK_SET_COUNT][3], yaw_now[BLOCK_SET_COUNT];   /* each box's middle and yaw this frame */
static float move_now[BLOCK_SET_COUNT][3], turn_now[BLOCK_SET_COUNT][3];
static int riding = -1, rides;

static float channel_at(const BlockChannel *c, float t) {
    const BlockKey *k = &block_keys[c->first];
    int i;
    if (t <= k[0].frame) return k[0].value;
    for (i = 1; i < c->count; ++i)
        if (t <= k[i].frame) {
            float span = (float)(k[i].frame - k[i - 1].frame);
            return k[i - 1].value + (k[i].value - k[i - 1].value) * (span > 0.0f ? (t - k[i - 1].frame) / span : 1.0f);
        }
    return k[c->count - 1].value;
}

/* p turned by (rx, ry, rz) as Rz * Ry * Rx. */
static void turn_point(const float r[3], const float p[3], float out[3]) {
    float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
    float x1 = p[0], y1 = cx * p[1] - sx * p[2], z1 = sx * p[1] + cx * p[2];      /* Rx */
    float x2 = cy * x1 + sy * z1, y2 = y1, z2 = -sy * x1 + cy * z1;               /* Ry */
    out[0] = cz * x2 - sz * y2; out[1] = sz * x2 + cz * y2; out[2] = z2;          /* Rz */
}

static void pose_all(void) {
    int i, k;
    float t = fmodf(clock_, LOOP_FRAMES);
    for (i = 0; i < BLOCK_SET_COUNT; ++i) {
        const BlockSet *b = &block_sets[i];
        float rel[3], turned[3];
        memset(move_now[i], 0, sizeof move_now[i]); memset(turn_now[i], 0, sizeof turn_now[i]);
        for (k = 0; k < b->count; ++k) {
            const BlockChannel *c = &block_channels[b->first + k];
            float v = channel_at(c, t);
            if (c->axis < 3) move_now[i][c->axis] = v; else turn_now[i][c->axis - 3] = v;
        }
        for (k = 0; k < 3; ++k) rel[k] = b->centre[k] - b->pivot[k];
        turn_point(turn_now[i], rel, turned);
        for (k = 0; k < 3; ++k) now_c[i][k] = b->pivot[k] + move_now[i][k] + turned[k];
        yaw_now[i] = turn_now[i][1];
        if (b->tag) stage_set_group_motion(b->tag, b->pivot, move_now[i], turn_now[i]);
    }
}

void blocks_enter(int room) {
    on = room == 611;
    clock_ = 0.0f;
    riding = -1;
    if (on) pose_all();
}

int blocks_active(void) { return on; }

void blocks_update(float dt, float *x, float *y, float *z, int on_ground) {
    float before[3], yaw_before = 0.0f;
    int k, was = on_ground ? riding : -1;
    if (!on) return;
    if (was >= 0) { for (k = 0; k < 3; ++k) before[k] = now_c[was][k]; yaw_before = yaw_now[was]; }
    clock_ += dt * 60.0f;
    pose_all();
    if (was >= 0) {
        /* Carried: his place relative to the block, moved and turned (about y) with it. */
        float dx = *x - before[0], dz = *z - before[2], a = yaw_now[was] - yaw_before, c = cosf(a), s = sinf(a);
        *x = now_c[was][0] + c * dx + s * dz;
        *z = now_c[was][2] - s * dx + c * dz;
        *y += now_c[was][1] - before[1];
        ++rides;
    }
    /* What he's standing on now, for the next frame. */
    riding = -1;
    for (k = 0; on_ground && k < BLOCK_SET_COUNT; ++k) {
        const BlockSet *b = &block_sets[k];
        float dx = *x - now_c[k][0], dz = *z - now_c[k][2], c = cosf(yaw_now[k]), s = sinf(yaw_now[k]);
        float lx = c * dx - s * dz, lz = s * dx + c * dz, top = now_c[k][1] + b->half[1];
        if (fabsf(lx) <= b->half[0] && fabsf(lz) <= b->half[2] && fabsf(*y - top) < 40.0f) riding = k;
    }
}

int blocks_floor(float x, float y, float z, float step, float *out) {
    int i, found = 0;
    float best = -1.0e30f;
    if (!on) return 0;
    for (i = 0; i < BLOCK_SET_COUNT; ++i) {
        const BlockSet *b = &block_sets[i];
        float dx = x - now_c[i][0], dz = z - now_c[i][2], c = cosf(yaw_now[i]), s = sinf(yaw_now[i]);
        float lx = c * dx - s * dz, lz = s * dx + c * dz, top = now_c[i][1] + b->half[1];
        if (fabsf(lx) > b->half[0] || fabsf(lz) > b->half[2]) continue;
        if (top > y + step + 60.0f || top <= best) continue;
        best = top; found = 1;
    }
    if (found) *out = best;
    return found;
}

void blocks_status(char *out, int size) {
    snprintf(out, size, "blocks on=%d frame=%.0f riding=%d rides=%d", on, fmodf(clock_, LOOP_FRAMES), riding,
             rides);
}
