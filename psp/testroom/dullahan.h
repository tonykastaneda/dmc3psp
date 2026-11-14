#ifndef DMC3PSP_TESTROOM_DULLAHAN_H
#define DMC3PSP_TESTROOM_DULLAHAN_H
/* Dullahan (CEm012, EST 30), native em012 model/motions with a separate shield/charge brain. */

enum { DULLAHAN_M_IDLE, DULLAHAN_M_RUN, DULLAHAN_M_LUNGE, DULLAHAN_M_POUNCE, DULLAHAN_M_FLINCH, DULLAHAN_M_KNOCK,
       DULLAHAN_M_DEATH, DULLAHAN_M_COUNT };
extern const char *const dullahan_motion_names[DULLAHAN_M_COUNT];   /* tools/prepare_skinned_model.py DULLAHAN_MOTIONS */

typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, vel;
    int action, step, health;
    int want;                    /* out: the motion to play (DULLAHAN_M_*) */
    int restart;                 /* out: start it from the top (cleared by the caller) */
    int land;                    /* out: an attack landed on Dante this frame (1 light, 2 heavy) */
    int remove;                  /* out: the body is gone */
} DullahanBrain;

void dullahan_spawn(DullahanBrain *b, int health);
/* f: frames this step; unit: a random number in [0, 1). attacks: whether it may hurt Dante. */
void dullahan_think(DullahanBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
/* A hit from (fx, fz): returns 1 if it died. */
int dullahan_hit(DullahanBrain *b, int heavy, float fx, float fz, int mortal);
int dullahan_loops(int motion);   /* idle and run loop */

#endif
