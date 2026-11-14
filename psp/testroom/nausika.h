#pragma once
int nausika_init(void);
void nausika_enter(int room);
float nausika_update(float frames,float x,float y,float z);
void nausika_draw(void);
void nausika_hit(float x,float y,float z,float yaw,float reach,int kind);
