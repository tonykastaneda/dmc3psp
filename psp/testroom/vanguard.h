#ifndef DMC3_VANGUARD_H
#define DMC3_VANGUARD_H

int vanguard_init(void);
void vanguard_set_difficulty(int difficulty);
void vanguard_spawn(float x, float y, float z);
/* Returns an attack landing. alive becomes 0 after the death animation. */
int vanguard_update(float frames, float px, float py, float pz, int attacks, float pos[3], int *alive);
/* Returns 1 only on the transition to death. Hidden warps reject hits. */
int vanguard_hit(int kind, int mortal);
void vanguard_kill(void);
void vanguard_draw(void);
int vanguard_hidden(void);
int vanguard_dying(void);
float vanguard_health(void);
float vanguard_max_health(void);
void vanguard_trace(const char *path);
void vanguard_render_test(int no_texture, int no_lighting);
void vanguard_note(const char *event, float value);

#endif
