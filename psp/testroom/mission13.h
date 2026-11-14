#pragma once
void mission13_reset(int difficulty);
void mission13_enter(int number,int previous);
void mission13_update(float frames);
void mission13_hit(float x,float y,float z,float yaw,float reach,int kind);
void mission13_push_out(float *x,float *z,float y,float radius);
void mission13_draw(void);
int mission13_open(void);
int mission13_busy(void);
int mission13_returning(void);
unsigned int mission13_broken(void);
