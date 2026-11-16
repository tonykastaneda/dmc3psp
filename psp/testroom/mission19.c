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

void m19_enter(int room, int stopped) {
    hg_on = room == 404;
    hg_running = hg_on && !stopped;
    hg_spin = 0.0f;
    hg_sand = hg_running ? 0.0f : SAND_FRAMES + 100.0f;   /* stopped: the sand has run through */
    if (hg_on) hg_pose();
}

int m19_update(float dt, int cleared) {
    int out = 0;
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
    snprintf(out, size, "hourglass on=%d running=%d sand=%.0f turns=%d", hg_on, hg_running, hg_sand, hg_turns);
}
