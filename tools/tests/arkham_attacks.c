#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/arkham_boss.c"
int testroom_frame;
int model_blend_off;
unsigned int enemy_ai_random(void){return 123456789u;}
void fx_hit_spark(const float p[3]){(void)p;}
void fx_explosion(const float p[3]){(void)p;}
int main(void){
 int i,p,heavy;float x,z,damage,mouth[3],held[3],yaw;
 assert(motion_init()>0 && arkham_boss_init() && motion_find("ah30")>=0);
 arkham_boss_spawn(0,0,0);
 ak.dante[2]=600;set_state(S_FRONT);ak.frame=44;ai_step(1);assert(!ak.landed);
 ak.frame=45;ai_step(1);
 assert(arkham_boss_attack_landed(&x,&z,&heavy,&damage) && damage==1200 && heavy && arkham_boss_attack_kind()==2);
 ai_step(1);assert(!ak.landed);
 ak.dante[0]=600*sinf(75*UNITS*65536/360);ak.dante[2]=600*cosf(75*UNITS*65536/360);
 set_state(S_LEFT);ak.frame=105;ai_step(1);
 assert(arkham_boss_attack_landed(&x,&z,&heavy,&damage) && damage==800 && arkham_boss_attack_kind()==2);
 set_state(S_LEFT2);ak.frame=45;ai_step(1);
 assert(arkham_boss_attack_landed(&x,&z,&heavy,&damage) && damage==800 && arkham_boss_attack_kind()==2);
 memset(ak.legions,0,sizeof ak.legions);ak.dante[0]=0;ak.dante[1]=0;ak.dante[2]=0;
 ak.legions[0].on=1;ak.legions[0].pos[0]=80;ak.legions[0].pos[1]=100;
 shells_update(0);assert(!ak.landed && ak.legions[0].on);
 ak.legions[0].pos[0]=79; shells_update(0);
 assert(arkham_boss_attack_landed(&x,&z,&heavy,&damage) && damage==300 && !heavy && arkham_boss_attack_kind()==0);
 for(p=0;p<3;++p){
  arkham_boss_spawn(0,0,0);ak.phase=p;set_state(S_GREEN);mouth_point(mouth);
  printf("Arkham mouth phase%d: %.1f %.1f %.1f\n",p,mouth[0],mouth[1],mouth[2]);
  ak.dante[0]=mouth[0]+arkham_hold_range[p]+1;ak.dante[1]=mouth[1];ak.dante[2]=mouth[2];
  ai_step(1);assert(!ak.holding);
  ak.dante[0]=mouth[0];ai_step(1);assert(ak.holding && arkham_boss_take_hold() && !arkham_boss_take_hold());
  for(i=0;i<61;++i)arkham_boss_update(1,mouth[0],mouth[1],mouth[2],1);
  assert(arkham_boss_take_drain()==0);
  arkham_boss_update(1,mouth[0],mouth[1],mouth[2],1);
  assert(arkham_boss_take_drain()==arkham_hold_drain[p]);
  assert(arkham_boss_held_position(held,&yaw) && isfinite(held[1]));
  arkham_boss_hit(HIT_LIGHT,0,600,1);
  assert(!ak.holding && ak.state==S_WIDE && arkham_boss_take_release() && !arkham_boss_take_release());
  ak.dante[0]=0;ak.dante[2]=600;ak.frame=20;ai_step(1);assert(!ak.landed);
 }
 puts("Arkham: native front/left records and events, Legion contact, hold ranges/grace/drain and harmless release passed");
 return 0;
}
