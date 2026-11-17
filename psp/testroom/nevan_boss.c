/* Nevan (class 31, EST 49; em028.pac at GDATA 133), Mission 9's boss, from her class's code (CEm031, 0x3b1b40-
 * 0x3c6000; docs/ENEMY_SPECS.md G), at rank 0 (Easy and Normal):
 * - Her brain picks a PATTERN by weight (0x5c1970: by rank, her phase, and whether Dante is beyond 600 cm), a list
 *   of MOVES (0x5c1930) ending in 23; each move is an ACTION (0x5bc250; moves 14 and 22 are 9). FUN_003bba20 /
 *   FUN_003bbc50 / FUN_003b9a00.
 * - Her phase is by health (FUN_003baf10): over 5000, over 2000, the rest; she has 7000 (0x5bc32c).
 * - Each action plays its own script (em028 [10]), whose motion and event frames are below.
 * - Idling (action 0) lasts 30 / 20 / 10 frames by phase (0x5bca20). Actions 1-3 close on Dante, 4-6 circle him: a
 *   speed that rises by a rate a frame to a top, moving its square in cm a frame, until a time runs out or she's at
 *   her distance (FUN_003b4500 / FUN_003b4e30, tables 0x5bcab0-0x5bcd80). 7-9 fade her out over 30 frames, put her
 *   400 (7) or 1000 (8) cm beyond Dante from the room's middle (FUN_003ba790) and fade her back.
 * - Action 22 lets out 2 / 3 / 4 bats (0x5bce38) 8 frames apart, 150 cm before her and 100 up (FUN_003b6c90); each
 *   aims once at Dante's chest and flies straight, its speed rising 0.2 a frame to 18 and moving its square, for
 *   360 frames; an 80 cm sphere, attack record 0x5bc3d0: 300 damage (FUN_00418a50 / FUN_00418bf0 / FUN_00418f50).
 * - Her hits (FUN_003bb150, FUN_003bbcf0): a light one makes her reel (10 or 11), a heavy one 12 (14 if reeling
 *   from 13-15), one that throws knocks her down (21); not while teleporting or in 33 / 34.
 * - Her other attacks make shells (hit records 0x5bc3d0, 0x40 apart, damage at +4: 300, 1500, 1000, 800, 500, 800,
 *   1500, 500; FUN_002949c0 picks one): 24 (FUN_003b7300) 1 / 1 / 2 strikes 30 frames apart (FUN_0045d720's class:
 *   his spot marked 40 frames, then live 20, radius 200, record 6); 29 / 38 (FUN_003b7bd0 / FUN_003b8d70) the floor
 *   lightning (FUN_0043df80's class: 50 frames charging, the ring of 800 about her 50, then the ring and the room's
 *   whole floor 30, record 1, only while she's in the action); 30 Shl03, 300 cm about her for the rest of the action
 *   (800); 32 Shl04, 70 cm on his spot after 50 frames, live 30 (500); 33-35 Shl05, the kiss, 200 cm about her lips
 *   for 5 frames (800).
 * 25 (FUN_003b7460) its lightning balls (FUN_00468580's class, orbs_start): pillars or bars sweeping at him.
 * Stand-ins: 29 / 38's event (FUN_003ba660), the kiss's hold, 23's circling bats (let out as the bats are), and the shapes of
 * Shl03-05 beyond those numbers. */
#include <math.h>
#include "nevan_boss.h"
#include "fx.h"

const char *const nevan_motion_names[NEVAN_M_COUNT] = {
    "nv200", "nv210", "nv211", "nv212", "nv213", "nv214", "nv216", "nv217", "nv224", "nv225", "nv230", "nv240",
    "nv241", "nv242", "nv243", "nv244", "nv245", "nv246", "nv247", "nv248", "nv249", "nv250", "nv251"};
enum { M0, M10, M11, M12, M13, M14, M16, M17, M24, M25, M30, M40, M41, M42, M43, M44, M45, M46, M47, M48, M49,
       M50, M51 };

/* Per action, from its script (em028 [10]): the motion, whether it loops, and the event's frame (-1 none). */
typedef struct { signed char motion, loops; short event; } NevanScript;
static const NevanScript scripts[39] = {
    {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1}, {M0, 1, -1},
    {M0, 1, -1}, {M0, 1, -1}, {M10, 0, -1}, {M11, 0, -1}, {M12, 0, -1}, {M13, 0, -1}, {M14, 0, -1}, {M16, 1, -1},
    {M16, 1, -1}, {M16, 0, -1}, {M17, 0, -1}, {M0, 0, -1}, {M0, 0, -1}, {M24, 0, -1}, {M40, 0, 82}, {M41, 0, 67},
    {M42, 0, 165}, {M46, 0, 126}, {M47, 0, -1}, {M43, 0, -1}, {M30, 0, -1}, {M25, 0, 90}, {M44, 1, 77},
    {M48, 0, -1}, {M45, 0, 24}, {M49, 0, 107}, {M49, 0, 107}, {M49, 0, 107}, {M50, 1, -1}, {M51, 0, -1},
    {M25, 0, 90}};
static const unsigned char move_action[23] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 22, 23, 24, 25, 9, 28, 29, 30, 32, 33,
                                              34, 35, 9};
#define END_MOVE 23
static const unsigned char patterns[16][16] = {
    {0, 17, 1, 18, 0, 3, 11, 15, 23}, {0, 3, 18, 2, 17, 3, 11, 2, 17, 15, 23}, {0, 3, 11, 12, 10, 2, 18, 17, 23},
    {0, 9, 16, 13, 13, 5, 17, 15, 23}, {0, 9, 16, 13, 13, 13, 2, 18, 15, 23},
    {0, 9, 16, 13, 12, 13, 12, 13, 2, 17, 3, 10, 23}, {0, 7, 21, 17, 15, 23}, {0, 11, 7, 21, 18, 12, 10, 15, 23},
    {4, 0, 23}, {11, 23}, {11, 12, 10, 23}, {0, 9, 16, 13, 13, 5, 17, 15, 23},
    {0, 9, 16, 13, 13, 13, 2, 18, 15, 23}, {0, 9, 16, 13, 12, 13, 12, 13, 2, 17, 3, 10, 23},
    {0, 7, 21, 17, 15, 23}, {11, 7, 21, 18, 12, 10, 23}};
/* Rank 0's weights, [phase][near, far], as {pattern, percent}. */
typedef struct { signed char pattern; unsigned char weight; } NevanPick;
static const NevanPick picks[3][2][3] = {
    {{{0, 80}, {1, 20}, {-1, 0}}, {{8, 50}, {9, 50}, {-1, 0}}},
    {{{0, 20}, {3, 60}, {4, 20}}, {{8, 50}, {10, 50}, {-1, 0}}},
    {{{0, 10}, {3, 20}, {6, 70}}, {{8, 50}, {14, 50}, {-1, 0}}}};
static const float idle_frames[3] = {30.0f, 20.0f, 10.0f};
/* Actions 1-6 (rank 0; the same in every phase but the time): time by phase, distance to keep, rate, top. */
static const float move_time[2][3] = {{200.0f, 150.0f, 120.0f}, {120.0f, 100.0f, 80.0f}};
static const float move_keep[7] = {0.0f, 300.0f, 300.0f, 900.0f, 0.0f, 300.0f, 800.0f};
static const float move_rate[7] = {0.0f, 0.1f, 0.5f, 0.5f, 0.2f, 0.1f, 0.1f};
static const float move_top[7] = {0.0f, 2.5f, 6.0f, 6.0f, 2.5f, 2.5f, 2.5f};
static const unsigned char bat_count[3] = {2, 3, 4};
#define BAT_DAMAGE 1000.0f   /* record 2 (FUN_002949c0(hit, 2) in FUN_00418f50) */
#define BAT_RADIUS 80.0f
#define STAGE_RADIUS 1500.0f   /* how far from where she came in she may go (the room's floor) */

int nevan_loops(int motion) { return motion == M0 || motion == M16 || motion == M44 || motion == M50; }

static void act(NevanBrain *b, int a) {
    /* 29 and 38 make their floor lightning as they start (FUN_003b7bd0 / FUN_003b8d70, state 0); it charges 50 frames
     * (FUN_0043e1c0) and lives only while she's in them (FUN_0043e250). */
    if (a == 29 || a == 38) { b->field = 0; b->field_wait = 50.0f; b->field_done = 0; }
    if (a == 25) b->orbs_pending = 1;   /* FUN_003b7460, state 0 */
    b->action = a;
    b->want = scripts[a].motion;
    b->restart = 1;
    b->timer = 0.0f;
    b->step = 0;
    b->done = 0;
    b->frame = 0.0f;
}

void nevan_spawn(NevanBrain *b, int health) {
    int i;
    b->health = b->max_health = health;
    b->land = b->remove = b->hidden = 0;
    b->speed = 0.0f;
    b->alpha = 1.0f;
    b->pattern = -1;
    for (i = 0; i < 6; ++i) b->shells[i].on = 0;
    for (i = 0; i < 4; ++i) b->strikes[i].on = 0;
    for (i = 0; i < 12; ++i) b->orbs[i].on = 0;
    b->field = -1;
    b->home[0] = b->pos[0]; b->home[1] = b->pos[1]; b->home[2] = b->pos[2];
    for (i = 0; i < NEVAN_BATS; ++i) b->bats[i].on = 0;
    act(b, 0);
}

static void turn_to(NevanBrain *b, float px, float pz) {
    b->yaw = atan2f(px - b->pos[0], pz - b->pos[2]);   /* FUN_00359c80: she faces him each frame */
}

static int phase_of(const NevanBrain *b) {
    float h = (float)b->health * 7000.0f / (float)(b->max_health > 0 ? b->max_health : 1);
    return h > 5000.0f ? 0 : h > 2000.0f ? 1 : 2;
}

/* FUN_003bba20: a pattern by weight; FUN_003bbc50: its next move, as an action. */
static void next_move(NevanBrain *b, float d, float (*unit)(void)) {
    int m;
    if (b->pattern < 0) {
        const NevanPick *row = picks[b->phase][d > 600.0f];
        float r = unit() * 100.0f, sum = 0.0f;
        int k;
        b->pattern = row[0].pattern;
        for (k = 0; k < 3 && row[k].pattern >= 0; ++k) {
            sum += (float)row[k].weight;
            if (r < sum) { b->pattern = row[k].pattern; break; }
        }
        b->pattern_at = 0;
    }
    m = patterns[b->pattern][b->pattern_at++];
    if (m == END_MOVE || b->pattern_at >= 16) { b->pattern = -1; m = 0; }
    b->move = m;
    act(b, move_action[m]);
    if ((b->action >= 4 && b->action <= 6) || b->action == 23) b->side = unit() < 0.5f;   /* +0x4e1f: a random bit (FUN_003b4d80) */
}

/* Action 23 (FUN_003ba470): 3 / 5 / 8 of the bats about her (0x5bc8e0, rank 0), 20 / 15 / 12 frames apart (0x5bc930). */
static const unsigned char swarm_count[3] = {3, 5, 8};
static const float swarm_gap[3] = {20.0f, 15.0f, 12.0f};

static void release_bats_n(NevanBrain *b, int count, float gap) {
    int i, n = 0;
    for (i = 0; i < NEVAN_BATS && n < count; ++i) {
        NevanBat *t = &b->bats[i];
        if (t->on) continue;
        t->on = 1;
        t->wait = gap * (float)n;
        t->life = 360.0f;
        t->speed = 0.0f;
        t->pos[0] = b->pos[0] + sinf(b->yaw) * 150.0f;
        t->pos[1] = b->pos[1] + 100.0f;
        t->pos[2] = b->pos[2] + cosf(b->yaw) * 150.0f;
        t->dir[0] = t->dir[1] = t->dir[2] = 0.0f;
        ++n;
    }
}

static void release_bats(NevanBrain *b) { release_bats_n(b, bat_count[b->phase], 8.0f); }

static void bats_update(NevanBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < NEVAN_BATS; ++i) {
        NevanBat *t = &b->bats[i];
        float dx, dy, dz, d, s;
        if (!t->on) continue;
        if (t->wait > 0.0f) {
            if ((t->wait -= f) > 0.0f) continue;
            /* Released: aimed once at his chest (Dante + 150). */
            dx = px - t->pos[0]; dy = py + 150.0f - t->pos[1]; dz = pz - t->pos[2];
            d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < 1.0f) d = 1.0f;
            t->dir[0] = dx / d; t->dir[1] = dy / d; t->dir[2] = dz / d;
        }
        t->speed += 0.2f * f;
        if (t->speed > 18.0f) t->speed = 18.0f;
        s = t->speed * t->speed * f;
        /* Swept: at full speed a bat covers 324 cm a frame, more than it and Dante measure together. */
        dx = px - t->pos[0]; dy = py + 100.0f - t->pos[1]; dz = pz - t->pos[2];
        d = dx * t->dir[0] + dy * t->dir[1] + dz * t->dir[2];
        if (d < 0.0f) d = 0.0f;
        if (d > s) d = s;
        dx -= t->dir[0] * d; dy -= t->dir[1] * d; dz -= t->dir[2] * d;
        t->pos[0] += t->dir[0] * s; t->pos[1] += t->dir[1] * s; t->pos[2] += t->dir[2] * s;
        if (dx * dx + dy * dy + dz * dz < (BAT_RADIUS + 60.0f) * (BAT_RADIUS + 60.0f)) {
            t->on = 0;
            if (attacks) { b->land = 2; b->land_damage = BAT_DAMAGE; }
            continue;
        }
        if ((t->life -= f) <= 0.0f) t->on = 0;
    }
}

static void keep_on_stage(NevanBrain *b) {
    float hx = b->pos[0] - b->home[0], hz = b->pos[2] - b->home[2], l = sqrtf(hx * hx + hz * hz);
    if (l > STAGE_RADIUS) { b->pos[0] = b->home[0] + hx / l * STAGE_RADIUS; b->pos[2] = b->home[2] + hz / l * STAGE_RADIUS; }
}

/* Her shells (0x5bc3d0's records by FUN_002949c0's index). A sphere that waits `fuse`, then is live `active` frames. */
static void shell(NevanBrain *b, const float at[3], float fuse, float active, float radius, float damage, int follow,
                  float speed, float top, float px, float py, float pz) {
    int i;
    for (i = 0; i < 6; ++i) {
        float d[3], l;
        if (b->shells[i].on) continue;
        b->shells[i].on = 1; b->shells[i].done = 0; b->shells[i].follow = follow;
        b->shells[i].pos[0] = at[0]; b->shells[i].pos[1] = at[1]; b->shells[i].pos[2] = at[2];
        b->shells[i].fuse = fuse; b->shells[i].active = active; b->shells[i].radius = radius; b->shells[i].damage = damage;
        b->shells[i].top = top;
        d[0] = px - at[0]; d[1] = py + 100.0f - at[1]; d[2] = pz - at[2];
        l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l < 1.0f) l = 1.0f;
        b->shells[i].vel[0] = d[0] / l * speed; b->shells[i].vel[1] = d[1] / l * speed; b->shells[i].vel[2] = d[2] / l * speed;
        return;
    }
}

/* Action 24's strikes (FUN_003b7300): 1 / 1 / 2 by phase (0x5bc920, rank 0), each 30 frames after the last. */
static const unsigned char strike_count[3] = {1, 1, 2};
#define STRIKE_RADIUS 200.0f     /* FUN_0045dd70: shape 2, (0, 100, 0) up from the mark, radius 200 */
#define STRIKE_DAMAGE 1500.0f    /* record 6 */
#define FIELD_RADIUS 100.0f      /* FUN_0043e5d0: shape 6 (a pillar) about her chest, (0, -200, 0), 800 high, radius 100 */
#define FIELD_DAMAGE 1500.0f     /* record 1 */
static const float field_floor_time[3] = {30.0f, 30.0f, 30.0f};   /* 0x5c9370, rank 0 by phase */

static void strikes_start(NevanBrain *b) {
    int i, n = 0;
    for (i = 0; i < 4 && n < strike_count[b->phase]; ++i) {
        if (b->strikes[i].on) continue;
        b->strikes[i].on = 1; b->strikes[i].state = 0; b->strikes[i].done = 0;
        b->strikes[i].wait = 30.0f * (float)n;
        ++n;
    }
}

/* FUN_0045d950 / FUN_0045da20: after its delay it marks his spot at 100 up (a beam from her right hand to it), 40
 * frames later strikes there, live 20 frames. */
static void strikes_update(NevanBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < 4; ++i) {
        float dx, dy, dz, top;
        if (!b->strikes[i].on) continue;
        b->strikes[i].wait -= f;
        if (b->strikes[i].state == 0) {
            if (b->strikes[i].wait > 0.0f) continue;
            b->strikes[i].pos[0] = px; b->strikes[i].pos[1] = 100.0f; b->strikes[i].pos[2] = pz;
            b->strikes[i].state = 1; b->strikes[i].wait = 40.0f;
        } else if (b->strikes[i].state == 1) {
            if (((int)b->strikes[i].wait & 3) == 0) {
                float at[3] = {b->strikes[i].pos[0], b->strikes[i].pos[1] - 90.0f, b->strikes[i].pos[2]}, v[3] = {0.0f, 20.0f, 0.0f};
                fx_mote(at, v, STRIKE_RADIUS, 12.0f, 0.3f, 0xffff80c0u);   /* the mark (effect 0x1c7) */
            }
            if (b->strikes[i].wait < 0.0f) {
                float at[3] = {b->strikes[i].pos[0], b->strikes[i].pos[1], b->strikes[i].pos[2]};
                fx_explosion(at);   /* the bolt (effect 0x1ce) */
                b->strikes[i].state = 2; b->strikes[i].wait = 20.0f;
            }
        } else {
            /* Dante's middle against the capsule from the mark up 100. */
            top = b->strikes[i].pos[1] + 100.0f;
            dx = px - b->strikes[i].pos[0]; dz = pz - b->strikes[i].pos[2];
            dy = py + 100.0f;
            dy = dy < b->strikes[i].pos[1] ? b->strikes[i].pos[1] - dy : dy > top ? dy - top : 0.0f;
            if (!b->strikes[i].done && attacks && dx * dx + dy * dy + dz * dz < (STRIKE_RADIUS + 60.0f) * (STRIKE_RADIUS + 60.0f)) {
                b->strikes[i].done = 1;
                b->land = 2; b->land_damage = STRIKE_DAMAGE;
            }
            if (b->strikes[i].wait < 0.0f) b->strikes[i].on = 0;
        }
    }
}

/* Action 25's balls (FUN_003b7460 makes them, FUN_00468580's class runs them). Its layouts, one at random:
 * 0x5bc910 sets of a bar (two halves 800 apart, mode 1) at 50 / 300 / 100 / 200 up by turns from a random one;
 * 0x5bc900 sets of pillars (mode 0), three (0, +-250) and two (+-125) by turns; or 0x5bc8f0's pillars at once (0,
 * +-250, +-500). Sets are 0x5bc960's frames apart. Rank 0 by phase. */
static const unsigned char bar_sets[3] = {1, 2, 3}, pillar_sets[3] = {1, 2, 3}, pillar_row[3] = {3, 3, 5};
static const float orb_gap[3] = {50.0f, 45.0f, 40.0f};
static const float row_side[5] = {0.0f, 250.0f, -250.0f, 500.0f, -500.0f};     /* 0x5cd210 */
static const float three_side[3] = {0.0f, 250.0f, -250.0f};                    /* 0x5cd240 */
static const float two_side[2] = {125.0f, -125.0f};                            /* 0x5cd260 */
static const float bar_height[4] = {50.0f, 300.0f, 100.0f, 200.0f};            /* 0x5cd290 */
#define ORB_DAMAGE 500.0f        /* record 7 */
#define ORB_RADIUS 30.0f         /* FUN_00469790: the pillar (shape 6) 600 high, the bar (shape 4) 800 long */

static int orb_make(NevanBrain *b, int mode, float side, float height, float wait, float px, float pz) {
    int i;
    float yaw = atan2f(px - b->pos[0], pz - b->pos[2]), s = sinf(yaw), c = cosf(yaw);
    for (i = 0; i < 12; ++i) {
        if (b->orbs[i].on) continue;
        b->orbs[i].on = 1; b->orbs[i].mode = mode; b->orbs[i].state = 0; b->orbs[i].partner = -1; b->orbs[i].done = 0;
        b->orbs[i].pos[0] = b->pos[0]; b->orbs[i].pos[1] = b->pos[1]; b->orbs[i].pos[2] = b->pos[2];
        /* FUN_00468860: by her, 100 ahead on his bearing; then 2600 ahead, at his height (+0x2924) plus its own. */
        b->orbs[i].at[0] = b->pos[0] + c * side + s * 100.0f;
        b->orbs[i].at[1] = b->pos[1] + height;
        b->orbs[i].at[2] = b->pos[2] - s * side + c * 100.0f;
        b->orbs[i].to[0] = b->pos[0] + c * side + s * 2600.0f;
        b->orbs[i].to[1] = height;   /* + Dante's floor, filled in by the caller */
        b->orbs[i].to[2] = b->pos[2] - s * side + c * 2600.0f;
        b->orbs[i].speed = 0.0f; b->orbs[i].wait = wait; b->orbs[i].life = 120.0f;
        return i;
    }
    return -1;
}

static void orbs_start(NevanBrain *b, float px, float py, float pz, float (*unit)(void)) {
    int k, n, i, j;
    float wait = 0.0f;
    if (unit() < 0.5f) {
        int h = (int)(unit() * 4.0f) & 3;
        for (n = 0; n < bar_sets[b->phase]; ++n, wait += orb_gap[b->phase], h = (h + 1) & 3) {
            i = orb_make(b, 1, -400.0f, bar_height[h], wait, px, pz);
            j = orb_make(b, 1, 400.0f, bar_height[h], wait, px, pz);
            if (i >= 0) { b->orbs[i].to[1] += py; b->orbs[i].partner = j; }
            if (j >= 0) b->orbs[j].to[1] += py;
        }
    } else if (unit() < 0.5f) {
        for (n = 0; n < pillar_sets[b->phase]; ++n, wait += orb_gap[b->phase])
            for (k = 0; k < (n & 1 ? 2 : 3); ++k)
                if ((i = orb_make(b, 0, n & 1 ? two_side[k] : three_side[k], 400.0f, wait, px, pz)) >= 0) b->orbs[i].to[1] += py;
    } else {
        for (k = 0; k < pillar_row[b->phase]; ++k)
            if ((i = orb_make(b, 0, row_side[k], 400.0f, 0.0f, px, pz)) >= 0) b->orbs[i].to[1] += py;
    }
}

/* FUN_00469340: toward its point, speed rising (0x5cd2c0: 2, then 1, a frame) to a top (0x5cd2a0: 10, then 20),
 * moving the speed's square past 1, no more than the top a frame. 1 when there. */
static int orb_toward(float pos[3], const float to[3], float *speed, float rate, float top, float f) {
    float d[3] = {to[0] - pos[0], to[1] - pos[1], to[2] - pos[2]}, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), step;
    if (l < 10.0f) return 1;
    *speed += rate * f;
    if (*speed > top) *speed = top;
    step = *speed > 1.0f ? *speed * *speed : *speed;
    if (step > top) step = top;
    step *= f;
    if (step > l) step = l;
    pos[0] += d[0] / l * step; pos[1] += d[1] / l * step; pos[2] += d[2] / l * step;
    return 0;
}

/* FUN_00469050 (state 0, by her, live, until her script's event 4 or she leaves 25) and FUN_004691d0 (state 1:
 * its wait, then 300 frames to its point), hurting all along: a pillar from it down 600 (mode 0), or the bar
 * between a pair (mode 1). */
static void orbs_update(NevanBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < 12; ++i) {
        float dx, dz, mid = py + 100.0f;
        int hit = 0;
        if (!b->orbs[i].on) continue;
        if (b->orbs[i].state == 0) {
            orb_toward(b->orbs[i].pos, b->orbs[i].at, &b->orbs[i].speed, 2.0f, 10.0f, f);
            if (b->action != 25) b->orbs[i].state = 1;
        } else if ((b->orbs[i].wait -= f) < 0.0f) {
            if (b->orbs[i].state == 1) { b->orbs[i].state = 2; b->orbs[i].life = 300.0f; b->orbs[i].speed = 0.0f; }
            if (orb_toward(b->orbs[i].pos, b->orbs[i].to, &b->orbs[i].speed, 1.0f, 20.0f, f) || (b->orbs[i].life -= f) < 0.0f) {
                b->orbs[i].on = 0;
                continue;
            }
        }
        if (b->orbs[i].mode == 0) {
            dx = px - b->orbs[i].pos[0]; dz = pz - b->orbs[i].pos[2];
            hit = dx * dx + dz * dz < (ORB_RADIUS + 60.0f) * (ORB_RADIUS + 60.0f) && py < b->orbs[i].pos[1] &&
                  py + 180.0f > b->orbs[i].pos[1] - 600.0f;
            {
                float at[3] = {b->orbs[i].pos[0], b->orbs[i].pos[1] - (float)(i & 3) * 100.0f, b->orbs[i].pos[2]}, v[3] = {0.0f, -200.0f, 0.0f};
                fx_mote(at, v, 40.0f, 10.0f, 0.12f, 0xffff80c0u);
            }
        } else if (b->orbs[i].partner >= 0 && b->orbs[b->orbs[i].partner].on) {
            const float *a = b->orbs[i].pos, *e = b->orbs[b->orbs[i].partner].pos;
            float ab[3] = {e[0] - a[0], e[1] - a[1], e[2] - a[2]}, len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
            float t = len2 > 0.0f ? ((px - a[0]) * ab[0] + (mid - a[1]) * ab[1] + (pz - a[2]) * ab[2]) / len2 : 0.0f, q[3], r;
            int k;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            q[0] = a[0] + ab[0] * t - px; q[1] = a[1] + ab[1] * t - mid; q[2] = a[2] + ab[2] * t - pz;
            r = ORB_RADIUS + 60.0f;
            /* Dante as a column (his feet to 180 up) against the bar. */
            if (q[1] > -80.0f && q[1] < 80.0f) q[1] = 0.0f;
            hit = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] < r * r;
            for (k = 0; k < 4; ++k) {
                float u = (float)(k + 1) / 5.0f, at[3] = {a[0] + ab[0] * u, a[1] + ab[1] * u, a[2] + ab[2] * u}, v[3] = {0.0f, 0.0f, 0.0f};
                fx_mote(at, v, 50.0f, 20.0f, 0.08f, 0xffff80c0u);
            }
        }
        if (hit && attacks && !b->orbs[i].done) { b->orbs[i].done = 1; b->land = 2; b->land_damage = ORB_DAMAGE; }
    }
}

/* FUN_0043e250: stage 1 (50 frames) the column through her (shape 6 about her chest: (0, -200, 0), 800 high,
 * radius 100), stage 2 that and the room's whole floor (a box at (2300, 50, 2300), 1700 x 50 x 2200 each way, world
 * space) for 30; each stage hurts once. Dante is clear of the floor in the air. */
static void field_update(NevanBrain *b, float f, float px, float py, float pz, int attacks, float (*unit)(void)) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], chest = b->pos[1] + 124.0f;
    int hit = 0, k;
    if (b->field < 0 || b->field >= 3) return;
    if (b->action != 29 && b->action != 38) { b->field = -1; return; }
    b->field_wait -= f;
    if (b->field == 0) {
        if (b->field_wait < 0.0f) { b->field = 1; b->field_wait = 50.0f; b->field_done = 0; }
        return;
    }
    if (dx * dx + dz * dz < (FIELD_RADIUS + 60.0f) * (FIELD_RADIUS + 60.0f) && py < chest + 200.0f && py + 180.0f > chest - 600.0f) hit = 1;
    if (b->field == 2 && px > 600.0f && px < 4000.0f && pz > 100.0f && pz < 4500.0f && py < 100.0f) hit = 1;
    if (hit && attacks && !b->field_done) { b->field_done = 1; b->land = 2; b->land_damage = FIELD_DAMAGE; }
    /* The sparks: about her, and, in stage 2, over the floor around him. */
    for (k = 0; k < 3; ++k) {
        float a = unit() * 6.2831853f, r = unit() * FIELD_RADIUS, v[3] = {0.0f, 40.0f, 0.0f};
        float at[3] = {b->pos[0] + sinf(a) * r, b->pos[1] + unit() * 200.0f, b->pos[2] + cosf(a) * r};
        fx_mote(at, v, 40.0f, 8.0f, 0.15f, 0xffff80c0u);
        if (b->field == 2) {
            float near[3] = {px + (unit() - 0.5f) * 1200.0f, 5.0f, pz + (unit() - 0.5f) * 1200.0f};
            fx_mote(near, v, 40.0f, 8.0f, 0.15f, 0xffff80c0u);
        }
    }
    if (b->field_wait < 0.0f) {
        if (b->field == 1) { b->field = 2; b->field_wait = field_floor_time[b->phase]; b->field_done = 0; }
        else b->field = 3;
    }
}

/* An attack's event (its script's frame). */
static void attack_event(NevanBrain *b, float px, float py, float pz, int attacks) {
    float mine[3] = {b->pos[0], b->pos[1], b->pos[2]}, his[3] = {px, py, pz};
    (void)attacks;
    switch (b->action) {
    case 22: release_bats(b); break;
    /* FUN_003ba470 -> FUN_003c9080: the bats about her sent at him, each to a random one of 0x5bc7d0's offsets; her
     * swarm (+0x4db0) isn't ported, so they're let out as action 22's bats are. */
    case 23: release_bats_n(b, swarm_count[b->phase], swarm_gap[b->phase]); break;
    case 24: strikes_start(b); break;   /* FUN_003b7300 → FUN_0045d720 */
    case 25: {   /* script event 4 (0x61acf8 = 3): her balls go (FUN_00469050 → state 1) */
        int i;
        for (i = 0; i < 12; ++i) if (b->orbs[i].on && b->orbs[i].state == 0) b->orbs[i].state = 1;
        break;
    }
    /* 29 and 38: their floor lightning started with the action (field_update); the event (5) calls FUN_003ba660,
     * not decoded. */
    case 30: shell(b, mine, 0.0f, 57.0f, 300.0f, 800.0f, 1, 0.0f, 0.0f, px, py, pz); break;      /* Shl03, record 3: to 134 */
    case 32: shell(b, his, 50.0f, 30.0f, 70.0f, 500.0f, 0, 0.0f, 0.0f, px, py, pz); break;       /* Shl04, record 4 */
    case 33: case 34: case 35: {                                                                 /* Shl05, record 5: the kiss */
        float lips[3] = {b->pos[0], b->pos[1] + 100.0f, b->pos[2]};
        shell(b, lips, 0.0f, 5.0f, 200.0f, 800.0f, 1, 0.0f, 0.0f, px, py, pz);
        break;
    }
    default: break;
    }
}

static void shells_update(NevanBrain *b, float f, float px, float py, float pz, int attacks) {
    int i;
    for (i = 0; i < 6; ++i) {
        float dx, dy, dz, r;
        if (!b->shells[i].on) continue;
        if (b->shells[i].follow) { b->shells[i].pos[0] = b->pos[0]; b->shells[i].pos[2] = b->pos[2]; }
        if (b->shells[i].top > 0.0f) {
            /* Shl06's flight: 20 a frame, rising to 40 (+0x428) [the rate is an estimate]. */
            float s = sqrtf(b->shells[i].vel[0] * b->shells[i].vel[0] + b->shells[i].vel[1] * b->shells[i].vel[1] + b->shells[i].vel[2] * b->shells[i].vel[2]);
            if (s > 0.0f && s < b->shells[i].top) { float k = (s + 0.5f * f) / s; b->shells[i].vel[0] *= k; b->shells[i].vel[1] *= k; b->shells[i].vel[2] *= k; }
        }
        b->shells[i].pos[0] += b->shells[i].vel[0] * f; b->shells[i].pos[1] += b->shells[i].vel[1] * f; b->shells[i].pos[2] += b->shells[i].vel[2] * f;
        if (b->shells[i].fuse > 0.0f) {
            b->shells[i].fuse -= f;
            if (((int)(b->shells[i].fuse) & 3) == 0) {
                float at[3] = {b->shells[i].pos[0], b->shells[i].pos[1] + 10.0f, b->shells[i].pos[2]}, v[3] = {0.0f, 30.0f, 0.0f};
                fx_mote(at, v, b->shells[i].radius, 12.0f, 0.3f, 0xffffd060u);   /* the warning */
            }
            if (b->shells[i].fuse <= 0.0f && !b->shells[i].follow) {
                float at[3] = {b->shells[i].pos[0], b->shells[i].pos[1] + 20.0f, b->shells[i].pos[2]};
                fx_explosion(at);
            }
            continue;
        }
        r = b->shells[i].radius + 60.0f;
        dx = px - b->shells[i].pos[0]; dy = py + 100.0f - b->shells[i].pos[1]; dz = pz - b->shells[i].pos[2];
        if (b->shells[i].follow) dy = 0.0f;
        if (!b->shells[i].done && attacks && dx * dx + dy * dy + dz * dz < r * r) {
            b->shells[i].done = 1;
            b->land = 2; b->land_damage = b->shells[i].damage;
        }
        {
            float still[3] = {0.0f, 0.0f, 0.0f};
            fx_mote(b->shells[i].pos, still, b->shells[i].radius * 0.5f, 10.0f, 0.06f, 0xffffe080u);
        }
        if ((b->shells[i].active -= f) <= 0.0f) b->shells[i].on = 0;
    }
}

void nevan_think(NevanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks) {
    float dx = px - b->pos[0], dz = pz - b->pos[2], d = sqrtf(dx * dx + dz * dz);
    const NevanScript *s = &scripts[b->action];
    int a = b->action;
    b->land = 0;
    b->timer += f;
    b->phase = phase_of(b);
    bats_update(b, f, px, py, pz, attacks);
    shells_update(b, f, px, py, pz, attacks);
    strikes_update(b, f, px, py, pz, attacks);
    field_update(b, f, px, py, pz, attacks, unit);
    if (b->orbs_pending) { b->orbs_pending = 0; orbs_start(b, px, py, pz, unit); }
    orbs_update(b, f, px, py, pz, attacks);
    if (a == 21 && b->health <= 0) {
        if (b->timer > 200.0f) { b->remove = 1; }
        return;
    }
    if (a <= 6 && a != 0) {
        /* 1-3 close on him to their distance (FUN_003b4330 & co. → FUN_003b4500); 4 circles him, heading 90 degrees
         * off his bearing (FUN_003b4c20 → FUN_003b4e30); 5 closes at 75 (FUN_003b5020 → FUN_003b51f0); 6 backs out to
         * 800 at 105 (FUN_003b5450 → FUN_003b5620); the side is +0x4e1f's. The speed climbs by the rate to the top
         * (FUN_0035a4a0) and she moves its square a frame, stopping at the distance; once the time's up (or she's at
         * it) she slows 0.98 a frame for 40 (state 2), and then moves on. 4 and 5 also slow within 200 of him, and
         * when slowing they move the speed itself, not its square. */
        static const float heading[7] = {0.0f, 0.0f, 0.0f, 0.0f, 1.5707963f, 1.3089969f, -1.8325957f};
        float keep = move_keep[a], room, step, dir;
        int slowing = b->step == 2 || ((a == 4 || a == 5) && d < 200.0f), reached = 0;
        turn_to(b, px, pz);
        if (b->step == 0) b->step = 1;
        if (!slowing) {
            b->speed += move_rate[a] * f;
            if (b->speed > move_top[a]) b->speed = move_top[a];
            step = b->speed * b->speed;
        } else {
            b->speed *= powf(0.98f, f);
            step = a <= 3 ? b->speed * b->speed : b->speed;
        }
        room = a == 6 ? keep - d : a == 4 ? 1e9f : d - keep;
        if (room <= 0.0f) { reached = 1; step = 0.0f; }
        else if (room < step) { reached = 1; step = room; }
        dir = b->yaw + (b->side ? -heading[a] : heading[a]);
        b->pos[0] += sinf(dir) * step * f; b->pos[2] += cosf(dir) * step * f;
        keep_on_stage(b);
        if (reached && a != 4) { next_move(b, d, unit); return; }
        if (b->step == 1 && (b->timer > move_time[a >= 4][b->phase] || (a != 4 && room <= 0.0f))) { b->step = 2; b->brake = 40.0f; }
        else if (b->step == 2 && (b->brake -= f) < 0.0f) next_move(b, d, unit);
        return;
    }
    switch (a) {
    case 0:
        turn_to(b, px, pz);
        b->speed *= 0.98f;
        if (b->timer > idle_frames[b->phase]) next_move(b, d, unit);
        break;
    case 7: case 8: case 9:
        /* FUN_003b5870, by its steps (+6): 0-1 30 frames on her materials (+0x9c / +0xa0 0 to 128: not decoded, so
         * not shown), 2 her alpha (+0x5161) down over 30, 3 placed (FUN_003ba790) and turned to him, 4-5 15 frames
         * away, 6 her alpha back over 30, 7 the materials back over 30, then the next move. */
        b->wait -= f;
        switch (b->step) {
        case 0: b->step = 1; b->wait = 30.0f; break;
        case 1: if (b->wait < 0.0f) { b->step = 2; b->wait = 30.0f; } break;
        case 2:
            b->alpha = b->wait > 0.0f ? b->wait / 30.0f : 0.0f;
            if (b->wait < 0.0f) b->step = 3;
            break;
        case 3:
            if (a == 9) {
                /* Moves 9, 14 and 22: to an end of the room, on its stage, away from him. */
                if (pz > 2500.0f) { b->pos[0] = 2500.0f; b->pos[1] = 300.0f; b->pos[2] = 1200.0f; }
                else { b->pos[0] = 2500.0f; b->pos[1] = 178.0f; b->pos[2] = 3800.0f; }
            } else {
                /* 400 (7) or 1000 (8) cm beyond him, on the line from the room's middle (2500, 2500); the floor
                 * under 400 up. [The game stops her short of a wall by a ray from where she was; here she keeps to
                 * the stage.] */
                float cx = px - 2500.0f, cz = pz - 2500.0f, l = sqrtf(cx * cx + cz * cz), r = a == 8 ? 1000.0f : 400.0f;
                if (l < 1.0f) { cx = 0.0f; cz = 1.0f; l = 1.0f; }
                b->pos[0] = px + cx / l * r; b->pos[2] = pz + cz / l * r;
                b->pos[1] = 400.0f;
                keep_on_stage(b);
            }
            b->placed = 1;
            turn_to(b, px, pz);
            b->hidden = 1; b->step = 4;
            break;
        case 4: b->step = 5; b->wait = 15.0f; break;
        case 5: if (b->wait < 0.0f) { b->step = 6; b->wait = 30.0f; b->hidden = 0; } break;
        case 6:
            b->alpha = b->wait > 0.0f ? 1.0f - b->wait / 30.0f : 1.0f;
            if (b->wait < 0.0f) { b->step = 7; b->wait = 30.0f; b->alpha = 1.0f; }
            break;
        default:
            if (b->wait < 0.0f) {
                /* Moves 14 and 22 go on to 27 and 38 (+0x4e16); the rest take the pattern's next move. */
                if (b->move == 14) { b->pattern = -1; act(b, 27); }
                else if (b->move == 22) { b->pattern = -1; act(b, 38); }
                else next_move(b, d, unit);
            }
            break;
        }
        break;
    case 10: case 11: case 12: case 13: case 14: case 21:
        if (b->done) { b->pattern = -1; next_move(b, d, unit); }
        break;
    case 23: {
        /* FUN_003b7180: she circles him as in action 4 (FUN_003b4e30) through the motion, sending the bats at its
         * event; when the motion's done she slows for 40 frames and moves on. */
        int slowing = b->step == 2 || d < 200.0f;
        float step, dir;
        turn_to(b, px, pz);
        if (!slowing) {
            b->speed += move_rate[4] * f;
            if (b->speed > move_top[4]) b->speed = move_top[4];
            step = b->speed * b->speed;
        } else { b->speed *= powf(0.98f, f); step = b->speed; }
        dir = b->yaw + (b->side ? -1.5707963f : 1.5707963f);
        b->pos[0] += sinf(dir) * step * f; b->pos[2] += cosf(dir) * step * f;
        keep_on_stage(b);
        if (b->step < 2) {
            if (s->event >= 0 && b->step == 0 && b->frame >= (float)s->event) { b->step = 1; attack_event(b, px, py, pz, attacks); }
            if (b->done) { b->step = 2; b->brake = 40.0f; }
        } else if ((b->brake -= f) < 0.0f) next_move(b, d, unit);
        break;
    }
    default:
        if (b->frame < 20.0f) turn_to(b, px, pz);
        if (s->event >= 0 && !b->step && b->frame >= (float)s->event) { b->step = 1; attack_event(b, px, py, pz, attacks); }
        if ((s->loops && b->timer > 134.0f) || (!s->loops && b->done)) next_move(b, d, unit);
        break;
    }
}

int nevan_hit(NevanBrain *b, int kind, float fx, float fz, int mortal) {
    int a = b->action, next = -1;
    (void)fx; (void)fz;
    if ((a == 21 && b->health <= 0) || b->hidden) return 0;
    if (mortal) {
        b->health -= kind ? 2 : 1;
        if (b->health <= 0) {
            int i;
            for (i = 0; i < NEVAN_BATS; ++i) b->bats[i].on = 0;
            for (i = 0; i < 4; ++i) b->strikes[i].on = 0;
            for (i = 0; i < 12; ++i) b->orbs[i].on = 0;
            b->health = 0;
            act(b, 21);
            return 1;
        }
    }
    /* FUN_003bbcf0: none while teleporting or in 33 / 34; 12 takes no new reel. */
    if ((a >= 7 && a <= 9) || a == 33 || a == 34) return 0;
    if (kind == 2) next = 21;
    else if (a == 12) next = -1;
    else if (a == 10) next = 11;
    else if (a == 11) next = 10;
    else if (a >= 13 && a <= 15) next = kind ? 14 : 13;
    else { static int flip; next = kind ? 12 : ((flip ^= 1) ? 10 : 11); }   /* 10 or 11 at random in the game */
    if (next >= 0) { b->pattern = -1; act(b, next); }
    return 0;
}

void nevan_draw_fx(const NevanBrain *b) {
    int i;
    for (i = 0; i < NEVAN_BATS; ++i) {
        const NevanBat *t = &b->bats[i];
        float still[3] = {0.0f, 0.0f, 0.0f};
        if (!t->on || t->wait > 0.0f) continue;
        /* A bat: a dark core with a crackle of electricity round it (the bats' own models aren't converted). */
        fx_mote(t->pos, still, 40.0f, 30.0f, 0.04f, 0xff401030u);
        fx_mote(t->pos, still, 70.0f, 20.0f, 0.04f, 0x80ffa0e0u);
    }
}
