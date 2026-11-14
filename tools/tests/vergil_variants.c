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
 assert(seen==7);evaluate_poses();
 for(i=0;i<VERGIL_PART_COUNT;++i){int j;skin_part(i);for(j=0;j<vergil_parts[i].vertices;++j)assert(isfinite(vertices[i][j].x));}
 assert(vergil_boss_hit(HIT_HEAVY,0,100,1)==0);
 puts("Vergil: M7 isolation, M13 health/DT threshold, Beowulf chain and finite poses passed");return 0;
}
