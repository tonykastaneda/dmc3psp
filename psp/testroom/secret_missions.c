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
 *   6  605  collect every red orb (the room's 45 item records) within 60 seconds [the limit is an estimate]: most
 *           float over the drops, reached with Air Raid
 *   7  606  reach the far end (the room's area 1, where the Blue Orb Fragment lies) within 30 seconds
 *           [the route is about 160 m, round the corner at (6118, 5668) and down the ramp: in Devil Trigger, at
 *           the PS2's 731 cm/s, it clears in about 28.6 s. Riding an enemy, the guide's quicker way, isn't in]
 *   8  607  beat every enemy (5 Arachnes, 2 Hell Vanguards)
 *  10  609  light the end crystal: the beam puzzle below, from the game's own code and layouts
 *  11  610  reach the far end (area 2) [the spike trap, CStageSetNeedle, isn't in]
 *  12  611  reach area 0, over the moving blocks (blocks.c)
 *   9  608  let no enemy escape: the wagons below, a stand-in for CStageSetTruck
 * The award, a Blue Orb Fragment, is only named: the port has no Devil Trigger gauge to lengthen yet. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <pspctrl.h>
#include "secret_missions.h"
#include "fx.h"
#include "sfx.h"
#include "nfx.h"
#include <stdlib.h>
#include "crystal_runtime.h"
#include "secret_orbs_runtime.h"
#include "enemy.h"
#include "bullets.h"

enum { R_KILL, R_KILL_CLEAN, R_AIR, R_LIFT, R_BREAK, R_REACH, R_CRYSTAL, R_ORBS, R_WAGONS, R_NONE };
typedef struct {
    short host;                  /* the main mission's room it's entered from */
    float at[3];                 /* the kind-7 area's centre there */
    short rule;
    float limit;                 /* seconds, 0: none */
    float goal[4];               /* R_REACH: x0, x1, z0, z1 of the goal area */
} SecretMission;
/* Secret Mission 6's time limit [ESTIMATE]: the mission screen says "in time" without a number, and no limit was found
 * in the executable (no per-mission table, and none of the 40 s / 30 s / 20 s constants of the other timed missions
 * sits beside a candidate) or in st605's data. 60 s leaves room for the two Air Raid flights. Secret Mission 5's 40 s
 * and 7's 30 s are the mission screens' own. */
#define SM6_LIMIT 60.0f
static const SecretMission secrets[12] = {
    {5, {5447.0f, 450.0f, 2861.0f}, R_KILL, 0.0f, {0}},
    {113, {2879.0f, -80.0f, 1556.0f}, R_KILL_CLEAN, 0.0f, {0}},
    {127, {3021.0f, 645.0f, 1220.0f}, R_AIR, 0.0f, {0}},
    {300, {5272.0f, 21.0f, -141.0f}, R_LIFT, 0.0f, {0}},
    {209, {1993.0f, 2141.0f, 2776.0f}, R_BREAK, 40.0f, {0}},
    {206, {10221.0f, 3000.0f, 7087.0f}, R_ORBS, SM6_LIMIT, {0}},
    {212, {2428.0f, 811.0f, 3133.0f}, R_REACH, 30.0f, {11221.0f, 11570.0f, 7001.0f, 7350.0f}},
    {230, {3645.0f, 580.0f, 3529.0f}, R_KILL, 0.0f, {0}},
    {233, {2911.0f, 401.0f, 1220.0f}, R_WAGONS, 0.0f, {0}},
    {146, {3784.0f, -34.0f, 2507.0f}, R_CRYSTAL, 0.0f, {0}},
    {140, {2496.0f, 856.0f, 2395.0f}, R_REACH, 0.0f, {9116.0f, 9462.0f, 1757.0f, 1898.0f}},
    {402, {2413.0f, 1720.0f, -3093.0f}, R_REACH, 0.0f, {3837.0f, 3937.0f, 3306.0f, 3706.0f}},
};
/* ---- Secret Mission 10's light beam: CStageSetCrystal (FUN_0041f460 lays a layout out on a 20 x 20 grid, 200 cm a
 * cell from the set's place; FUN_0041fbf0 runs the beam from the start crystal: a relay turns it to its own heading,
 * the cover or the grid's edge stops it, the end crystal solves it; FUN_00420450 smashes relays and the cover when
 * struck). 609's set is "no 2": a layout picked at random from 2-7 (FUN_0041f160). Its crest switch, 100, puts the
 * crystals back (the guide's flame panel). Crystals are glows, the beam a line: their models aren't converted. */
#define GRID 20
#define CELL 200.0f
static float crystal_origin[3] = {1700.0f, 164.0f, 299.0f}, crystal_reset[3] = {2102.0f, 150.0f, 1685.0f};
static int layout = -1, solved;
static int crystal_room = -1;   /* a main mission's puzzle (Mission 9's 203, "no 0"), or -1 */
static unsigned char present[24];
static signed char grid[GRID][GRID];   /* [y][x]: the crystal there, or -1 */
static short beam[32][2], beam_count;
static int crystal_seen = -1;

static void crystal_place(float out[3], int k) {
    const CrystalRecord *r = &crystal_layouts[layout].recs[k];
    out[0] = crystal_origin[0] + r->x * CELL; out[1] = crystal_origin[1]; out[2] = crystal_origin[2] + r->y * CELL;
}

/* FUN_0041fbf0: 1 if the beam reaches the end crystal. Its path, cell by cell, into beam[]. */
static int crystal_trace(void) {
    static const signed char step[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
    const CrystalLayout *l = &crystal_layouts[layout];
    int k, x = -1, y = -1, dir = 0, guard;
    unsigned char lit[24] = {0};
    for (k = 0; k < l->count; ++k) if (l->recs[k].type == 0) { x = l->recs[k].x; y = l->recs[k].y; dir = l->recs[k].dir; }
    beam_count = 0;
    if (x < 0) return 0;
    beam[beam_count][0] = (short)x; beam[beam_count][1] = (short)y; ++beam_count;
    for (guard = 0; guard < 400; ++guard) {
        int c;
        x += step[dir & 7][0]; y += step[dir & 7][1];
        if (x < 0 || x >= GRID || y < 0 || y >= GRID) break;   /* out of the grid: FUN_0041ff40 */
        c = grid[y][x];
        if (c < 0 || !present[c]) continue;
        if (beam_count < 32) { beam[beam_count][0] = (short)x; beam[beam_count][1] = (short)y; ++beam_count; }
        if (lit[c]) return 0;
        lit[c] = 1;
        switch (l->recs[c].type) {
        case 1: return 1;                       /* the end: solved */
        case 2: dir = l->recs[c].dir; break;   /* a relay turns it */
        default: return 0;                      /* the cover, or a start crystal, stops it */
        }
    }
    if (beam_count < 32) { beam[beam_count][0] = (short)x; beam[beam_count][1] = (short)y; ++beam_count; }
    return 0;
}

static void crystal_reset_all(void) {
    const CrystalLayout *l = &crystal_layouts[layout];
    int k, x, y;
    for (y = 0; y < GRID; ++y) for (x = 0; x < GRID; ++x) grid[y][x] = -1;
    for (k = 0; k < l->count && k < 24; ++k) {
        present[k] = 1;
        if (l->recs[k].x >= 0 && l->recs[k].x < GRID && l->recs[k].y >= 0 && l->recs[k].y < GRID) grid[l->recs[k].y][l->recs[k].x] = (signed char)k;
    }
    solved = crystal_trace();
}

static int forced_layout = -1;
void sm_test_layout(int n) { forced_layout = n; }

static void crystal_start(void) {
    static const float origin[3] = {1700.0f, 164.0f, 299.0f}, reset[3] = {2102.0f, 150.0f, 1685.0f};
    memcpy(crystal_origin, origin, sizeof origin); memcpy(crystal_reset, reset, sizeof reset);
    crystal_room = -1;
    layout = forced_layout >= 0 && forced_layout < 8 ? forced_layout : 2 + rand() % 6;   /* "no 2": one of 2-7 */
    crystal_reset_all();
}

/* A main mission's beam puzzle: the room's CRYSTAL set at origin, its layout ("no" 0 and 1 are fixed layouts; FUN_0041f160
 * picks one of 2-7 for "no 2"), struck back by the switch at reset. Solved stays solved while it's the same room's. */
void sm_crystal_room(int room, const float origin[3], const float reset[3], int no) {
    if (crystal_room == room && layout == no) return;
    memcpy(crystal_origin, origin, sizeof crystal_origin); memcpy(crystal_reset, reset, sizeof crystal_reset);
    crystal_room = room;
    layout = no;
    crystal_reset_all();
}
int sm_crystal_solved(int room) { return crystal_room == room && solved; }
static int crystal_here(void);

/* ---- Secret Mission 6's red orbs: every item record in 605 (tools/gen_rooms.py write_secret_orbs), most of them in
 * the air over the drop, where only Air Raid reaches (player.c). CItemOrb's update (FUN_002f2fa0) sets an orb
 * homing once it's within 300 cm of his position (+0x50, his feet), flying to him at up to 50 cm a frame, and
 * takes it within 50: here, within 300 cm it's his. The time limit is an estimate (SM6_LIMIT). */
static unsigned char orb_taken[SECRET_ORB_COUNT];
static int orbs_left;
static void orbs_start(void) { int k; for (k = 0; k < SECRET_ORB_COUNT; ++k) orb_taken[k] = 0; orbs_left = SECRET_ORB_COUNT; }
static void orbs_update(const SmView *v) {
    int k;
    for (k = 0; k < SECRET_ORB_COUNT; ++k) {
        float dx = secret_orbs[k][0] - v->x, dz = secret_orbs[k][2] - v->z, dy = secret_orbs[k][1] - v->y;
        if (orb_taken[k] || dx * dx + dy * dy + dz * dz > 300.0f * 300.0f) continue;
        orb_taken[k] = 1;
        --orbs_left;
    }
}
static void orbs_draw(void) {
    int k;
    float still[3] = {0.0f, 0.0f, 0.0f};
    for (k = 0; k < SECRET_ORB_COUNT; ++k)
        if (!orb_taken[k]) fx_mote(secret_orbs[k], still, 40.0f + 6.0f * sinf(k + 0.0f), 30.0f, 0.04f, 0xff2020ffu);
}

/* ---- Secret Mission 9's wagons. 608 is Dante on a rail wagon (STAY 4, a riding box; start 0 on it) beside a second
 * track: its TRUCK set (100) places the enemies' wagon at em_base (10000, 1325, 11625), 7.25 m off his line, with a
 * 400 x 100 x 200 hit box. Each enemies' wagon is a CStageSetTruckEnemy (made by an event op through FUN_00486940;
 * run by FUN_004878d0): it comes in at its speed to its stop angle, stops there while it has riders (up to three)
 * or, timed, until its clock runs out, then leaves at its second speed; a timed one that leaves with riders takes
 * them away (escaped: the truck's +0x195). It sounds 0x5c / 0x5d by turns every 30 frames (room bank, bank 0x28).
 * What isn't found is the op's caller for 608 (its wagons' stop angles, speeds, clocks and riders): the port runs
 * eight wagons (the walkthrough's: Wraths, with a Pride or a Lust on some) coming in from 35 m at 2.5 m a second,
 * stopping beside Dante for 10 s and leaving [estimates]; one that leaves with anyone on it fails the mission, all
 * eight emptied clears it. The wagon is drawn as glows: its model (the set's em_model 2) isn't converted. */
#define WAGONS 8
#define WAGON_Z 11625.0f
#define WAGON_DECK 1425.0f
#define WAGON_FROM 13500.0f
#define WAGON_TO 6500.0f
#define WAGON_SPEED 250.0f
#define WAGON_STOP 10000.0f   /* beside Dante (his wagon at the truck's base) */
#define WAGON_WAIT 10.0f      /* seconds stopped [estimate] */
static const signed char wagon_riders[WAGONS][3] = {
    {ENEMY_WRATH, ENEMY_WRATH, ENEMY_PRIDE}, {ENEMY_WRATH, ENEMY_PRIDE, -1}, {ENEMY_WRATH, ENEMY_WRATH, ENEMY_LUST},
    {ENEMY_WRATH, ENEMY_LUST, -1}, {ENEMY_WRATH, ENEMY_WRATH, ENEMY_LUST}, {ENEMY_WRATH, -1, -1},
    {ENEMY_WRATH, ENEMY_WRATH, -1}, {ENEMY_WRATH, ENEMY_WRATH, ENEMY_WRATH}};
static int wagon, wagon_live, wagon_result;
static float wagon_x, wagon_gap, wagon_wait, wagon_sound;
static int wagon_tick;
static void wagons_start(void) { wagon = 0; wagon_live = 0; wagon_result = 0; wagon_gap = 2.0f; }
static void wagons_update(float dt) {
    float dx;
    if (wagon_result) return;
    if (!wagon_live) {
        int k;
        if (wagon >= WAGONS) { wagon_result = 1; return; }
        if ((wagon_gap -= dt) > 0.0f) return;
        wagon_x = WAGON_FROM;
        enemy_spawn_height(WAGON_DECK + 100.0f);
        for (k = 0; k < 3; ++k) if (wagon_riders[wagon][k] >= 0) enemy_add_kind(wagon_riders[wagon][k], wagon_x - 120.0f + 120.0f * k, WAGON_Z);
        wagon_live = 1;
        wagon_wait = WAGON_WAIT;
    }
    if ((wagon_sound += dt * 60.0f) >= 30.0f) {   /* FUN_004878d0: 0x5c / 0x5d by turns */
        wagon_sound = 0.0f;
        sfx_play_room(wagon_tick ? 0x5d : 0x5c, 0x28, 0.0f, 1.0f);
        wagon_tick = !wagon_tick;
    }
    /* In to its stop, stopped for its clock, then away. */
    dx = wagon_x > WAGON_STOP || wagon_wait <= 0.0f ? -WAGON_SPEED * dt : 0.0f;
    if (wagon_x - WAGON_SPEED * dt <= WAGON_STOP && wagon_x > WAGON_STOP) dx = WAGON_STOP - wagon_x;
    if (dx == 0.0f) wagon_wait -= dt;
    wagon_x += dx;
    enemy_ride(wagon_x - 200.0f, wagon_x + 200.0f, WAGON_Z - 100.0f, WAGON_Z + 100.0f, dx, WAGON_DECK);
    if (enemy_alive_count() == 0) { wagon_live = 0; ++wagon; wagon_gap = 1.5f; return; }
    if (wagon_x < WAGON_TO) wagon_result = -1;   /* gone by with riders on it */
}
static void wagons_draw(void) {
    float still[3] = {0.0f, 0.0f, 0.0f};
    int k;
    if (!wagon_live) return;
    for (k = 0; k < 6; ++k) {
        float at[3] = {wagon_x - 200.0f + 80.0f * k, WAGON_DECK - 60.0f, WAGON_Z - 100.0f}, at2[3] = {at[0], at[1], WAGON_Z + 100.0f};
        fx_mote(at, still, 50.0f, 40.0f, 0.04f, 0xff2050a0u);
        fx_mote(at2, still, 50.0f, 40.0f, 0.04f, 0xff2050a0u);
    }
}

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

void sm_reset(void) { current = -1; ending = 0; caption = 0; caption_time = 0.0f; crystal_room = -1; layout = -1; }
int sm_active(void) { return current >= 0; }

static int rule_met(const SecretMission *s, const SmView *v) {
    switch (s->rule) {
    case R_KILL: case R_KILL_CLEAN: return v->fight_done && v->alive == 0;
    case R_AIR: return air >= AIR_NEEDED;
    case R_LIFT: return v->lift_top;
    case R_BREAK: return v->breakables_left == 0;
    case R_REACH: return v->x > s->goal[0] && v->x < s->goal[1] && v->z > s->goal[2] && v->z < s->goal[3];
    case R_CRYSTAL: return solved;
    case R_ORBS: return orbs_left == 0;
    case R_WAGONS: return wagon_result == 1;
    }
    return 0;
}

int sm_update(float dt, const SmView *v, int *prompt, int *go_room, int *go_start, float go_place[4]) {
    int n;
    spin += dt;
    room_seen = v->room;
    crystal_seen = v->room;
    /* 608's cart is caged by walls (attribute 0x18260001) that stop Dante, not his shots; the port's stage data
     * doesn't keep the attribute, so the shots go through every wall here. */
    bullets_through_walls = current >= 0 && secrets[current].rule == R_WAGONS && v->room == 600 + current;
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
                if (s->rule == R_CRYSTAL) crystal_start();
                if (s->rule == R_ORBS) orbs_start();
                if (s->rule == R_WAGONS) wagons_start();
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
        if (s->rule == R_ORBS) orbs_update(v);
        if (s->rule == R_WAGONS) {
            wagons_update(dt);
            if (wagon_result < 0) { ending = 2; end_wait = END_WAIT; say("MISSION FAILED", END_WAIT); return 0; }
        }
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
    }
    return 0;
}

/* The countdown (or the time in the air) while a timed one is played: drawn by main at the top of the screen. */
int sm_clock_text(char *out, int size, int *urgent) {
    const SecretMission *s;
    float shown;
    if (current < 0 || room_seen != 600 + current) return 0;
    s = &secrets[current];
    if (!(s->limit > 0.0f || s->rule == R_AIR)) return 0;
    shown = s->rule == R_AIR ? air : s->limit - clock_;
    if (shown < 0.0f) shown = 0.0f;
    snprintf(out, size, s->rule == R_AIR ? "AIR %d.%d / 20" : "TIME %d.%d", (int)shown, (int)(shown * 10.0f) % 10);
    *urgent = s->rule != R_AIR && shown < 10.0f;
    return 1;
}

const char *sm_caption(float *alpha) {
    if (!caption || caption_time <= 0.0f) return 0;
    *alpha = caption_time > 0.5f ? 1.0f : caption_time * 2.0f;
    return caption;
}

static int crystal_here(void) {
    if (layout < 0) return 0;
    if (current >= 0) return secrets[current].rule == R_CRYSTAL && crystal_seen == 600 + current && !ending;
    return crystal_room >= 0 && crystal_seen == crystal_room;
}

static void crystal_draw(void) {
    int k;
    float still[3] = {0.0f, 0.0f, 0.0f};
    for (k = 0; k < crystal_layouts[layout].count; ++k) {
        const CrystalRecord *r = &crystal_layouts[layout].recs[k];
        static const unsigned int colour[4] = {0xff40e0ffu, 0xffffffffu, 0xffffd060u, 0xffc040c0u};   /* start, end, relay, cover */
        float at[3];
        if (!present[k]) continue;
        crystal_place(at, k);
        at[1] += 100.0f;
        fx_mote(at, still, r->type == 3 ? 220.0f : r->type == 1 ? 110.0f : 70.0f, 60.0f, 0.04f, colour[r->type & 3]);
    }
    for (k = 1; k < beam_count; ++k) {
        float a[3] = {crystal_origin[0] + beam[k - 1][0] * CELL, crystal_origin[1] + 100.0f, crystal_origin[2] + beam[k - 1][1] * CELL};
        float b[3] = {crystal_origin[0] + beam[k][0] * CELL, crystal_origin[1] + 100.0f, crystal_origin[2] + beam[k][1] * CELL};
        fx_tracer(a, b, solved ? 0xffffffffu : 0xffc0f0ffu, 12.0f);
    }
}

void sm_draw(void) {
    if (crystal_here() || (current >= 0 && secrets[current].rule == R_CRYSTAL && crystal_seen == 600 + current && layout >= 0)) crystal_draw();
    if (current >= 0 && secrets[current].rule == R_ORBS && crystal_seen == 600 + current && !ending) orbs_draw();
    if (current >= 0 && secrets[current].rule == R_WAGONS && crystal_seen == 600 + current && !ending) wagons_draw();
    /* The red structure: a red glow where one can be entered. */
    int n;
    for (n = 0; current < 0 && n < 12; ++n) {
        const SecretMission *s = &secrets[n];
        float at[3] = {s->at[0], s->at[1] + 120.0f, s->at[2]}, still[3] = {0.0f, 0.0f, 0.0f};
        if (s->host != room_seen || s->rule == R_NONE || (cleared >> n & 1)) continue;
        fx_mote(at, still, 90.0f + 15.0f * sinf(spin * 5.0f), 80.0f, 0.05f, 0xc02020ffu);
    }
}

/* A melee hit's frame: Secret Mission 10's crystals (relays and the cover break) and its reset switch. */
void sm_hit(float x, float y, float z, float yaw, float reach) {
    int k;
    if (!crystal_here()) return;
    {
        float dx = crystal_reset[0] - x, dz = crystal_reset[2] - z;
        if (dx * dx + dz * dz < (reach + 80.0f) * (reach + 80.0f) && fabsf(crystal_reset[1] - y) < 300.0f) {
            float at[3] = {crystal_reset[0], crystal_reset[1], crystal_reset[2]};
            fx_hit_spark(at);
            crystal_reset_all();
            return;
        }
    }
    for (k = 0; k < crystal_layouts[layout].count; ++k) {
        const CrystalRecord *r = &crystal_layouts[layout].recs[k];
        float at[3], dx, dz, d;
        if (!present[k] || (r->type != 2 && r->type != 3)) continue;
        crystal_place(at, k);
        dx = at[0] - x; dz = at[2] - z; d = sqrtf(dx * dx + dz * dz);
        if (d > reach + (r->type == 3 ? 150.0f : 60.0f) || fabsf(at[1] - y) > 300.0f) continue;
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        present[k] = 0;
        nfx_crystal_break(at, r->type);   /* the crystal's own effects (FUN_00420450) */
        solved = crystal_trace();
        return;   /* one a swing */
    }
}

static unsigned int gone_mask(void) {
    unsigned int m = 0;
    int k;
    if (layout >= 0) for (k = 0; k < crystal_layouts[layout].count && k < 24; ++k) if (!present[k]) m |= 1u << k;
    return m;
}

void sm_status(char *out, int size) {
    snprintf(out, size, "sm=%d end=%d clock=%.1f air=%.1f cleared=%03x layout=%d solved=%d beam=%d gone=%06x orbs=%d wagon=%d/%d x=%.0f end=%d,%d", current + 1, ending, clock_, air, cleared, layout, solved, beam_count, gone_mask(), orbs_left, wagon, wagon_result, wagon_x, beam_count ? beam[beam_count - 1][0] : -1, beam_count ? beam[beam_count - 1][1] : -1);
}
