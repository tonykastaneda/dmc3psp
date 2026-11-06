#ifndef DMC3PSP_CERBERUS_BOSS_H
#define DMC3PSP_CERBERUS_BOSS_H

/* Mission 3's CEm025. One enemy-manager slot owns the body, three heads and all shells. */
int cerberus_boss_init(void);
void cerberus_boss_set_difficulty(int difficulty);
void cerberus_boss_spawn(void);
void cerberus_boss_kill(void);
int cerberus_boss_update(float frames, float px, float py, float pz, int attacks);
/* Returns 1 only on the transition to the story death state. */
int cerberus_boss_hit(int kind, float from_x, float from_z, int mortal);
void cerberus_boss_draw(void);
int cerberus_boss_dying(void);
float cerberus_boss_health(void);
float cerberus_boss_max_health(void);
void cerberus_boss_select_target(float px, float pz);
void cerberus_boss_root_position(float out[3]);
void cerberus_boss_target_position(float out[3]);
float cerberus_boss_segment_hit(const float a[3], const float b[3]);
void cerberus_boss_clamp_player(float *z);
/* One damage event, cleared by reading it. Damage is the original record's value. */
int cerberus_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
void cerberus_boss_trace(const char *path);

#endif
