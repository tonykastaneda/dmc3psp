#ifndef DMC3PSP_GERYON_BOSS_H
#define DMC3PSP_GERYON_BOSS_H

/* Mission 12's Geryon: CEm029 (the horse) and CEm029Cart (the carriage), em029.pac. One enemy-manager slot owns
 * both, and his shells (the cart's missiles, the red time spheres). Phase 0 runs on 228's bridge; at 80% health he
 * leaves it and phase 1 is fought on the ground below. See docs/GERYON_SPEC.md. */
int geryon_boss_init(void);
void geryon_boss_set_difficulty(int difficulty);
/* (x, y, z): his EST place. Phase 0 starts on the bridge whatever it says. */
void geryon_boss_spawn(float x, float y, float z);
void geryon_boss_kill(void);
void geryon_boss_update(float frames, float px, float py, float pz, int attacks);
/* Returns 1 only on the transition to the dying state. */
int geryon_boss_hit(int kind, float from_x, float from_z, int mortal);
void geryon_boss_draw(void);
int geryon_boss_dying(void);
int geryon_boss_hidden(void);   /* between the phases */
float geryon_boss_health(void);
float geryon_boss_max_health(void);
void geryon_boss_position(float out[3]);          /* the horse's feet */
/* The point of his body (horse and carriage) nearest Dante, at chest height: what a sword finds. */
void geryon_boss_target_position(float out[3]);
void geryon_boss_push_out(float *x, float *z, float y, float r);
/* One damage event, cleared by reading it. */
int geryon_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
/* Once, as phase 1 starts: where Dante goes (228's start 2, below the fallen bridge). */
int geryon_boss_take_warp(float out[4]);
void geryon_boss_trace(const char *path);
void geryon_boss_set_floor(int (*floor_fn)(float x, float y, float z, float *out));
/* Mission 18's 417 is 228's ground again: with this on, a spawn starts in phase 1 there (no bridge). */
void geryon_boss_start_on_ground(int on);

#endif
