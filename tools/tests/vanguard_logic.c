/* Host regression of the actual Vanguard implementation and original MOT data.
 * GPU calls are no-ops; a thin wall/floor fixture isolates movement. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *test_memalign(size_t alignment,size_t bytes) {(void)alignment;return malloc(bytes);}
#define memalign test_memalign
#include "../../psp/testroom/vanguard.c"

static int wall,missing_floor;
static unsigned int random_state=1;
unsigned int enemy_ai_random(void) {random_state=random_state*1664525u+1013904223u;return random_state;}
void fx_vanguard_event(int id,const float at[3]) {(void)id;(void)at;}
int testroom_frame;
unsigned int fx_random_u32(void) {static unsigned int x=1;x=x*1103515245u+12345u;return x>>8;}
void fx_mote(const float a[3],const float v[3],float s0,float s1,float l,unsigned int c) {(void)a;(void)v;(void)s0;(void)s1;(void)l;(void)c;}
int stage_collide(float *x,float *z,float y,float radius) {
    (void)z;(void)y;
    if(wall && fabsf(*x)<radius) {*x=*x<0?-radius:radius;return 1;}
    return 0;
}
int stage_raycast(const float a[3],const float b[3],float *t) {
    if(wall && a[0]*b[0]<0) {*t=.5f;return 1;}return 0;
}
static int floor_fixture(float x,float y,float z,float step,float *out) {
    (void)y;(void)z;(void)step;
    if(missing_floor && x>0)return 0;
    *out=0;return 1;
}
int (*player_floor_fn)(float,float,float,float,float *)=floor_fixture;
static void reset(void) {
    int i;wall=missing_floor=0;random_state=1;memset(&vg,0,sizeof(vg));
    vanguard_set_difficulty(1);
    for(i=0;i<80;++i) {
        char name[12];snprintf(name,sizeof(name),"%s-%02d",i<71?"vg":"vx",i<71?i:i-71);
        vg.ids[i]=motion_find(name);assert(vg.ids[i]>=0);
    }
    vanguard_spawn(0,0,0);
}
int main(void) {
    int i;float yaw,before[3];
    assert(motion_init()>0);
    reset();vg.token=73;decide(100,1);assert(vg.token==73);
    vg.token=0;decide(100,0);assert(vg.token==180);
    {
        int primary=0,previous=-1;
        reset();
        for(i=0;i<1000;++i) {
            step(1,300,0,0,1);
            if(vg.action==16 && previous!=16)++primary;
            previous=vg.action;
        }
        assert(primary>=2); /* defensive actions cannot starve primary attacks */
    }
    reset();idle();
    for(i=0;i<59;++i)vanguard_hit(HIT_PISTOL,1);
    assert(vg.action==15 && vg.hp==2410 && vg.strain==5);
    vanguard_hit(HIT_PISTOL,1);
    assert(vg.action==0 && vg.hp==2400 && vg.strain==300);
    reset();idle();vanguard_set_difficulty(0);vanguard_hit(HIT_PISTOL,1);
    assert(vg.hp==2980 && vg.action==15);
    reset();idle();vanguard_hit(HIT_HEAVY,1);assert(vg.hp==2916 && vg.action==0);
    reset();action(69);
    assert(vg.hidden && vg.visible); /* departure: immune, but still drawn */
    for(i=0;i<31;++i)step(1,300,0,0,0);
    assert(vg.phase==0 && !vg.visible);
    for(i=0;i<200 && vg.phase!=3;++i)step(1,300,0,0,0);
    assert(vg.phase==3 && vg.script==102 && vg.frame==202 && vg.visible);
    assert(vg.stop==32767 && vg.cap_left[17]>0); /* no reverse seek to wait 200 */
    yaw=vg.yaw;
    step(1,300,0,0,0);
    assert(fabsf(vg.yaw-yaw)<.00001f); /* no invented actor spin */
    assert(vg.frame==203);

    reset();action(70);
    for(i=0;i<100 && vg.phase!=1;++i)step(1,300,0,20,0);
    assert(vg.hidden && vg.pos[0]==300 && vg.pos[2]==20);
    step(1,300,0,20,0);assert(vg.hidden && !vg.visible && vg.phase==2 && vg.script==105);
    for(i=0;i<50;++i)step(1,300,0,20,0);
    assert(vg.phase==2);step(1,300,0,20,0);
    assert(vg.phase==3 && vg.script==107 && !vg.hidden && vg.visible);
    for(i=0;i<3;++i)step(1,300,0,20,0);
    assert(vg.phase==3 && vg.pos[1]==0);
    step(1,300,0,20,0);assert(vg.phase==4);
    for(i=0;i<100 && vg.action==70;++i)step(1,300,0,20,0);
    assert(vg.action==73 && vg.pos[1]==0);

    reset();wall=1;vg.pos[0]=-100;move_ground(300,0);
    assert(vg.pos[0]<=-vg_caps[0].radius); /* cannot tunnel across the thin wall */
    reset();missing_floor=1;vg.pos[0]=-100;move_ground(300,0);
    assert(vg.pos[0]<=0); /* do not step off the supported floor */
    reset();wall=1;vg.hidden=1;warp_to(-100,0,0,400);
    assert(vg.pos[0]<=-vg_caps[0].radius);
    assert(fabsf(hypotf(vg.pos[0]+100,vg.pos[2])-400)<=10.1f);

    reset();start_death();pose();memcpy(before,body[3].p,sizeof(before));
    for(i=0;i<20;++i)step(1,300,0,0,0);
    assert(vg.frame==20 && memcmp(before,body[3].p,sizeof(before))!=0);
    puts("PASS: pistol damage/strain vs sword flinch; rush start; uppercut timing; collision; visibility; death pose");
    return 0;
}
