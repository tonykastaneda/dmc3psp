/* Damned Bishop: native em017 chess model/death motion. The stationary
 * aimed spell cycle is a port approximation, not a decoded CEm019 AI. */
#include "bishop.h"
#include <math.h>
#include <string.h>
const char *const bishop_motion_names[BISHOP_M_COUNT]={"bishop-rest","bishop-rest","bi01"};
int bishop_loops(int motion){return motion!=BISHOP_M_DEATH;}
void bishop_spawn(BishopBrain *b,int health){b->health=health;b->timer=0;b->action=0;b->want=0;b->restart=0;b->remove=0;b->land=0;b->cast=0;}
void bishop_think(BishopBrain *b,float f,float x,float y,float z,float (*unit)(void),int attacks){
 float dx=x-b->pos[0],dz=z-b->pos[2];(void)unit;b->land=0;b->cast=0;b->timer+=f;
 if(b->health<=0){if(b->timer>150)b->remove=1;return;}
 b->yaw=atan2f(dx,dz);
 if(!b->action && b->timer>150 && dx*dx+dz*dz<1400*1400){b->action=1;b->timer=0;b->target[0]=x;b->target[1]=y;b->target[2]=z;b->step=0;b->cast=1;}
 if(b->action && b->timer>=60 && !b->step){float a=x-b->target[0],c=z-b->target[2];b->step=1;if(attacks && a*a+c*c<180*180 && fabsf(y-b->target[1])<220)b->land=2;b->cast=2;}
 if(b->action && b->timer>100){b->action=0;b->timer=0;}
}
int bishop_hit(BishopBrain *b,int heavy,float x,float z,int mortal){(void)x;(void)z;if(b->health<=0)return 0;if(mortal)b->health-=heavy?2:1;if(b->health<=0){b->want=BISHOP_M_DEATH;b->restart=1;b->timer=0;return 1;}return 0;}
