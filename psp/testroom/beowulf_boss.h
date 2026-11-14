#ifndef DMC3PSP_TESTROOM_BEOWULF_BOSS_H
#define DMC3PSP_TESTROOM_BEOWULF_BOSS_H
/* Beowulf (class 33, EST 51; em030.pac), Mission 11's boss in 217, the Torture Chamber. enemy.c keeps his body
 * (model, motion, pose, health bar); this is what he does. See beowulf_boss.c. */

enum { BEOWULF_M_IDLE, BEOWULF_M_WALK, BEOWULF_M_CHARGE, BEOWULF_M_PUNCH1, BEOWULF_M_PUNCH2, BEOWULF_M_PUNCH3,
       BEOWULF_M_STOMP, BEOWULF_M_RUSH, BEOWULF_M_LASER, BEOWULF_M_FLINCH, BEOWULF_M_KNOCK, BEOWULF_M_DEATH,
       BEOWULF_M_COUNT };
extern const char *const beowulf_motion_names[BEOWULF_M_COUNT];   /* tools/prepare_skinned_model.py BEOWULF_MOTIONS */

#define BEOWULF_SHOTS 12
typedef struct { int on, kind; float pos[3], vel[3], life; } BeowulfShot;   /* kind 0 a pillar, 1 a feather */
typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float home[3];               /* where he came in: he keeps to the chamber around it */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, gauge;
    int action, step, health;
    int want, restart;           /* out: the motion to play, and whether from the top */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    BeowulfShot shots[BEOWULF_SHOTS];
} BeowulfBrain;

void beowulf_spawn(BeowulfBrain *b, int health);
void beowulf_think(BeowulfBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
int beowulf_hit(BeowulfBrain *b, int heavy, int mortal);   /* 1 if he fell */
int beowulf_loops(int motion);
void beowulf_draw_fx(const BeowulfBrain *b);

#endif
