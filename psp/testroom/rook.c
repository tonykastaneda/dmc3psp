/* EST38 CEm017,436220; movement436850,beams436c70,summon437040,
 * beam constructor440d70/update440ff0; castle408dd0. Native rank0. */
#include "rook.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
const char *const rook_motion_names[ROOK_M_COUNT]={"cr00","cr00","cr01"};
void rook_force(RookBrain *b,ChessPiece *p,int action,float (*unit)(void)) {
 b->action=action;b->step=0;b->timer=0;b->restart=1;b->cast=0;
 b->want=action==4?ROOK_M_SPECIAL:action==3?ROOK_M_ATTACK:ROOK_M_IDLE;
 p->waiting=0;p->busy=action!=0;p->swap_blocked=action==11;
 if(!action){p->command=0;p->waited=0;b->wait=chess_wait(38,0,0,unit());}
}
void rook_spawn(RookBrain *b,int health,int board,float (*unit)(void)) {
 int i;b->health=health;b->board=board;b->flags=board?3:0;b->summon_turns=10+(int)(unit()*5);
 b->alternate_wait=0;b->action=b->step=b->want=0;b->restart=1;b->done=b->remove=b->land=0;
 b->beam_event=b->summon=b->castle=b->cast=b->blocked=0;b->timer=0;b->wait=chess_wait(38,0,0,unit());
 b->vx=b->vy=b->vz=0;for(i=0;i<4;++i){b->beam_age[i]=-1;b->beam_hit[i]=0;}
}
int rook_beam_hits(const float from[3],float yaw,float x,float y,float z) {
 float sx=sinf(yaw),sz=cosf(yaw),dx=x-from[0],dz=z-from[2],along=dx*sx+dz*sz;
 float near=fmaxf(y+20,fminf(y+160,from[1])),dy=from[1]-near;
 along=fmaxf(0,fminf(1500,along));dx-=along*sx;dz-=along*sz;
 return dx*dx+dy*dy+dz*dz<70*70;
}
static int aligned(RookBrain *b,float x,float z) {
 return !b->blocked && ((fabsf(x-b->pos[0])<60 && fabsf(z-b->pos[2])<1500) ||
                       (fabsf(z-b->pos[2])<60 && fabsf(x-b->pos[0])<1500));
}
void rook_think(RookBrain *b,ChessPiece *p,float f,float x,float y,float z,float (*unit)(void),int attacks) {
 int i;b->land=b->beam_event=b->summon=b->castle=b->cast=0;
 if(b->health<=0){b->timer+=f;if(b->timer>=60)b->remove=1;return;}
 if(p->kill){p->kill=0;b->health=0;b->timer=0;return;}
 b->alternate_wait-=f;p->alternate_ready=p->swap_ready=b->alternate_wait<=0;
 for(i=0;i<4;++i)if(b->beam_age[i]>=0){b->beam_age[i]+=f;
  if(b->beam_age[i]>=180){b->beam_age[i]=-1;continue;}
  if(attacks && !b->beam_hit[i] && b->beam_age[i]>=90 && b->beam_age[i]<=120 &&
     rook_beam_hits(b->beam_pos[i],b->yaw+(-180+90*i)*PI/180,x,y,z)){b->beam_hit[i]=1;b->land=1;}}
 if(p->command==30 && b->action!=11){p->command=0;b->alternate_wait=1800+500*unit();rook_force(b,p,11,unit);b->castle=1;}
 b->timer+=f;
 switch(b->action){
 case 0:
  b->timer+=f*(chess_wait_step(hypotf(x-b->pos[0],z-b->pos[2]),0)-1);
  if(b->timer>b->wait){p->waiting=1;p->waited+=f;
   if(p->command>=10){int alternate=p->command==15;p->command=0;
    if(alternate){b->alternate_wait=1800+500*unit();rook_force(b,p,4,unit);}
    else if(b->count<(b->board?14:8) && (b->flags&1) && --b->summon_turns<=0){
     b->summon_turns=10+(int)(unit()*5);rook_force(b,p,7,unit);}
    else rook_force(b,p,2,unit);}}
  break;
 case 2:
  if(!b->step){b->pos[1]+=2*f;if(b->timer>=20){float a;
   a=unit()<.5f?atan2f(x-b->pos[0],0):atan2f(0,z-b->pos[2]);
   b->vx=10*sinf(a);b->vz=10*cosf(a);b->wait=chess_wait(38,2,0,unit());b->timer=0;b->step=1;}}
  else if(b->step==1){b->pos[0]+=b->vx*f;b->pos[2]+=b->vz*f;
   if(aligned(b,x,z)||b->timer>=b->wait){b->step=2;b->vy=0;}}
  else {b->vy-=3.2f*f;b->pos[1]+=b->vy*f;if(b->pos[1]<=b->floor_y){b->pos[1]=b->floor_y;rook_force(b,p,aligned(b,x,z)?3:0,unit);}}
  break;
 case 3:
  if(!b->step && b->frame>1 && b->frame<3){for(i=0;i<4;++i){b->beam_age[i]=0;b->beam_hit[i]=0;}b->beam_event=1;b->step=1;}
  if(b->done)rook_force(b,p,0,unit);
  break;
 case 4:if(b->done)rook_force(b,p,0,unit);break;
 case 7:
  if(!b->step){float r=unit()*100;b->summon=r<40?35:r<70?36:r<90?37:38;b->cast=1;b->step=1;}
  else if(b->done)rook_force(b,p,0,unit);
  break;
 case 11:
  if(!b->step){for(i=0;i<3;++i)b->pos[i]+=(p->destination[i]-b->pos[i])*.09f;
   if(b->timer>=30){memcpy(b->pos,p->destination,sizeof b->pos);b->step=1;b->timer=0;b->vy=0;b->castle=2;}}
  else {b->vy-=3.2f*f;b->pos[1]=fmaxf(b->floor_y,b->pos[1]+b->vy*f);
   if(b->timer>=180){rook_force(b,p,3,unit);b->castle=3;}}
  break;
 }
}
