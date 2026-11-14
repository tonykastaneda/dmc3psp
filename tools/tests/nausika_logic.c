#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t a,size_t n){(void)a;return malloc(n);}
#define memalign test_memalign
#include "../../psp/testroom/nausika.c"
int main(void) {
    int i;float damage=0;
    assert(motion_init()>0);assert(nausika_init());
    nausika_enter(302);assert(!chase.enabled);
    nausika_enter(303);assert(chase.enabled && chase.hp==3000);
    assert(NAUSIKA_PATH_COUNT==44);
    nausika_update(1,10000,0,10000);assert(!chase.started);
    for(i=0;i<120;++i)damage+=nausika_update(1,chase.pos[0]+100,0,chase.pos[2]+100);
    assert(chase.started && damage>=1000);
    for(i=0;i<19;++i)nausika_hit(chase.pos[0],chase.pos[1],chase.pos[2],0,230,0);
    assert(chase.state==3);
    for(i=0;i<100;++i)nausika_hit(chase.pos[0],chase.pos[1],chase.pos[2],0,230,0);
    assert(chase.state==4);
    nausika_enter(307);pose_actor(0);
    for(i=0;i<nausika_parts[0].vertices;++i)assert(isfinite(vertices[0][i].x));
    puts("Nausika: native path, trigger, attack, stagger, death and finite native pose passed");
    return 0;
}

