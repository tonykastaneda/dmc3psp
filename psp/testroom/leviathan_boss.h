#ifndef DMC3PSP_LEVIATHAN_BOSS_H
#define DMC3PSP_LEVIATHAN_BOSS_H

/* CStageSetHeart and the two CStageSetLungs form one encounter. Coordinates
 * are the stage models' native centimetres. Side organs are targetable too. */
enum { LEVIATHAN_HEART, LEVIATHAN_RIGHT_LUNG, LEVIATHAN_LEFT_LUNG };
int leviathan_boss_init(void);
void leviathan_boss_draw(void);
void leviathan_boss_set_difficulty(int difficulty);
void leviathan_boss_spawn(void);
void leviathan_boss_kill(void);
int leviathan_boss_update(float frames, float px, float py, float pz, int attacks);
int leviathan_boss_hit(int part, float damage, int mortal);
void leviathan_boss_select_target(float px, float pz);
void leviathan_boss_target_position(float out[3]);
float leviathan_boss_health(void);
float leviathan_boss_max_health(void);
int leviathan_boss_dying(void);
int leviathan_boss_open(void);
int leviathan_boss_target(void);
/* Feed the killed Envy's orbs into the corresponding lung. */
void leviathan_boss_absorb(int part, float amount);

#endif
