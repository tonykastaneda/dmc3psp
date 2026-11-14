#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t alignment, size_t bytes) {(void)alignment;return malloc(bytes);}
#define memalign test_memalign
#include "../../psp/testroom/leviathan_boss.c"
#include "../../psp/testroom/orbs.c"
int (*player_floor_fn)(float,float,float,float,float *);

int nfx_spawn(int bank,int id,const float p[3],float yaw,int loop) {(void)bank;(void)id;(void)p;(void)yaw;(void)loop;return 0;}
void nfx_kill(int handle) {(void)handle;}
unsigned int fx_random_u32(void) {return 1;}
void nfx_tint(int handle,unsigned int r,unsigned int g,unsigned int b) {(void)handle;(void)r;(void)g;(void)b;}
int nfx_alive(int handle) {(void)handle;return 1;}
int nfx_spawn_e(int bank,int id,const float p[3],float yaw,int loop) {return nfx_spawn(bank,id,p,yaw,loop);}
void nfx_move(int handle,const float p[3],float yaw) {(void)handle;(void)p;(void)yaw;}
void fx_tracer(const float a[3],const float b[3],unsigned int color,float width) {(void)a;(void)b;(void)color;(void)width;}
void fx_projectile(const float a[3],const float b[3],unsigned int color) {(void)a;(void)b;(void)color;}

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
        leviathan_boss_update(143,0,0,0,1);
        assert(!leviathan_boss_open());
        leviathan_boss_update(2,0,0,0,1);
        assert(leviathan_boss_open());
        leviathan_boss_hit(0,100,0);
        assert(leviathan_boss_health()==hp); /* training immortality */
        leviathan_boss_hit(0,100,1);
        assert(fabsf(leviathan_boss_health()-(hp-100*damage_scale[d]))<.01f);
        leviathan_boss_update(1000,0,0,0,1);
        assert(!leviathan_boss_open());
        assert(heart.lung_hp[0]==1500);
        leviathan_boss_hit(2,5000,1);
        leviathan_boss_update(145,0,0,0,1);
        assert(leviathan_boss_hit(0,20000,1));
        assert(!leviathan_boss_hit(0,20000,1)); /* death event exactly once */
        assert(leviathan_boss_dying());
        assert(leviathan_boss_health()==0);
        assert(leviathan_boss_update(120,0,0,0,1));
    }
    leviathan_boss_spawn();
    heart.hp=6000;
    leviathan_boss_collect_orbs(100);
    assert(heart.hp==6005);
    leviathan_boss_absorb(1,400);
    leviathan_boss_update(1,0,0,0,1);
    assert(heart.state==HEART_ATTACK);
    assert(!leviathan_boss_open());
    leviathan_boss_update(54,0,0,0,1);
    assert(leviathan_boss_open());
    leviathan_boss_update(70,0,0,0,1);
    assert(heart.attack_step==1);
    leviathan_boss_update(60,4978.54f,0,3264.0f,1);
    assert(heart.landed && heart.landed_damage==1500);
    leviathan_boss_kill();
    assert(!leviathan_boss_active());
    leviathan_boss_spawn();
    leviathan_boss_absorb(2,501);
    leviathan_boss_update(125,0,0,0,1);
    assert(shells[0].active);
    leviathan_boss_kill();
    assert(!shells[0].active);
    leviathan_boss_spawn();
    {
        int wallet=0;
        orbs_set_counter(&wallet);
        orbs_set_sink(leviathan_boss_orb_sink,leviathan_boss_collect_orbs);
        orbs_burst(target_pos[2][0],target_pos[2][1],target_pos[2][2],20);
        orbs_update(.5f,0,0,0);
        assert(wallet==0 && heart.charge[1]==20);
        leviathan_boss_kill();
        orbs_burst(0,20,0,20);
        orbs_update(.5f,0,0,0);
        assert(wallet==20); /* ordinary collection restored after encounter */
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
