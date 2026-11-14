#ifndef DMC3PSP_ARKHAM_BOSS_H
#define DMC3PSP_ARKHAM_BOSS_H

/* Mission 19's Arkham (class 35, em pack 23, GDATA 137) in room 421, Forbidden Nirvana, with his Legions (class 36).
 * One enemy-manager slot owns him and every Legion. See docs/ARKHAM_SPEC.md. */
int arkham_boss_init(void);
void arkham_boss_set_difficulty(int difficulty);
void arkham_boss_spawn(float x, float y, float z);
void arkham_boss_kill(void);
void arkham_boss_update(float frames, float px, float py, float pz, int attacks);
/* Dante's blade: a Legion in its reach is cut down. */
void arkham_boss_strike(float x, float y, float z, float yaw, float reach);
/* Returns 1 only on the transition to the dying state. */
int arkham_boss_hit(int kind, float from_x, float from_z, int mortal);
void arkham_boss_draw(void);
int arkham_boss_dying(void);
int arkham_boss_hidden(void);
float arkham_boss_health(void);
float arkham_boss_max_health(void);
void arkham_boss_position(float out[3]);
void arkham_boss_target_position(float out[3]);
void arkham_boss_push_out(float *x, float *z, float y, float r);
int arkham_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
void arkham_boss_trace(const char *path);
void arkham_boss_set_floor(int (*floor_fn)(float x, float y, float z, float *out));

#endif
