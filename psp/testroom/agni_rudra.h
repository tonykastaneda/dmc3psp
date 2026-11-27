#ifndef DMC3PSP_AGNI_RUDRA_H
#define DMC3PSP_AGNI_RUDRA_H

/* Agni & Rudra's brain (class 27's brothers, docs/ENEMY_SPECS.md F): the game's state ids, what each plays and
 * what each picks next. enemy.c owns their bodies and runs the motion scripts; this decides. Rank 0 (Easy and
 * Normal). Frames are the game's 60 Hz frames, distances cm. */

#define AR_STATE_COUNT 66
#define AR_IDLE 2
#define AR_DEATH 39

typedef struct {
    signed char scripts[6];   /* motion scripts in order (-1 ends); each runs to its finish, then the next */
    unsigned short turn;      /* how far he turns to Dante a frame, 65536 a circle (0: holds his facing) */
    signed char next;         /* the state after the last script (-1: the brain picks, from idle) */
    unsigned char brain;      /* the brain runs every frame in this state (idle and the moves) */
    signed char dash;         /* 0-3: a dash from 0x5b7600 (forward, back, right, left), else -1 */
} ArStateInfo;

typedef struct {
    int set;             /* swords held: 0 Agni's, 1 Rudra's, 2 both (the survivor) */
    float attack_timer;  /* +0x2e20: runs down to the next attack */
    int may_attack;      /* +0x2de6 */
    float move_wait;     /* +0x2e24 / +0x2e28: frames before another move */
    float high;          /* +0x2f38: frames Dante's been 300 above */
    int charge_fx;       /* +0x2ef8 / +0x2efc: the charged blade's effect (enemy.c), 0 none */
    int charge_sword;    /* the sword charged: 0 Agni's (state 50), 1 Rudra's (51) */
    float charged;       /* +0x2eec: frames left on the charged sword (+0x2ee4) */
    unsigned short flags; /* +0x2ede, from the pair's formation (0x5cf6a0): role in bits 0-3, 0x10 / 0x20 which way
                            to flank, 0x100 / 0x200 no attack permission, 0x1000 stand, 0x8000 climb his pedestal */
    int paired;          /* +0x2ee7: the pair's brain (FUN_003acfb0), until he takes up both swords (FUN_003aa180) */
} ArBrain;

/* A hop (FUN_003b09c0, records 0x5b76d0 + 0x40 n): its four scripts (take-off, rise, fall, landing), its turn to the
 * target a frame, and the local speeds (x right, y up, z ahead) it rises then falls with: a start, accelerations
 * and caps, as FUN_003b0090 / FUN_003b02b0 run them. */
typedef struct {
    unsigned short turn;
    signed char scripts[4];
    float up, rise_ay, rise_ax, rise_az, rise_cap_x, rise_cap_z;
    float fall_ay, fall_cap_y, fall_ax, fall_az, fall_cap_x, fall_cap_z;
} ArHop;

/* What the brain sees. */
typedef struct {
    float distance, above;   /* to Dante: on the ground, and his height over them */
    float health_fraction;
    int brother_alive, brother_state, brother_attacking;
    int flank;               /* +0x2ee3 (FUN_003acae0): 0 across Dante from his brother, 1 / 3 part way, 2 / 4 near */
} ArSense;

const ArStateInfo *ar_state_info(int state);
void ar_brain_reset(ArBrain *b, int set);
/* Once a frame, f frames long: the timers. */
void ar_brain_tick(ArBrain *b, const ArSense *s, float f);
/* The next state, or -1 to carry on. Call in states whose info has `brain`. */
int ar_brain_decide(ArBrain *b, const ArSense *s, int state);
/* A state that charges the sword (50 Agni's, 51 Rudra's) finished: charged for 1800 frames (003ac280). */
void ar_brain_charged(ArBrain *b);
/* A guard turned a blow (22, 24, 26 set +0x2de6 = 2): he may counter at once, from the second table. */
void ar_brain_counter(ArBrain *b);
/* The pair's controller (class 27, FUN_004810f0 / 004811b0) at rank 0: every 1800 frames the next formation, 6
 * then 5; the healthier brother takes the first slot's flags. Fills both brains' flags. */
void ar_pair_formation(ArBrain *first, ArBrain *second, int formation);
/* A dash record: speeds by phase (0x5b7600; start, top, held frames, slowing a frame). */
void ar_dash(int dash, float *start, float *top, float *hold, float *slow, int *sideways);
/* Hop n (0-5): states 16-20 are hops 0-4, the pedestal climb (63) hop 5. */
const ArHop *ar_hop(int n);
/* The pair's controller when the second brother falls (FUN_004810f0: one left of two, formation 9): flags 0x8000,
 * so the survivor's brain (FUN_003acfb0) sends him up his pedestal (state 63). */
void ar_pair_survivor(ArBrain *b);

#endif
