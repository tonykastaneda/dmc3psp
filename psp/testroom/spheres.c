/* Mission 16's hanging spheres: CStageSetHang (146, set 150 HANG) and CStageSetHang2 (109, set 150 HANG2, two
 * balls), decoded from the SE executable (FUN_00394920 the set's parser, FUN_00395ac0 its frame, FUN_00396970 the
 * swing, FUN_00397120 a hit; Hang2's FUN_004ede50, FUN_004ef250, FUN_004f0130, FUN_004f09d0):
 *
 *   The ball hangs on a chain of `length` from an anchor at the set's pos, raised `distance` while it's up. Each
 *   frame it moves by its velocity, the velocity takes its acceleration, and if it's further from the anchor than
 *   the chain it's put back on it (at 1.01 times the length) and loses 1.2 times its outward speed; then the
 *   velocity is kept at 0.998.
 *   States (the set's byte +5): 2 up, hanging still (velocity x and z at 0.95 a frame, y less 1, x and z drawn 5%
 *   a frame under the anchor); its trigger flag set (the crest switch, through the event script) lowers it: 3, the
 *   anchor comes down 20 a frame, then one push of 3 in x and z, and it stays down `time` frames; then 1, the
 *   anchor goes back up 10 a frame and the trigger flag is cleared (the switch must be struck again); 4, done.
 *   A sword hit on a lowered ball pushes it away from the blow by damage / `math` (its mass; y halved).
 *   146's ball has a shell of 1200: each hit takes its damage off, its look steps down at 1000, 800, 600, 400 and
 *   200, and below 0 it's broken (state 4): its done flag is set (EventTbl16: flag 35, which opens gates 300 and
 *   301 to the Onyx Moonshard).
 *   109's two balls aren't worn: when both are down and touch, both are done (FUN_004eff40), which frees the Golden
 *   Sun.
 *
 * The port's: the balls are plain spheres of 200 cm (their EFM meshes' width) on a line for the chain (their EFM models and the shell's
 * pieces aren't drawn), and the switches small posts; the acceleration is gravity of 1 cm a frame squared (the up
 * state's own pull; the set's acceleration isn't read); the damage is the port's 42 a light hit and 84 a heavy one;
 * 109's switch 100 lowers the ball by it (z 3050) and 101 the other. The items aren't held back by the puzzles
 * yet (they lie open, as before): in PPSSPP the balls came within 450 cm of the 400 that's a touch in 109, and
 * 146's shell wore but a standing Dante lost the ball as it swung, so it's not shown that the estimated gravity and
 * damage let a player finish them. The gates (300, 301) stay as they're drawn. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>

#include "spheres.h"

#define BALL_RADIUS 200.0f   /* the balls' EFM meshes span x and z +-200 */
#define SHELL 1200.0f
enum { ST_RISING = 1, ST_UP = 2, ST_DOWN = 3, ST_DONE = 4 };
typedef struct {
    float anchor[3], base_y, pos[3], vel[3];
    float length, distance, mass, time, timer, shell;
    int state, kicked;
    float sw[3];   /* its switch */
} Ball;
static Ball balls[2];
static int count, room_on = -1, worn, solved146, solved109, hits;
static float closest = 1.0e9f;   /* tests: the nearest the two balls have come */

static void ball_init(Ball *b, float x, float y, float z, float length, float distance, float mass, float time,
                      float sx, float sy, float sz, int done) {
    memset(b, 0, sizeof *b);
    b->anchor[0] = x; b->anchor[2] = z; b->base_y = y;
    b->length = length; b->distance = distance; b->mass = mass; b->time = time;
    b->sw[0] = sx; b->sw[1] = sy; b->sw[2] = sz;
    b->shell = SHELL;
    b->state = done ? ST_DONE : ST_UP;
    b->anchor[1] = y + distance;   /* up */
    b->pos[0] = x; b->pos[1] = b->anchor[1] - length; b->pos[2] = z;
}

void spheres_reset(void) { solved146 = solved109 = 0; }

void spheres_enter(int room) {
    room_on = room; count = 0; worn = 0;
    if (room == 146) {
        /* SET 150 HANG: pos 2400, 2800, 2900; length 2500, distance 500, math 10, time 1500. Switch 100 at
         * (2500, 145, 3775). */
        ball_init(&balls[0], 2400.0f, 2800.0f, 2900.0f, 2500.0f, 500.0f, 10.0f, 1500.0f, 2500.0f, 145.0f, 3775.0f, solved146);
        count = 1; worn = 1;
    } else if (room == 109) {
        /* SET 150 HANG2: pos 2500, 1400, 3050 and pos2 2500, 1400, 4050; length 1100, distance 600, math 7, time
         * 3000. Switches 100 (3050, 200, 3100) and 101 (3050, 200, 3900). */
        ball_init(&balls[0], 2500.0f, 1400.0f, 3050.0f, 1100.0f, 600.0f, 7.0f, 3000.0f, 3050.0f, 200.0f, 3100.0f, solved109);
        ball_init(&balls[1], 2500.0f, 1400.0f, 4050.0f, 1100.0f, 600.0f, 7.0f, 3000.0f, 3050.0f, 200.0f, 3900.0f, solved109);
        count = 2;
    }
}

int spheres_solved(int room) { return room == 146 ? solved146 : room == 109 ? solved109 : 1; }

static void finish(Ball *b) { b->state = ST_DONE; }

static void step(Ball *b) {
    float d[3], len, out;
    int k;
    switch (b->state) {
    case ST_UP:
        b->vel[0] *= 0.95f; b->vel[1] -= 1.0f; b->vel[2] *= 0.95f;
        b->pos[0] = b->anchor[0] * 0.05f + b->pos[0] * 0.95f;
        b->pos[2] = b->anchor[2] * 0.05f + b->pos[2] * 0.95f;
        break;
    case ST_DOWN:
        if (b->anchor[1] > b->base_y) b->anchor[1] -= 20.0f;
        else {
            b->anchor[1] = b->base_y;
            if (!b->kicked) { b->vel[0] += 3.0f; b->vel[2] += 3.0f; b->kicked = 1; }
        }
        if ((b->timer -= 1.0f) < 0.0f) { b->kicked = 0; b->state = ST_RISING; }
        break;
    case ST_RISING:
    case ST_DONE:
        if (b->anchor[1] < b->base_y + b->distance) b->anchor[1] += 10.0f;
        else { b->anchor[1] = b->base_y + b->distance; if (b->state == ST_RISING) { b->state = ST_UP; if (worn) b->shell = SHELL; } }
        break;
    }
    for (k = 0; k < 3; ++k) b->pos[k] += b->vel[k];
    b->vel[1] -= 1.0f;   /* gravity [estimate] */
    for (k = 0; k < 3; ++k) d[k] = b->pos[k] - b->anchor[k];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > b->length && len > 0.0f) {
        for (k = 0; k < 3; ++k) { d[k] /= len; b->pos[k] = b->anchor[k] + d[k] * b->length * 1.01f; }
        out = b->vel[0] * d[0] + b->vel[1] * d[1] + b->vel[2] * d[2];
        for (k = 0; k < 3; ++k) b->vel[k] -= d[k] * out * 1.2f;
    }
    for (k = 0; k < 3; ++k) b->vel[k] *= 0.998f;
}

void spheres_update(float dt) {
    static float carry;
    int i;
    if (!count) return;
    carry += dt * 60.0f;
    if (carry > 4.0f) carry = 4.0f;
    while (carry >= 1.0f) {
        carry -= 1.0f;
        for (i = 0; i < count; ++i) step(&balls[i]);
        if (count == 2 && balls[0].state == ST_DOWN && balls[1].state == ST_DOWN) {
            float dx = balls[0].pos[0] - balls[1].pos[0], dy = balls[0].pos[1] - balls[1].pos[1], dz = balls[0].pos[2] - balls[1].pos[2];
            if (sqrtf(dx * dx + dy * dy + dz * dz) < closest) closest = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dx * dx + dy * dy + dz * dz < (2.0f * BALL_RADIUS) * (2.0f * BALL_RADIUS)) {
                finish(&balls[0]); finish(&balls[1]); solved109 = 1;   /* knocked together */
            }
        }
    }
}

void spheres_hit(float x, float y, float z, float yaw, float reach, int heavy) {
    int i;
    float damage = heavy ? 84.0f : 42.0f, fx = sinf(yaw), fz = cosf(yaw);
    for (i = 0; i < count; ++i) {
        Ball *b = &balls[i];
        float d[3] = {b->pos[0] - x, b->pos[1] - (y + 100.0f), b->pos[2] - z}, len, sx, sz, sd;
        /* its switch: struck while it's up, it comes down */
        sx = b->sw[0] - x; sz = b->sw[2] - z; sd = sqrtf(sx * sx + sz * sz);
        if (b->state == ST_UP && sd < reach + 100.0f && fabsf(b->sw[1] - y) < 300.0f && (sd < 1.0f || (sx * fx + sz * fz) / sd > 0.2f)) {
            b->state = ST_DOWN; b->timer = b->time; b->kicked = 0;
            b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
            ++hits;
            continue;
        }
        if (b->state != ST_DOWN) continue;
        /* within his reach across the floor, and the ball's height within his swing's (feet to 2.5 m) */
        len = sqrtf(d[0] * d[0] + d[2] * d[2]);
        if (len > reach + BALL_RADIUS || b->pos[1] - BALL_RADIUS > y + 250.0f || b->pos[1] + BALL_RADIUS < y) continue;
        if (len > 1.0f && (d[0] * fx + d[2] * fz) / len < 0.0f) continue;   /* in front of him */
        len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 1.0f) continue;
        b->vel[0] += damage * d[0] / len / b->mass;
        b->vel[1] += damage * d[1] / len / b->mass / 2.0f;
        b->vel[2] += damage * d[2] / len / b->mass;
        ++hits;
        if (worn && (b->shell -= damage) < 0.0f) { finish(b); solved146 = 1; }
    }
}

void spheres_status(char *out, int size) {
    snprintf(out, size, "spheres room=%d n=%d st=%d,%d y=%.0f,%.0f z=%.0f,%.0f closest=%.0f shell=%.0f solved=%d,%d hits=%d", room_on, count,
             count ? balls[0].state : 0, count > 1 ? balls[1].state : 0, count ? balls[0].pos[1] : 0.0f,
             count > 1 ? balls[1].pos[1] : 0.0f, count ? balls[0].pos[2] : 0.0f, count > 1 ? balls[1].pos[2] : 0.0f, closest,
             count ? balls[0].shell : 0.0f, solved146, solved109, hits);
}

typedef struct { unsigned int color; float x, y, z; } SphereVertex;
#define SLICES 12
#define STACKS 8
static void draw_ball(const Ball *b) {
    /* shaded by height: lit from above; the shell's colour runs from steel blue to purple as it wears */
    float wear = worn ? 1.0f - fmaxf(b->shell, 0.0f) / SHELL : 0.0f;
    unsigned int r0 = (unsigned int)(70 + 110 * wear), g0 = (unsigned int)(90 - 40 * wear), b0 = (unsigned int)(150 + 40 * wear);
    SphereVertex *v = sceGuGetMemory(sizeof(SphereVertex) * SLICES * STACKS * 6);
    int i, j, n = 0;
    for (j = 0; j < STACKS; ++j) {
        float a0 = 3.14159265f * j / STACKS, a1 = 3.14159265f * (j + 1) / STACKS;
        for (i = 0; i < SLICES; ++i) {
            float c0 = 6.2831853f * i / SLICES, c1 = 6.2831853f * (i + 1) / SLICES;
            float p[4][3] = {{sinf(a0) * cosf(c0), cosf(a0), sinf(a0) * sinf(c0)}, {sinf(a0) * cosf(c1), cosf(a0), sinf(a0) * sinf(c1)},
                             {sinf(a1) * cosf(c1), cosf(a1), sinf(a1) * sinf(c1)}, {sinf(a1) * cosf(c0), cosf(a1), sinf(a1) * sinf(c0)}};
            static const int q[6] = {0, 1, 2, 0, 2, 3};
            int k;
            for (k = 0; k < 6; ++k) {
                const float *e = p[q[k]];
                float light = 0.55f + 0.45f * e[1];
                unsigned int col = 0xff000000u | (unsigned int)(b0 * light) << 16 | (unsigned int)(g0 * light) << 8 | (unsigned int)(r0 * light);
                v[n++] = (SphereVertex){col, b->pos[0] + e[0] * BALL_RADIUS, b->pos[1] + e[1] * BALL_RADIUS, b->pos[2] + e[2] * BALL_RADIUS};
            }
        }
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    {
        SphereVertex *l = sceGuGetMemory(sizeof(SphereVertex) * 2);
        l[0] = (SphereVertex){0xff303438u, b->anchor[0], b->anchor[1], b->anchor[2]};
        l[1] = (SphereVertex){0xff303438u, b->pos[0], b->pos[1] + BALL_RADIUS, b->pos[2]};
        sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 2, 0, l);
    }
    {
        /* its switch: a post, lit while the ball's down */
        SphereVertex *s = sceGuGetMemory(sizeof(SphereVertex) * 6);
        unsigned int col = b->state == ST_DOWN ? 0xff40c0ffu : 0xff606060u;
        float x = b->sw[0], y = b->sw[1], z = b->sw[2];
        s[0] = (SphereVertex){col, x - 30.0f, y - 100.0f, z}; s[1] = (SphereVertex){col, x + 30.0f, y - 100.0f, z};
        s[2] = (SphereVertex){col, x + 30.0f, y + 100.0f, z}; s[3] = (SphereVertex){col, x - 30.0f, y - 100.0f, z};
        s[4] = (SphereVertex){col, x + 30.0f, y + 100.0f, z}; s[5] = (SphereVertex){col, x - 30.0f, y + 100.0f, z};
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, s);
    }
}

void spheres_draw(void) {
    int i;
    if (!count) return;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuShadeModel(GU_SMOOTH);
    for (i = 0; i < count; ++i) draw_ball(&balls[i]);
    sceGuEnable(GU_TEXTURE_2D);
}
