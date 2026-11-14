#ifndef DMC3PSP_MISSION8_H
#define DMC3PSP_MISSION8_H
void mission8_reset(void);
void mission8_enter(int number);
void mission8_hit(float x,float y,float z,float yaw,float reach,int kind);
int mission8_stomach_open(void);
/* Raw HP lost to authored material-13 floor triangles; elapsed frames are 60 Hz. */
float mission8_acid(float frames,float x,float y,float z,int grounded);
void mission8_draw(void);
#endif
