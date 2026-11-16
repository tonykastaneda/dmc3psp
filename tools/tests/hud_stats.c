#include "player.h"
#include <assert.h>
#include <stdio.h>
#include "motion.h"
#include "camera.h"
#include "stage.h"

/* Flat room without motion assets; exercise the real player update. */
int motion_find(const char *name) { (void)name; return -1; }
const Motion *motion_get(int index) { (void)index; return NULL; }
int stage_loaded(void) { return 0; }
int stage_collide(float *x, float *z, float y, float r) { (void)x; (void)z; (void)y; (void)r; return 0; }
int stage_raycast(const float a[3], const float b[3], float *t) { (void)a; (void)b; (void)t; return 0; }
float camera_stick_yaw(FollowCamera *c, float x, float y, int active) { (void)x; (void)y; (void)active; return c->yaw; }
void camera_update(const Player *p, FollowCamera *c, int reset, float dt) { (void)p; (void)c; (void)reset; (void)dt; }

int main(void) {
    Player p = {0};
    FollowCamera camera;
    player_reset(&p, &camera);
    assert(p.health == PLAYER_HEALTH_BASE && p.health_max == PLAYER_HEALTH_BASE);
    assert(p.dt_energy == 3 && p.dt_max == 3);
    p.health = 4000; p.dt_energy = 1;
    player_set_capacity(&p, 0, 0);
    assert(p.health == 4000 && p.dt_energy == 1); /* no refill each frame */
    player_set_capacity(&p, 1, 1);
    assert(p.health == 11000 && p.health_max == 11000);
    assert(p.dt_energy == 4 && p.dt_max == 4);
    player_set_capacity(&p, 999, 999);
    assert(p.health_max == PLAYER_HEALTH_MAX && p.dt_max == 10);
    p.dt_energy = 9.9f;
    player_gain_dt(&p, 1);
    assert(p.dt_energy == 10);
    p.dt_energy = 1; p.devil = 1;
    player_gain_dt(&p, 1);
    assert(p.dt_energy == 1);
    p.devil = 0; p.quicksilver = 1;
    player_gain_dt(&p, 1);
    assert(p.dt_energy == 1);
    p.quicksilver = 0; p.doppelganger = 1;
    player_gain_dt(&p, 1);
    assert(p.dt_energy == 1);
    p.doppelganger = 0;
    player_gain_dt(&p, -1);
    assert(p.dt_energy == 1);
    player_gain_dt(&p, .25f);
    assert(p.dt_energy == 1.25f);
    player_set_capacity(&p, -5, -5);
    assert(p.health_max == PLAYER_HEALTH_BASE && p.dt_max == 3);
    assert(p.health <= p.health_max && p.dt_energy <= p.dt_max);
    {
        PlayerInput input = {.style = -1};
        int i;
        player_reset(&p, &camera);
        input.devil = 1;
        player_update(&p, &camera, &input, 1.0f/60);
        assert(p.devil && p.dt_energy < 3);
        input.devil = 0;
        for(i=0;i<190;++i) player_update(&p, &camera, &input, 1.0f/60);
        assert(!p.devil && p.dt_energy == 0);
        input.devil = 1;
        player_update(&p, &camera, &input, 1.0f/60);
        assert(!p.devil); /* cannot activate on an empty gauge */
        input.devil = 0;
        p.dt_energy = .001f; p.quicksilver = p.doppelganger = 1;
        player_update(&p, &camera, &input, 1.0f/60);
        assert(!p.quicksilver && !p.doppelganger && p.dt_energy == 0);
    }
    puts("HUD capacity and DT gain checks passed.");
    return 0;
}
