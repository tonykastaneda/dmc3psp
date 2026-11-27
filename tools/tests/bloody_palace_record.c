#include "bloody_palace.h"
#include <assert.h>
#include <stdio.h>
int menu_loaded(void) { return 0; }
float menu_font_draw_scaled(float x,float y,float h,float w,const char *s,unsigned int c) {
    (void)x;(void)y;(void)h;(void)w;(void)s;(void)c;return 0;
}
float menu_font_draw(float x,float y,float h,const char *s,unsigned int c) {
    (void)x;(void)y;(void)h;(void)s;(void)c;return 0;
}
int main(int argc,char **argv) {
    int room,start,i; char path[512]; FILE *f;
    assert(argc==2);
    snprintf(path,sizeof path,"%s/BP_BEST.TXT",argv[1]);
    bp_init(argv[1]); assert(bp_best_floor()==0);
    bp_reset(1); bp_enter(423);
    for(i=0;i<10;++i) {
        bp_update(180,1); assert(bp_route(2,&room,&start)); bp_enter(room);
    }
    assert(bp_floor()==1000 && bp_best_floor()==1000);
    bp_reset(0); assert(bp_best_floor()==1000);
    bp_init(argv[1]); assert(bp_best_floor()==1000);
    bp_reset(1); bp_test_start(9999,0,-1,0); bp_enter(444);
    assert(bp_best_floor()==1000);
    bp_init(argv[1]); assert(bp_best_floor()==1000);
    f=fopen(path,"w"); assert(f); fputs("BPB1 10000\n",f); fclose(f);
    bp_init(argv[1]); assert(bp_best_floor()==0);
    f=fopen(path,"w"); assert(f); fputs("BPB1 1234 garbage\n",f); fclose(f);
    bp_init(argv[1]); assert(bp_best_floor()==0);
    f=fopen(path,"w"); assert(f); fputs("BPB1 9999\n",f); fclose(f);
    bp_init(argv[1]); assert(bp_best_floor()==9999);
    bp_reset(1); bp_enter(423);
    bp_init(argv[1]); assert(bp_best_floor()==9999);
    puts("Palace best-floor reload, monotonicity, malformed-save and test-isolation checks passed");
    return 0;
}
