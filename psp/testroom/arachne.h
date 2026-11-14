#ifndef DMC3PSP_TESTROOM_ARACHNE_H
#define DMC3PSP_TESTROOM_ARACHNE_H
/* The Arachne (class 10, EST 28; em010.pac), Mission 9's spider. enemy.c keeps the body (model, motion, pose);
 * this is what it decides. See arachne.c. */

enum { ARACHNE_M_IDLE, ARACHNE_M_RUN, ARACHNE_M_LUNGE, ARACHNE_M_POUNCE, ARACHNE_M_FLINCH, ARACHNE_M_KNOCK,
       ARACHNE_M_DEATH, ARACHNE_M_COUNT };
extern const char *const arachne_motion_names[ARACHNE_M_COUNT];   /* tools/prepare_skinned_model.py ARACHNE_MOTIONS */

typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, vel;
    int action, step, health;
    int want;                    /* out: the motion to play (ARACHNE_M_*) */
    int restart;                 /* out: start it from the top (cleared by the caller) */
    int land;                    /* out: an attack landed on Dante this frame (1 light, 2 heavy) */
    int remove;                  /* out: the body is gone */
} ArachneBrain;

void arachne_spawn(ArachneBrain *b, int health);
/* f: frames this step; unit: a random number in [0, 1). attacks: whether it may hurt Dante. */
void arachne_think(ArachneBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
/* A hit from (fx, fz): returns 1 if it died. */
int arachne_hit(ArachneBrain *b, int heavy, float fx, float fz, int mortal);
int arachne_loops(int motion);   /* idle and run loop */

#endif
