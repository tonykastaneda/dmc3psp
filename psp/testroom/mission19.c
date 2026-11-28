#include "mission19.h"

/* 404, Infinity Nirvana: with the Samsara taken, the large mirror that appears takes Dante out by 403 (Lost Soul
 * Nirvana) and 406's mirror back to 405, where it's used. The port takes him straight to 405's start 0
 * [approximation: 403 and 406 are only passed through]. */
int m19_door_route(int room, int door, int has_samsara, int *next_room, int *start) {
    (void)door;
    if (room == 404 && has_samsara) { *next_room = 405; *start = 0; return 1; }
    return 0;
}

/* 404's hourglass (EventTbl19's room 404, decoded):
 *   entering with flag 2 clear (the Samsara not yet taken): op 7f [5400, 1] and op 81 start a 5400-frame timer
 *     (90 s) on the HUD (CUIHud's +0x790 block); the sets run their motion 0 (cmd2c [100/101, 0]).
 *   a block waits on switch 0x22 (that timer run out); then the glass turns (cmd2c [100, 2] and [103, 2], set 101's
 *     sand off and 102's on), group 1 is spawned (3 Abyss), then 101 back, group 0 again (5 Abyss), and the timer
 *     restarts: the Abyss come back each time the sand runs out.
 *   once the room's enemies are cleared: cmd2c [100, 1, 1] and [101, 1, 1] stop it (motion 1, a still pose) and
 *     the path (set 200) is raised.
 * The parts' motions are the room's own (PNST 10N+1, tools/gen_rooms.py write_hourglass): the frame and sand
 * spin with bone 1 (155 radians over 10800 frames), the sphere rolls with its bone 1 (12.57 over 4320), and the sand
 * drains with bone 2 over 5400 frames (the upper sand shrinks to nothing and drops, the lower pile grows six times,
 * the falling stream thins out at the end). The port's: each part turns as one piece (the frame's rings and the
 * sphere's inner hoops have their own bones, not played), the turn of the glass (motion 2, and set 102's sand) isn't
 * played, the timer shows as a countdown at the top of the screen, and on running out the Abyss still standing
 * are taken away and group 0 is run again (group 1's three aren't added). */
#include <math.h>
#include <stdio.h>
#include "stage.h"
#include "hourglass_runtime.h"
#include "enemy.h"
#include "fx.h"
#include "nfx.h"

#define SAND_FRAMES 5400.0f
static int hg_on, hg_running, hg_turns;
static float hg_spin, hg_sand;

static float hg_channel(const HourglassChannel *c, float t, int loop) {
    const HourglassKey *k = &hourglass_keys[c->first];
    int i;
    float last = (float)k[c->count - 1].frame;
    if (loop && last > 0.0f) t = fmodf(t, last);
    if (t <= k[0].frame) return k[0].value;
    for (i = 1; i < c->count; ++i)
        if (t <= k[i].frame) {
            float span = (float)(k[i].frame - k[i - 1].frame);
            return k[i - 1].value + (k[i].value - k[i - 1].value) * (span > 0.0f ? (t - k[i - 1].frame) / span : 1.0f);
        }
    return k[c->count - 1].value;
}

static void hg_pose(void) {
    int i, j;
    for (i = 0; i < HOURGLASS_PART_COUNT; ++i) {
        const HourglassPart *p = &hourglass_parts[i];
        float move[3] = {0, 0, 0}, turn[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
        if (p->hidden) { scale[0] = scale[1] = scale[2] = 0.0f; }
        for (j = 0; j < p->count; ++j) {
            const HourglassChannel *c = &hourglass_channels[p->first + j];
            if (c->axis >= 3 && c->axis <= 5) turn[c->axis - 3] = hg_channel(c, hg_spin, 1);
            else if (c->axis < 3) move[c->axis] = hg_channel(c, hg_sand, 0);
            else scale[c->axis - 6] = hg_channel(c, hg_sand, 0);
        }
        stage_set_group_motion_scaled(p->tag, p->pivot, move, turn, p->pivot2, scale);
    }
}

/* 405's mirror set (SET 444 MIRROR: CStageSetMirror, vtable 0x60f530; FUN_004621f0 its start, FUN_004624b0 its round,
 * FUN_00462890 a hit, FUN_00462bd0 a break). Eight mirrors (0x5ccfe0: place and turn; STAY sets 445-452, 0x5cd090);
 * one at a time is lit green (175, 255, 223), another unbroken one every 420 frames (+0x110), at random. Only the lit
 * one takes blows: a box 300 wide, 650 high, 200 deep, 60 before it (0x5cd060), 1000 health each (+0xf0). Each blow
 * throws every enemy in the room (FUN_0035a610); a broken one shatters (effect 0x1ea), its STAY goes, and the others'
 * health is whole again; with two broken the set is done and the Abyss bound to it die (FUN_0035a720, each frame).
 * Coming back (flag 1, gimmick_cmd10 444 1), they're all broken. */
static const float mirror_at[8][4] = {{1679, 0, 2784, 112.5f}, {1679, 0, 2137, 67.5f}, {2136, 0, 1679, 22.5f},
    {2783, 0, 1679, -22.5f}, {3241, 0, 2136, -67.5f}, {3241, 0, 2783, -112.5f}, {2784, 0, 3241, -157.5f},
    {2137, 0, 3241, 157.5f}};
static int mr_on, mr_state, mr_lit, mr_broken[8], mr_breaks, mr_blows, mr_struck;
static float mr_health[8], mr_clock, mr_from[2];
static unsigned int mr_seed = 0x2545f491u;
static int mr_rand(int n) { mr_seed ^= mr_seed << 13; mr_seed ^= mr_seed >> 17; mr_seed ^= mr_seed << 5; return (int)(mr_seed % (unsigned int)n); }

static void mr_pick(void) {
    int open[8], n = 0, i;
    for (i = 0; i < 8; ++i) if (!mr_broken[i] && i != mr_lit) open[n++] = i;
    if (n) mr_lit = open[mr_rand(n)];
    mr_clock = 420.0f;
}

static void mr_enter(int room, int done) {
    int i;
    mr_on = room == 405;
    mr_struck = 0;
    if (!mr_on) return;
    for (i = 0; i < 8; ++i) { mr_broken[i] = done; mr_health[i] = 1000.0f; if (done) stage_set_group_mode(304 + i, 2); }   /* STAY 445 + i: tools/prepare_stage.py tags 304-311 */
    mr_breaks = done ? 2 : 0;
    mr_state = done ? 2 : 1;
    mr_lit = mr_rand(8);
    mr_clock = 420.0f;
}

void m19_test_mirror(int k) {   /* tests: "mirrorlit frame k" lights mirror k; k + 8 breaks it */
    if (mr_on && k >= 8 && k < 16 && !mr_broken[k - 8]) { mr_lit = k - 8; mr_health[mr_lit] = -1.0f; return; }
    if (mr_on && k >= 0 && k < 8 && !mr_broken[k]) { mr_lit = k; mr_clock = 1e9f; }
}

void m19_mirror_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    const float *m;
    float a, s, c, lx, lz, ly, dx, dz, nx, nz;
    if (!mr_on || mr_state != 1 || mr_broken[mr_lit]) return;
    m = mirror_at[mr_lit];
    a = m[3] * 3.14159265f / 180.0f; s = sinf(a); c = cosf(a);
    /* Dante in the mirror's frame: its box spans x -150..150, y 0..650, z -40..160. */
    dx = x - m[0]; dz = z - m[2];
    lx = dx * c - dz * s; lz = dx * s + dz * c; ly = y - m[1];
    nx = lx < -150.0f ? -150.0f - lx : lx > 150.0f ? lx - 150.0f : 0.0f;
    nz = lz < -40.0f ? -40.0f - lz : lz > 160.0f ? lz - 160.0f : 0.0f;
    if (nx * nx + nz * nz > reach * reach || ly + 250.0f < 0.0f || ly > 650.0f) return;
    {
        /* facing it */
        float cx = m[0] + s * 60.0f - x, cz = m[2] + c * 60.0f - z, l = sqrtf(cx * cx + cz * cz);
        if (l > 1.0f && (cx * sinf(yaw) + cz * cosf(yaw)) / l < 0.0f) return;
    }
    mr_health[mr_lit] -= heavy ? 84.0f : 42.0f;   /* the port's hit values */
    ++mr_blows;
    mr_struck = 1; mr_from[0] = x; mr_from[1] = z;
}

static void mr_update(float f) {
    int i;
    if (!mr_on) return;
    if (mr_state == 2) { enemy_mirror_kill(); return; }
    if (mr_struck) { mr_struck = 0; enemy_mirror_blow(mr_from[0], mr_from[1]); }
    if (mr_health[mr_lit] < 0.0f && !mr_broken[mr_lit]) {
        float at[3] = {mirror_at[mr_lit][0], 325.0f, mirror_at[mr_lit][2]};
        mr_broken[mr_lit] = 1;
        stage_set_group_mode(304 + mr_lit, 2);   /* its STAY (445 + k), tag 304 + k */
        nfx_spawn(NFX_MIRROR, 0x1ea, at, mirror_at[mr_lit][3] * 0.0174533f, 0);   /* the shatter (FUN_00462f90), P113 */
        if (++mr_breaks > 1) { mr_state = 2; enemy_mirror_kill(); return; }
        for (i = 0; i < 8; ++i) if (!mr_broken[i]) mr_health[i] = 1000.0f;
        mr_pick();
    }
    if ((mr_clock -= f) < 0.0f) mr_pick();
    {
        /* The lit mirror's glow: its effect is E0x2e0 (FUN_004621f0), a model (M155) whose texture is a 1-byte
         * reference the port doesn't decode, so a green glow stands in. */
        float at[3] = {mirror_at[mr_lit][0], 325.0f, mirror_at[mr_lit][2]}, v[3] = {0.0f, 0.0f, 0.0f};
        fx_mote(at, v, 260.0f, 240.0f, 0.05f, 0xa0dfffafu);
    }
}
void m19_enter(int room, int stopped) {
    mr_enter(room, stopped);
    hg_on = room == 404;
    hg_running = hg_on && !stopped;
    hg_spin = 0.0f;
    hg_sand = hg_running ? 0.0f : SAND_FRAMES + 100.0f;   /* stopped: the sand has run through */
    if (hg_on) hg_pose();
}

int m19_update(float dt, int cleared) {
    int out = 0;
    mr_update(dt * 60.0f);
    if (!hg_on) return 0;
    if (hg_running && cleared) hg_running = 0;   /* the enemies cleared stop it, as it stands */
    if (hg_running) {
        hg_spin += dt * 60.0f;
        hg_sand += dt * 60.0f;
        if (hg_sand >= SAND_FRAMES) { hg_sand = 0.0f; ++hg_turns; out = 1; }   /* run out: the Abyss come back */
    }
    hg_pose();
    return out;
}

int m19_clock_text(char *out, int size) {
    int left;
    if (!hg_on || !hg_running) return 0;
    left = (int)((SAND_FRAMES - hg_sand) / 60.0f + 0.999f);
    snprintf(out, size, "%d:%02d", left / 60, left % 60);
    return 1;
}

void m19_status(char *out, int size) {
    snprintf(out, size, "hourglass on=%d running=%d sand=%.0f turns=%d mirrors on=%d state=%d lit=%d breaks=%d blows=%d hp=%.0f", hg_on, hg_running, hg_sand, hg_turns,
             mr_on, mr_state, mr_lit, mr_breaks, mr_blows, mr_on ? mr_health[mr_lit] : 0.0f);
}
