#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/doppelganger_boss.c"
int testroom_frame;
unsigned int enemy_ai_random(void){return 123456789u;}
void fx_hit_spark(const float p[3]){(void)p;}
void fx_explosion(const float p[3]){(void)p;}
int main(void){
 int i;
 assert(motion_init()>0 && doppelganger_boss_init());
 for(i=0;i<7;++i)assert(region(doppel_regions[i][0],doppel_regions[i][1])==i);
 assert(region(3050.5f,2500)==6); /* Native distance truncates before <=550. */
 assert(region(3051,2500)==-1);  /* The narrow gap is not an angular sector. */
 assert(region(2500,884.5f)==0 && region(2500,884)==-1);
 assert(region(3500,2500)==2 && region(1450,2500)==5); /* Higher index wins overlaps. */
 assert(region(10000,10000)==-1);
 set_state(S_THINK);assert(region_lit(-1));
 set_state(S_ALL_LIT);assert(!region_lit(-1));
 doppelganger_boss_spawn(3500,5,2500);
 db.lit[2]=1;db.lit[1]=0;set_state(S_THINK);
 assert(in_light());
 {float hp=db.hp;doppelganger_boss_hit(HIT_LIGHT,3500,2600,1);assert(db.hp<hp);}
 puts("Doppelganger: seven native discs, truncated boundaries, reverse overlap priority, gaps and light damage passed");
 return 0;
}
