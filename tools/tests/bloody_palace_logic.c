/* Boundary and lifecycle checks using the real disc-generated EST header. */
#include "bloody_palace.h"
#include <assert.h>
#include <stdio.h>
int menu_loaded(void) { return 0; }
float menu_font_draw(float x,float y,float h,const char *s,unsigned int c) {
    (void)x;(void)y;(void)h;(void)s;(void)c;return 0;
}
int main(void) {
    PalaceOp op; int room,start,i; float pickup[3];
    bp_reset(1); bp_test_start(0,0,-1,0); bp_enter(423);
    assert(bp_floor()==0 && bp_op(0,&op));
    assert(!bp_route(0,&room,&start));
    bp_update(59,1); assert(!bp_portal_ready(0));
    bp_update(1,1); assert(bp_portal_ready(0) && !bp_portal_ready(1));
    bp_update(60,1); assert(bp_portal_ready(1) && !bp_portal_ready(2));
    bp_update(60,1); assert(bp_route(2,&room,&start) && room==423 && bp_floor()==100);
    bp_enter(room); assert(!bp_portal_ready(0) && bp_op(0,&op));
    bp_update(180,1); bp_update(1,0); assert(!bp_portal_ready(0));
    bp_reset(1); bp_test_start(990,0,-1,0); bp_enter(423); bp_update(180,1);
    assert(bp_route(1,&room,&start) && room==424 && bp_floor()==1000);
    bp_enter(room);assert(bp_pickup_position(pickup)==15);
    assert(bp_pickup_collect(pickup[0]+1000,pickup[1],pickup[2])==0);
    bp_pickup_fall(60,514);assert(bp_pickup_position(pickup)==15);
    assert(bp_pickup_collect(pickup[0],514,pickup[2])==15);
    assert(bp_pickup_collect(pickup[0],514,pickup[2])==0);
    for (i=0;i<17;++i) {
        bp_reset(1);bp_test_start(900,9,i,0);bp_enter(423);bp_update(180,1);
        assert(bp_route(0,&room,&start) && !(room>=423 && room<=432));
        assert((room==438)==(start==2));
        bp_enter(room); assert(bp_op(0,&op) && bp_pickup_position(pickup)==12);bp_update(180,1);
        assert(bp_route(0,&room,&start) && room==423 && bp_floor()==901);
        bp_enter(room);assert(bp_op(0,&op) && !bp_pickup_position(pickup));
    }
    bp_reset(1);bp_test_start(9990,9,0,0);bp_enter(432);bp_update(180,1);
    assert(bp_route(2,&room,&start) && room==444 && bp_floor()==9999);
    bp_enter(room);bp_update(179,1);assert(!bp_finished());bp_update(1,1);
    assert(bp_finished() && !bp_route(0,&room,&start));
    bp_reset(0);assert(!bp_active() && !bp_op(0,&op) && !bp_finished());
    puts("Bloody Palace routing, encounter reset, boss return and final-floor checks passed");
}
