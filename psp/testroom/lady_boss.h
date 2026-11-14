#ifndef DMC3PSP_TESTROOM_LADY_BOSS_H
#define DMC3PSP_TESTROOM_LADY_BOSS_H
/* Lady (class 37, EST 55; em034.pac), Mission 16's boss in 115, the Divine Library. enemy.c keeps her body (model,
 * motion, pose, health bar); this is what she does. See lady_boss.c. */

enum { LADY_M_IDLE, LADY_M_RUN, LADY_M_STRAFE_R, LADY_M_STRAFE_L, LADY_M_BACK, LADY_M_SHOOT, LADY_M_GRENADE,
       LADY_M_KALINA, LADY_M_ESCAPE, LADY_M_FLINCH, LADY_M_KNOCK, LADY_M_DOWN, LADY_M_COUNT };
extern const char *const lady_motion_names[LADY_M_COUNT];   /* tools/prepare_skinned_model.py LADY_MOTIONS */

#define LADY_SHOTS 16
typedef struct { int on, kind; float pos[3], vel[3], life; } LadyShot;   /* 0 a bullet, 1 a grenade, 2 a rocket */
typedef struct {
    float pos[3], yaw, floor_y;  /* in and out */
    float home[3];
    float frame;                 /* in */
    int done;                    /* in */
    float timer, gauge;
    int action, step, health;
    int want, restart;           /* out */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    LadyShot shots[LADY_SHOTS];
} LadyBrain;

void lady_spawn(LadyBrain *b, int health);
void lady_think(LadyBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks);
int lady_hit(LadyBrain *b, int heavy, int mortal);   /* 1 if she's beaten */
int lady_loops(int motion);
void lady_draw_fx(const LadyBrain *b);

#endif
