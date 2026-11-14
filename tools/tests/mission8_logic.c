#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t alignment,size_t bytes) {(void)alignment;return malloc(bytes);}
#define memalign test_memalign
#include "../../psp/testroom/mission8.c"
static int hidden;
void stage_hide_group(int group) {assert(group==128);++hidden;}
void fx_hit_spark(const float p[3]) {(void)p;}
void fx_explosion(const float p[3]) {(void)p;}
int main(void) {
    int i,j;
    mission8_reset();mission8_enter(300);
    assert(!mission8_stomach_open());
    assert(mission8_acid(1,469,-131,3061,1)==25);
    assert(mission8_acid(6,469,-131,3061,1)==0);
    assert(mission8_acid(1,469,-131,3061,1)==25);
    assert(mission8_acid(7,469,-131,3061,0)==0);
    assert(mission8_acid(7,1700,705,4715,1)==0);
    for(i=0;i<5;++i) {
        const M8Eye *e=&m8_eyes[i];
        for(j=0;j<3;++j)mission8_hit(e->pos[0],e->pos[1],e->pos[2]-100,0,200,HIT_LIGHT);
        assert(eye_hp[i]==0);
        if(i<4)assert(!mission8_stomach_open());
    }
    assert(mission8_stomach_open() && hidden==1);
    mission8_enter(301);mission8_enter(300);
    assert(mission8_stomach_open() && hidden==2);
    mission8_reset();assert(!mission8_stomach_open());
    puts("Mission 8: authored acid triangles, jump avoidance, five seals, persistent unlock and reset passed");
    return 0;
}
