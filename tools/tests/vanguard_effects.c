/* Exercise the actual subset E/V implementation with extracted effect data. */
#include <assert.h>
#include <stdio.h>
#include "../../psp/testroom/fx.c"
int main(void) {
    int i,n=0;float at[3]={100,0,200},u0,v0,u1,v1;
    fx_reset();fx_vanguard_event(702,at);
    for(i=0;i<10;++i)fx_update(1.0f/60);
    for(i=0;i<MAX_SPRITES;++i)n+=sprites[i].native!=0;
    assert(n==0); /* the first native E child is scheduled strictly after frame 10 */
    fx_update(1.0f/60);n=0;
    for(i=0;i<MAX_SPRITES;++i)if(sprites[i].native) {
        Sprite *s=&sprites[i];++n;
        assert(s->native==vg_fx_E796 && s->size0>0 && s->size0<200);
        vg_fx_uv(s,&u0,&v0,&u1,&v1);
        assert(u0>=0 && v0>=0 && u1<=1 && v1<=1 && u1>u0 && v1>v0);
    }
    assert(n==1);
    for(i=0;i<10;++i)fx_update(1.0f/60);n=0;
    for(i=0;i<MAX_SPRITES;++i)n+=sprites[i].native!=0;
    assert(n==2);
    assert((vg_fx_color(vg_fx_E1311,0)>>24)==0);
    assert((vg_fx_color(vg_fx_E1311,10)>>24)>0);
    assert((vg_fx_color(vg_fx_E1311,40)>>24)==0);
    fx_reset();fx_vanguard_event(516,at);
    for(i=0;i<31;++i)fx_update(1.0f/60);n=0;
    for(i=0;i<MAX_SPRITES;++i)n+=sprites[i].native!=0;
    assert(n==0);
    fx_update(1.0f/60);n=0;
    for(i=0;i<MAX_SPRITES;++i)n+=sprites[i].native!=0;
    assert(n==1);
    puts("PASS: native departure/uppercut effect scheduling, atlas UVs, scale and alpha curves");
    return 0;
}
