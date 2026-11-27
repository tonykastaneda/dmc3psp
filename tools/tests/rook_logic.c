#include "rook.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static float half(void){return .5f;}
int main(void) {
 RookBrain b={0};ChessPiece p={0};int i;
 rook_spawn(&b,43,1,half);rook_force(&b,&p,2,half);
 for(i=0;i<20;++i)rook_think(&b,&p,1,1000,0,500,half,0);
 assert(b.pos[1]==40 && b.step==1);
 for(i=0;i<20;++i)rook_think(&b,&p,1,1000,0,500,half,0);
 assert(fabsf(b.pos[0])<.01f && fabsf(b.pos[2]-200)<.01f);
 rook_spawn(&b,43,1,half);rook_force(&b,&p,3,half);b.frame=2;
 rook_think(&b,&p,1,20000,0,20000,half,1);assert(b.beam_event);
 for(i=0;i<4;++i)assert(b.beam_age[i]==0);
 for(i=0;i<89;++i)rook_think(&b,&p,1,20000,0,20000,half,1);
 assert(!b.land && b.beam_age[0]==89);
 rook_think(&b,&p,1,0,0,-1000,half,1);assert(b.land && b.beam_hit[0]);
 rook_think(&b,&p,1,0,0,-1000,half,1);assert(!b.land);
 assert(rook_beam_hits((float[3]){0,100,0},0,69,0,1000));
 assert(!rook_beam_hits((float[3]){0,100,0},0,71,0,1000));
 rook_spawn(&b,43,1,half);b.wait=0;b.summon_turns=1;b.count=14;p.command=10;
 rook_think(&b,&p,1,1000,0,500,half,0);assert(b.action==2 && b.summon_turns==1);
 rook_force(&b,&p,0,half);b.wait=0;b.count=13;p.command=10;
 rook_think(&b,&p,1,1000,0,500,half,0);assert(b.action==7);
 rook_think(&b,&p,1,1000,0,500,half,0);assert(b.summon==36);
 rook_force(&b,&p,0,half);p.destination[0]=500;p.destination[1]=0;p.destination[2]=200;p.command=30;
 rook_think(&b,&p,1,1000,0,500,half,0);assert(b.action==11 && p.swap_blocked);
 for(i=0;i<29;++i)rook_think(&b,&p,1,1000,0,500,half,0);
 assert(b.pos[0]==500 && b.pos[2]==200);
 for(i=0;i<180;++i)rook_think(&b,&p,1,1000,0,500,half,0);assert(b.action==3);
 b.health=0;b.timer=0;for(i=0;i<59;++i)rook_think(&b,&p,1,0,0,0,half,0);assert(!b.remove);
 rook_think(&b,&p,1,0,0,0,half,0);assert(b.remove);
 puts("Rook: axial slide, beam window/radius, summon cap/turns, castle and death passed");
 return 0;
}
