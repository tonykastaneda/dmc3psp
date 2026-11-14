#ifndef DMC3PSP_TESTROOM_NEVAN_BOSS_H
#define DMC3PSP_TESTROOM_NEVAN_BOSS_H
/* Nevan (class 31, EST 49; em028.pac), Mission 9's boss in 210, the Sunken Opera House. enemy.c keeps her body
 * (model, motion, pose, health bar); this is what she does. See nevan_boss.c. */

enum { NEVAN_M_HOVER, NEVAN_M_GLIDE, NEVAN_M_BLADE, NEVAN_M_BATS, NEVAN_M_FLINCH, NEVAN_M_KNOCK, NEVAN_M_DEATH,
       NEVAN_M_COUNT };
extern const char *const nevan_motion_names[NEVAN_M_COUNT];   /* tools/prepare_skinned_model.py NEVAN_MOTIONS */

#define NEVAN_BATS 12
typedef struct { int on; float pos[3], vel[3], life; } NevanBat;
typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float home[3];               /* where she came in: she keeps to the stage around it */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, vel, gauge, alpha;
    int action, step, health, hidden;
    int want, restart;           /* out: the motion to play, and whether from the top */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    int remove;                  /* out: she's gone */
    NevanBat bats[NEVAN_BATS];
} NevanBrain;

void nevan_spawn(NevanBrain *b, int health);
void nevan_think(NevanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
int nevan_hit(NevanBrain *b, int heavy, float fx, float fz, int mortal);   /* 1 if she fell */
int nevan_loops(int motion);
void nevan_draw_fx(const NevanBrain *b);   /* her bats, as glows */

#endif
