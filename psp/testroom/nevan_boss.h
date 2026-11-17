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
    int action, step, health, max_health, hidden, side;
    int pattern, pattern_at, move, phase;
    int want, restart;           /* out: the motion to play (an index into nevan_motion_names), and from the top */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    float land_damage;           /* out: its damage (the attack record's) */
    int remove;                  /* out: she's gone */
    NevanBat bats[NEVAN_BATS];
    float strike[3], strike_time;   /* a stand-in strike's spot and its fuse */
} NevanBrain;

void nevan_spawn(NevanBrain *b, int health);
void nevan_think(NevanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
/* kind: 0 light, 1 heavy, 2 a blow that throws. 1 if she fell. */
int nevan_hit(NevanBrain *b, int kind, float fx, float fz, int mortal);
int nevan_loops(int motion);
void nevan_draw_fx(const NevanBrain *b);   /* her bats, as glows */

#endif
