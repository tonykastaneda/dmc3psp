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
    float charged;       /* +0x2eec: frames left on the charged sword (+0x2ee4) */
} ArBrain;

/* What the brain sees. */
typedef struct {
    float distance, above;   /* to Dante: on the ground, and his height over them */
    float health_fraction;
    int brother_alive, brother_state, brother_attacking;
} ArSense;

const ArStateInfo *ar_state_info(int state);
void ar_brain_reset(ArBrain *b, int set);
/* Once a frame, f frames long: the timers. */
void ar_brain_tick(ArBrain *b, const ArSense *s, float f);
/* The next state, or -1 to carry on. Call in states whose info has `brain`. */
int ar_brain_decide(ArBrain *b, const ArSense *s, int state);
/* A state that charges the sword (50 Agni's, 51 Rudra's) finished: charged for 1800 frames (003ac280). */
void ar_brain_charged(ArBrain *b);
/* A dash record: speeds by phase (0x5b7600; start, top, held frames, slowing a frame). */
void ar_dash(int dash, float *start, float *top, float *hold, float *slow, int *sideways);

#endif
