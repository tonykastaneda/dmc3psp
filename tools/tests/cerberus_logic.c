#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_memalign(size_t alignment, size_t bytes) {(void)alignment; return malloc(bytes);}
#define memalign test_memalign
#include "../../psp/testroom/cerberus_boss.c"

static unsigned int rng = 1;
unsigned int enemy_ai_random(void) {rng = rng * 1664525u + 1013904223u; return rng;}
int stage_loaded(void) {return 0;}
int stage_raycast(const float a[3], const float b[3], float *t) {(void)a;(void)b;(void)t;return 0;}
void fx_hit_spark(const float p[3]) {(void)p;}
void fx_wall_spark(const float p[3]) {(void)p;}
void fx_explosion(const float p[3]) {(void)p;}
void fx_dust(const float p[3]) {(void)p;}
void fx_screen_tint(unsigned int color) {(void)color;}
int testroom_frame;
static int nfx_handles;
int nfx_spawn(int b, int v, const float p[3], float y, int l) {(void)b;(void)v;(void)p;(void)y;(void)l;return ++nfx_handles;}
int nfx_spawn_e(int b, int e, const float p[3], float y, int l) {(void)b;(void)e;(void)p;(void)y;(void)l;return ++nfx_handles;}
void nfx_move(int h, const float p[3], float y) {(void)h;(void)p;(void)y;}
void nfx_scale(int h, float s) {(void)h;(void)s;}
void nfx_kill(int h) {(void)h;}

int main(void) {
    int phase, id, i;
    assert(motion_init() > 0);
    assert(cerberus_boss_init());
    cerberus_boss_spawn();
    cb.action=CB_TURN; cb.action_step=1; cb.body.frame=10; cb.body.done=0;
    update_action(1,2100,0,2200);
    assert(cb.yaw > PI && cb.yaw < PI + 0.03f);
    cb.yaw=PI; cb.action_step=-1;
    update_action(1,2900,0,2200);
    assert(cb.yaw < PI && cb.yaw > PI - 0.03f);
    cerberus_boss_spawn(); cb.phase=2; cb.action=CB_REICE;
    cb.reice=-1; cb.armour[ARMOUR_LEFT]=4;
    finish_move();
    assert(cb.reice==3000 && cb.action!=CB_REICE);
    /* Every real attack must leave its committed state, including head rotations. */
    for (phase = 0; phase < 3; ++phase) for (id = 0; id <= 11; ++id) {
        int began, max_shells = 0;
        if (id == 10) continue;
        cerberus_boss_spawn(); cb.phase = phase;
        cb.dante[0] = 2500; cb.dante[2] = 2600;
        begin_decision(id); began = cb.action;
        for (i = 0; i < 2000; ++i) {
            cerberus_boss_update(1, 2500, 0, 2600, 0);
            if (shell_count() > max_shells) max_shells = shell_count();
            assert(isfinite(body_pose[0].p[0]));
            if (cb.action == CB_IDLE || cb.action == CB_STRUGGLE) break;
        }
        if (i == 2000) fprintf(stderr, "stuck phase=%d move=%d action=%d began=%d script=%d frame=%f\n",phase,id,cb.action,began,cb.body.script,cb.body.frame);
        assert(i < 2000);
        if (id == 2 || id == 3 || id == 6 || id == 7 || id == 8) assert(max_shells > 0);
        printf("phase=%d move=%d completed=%d frames\n", phase, id, i);
    }
    /* An actual head intersection must remain the damage target even when
     * the shooter stands closer to a paw. */
    for (id=12;id<20;++id) {
        cerberus_boss_spawn(); cb.phase=2;
        schedule_decision(id);
        for(i=0;i<5000 && cb.combo_active;++i) {
            assert(cb.pending >= -1 && cb.pending <= 11);
            cerberus_boss_update(1,2500,0,2600,0);
        }
        assert(!cb.combo_active && i<5000);
    }
    cerberus_boss_spawn();
    {
        float p[3], a[3];
        part_position(ARMOUR_HEAD0, p);
        memcpy(a, p, sizeof(a)); a[2] -= 200;
        assert(cerberus_boss_segment_hit(a,p) >= 0);
        i = cb.target_part;
        assert(i >= ARMOUR_HEAD0);
        cerberus_boss_hit(HIT_PISTOL,2500,1400,1);
        assert(cb.armour[i] == 1 && cb.armour[ARMOUR_LEFT] == 0);
    }
    cerberus_boss_spawn();
    /* A complete fight through the normal damage API, with no kill hook. */
    for(i=0;i<6000 && !cb.dying;++i) {
        ++cb.tick;
        cerberus_boss_hit(HIT_PISTOL,2500,2200,1);
    }
    assert(cb.dying && cerberus_boss_health() == 0 && cb.phase == 2);
    cerberus_boss_spawn();
    {
        float p[3];
        part_position(ARMOUR_LEFT,p);
        for(i=0;i<4;++i) cerberus_boss_hit(HIT_PISTOL,p[0],p[2],1);
        assert(cb.armour[ARMOUR_LEFT] == 4);
        assert(cerberus_boss_health() == 7200);
        cerberus_boss_hit(HIT_PISTOL,p[0],p[2],1);
        assert(cb.body_hp == 2392);
        cerberus_boss_set_difficulty(0);
        cerberus_boss_hit(HIT_PISTOL,p[0],p[2],1);
        assert(cb.body_hp == 2376);
        cerberus_boss_set_difficulty(1);
        cb.phase=1;
        cb.last_head_damage_tick=-1;
        cerberus_boss_hit(HIT_PISTOL,p[0],p[2],1);
        assert(cb.head_hp[0] < 1650 && cb.head_hp[1] < 1600 && cb.head_hp[2] < 1550);
    }
    puts("Cerberus original-motion attack progression and armour/damage checks passed.");
    return 0;
}
