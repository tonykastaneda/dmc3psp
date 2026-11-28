#ifndef DMC3PSP_VERGIL_BOSS_H
#define DMC3PSP_VERGIL_BOSS_H

/* Mission 7's Vergil: CEm035, variant 0 (Yamato only). One enemy-manager slot owns him, his coat,
 * Yamato and its scabbard. See docs/VERGIL_SPEC.md. */
int vergil_boss_init(void);
void vergil_boss_set_difficulty(int difficulty);
/* 0 retains M7; 1 selects M13 Beowulf / Devil Trigger; 2 selects M20 Force Edge. Set before spawn. */
void vergil_boss_set_variant(int variant);
int vergil_boss_variant(void);
int vergil_boss_devil(void);
/* (x, y, z): his EST place, feet on the floor. */
void vergil_boss_spawn(float x, float y, float z);
void vergil_boss_kill(void);
/* Dante's command-table action (0 = none) and facing, for the guard trigger and the teleports. */
void vergil_boss_dante(int action, float yaw);
void vergil_boss_update(float frames, float px, float py, float pz, int attacks);
/* Returns 1 only on the transition to the dying state. */
int vergil_boss_hit(int kind, float from_x, float from_z, int mortal);
void vergil_boss_draw(void);
/* Mission 19's partner: his body posed by a player Pose's model-space bone transforms (24 bones), at (x, y, z), yaw. */
void vergil_boss_draw_partner(const float rot[][3][3], const float pos[][3], float x, float y, float z, float yaw);
/* Yamato in his hand (he's attacking) or in its scabbard; and a fresh start for his coat's chains. */
void vergil_boss_partner_drawn(int drawn);
void vergil_boss_partner_reset(void);
int vergil_boss_dying(void);
int vergil_boss_hidden(void);   /* mid-teleport: not there to be hit */
float vergil_boss_health(void);
float vergil_boss_max_health(void);
void vergil_boss_position(float out[3]);          /* feet */
void vergil_boss_target_position(float out[3]);   /* chest, for lock-on */
void vergil_boss_push_out(float *x, float *z, float y, float r);
/* One damage event, cleared by reading it. Damage is the original record's value. */
int vergil_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage);
int vergil_boss_attack_kind(void);   /* the last landed attack's record kind, -1 if none */
/* Tests: exercise a native AI state in the actual encounter. */
int vergil_boss_test_state(int state);
void vergil_boss_trace(const char *path);
/* Tests: a line in the same log (Mission 7's route checks). */
void vergil_boss_note(const char *event, float a, float b, float c, float d);
/* The floor under (x, z) near height y; set by enemy.c so he walks the room's floor. */
void vergil_boss_set_floor(int (*floor_fn)(float x, float y, float z, float *out));

void vergil_boss_data_release(void);   /* frees its model's buffers unless it's active (made again at its next spawn) */
int vergil_boss_data_load(void);   /* makes its model's buffers now (a room load, ahead of its spawn) */
#endif
