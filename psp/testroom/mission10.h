#pragma once
void mission10_reset(void);
void mission10_enter(int room,int previous);
int mission10_hold(void);
void mission10_control(float x,float y,float z);
int mission10_clear_area(float x,float y,float z);
void mission10_update(float frames,int mask_used,int generator_used);
int mission10_floor(float x,float y,float z,float step,float *height);
int mission10_bridge(float x,float y,float z);
