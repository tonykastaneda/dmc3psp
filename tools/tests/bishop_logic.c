#include <assert.h>
#include <stdio.h>
#include "../../psp/testroom/bishop.c"
static float unit(void){return .5f;}
int main(void){BishopBrain b={0};bishop_spawn(&b,6);bishop_think(&b,151,500,0,0,unit,1);assert(b.cast==1);bishop_think(&b,61,500,0,0,unit,1);assert(b.land==2);
 bishop_spawn(&b,6);bishop_think(&b,151,500,0,0,unit,1);bishop_think(&b,61,900,0,0,unit,1);assert(!b.land);
 assert(!bishop_hit(&b,1,0,0,0)&&b.health==6);bishop_hit(&b,1,0,0,1);bishop_hit(&b,1,0,0,1);assert(bishop_hit(&b,1,0,0,1));bishop_think(&b,151,0,0,0,unit,1);assert(b.remove);
 puts("Bishop: aimed spell, dodge, immortality and death passed");return 0;}
