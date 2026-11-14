#include <assert.h>
#include <stdio.h>
#include "../../psp/testroom/dullahan.c"
static float unit(void){return .5f;}
int main(void){
 DullahanBrain b={0};int hp;dullahan_spawn(&b,6);b.yaw=0;hp=b.health;
 assert(!dullahan_hit(&b,1,0,100,1));assert(b.health==hp);
 assert(!dullahan_hit(&b,1,0,-100,0));assert(b.health==hp);
 b.yaw=0;assert(!dullahan_hit(&b,1,0,-100,1));assert(b.health==hp-2);
 b.yaw=0;dullahan_hit(&b,1,0,-100,1);b.yaw=0;
 assert(dullahan_hit(&b,1,0,-100,1));
 dullahan_think(&b,151,0,0,0,unit,0);assert(b.remove);
 puts("Dullahan: front shield, rear damage, immortality and death passed");return 0;
}
