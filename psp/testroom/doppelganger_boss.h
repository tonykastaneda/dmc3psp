#ifndef DMC3PSP_DOPPELGANGER_BOSS_H
#define DMC3PSP_DOPPELGANGER_BOSS_H

/* Mission 17's Doppelganger: CEm034 (em034.pac), Dante's shadow, in room 139 (Apparition Incarnate). He can only be
 * hurt where the light falls: the six crests on the walls (BROCKEN sets 0-5) open their shutters when struck, and
 * each lights its sixth of the arena; with all six open the middle is lit too. See docs/DOPPELGANGER_SPEC.md. */
int doppelganger_boss_init(void);
void doppelganger_boss_set_difficulty(int difficulty);
void doppelganger_boss_spawn(float x, float y, float z);
void doppelganger_boss_kill(void);
void doppelganger_boss_update(float frames, float px, float py, float pz, int attacks);
/* Dante's facing: he rises out of the shadow behind him. */
void doppelganger_boss_dante(float yaw);
/* Dante's blade: (x, y, z) his feet, facing yaw, reaching this far. Opens a crest it reaches. */
void doppelganger_boss_strike(float x, float y, float z, float yaw, float reach);
/* Returns 1 only on the transition to the dying state. */
int doppelganger_boss_hit(int kind, float from_x, float from_z, int mortal);
void doppelganger_boss_draw(void);
int doppelganger_boss_dying(void);
int doppelganger_boss_hidden(void);   /* sunk into a shadow */
float doppelganger_boss_health(void);
float doppelganger_boss_max_health(void);
void doppelganger_boss_position(float out[3]);
void doppelganger_boss_target_position(float out[3]);
void doppelganger_boss_push_out(float *x, float *z, float y, float r);
int doppelganger_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
void doppelganger_boss_trace(const char *path);
void doppelganger_boss_set_floor(int (*floor_fn)(float x, float y, float z, float *out));

#endif
