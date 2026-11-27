/* Agni & Rudra's brain: the game's states and choices (docs/ENEMY_SPECS.md F). The brothers' class runs 66
 * states (dispatched by FUN_003a0d50); each plays motion scripts from em026 [12] / [13] (+0x2ddc) and the brain
 * (FUN_003acfb0 paired, FUN_003ace70 alone) writes the next one. Rank 0 (Easy and Normal). */
#include "agni_rudra.h"

#include <stdint.h>

/* The game's own generator (0x1e560b0's twister), not needed here: any uniform bits will do. */
static uint32_t ar_seed = 0x2545f491u;
static uint32_t ar_bits(void) {
    ar_seed ^= ar_seed << 13; ar_seed ^= ar_seed >> 17; ar_seed ^= ar_seed << 5;
    return ar_seed;
}

#define S(...) {__VA_ARGS__, -1}
/* From each state's handler: the scripts it plays in order, its turn rate (FUN_003afe00), the state after. */
static const ArStateInfo states[AR_STATE_COUNT] = {
    /* 0 a flinch (the brain's +0x2df5 bit) */ {S(26), 0x888, 2, 0, -1},
    /* 1 */ {S(24, 25), 0, 2, 0, -1},
    /* 2 idle */ {S(0), 0, -1, 1, -1},
    /* 3 walk */ {S(1), 0x300, -1, 1, -1},
    /* 4 */ {S(94), 0x300, -1, 1, -1},
    /* 5-7 */ {S(96), 0x300, -1, 1, -1}, {S(96), 0x300, -1, 1, -1}, {S(96), 0x300, -1, 1, -1},
    /* 8-10 */ {S(95), 0x300, -1, 1, -1}, {S(95), 0x300, -1, 1, -1}, {S(95), 0x300, -1, 1, -1},
    /* 11 the leap (FUN_003a3860; enemy.c ar_leap): take-off, rise, air, landing */ {S(2, 3, 4, 5), 0x800, 2, 0, -1},
    /* 12-15 dashes (FUN_003b0450 n, records 0x5b7600 + 0x30 n) */
    {S(6, 7, 8, 9), 0, 2, 0, 0}, {S(10, 11, 12, 13), 0, 2, 0, 1}, {S(14, 15, 16, 17), 4096, 2, 0, 2}, {S(18, 19, 20, 21), 4096, 2, 0, 3},
    /* 16-20 hops 0-4 (FUN_003b09c0 n, records 0x5b76d0 + 0x40 n; enemy.c ar_hop_run) */
    {S(22, 23, 24, 25), 0, 2, 0, -1}, {S(22, 23, 24, 25), 0, 2, 0, -1}, {S(22, 23, 24, 25), 0, 2, 0, -1},
    {S(22, 23, 24, 25), 0, 2, 0, -1}, {S(22, 23, 24, 25), 0, 2, 0, -1},
    /* 21, 23, 25 the guard stances by swords (FUN_003a4270 / 4770 / 4c70: the guard up, +0x1e4 = 2, turning for 20
     * frames, then lowered); 22, 24, 26 a blow taken on the guard (FUN_003a44c0 / 49c0 / 4e90: the hold, then
     * lowered; without an attack of his own yet (+0x2de5) only lowered, enemy.c ar_go) */
    {S(27, 29), 0x800, 2, 0, -1},
    {S(28, 29), 0x800, 2, 0, -1}, {S(30, 32), 0x800, 2, 0, -1}, {S(31, 32), 0x800, 2, 0, -1},
    {S(33, 35), 0x800, 2, 0, -1}, {S(34, 35), 0x800, 2, 0, -1},
    /* 27-29 the guard broken (FUN_003a5140..: 11 blows' worth, 0x619588) */ {S(36), 0x800, 2, 0, -1}, {S(37), 0x800, 2, 0, -1}, {S(38), 0x800, 2, 0, -1},
    /* 30-35 a blow met in his swing's guard (FUN_003a24d0 by the swing: the parry, then its counter) */
    {S(39, 40), 0, 2, 0, -1}, {S(39, 41), 0, 2, 0, -1}, {S(45), 0, 2, 0, -1},
    {S(42, 43), 0, 2, 0, -1}, {S(42, 44), 0, 2, 0, -1}, {S(45), 0, 2, 0, -1},
    /* 36 his swing broken (FUN_003a5ee0; enemy.c ar_stun): staggered, held, a spinning hop back up */ {S(46, 47, 48, 49), 0, 2, 0, -1},
    /* 37, 38 */ {S(50), 0, 2, 0, -1}, {S(51), 0, 2, 0, -1},
    /* 39 death */ {S(52), 0, -1, 0, -1},
    /* 40, 41 */ {S(53), 0, 2, 0, -1}, {S(53), 0, 2, 0, -1},
    /* 42-45 sword attacks: Agni's (42, 43), Rudra's (44, 45) */
    {S(55), 0x300, 2, 0, -1}, {S(56), 0x300, 2, 0, -1}, {S(57), 0x300, 2, 0, -1}, {S(58), 0x300, 2, 0, -1},
    /* 46-49 the long attacks */ {S(87), 0x300, 2, 0, -1}, {S(88), 0x300, 2, 0, -1}, {S(89), 0x300, 2, 0, -1}, {S(90), 0x300, 2, 0, -1},
    /* 50, 51 charge the sword */ {S(59), 0, 2, 0, -1}, {S(60), 0, 2, 0, -1},
    /* 52-54 the leaping slam (FUN_003a8200; its scripts by swords, gp tables 0x6195a0-0x6195b8): the blade meets
     * the floor at frame 165 and bursts there, fire (effect 0x311) or wind (0x312) */
    {S(61, 62, 63, 64), 0x800, 2, 0, -1}, {S(61, 62, 63, 64), 0x800, 2, 0, -1}, {S(65, 66, 67, 68), 0x800, 2, 0, -1},
    /* 55 at Dante above them */ {S(69, 70, 71, 72, 73), 0x800, 2, 0, -1},
    /* 56 the leap at Dante (FUN_003a94c0 0; enemy.c ar_pounce): crouch, spring, air, landing */ {S(74, 75, 76, 77), 0x800, 2, 0, -1},
    /* 57, 58 (the survivor's below each health mark, FUN_003aeca0; their shots aren't ported, never picked) */
    {S(84, 85, 86), 0, 2, 0, -1}, {S(84, 85, 86), 0x300, 2, 0, -1},
    /* 59, 60 on his pedestal taking up both swords (FUN_003aa180 0 / 1; enemy.c ar_pedestal) */
    {S(78, 80, 82), 0, 56, 0, -1}, {S(79, 81, 83), 0, 56, 0, -1},
    /* 61, 62 */ {S(-1), 0, 2, 0, -1}, {S(-1), 0, 2, 0, -1},
    /* 63 the pedestal climb (FUN_003aad50; enemy.c ar_climb), 64 the opening (enemy.c ar_opening) */
    {S(26, 2, 3, 4), 0x800, 2, 0, -1}, {S(26), 0x888, 2, 0, -1},
    /* 65 the opening's leap, from the spring (FUN_003a94c0 1) */ {S(75, 76, 77), 0x800, 2, 0, -1},
};

const ArStateInfo *ar_state_info(int state) {
    return state >= 0 && state < AR_STATE_COUNT ? &states[state] : &states[AR_IDLE];
}

/* FUN_003af760: the wait to the next attack, 0x5b7440[rank]. */
#define AR_ATTACK_WAIT 300.0f

void ar_brain_reset(ArBrain *b, int set) {
    b->set = set;
    b->attack_timer = AR_ATTACK_WAIT;
    b->may_attack = 0;
    b->move_wait = 0.0f;
    b->high = 0.0f;
    b->charged = 0.0f;
    b->charge_fx = 0;
    b->flags = 0x12;
    b->paired = 1;
}

void ar_pair_survivor(ArBrain *b) { b->flags = 0x8000; }   /* 0x5cf600 record 9: {0x8000, 0x8000}, 999999 frames */

/* 0x5b76d0: 0 straight up, 1 / 2 ahead / back, 3 / 4 right / left, 5 the climb (up 40, slowing 0.5 a frame, turning
 * 0x1000 = 0x5b7410[4]). All play scripts 22-25. */
static const ArHop hops[6] = {
    {0, {22, 23, 24, 25}, 50.0f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f, -25.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0, {22, 23, 24, 25}, 50.0f, -3.0f, 0.0f, 12.0f, 0.0f, 12.0f, -3.0f, -25.0f, 0.0f, 12.0f, 0.0f, 12.0f},
    {0, {22, 23, 24, 25}, 50.0f, -3.0f, 0.0f, -12.0f, 0.0f, -12.0f, -3.0f, -25.0f, 0.0f, -12.0f, 0.0f, -12.0f},
    {0, {22, 23, 24, 25}, 40.0f, -3.0f, 10.0f, 0.0f, 10.0f, 0.0f, -3.0f, -25.0f, 10.0f, 0.0f, 10.0f, 0.0f},
    {0, {22, 23, 24, 25}, 40.0f, -3.0f, -10.0f, 0.0f, -10.0f, 0.0f, -3.0f, -25.0f, -10.0f, 0.0f, -10.0f, 0.0f},
    {0x1000, {22, 23, 24, 25}, 40.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, -20.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};
const ArHop *ar_hop(int n) { return &hops[n >= 0 && n < 6 ? n : 0]; }

void ar_brain_charged(ArBrain *b) { b->charged = 1800.0f; }

static int zone(float d) { return d <= 210.0f ? 0 : d <= 350.0f ? 1 : d <= 650.0f ? 2 : d <= 1000.0f ? 3 : 4; }

void ar_brain_tick(ArBrain *b, const ArSense *s, float f) {
    float rate;
    /* FUN_003afbe0: Dante 300 above them. */
    if (s->above > 300.0f) b->high += f;
    else b->high = 0.0f;
    if (b->charged > 0.0f) { b->charged -= f; if (b->charged < 0.0f) b->charged = 0.0f; }
    if (b->move_wait > 0.0f) b->move_wait -= f;
    /* FUN_003af5c0: the attack timer runs (2 - health fraction) a frame, faster for the survivor and the
     * nearer, slower while his brother attacks (he's the one with permission then: flag 0x100). */
    if (b->may_attack) return;
    if (s->brother_alive && (b->flags & 0x300)) return;   /* the formation keeps him from attacking */
    rate = (2.0f - s->health_fraction) * f;
    if (s->brother_alive && s->brother_state == 36) rate *= 2.5f;
    if (b->set == 2) rate *= 2.0f;
    rate *= zone(s->distance) < 3 ? 0.75f : 0.5f;
    b->attack_timer -= rate;
    if (b->attack_timer <= 0.0f) { b->attack_timer = 0.0f; b->may_attack = 1; }
}

void ar_brain_counter(ArBrain *b) { b->may_attack = 2; }

/* FUN_003ade30's second table, after a guard (permission 2 or 4). */
static int pick_counter(ArBrain *b, const ArSense *s) {
    int z = zone(s->distance), charged = b->charged > 0.0f;
    if (b->high >= 88.0f) return 55;
    if (b->set == 2) {
        if (z < 2) return (ar_bits() & 15) == 15 ? 54 : (ar_bits() & 2) ? 43 : 45;
        if (z < 3) return (ar_bits() & 7) == 7 ? 54 : (ar_bits() & 2) ? 47 : 49;
        return (ar_bits() & 3) == 3 ? 54 : 11;
    }
    if (b->set == 1) {
        if (z < 2) return !charged ? 44 : (ar_bits() & 7) == 7 ? 53 : 45;
        if (z < 3) return !charged ? 48 : (ar_bits() & 7) == 7 ? 53 : 49;
        if (z < 4) return 56;
        return -1;
    }
    if (z < 2) return !charged ? 42 : 43;
    if (z < 3) return !charged ? 46 : (ar_bits() & 7) == 7 ? 52 : 47;
    if (z < 4) return !charged ? 56 : 52;
    return -1;
}

/* FUN_003ade30 with permission 1: by swords, zone, and whether they're charged. */
static int pick_attack(ArBrain *b, const ArSense *s) {
    int z = zone(s->distance), charged = b->charged > 0.0f;
    if (b->may_attack != 1) return pick_counter(b, s);
    if (b->high >= 88.0f) return 55;
    if (b->set == 2) {
        if (z < 2) return (ar_bits() & 7) == 7 ? 54 : (ar_bits() & 2) ? 43 : 45;
        if (z < 3) return (ar_bits() & 3) == 3 ? 54 : (ar_bits() & 2) ? 47 : 49;
        if (z < 4) return (ar_bits() & 4) ? 54 : 56;
        return -1;
    }
    if (b->set == 1) {
        if (z < 2) return !charged ? 44 : (ar_bits() & 3) == 3 ? 53 : 45;
        if (z < 3) return !charged ? 51 : (ar_bits() & 3) == 3 ? 53 : 49;
        if (z < 4) return !charged ? 51 : 56;
        return -1;
    }
    if (z < 2) return !charged ? 42 : 43;
    if (z < 3) return !charged ? 50 : (ar_bits() & 3) == 3 ? 52 : 47;
    if (z < 4) return !charged ? 50 : 52;
    return -1;
}

void ar_pair_formation(ArBrain *first, ArBrain *second, int formation) {
    /* 0x5cf600 records 5 and 6: {0x12, 0x213} and {0x22, 0x223}. */
    first->flags = formation == 5 ? 0x12 : 0x22;
    second->flags = formation == 5 ? 0x213 : 0x223;
}

/* +0x2ee2: the role, [2,0,1,2,3,4][flags & 15] (0x6195d8). */
static int role(const ArBrain *b) { static const unsigned char r[6] = {2, 0, 1, 2, 3, 4}; return (b->flags & 15) < 6 ? r[b->flags & 15] : 1; }

/* FUN_003ad5d0's three ways round Dante, by the flank. */
static int round_a(int flank) { static const signed char t[5] = {3, 9, 15, 6, 14}; return t[flank]; }
static int round_c(int flank) { static const signed char t[5] = {4, 10, 15, 7, 14}; return t[flank]; }
static int round_b(ArBrain *b, int flank) {
    static const signed char t[5] = {-1, 8, 15, 5, 14};
    if (flank) return t[flank];
    if (b->flags & 0x10) return 5;
    if (b->flags & 0x20) return 8;
    b->move_wait = 0.0f;
    return 2;
}

/* FUN_003ad5d0, with his brother up. */
static int pick_move_paired(ArBrain *b, const ArSense *s) {
    int r = role(b), f = s->flank;
    switch (zone(s->distance)) {
    case 4:
        if (r < 3) return 11;
        if (b->move_wait > 0.0f) return -1;
        b->move_wait = 12.0f;
        return r < 4 ? round_a(f) : round_b(b, f);
    case 3:
        if (r < 2) return 12;
        if (b->move_wait > 0.0f) return -1;
        b->move_wait = 12.0f;
        return r < 3 ? round_a(f) : r < 4 ? round_b(b, f) : round_c(f);
    case 2:
        if (r >= 4) return 13;
        if (b->move_wait > 0.0f) return -1;
        b->move_wait = 12.0f;
        return r == 3 ? round_c(f) : r < 2 ? round_a(f) : round_b(b, f);
    case 1:
        if (r >= 3) return 13;
        if (b->move_wait > 0.0f) return -1;
        b->move_wait = 12.0f;
        return r == 2 ? round_c(f) : round_b(b, f);
    default:
        return r < 2 ? (r ? 4 : -1) : 13;
    }
}

/* FUN_003ad180 (alone; the role +0x2ee2 is 1 then). */
static int pick_move(ArBrain *b, const ArSense *s) {
    if (s->brother_alive) return pick_move_paired(b, s);
    switch (zone(s->distance)) {
    case 4: return 11;
    case 3: case 2: return 12;
    case 1:
        if (b->move_wait > 0.0f) return -1;
        b->move_wait = 12.0f;
        return (ar_bits() & 4) ? 15 : 14;
    default: return 4;
    }
}

int ar_brain_decide(ArBrain *b, const ArSense *s, int state) {
    int next;
    if (b->may_attack) {
        next = pick_attack(b, s);
        if (next >= 0) {
            b->may_attack = 0;
            b->attack_timer = AR_ATTACK_WAIT;
            return next;
        }
    }
    next = pick_move(b, s);
    return next == state ? -1 : next;
}

void ar_dash(int dash, float *start, float *top, float *hold, float *slow, int *sideways) {
    /* 0x5b7600: {6,7,8,9} forward, {10..13} back, {14..17} and {18..21} to each side: 5 rising to 25, held 9
     * frames, then slowing 1.43 a frame. */
    *start = 5.0f; *top = 25.0f; *hold = 9.0f; *slow = 1.43f;
    *sideways = dash >= 2;
    if (dash == 1 || dash == 3) { *start = -*start; *top = -*top; *slow = -*slow; }
}
