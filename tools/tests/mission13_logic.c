#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/mission13.c"
void stage_hide_group(int n){(void)n;}
int enemy_alive_count(void){return 0;}
void enemy_spawn_height(float y){(void)y;}
int enemy_kind_for_class(int c){return c;}
int enemy_add_kind(int k,float x,float z){(void)k;(void)x;(void)z;return 1;}
void fx_hit_spark(const float*p){(void)p;}
void fx_tracer(const float*a,const float*b,unsigned int c,float w){(void)a;(void)b;(void)c;(void)w;}
static void strike(int n){float p[3];node_pos(n,p);mission13_hit(p[0],p[1],p[2]+200,3.14159265f,230,HIT_LIGHT);}
int main(void){int i;
 mission13_reset(1);mission13_enter(233,241);assert(!mission13_open());
 strike(8);assert(broken&(1u<<8));
 for(i=0;i<3;++i)mission13_hit(2102,150,1900,3.14159265f,230,HIT_LIGHT);
 assert(broken==0);
 strike(8);strike(11);strike(3);strike(14);assert(mission13_open());assert((broken&18696)==18696);
 mission13_enter(241,233);assert(mission13_returning() && mission13_busy());
 for(i=0;i<1000;++i)mission13_update(2);assert(!mission13_busy());
 mission13_enter(233,241);assert(mission13_open());mission13_reset(0);assert(!mission13_open());
 puts("M13: native grid solution, switch reset, persistence and return encounter passed");return 0;
}
