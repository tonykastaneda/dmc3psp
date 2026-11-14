#include "motion.h"

#include <stdlib.h>
#include <string.h>

#include "motion_runtime.h"
#include "vanguard_motions.h"
#include "boss_cerberus_motions.h"
#include "boss_vergil_motions.h"
#include "boss_leviathan_motions.h"

#define TOTAL_MOTIONS (DMC3_MOTION_COUNT + VANGUARD_MOTION_COUNT + CERBERUS_MOTION_COUNT + LEVIATHAN_MOTION_COUNT + VERGIL_MOTION_COUNT)
static Motion motions[TOTAL_MOTIONS];

static unsigned int rd16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p) { return rd16(p) | (rd16(p + 2) << 16); }
static float rdf(const unsigned char *p) { unsigned int u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static int parse(const Dmc3MotionBlob *blob, Motion *m) {
    const unsigned char *d = blob->data;
    unsigned int header, offset, bone, bit, declared;
    int skipped = 0;
    int bones;
    if (blob->size < 0x34 || memcmp(d + 4, "MOT", 4) != 0) return 0;
    header = rd32(d);   /* 0x30 / 0x40 for weapons' few bones, 0x50, or 0x60 for more than 24 */
    if (header < 0x30 || header > 0x80 || (header & 0xf) || blob->size < header + 4) return 0;   /* 0x80: the Enigma's 42 bones */
    declared = rd32(d + header);
    offset = header + 4;
    m->name = blob->name;
    m->loop_start = rdf(d + 0x08);
    m->end_frame = rdf(d + 0x0C);
    m->loop_from = rdf(d + 0x10);
    m->loop_to = rdf(d + 0x14);
    m->looping = (d[0x18] | (d[0x19] << 8)) == 1;
    bones = (int)rd16(d + 0x1C);
    if (bones <= 0 || bones > MOTION_MAX_BONES) return 0;
    m->channel_count = 0;
    m->channels = malloc(declared * sizeof(MotionChannel));
    if (!m->channels) return 0;
    for (bone = 0; bone < (unsigned)bones; ++bone) {
        unsigned int mask = rd16(d + 0x1E + bone * 2);
        for (bit = 0; bit < 16; ++bit) {
            MotionChannel *c;
            unsigned int size, count, k;
            if (!(mask >> bit & 1)) continue;
            if (offset + 16 > blob->size) return 0;
            unsigned int kind, record, stride;
            size = rd16(d + offset);
            count = rd16(d + offset + 2);
            kind = rd16(d + offset + 4);  /* the u16 after it is usually 0; see tools/mot_ps2.py */
            if (kind == 3) { record = 32; stride = 8; }
            else if (kind == 2) { record = 16; stride = 4; }  /* quantized linear (enemy motions) */
            else if (kind == 1) { record = 8; stride = 16; } /* direct Hermite (Cerberus) */
            else if (kind == 0) { record = 8; stride = 8; }   /* direct linear (Cerberus) */
            else return 0;
            if (size != record + count * stride || offset + size > blob->size || !count) return 0;
            if (bit < 3 || bit > 8) {
                /* Bits 0-2 (scale, seen on one bone of hl-38): not applied; skip the record. */
                ++skipped;
                offset += size;
                continue;
            }
            if ((unsigned)(m->channel_count + skipped) >= declared) return 0;
            c = &m->channels[m->channel_count++];
            c->bone = (unsigned short)bone;
            c->channel = (unsigned short)bit;
            c->key_count = (unsigned short)count;
            c->kind = (unsigned short)kind;
            for (k = 0; k < 6; ++k) c->params[k] = k < (kind == 3 ? 6u : 2u) ? rdf(d + offset + 8 + k * 4) : 0.0f;
            c->keys = kind >= 2 ? (const unsigned short *)(d + offset + record) : 0; /* blob is 4-byte aligned */
            c->direct_keys = kind < 2 ? d + offset + record : 0;
            offset += size;
        }
    }
    return m->channel_count + skipped == (int)declared;
}

int motion_init(void) {
    int i;
    for (i = 0; i < DMC3_MOTION_COUNT; ++i) {
        if (!parse(&dmc3_motions[i], &motions[i])) return -(i + 1);
    }
    for (i = 0; i < VANGUARD_MOTION_COUNT; ++i)
        if (!parse(&vg_motion_blobs[i], &motions[DMC3_MOTION_COUNT + i])) return -(DMC3_MOTION_COUNT + i + 1);
    for (i = 0; i < CERBERUS_MOTION_COUNT; ++i) {
        int at = DMC3_MOTION_COUNT + VANGUARD_MOTION_COUNT + i;
        if (!parse(&cerberus_motion_blobs[i], &motions[at])) return -(at + 1);
    }
    for (i = 0; i < LEVIATHAN_MOTION_COUNT; ++i) {
        int at = DMC3_MOTION_COUNT + VANGUARD_MOTION_COUNT + CERBERUS_MOTION_COUNT + i;
        if (!parse(&leviathan_motion_blobs[i], &motions[at])) return -(at + 1);
    }
    for (i = 0; i < VERGIL_MOTION_COUNT; ++i) {
        int at = DMC3_MOTION_COUNT + VANGUARD_MOTION_COUNT + CERBERUS_MOTION_COUNT + LEVIATHAN_MOTION_COUNT + i;
        if (!parse(&vergil_motion_blobs[i], &motions[at])) return -(at + 1);
    }
    return TOTAL_MOTIONS;
}

int motion_count(void) { return TOTAL_MOTIONS; }

int motion_find(const char *name) {
    int i;
    for (i = 0; i < TOTAL_MOTIONS; ++i)
        if (strcmp(motions[i].name, name) == 0) return i;
    return -1;
}

const Motion *motion_get(int index) {
    if (index < 0 || index >= TOTAL_MOTIONS) return 0;
    return &motions[index];
}

float motion_wrap(const Motion *m, float frame) {
    float span = m->end_frame - m->loop_start;
    if (frame <= m->end_frame) return frame < 0.0f ? 0.0f : frame;
    if (span <= 0.0f) return m->end_frame;
    while (frame > m->end_frame) frame -= span;
    return frame;
}

#define KEY_STRIDE(c) ((c)->kind == 3 ? 4 : 2)
#define DIRECT_STRIDE(c) ((c)->kind == 1 ? 16 : 8)
#define KEY_FRAME(c, k) ((c)->kind < 2 ? (float)(rd16((c)->direct_keys + (k) * DIRECT_STRIDE(c)) & 0x7FFF) : \
                         (float)((c)->keys[(k) * KEY_STRIDE(c)] & 0x7FFF))
#define KEY_Q(c, k, n) ((float)(c)->keys[(k) * KEY_STRIDE(c) + (n)] * (1.0f / 65535.0f))

/* n = 1 value, 2 incoming tangent, 3 outgoing tangent. Types 0/1 store all
 * three directly; types 2/3 quantize them around a base and scale. */
static float key_value(const MotionChannel *c, int k, int n) {
    if (c->kind < 2) return rdf(c->direct_keys + k * DIRECT_STRIDE(c) + n * 4);
    if (n == 1) return c->params[0] + c->params[1] * KEY_Q(c, k, 1);
    if (n == 2) return c->params[2] + c->params[3] * KEY_Q(c, k, 2);
    return c->params[4] + c->params[5] * KEY_Q(c, k, 3);
}

/* Hermite: value/tangent, tangents per frame. */
static float evaluate(const MotionChannel *c, float frame) {
    int k;
    if (frame <= KEY_FRAME(c, 0) || c->key_count == 1) return key_value(c, 0, 1);
    for (k = 1; k < c->key_count; ++k) {
        float f1 = KEY_FRAME(c, k);
        if (frame <= f1) {
            float f0 = KEY_FRAME(c, k - 1);
            float span = f1 - f0;
            float t = (frame - f0) / span, t2 = t * t, t3 = t2 * t;
            float v0 = key_value(c, k - 1, 1);
            float v1 = key_value(c, k, 1);
            float out0, in1;
            if (c->kind == 0 || c->kind == 2) return v0 + (v1 - v0) * t;  /* linear */
            out0 = key_value(c, k - 1, 3);
            in1 = key_value(c, k, 2);
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + (t3 - 2.0f * t2 + t) * span * out0 +
                   (-2.0f * t3 + 3.0f * t2) * v1 + (t3 - t2) * span * in1;
        }
    }
    return key_value(c, c->key_count - 1, 1);
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
        if (c->bone > 0) break;  /* in bone order */
        if (c->channel >= 6) out[c->channel - 6] = evaluate(c, frame);
    }
}
