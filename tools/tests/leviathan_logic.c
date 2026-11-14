#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t alignment, size_t bytes) {(void)alignment;return malloc(bytes);}
#define memalign test_memalign
#include "../../psp/testroom/leviathan_boss.c"

int main(void) {
    float hp;
    int d;
    assert(motion_init()>0);
    assert(leviathan_boss_init());
    for(d=0;d<5;++d) {
        leviathan_boss_set_difficulty(d);
        leviathan_boss_spawn();
        hp=leviathan_boss_health();
        assert(!leviathan_boss_hit(0,500,1));
        assert(leviathan_boss_health()==hp); /* shielded core */
        leviathan_boss_hit(1,4000,1);
        assert(!leviathan_boss_open());
        leviathan_boss_update(54,0,0,0,1);
        assert(!leviathan_boss_open());
        leviathan_boss_update(1,0,0,0,1);
        assert(leviathan_boss_open());
        leviathan_boss_hit(0,100,0);
        assert(leviathan_boss_health()==hp); /* training immortality */
        leviathan_boss_hit(0,100,1);
        assert(fabsf(leviathan_boss_health()-(hp-100*damage_scale[d]))<.01f);
        leviathan_boss_update(1000,0,0,0,1);
        assert(!leviathan_boss_open());
        assert(heart.lung_hp[0]==1500);
        leviathan_boss_hit(2,5000,1);
        leviathan_boss_update(55,0,0,0,1);
        assert(leviathan_boss_hit(0,20000,1));
        assert(!leviathan_boss_hit(0,20000,1)); /* death event exactly once */
        assert(leviathan_boss_dying());
        assert(leviathan_boss_health()==0);
        assert(leviathan_boss_update(120,0,0,0,1));
    }
    leviathan_boss_spawn();
    pose_actor(0);pose_actor(1);pose_actor(2);
    for(d=0;d<3;++d) {
        int v;
        for(v=0;v<leviathan_parts[d].vertices;++v) {
            assert(isfinite(vertices[d][v].x));
            assert(isfinite(vertices[d][v].y));
            assert(isfinite(vertices[d][v].z));
        }
    }
    puts("Leviathan: native motions, shield, lung stun/recovery, difficulty damage, death and finite poses passed");
    return 0;
}
