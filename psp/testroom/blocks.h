#ifndef DMC3PSP_TESTROOM_BLOCKS_H
#define DMC3PSP_TESTROOM_BLOCKS_H

/* Secret Mission 12's moving blocks in st611 (blocks.c). */
void blocks_enter(int room);   /* runs in 611 only */
int blocks_active(void);
/* Each frame: moves them, and Dante with the one he's standing on (his feet, on the ground last frame). */
void blocks_update(float dt, float *x, float *y, float *z, int on_ground);
/* Their tops, as a floor: the highest at or below y + step (+ 60 cm for the rising ones). */
int blocks_floor(float x, float y, float z, float step, float *out);
void blocks_status(char *out, int size);

#endif
