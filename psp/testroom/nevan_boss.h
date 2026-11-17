#ifndef DMC3PSP_TESTROOM_NEVAN_BOSS_H
#define DMC3PSP_TESTROOM_NEVAN_BOSS_H
/* Nevan (class 31, EST 49; em028.pac), Mission 9's boss in 210, the Sunken Opera House. enemy.c keeps her body
 * (model, motion, pose, health bar); this is what she does (docs/ENEMY_SPECS.md G). See nevan_boss.c. */

/* The motions her action scripts play: "nv2NN", PAC [2] slot NN (tools/prepare_skinned_model.py NEVAN_MOTIONS). */
#define NEVAN_M_COUNT 23
extern const char *const nevan_motion_names[NEVAN_M_COUNT];

#define NEVAN_BATS 12
typedef struct { int on; float pos[3], dir[3], speed, wait, life; } NevanBat;
typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float home[3];               /* where she came in: she keeps to the stage around it */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, speed, alpha, wait;
    float brake;                 /* a move's last 40 frames, slowing (FUN_003b4330 & co., state 2) */
    int placed;                  /* out: a teleport put her here; find the floor from pos[1] down */
    int action, step, health, max_health, hidden, side;
    int pattern, pattern_at, move, phase;
    int want, restart;           /* out: the motion to play (an index into nevan_motion_names), and from the top */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    float land_damage;           /* out: its damage (the attack record's) */
    /* The kiss's hold (action 36, FUN_003b8810). */
    int hold;                    /* out: she took Dante this frame; the mash he needs (0x5bcf10) */
    int held_free;               /* in: he mashed free */
    int release;                 /* out: she let him go (1) or threw him (2) */
    float drain;                 /* out: health taken from him this frame */
    float hold_time, heal;       /* +0x517c; her health back, part units */
    int remove;                  /* out: she's gone */
    NevanBat bats[NEVAN_BATS];
    /* Her shells (CEm028Shl01, Shl03-Shl07): a sphere that waits its fuse, then hurts for `active` frames; follow 1: it
     * stays on her; vel: it flies. Each hurts once. */
    struct { int on, follow, done; float pos[3], vel[3], fuse, active, radius, damage, top; } shells[6];
    /* Action 24's strikes (FUN_0045d720's class): a delay, then his spot marked for 40 frames, then live for 20. */
    struct { int on, state, done; float wait, pos[3]; } strikes[4];
    /* Actions 29 / 38's floor lightning (FUN_0043df80's class): its stage (0 charging, 1 the ring about her, 2 the
     * ring and the whole floor, 3 spent) and time; whether this stage has hurt him. */
    int field, field_done;
    float field_wait;
    /* Action 25's balls (FUN_00468580's class): a pillar (mode 0) or half a bar (mode 1, partner = the other half),
     * gathering by her (state 0) until released, then after `wait` flying to `to` (state 1, `life` frames). */
    struct { int on, mode, state, partner, done; float pos[3], at[3], to[3], speed, wait, life; } orbs[12];
    int orbs_pending;            /* 25 has started: make its balls this frame */
} NevanBrain;

void nevan_force(NevanBrain *b, int action);   /* tests: start an action now */
void nevan_spawn(NevanBrain *b, int health);
void nevan_think(NevanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
/* kind: 0 light, 1 heavy, 2 a blow that throws. 1 if she fell. */
int nevan_hit(NevanBrain *b, int kind, float fx, float fz, int mortal);
int nevan_loops(int motion);
void nevan_draw_fx(const NevanBrain *b);   /* her bats, as glows */

#endif
