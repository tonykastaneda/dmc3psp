/* Mission 6, "Family ties", from its event script (EventTbl06, GDATA 244; docs/GAME_FLOW.md has the decoded rooms).
 *
 * 122 Mute Goddess' Chamber: the statue (gimmick 300) takes the Essences by O in its area 0, one at a time
 *     (counter 5). The second blasts the rubble (STAY 100 and its walls) off the door to 127; the third reveals
 *     Artemis (item 30, which the port doesn't have as a gun yet).
 * 123 Chamber of 3 Trials: nothing scripted.
 * 126 Trial of Wisdom: through the doorways in the order 3, 1, 2 (counters 0 and 1); each one leads back into the
 *     room. A wrong one locks all four doors and starts group 0; once it's beaten only door 0 (to 123) opens. Right
 *     three times, the Essence of Intelligence (item 42) shows on the pedestal and doors 1-3 lead out to 123.
 * 125 Trial of Skill: area 0 sets the spike trap (gimmick 100, CStageSetNeedle) going, area 1 stops it. Its
 *     Essence (item 41) at the far end; once it's taken, group 0 on the way back.
 * 124 Trial of the Warrior: group 0 under a barrier (op 0x41) while crest switches 100 and 101 aren't both lit.
 *     Lit, a switch stays so for its time (op 0x35: 2400 frames on Easy and Normal, 1800 Hard, 1200 above) and
 *     then goes out. All beaten: the barrier's gone and the Essence of Fighting (item 40) is there.
 * 127 The Dark Corridor: its area 0 plays demo 15, which clears the mission.
 *
 * Stand-ins: the Essences lie as a glow (as Mission 4's Astronomical Board); the switches are drawn as Mission 3's
 * is; the barrier is a purple mist on each enemy and makes them take no damage (the walkthrough: "lighting the two
 * structures removes the barriers"; what the barrier does in the game, through each enemy's vfunc +0x114, isn't
 * decoded). The pillars don't rise, and the cameras, messages and the demo aren't in. */
#include <math.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>

#include "mission6.h"
#include "enemy.h"
#include "fx.h"
#include "stage.h"

#define STORY_TAG 128            /* tools/gimmicks.py: STAY set n >= 100 is stage tag 128 + n - 100 */
#define RUBBLE_TAG (STORY_TAG + 0)
#define ITEM_RANGE 160.0f        /* O this close to a key item (Mission 4's, main.c) */
#define WALL_TAG 250             /* 124's fight walls: stage box tags no stage batch uses */

enum { ESS_FIGHTING, ESS_TECHNIQUE, ESS_INTELLIGENCE };   /* items 40, 41, 42 */
static const char *const essence_names[3] = {"ESSENCE OF FIGHTING", "ESSENCE OF TECHNIQUE", "ESSENCE OF INTELLIGENCE"};
/* Where each lies (its room's ITM chunk). */
static const struct { short room; float x, y, z; } essence_at[3] = {
    {124, 3393.0f, 127.0f, 2501.0f}, {125, 418.0f, 281.0f, 2502.0f}, {126, 2499.0f, 187.0f, 2497.0f}};

static int difficulty, room_now;
static unsigned int got, placed;   /* essences held (bits), essences given to the statue (counter 5) */
static int wisdom_step, wisdom_door = -1, wisdom_from_self;   /* counters 0 and 1, the doorway just taken */
static int skill_taken;                                      /* 125's fight released */
static int warrior_done;
static float caption_time;
static const char *caption;
static float spin;

/* ---- 124's crest switches (CStageSetSwitch, docs/BAR_CRANK_SPEC.md section 3). Attached to pillars 110 (3400, 0,
 * 2500) and 111 (1400, 0, 2500, turned 180), at their joint 1 (-112, -160, 0) facing the room's middle. The joint's
 * height isn't read: 140 above the floor, as room 3's switch. */
#define SW_CHARGE 300.0f
#define SW_FLAMES 560.0f
#define SW_PER_FLAME 70.0f
#define SW_WINDOW 120.0f
#define SW_DRAIN 2.0f
#define SW_RADIUS 60.0f
typedef struct { float x, y, z, yaw; float charge, wing, flames, window, spin, spin2, lit_left; int lit; } Switch;
static Switch sw[2];
static const float sw_place[2][4] = {{3288.0f, 140.0f, 2500.0f, -90.0f}, {1512.0f, 140.0f, 2500.0f, 90.0f}};
static int barrier_on;

/* ---- 125's spike trap (CStageSetNeedle, set 100). Its code isn't decoded: a stand-in from the walkthrough. Rows
 * of spikes come at Dante down the corridor (toward +x, the way back) in batches: from the left wall (step right),
 * the right wall (step left), the floor (jump) or the ceiling (stay on the ground). Area 0 (x 9035-9235) starts
 * them, area 1 (x 1310-1610) stops them. Speed, spacing and the 700 damage (the Seven Hells' light fallback) are
 * estimates. */
#define NEEDLE_MAX 8
#define NEEDLE_MID 2500.0f       /* the corridor's middle (its door, start and Essence) */
#define NEEDLE_HALF 450.0f       /* its half width (area 0's) */
#define NEEDLE_SPEED 700.0f
#define NEEDLE_EVERY 1.6f
typedef struct { float x, floor; int kind, on, hit; } Needle;
static Needle needles[NEEDLE_MAX];
static int needles_on, needles_done, needle_seq, hazard;
static float needle_clock, hazard_x, hazard_z;
static const unsigned char needle_pattern[7] = {0, 1, 2, 0, 3, 1, 2};

static float lit_time(void) { return difficulty <= 1 ? 2400.0f : difficulty == 2 ? 1800.0f : 1200.0f; }

static void say(const char *text) { caption = text; caption_time = 3.0f; }

void m6_reset(int d) {
    difficulty = d;
    room_now = -1;
    got = placed = 0;
    wisdom_step = 0; wisdom_door = -1; wisdom_from_self = 0;
    skill_taken = 0;
    warrior_done = 0;
    caption = 0; caption_time = 0.0f;
    barrier_on = 0;
    memset(sw, 0, sizeof sw);
    needles_done = 0;
}

static void barrier(int on) {
    barrier_on = on;
    enemy_set_mortal(!on);
}

int m6_enter(int room) {
    int restart = 0, i;
    room_now = room;
    barrier(0);
    needles_on = 0; needle_clock = 0.0f; hazard = 0;
    memset(needles, 0, sizeof needles);
    if (room == 125 && !(got >> ESS_TECHNIQUE & 1)) needles_done = 0;   /* block 1 again: flag 32 is clear */
    if (room == 122 && placed >= 2) stage_hide_group(RUBBLE_TAG);
    if (room == 126) {
        /* Counter 1 from the doorway taken (only when coming from 126 itself), then counter 0 = counter 1. */
        int next = 0;
        if (wisdom_from_self && !(got >> ESS_INTELLIGENCE & 1)) {
            if (wisdom_step == 0) next = wisdom_door == 3 ? 1 : 4;
            else if (wisdom_step == 1) next = wisdom_door == 1 ? 2 : 4;
            else if (wisdom_step == 2) next = wisdom_door == 2 ? 3 : 4;
            else next = wisdom_step == 3 ? 3 : 4;
        }
        wisdom_step = next;
        restart = next == 4;
    }
    wisdom_from_self = 0;
    if (room == 124 && !warrior_done) {
        /* Block 1: the switches go dark (thread 2 clears flags 32-34), the barrier goes up, group 0 comes. */
        for (i = 0; i < 2; ++i) {
            memset(&sw[i], 0, sizeof sw[i]);
            sw[i].x = sw_place[i][0]; sw[i].y = sw_place[i][1]; sw[i].z = sw_place[i][2]; sw[i].yaw = sw_place[i][3];
        }
        barrier(1);
        /* COLLISION 113 on pillar 111 (box -60, 0, 0 half 100, 1100, 1000, the pillar turned 180): it shuts the
         * corridor from the door off while the fight is on (its joint isn't read: here it stands on the corridor's
         * side of the pillar, behind the switch). And the switches' hubs are solid. */
        stage_add_box(1300.0f, 0.0f, 2500.0f, 100.0f, 1100.0f, 1000.0f, 0.0f, WALL_TAG);
        for (i = 0; i < 2; ++i) stage_add_box(sw[i].x + (i ? -40.0f : 40.0f), sw[i].y, sw[i].z, 50.0f, 250.0f, 80.0f, 0.0f, WALL_TAG);
    }
    return restart;
}

int m6_door_route(int room, int door, int *next_room, int *start) {
    if (room != 126 || door < 1 || door > 3) return 0;
    if ((got >> ESS_INTELLIGENCE & 1) || wisdom_step == 3) {
        *next_room = 123; *start = 3;   /* door_retarget [n, 0, 123, 3] */
        return 1;
    }
    wisdom_door = door;
    wisdom_from_self = 1;
    return 0;
}

int m6_door_shut(int room, int door) {
    if (room == 122 && door == 2) return placed < 2;          /* the rubble (STAY 100's walls) */
    if (room == 126 && wisdom_step == 4) return door != 0;     /* door_unlock [0, 1] only */
    return 0;
}

int m6_script_held(int room) {
    if (room == 126) return wisdom_step != 4;
    if (room == 125) return !skill_taken;
    return 0;
}

static int near_item(int e, float x, float y, float z) {
    float dx = x - essence_at[e].x, dz = z - essence_at[e].z;
    return dx * dx + dz * dz < ITEM_RANGE * ITEM_RANGE && fabsf(y - essence_at[e].y) < 300.0f;
}

static int item_shown(int e) {
    if (got >> e & 1) return 0;
    if (e == ESS_FIGHTING) return warrior_done;
    if (e == ESS_INTELLIGENCE) return wisdom_step == 3;
    return 1;
}

static void needles_update(float dt, float x, float y, float z, int on_ground) {
    int i;
    if (!needles_on && !needles_done && x > 9035.0f && x < 9235.0f && fabsf(z - NEEDLE_MID) < NEEDLE_HALF) needles_on = 1;
    if (needles_on && x > 1310.0f && x < 1610.0f) { needles_on = 0; needles_done = 1; }
    if (needles_on && (needle_clock -= dt) <= 0.0f) {
        needle_clock = NEEDLE_EVERY;
        for (i = 0; i < NEEDLE_MAX; ++i) if (!needles[i].on) break;
        if (i < NEEDLE_MAX) {
            Needle *n = &needles[i];
            float f;
            n->on = 1; n->hit = 0;
            n->x = fmaxf(x - 1800.0f, 1610.0f);
            n->kind = needle_pattern[needle_seq++ % 7];
            n->floor = stage_floor(n->x, y + 200.0f, NEEDLE_MID, 400.0f, &f) ? f : y;
        }
    }
    for (i = 0; i < NEEDLE_MAX; ++i) {
        Needle *n = &needles[i];
        int caught;
        if (!n->on) continue;
        n->x += NEEDLE_SPEED * dt;
        if (n->x > x + 1500.0f || n->x > 9300.0f || (!needles_on && n->x > x + 200.0f)) { n->on = 0; continue; }
        if (n->hit || fabsf(n->x - x) > 40.0f) continue;
        caught = n->kind == 0 ? z < NEEDLE_MID : n->kind == 1 ? z >= NEEDLE_MID : n->kind == 2 ? on_ground : !on_ground;
        if (caught) { n->hit = 1; hazard = 1; hazard_x = n->x - 200.0f; hazard_z = z; }
    }
}

int m6_hazard(float *from_x, float *from_z) {
    if (!hazard) return 0;
    hazard = 0;
    *from_x = hazard_x; *from_z = hazard_z;
    return 1;
}

static void switches_update(float frames) {
    int i;
    for (i = 0; i < 2; ++i) {
        Switch *s = &sw[i];
        if (!s->lit) {
            if (s->window > 0.0f) s->window -= frames;
            else {
                float drain = SW_DRAIN * frames;
                s->charge = fmaxf(s->charge - drain, 0.0f);
                s->wing = fmaxf(s->wing - drain, 0.0f);
                if (s->charge <= 100.0f) s->flames = fmaxf(s->flames - drain, 0.0f);
            }
            if (s->charge >= SW_CHARGE && s->flames >= SW_FLAMES) { s->lit = 1; s->lit_left = lit_time(); }
        } else {
            /* FUN_003055c0: the lit timer; at its end the flag is cleared and the switch resets (FUN_00305fd0). */
            s->lit_left -= frames;
            if (s->lit_left <= 0.0f) { s->lit = 0; s->charge = s->wing = s->flames = s->window = 0.0f; }
        }
        s->spin += 60.0f * s->charge / SW_CHARGE * frames;
        s->spin2 += 20.0f * s->wing / 200.0f * frames;
    }
}

int m6_update(float dt, unsigned int pressed, float x, float y, float z, int on_ground, int fight_on, int *prompt) {
    int e;
    float frames = dt * 60.0f;
    spin += frames;
    if (caption_time > 0.0f) caption_time -= dt;
    for (e = 0; e < 3; ++e) {
        if (essence_at[e].room != room_now || !item_shown(e) || fight_on || !near_item(e, x, y, z)) continue;
        *prompt = 1;
        if (pressed & PSP_CTRL_CIRCLE) {
            got |= 1u << e;
            say(essence_names[e]);
            if (e == ESS_TECHNIQUE) skill_taken = 1;   /* once-per-mission block 0: group 0 on the way back */
        }
    }
    if (room_now == 122) {
        /* The statue's area 0 (2426-2676, 2405-2605, from y 300 up 300): O gives it the next Essence held. */
        if (x > 2426.0f && x < 2676.0f && z > 2405.0f && z < 2605.0f && y > 200.0f && y < 600.0f && (got & ~placed)) {
            *prompt = 1;
            if (pressed & PSP_CTRL_CIRCLE) {
                for (e = 0; e < 3; ++e) if ((got & ~placed) >> e & 1) { placed |= 1u << e; break; }
                {
                    int n = (placed & 1) + (placed >> 1 & 1) + (placed >> 2 & 1);
                    if (n == 2) {
                        /* Thread 1: effects 436-438 at gimmick 200, the rubble goes. */
                        float at[3] = {4100.0f, 700.0f, 2500.0f};
                        stage_hide_group(RUBBLE_TAG);
                        fx_explosion(at);
                        say("THE WAY IS OPEN");
                    } else if (n == 3) say("ARTEMIS");
                }
            }
        }
    }
    if (room_now == 125) needles_update(dt, x, y, z, on_ground);
    if (room_now == 124 && !warrior_done) {
        int both;
        switches_update(frames);
        both = sw[0].lit && sw[1].lit;
        if (both == barrier_on) barrier(!both);   /* block 2: flag 34 follows flags 32 and 33 */
        if (!fight_on) {
            /* Block 3: all beaten. */
            warrior_done = 1;
            barrier(0);
            stage_hide_group(WALL_TAG);
        }
    }
    if (room_now == 127 && x > 2695.0f && x < 3495.0f && z > 2184.0f && z < 2484.0f && y > -100.0f && y < 2900.0f)
        return 1;   /* area 0: demo 15, then mission_clear */
    return 0;
}

void m6_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    int i;
    if (room_now != 124 || warrior_done) return;
    for (i = 0; i < 2; ++i) {
        Switch *s = &sw[i];
        float dx = s->x - x, dz = s->z - z, dy = s->y - y, d = sqrtf(dx * dx + dz * dz), amount;
        if (s->lit) continue;
        if (d > reach + SW_RADIUS || dy < -SW_RADIUS || dy > SW_RADIUS + 200.0f) continue;
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        amount = heavy ? 84.0f : 42.0f;   /* main.c's switch_hit: Dante's real damage isn't read */
        if (s->charge < SW_CHARGE) {
            s->charge = fminf(s->charge + amount, SW_CHARGE + 1.0f);
            if (s->charge > 100.0f) s->wing = fminf(s->wing + amount, 201.0f);
        } else s->flames = fminf(s->flames + amount, SW_FLAMES + 1.0f);
        s->window = SW_WINDOW;
        {
            float at[3] = {s->x, s->y, s->z};
            fx_hit_spark(at);
        }
    }
}

typedef struct { unsigned int color; float x, y, z; } M6Vertex;
#define M6_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
static void box(float x, float y0, float y1, float z, float hx, float hz, float yaw, unsigned int top, unsigned int side) {
    float a = yaw * 0.0174533f, c = cosf(a), s = sinf(a), px[4], pz[4];
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    static const int q[6] = {0, 1, 2, 0, 2, 3};
    int i, n = 0;
    M6Vertex *o = (M6Vertex *)sceGuGetMemory(sizeof(M6Vertex) * 30);
    for (i = 0; i < 4; ++i) {
        float lx = cx[i] * hx, lz = cz[i] * hz;
        px[i] = x + lx * c + lz * s;
        pz[i] = z - lx * s + lz * c;
    }
    for (i = 0; i < 6; ++i) o[n++] = (M6Vertex){top, px[q[i]], y1, pz[q[i]]};
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        o[n++] = (M6Vertex){side, px[i], y1, pz[i]};
        o[n++] = (M6Vertex){side, px[j], y1, pz[j]};
        o[n++] = (M6Vertex){side, px[j], y0, pz[j]};
        o[n++] = (M6Vertex){side, px[i], y1, pz[i]};
        o[n++] = (M6Vertex){side, px[j], y0, pz[j]};
        o[n++] = (M6Vertex){side, px[i], y0, pz[i]};
    }
    sceGumDrawArray(GU_TRIANGLES, M6_VTYPE, n, 0, o);
}

/* main.c's seal_draw stand-in for a switch: a hub, 8 blades turning with the charge, 8 flames on a 108 cm circle. */
static void switch_draw(const Switch *s) {
    float a = s->yaw * 0.0174533f, ux = sinf(a), uz = cosf(a);
    float sx = uz, sz = -ux, px = s->x + ux * 8.0f, py = s->y, pz = s->z + uz * 8.0f;
    int i;
    box(s->x, py - 22.0f, py + 22.0f, s->z, 22.0f, 22.0f, s->yaw, 0xff505860u, 0xff30363cu);
    for (i = 0; i < 8; ++i) {
        float b = (s->spin + 45.0f * i) * 0.0174533f, r = 50.0f;
        box(px + sx * sinf(b) * r, py + cosf(b) * r - 8.0f, py + cosf(b) * r + 8.0f, pz + sz * sinf(b) * r, 8.0f, 8.0f,
            s->yaw, 0xff8090a0u, 0xff506070u);
    }
    for (i = 0; i < 8; ++i) {
        float b = 45.0f * i * 0.0174533f, r = 108.0f;
        int lit = s->lit || s->flames >= SW_PER_FLAME * (i + 1);
        box(px + sx * sinf(b) * r, py + cosf(b) * r - 9.0f, py + cosf(b) * r + 9.0f, pz + sz * sinf(b) * r, 9.0f, 9.0f,
            s->yaw, lit ? 0xff3050ffu : 0xff202028u, lit ? 0xff2030c0u : 0xff101018u);
    }
}

void m6_draw(void) {
    int e, i;
    for (e = 0; e < 3; ++e) {
        /* A key item (CItemKey) lies as a glow, not a model: a soft core and sparks rising off it. */
        float core[3], still[3] = {0.0f, 0.0f, 0.0f};
        if (essence_at[e].room != room_now || !item_shown(e)) continue;
        core[0] = essence_at[e].x; core[1] = essence_at[e].y + 70.0f; core[2] = essence_at[e].z;
        fx_mote(core, still, 70.0f + 12.0f * sinf(spin * 0.15f), 60.0f, 0.05f, 0xffa0f0ffu);
        if (((int)spin % 4) == 0) {
            unsigned int r = fx_random_u32();
            float at[3] = {core[0] + (float)((int)(r & 63) - 32), core[1] - 20.0f, core[2] + (float)((int)(r >> 8 & 63) - 32)};
            float up[3] = {0.0f, 50.0f + (float)(r >> 16 & 31), 0.0f};
            fx_mote(at, up, 24.0f, 4.0f, 0.9f, (r >> 24 & 1) ? 0xffffffffu : 0xff60d0ffu);
        }
    }
    for (i = 0; room_now == 125 && i < NEEDLE_MAX; ++i) {
        /* A row of spikes: a slab from one wall to the middle, low across the floor, or high across the ceiling. */
        const Needle *n = &needles[i];
        int k;
        if (!n->on) continue;
        for (k = 0; k < 9; ++k) {
            float zz = NEEDLE_MID - NEEDLE_HALF + (k + 0.5f) * (NEEDLE_HALF * 2.0f / 9.0f), y0 = n->floor, y1 = n->floor + 320.0f;
            if (n->kind == 0 && zz >= NEEDLE_MID) continue;
            if (n->kind == 1 && zz < NEEDLE_MID) continue;
            if (n->kind == 2) y1 = n->floor + 70.0f;
            if (n->kind == 3) { y0 = n->floor + 230.0f; y1 = n->floor + 420.0f; }
            box(n->x, y0, y1, zz, 7.0f, 10.0f, 0.0f, 0xffa0a8b0u, 0xff505860u);
        }
    }
    if (room_now == 124 && !warrior_done) {
        float feet[16][3];
        int n = enemy_alive_positions(feet, 16);
        for (i = 0; i < 2; ++i) switch_draw(&sw[i]);
        if (barrier_on && ((int)spin % 3) == 0) {
            /* The barrier: a purple mist rising off each one. */
            for (i = 0; i < n; ++i) {
                unsigned int r = fx_random_u32();
                float at[3] = {feet[i][0] + (float)((int)(r & 127) - 64), feet[i][1] + 40.0f + (float)(r >> 8 & 127),
                               feet[i][2] + (float)((int)(r >> 16 & 127) - 64)};
                float up[3] = {0.0f, 60.0f, 0.0f};
                fx_mote(at, up, 50.0f, 90.0f, 0.8f, 0x90c040a0u);
            }
        }
    }
}

#include <stdio.h>
void m6_status(char *out, int size) {
    snprintf(out, size, "room=%d got=%u placed=%u wisdom=%d/%d skill=%d warrior=%d barrier=%d sw0=%.0f/%.0f/%d/%.0f sw1=%.0f/%.0f/%d/%.0f",
             room_now, got, placed, wisdom_step, wisdom_door, skill_taken, warrior_done, barrier_on,
             sw[0].charge, sw[0].flames, sw[0].lit, sw[0].lit_left, sw[1].charge, sw[1].flames, sw[1].lit, sw[1].lit_left);
}

const char *m6_caption(float *alpha) {
    if (!caption || caption_time <= 0.0f) return 0;
    *alpha = caption_time > 0.5f ? 1.0f : caption_time * 2.0f;
    return caption;
}
