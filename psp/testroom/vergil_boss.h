#ifndef DMC3PSP_VERGIL_BOSS_H
#define DMC3PSP_VERGIL_BOSS_H

/* Mission 7's Vergil: CEm035, variant 0 (Yamato only). One enemy-manager slot owns him, his coat,
 * Yamato and its scabbard. See docs/VERGIL_SPEC.md. */
int vergil_boss_init(void);
void vergil_boss_set_difficulty(int difficulty);
/* (x, y, z): his EST place, feet on the floor. */
void vergil_boss_spawn(float x, float y, float z);
void vergil_boss_kill(void);
/* Dante's command-table action (0 = none) and facing, for the guard trigger and the teleports. */
void vergil_boss_dante(int action, float yaw);
void vergil_boss_update(float frames, float px, float py, float pz, int attacks);
/* Returns 1 only on the transition to the dying state. */
int vergil_boss_hit(int kind, float from_x, float from_z, int mortal);
void vergil_boss_draw(void);
int vergil_boss_dying(void);
int vergil_boss_hidden(void);   /* mid-teleport: not there to be hit */
float vergil_boss_health(void);
float vergil_boss_max_health(void);
void vergil_boss_position(float out[3]);          /* feet */
void vergil_boss_target_position(float out[3]);   /* chest, for lock-on */
void vergil_boss_push_out(float *x, float *z, float y, float r);
/* One damage event, cleared by reading it. Damage is the original record's value. */
int vergil_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
void vergil_boss_trace(const char *path);
/* Tests: a line in the same log (Mission 7's route checks). */
void vergil_boss_note(const char *event, float a, float b, float c, float d);
/* The floor under (x, z) near height y; set by enemy.c so he walks the room's floor. */
void vergil_boss_set_floor(int (*floor_fn)(float x, float y, float z, float *out));

#endif
