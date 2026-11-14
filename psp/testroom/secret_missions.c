/* The Secret Missions.
 *
 * Where each is entered: the main missions' rooms carry an EVE area of kind 7 whose id is the Secret Mission's number
 * (room 005 area 3 is Secret Mission 1, 113 area 6 is 2, 127 area 1 is 3, 300 area 8 is 4, 209 area 1 is 5, 206 area 3
 * is 6, 212 area 1 is 7, 230 area 0 is 8, 233 area 1 is 9, 146 area 2 is 10, 140 area 0 is 11, 402 area 0 is 12).
 * Touching the red structure there (O, in the port) takes Dante to the Secret Mission's own room, st600-st611: Secret
 * Mission n is room 599 + n. Cleared or failed, he's put back where he entered.
 *
 * What each asks (the rules as the game states them; their code isn't decoded, so the checks are the port's):
 *   1  600  beat every enemy (Prides, Lusts, Sloths)             [the rule isn't known: inferred from the room]
 *   2  601  beat every enemy (4 Enigmas) without taking a hit
 *   3  602  stay in the air for 20 seconds (in all: the port adds up his time off the ground)
 *   4  603  ride the elevator to the top; it holds while a wave is on it (113's riding lift)
 *   5  604  break every prop in the room within 40 seconds
 *   7  606  reach the far end (the room's area 1, where the Blue Orb Fragment lies) within 30 seconds
 *   8  607  beat every enemy (5 Arachnes, 2 Hell Vanguards)
 *  11  610  reach the far end (area 2) [the spike trap, CStageSetNeedle, isn't in]
 *  12  611  reach area 0 [its moving blocks aren't in: the room's own floor]
 * Not in yet: 6 (the red orbs: 605's ITM records), 9 (the wagons, 608's TRUCK) and 10 (the crystal puzzle, 609).
 * The award, a Blue Orb Fragment, is only named: the port has no Devil Trigger gauge to lengthen yet. */
#include <math.h>
#include <stdio.h>
#include <pspctrl.h>
#include "secret_missions.h"
#include "fx.h"

enum { R_KILL, R_KILL_CLEAN, R_AIR, R_LIFT, R_BREAK, R_REACH, R_NONE };
typedef struct {
    short host;                  /* the main mission's room it's entered from */
    float at[3];                 /* the kind-7 area's centre there */
    short rule;
    float limit;                 /* seconds, 0: none */
    float goal[4];               /* R_REACH: x0, x1, z0, z1 of the goal area */
} SecretMission;
static const SecretMission secrets[12] = {
    {5, {5447.0f, 450.0f, 2861.0f}, R_KILL, 0.0f, {0}},
    {113, {2879.0f, -80.0f, 1556.0f}, R_KILL_CLEAN, 0.0f, {0}},
    {127, {3021.0f, 645.0f, 1220.0f}, R_AIR, 0.0f, {0}},
    {300, {5272.0f, 21.0f, -141.0f}, R_LIFT, 0.0f, {0}},
    {209, {1993.0f, 2141.0f, 2776.0f}, R_BREAK, 40.0f, {0}},
    {206, {10221.0f, 3000.0f, 7087.0f}, R_NONE, 0.0f, {0}},
    {212, {2428.0f, 811.0f, 3133.0f}, R_REACH, 30.0f, {11221.0f, 11570.0f, 7001.0f, 7350.0f}},
    {230, {3645.0f, 580.0f, 3529.0f}, R_KILL, 0.0f, {0}},
    {233, {2911.0f, 401.0f, 1220.0f}, R_NONE, 0.0f, {0}},
    {146, {3784.0f, -34.0f, 2507.0f}, R_NONE, 0.0f, {0}},
    {140, {2496.0f, 856.0f, 2395.0f}, R_REACH, 0.0f, {9116.0f, 9462.0f, 1757.0f, 1898.0f}},
    {402, {2413.0f, 1720.0f, -3093.0f}, R_REACH, 0.0f, {3837.0f, 3937.0f, 3306.0f, 3706.0f}},
};
#define AIR_NEEDED 20.0f
#define ENTRY_RANGE 200.0f
#define END_WAIT 2.5f

static int current = -1;           /* the Secret Mission being played (0-11), or -1 */
static int ending;                  /* 1 cleared, 2 failed: waiting to go back */
static unsigned int cleared;        /* bit n: Secret Mission n + 1 cleared this session */
static float clock_, air, start_health, end_wait, caption_time, spin;
static const char *caption;
static int room_seen = -1;
static char caption_buf[48];

static void say(const char *text, float seconds) { caption = text; caption_time = seconds; }

void sm_reset(void) { current = -1; ending = 0; caption = 0; caption_time = 0.0f; }
int sm_active(void) { return current >= 0; }

static int rule_met(const SecretMission *s, const SmView *v) {
    switch (s->rule) {
    case R_KILL: case R_KILL_CLEAN: return v->fight_done && v->alive == 0;
    case R_AIR: return air >= AIR_NEEDED;
    case R_LIFT: return v->lift_top;
    case R_BREAK: return v->breakables_left == 0;
    case R_REACH: return v->x > s->goal[0] && v->x < s->goal[1] && v->z > s->goal[2] && v->z < s->goal[3];
    }
    return 0;
}

int sm_update(float dt, const SmView *v, int *prompt, int *go_room, int *go_start, float go_place[4]) {
    int n;
    spin += dt;
    room_seen = v->room;
    if (caption_time > 0.0f) caption_time -= dt;
    go_place[3] = 0.0f;
    if (current < 0) {
        /* A main mission: the red structure where one is entered. */
        for (n = 0; n < 12; ++n) {
            const SecretMission *s = &secrets[n];
            float dx = v->x - s->at[0], dz = v->z - s->at[2];
            if (s->host != v->room || s->rule == R_NONE || dx * dx + dz * dz > ENTRY_RANGE * ENTRY_RANGE || fabsf(v->y - s->at[1]) > 300.0f) continue;
            *prompt = 1;
            if (v->pressed & PSP_CTRL_CIRCLE) {
                current = n; ending = 0; clock_ = 0.0f; air = 0.0f; start_health = v->health;
                snprintf(caption_buf, sizeof caption_buf, "SECRET MISSION %d", n + 1);
                say(caption_buf, 2.5f);
                *go_room = 600 + n; *go_start = 0;
                return 1;
            }
        }
        return 0;
    }
    {
        const SecretMission *s = &secrets[current];
        if (v->room != 600 + current) return 0;   /* still on the way in */
        if (ending) {
            end_wait -= dt;
            if (end_wait > 0.0f) return 0;
            /* Back where he came in. */
            *go_room = s->host; *go_start = 0;
            go_place[0] = s->at[0]; go_place[1] = s->at[1]; go_place[2] = s->at[2]; go_place[3] = 1.0f;
            current = -1; ending = 0;
            return 1;
        }
        clock_ += dt;
        if (!v->on_ground) air += dt;
        if (s->rule == R_KILL_CLEAN && v->health < start_health - 0.5f) {
            ending = 2; end_wait = END_WAIT; say("MISSION FAILED", END_WAIT);
            return 0;
        }
        if (rule_met(s, v)) {
            ending = 1; end_wait = END_WAIT; cleared |= 1u << current;
            say("SECRET MISSION CLEAR  BLUE ORB FRAGMENT", END_WAIT);
            return 0;
        }
        if (s->limit > 0.0f && clock_ >= s->limit) {
            ending = 2; end_wait = END_WAIT; say("MISSION FAILED", END_WAIT);
            return 0;
        }
        if (s->limit > 0.0f || s->rule == R_AIR) {
            /* The clock (or the time in the air), as a line. */
            float shown = s->rule == R_AIR ? air : s->limit - clock_;
            snprintf(caption_buf, sizeof caption_buf, s->rule == R_AIR ? "AIR %d.%d / 20" : "TIME %d.%d", (int)shown, (int)(shown * 10.0f) % 10);
            caption = caption_buf; caption_time = 0.2f;
        }
    }
    return 0;
}

const char *sm_caption(float *alpha) {
    if (!caption || caption_time <= 0.0f) return 0;
    *alpha = caption_time > 0.5f ? 1.0f : caption_time * 2.0f;
    return caption;
}

void sm_draw(void) {
    /* The red structure: a red glow where one can be entered. */
    int n;
    for (n = 0; current < 0 && n < 12; ++n) {
        const SecretMission *s = &secrets[n];
        float at[3] = {s->at[0], s->at[1] + 120.0f, s->at[2]}, still[3] = {0.0f, 0.0f, 0.0f};
        if (s->host != room_seen || s->rule == R_NONE || (cleared >> n & 1)) continue;
        fx_mote(at, still, 90.0f + 15.0f * sinf(spin * 5.0f), 80.0f, 0.05f, 0xc02020ffu);
    }
}

void sm_status(char *out, int size) {
    snprintf(out, size, "sm=%d end=%d clock=%.1f air=%.1f cleared=%03x", current + 1, ending, clock_, air, cleared);
}
