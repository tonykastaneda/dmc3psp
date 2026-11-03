#ifndef DMC3PSP_TESTROOM_ENEMY_H
#define DMC3PSP_TESTROOM_ENEMY_H

/* Training dummy: a Seven Hells enemy (GDATA slot 117, em000) standing in the
 * test street with infinite health. It never attacks; it idles, flinches,
 * gets knocked back, launched, juggled, knocked down and gets back up, all
 * with its own motions (hl-NN). */

typedef enum {
    HIT_LIGHT,    /* combo hits: flinch in place */
    HIT_HEAVY,    /* finishers, Stinger: knock back */
    HIT_LAUNCH,   /* High Time: launched into the air */
    HIT_AIR,      /* any hit while airborne: juggle pop */
    HIT_SLAM      /* Helm Breaker: smashed to the floor */
} HitKind;

typedef enum { ENEMY_IDLE, ENEMY_FLINCH, ENEMY_KNOCKBACK, ENEMY_LAUNCHED, ENEMY_AIRBORNE, ENEMY_DOWN, ENEMY_GETUP } EnemyState;

int enemy_init(void);
void enemy_spawn(float x, float z);
void enemy_update(float dt);
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
