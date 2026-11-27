#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>

#include "nfx.h"
#include "fx_runtime.h"
#include "trail_runtime.h"
#include "vanguard_fx_runtime.h"

/* Sized for a boss fight: E&I's flashes (three a shot) and hit sparks no longer recycle a boss's own
 * timed sprites before their authored lifetimes end. */
#define MAX_SPRITES 128
#define TRAIL_POINTS 14
#define TRAIL_SECONDS 0.14f

typedef enum { MODE_BILLBOARD, MODE_FLAT, MODE_BEAM, MODE_DISC } SpriteMode;  /* DISC faces along end[] */

typedef struct {
    float pos[3], vel[3], end[3];
    float size0, size1, life, total, spin, angle, gravity;
    unsigned int color;
    unsigned char sheet, cell, grid, mode, smoke;
    const unsigned char *native;
    float native_age, native_scale[3], native_height;
} Sprite;

typedef struct { float u, v; unsigned int color; float x, y, z; } FxVertex;
typedef struct { unsigned int color; float x, y, z; } FxPlainVertex;

static Sprite sprites[MAX_SPRITES];
static int next_sprite;
static unsigned int seed = 12345u;

/* Two sweeps (A&R's two blades). Textured as the game's weapon trails (docs/NATIVE_FX.md "Blade trails"): an nfx
 * texture and the rectangle of it its trail model maps, u along the sweep (newest first), v hilt to tip. */
static struct {
    struct { float base[3], tip[3], age; } p[TRAIL_POINTS];
    int count, fresh, bank, tex;
    unsigned int color;
    float u0, u1, v0, v1;
} trails[2];

/* The game's model trails (docs/NATIVE_FX.md "Blade trails", FUN_00309030 kinds 2 / 3): the weapon frame's last 11
 * matrices (FUN_0030a380, a sample every frame), made into 33 bone frames (FUN_0030aa50: 4 a segment, the rotations
 * blended 1 / 0.75 / 0.5 / 0.25, the places a quadratic B-spline through the segments' midpoints, the first the newest
 * frame itself) that carry the trail model's vertices. Alpha (FUN_00309d30, the table's +8 / +9 / +10 / +0xc): up by
 * the step a frame to the top while fed, down by it to the floor from +0xc frames after the last feed. */
#define MTRAIL_HISTORY 11
static struct {
    float m[MTRAIL_HISTORY][3][3], p[MTRAIL_HISTORY][3];
    int count, model;
    float alpha, since;
} mtrails[2];
typedef struct { const TrailVertex *v; const unsigned short *idx; int nv, ni, bank, tex; float top, step, floor_, delay; } MTrailModel;
static const MTrailModel *mtrail_model(int model) {
    static const MTrailModel models[3] = {
        {trail_m34_vertices, trail_m34_indices, TRAIL_M34_VERTICES, TRAIL_M34_INDICES, NFX_DANTE, 10034, 128, 16, 0, 3},
        {trail_m30_vertices, trail_m30_indices, TRAIL_M30_VERTICES, TRAIL_M30_INDICES, NFX_ARM_AGNI, 10030, 128, 24, 8, 5},
        {trail_m31_vertices, trail_m31_indices, TRAIL_M31_VERTICES, TRAIL_M31_INDICES, NFX_ARM_AGNI, 10031, 128, 24, 8, 5}};
    return model == 34 ? &models[0] : model == 30 ? &models[1] : model == 31 ? &models[2] : 0;
}
void fx_trail_sample(int slot, int model, const float m[3][3], const float p[3], int feed) {
    int i;
    if (slot < 0 || slot > 1 || !mtrail_model(model)) return;
    if (mtrails[slot].model != model) { mtrails[slot].model = model; mtrails[slot].count = 0; mtrails[slot].alpha = 0.0f; mtrails[slot].since = 99.0f; }
    if (mtrails[slot].count < MTRAIL_HISTORY) ++mtrails[slot].count;
    for (i = mtrails[slot].count - 1; i > 0; --i) {
        memcpy(mtrails[slot].m[i], mtrails[slot].m[i - 1], sizeof(mtrails[slot].m[i]));
        memcpy(mtrails[slot].p[i], mtrails[slot].p[i - 1], sizeof(mtrails[slot].p[i]));
    }
    memcpy(mtrails[slot].m[0], m, sizeof(mtrails[slot].m[0]));
    memcpy(mtrails[slot].p[0], p, sizeof(mtrails[slot].p[0]));
    if (feed) mtrails[slot].since = 0.0f;
}
static void mtrail_update(float frames) {
    int k;
    for (k = 0; k < 2; ++k) {
        const MTrailModel *t = mtrail_model(mtrails[k].model);
        if (!t) continue;
        mtrails[k].since += frames;
        if (mtrails[k].since > t->delay) { mtrails[k].alpha -= t->step * frames; if (mtrails[k].alpha < t->floor_) mtrails[k].alpha = t->floor_; }
        else { mtrails[k].alpha += t->step * frames; if (mtrails[k].alpha > t->top) mtrails[k].alpha = t->top; }
    }
}
static void mtrail_draw(void) {
    static const float w[4][3] = {{1.0f, 0.0f, 0.0f}, {0.5625f, 0.375f, 0.0625f}, {0.25f, 0.5f, 0.25f}, {0.0625f, 0.375f, 0.5625f}};
    static const float ab[4][2] = {{1.0f, 0.0f}, {0.75f, 0.25f}, {0.5f, 0.5f}, {0.25f, 0.75f}};
    int k;
    for (k = 0; k < 2; ++k) {
        const MTrailModel *t = mtrail_model(mtrails[k].model);
        float bm[33][3][3], bp[33][3];
        int n = mtrails[k].count, b = 0, i, j, a, c;
        unsigned int color;
        FxVertex *v;
        if (!t || n < 2 || mtrails[k].alpha <= t->floor_ || !nfx_bind_texture(t->bank, t->tex)) continue;
        for (i = 0; i < 9 && b < 33; ++i) {
            int i0 = i < n ? i : n - 1, i1 = i + 1 < n ? i + 1 : n - 1, i2 = i + 2 < n ? i + 2 : n - 1;
            for (j = 0; j < 4 && b < 33; ++j, ++b) {
                for (a = 0; a < 3; ++a) {
                    float len = 0.0f;
                    for (c = 0; c < 3; ++c) {
                        bm[b][c][a] = mtrails[k].m[i0][c][a] * ab[j][0] + mtrails[k].m[i1][c][a] * ab[j][1];
                        len += bm[b][c][a] * bm[b][c][a];
                    }
                    len = len > 1e-6f ? 1.0f / sqrtf(len) : 0.0f;
                    for (c = 0; c < 3; ++c) bm[b][c][a] *= len;
                }
                for (c = 0; c < 3; ++c)
                    bp[b][c] = (i == 0 && j == 0) ? mtrails[k].p[0][c]
                             : (mtrails[k].p[i0][c] + mtrails[k].p[i1][c]) * 0.5f * w[j][0] + mtrails[k].p[i1][c] * w[j][1]
                               + (mtrails[k].p[i1][c] + mtrails[k].p[i2][c]) * 0.5f * w[j][2];
            }
        }
        color = ((unsigned int)(mtrails[k].alpha * 255.0f / 128.0f) << 24) | 0xffffffu;
        v = sceGuGetMemory(sizeof(FxVertex) * t->ni);
        for (i = 0; i < t->ni; ++i) {
            const TrailVertex *s = &t->v[t->idx[i]];
            int bn = s->bone < b ? s->bone : b - 1;
            float q[3];
            for (c = 0; c < 3; ++c) q[c] = bp[bn][c] + bm[bn][c][0] * s->x + bm[bn][c][1] * s->y + bm[bn][c][2] * s->z;
            v[i] = (FxVertex){s->u, s->v, color, q[0], q[1], q[2]};
        }
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        sceGuDisable(GU_CULL_FACE);
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, t->ni, 0, v);
    }
}
static void vg_fx_reset(void);

static float fx_rand(void) {   /* 0..1 */
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 16) & 0x7fff) / 32767.0f;
}

/* Visual-only randomness, separate from the AI's stream so effects never change a decision. */
unsigned int fx_random_u32(void) {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

void fx_reset(void) {
    memset(sprites, 0, sizeof(sprites));
    trails[0].count = trails[1].count = 0;
    memset(mtrails, 0, sizeof(mtrails));
    vg_fx_reset();
}

static Sprite *spawn(int sheet, int grid, int cell, const float at[3], float size0, float size1, float life, unsigned int color) {
    Sprite *s;
    int tries;
    /* Round robin, but a live native (game-timed) sprite is never the one taken: it would vanish mid-effect,
     * the "blinking" of the Vanguard's teleports under gunfire. */
    for (tries = 0; tries < MAX_SPRITES; ++tries) {
        s = &sprites[next_sprite];
        if (!(s->native && s->life > 0.0f)) break;
        next_sprite = (next_sprite + 1) % MAX_SPRITES;
    }
    s = &sprites[next_sprite];
    next_sprite = (next_sprite + 1) % MAX_SPRITES;
    memset(s, 0, sizeof(*s));
    s->sheet = (unsigned char)sheet;
    s->grid = (unsigned char)grid;
    s->cell = (unsigned char)cell;
    memcpy(s->pos, at, sizeof(s->pos));
    s->size0 = size0;
    s->size1 = size1;
    s->life = s->total = life;
    s->color = color;
    return s;
}

#include "vanguard_fx.inc"

void fx_update(float dt) {
    int i, k;
    vg_fx_update(dt * 60.0f);
    for (i = 0; i < MAX_SPRITES; ++i) {
        Sprite *s = &sprites[i];
        if (s->life <= 0.0f) continue;
        if(s->native) {vg_fx_sprite(s,dt*60.0f);continue;}
        s->life -= dt;
        s->vel[1] -= s->gravity * dt;
        for (k = 0; k < 3; ++k) s->pos[k] += s->vel[k] * dt;
        s->angle += s->spin * dt;
    }
    mtrail_update(dt * 60.0f);
    for (k = 0; k < 2; ++k) {
        for (i = 0; i < trails[k].count; ++i) trails[k].p[i].age += dt;
        while (trails[k].count > 0 && trails[k].p[trails[k].count - 1].age > TRAIL_SECONDS) --trails[k].count;
        trails[k].fresh = 0;
    }
}

/* Matched to the real game frame by frame: a big white and orange spiked flash for two
 * frames, a grey puff that lingers, and no visible bullet. */
void fx_muzzle(const float at[3], const float dir[3], unsigned int color) {
    float p[3] = {at[0] + dir[0] * 30.0f, at[1] + dir[1] * 30.0f, at[2] + dir[2] * 30.0f};
    Sprite *s = spawn(FX_SHEET_SPARK_A, 1, 0, p, 72.0f, 80.0f, 0.04f, 0xff60b0ffu);
    s->angle = fx_rand() * 6.28f;
    s = spawn(FX_SHEET_FLARE, 2, 2, p, 62.0f, 50.0f, 0.04f, 0xffffffffu);
    s->angle = fx_rand() * 6.28f;
    (void)color;
    /* The puff is alpha blended grey, not added. */
    s = spawn(FX_SHEET_SMOKE, 2, 0, at, 9.0f, 26.0f, 0.65f, 0x50c0c0c0u);
    s->vel[1] = 14.0f;
    s->smoke = 1;
    s->angle = fx_rand() * 6.28f;
}

void fx_mote(const float at[3], const float vel[3], float size0, float size1, float life, unsigned int color) {
    Sprite *s = spawn(FX_SHEET_GLOW, 4, 0, at, size0, size1, life, color);
    memcpy(s->vel, vel, sizeof(s->vel));
}

void fx_tracer(const float from[3], const float to[3], unsigned int color, float width) {
    Sprite *s = spawn(FX_SHEET_LINES, 4, 12, from, width, width * 0.3f, 0.06f, color);
    s->mode = MODE_BEAM;
    memcpy(s->end, to, sizeof(s->end));
}

void fx_projectile(const float tail[3], const float head[3], unsigned int color) {
    Sprite *s = spawn(FX_SHEET_LINES, 4, 12, tail, 10.0f, 10.0f, 0.02f, color);
    s->mode = MODE_BEAM;
    memcpy(s->end, head, sizeof(s->end));
    spawn(FX_SHEET_GLOW, 4, 0, head, 18.0f, 18.0f, 0.02f, color);
}

/* A white star of spikes, a thin red ring opening out, and smoke. */
void fx_hit_spark(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_FLARE, 2, 2, at, 62.0f, 46.0f, 0.07f, 0xffffffffu);
    s->angle = fx_rand() * 6.28f;
    s = spawn(fx_rand() < 0.5f ? FX_SHEET_SPARK_A : FX_SHEET_SPARK_B, 1, 0, at, 30.0f, 60.0f, 0.12f, 0xffffffffu);
    s->angle = fx_rand() * 6.28f;
    spawn(FX_SHEET_BITS, 4, 10, at, 10.0f, 36.0f, 0.13f, 0xff4040ffu);
    s = spawn(FX_SHEET_SMOKE, 2, 1, at, 16.0f, 44.0f, 0.35f, 0x40c0c0c0u);
    s->smoke = 1;
    s->angle = fx_rand() * 6.28f;
}

void fx_wall_spark(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_SPARK_B, 1, 0, at, 8.0f, 26.0f, 0.1f, 0xffa0e0ffu);
    s->angle = fx_rand() * 6.28f;
}

void fx_explosion(const float at[3]) {
    int i;
    for (i = 0; i < 3; ++i) {
        float p[3] = {at[0] + (fx_rand() - 0.5f) * 40.0f, at[1] + (fx_rand() - 0.5f) * 40.0f, at[2] + (fx_rand() - 0.5f) * 40.0f};
        Sprite *s = spawn(FX_SHEET_FIRE, 2, i, p, 40.0f, 120.0f, 0.3f + 0.1f * (float)i, 0xffffffffu);
        s->angle = fx_rand() * 6.28f;
        s->spin = fx_rand() - 0.5f;
    }
}

void fx_dust(const float at[3]) {
    int i;
    for (i = 0; i < 3; ++i) {
        float a = fx_rand() * 6.28f;
        float p[3] = {at[0], at[1] + 8.0f, at[2]};
        Sprite *s = spawn(FX_SHEET_SMOKE, 2, i, p, 18.0f, 56.0f, 0.3f, 0x60a0b0c0u);
        s->vel[0] = sinf(a) * 70.0f;
        s->vel[2] = cosf(a) * 70.0f;
        s->vel[1] = 16.0f;
        s->angle = a;
        s->spin = 0.6f;
    }
}

void fx_jump_burst(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_BITS, 4, 10, at, 16.0f, 88.0f, 0.22f, 0xffffffffu);
    s->mode = MODE_FLAT;
    s = spawn(FX_SHEET_GLOW, 4, 0, at, 44.0f, 8.0f, 0.15f, 0xffa0d0ffu);
}

void fx_devil_burst(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_FLARE, 2, 0, at, 60.0f, 170.0f, 0.3f, 0xff3030ffu);
    s->angle = fx_rand() * 6.28f;
    s = spawn(FX_SHEET_RAYS, 2, 0, at, 50.0f, 150.0f, 0.25f, 0xff4040ffu);
    s->angle = fx_rand() * 6.28f;
    s = spawn(FX_SHEET_GLOW, 4, 8, at, 20.0f, 130.0f, 0.3f, 0xff2020ffu);
}

/* A low red glow on him and, now and then, a crackle. */
void fx_devil_aura(const float at[3]) {
    static int tick;
    Sprite *s;
    if (++tick % 3 == 0) {
        s = spawn(FX_SHEET_GLOW, 4, 0, at, 95.0f, 100.0f, 0.1f, 0x402020ffu);
    }
    if (tick % 14 == 0) {
        float p[3] = {at[0] + (fx_rand() - 0.5f) * 70.0f, at[1] + (fx_rand() - 0.5f) * 110.0f, at[2] + (fx_rand() - 0.5f) * 70.0f};
        s = spawn(FX_SHEET_RAYS, 2, 2 + (tick / 14) % 2, p, 30.0f, 36.0f, 0.1f, 0xc04040ffu);
        s->angle = fx_rand() * 6.28f;
    }
}

/* A pale streak left along his path: the real dash has no flash or dust. */
void fx_streak(const float from[3], const float to[3]) {
    Sprite *s = spawn(FX_SHEET_LINES, 4, 12, from, 15.0f, 5.0f, 0.13f, 0x48d8d8ffu);
    s->mode = MODE_BEAM;
    memcpy(s->end, to, sizeof(s->end));
}

/* Air Hike: a flash and light going up from his feet, leaving a seal in the air that turns,
 * widens and fades. In the weapon's colour. Hand-built from the real game's footage. */
void fx_hike_seal(const float at[3], unsigned int color) {
    static const float up[3] = {0.0f, 1.0f, 0.0f};
    int i;
    Sprite *s = spawn(FX_SHEET_SEAL, 2, 2, at, 70.0f, 165.0f, 0.6f, color);
    s->mode = MODE_DISC;
    memcpy(s->end, up, sizeof(s->end));
    s->spin = 1.4f;
    s = spawn(FX_SHEET_GLOW, 4, 0, at, 130.0f, 90.0f, 0.16f, color);
    s->mode = MODE_DISC;
    memcpy(s->end, up, sizeof(s->end));
    for (i = 0; i < 3; ++i) {
        float base[3] = {at[0] + (fx_rand() - 0.5f) * 50.0f, at[1], at[2] + (fx_rand() - 0.5f) * 50.0f};
        s = spawn(FX_SHEET_FLARE, 2, 3, base, 34.0f, 12.0f, 0.2f, color);
        s->mode = MODE_BEAM;
        memcpy(s->end, base, sizeof(s->end));
        s->end[1] += 210.0f + fx_rand() * 90.0f;
    }
}

/* Sky Star: the same seal standing across his path, where he left from. */
void fx_star_seal(const float at[3], const float direction[3], unsigned int color) {
    Sprite *s = spawn(FX_SHEET_SEAL, 2, 2, at, 85.0f, 135.0f, 0.5f, color);
    s->mode = MODE_DISC;
    memcpy(s->end, direction, sizeof(s->end));
    s->spin = -1.4f;
    spawn(FX_SHEET_GLOW, 4, 0, at, 120.0f, 70.0f, 0.18f, color);
}

void fx_guard_flash(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_GLOW, 4, 8, at, 40.0f, 150.0f, 0.18f, 0xffa0a0ffu);
    s->angle = 0.0f;
    spawn(FX_SHEET_FLARE, 2, 1, at, 70.0f, 40.0f, 0.16f, 0xffffffffu);
}

void fx_trail_add_slot(int slot, const float base[3], const float tip[3], unsigned int color, int bank, int tex,
                       float u0, float u1, float v0, float v1) {
    int i;
    if (slot < 0 || slot > 1 || trails[slot].fresh) return;
    trails[slot].fresh = 1;
    trails[slot].color = color;
    trails[slot].bank = bank; trails[slot].tex = tex;
    trails[slot].u0 = u0; trails[slot].u1 = u1; trails[slot].v0 = v0; trails[slot].v1 = v1;
    if (trails[slot].count < TRAIL_POINTS) ++trails[slot].count;
    for (i = trails[slot].count - 1; i > 0; --i) trails[slot].p[i] = trails[slot].p[i - 1];
    memcpy(trails[slot].p[0].base, base, sizeof(trails[slot].p[0].base));
    memcpy(trails[slot].p[0].tip, tip, sizeof(trails[slot].p[0].tip));
    trails[slot].p[0].age = 0.0f;
}

void fx_trail_add(const float base[3], const float tip[3], unsigned int color) {
    fx_trail_add_slot(0, base, tip, color, -1, -1, 0.0f, 0.0f, 0.0f, 0.0f);
}

static unsigned int scale_alpha(unsigned int color, float k) {
    unsigned int a = (unsigned int)((float)(color >> 24) * k);
    return (color & 0x00ffffffu) | (a << 24);
}

static void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void normalize(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0001f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void put_quad(FxVertex *v, const float c[4][3], float u0, float v0, float u1, float v1, unsigned int color) {
    static const int corner[6] = {0, 1, 2, 2, 1, 3};
    int i;
    for (i = 0; i < 6; ++i) {
        const float *p = c[corner[i]];
        v[i].u = (corner[i] & 1) ? u1 : u0;
        v[i].v = (corner[i] & 2) ? v1 : v0;
        v[i].color = color;
        v[i].x = p[0]; v[i].y = p[1]; v[i].z = p[2];
    }
}

static void bind(int sheet) {
    if(sheet>=FX_SHEET_COUNT) {
        int light=sheet==FX_SHEET_COUNT+1,size=light?128:256;
        sceGuClutMode(GU_PSM_8888,0,0xff,0);
        sceGuClutLoad(32,light?vg_fx_palette_48:vg_fx_palette_25);
        sceGuTexMode(GU_PSM_T8,0,0,0);
        sceGuTexImage(0,size,size,size,light?vg_fx_pixels_48:vg_fx_pixels_25);
        sceGuTexFlush();return;
    }
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(256 / 8, fx_palettes[sheet]);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, FX_TEX_SIZE, FX_TEX_SIZE, FX_TEX_SIZE, fx_pixels[sheet]);
    sceGuTexFlush();
}

static void sprite_corners(const Sprite *s, const float eye[3], float c[4][3]) {
    if (s->mode == MODE_DISC) {
        /* Two axes across the facing direction, turned by the sprite's angle. */
        float size = s->size1 + (s->size0 - s->size1) * (s->life / s->total);
        float n[3] = {s->end[0], s->end[1], s->end[2]};
        float helper[3] = {fabsf(n[1]) > 0.9f ? 1.0f : 0.0f, fabsf(n[1]) > 0.9f ? 0.0f : 1.0f, 0.0f};
        float a0[3] = {n[1] * helper[2] - n[2] * helper[1], n[2] * helper[0] - n[0] * helper[2], n[0] * helper[1] - n[1] * helper[0]};
        float len = sqrtf(a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]);
        float a1[3], co = cosf(s->angle), si = sinf(s->angle), r[3], u[3];
        int k;
        if (len < 0.001f) len = 1.0f;
        for (k = 0; k < 3; ++k) a0[k] /= len;
        a1[0] = n[1] * a0[2] - n[2] * a0[1];
        a1[1] = n[2] * a0[0] - n[0] * a0[2];
        a1[2] = n[0] * a0[1] - n[1] * a0[0];
        for (k = 0; k < 3; ++k) {
            r[k] = (a0[k] * co + a1[k] * si) * size;
            u[k] = (a1[k] * co - a0[k] * si) * size;
            c[0][k] = s->pos[k] - r[k] - u[k];
            c[1][k] = s->pos[k] + r[k] - u[k];
            c[2][k] = s->pos[k] - r[k] + u[k];
            c[3][k] = s->pos[k] + r[k] + u[k];
        }
        return;
    }
    float t = 1.0f - s->life / s->total;
    float size = s->size0 + (s->size1 - s->size0) * t;
    float view[3] = {s->pos[0] - eye[0], s->pos[1] - eye[1], s->pos[2] - eye[2]};
    float right[3], up[3];
    int i, k;
    if (s->mode == MODE_BEAM) {
        float dir[3] = {s->end[0] - s->pos[0], s->end[1] - s->pos[1], s->end[2] - s->pos[2]};
        cross(dir, view, right);
        normalize(right);
        for (k = 0; k < 3; ++k) {
            c[0][k] = s->pos[k] - right[k] * size;
            c[1][k] = s->end[k] - right[k] * size;
            c[2][k] = s->pos[k] + right[k] * size;
            c[3][k] = s->end[k] + right[k] * size;
        }
        return;
    }
    if (s->mode == MODE_FLAT) {
        right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    } else {
        static const float world_up[3] = {0.0f, 1.0f, 0.0f};
        float r0[3], u0[3], ca = cosf(s->angle), sa = sinf(s->angle);
        cross(world_up, view, r0);
        normalize(r0);
        cross(view, r0, u0);
        normalize(u0);
        for (k = 0; k < 3; ++k) {
            right[k] = r0[k] * ca + u0[k] * sa;
            up[k] = u0[k] * ca - r0[k] * sa;
        }
    }
    for (i = 0; i < 4; ++i) {
        float height=s->native?s->native_height:size;
        float sx = (i & 1) ? size : -size, sy = (i & 2) ? -height : height;
        for (k = 0; k < 3; ++k) c[i][k] = s->pos[k] + right[k] * sx + up[k] * sy;
    }
}

void fx_draw(const float eye[3]) {
    int sheet, i, k;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDepthMask(GU_TRUE);    /* test against the world, don't write */
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuColor(0xffffffff);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);

    {
        int pass;
        for (pass = 0; pass < 2; ++pass) {
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, pass ? GU_FIX : GU_ONE_MINUS_SRC_ALPHA, 0, 0xffffffff);
            for (sheet = 0; sheet < FX_SHEET_COUNT+2; ++sheet) {
                FxVertex *v;
                int count = 0, n = 0;
                for (i = 0; i < MAX_SPRITES; ++i)
                    if (sprites[i].life > 0.0f && sprites[i].sheet == sheet && sprites[i].smoke != pass) ++count;
                if (!count) continue;
                v = sceGuGetMemory(sizeof(FxVertex) * 6 * count);
                for (i = 0; i < MAX_SPRITES; ++i) {
                    const Sprite *s = &sprites[i];
                    float c[4][3], u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f, fade;
                    if (s->life <= 0.0f || s->sheet != sheet || s->smoke == pass) continue;
                    if (s->grid > 1) {
                        float cell = 1.0f / (float)s->grid;
                        u0 = (float)(s->cell % s->grid) * cell;
                        v0 = (float)(s->cell / s->grid) * cell;
                        u1 = u0 + cell;
                        v1 = v0 + cell;
                    }
                    if(s->native)vg_fx_uv(s,&u0,&v0,&u1,&v1);
                    sprite_corners(s, eye, c);
                    fade = s->native?1.0f:s->life / s->total;
                    put_quad(v + n, c, u0, v0, u1, v1, scale_alpha(s->color, fade));
                    n += 6;
                }
                bind(sheet);
                sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
            }
        }
    }

    mtrail_draw();
    /* The blade's sweep. Textured (the game's trail model's texture, added): u runs along it from the newest sample,
     * fading with age. Else a pale fan, solid at the tip, gone at the hilt and with age. */
    for (k = 0; k < 2; ++k) {
        int count = trails[k].count;
        if (count < 2) continue;
        if (trails[k].tex >= 0 && nfx_bind_texture(trails[k].bank, trails[k].tex)) {
            FxVertex *v = sceGuGetMemory(sizeof(FxVertex) * 2 * count);
            int n = 0;
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
            for (i = 0; i < count; ++i) {
                float a = 1.0f - trails[k].p[i].age / TRAIL_SECONDS, t = (float)i / (float)(TRAIL_POINTS - 1);
                float u = trails[k].u0 + (trails[k].u1 - trails[k].u0) * t;
                unsigned int c = scale_alpha(trails[k].color, a < 0.0f ? 0.0f : a);
                v[n++] = (FxVertex){u, trails[k].v0, c, trails[k].p[i].base[0], trails[k].p[i].base[1], trails[k].p[i].base[2]};
                v[n++] = (FxVertex){u, trails[k].v1, c, trails[k].p[i].tip[0], trails[k].p[i].tip[1], trails[k].p[i].tip[2]};
            }
            sceGumDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
        } else {
            FxPlainVertex *v = sceGuGetMemory(sizeof(FxPlainVertex) * 2 * count);
            int n = 0;
            for (i = 0; i < count; ++i) {
                float a = 1.0f - trails[k].p[i].age / TRAIL_SECONDS;
                if (a < 0.0f) a = 0.0f;
                v[n++] = (FxPlainVertex){scale_alpha(trails[k].color, a * 0.15f), trails[k].p[i].base[0], trails[k].p[i].base[1], trails[k].p[i].base[2]};
                v[n++] = (FxPlainVertex){scale_alpha(trails[k].color, a), trails[k].p[i].tip[0], trails[k].p[i].tip[1], trails[k].p[i].tip[2]};
            }
            sceGuDisable(GU_TEXTURE_2D);
            sceGumDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
            sceGuEnable(GU_TEXTURE_2D);
        }
    }

    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
}

/* Soft dark discs on the floor, all in one draw: n records of {x, y, z, radius, alpha 0-255}. Untextured: a solid
 * middle out to half the radius, then a ramp to nothing at the rim, in the vertex colours (the textured disc drew as
 * nothing in PPSSPP, and this needs no texture bound). 17 vertices and 24 triangles each. Drawn with a depth bias a
 * little over the stage's decals' (stage.c DECAL_DEPTH_OFFSET 64): the floors' grime layers are pushed toward the
 * camera by that much, which hid a disc 3 cm over the floor (Dante's own shadow never showed). */
#define SHADOW_SIDES 8
typedef struct { unsigned int color; float x, y, z; } ShadowVertex;
void fx_draw_shadows(const float *s, int n) {
    static float ring[SHADOW_SIDES][2];
    static unsigned short pattern[SHADOW_SIDES * 9];
    ShadowVertex *v;
    unsigned short *idx;
    int i, k, per = 1 + 2 * SHADOW_SIDES;
    if (n <= 0) return;
    if (!pattern[1]) {
        int o = 0;
        for (k = 0; k < SHADOW_SIDES; ++k) {
            int j = (k + 1) % SHADOW_SIDES;
            ring[k][0] = cosf(6.2831853f * k / SHADOW_SIDES); ring[k][1] = sinf(6.2831853f * k / SHADOW_SIDES);
            /* the middle (0), the inner ring (1..), the rim (1 + SIDES..) */
            pattern[o++] = 0; pattern[o++] = 1 + k; pattern[o++] = 1 + j;
            pattern[o++] = 1 + k; pattern[o++] = 1 + SHADOW_SIDES + k; pattern[o++] = 1 + SHADOW_SIDES + j;
            pattern[o++] = 1 + k; pattern[o++] = 1 + SHADOW_SIDES + j; pattern[o++] = 1 + j;
        }
    }
    v = sceGuGetMemory(sizeof(ShadowVertex) * per * n);
    idx = sceGuGetMemory(sizeof(unsigned short) * SHADOW_SIDES * 9 * n);
    for (i = 0; i < n; ++i, s += 5) {
        ShadowVertex *o = v + per * i;
        float x = s[0], y = s[1] + 3.0f, z = s[2], r = s[3];
        unsigned int dark = (unsigned int)s[4] << 24;
        o[0] = (ShadowVertex){dark, x, y, z};
        for (k = 0; k < SHADOW_SIDES; ++k) {
            o[1 + k] = (ShadowVertex){dark, x + ring[k][0] * r * 0.5f, y, z + ring[k][1] * r * 0.5f};
            o[1 + SHADOW_SIDES + k] = (ShadowVertex){0u, x + ring[k][0] * r, y, z + ring[k][1] * r};
        }
        for (k = 0; k < SHADOW_SIDES * 9; ++k) idx[SHADOW_SIDES * 9 * i + k] = (unsigned short)(per * i + pattern[k]);
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_SMOOTH);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDepthOffset(96);
    sceGumDrawArray(GU_TRIANGLES, GU_INDEX_16BIT | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    SHADOW_SIDES * 9 * n, idx, v);
    sceGuDepthOffset(0);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuEnable(GU_TEXTURE_2D);
}

void fx_draw_shadow(float x, float y, float z, float radius, unsigned int alpha) {
    float s[5] = {x, y, z, radius, (float)alpha};
    fx_draw_shadows(s, 1);
}

void fx_screen_tint(unsigned int color) {
    FxPlainVertex *v = sceGuGetMemory(sizeof(FxPlainVertex) * 2);
    v[0] = (FxPlainVertex){color, 0.0f, 0.0f, 0.0f};
    v[1] = (FxPlainVertex){color, 480.0f, 272.0f, 0.0f};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
}

/* Quicksilver's world on the real game: a pale negative of the picture. Drawn after the stage and
 * enemies, before Dante. As a source factor, GU_ONE_MINUS_SRC_COLOR means one minus the destination. */
void fx_screen_negative(void) {
    FxPlainVertex *v = sceGuGetMemory(sizeof(FxPlainVertex) * 4);
    v[0] = (FxPlainVertex){0xffffffffu, 0.0f, 0.0f, 0.0f};
    v[1] = (FxPlainVertex){0xffffffffu, 480.0f, 272.0f, 0.0f};
    v[2] = (FxPlainVertex){0xa0b0a8a0u, 0.0f, 0.0f, 0.0f};
    v[3] = (FxPlainVertex){0xa0b0a8a0u, 480.0f, 272.0f, 0.0f};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_ONE_MINUS_SRC_COLOR, GU_FIX, 0, 0);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v + 2);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
}
