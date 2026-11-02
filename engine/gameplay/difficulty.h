#ifndef DMC3PSP_GAMEPLAY_DIFFICULTY_H
#define DMC3PSP_GAMEPLAY_DIFFICULTY_H

/* PSP adaptations of the decomp project's difficulty/gauge routines. */
void dmc3_difficulty_gauge_init(float *state);
void dmc3_difficulty_clear(void *state);
void dmc3_difficulty_pair(void *destination, const void *source);
void dmc3_difficulty_clamp(void *state);
float dmc3_difficulty_read(const void *state);
int dmc3_gameplay_self_test(void);

#endif
