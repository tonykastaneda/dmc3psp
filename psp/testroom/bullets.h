#ifndef DMC3PSP_TESTROOM_BULLETS_H
#define DMC3PSP_TESTROOM_BULLETS_H

/* Gun shots. In the game Ebony & Ivory's shell (FUN_002ef920) lives one frame:
 * an instant hit from the muzzle toward the lock-on point, or straight ahead
 * with no lock-on (FUN_002698d0). The shotgun and Spiral are treated the same
 * with their own ranges; Artemis and Kalina Ann fly (their shells are separate
 * code, not decoded: the speeds here are estimates). The flashes and tracers are in fx.c. */

typedef enum {
    BULLET_EBONY_IVORY, BULLET_SHOTGUN, BULLET_ARTEMIS, BULLET_SPIRAL, BULLET_KALINA_ANN, BULLET_KIND_COUNT
} BulletKind;

void bullets_reset(void);
void bullets_fire(int kind, const float muzzle[3], const float yaw, int has_target, const float target[3]);
/* The PS2 kind of the next Ebony & Ivory shot (0 ordinary, 5 charged, 7-9 rapid fire upgraded): its flash and damage. */
void bullets_set_shot_kind(int kind);
void bullets_update(float dt);
int bullets_shots(void);
int bullets_hits(void);

/* Shots pass the stage's walls: set by Secret Mission 9 (secret_missions.c), whose cart is caged by player-only
 * walls the port can't tell apart. */
extern int bullets_through_walls;

#endif
