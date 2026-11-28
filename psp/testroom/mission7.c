#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include "mission7.h"
#include "gimmick_models.h"
#include "fx.h"
#include "enemy.h"

/* Room 105, from EventTbl07: gimmick_bind_flag 100 (the crest switch) -> 35; with flag 35 set and 38 clear, flag
 * 36 (the ball, HANG 200, starts) and 38; flag 37 (its shell broken) -> flag 4, the key item's stay (400/401) and
 * item 45. The places are the stage's: switch (2500, 168, 1501), the ball's root (2495, 1500, 2750), the skull
 * (2538, 20, 2701).
 *
 * The ball (CStageSetHang, vtable 0x609160, update FUN_00395ac0): HANG 200's "length 1200", "time 1320", "math 15".
 * Started (FUN_00396600) it comes down from its root at 20 cm a frame to 1200 below (FUN_00396640; at the bottom the
 * room bank's 0xa3, bank 0x21) and hangs for 1320 frames from its start; then it rises at 10 a frame (FUN_00396360),
 * its shell whole again at the top (+0xdac = 1200, 0x5b62b8). A melee hit (FUN_00397120) takes the attack's damage off
 * that and pushes it by damage / 15 along the blow, plays 0x3d; gun hits only spark (0x95). Its shell shows one of
 * five stages by what's left (over 1000 / 800 / 600 / 400 / 200, 0x5b62a0); under 0 it breaks (FUN_00396740: flag 37,
 * 0x3e) and rises away; risen, its switch must be struck again (as spheres.c's, Mission 16's decoded CStageSetHang).
 * Stand-ins: the ball's EFM model (blocks), its swing a plain pendulum (spheres.c holds the chain's length as the
 * game does), Dante's damage (42 a light hit, 84 heavy); the switch lights after SWITCH_HITS hits. */
#include "sfx.h"
#define SWITCH_HITS 8
#define BALL_R 110.0f
#define BALL_TOP 1500.0f
#define BALL_LENGTH 1200.0f
#define BALL_TIME 1320.0f
#define BALL_HP 1200.0f
#define BALL_MASS 15.0f
enum { BALL_UP, BALL_LOWERING, BALL_DOWN, BALL_RISING, BALL_BROKEN };

typedef struct { unsigned int color; float x, y, z; } M7Vertex;
#define M7_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static int ambush_step, ambush_done;
static float ambush_clock, ambush_y;
static int room_now, switch_hits, switch_lit, ball_state, ball_broken;
static float ball_hp, ball_timer, ball_drop, ball_off[2], ball_vel[2], spin;
static const float sw_at[3] = {2500.0f, 168.0f, 1501.0f}, ball_root[3] = {2495.0f, BALL_TOP, 2750.0f};
/* Where the ball is: under its root by its drop, swung out by its offset. */
static void ball_where(float at[3]) {
    float o2 = ball_off[0] * ball_off[0] + ball_off[1] * ball_off[1], l = ball_drop;
    at[0] = ball_root[0] + ball_off[0];
    at[2] = ball_root[2] + ball_off[1];
    at[1] = ball_root[1] - (l * l > o2 ? sqrtf(l * l - o2) : 0.0f);
}
static int stage_of(float hp) {
    return hp >= 1000.0f ? 0 : hp > 800.0f ? 1 : hp > 600.0f ? 2 : hp > 400.0f ? 3 : 4;
}
static int ball_out(void) { return ball_state == BALL_LOWERING || ball_state == BALL_DOWN; }

void m7_reset(void) {
    room_now = -1;
    switch_hits = switch_lit = ball_broken = 0;
    ball_state = BALL_UP; ball_hp = BALL_HP; ball_timer = ball_drop = 0.0f;
    ball_off[0] = ball_off[1] = ball_vel[0] = ball_vel[1] = 0.0f;
    ambush_step = ambush_done = 0;
    spin = 0.0f;
}
/* 136's return (EventTbl07 block 2: flag 2 clear, item 45 got): door_lock, spawn_group 2. Its ops: wait 60, a Greed
 * at (2330, 0, 2520); wait 60, a Greed at (2417, -12, 1951); once they're dead and 60 more, a Hell Vanguard (EST 26)
 * at (2586, 0, 2558). Cleared, the doors open and flag 2 is set. */
void m7_enter(int room, int has_skull, float floor_y) {
    room_now = room;
    if (room == 136 && has_skull && !ambush_done) { ambush_step = 1; ambush_clock = 0.0f; ambush_y = floor_y; }
    else ambush_step = 0;
}
/* 127's door into 114 lands on its balcony at 9000 (start 1), where the game has Dante drop over the railing to
 * the ground floor; the port's collision there keeps him on the balcony, so the door takes him to the ground
 * floor's start 0 instead [approximation]. He climbs back past the balcony on the jump pads. */
int m7_door_route(int room, int door, int *next_room, int *start) {
    if (room == 127 && door == 1) { *next_room = 114; *start = 0; return 1; }
    return 0;
}
int m7_fight_on(void) { return ambush_step != 0; }
int m7_item_shown(int item) { return item != 45 || ball_broken; }
void m7_status(char *out, int size) {
    snprintf(out, size, "m7 room=%d switch=%d/%d ball state=%d hp=%.0f stage=%d timer=%.0f drop=%.0f broken=%d", room_now,
             switch_hits, switch_lit, ball_state, ball_hp, stage_of(ball_hp), ball_timer, ball_drop, ball_broken);
}

static int in_reach(const float at[3], float r, float x, float y, float z, float yaw, float reach) {
    float dx = at[0] - x, dz = at[2] - z, d = sqrtf(dx * dx + dz * dz), dy = at[1] - y;
    if (d > reach + r || dy < -150.0f || dy > 300.0f + (r > 100.0f ? r : 0.0f)) return 0;   /* the ball: its bottom in reach */
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
    if (switch_lit && ball_out()) {
        float at[3], damage = heavy ? 84.0f : 42.0f, dx, dz, d;
        ball_where(at);
        if (!in_reach(at, BALL_R, x, y, z, yaw, reach)) return;
        fx_hit_spark(at);
        sfx_play_room(0x3d, 0x21, 0.0f, 1.0f);
        /* FUN_00397120: pushed along the blow by damage / its weight. */
        dx = at[0] - x; dz = at[2] - z; d = sqrtf(dx * dx + dz * dz);
        if (d < 1.0f) { dx = sinf(yaw); dz = cosf(yaw); d = 1.0f; }
        ball_vel[0] += dx / d * damage / BALL_MASS;
        ball_vel[1] += dz / d * damage / BALL_MASS;
        if (stage_of(ball_hp) != stage_of(ball_hp - damage)) fx_explosion(at);   /* a stage of its shell breaks off */
        ball_hp -= damage;
        if (ball_hp < 0.0f) {
            ball_state = BALL_BROKEN; ball_broken = 1;   /* FUN_00396740: flag 37, the skull */
            sfx_play_room(0x3e, 0x21, 0.0f, 1.0f);
            fx_explosion(at);
        }
    }
}
void m7_push_out(float *x, float *z, float y, float r) {
    float dx, dz, d, min = BALL_R + r, at[3];
    if (room_now != 105 || !ball_out()) return;
    ball_where(at);
    if (y > at[1] + BALL_R || y + 180.0f < at[1] - BALL_R) return;
    dx = *x - at[0]; dz = *z - at[2];
    d = sqrtf(dx * dx + dz * dz);
    if (d >= min) return;
    if (d < 0.01f) { dx = 0.0f; dz = 1.0f; d = 1.0f; }
    *x = at[0] + dx / d * min;
    *z = at[2] + dz / d * min;
}
void m7_update(float dt) {
    float f = dt * 60.0f;
    spin += f;
    if (room_now == 136 && ambush_step) {
        ambush_clock += f;
        enemy_spawn_height(ambush_y);
        if (ambush_step == 1 && ambush_clock > 60.0f) { enemy_add_kind(ENEMY_GREED, 2330.0f, 2520.0f); ambush_step = 2; ambush_clock = 0.0f; }
        else if (ambush_step == 2 && ambush_clock > 60.0f) { enemy_add_kind(ENEMY_GREED, 2417.0f, 1951.0f); ambush_step = 3; ambush_clock = 0.0f; }
        else if (ambush_step == 3) { if (enemy_alive_count() > 0) ambush_clock = 0.0f; else if (ambush_clock > 60.0f) { enemy_add_kind(ENEMY_VANGUARD, 2586.0f, 2558.0f); ambush_step = 4; ambush_clock = 0.0f; } }
        else if (ambush_step == 4 && ambush_clock > 30.0f && enemy_alive_count() == 0) { ambush_step = 0; ambush_done = 1; }
    }
    if (room_now != 105) return;
    if (ball_state == BALL_UP && switch_lit) { ball_state = BALL_LOWERING; ball_timer = BALL_TIME; }   /* flag 36 */
    if (ball_out() && (ball_timer -= f) < 0.0f) ball_state = BALL_RISING;   /* FUN_00396320 */
    if (ball_state == BALL_LOWERING && (ball_drop += 20.0f * f) >= BALL_LENGTH) {
        ball_drop = BALL_LENGTH; ball_state = BALL_DOWN;
        ball_vel[0] += 3.0f; ball_vel[1] += 3.0f;   /* FUN_00396640: a push of 3 as it arrives */
        sfx_play_room(0xa3, 0x21, 0.0f, 1.0f);
    }
    if ((ball_state == BALL_RISING || ball_state == BALL_BROKEN) && ball_drop > 0.0f && (ball_drop -= 10.0f * f) <= 0.0f) {
        ball_drop = 0.0f;
        if (ball_state == BALL_RISING) {
            /* At the top its shell is whole again (FUN_00396360); the switch must be lit again [inferred]. */
            ball_state = BALL_UP; ball_hp = BALL_HP; switch_lit = 0; switch_hits = 0;
        }
    }
    {   /* The swing: a pendulum on its chain, kept at 0.998 a frame as spheres.c's (gravity 1 cm a frame squared [estimate]). */
        float l = ball_drop > 100.0f ? ball_drop : 100.0f;
        int k;
        for (k = 0; k < 2; ++k) {
            ball_vel[k] += -ball_off[k] / l * f;
            ball_vel[k] *= powf(0.998f, f);
            ball_off[k] += ball_vel[k] * f;
        }
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
    float y;
    if (room_now != 105) return;
    if (gim_has_switch(100)) {
        /* The crest switch's own model (gimmick_models.c): a flame out for each eighth of its hits, spinning once lit
         * [the port's hit count, not CStageSetSwitch's charge]. */
        float push[8];
        int lit = switch_lit ? 8 : switch_hits * 8 / SWITCH_HITS;
        for (i = 0; i < 8; ++i) push[i] = i < lit ? 23.0f : 0.0f;
        gim_switch_pose(100, switch_lit ? fmodf(spin * 60.0f, 180.0f) : 0.0f, switch_lit ? fmodf(spin * 20.0f, 180.0f) : 0.0f, push, 0);
    } else
    /* Stand-ins (the stage's own models aren't converted): the switch's hub, red once lit... */
    box(sw_at[0], sw_at[1] - 25.0f, sw_at[1] + 25.0f, sw_at[2], 25.0f, switch_lit ? 0xff3050ffu : 0xff606870u,
        switch_lit ? 0xff2030c0u : 0xff404850u);
    if (ball_drop <= 0.0f || ball_broken) return;
    /* ...and the ball: a blue core in its grey shell pieces (fewer by its stage), on its chain from the ceiling. */
    {
        float at[3];
        ball_where(at);
        y = at[1];
        box(at[0], y + BALL_R, BALL_TOP, at[2], 6.0f, 0xff404040u, 0xff303030u);
        box(at[0], y - 60.0f, y + 60.0f, at[2], 60.0f, 0xffff6020u, 0xffc04010u);
        left = 8 - 2 * stage_of(ball_hp);
        for (i = 0; i < left; ++i) {
            float a = (i * 45.0f + spin * 0.5f) * 0.0174533f;
            box(at[0] + sinf(a) * 75.0f, y - 50.0f + (i & 1) * 40.0f, y + 10.0f + (i & 1) * 40.0f, at[2] + cosf(a) * 75.0f, 38.0f,
                0xff909498u, 0xff60666cu);
        }
    }
}
