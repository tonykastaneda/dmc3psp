#include "bishop.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static float half(void){return .5f;}
int main(void) {
 BishopBrain b={0};ChessPiece p={0};int i;float x,z;
 bishop_spawn(&b,24);b.action=2;
 for(i=0;i<20;++i)bishop_think(&b,&p,1,1000,0,500,half,0);
 assert(b.pos[1]==40 && b.step==1);
 for(i=0;i<20;++i)bishop_think(&b,&p,1,1000,0,500,half,0);
 assert(fabsf(hypotf(b.pos[0],b.pos[2])-200)<.01f);
 b.pos[0]=b.pos[1]=b.pos[2]=0;bishop_spawn(&b,24);b.action=3;
 bishop_think(&b,&p,1,20000,0,20000,half,1);
 for(i=0;i<4;++i)assert(b.orbs[i].stage==1 && b.orbs[i].clock==160);
 for(i=0;i<160;++i)bishop_think(&b,&p,1,20000,0,20000,half,1);
 for(i=0;i<4;++i)assert(b.orbs[i].stage==2 && b.orbs[i].clock==40);
 for(i=0;i<40;++i)bishop_think(&b,&p,1,20000,0,20000,half,1);
 assert(b.orbs[0].stage==3);
 bishop_force(&b,&p,3,half);bishop_think(&b,&p,1,20000,0,20000,half,1);
 {int count=0;for(i=0;i<BISHOP_ORBS;++i)count+=b.orbs[i].stage!=0;assert(count==8);}
 x=b.orbs[0].pos[0];z=b.orbs[0].pos[2];
 bishop_think(&b,&p,1,20000,0,20000,half,1);
 assert(fabsf(hypotf(b.orbs[0].pos[0]-x,b.orbs[0].pos[2]-z)-20)<.01f);
 x=b.orbs[0].pos[0]+40*sinf(b.orbs[0].yaw);z=b.orbs[0].pos[2]+40*cosf(b.orbs[0].yaw);
 bishop_think(&b,&p,1,x,0,z,half,1);assert(b.land && !b.orbs[0].stage);
 b.action=4;b.step=0;b.frame=61;bishop_think(&b,&p,1,0,0,0,half,0);assert(b.heal);
 b.health=1;assert(bishop_hit(&b,1,0,0,1));
 for(i=0;i<59;++i)bishop_think(&b,&p,1,0,0,0,half,0);assert(!b.remove);
 bishop_think(&b,&p,1,0,0,0,half,0);assert(b.remove);
 puts("Bishop: hover/diagonal speed, four orbit/launch clocks, contact, healing and death passed");
 return 0;
}
