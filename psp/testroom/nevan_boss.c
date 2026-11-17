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
 * Stand-ins: the other attacks' events (23-25, 29, 30, 32, 35, 38) fire on their scripts' frames, but what they make
 * (shells 0x45d720, 0x468580, 0x43df80, 0x417340, 0x4179a0, 0x4181a0, the kiss's hold) isn't decoded: a bolt that
 * falls on Dante's spot after 30 frames, or a sweep about her for 32, at the bats' 300 damage. Where move 9 takes
 * her isn't read: back to where she came in. */
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
#define BAT_DAMAGE 300.0f
#define BAT_RADIUS 80.0f
#define STAGE_RADIUS 1500.0f   /* how far from where she came in she may go (the room's floor) */

int nevan_loops(int motion) { return motion == M0 || motion == M16 || motion == M44 || motion == M50; }

static void act(NevanBrain *b, int a) {
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
    b->strike_time = 0.0f;
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
    if (b->action >= 4 && b->action <= 6) b->side = unit() < 0.5f;   /* which way she circles (+0x4e1f) isn't read */
}

static void release_bats(NevanBrain *b) {
    int i, n = 0;
    for (i = 0; i < NEVAN_BATS && n < bat_count[b->phase]; ++i) {
        NevanBat *t = &b->bats[i];
        if (t->on) continue;
        t->on = 1;
        t->wait = 8.0f * (float)n;
        t->life = 360.0f;
        t->speed = 0.0f;
        t->pos[0] = b->pos[0] + sinf(b->yaw) * 150.0f;
        t->pos[1] = b->pos[1] + 100.0f;
        t->pos[2] = b->pos[2] + cosf(b->yaw) * 150.0f;
        t->dir[0] = t->dir[1] = t->dir[2] = 0.0f;
        ++n;
    }
}

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

/* An attack's event (its script's frame): bats, or a stand-in strike. */
static void attack_event(NevanBrain *b, float px, float py, float pz, int attacks) {
    switch (b->action) {
    case 22: case 23: release_bats(b); break;   /* 23's FUN_003ba470 also lets out bats it keeps count of [inferred] */
    case 32: {
        float dx = px - b->pos[0], dz = pz - b->pos[2];
        if (attacks && dx * dx + dz * dz < 300.0f * 300.0f && fabsf(py - b->pos[1]) < 300.0f) { b->land = 2; b->land_damage = BAT_DAMAGE; }
        break;
    }
    default:
        b->strike[0] = px; b->strike[1] = py; b->strike[2] = pz;
        b->strike_time = 30.0f;
        break;
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
    if (b->strike_time > 0.0f && (b->strike_time -= f) <= 0.0f) {
        float sx = px - b->strike[0], sz = pz - b->strike[2];
        float at[3] = {b->strike[0], b->strike[1] + 20.0f, b->strike[2]};
        fx_explosion(at);
        if (attacks && sx * sx + sz * sz < 150.0f * 150.0f) { b->land = 2; b->land_damage = BAT_DAMAGE; }
    }
    if (b->strike_time > 0.0f && ((int)b->timer & 3) == 0) {
        float at[3] = {b->strike[0], b->strike[1] + 10.0f, b->strike[2]}, v[3] = {0.0f, 30.0f, 0.0f};
        fx_mote(at, v, 120.0f, 12.0f, 0.3f, 0xffffd060u);
    }
    if (a == 21 && b->health <= 0) {
        if (b->timer > 200.0f) { b->remove = 1; }
        return;
    }
    if (a <= 6 && a != 0) {
        /* FUN_003b4500 (1-3) and FUN_003b4e30 (4-6): the speed climbs to its top; the square of it, in cm, a frame. */
        float keep = move_keep[a], room = d - keep, step;
        b->speed += move_rate[a] * f;
        if (b->speed > move_top[a]) b->speed = move_top[a];
        step = b->speed * b->speed * f;
        turn_to(b, px, pz);
        if (a <= 3) {
            if (room <= 0.0f) step = 0.0f; else if (room < step) step = room;
            b->pos[0] += sinf(b->yaw) * step; b->pos[2] += cosf(b->yaw) * step;
        } else {
            float side = b->yaw + (b->side ? -1.5707963f : 1.5707963f);
            if (d < 200.0f) { b->speed *= 0.98f; step = b->speed * b->speed * f; }
            b->pos[0] += sinf(side) * step; b->pos[2] += cosf(side) * step;
            if (keep > 0.0f && room > 0.0f) { float k = room < step ? room : step; b->pos[0] += sinf(b->yaw) * k; b->pos[2] += cosf(b->yaw) * k; }
        }
        if (b->timer > move_time[a >= 4][b->phase] || (keep > 0.0f && room <= 0.0f)) next_move(b, d, unit);
        keep_on_stage(b);
        return;
    }
    switch (a) {
    case 0:
        turn_to(b, px, pz);
        b->speed *= 0.98f;
        if (b->timer > idle_frames[b->phase]) next_move(b, d, unit);
        break;
    case 7: case 8: case 9:
        /* FUN_003b5870: out over 30 frames, moved, back over 30. */
        if (b->step == 0) {
            b->alpha = 1.0f - b->timer / 30.0f;
            if (b->timer >= 30.0f) {
                float cx = px - 2500.0f, cz = pz - 2500.0f, l = sqrtf(cx * cx + cz * cz), r = a == 8 ? 1000.0f : 400.0f;
                if (l < 1.0f) { cx = 0.0f; cz = 1.0f; l = 1.0f; }
                if (a == 9) { b->pos[0] = b->home[0]; b->pos[2] = b->home[2]; }
                else { b->pos[0] = px + cx / l * r; b->pos[2] = pz + cz / l * r; }
                keep_on_stage(b);
                turn_to(b, px, pz);
                b->hidden = 1; b->step = 1; b->timer = 0.0f;
            }
        } else {
            b->hidden = 0;
            b->alpha = b->timer / 30.0f;
            if (b->timer >= 30.0f) { b->alpha = 1.0f; next_move(b, d, unit); }
        }
        break;
    case 10: case 11: case 12: case 13: case 14: case 21:
        if (b->done) { b->pattern = -1; next_move(b, d, unit); }
        break;
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
