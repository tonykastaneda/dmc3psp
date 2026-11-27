#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/geryon_boss.c"
int testroom_frame;
unsigned int enemy_ai_random(void){return 123456789u;}
void fx_hit_spark(const float p[3]){(void)p;}
int stage_loaded(void){return 0;}
int stage_collide(float *x,float *z,float y,float r){(void)x;(void)z;(void)y;(void)r;return 0;}
int main(void){
 int side,i;
 assert(motion_init()>0 && geryon_boss_init());
 for(side=0;side<2;++side){
  geryon_boss_start_on_ground(1);geryon_boss_spawn(0,0,0);
  gb.side=side;gb.state=S_GALLOP;gb.laps=40;gb.window_time=0;gb.gallop_clip=0;
  ai_step(28);assert(gb.laps==40);
  ai_step(1);assert(gb.laps==39);
  for(i=1;i<40;++i)ai_step(29);
  assert(gb.laps==0 && gb.state==S_TURN && gb.action==(side?A_TURN_R:A_TURN_L));
  gb.state=S_GALLOP;gb.laps=40;gb.window_time=0;gb.gallop_clip=0;gb.high_history=1;
  for(i=0;i<12;++i)ai_step(29);
  assert(gb.laps==4 && gb.state==S_GALLOP);
  for(i=0;i<4;++i)ai_step(29);
  assert(gb.laps==0 && gb.state==S_TURN);
 }
 geryon_boss_spawn(0,0,0);
 geryon_boss_update(1,gb.pos[0],gb.pos[1]+350,gb.pos[2],0);
 assert(gb.high_history==1);
 for(i=0;i<7;++i)geryon_boss_update(1,10000,0,10000,0);
 assert(gb.high_history==128);
 geryon_boss_update(1,10000,0,10000,0);assert(!gb.high_history);
 puts("Geryon: 29-frame windows, 40-event exit, high-contact decrement/history and both turns passed");
 return 0;
}
