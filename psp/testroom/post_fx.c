/* The cutscenes' ScrEfc blur and glow on the GE (post_fx.h). Two 128 x 80 8888 buffers in the VRAM past the depth
 * buffer hold a quarter-size (120 x 68) copy of the frame; a blur pass redraws one into the other four times, a
 * texel off each way, at a quarter each, with the bilinear filter; the result goes back over the frame as
 * screen-size sprites. Everything is GE work: no CPU reads of VRAM. */
#include "post_fx.h"

#include <pspge.h>
#include <pspgu.h>

#define SCR_W 480
#define SCR_H 272
#define FB_STRIDE 512
#define Q_W 120
#define Q_H 68
#define Q_STRIDE 128
/* Past the two 512 x 272 8888 frame buffers and the 16-bit depth buffer (main.c: 0x154000 in all). */
#define Q_A ((void *)0x160000)
#define Q_B ((void *)0x16c000)

typedef struct { float u, v, x, y, z; } PostVertex;
#define POST_VTYPE (GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

static void *abs_vram(void *rel) { return (void *)((unsigned int)sceGeEdramGetAddr() + (unsigned int)rel); }

static void sprite(float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, float z) {
    PostVertex *v = (PostVertex *)sceGuGetMemory(2 * sizeof(PostVertex));
    v[0].u = u0; v[0].v = v0; v[0].x = x0; v[0].y = y0; v[0].z = z;
    v[1].u = u1; v[1].v = v1; v[1].x = x1; v[1].y = y1; v[1].z = z;
    sceGuDrawArray(GU_SPRITES, POST_VTYPE, 2, 0, v);
}

static void target(void *rel, int stride, int w, int h) {
    sceGuDrawBufferList(GU_PSM_8888, rel, stride);
    sceGuOffset(2048 - (w / 2), 2048 - (h / 2));
    sceGuViewport(2048, 2048, w, h);
    sceGuScissor(0, 0, w, h);
}

static void source(void *rel, int size, int stride) {
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, size, size, stride, abs_vram(rel));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
}

/* One blur pass, from one quarter buffer into the other: four taps a texel off diagonally, a quarter each. */
static void blur_pass(void *from, void *to) {
    static const float off[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
    int i;
    target(to, Q_STRIDE, Q_W, Q_H);
    sceGuClearColor(0);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    source(from, 128, Q_STRIDE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x404040, 0xffffff);
    for (i = 0; i < 4; ++i)
        sprite(off[i][0] * 0.75f, off[i][1] * 0.75f, Q_W + off[i][0] * 0.75f, Q_H + off[i][1] * 0.75f, 0, 0, Q_W, Q_H, 0);
}

/* The depth buffer's value for a view distance d (main.c: sceGuDepthRange(65535, 0), sceGumPerspective). */
static float depth_at(float d, float n, float f) {
    float ndc;
    if (d < n) d = n;
    ndc = (f + n) / (f - n) - 2.0f * f * n / ((f - n) * d);
    return 32767.5f - 32767.5f * ndc;
}

static unsigned int grey(float a) {
    unsigned int k = a <= 0.0f ? 0u : a >= 1.0f ? 255u : (unsigned int)(a * 255.0f);
    return k | k << 8 | k << 16;
}

void post_fx_draw(void *draw_buffer, const PostFx *fx) {
    int i, passes;
    void *a = Q_A, *b = Q_B, *t;
    if (!fx || (!fx->blur && !fx->glow)) return;
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuColor(0xffffffffu);
    sceGuDepthMask(GU_TRUE);

    /* The quarter-size copy (the bilinear filter averages as it shrinks). */
    target(a, Q_STRIDE, Q_W, Q_H);
    sceGuDisable(GU_BLEND);
    source(draw_buffer, 512, FB_STRIDE);
    sprite(0, 0, SCR_W, SCR_H, 0, 0, Q_W, Q_H, 0);

    if (fx->blur) {
        passes = fx->blur_passes < 1 ? 1 : fx->blur_passes > 3 ? 3 : fx->blur_passes;
        for (i = 0; i < passes; ++i) { blur_pass(a, b); t = a; a = b; b = t; }
        /* Back over the frame, only where it's farther than each distance: half beyond blur_from, all beyond blur_to. */
        target(draw_buffer, FB_STRIDE, SCR_W, SCR_H);
        source(a, 128, Q_STRIDE);
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(GU_GEQUAL);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, grey(0.5f), grey(0.5f));
        sprite(0, 0, Q_W, Q_H, 0, 0, SCR_W, SCR_H, depth_at(fx->blur_from, fx->near, fx->far));
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, grey(1.0f), grey(0.0f));
        sprite(0, 0, Q_W, Q_H, 0, 0, SCR_W, SCR_H, depth_at(fx->blur_to, fx->near, fx->far));
        sceGuDisable(GU_DEPTH_TEST);
        if (fx->glow) {   /* the glow is taken from the blurred frame then: a fresh copy */
            target(Q_A, Q_STRIDE, Q_W, Q_H);
            sceGuDisable(GU_BLEND);
            source(draw_buffer, 512, FB_STRIDE);
            sprite(0, 0, SCR_W, SCR_H, 0, 0, Q_W, Q_H, 0);
            a = Q_A; b = Q_B;
        }
    }
    if (fx->glow) {
        /* Its bright part: the copy less a level (dst - src, clamped at 0) [the threshold is the port's]. */
        target(a, Q_STRIDE, Q_W, Q_H);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0x707070, 0xffffff);
        sprite(0, 0, 0, 0, 0, 0, Q_W, Q_H, 0);
        sceGuEnable(GU_TEXTURE_2D);
        passes = fx->glow_passes < 1 ? 1 : fx->glow_passes > 3 ? 3 : fx->glow_passes;
        for (i = 0; i < passes; ++i) { blur_pass(a, b); t = a; a = b; b = t; }
        /* Added back, in the clip's colour at its Alpha (0x80 = 1). */
        target(draw_buffer, FB_STRIDE, SCR_W, SCR_H);
        source(a, 128, Q_STRIDE);
        sceGuEnable(GU_BLEND);
        {
            unsigned int c = fx->glow_color, r = c & 0xff, g = (c >> 8) & 0xff, bl = (c >> 16) & 0xff;
            float k = fx->glow_alpha * 2.0f;   /* colour 0x80 = 1.0, so twice the byte over 255 */
            r = (unsigned int)(r * k > 255.0f ? 255.0f : r * k);
            g = (unsigned int)(g * k > 255.0f ? 255.0f : g * k);
            bl = (unsigned int)(bl * k > 255.0f ? 255.0f : bl * k);
            sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, r | g << 8 | bl << 16, 0xffffff);
        }
        sprite(0, 0, Q_W, Q_H, 0, 0, SCR_W, SCR_H, 0);
    }
    /* As the frame had it for the 2D after. */
    target(draw_buffer, FB_STRIDE, SCR_W, SCR_H);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
}
