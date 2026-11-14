#include "orbs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "nfx.h"
#include "player.h"

/* The burst's split is the game's (FUN_002f4510); how pieces fly and home is still an estimate, made to
 * look like it. Each piece is drawn by the game's own effect for its size (CItemOrb, FUN_002f2690):
 * V45, V186, V187, V188, V189 for 1, 5, 20, 100 and 1000, tinted by the red entry at 005aa590. */
#define ORB_MAX 96
#define ORB_GRAVITY 1500.0f    /* cm/s^2 */
#define ORB_SETTLE 0.45f       /* seconds before they start for Dante */
#define ORB_PULL 2600.0f       /* cm/s^2 toward him */
#define ORB_TOP_SPEED 1400.0f  /* cm/s */
#define ORB_COLLECT 70.0f      /* cm from where he picks them up */

typedef struct { int active, value, size, fx; float pos[3], vel[3], age; } Orb;

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

static float unit_random(void) { return (float)(fx_random_u32() & 0xffff) / 65535.0f; }

static void add_piece(float x, float y, float z, int size) {
    float a = unit_random() * 6.2831853f, out = 120.0f + unit_random() * 160.0f, at[3] = {x, y, z};
    int i;
    for (i = 0; i < ORB_MAX && orbs[i].active; ++i) {}
    if (i >= ORB_MAX) {
        /* No room: straight into the count. */
        float at[3];
        if(sink_position && sink_collect && sink_position(at))sink_collect(orb_value[size]);
        else if (counter) *counter += orb_value[size];
        return;
    }
    orbs[i].active = 1;
    orbs[i].value = orb_value[size];
    orbs[i].size = size;
    orbs[i].pos[0] = x; orbs[i].pos[1] = y; orbs[i].pos[2] = z;
    orbs[i].vel[0] = sinf(a) * out; orbs[i].vel[1] = 380.0f + unit_random() * 260.0f; orbs[i].vel[2] = cosf(a) * out;
    orbs[i].age = 0.0f;
    orbs[i].fx = nfx_spawn(NFX_STATIC, orb_effect[size], at, 0.0f, 1);
    nfx_tint(orbs[i].fx, 80, 0, 0);
}

void orbs_burst(float x, float y, float z, int total) {
    int count[5], pieces, size, k;
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
    for (size = 4; size >= 0; --size)
        for (k = 0; k < count[size]; ++k) add_piece(x, y, z, size);
}

void orbs_update(float dt, float px, float py, float pz) {
    int i;
    float target[3];
    int to_sink=sink_position && sink_collect && sink_position(target);
    if(to_sink) {px=target[0];py=target[1]-20;pz=target[2];}
    since_collected += dt;
    for (i = 0; i < ORB_MAX; ++i) {
        Orb *o = &orbs[i];
        float g, shown[3];
        if (!o->active) continue;
        o->age += dt;
        if (o->age < ORB_SETTLE) {
            /* Thrown out, falling, bouncing off the floor. */
            o->vel[1] -= ORB_GRAVITY * dt;
            o->pos[0] += o->vel[0] * dt; o->pos[1] += o->vel[1] * dt; o->pos[2] += o->vel[2] * dt;
            if (player_floor_fn && player_floor_fn(o->pos[0], o->pos[1] + 60.0f, o->pos[2], 0.0f, &g) && o->pos[1] < g) {
                o->pos[1] = g;
                o->vel[1] = -o->vel[1] * 0.4f;
                o->vel[0] *= 0.6f; o->vel[2] *= 0.6f;
            }
        } else {
            /* To him, faster and faster: the effect's +80 lands on his chest. */
            float dx = px - o->pos[0], dy = py + 20.0f - o->pos[1], dz = pz - o->pos[2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f, v;
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
                /* FUN_002f3750: E185 where he takes it, in the same colour. */
                flash = nfx_spawn_e(NFX_STATIC, 185, chest, 0.0f, 0);
                nfx_tint(flash, 80, 0, 0);
                continue;
            }
            o->vel[0] += dx / d * ORB_PULL * dt; o->vel[1] += dy / d * ORB_PULL * dt; o->vel[2] += dz / d * ORB_PULL * dt;
            /* Mostly toward him: what's sideways dies away, so they don't orbit. */
            o->vel[0] *= 0.94f; o->vel[1] *= 0.94f; o->vel[2] *= 0.94f;
            v = sqrtf(o->vel[0] * o->vel[0] + o->vel[1] * o->vel[1] + o->vel[2] * o->vel[2]);
            if (v > ORB_TOP_SPEED) { float k = ORB_TOP_SPEED / v; o->vel[0] *= k; o->vel[1] *= k; o->vel[2] *= k; }
            o->pos[0] += o->vel[0] * dt; o->pos[1] += o->vel[1] * dt; o->pos[2] += o->vel[2] * dt;
        }
        memcpy(shown, o->pos, sizeof(shown));
        if (!nfx_alive(o->fx)) {
            o->fx = nfx_spawn(NFX_STATIC, orb_effect[o->size], shown, 0.0f, 1);
            nfx_tint(o->fx, 80, 0, 0);
        }
        nfx_move(o->fx, shown, 0.0f);
    }
}

