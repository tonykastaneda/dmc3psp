#ifndef DMC3PSP_TESTROOM_SPHERES_H
#define DMC3PSP_TESTROOM_SPHERES_H

/* Mission 16's hanging spheres (spheres.c): 146's shelled ball, 109's pair. */
void spheres_reset(void);              /* a new mission: unsolved */
void spheres_enter(int room);          /* -1: none */
void spheres_update(float dt);
void spheres_hit(float x, float y, float z, float yaw, float reach, int heavy);   /* a sword swing of Dante's */
int spheres_solved(int room);          /* 146: the shell broken; 109: the two knocked together */
void spheres_draw(void);
void spheres_status(char *out, int size);

#endif
