/* CEm017 EST37:435510, movement435890, four shells435cb0, healing435e90.
 * CEm017Shl00 init43fc60/update43ff70. Native frames and centimetres. */
#include "bishop.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
const char *const bishop_motion_names[BISHOP_M_COUNT]={"bi00","bi00","bi01","bi00"};
int bishop_loops(int motion){(void)motion;return 0;}
static void state(BishopBrain *b,ChessPiece *p,int action,float (*unit)(void)) {
 b->action=action;b->step=0;b->timer=0;b->restart=1;b->cast=0;
 p->waiting=0;p->busy=action!=0;
 b->want=action==4?BISHOP_M_HEAL:action==3?BISHOP_M_CAST:BISHOP_M_IDLE;
 if(!action){p->command=0;p->waited=0;b->wait=chess_wait(37,0,0,unit());}
}
void bishop_spawn(BishopBrain *b,int health) {
 b->health=b->max_health=b->previous_hp=health;b->timer=0;b->action=0;b->want=0;b->restart=1;
 b->remove=b->land=b->cast=b->heal=b->step=b->blocked=0;b->wait=0;
 b->vx=b->vy=b->vz=0;memset(b->orbs,0,sizeof b->orbs);
}
static void projections(BishopBrain *b,float x,float z,float out[4]) {
 float difference=b->pos[2]-b->pos[0],target_difference=z-x;
 out[0]=(x+z-difference)*.5f;out[1]=out[0]+difference;
 out[2]=(b->pos[0]+b->pos[2]-target_difference)*.5f;out[3]=out[2]+target_difference;
}
static int aligned(BishopBrain *b,float x,float z) {
 float v[4];projections(b,x,z,v);
 return !b->blocked && (hypotf(v[0]-x,v[1]-z)<60 || hypotf(v[2]-x,v[3]-z)<60);
}
static void shells(BishopBrain *b,float f,float px,float py,float pz,int attacks) {
 int i;
 for(i=0;i<BISHOP_ORBS;++i){BishopOrb *o=&b->orbs[i];float x,y,z,near;
  o->event=0;if(!o->stage)continue;o->age+=f;
  if(o->age>=900){o->stage=0;continue;}
  if(o->stage==1){float a=o->yaw+o->clock*4.5f*PI/180;
   o->pos[0]=b->pos[0]+100*sinf(a);o->pos[1]=b->pos[1]+100;o->pos[2]=b->pos[2]+100*cosf(a);
   o->clock-=f;if(o->clock<=0){o->stage=2;o->clock=40;o->event=2;}
  } else if(o->stage==2){o->clock-=f;if(o->clock<=0){o->stage=3;o->event=3;}}
  else {o->pos[0]+=20*sinf(o->yaw)*f;o->pos[2]+=20*cosf(o->yaw)*f;}
  if(o->stage==1 && o->clock>=75)continue;
  x=o->pos[0]+20*sinf(o->yaw)-px;z=o->pos[2]+20*cosf(o->yaw)-pz;
  near=fmaxf(py+20,fminf(py+160,o->pos[1]));y=o->pos[1]-near;
  if(attacks && x*x+y*y+z*z<80*80){b->land=1;o->stage=0;}
 }
}
void bishop_think(BishopBrain *b,ChessPiece *p,float f,float x,float y,float z,float (*unit)(void),int attacks) {
 int i;b->land=b->cast=b->heal=0;
 if(b->health<=0){b->timer+=f;if(b->timer>=60)b->remove=1;return;}
 if(p->kill){p->kill=0;b->health=0;b->timer=0;return;}
 shells(b,f,x,y,z,attacks);b->timer+=f;
 switch(b->action){
 case 0:
  if(b->wait==0)b->wait=chess_wait(37,0,0,unit());
  b->timer+=f*(chess_wait_step(hypotf(x-b->pos[0],z-b->pos[2]),0)-1);
  if(b->timer>b->wait){p->waiting=1;p->waited+=f;
   if(p->command>=10){p->command=0;state(b,p,unit()<.05f?4:2,unit);}}
  break;
 case 2:
  if(b->step==0){b->pos[1]+=2*f;if(b->timer>=20){float v[4],a;int j=unit()<.5f?0:2;
    projections(b,x,z,v);a=atan2f(v[j]-b->pos[0],v[j+1]-b->pos[2]);
    a=floorf(a/(PI/4)+.5f)*(PI/4);b->vx=10*sinf(a);b->vz=10*cosf(a);
    b->wait=chess_wait(37,2,0,unit());b->timer=0;b->step=1;}}
  else if(b->step==1){b->pos[0]+=b->vx*f;b->pos[2]+=b->vz*f;
    if(aligned(b,x,z)||b->timer>=b->wait){b->step=2;b->vy=0;}}
  else {b->vy-=3.2f*f;b->pos[1]+=b->vy*f;if(b->pos[1]<=b->floor_y){b->pos[1]=b->floor_y;state(b,p,aligned(b,x,z)?3:0,unit);}}
  break;
 case 3:
  if(b->step==0){int n=0;for(i=0;i<BISHOP_ORBS && n<4;++i){BishopOrb *o=&b->orbs[i];if(o->stage)continue;
    o->stage=1;o->event=1;o->age=0;o->clock=160;o->yaw=(-135+90*n++)*PI/180;
    o->pos[0]=b->pos[0]+100*sinf(o->yaw);o->pos[1]=b->pos[1]+100;o->pos[2]=b->pos[2]+100*cosf(o->yaw);}
   b->step=1;b->timer=0;b->cast=1;}
  if(b->step==1 && b->timer>=80){b->step=2;b->timer=0;b->cast=2;}
  else if(b->step==2 && b->timer>=20){b->step=3;b->timer=0;b->cast=3;}
  else if(b->step==3 && b->timer>=220)state(b,p,0,unit);
  break;
 case 4:
  if(b->step==2){if(b->timer>=10)state(b,p,0,unit);break;}
  if(b->frame>60 && b->step==0){b->step=1;b->cast=4;}
  if(b->step==1)b->heal=1;
  if(b->health<b->previous_hp || b->done){b->step=2;b->timer=0;b->heal=0;b->cast=5;}
  break;
 }
 b->previous_hp=b->health;
}
int bishop_hit(BishopBrain *b,int heavy,float x,float z,int mortal) {
 (void)x;(void)z;if(b->health<=0)return 0;if(mortal)b->health-=heavy?2:1;
 if(b->health<=0){b->want=BISHOP_M_DEATH;b->restart=1;b->timer=0;return 1;}return 0;
}

void bishop_force(BishopBrain *b,ChessPiece *p,int action,float (*unit)(void)){if(action==0 || action==2 || action==3 || action==4)state(b,p,action,unit);}
