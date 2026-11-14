#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/vergil_boss.c"
int testroom_frame;
unsigned int enemy_ai_random(void){return 123456789u;}
void fx_hit_spark(const float p[3]){(void)p;}
void fx_guard_flash(const float p[3]){(void)p;}
int main(void){int i,seen=0;
 assert(motion_init()>0);assert(vergil_boss_init());
 vergil_boss_set_variant(0);vergil_boss_spawn(0,0,0);vb.hp=5000;
 for(i=0;i<1000;++i)vergil_boss_update(1,0,0,400,1);
 assert(!vergil_boss_devil() && !vb.dt_unlocked && vergil_boss_variant()==0);
 vergil_boss_set_variant(1);vergil_boss_spawn(0,0,0);assert(vergil_boss_health()==8000);
 vb.hp=6050;set_state(S_FLINCH);vergil_boss_hit(HIT_HEAVY,0,100,1);assert(vb.state==S_DT);
 for(i=0;i<900;++i){vergil_boss_update(1,0,0,700,1);if(vb.dt)seen=1;}
 assert(seen && vb.dt_unlocked);
 vb.dt=0;vb.dt_cooldown=10000;set_state(S_BEOWULF);seen=0;
 for(i=0;i<900;++i){vergil_boss_update(1,0,0,300,1);if(vb.action==0x2b)seen|=1;if(vb.action==0x2c)seen|=2;if(vb.action==0x2d)seen|=4;}
 assert(seen==7);
 vb.pos[0]=8000;vb.pos[2]=8000;vergil_boss_update(1,2500,100,2500,0);
 assert(hypotf(vb.pos[0]-2500,vb.pos[2]-2500)<=1101);
 evaluate_poses();
 for(i=0;i<VERGIL_PART_COUNT;++i){int j;skin_part(i);for(j=0;j<vergil_parts[i].vertices;++j)assert(isfinite(vertices[i][j].x));}
 assert(vergil_boss_hit(HIT_HEAVY,0,100,1)==0);
 vergil_boss_set_variant(2);vergil_boss_spawn(2500,-208,2400);
 assert(vergil_boss_variant()==2 && vergil_boss_health()==10000 && vergil_boss_max_health()==10000);
 assert(vb.centre[0]==2500 && vb.centre[2]==3750);
 vb.hp=7400;vergil_boss_update(1,2500,-208,2800,1);assert(vb.state!=S_DT);
 vb.hp=3902;vergil_boss_hit(HIT_PISTOL,2500,2800,1);assert(!vb.m20_low);
 vergil_boss_hit(HIT_PISTOL,2500,2800,1);assert(vb.m20_low);
 set_state(S_THINK);vergil_boss_update(1,2500,-208,2800,1);assert(vb.state==S_DT);
 seen=0;for(i=0;i<600;++i){vergil_boss_update(1,2500,-208,2800,1);if(vb.dt)seen=1;}
 assert(seen);
 vb.dt=1;vb.dt_left=1;set_state(S_FORCE_A);vergil_boss_update(2,2500,-208,2800,0);assert(vb.dt);
 set_state(S_THINK);assert(!vb.dt && vb.dt_cooldown==2700);
 vergil_boss_update(2,2500,-208,2800,1);assert(vb.state!=S_DT);
 vb.dt_cooldown=0;set_state(S_THINK);vergil_boss_update(1,2500,-208,2800,1);assert(vb.state==S_DT);
 vb.dt=0;vb.dt_unlocked=1;vb.dt_cooldown=10000;
 set_state(S_FORCE_A);seen=0;
 for(i=0;i<600;++i){vergil_boss_update(1,2500,-208,2800,1);if(vb.action==0x65)seen=1;}
 assert(seen);set_state(S_JUDGMENT);vb.landed=0;
 for(i=0;i<400 && !vb.landed;++i)vergil_boss_update(1,2500,-208,2800,1);
 assert(vb.landed && vb.landed_damage==1000);
 vb.dt=1;evaluate_poses();
 for(i=PART_M20_DT_BODY;i<VERGIL_PART_COUNT;++i){int j;skin_part(i);for(j=0;j<vergil_parts[i].vertices;++j)assert(isfinite(vertices[i][j].x));}
 vb.dt=1;vb.dt_left=100000;vb.dt_cooldown=100000;vb.m20_low=0;
 set_state(S_STORM);seen=0;
 {int last=0;for(i=0;i<2400;++i){vergil_boss_update(1,2500,-208,2800,1);if(vb.action==0x70 && last!=0x70)++seen;last=vb.action;if(vb.state==S_THINK)break;}}
 assert(vb.state==S_THINK && seen==4 && fabsf(vb.pos[1]-vb.floor_y)<1 && !vb.hidden);
 set_state(S_ROUND_TRIP);vb.sword=0;seen=0;
 for(i=0;i<1200;++i){vergil_boss_update(1,2500,-208,2800,1);if(vb.sword)seen=1;if(seen && !vb.sword)break;}
 assert(seen && !vb.sword);
 set_state(S_CUT_RANDOM);seen=0;
 for(i=0;i<2400;++i){vergil_boss_update(1,2500,-208,2800,1);if(vb.action==0x6e)seen=1;if(vb.state==S_THINK)break;}
 assert(seen && vb.state==S_THINK);
 puts("Vergil: M7 isolation, M13 health/DT threshold, Beowulf chain, finite poses, M20 HP/DT/Force Edge/Judgment Cut, four dives, Round Trip and area-cut recovery passed");return 0;
}
