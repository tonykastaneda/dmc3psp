#include <assert.h>
#include <stdio.h>
#include "../../psp/testroom/mission10.c"
void stage_set_group_pose(int t,float x,float z,float a,float y){(void)t;(void)x;(void)z;(void)a;(void)y;}
int main(void){float h;
 mission10_reset();mission10_enter(209,-1);assert(mission10_hold());mission10_enter(209,206);assert(!mission10_hold());
 mission10_enter(206,209);assert(!mission10_floor(13500,4000,7396,0,&h));
 mission10_update(600,1,0);assert(mission10_floor(13500,4000,7396,0,&h));assert(h>2575 && h<3140);
 mission10_enter(201,202);assert(!mission10_floor(2500,60,2500,0,&h));mission10_update(600,1,1);
 assert(mission10_floor(2500,60,2500,0,&h));assert(h==45);assert(!mission10_floor(2500,60,1000,0,&h));
 mission10_control(1255,45,3617);mission10_update(600,1,1);assert(!mission10_floor(2500,60,2500,0,&h));
 mission10_control(1255,45,3617);mission10_update(600,1,1);assert(mission10_floor(2500,60,2500,0,&h));
 assert(mission10_clear_area(2700,0,2300));assert(!mission10_clear_area(1692,0,2932));
 mission10_reset();assert(!mission10_floor(2500,60,2500,0,&h));puts("Mission 10: item-powered decks, reset, conditional cavern and native clear area passed");return 0;
}
