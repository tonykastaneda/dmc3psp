#include "motion.h"

#include <string.h>

#include "motion_runtime.h"

static Motion motions[DMC3_MOTION_COUNT];

static unsigned int rd16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p) { return rd16(p) | (rd16(p + 2) << 16); }
static float rdf(const unsigned char *p) { unsigned int u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static int parse(const Dmc3MotionBlob *blob, Motion *m) {
    const unsigned char *d = blob->data;
    unsigned int offset = 0x54, bone, bit;
    int bones;
    if (blob->size < 0x54 || rd32(d) != 0x50 || memcmp(d + 4, "MOT", 4) != 0) return 0;
    m->name = blob->name;
    m->loop_start = rdf(d + 0x08);
    m->end_frame = rdf(d + 0x0C);
    bones = (int)rd16(d + 0x1C);
    if (bones <= 0 || bones > MOTION_MAX_BONES) return 0;
    m->channel_count = 0;
    for (bone = 0; bone < (unsigned)bones; ++bone) {
        unsigned int mask = rd16(d + 0x1E + bone * 2);
        for (bit = 0; bit < 16; ++bit) {
            MotionChannel *c;
            unsigned int size, count, k;
            if (!(mask >> bit & 1)) continue;
            if (m->channel_count >= MOTION_MAX_CHANNELS || offset + 32 > blob->size) return 0;
            size = rd16(d + offset);
            count = rd16(d + offset + 2);
            if (rd32(d + offset + 4) != 3 || size != 32 + count * 8 || offset + size > blob->size || !count) return 0;
            if (bit < 3 || bit > 8) return 0; /* only rotation/translation are understood */
            c = &m->channels[m->channel_count++];
            c->bone = (unsigned short)bone;
            c->channel = (unsigned short)bit;
            c->key_count = (unsigned short)count;
            for (k = 0; k < 6; ++k) c->params[k] = rdf(d + offset + 8 + k * 4);
            c->keys = (const unsigned short *)(d + offset + 32); /* blob is 4-byte aligned */
            offset += size;
        }
    }
    return m->channel_count == (int)rd32(d + 0x50);
}

int motion_init(void) {
    int i;
    for (i = 0; i < DMC3_MOTION_COUNT; ++i) {
        if (!parse(&dmc3_motions[i], &motions[i])) return -(i + 1);
    }
    return DMC3_MOTION_COUNT;
}

int motion_count(void) { return DMC3_MOTION_COUNT; }

int motion_find(const char *name) {
    int i;
    for (i = 0; i < DMC3_MOTION_COUNT; ++i)
        if (strcmp(motions[i].name, name) == 0) return i;
    return -1;
}

const Motion *motion_get(int index) {
    if (index < 0 || index >= DMC3_MOTION_COUNT) return 0;
    return &motions[index];
}

float motion_wrap(const Motion *m, float frame) {
    float span = m->end_frame - m->loop_start;
    if (frame <= m->end_frame) return frame < 0.0f ? 0.0f : frame;
    if (span <= 0.0f) return m->end_frame;
    while (frame > m->end_frame) frame -= span;
    return frame;
}

#define KEY_FRAME(c, k) ((float)((c)->keys[(k) * 4] & 0x7FFF))
#define KEY_Q(c, k, n) ((float)(c)->keys[(k) * 4 + (n)] * (1.0f / 65535.0f))

/* Quantized Hermite: value/tangent = base + scale * q; tangents per frame. */
static float evaluate(const MotionChannel *c, float frame) {
    int k;
    if (frame <= KEY_FRAME(c, 0) || c->key_count == 1) return c->params[0] + c->params[1] * KEY_Q(c, 0, 1);
    for (k = 1; k < c->key_count; ++k) {
        float f1 = KEY_FRAME(c, k);
        if (frame <= f1) {
            float f0 = KEY_FRAME(c, k - 1);
            float span = f1 - f0;
            float t = (frame - f0) / span, t2 = t * t, t3 = t2 * t;
            float v0 = c->params[0] + c->params[1] * KEY_Q(c, k - 1, 1);
            float v1 = c->params[0] + c->params[1] * KEY_Q(c, k, 1);
            float out0 = c->params[4] + c->params[5] * KEY_Q(c, k - 1, 3);
            float in1 = c->params[2] + c->params[3] * KEY_Q(c, k, 2);
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + (t3 - 2.0f * t2 + t) * span * out0 +
                   (-2.0f * t3 + 3.0f * t2) * v1 + (t3 - t2) * span * in1;
        }
    }
    return c->params[0] + c->params[1] * KEY_Q(c, c->key_count - 1, 1);
}

void motion_sample(const Motion *m, float frame, MotionSample *out) {
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < m->channel_count; ++i) {
        const MotionChannel *c = &m->channels[i];
        float v = evaluate(c, frame);
        if (c->channel <= 5) { out->rot[c->bone][c->channel - 3] = v; out->has_rot[c->bone][c->channel - 3] = 1; }
        else { out->trans[c->bone][c->channel - 6] = v; out->has_trans[c->bone][c->channel - 6] = 1; }
    }
}

void motion_root(const Motion *m, float frame, float out[3]) {
    int i;
    out[0] = out[1] = out[2] = 0.0f;
    for (i = 0; i < m->channel_count; ++i) {
        const MotionChannel *c = &m->channels[i];
        if (c->bone == 0 && c->channel >= 6) out[c->channel - 6] = evaluate(c, frame);
    }
}
