#include "orbs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "sfx.h"
#include "nfx.h"
#include "player.h"

/* CItemOrb, from the PS2 code. The burst (FUN_002f4510, called with spread 120 / 50 on an enemy's death by
 * FUN_00366f10): the first piece where it died, the rest up to 119 cm off on x and z and 49 on y. Each falls to the
 * floor under the burst at 0.1 cm/frame^2 (at most 10 cm/frame), bounces once at 3 cm/frame and rests, and lives
 * 1200 frames, blinking for the last 400 (FUN_002f2fa0, FUN_002f5010). Only when Dante comes within 300 cm does
 * it home: a speed growing 0.3 a frame (FUN_0035a4a0, capped at 50), each frame moving it its square (at most
 * 50 cm) straight at his feet, and it's his within 50 cm (FUN_002f3750). Each piece is drawn by the game's own
 * effect for its size (CItemOrb, FUN_002f2690): V45, V186, V187, V188, V189 for 1, 5, 20, 100 and 1000, tinted by
 * the red entry at 005aa590. */
#define ORB_MAX 96
#define ORB_LIFE 1200.0f
#define ORB_HOME_RANGE 300.0f   /* 10000 in rooms 0xd8 and 0x260, 3000-4000 with a second collector: not ported */
#define ORB_COLLECT 50.0f
#define ORB_HOME_ACCEL 0.3f
#define ORB_HOME_MAX 50.0f

typedef struct {
    int active, value, size, fx, dropping, bounced, homing;
    float pos[3], floor_y, vy_base, vy, speed, life, blink;
} Orb;

static Orb orbs[ORB_MAX];
static int *counter;
static int (*sink_position)(float out[3]);
static void (*sink_collect)(int value);
static float since_collected = 1.0e9f;
static const int orb_effect[5] = {45, 186, 187, 188, 189};
static const int orb_value[5] = {1, 5, 20, 100, 1000};

void orbs_set_counter(int *red_orbs) { counter = red_orbs; }
void orbs_set_sink(int (*position)(float out[3]), void (*collect)(int value)) {
    sink_position=position;sink_collect=collect;
}
void orbs_clear(void) {
    int i;
    for (i = 0; i < ORB_MAX; ++i) if (orbs[i].active) nfx_kill(orbs[i].fx);
    memset(orbs, 0, sizeof(orbs));
}
float orbs_since_collected(void) { return since_collected; }

int orbs_for_enemy(int max_health, int difficulty) {
    float scale = difficulty == 0 ? 0.5f : 1.0f;
    int total = (int)((float)max_health * 5.0f * scale / 100.0f);
    return total > 0 ? total : 1;
}


static int spread(int range) { return (int)fx_random_u32() % range; }

static void add_piece(float x, float y, float z, int size, int first) {
    float at[3], ground = y - 100.0f;
    int i;
    for (i = 0; i < ORB_MAX && orbs[i].active; ++i) {}
    if (i >= ORB_MAX) {
        /* No room: straight into the count. */
        if(sink_position && sink_collect && sink_position(at))sink_collect(orb_value[size]);
        else if (counter) *counter += orb_value[size];
        return;
    }
    if (player_floor_fn) player_floor_fn(x, y, z, 0.0f, &ground);   /* the floor under the burst, for all of them */
    memset(&orbs[i], 0, sizeof(orbs[i]));
    orbs[i].active = 1;
    orbs[i].value = orb_value[size];
    orbs[i].size = size;
    orbs[i].pos[0] = first ? x : x + (float)spread(120);
    orbs[i].pos[1] = first ? y : y + (float)spread(50);
    orbs[i].pos[2] = first ? z : z + (float)spread(120);
    orbs[i].floor_y = ground;
    orbs[i].dropping = 1;
    orbs[i].life = ORB_LIFE;
    orbs[i].blink = (float)(fx_random_u32() % 10);
    memcpy(at, orbs[i].pos, sizeof(at));
    orbs[i].fx = nfx_spawn(NFX_STATIC, orb_effect[size], at, 0.0f, 1);
    nfx_tint(orbs[i].fx, 80, 0, 0);
}

void orbs_burst(float x, float y, float z, int total) {
    int count[5], pieces, size, k, first;
    if (total <= 0) return;
    /* FUN_002f4510: thousands, hundreds, twenties, fives and ones, then larger ones broken down until
     * there are at least seven pieces. */
    count[4] = total / 1000; count[3] = total % 1000 / 100; count[2] = total % 100 / 20;
    count[1] = total % 20 / 5; count[0] = total % 5;
    pieces = count[0] + count[1] + count[2] + count[3] + count[4];
    while (count[4] > 0 && pieces < 7) { --count[4]; count[3] += 10; pieces += 9; }
    while (count[3] > 0 && pieces < 7) { --count[3]; count[2] += 5; pieces += 4; }
    while (count[2] > 0 && pieces < 7) { --count[2]; count[1] += 4; pieces += 3; }
    while (count[1] > 0 && pieces < 7) { --count[1]; count[0] += 5; pieces += 4; }
    for (size = 4, first = 1; size >= 0; --size)
        for (k = 0; k < count[size]; ++k, first = 0) add_piece(x, y, z, size, first);
}

void orbs_update(float dt, float px, float py, float pz) {
    int i;
    float target[3], f = dt * 60.0f;   /* the game's steps */
    int to_sink=sink_position && sink_collect && sink_position(target);
    if(to_sink) {px=target[0];py=target[1]-20;pz=target[2];}
    since_collected += dt;
    for (i = 0; i < ORB_MAX; ++i) {
        Orb *o = &orbs[i];
        float dx, dy, dz, d, shown[3];
        if (!o->active) continue;
        o->blink += f;
        o->life -= f;
        if (o->life < 0.0f) { nfx_kill(o->fx); o->active = 0; continue; }
        dx = px - o->pos[0]; dy = py - o->pos[1]; dz = pz - o->pos[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < ORB_COLLECT) {
            float chest[3] = {px, py + 100.0f, pz};
            int flash;
            if(to_sink)sink_collect(o->value);
            else {
                if(counter)*counter+=o->value;
                since_collected=0;
            }
            nfx_kill(o->fx);
            o->active = 0;
            /* FUN_002f3750: E185 where he takes it, in the same colour, and the system bank's sound 0 for the
             * small sizes (below 3: 1, 5, 20), 1 for the others. */
            flash = nfx_spawn_e(NFX_STATIC, 185, chest, 0.0f, 0);
            sfx_play(o->value <= 20 ? 0 : 1, 0.0f, 1.0f);
            nfx_tint(flash, 80, 0, 0);
            continue;
        }
        if (!o->homing && d < ORB_HOME_RANGE) { o->homing = 1; o->dropping = 0; }
        if (o->homing) {
            float step;
            o->speed += ORB_HOME_ACCEL * f;
            if (o->speed > ORB_HOME_MAX) o->speed = ORB_HOME_MAX;
            step = o->speed * o->speed;
            if (step > 50.0f) step = 50.0f;
            step *= f;
            if (step > d) step = d;
            o->pos[0] += dx / d * step; o->pos[1] += dy / d * step; o->pos[2] += dz / d * step;
        } else if (o->dropping) {
            o->vy -= 0.1f * f;
            if (o->vy < -10.0f) o->vy = -10.0f;
            o->pos[1] += (o->vy_base + o->vy) * f;
            if (o->pos[1] < o->floor_y) {
                o->pos[1] = o->floor_y;
                if (o->bounced) o->dropping = 0;
                else { o->bounced = 1; o->vy_base = 3.0f; o->vy = 0.0f; }
            }
        }
        memcpy(shown, o->pos, sizeof(shown));
        if (!nfx_alive(o->fx)) {
            o->fx = nfx_spawn(NFX_STATIC, orb_effect[o->size], shown, 0.0f, 1);
            nfx_tint(o->fx, 80, 0, 0);
        }
        nfx_move(o->fx, shown, 0.0f);
        /* Blinking at the end: out one frame in 8 under 400 left, one in 3 under 200. */
        nfx_scale(o->fx, (o->life < 200.0f && (int)o->blink % 3 == 0) || (o->life < 400.0f && o->life >= 200.0f && (int)o->blink % 8 == 0) ? 0.0f : 1.0f);
    }
}
