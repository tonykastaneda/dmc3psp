#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>

#include "fx_runtime.h"

#define MAX_SPRITES 56
#define TRAIL_POINTS 14
#define TRAIL_SECONDS 0.14f

typedef enum { MODE_BILLBOARD, MODE_FLAT, MODE_BEAM } SpriteMode;

typedef struct {
    float pos[3], vel[3], end[3];
    float size0, size1, life, total, spin, angle, gravity;
    unsigned int color;
    unsigned char sheet, cell, grid, mode, smoke;
} Sprite;

typedef struct { float u, v; unsigned int color; float x, y, z; } FxVertex;
typedef struct { unsigned int color; float x, y, z; } FxPlainVertex;

static Sprite sprites[MAX_SPRITES];
static int next_sprite;
static unsigned int seed = 12345u;

static struct { float base[3], tip[3], age; } trail[TRAIL_POINTS];
static int trail_count;
static unsigned int trail_color;
static int trail_fresh;

static float fx_rand(void) {   /* 0..1 */
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 16) & 0x7fff) / 32767.0f;
}

void fx_reset(void) {
    memset(sprites, 0, sizeof(sprites));
    trail_count = 0;
}

static Sprite *spawn(int sheet, int grid, int cell, const float at[3], float size0, float size1, float life, unsigned int color) {
    Sprite *s = &sprites[next_sprite];
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

void fx_update(float dt) {
    int i, k;
    for (i = 0; i < MAX_SPRITES; ++i) {
        Sprite *s = &sprites[i];
        if (s->life <= 0.0f) continue;
        s->life -= dt;
        s->vel[1] -= s->gravity * dt;
        for (k = 0; k < 3; ++k) s->pos[k] += s->vel[k] * dt;
        s->angle += s->spin * dt;
    }
    for (i = 0; i < trail_count; ++i) trail[i].age += dt;
    while (trail_count > 0 && trail[trail_count - 1].age > TRAIL_SECONDS) --trail_count;
    trail_fresh = 0;
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

void fx_guard_flash(const float at[3]) {
    Sprite *s = spawn(FX_SHEET_GLOW, 4, 8, at, 40.0f, 150.0f, 0.18f, 0xffa0a0ffu);
    s->angle = 0.0f;
    spawn(FX_SHEET_FLARE, 2, 1, at, 70.0f, 40.0f, 0.16f, 0xffffffffu);
}

void fx_trail_add(const float base[3], const float tip[3], unsigned int color) {
    int i;
    if (trail_fresh) return;
    trail_fresh = 1;
    trail_color = color;
    if (trail_count < TRAIL_POINTS) ++trail_count;
    for (i = trail_count - 1; i > 0; --i) trail[i] = trail[i - 1];
    memcpy(trail[0].base, base, sizeof(trail[0].base));
    memcpy(trail[0].tip, tip, sizeof(trail[0].tip));
    trail[0].age = 0.0f;
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
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(256 / 8, fx_palettes[sheet]);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, FX_TEX_SIZE, FX_TEX_SIZE, FX_TEX_SIZE, fx_pixels[sheet]);
    sceGuTexFlush();
}

static void sprite_corners(const Sprite *s, const float eye[3], float c[4][3]) {
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
        float sx = (i & 1) ? size : -size, sy = (i & 2) ? -size : size;
        for (k = 0; k < 3; ++k) c[i][k] = s->pos[k] + right[k] * sx + up[k] * sy;
    }
}

void fx_draw(const float eye[3]) {
    int sheet, i;
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
            for (sheet = 0; sheet < FX_SHEET_COUNT; ++sheet) {
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
                    sprite_corners(s, eye, c);
                    fade = s->life / s->total;
                    put_quad(v + n, c, u0, v0, u1, v1, scale_alpha(s->color, fade));
                    n += 6;
                }
                bind(sheet);
                sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
            }
        }
    }

    /* The blade's sweep: a pale fan, solid at the tip, gone at the hilt and with age. */
    if (trail_count >= 2) {
        FxPlainVertex *v = sceGuGetMemory(sizeof(FxPlainVertex) * 2 * trail_count);
        int n = 0;
        for (i = 0; i < trail_count; ++i) {
            float k = 1.0f - trail[i].age / TRAIL_SECONDS;
            if (k < 0.0f) k = 0.0f;
            v[n++] = (FxPlainVertex){scale_alpha(trail_color, k * 0.15f), trail[i].base[0], trail[i].base[1], trail[i].base[2]};
            v[n++] = (FxPlainVertex){scale_alpha(trail_color, k), trail[i].tip[0], trail[i].tip[1], trail[i].tip[2]};
        }
        sceGuDisable(GU_TEXTURE_2D);
        sceGumDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
        sceGuEnable(GU_TEXTURE_2D);
    }

    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
}

void fx_draw_shadow(float x, float y, float z, float radius, unsigned int alpha) {
    FxVertex *v = sceGuGetMemory(sizeof(FxVertex) * 6);
    float c[4][3] = {{x - radius, y + 3.0f, z - radius}, {x + radius, y + 3.0f, z - radius},
                     {x - radius, y + 3.0f, z + radius}, {x + radius, y + 3.0f, z + radius}};
    static unsigned int disc[32 * 32] __attribute__((aligned(16)));
    if (!disc[16 * 32 + 16]) {
        /* A soft round alpha ramp, black. */
        int px, py;
        for (py = 0; py < 32; ++py)
            for (px = 0; px < 32; ++px) {
                float dx = ((float)px + 0.5f - 16.0f) / 16.0f, dy = ((float)py + 0.5f - 16.0f) / 16.0f;
                float d = 1.0f - sqrtf(dx * dx + dy * dy);
                disc[py * 32 + px] = (unsigned int)(255.0f * (d < 0.0f ? 0.0f : d > 0.5f ? 1.0f : d * 2.0f)) << 24;
            }
        sceKernelDcacheWritebackInvalidateAll();
    }
    put_quad(v, c, 0.0f, 0.0f, 1.0f, 1.0f, alpha << 24 | 0xffffffu);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuColor(0xffffffff);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 32, 32, 32, disc);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
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
