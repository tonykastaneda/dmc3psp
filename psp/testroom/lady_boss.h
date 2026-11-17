#ifndef DMC3PSP_TESTROOM_LADY_BOSS_H
#define DMC3PSP_TESTROOM_LADY_BOSS_H
/* Lady (class 37, EST 55; em034.pac), Mission 16's boss in 115, the Divine Library. enemy.c keeps her body (model,
 * motion, pose, health bar); this is what she does (docs/ENEMY_SPECS.md J). See lady_boss.c. */

/* Her motions, "ldCNN": action a plays PAC [C] slot NN by FUN_0044afc0's ranges (lady_boss.c). */
enum { LADY_M_IDLE, LADY_M_STRAFE_R, LADY_M_STRAFE_L, LADY_M_GUN, LADY_M_TURN, LADY_M_SHOT, LADY_M_SHOT_UP,
       LADY_M_SHOT_DOWN, LADY_M_GUN2, LADY_M_TURN2, LADY_M_SHOT2, LADY_M_SHOT2_UP, LADY_M_SHOT2_DOWN, LADY_M_WALK,
       LADY_M_RUN, LADY_M_PAUSE, LADY_M_RELOAD, LADY_M_GRENADE, LADY_M_KALINA, LADY_M_ESCAPE, LADY_M_FLINCH,
       LADY_M_KNOCK, LADY_M_POSE, LADY_M_AIM, LADY_M_BIG, LADY_M_BIG_UP, LADY_M_BIG_DOWN, LADY_M_BIG_END,
       LADY_M_VOLLEY, LADY_M_VOLLEY_END, LADY_M_WIRE, LADY_M_ZIP, LADY_M_ZIP_LAND, LADY_M_ZIP_FALL, LADY_M_LEDGE_SHOT,
       LADY_M_HOP, LADY_M_HOP_LAND, LADY_M_COUNT };
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
    int magazine, bursts, last_mode;   /* +0x47e0 shots left, +0x47cc bursts since a pause, +0x47c1 */
    int max_health;
    int ring, spot, side;        /* her waypoint ring (+0x4859: 1 the floor's, 0 the middle's), its point (+0x47e4), +0x4858 */
    float to[3];                 /* where she's going (+0x47f0) */
    int volleys;                 /* mode 0x1c's shots left (+0x4848) */
    float token;                 /* frames until her next special (the manager +0x130's gate isn't read) */
    float speed, clock;          /* the grapple's rise (+0x4824 / +0x4828) and its clock (+0x482c) */
    int air;                     /* out: off the floor, pos[1] hers (2: the hop, which her motion's root travel moves;
                                    3: on a ledge) */
    int ledge;                   /* she let go over a ledge (0x5cc360) */
    int want, restart;           /* out */
    int land;                    /* out: hurt Dante this frame (1 light, 2 heavy) */
    float land_damage;           /* out: its attack record's damage */
    LadyShot shots[LADY_SHOTS];
} LadyBrain;

void lady_spawn(LadyBrain *b, int health);
/* sight: Dante's in her line of sight (FUN_00449450, a ray from her to him). */
void lady_think(LadyBrain *b, float f, float px, float py, float pz, float (*unit)(void), int attacks, int sight);
int lady_hit(LadyBrain *b, int heavy, int mortal);   /* 1 if she's beaten */
int lady_loops(int motion);
void lady_draw_fx(const LadyBrain *b);

#endif
