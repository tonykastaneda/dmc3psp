#ifndef DMC3PSP_TESTROOM_ENEMY_H
#define DMC3PSP_TESTROOM_ENEMY_H

/* Training dummies: Seven Hells enemies (GDATA slot 117, em000) in the test street with
 * infinite health. They idle, flinch, get knocked back, launched, juggled, knocked down and
 * get back up, all with their own motions (hl-NN). The debug menu sets how many there are and
 * whether they attack Dante.
 *
 * Calls that take no index act on the selected dummy: see enemy_select. */
#define ENEMY_MAX 32

typedef enum {
    HIT_LIGHT,    /* combo hits: flinch in place */
    HIT_HEAVY,    /* finishers, Stinger: knock back */
    HIT_LAUNCH,   /* High Time: launched into the air */
    HIT_AIR,      /* any hit while airborne: juggle pop */
    HIT_SLAM      /* Helm Breaker: smashed to the floor */
} HitKind;

typedef enum { ENEMY_IDLE, ENEMY_FLINCH, ENEMY_KNOCKBACK, ENEMY_LAUNCHED, ENEMY_AIRBORNE, ENEMY_DOWN, ENEMY_GETUP, ENEMY_WALK, ENEMY_SWING, ENEMY_JUMP } EnemyState;

int enemy_init(void);
void enemy_spawn(float x, float z);          /* one dummy, here, selected */
void enemy_spawn_height(float y);            /* look for the floor from about here, not the roofs */
/* Keep the ones there are; add new ones in a ring around (x, z) or remove the last ones. */
void enemy_set_count(int count, float x, float z);
int enemy_count(void);
void enemy_select(int index);
void enemy_select_nearest(float x, float z);
void enemy_set_attacks(int attacks);
/* Bones update less often the more dummies there are; their positions still move every frame. */
void enemy_set_anim_auto(int on);
/* For the PSP benchmark, to see what costs what. */
void enemy_set_test(int no_texture, int no_lighting, float ring);
int enemy_anim_rate(void);          /* poses a second each right now, 0 = every frame */
int enemy_poses_done(void);         /* running total, to measure the saving */
/* (px, py, pz) is Dante, whom they go for when attacking. */
void enemy_update(float dt, float px, float py, float pz);
/* An attack reached Dante this frame: returns 1 once per hit, from where, and if it's a heavy one. */
int enemy_attack_landed(float *from_x, float *from_z, int *heavy);
/* Draw with the view/projection set, inside an open display list. */
void enemy_draw(void);
int enemy_active(void);
EnemyState enemy_state(void);
const char *enemy_state_name(void);
void enemy_position(float out[3]);   /* feet */
float enemy_height(void);            /* feet height above its floor (air time) */
int enemy_hits_taken(void);
/* Apply a hit from a point (Dante's position) -- knock-back direction. */
void enemy_hit(HitKind kind, float from_x, float from_z);
/* Keep Dante (cylinder radius r at x,z) out of the dummy. */
void enemy_push_out(float *x, float *z, float r);

#endif
