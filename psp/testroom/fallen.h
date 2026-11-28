#ifndef DMC3PSP_TESTROOM_FALLEN_H
#define DMC3PSP_TESTROOM_FALLEN_H
/* The Fallen (CEm011, EST 29): em011's own model and motions, its scripts' timings, and the decoded hover and
 * attack picks. See docs/FALLEN_SPEC.md. */

/* The motions the brain asks for, by the scripts' ids (em011 [7]): "faBNN" is id B*100+NN. */
enum { FALLEN_M_APPEAR, FALLEN_M_HOVER, FALLEN_M_DRIFT, FALLEN_M_REACT, FALLEN_M_FALL, FALLEN_M_RISE, FALLEN_M_THRUST,
       FALLEN_M_LUNGE, FALLEN_M_SLASHES, FALLEN_M_DEATH, FALLEN_M_COUNT };
extern const char *const fallen_motion_names[FALLEN_M_COUNT];

typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float frame;                 /* in: the motion's frame */
    int done;                    /* in: the motion has played to its end */
    float timer, speed, bob, gauge, guard_hits, grounded_time;
    float goal[3];
    int state, action, step, side, mode, health;
    int want, restart;           /* out: the motion to play (FALLEN_M_*), from its start */
    int attack_record;           /* live record in the attack window */
    float attack_frames;
    int remove;                  /* out: the body is gone */
} FallenBrain;

void fallen_spawn(FallenBrain *b, int health);
/* f: frames this step; unit: a random number in [0, 1); token: whether a shared slot is available (it's
 * taken from the shared attack slots when this one attacks). */
void fallen_think(FallenBrain *b, float f, float px, float py, float pz, float player_yaw,
                  float (*unit)(void), int attacks, int *token);
/* A hit from (fx, fz): returns 1 if it died. heavy: a finisher. */
int fallen_hit(FallenBrain *b, int heavy, float fx, float fz, int mortal);
int fallen_loops(int motion);
int fallen_flying(const FallenBrain *b);
int fallen_appearing(const FallenBrain *b);   /* its entrance through the wall (state 0xe): no wall collision */

#endif
