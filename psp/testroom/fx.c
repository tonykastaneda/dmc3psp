#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>

#include "fx_runtime.h"
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

static struct { float base[3], tip[3], age; } trail[TRAIL_POINTS];
static int trail_count;
static unsigned int trail_color;
static int trail_fresh;
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
    trail_count = 0;
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
