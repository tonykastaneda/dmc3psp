#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include "mission7.h"
#include "fx.h"

/* Room 105, from EventTbl07: gimmick_bind_flag 100 (the crest switch) -> 35; with flag 35 set and 38 clear, flag
 * 36 (the ball, HANG 200, starts) and 38; flag 37 (its shell broken) -> flag 4, the key item's stay (400/401) and
 * item 45. The places are the stage's: switch (2500, 168, 1501), the ball's root (2495, 1500, 2750), the skull
 * (2538, 20, 2701). Approximations: CStageSetHang isn't decoded, so the ball hangs still above the skull, takes
 * BALL_PIECES x HITS_PER_PIECE hits, and if it's left alone for BALL_IDLE frames it rises and its shell grows back
 * (the walkthrough's "if the ball rises up, you have to restart"); the switch lights after SWITCH_HITS hits. */
#define SWITCH_HITS 8
#define BALL_PIECES 8
#define HITS_PER_PIECE 3
#define BALL_IDLE 300.0f
#define BALL_Y 150.0f
#define BALL_R 110.0f

typedef struct { unsigned int color; float x, y, z; } M7Vertex;
#define M7_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static int room_now, switch_hits, switch_lit, ball_hits, ball_broken;
static float ball_idle, ball_rise, spin;
static const float sw_at[3] = {2500.0f, 168.0f, 1501.0f}, ball_at[3] = {2495.0f, BALL_Y, 2750.0f};

void m7_reset(void) {
    room_now = -1;
    switch_hits = switch_lit = ball_hits = ball_broken = 0;
    ball_idle = ball_rise = spin = 0.0f;
}
void m7_enter(int room) { room_now = room; }
/* 127's door into 114 lands on its balcony at 9000 (start 1), where the game has Dante drop over the railing to
 * the ground floor; the port's collision there keeps him on the balcony, so the door takes him to the ground
 * floor's start 0 instead [approximation]. He climbs back past the balcony on the jump pads. */
int m7_door_route(int room, int door, int *next_room, int *start) {
    if (room == 127 && door == 1) { *next_room = 114; *start = 0; return 1; }
    return 0;
}
int m7_item_shown(int item) { return item != 45 || ball_broken; }
void m7_status(char *out, int size) {
    snprintf(out, size, "m7 room=%d switch=%d/%d ball=%d/%d broken=%d rise=%.0f", room_now, switch_hits, switch_lit,
             ball_hits, BALL_PIECES * HITS_PER_PIECE, ball_broken, ball_rise);
}

static int in_reach(const float at[3], float r, float x, float y, float z, float yaw, float reach) {
    float dx = at[0] - x, dz = at[2] - z, d = sqrtf(dx * dx + dz * dz), dy = at[1] - y;
    if (d > reach + r || dy < -150.0f || dy > 300.0f) return 0;
    return d < 1.0f || (dx * sinf(yaw) + dz * cosf(yaw)) / d > 0.2f;
}
void m7_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    int n = heavy ? 2 : 1;
    if (room_now != 105) return;
    if (!switch_lit && in_reach(sw_at, 60.0f, x, y, z, yaw, reach)) {
        float at[3] = {sw_at[0], sw_at[1], sw_at[2]};
        fx_hit_spark(at);
        switch_hits += n;
        if (switch_hits >= SWITCH_HITS) switch_lit = 1;   /* flag 35: the ball comes down */
    }
    if (switch_lit && !ball_broken && ball_rise <= 0.0f && in_reach(ball_at, BALL_R, x, y, z, yaw, reach)) {
        float at[3] = {ball_at[0], ball_at[1], ball_at[2]};
        fx_hit_spark(at);
        ball_hits += n;
        ball_idle = 0.0f;
        if (ball_hits % HITS_PER_PIECE == 0) fx_explosion(at);   /* a piece of its shell breaks off */
        if (ball_hits >= BALL_PIECES * HITS_PER_PIECE) {
            ball_broken = 1;   /* flag 37: the skull */
            fx_explosion(at);
        }
    }
}
void m7_push_out(float *x, float *z, float y, float r) {
    float dx, dz, d, min = BALL_R + r;
    if (room_now != 105 || !switch_lit || ball_broken || ball_rise > 0.0f || y > ball_at[1] + BALL_R) return;
    dx = *x - ball_at[0]; dz = *z - ball_at[2];
    d = sqrtf(dx * dx + dz * dz);
    if (d >= min) return;
    if (d < 0.01f) { dx = 0.0f; dz = 1.0f; d = 1.0f; }
    *x = ball_at[0] + dx / d * min;
    *z = ball_at[2] + dz / d * min;
}
void m7_update(float dt) {
    float f = dt * 60.0f;
    spin += f;
    if (room_now != 105 || !switch_lit || ball_broken) return;
    if (ball_rise > 0.0f) {
        ball_rise -= f;
        if (ball_rise <= 0.0f) ball_rise = 0.0f;
        return;
    }
    if (ball_hits > 0 && (ball_idle += f) > BALL_IDLE) {
        ball_hits = 0;      /* it rises, and its shell's whole again */
        ball_idle = 0.0f;
        ball_rise = 180.0f;
    }
}

static void box(float x, float y0, float y1, float z, float h, unsigned int top, unsigned int side) {
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    static const int q[6] = {0, 1, 2, 0, 2, 3};
    float px[4], pz[4];
    int i, n = 0;
    M7Vertex *o = (M7Vertex *)sceGuGetMemory(sizeof(M7Vertex) * 36);
    for (i = 0; i < 4; ++i) { px[i] = x + cx[i] * h; pz[i] = z + cz[i] * h; }
    for (i = 0; i < 6; ++i) o[n++] = (M7Vertex){top, px[q[i]], y1, pz[q[i]]};
    for (i = 0; i < 6; ++i) o[n++] = (M7Vertex){side, px[q[i]], y0, pz[q[i]]};
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        o[n++] = (M7Vertex){side, px[i], y1, pz[i]}; o[n++] = (M7Vertex){side, px[j], y1, pz[j]}; o[n++] = (M7Vertex){side, px[j], y0, pz[j]};
        o[n++] = (M7Vertex){side, px[i], y1, pz[i]}; o[n++] = (M7Vertex){side, px[j], y0, pz[j]}; o[n++] = (M7Vertex){side, px[i], y0, pz[i]};
    }
    sceGumDrawArray(GU_TRIANGLES, M7_VTYPE, n, 0, o);
}
void m7_draw(void) {
    int i, left;
    float lift, y;
    if (room_now != 105) return;
    /* Stand-ins (the stage's own models aren't converted): the switch's hub, red once lit... */
    box(sw_at[0], sw_at[1] - 25.0f, sw_at[1] + 25.0f, sw_at[2], 25.0f, switch_lit ? 0xff3050ffu : 0xff606870u,
        switch_lit ? 0xff2030c0u : 0xff404850u);
    if (!switch_lit || ball_broken) return;
    /* ...and the ball: a blue core in its grey shell pieces, on its chain from the ceiling. */
    lift = ball_rise > 0.0f ? sinf(ball_rise / 180.0f * 3.1415927f) * 600.0f : 0.0f;
    y = ball_at[1] + lift;
    box(ball_at[0], y + BALL_R, 1500.0f, ball_at[2], 6.0f, 0xff404040u, 0xff303030u);
    box(ball_at[0], y - 60.0f, y + 60.0f, ball_at[2], 60.0f, 0xffff6020u, 0xffc04010u);
    left = BALL_PIECES - ball_hits / HITS_PER_PIECE;
    for (i = 0; i < left; ++i) {
        float a = (i * 45.0f + spin * 0.5f) * 0.0174533f;
        box(ball_at[0] + sinf(a) * 75.0f, y - 50.0f + (i & 1) * 40.0f, y + 10.0f + (i & 1) * 40.0f, ball_at[2] + cosf(a) * 75.0f, 38.0f,
            0xff909498u, 0xff60666cu);
    }
}
