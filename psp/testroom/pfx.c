/* The game's P effects (particle programs), from their P records. Decoded from the PS2 code; docs/NATIVE_FX.md,
 * "P effects", has the record layout and the functions each part comes from.
 *
 * A P record is an offset table (FUN_001b3bf0): its parameter block, and sub-records ("fake" particles, CFPtcl*) that
 * draw the same particles again in a frame and colours of their own. Block +1 picks the class (FUN_001b45e0):
 *   1 CPtclPoly00: 16 triangles (shards, or streaks along their velocity)      FUN_001ba340 / FUN_001bb310
 *   2 CPtclPoly01: a flat ring growing outwards, 8 segments of a quarter turn    FUN_001bda50 / FUN_001bdc40
 *   3 CPtclSprt00: 12 sprites cut from a sheet by an A record                    FUN_001bc050 / FUN_001bca90
 *   4 CPtclLine01: 12 streaks, two lines each, along their velocity              FUN_001f66d0 / FUN_001f6e40
 *   0 CPtclLine00: 24 streaks, one line each                                    FUN_001b55c0 / FUN_001b5cb0
 *   5 CPtclLine02 isn't ported (no pack the port loads uses it).
 * An instance is one burst: its particles all start together (from a hollow box, FUN_001b4990), move, slow and fall
 * for the block's life, then it ends (or starts again, as the game's +0x98, while a looping owner lasts). */
#include "pfx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>

#include "nfx.h"

#define MAX_INST 96   /* demo 8 (Lady's missiles) peaks past 64 */
#define MAX_PARTS 24
#define PI 3.14159265f

typedef struct {
    unsigned char active, bank, type, count, billboard, loop, colours;
    short parent;                 /* a fake: its main instance; -1 for a main one */
    unsigned short gen, parent_gen;
    const unsigned char *rec, *p; /* the P record and its parameter block */
    const unsigned char *sub;     /* a fake: its sub-record */
    int owner, root;
    PfxFrame place, last;         /* about the owner; the owner's last frame */
    float life;
    float tr[3], rot[3], sc[3];   /* the emitter (+0xd0 / +0xe0 / +0xf0), moved by its rates (FUN_001b38c0) */
    float drag[3];                /* +0xb00 (sprites) / +0xac0: speed lost a frame, itself shrinking */
    float hw, hh;                 /* a sprite's half size (+0xac0..); a ring's outer and inner radius (+0x990) */
    const unsigned char *keys;    /* colour keys (+0x444, FUN_001f8ce0) */
    unsigned char kdone, kidx;
    float ktimer;
    unsigned char col[16];        /* PS2 colours, 0x80 = 1.0, one a corner */
    const unsigned char *atlas;   /* the A record (FUN_001ec2f0) and its frame */
    int tex;
    short frame;
    float atimer;
    float pos[MAX_PARTS][9];      /* sprites: the position; streaks: the head; triangles: their three corners */
    float vel[MAX_PARTS][3];
} Inst;

static Inst inst[MAX_INST];
static unsigned short generation = 1;
static int enabled = 1, started, dropped, peak;
void pfx_enable(int on) { enabled = on; }
int pfx_started(void) { return started; }
int pfx_dropped(void) { return dropped; }
int pfx_peak(void) { return peak; }
static unsigned int seed = 0x9e3779b9u;

static float frand(void) { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; }
static unsigned int irand(void) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }
static unsigned int rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int rs16(const unsigned char *p) { return (short)rd16(p); }
static float rdf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }
static int rd32(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static float s16f(const unsigned char *p, float div) { return (float)rs16(p) / div; }

void pfx_reset(void) { memset(inst, 0, sizeof(inst)); }

/* ---- Frames (as nfx.c's) ------------------------------------------------- */
static void frame_mul(PfxFrame *out, const PfxFrame *a, const PfxFrame *b) {
    PfxFrame t;
    int i, j;
    for (i = 0; i < 3; ++i) {
        t.p[i] = a->p[i];
        for (j = 0; j < 3; ++j) {
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
            t.p[i] += a->m[i][j] * b->p[j];
        }
    }
    *out = t;
}
static void frame_local(PfxFrame *f, const float tr[3], const float rot[3], const float sc[3]) {
    float cx = cosf(rot[0]), sx = sinf(rot[0]), cy = cosf(rot[1]), sy = sinf(rot[1]), cz = cosf(rot[2]), sz = sinf(rot[2]);
    float r[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
                     {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
                     {-sy, cy * sx, cy * cx}};
    int i, j;
    for (i = 0; i < 3; ++i) {
        f->p[i] = tr[i];
        for (j = 0; j < 3; ++j) f->m[i][j] = r[i][j] * sc[j];
    }
}
static void apply(const PfxFrame *f, const float *v, float *out) {
    int k;
    for (k = 0; k < 3; ++k) out[k] = f->p[k] + f->m[k][0] * v[0] + f->m[k][1] * v[1] + f->m[k][2] * v[2];
}
static float column(const PfxFrame *f, int c) {
    return sqrtf(f->m[0][c] * f->m[0][c] + f->m[1][c] * f->m[1][c] + f->m[2][c] * f->m[2][c]);
}

/* ---- Colour keys (FUN_001f8e40 / FUN_001f8ce0) ---------------------------- */
/* Four keys of 0x14 bytes: an s16 time, then four RGBA corners. Then +0x50 on, +0x51 loop, +0x52 how many colours
 * (DAT_00590010: 1, 2 or 4; 1 fills every corner, 2 puts the second on corners 1 and 2). */
static void keys_lerp(Inst *s, float t, int n) {
    static const int out_count[3] = {1, 2, 4};
    const unsigned char *k = s->keys, *a = k + s->kidx * 0x14, *b = k + ((s->kidx + 1) & 3) * 0x14;
    int m = k[0x52] < 3 ? out_count[k[0x52]] : 4, c, i;
    if (n > m) n = m;
    for (c = 0; c < n; ++c)
        for (i = 0; i < 4; ++i) s->col[c * 4 + i] = (unsigned char)(int)((float)a[4 + c * 4 + i] + t * (float)((int)b[4 + c * 4 + i] - (int)a[4 + c * 4 + i]));
    if (m == 2) { memcpy(s->col + 12, s->col, 4); memcpy(s->col + 8, s->col + 4, 4); }
    else if (m == 1) { memcpy(s->col + 4, s->col, 4); memcpy(s->col + 8, s->col, 4); memcpy(s->col + 12, s->col, 4); }
}
static void keys_start(Inst *s, const unsigned char *keys) {
    s->keys = keys;
    s->kidx = 0; s->kdone = 0;
    s->ktimer = (float)rs16(keys);
    memcpy(s->col, keys + 4, 16);
    /* key 0 as the colour count says (one colour fills every corner): the game ticks the colour before it first draws,
     * so a fresh burst never shows key 0's unused corners (white in Nausika's P443) */
    keys_lerp(s, 0.0f, 4);
}
static void keys_step(Inst *s, float dt, int n) {
    const unsigned char *k = s->keys;
    int dur = rs16(k + s->kidx * 0x14);
    if (!k[0x50] || (k[0x51] != 1 && s->kdone)) return;
    keys_lerp(s, dur ? 1.0f - s->ktimer / (float)dur : 1.0f, n);
    s->ktimer -= dt;
    while (s->ktimer <= 0.0f) {
        int d;
        if (s->kidx + 1 < 3) ++s->kidx;
        else { s->kidx = 0; s->kdone = 1; }
        d = rs16(k + s->kidx * 0x14);
        if (d <= 0 && s->ktimer <= -1000.0f) break;   /* guard against keys without time */
        s->ktimer += (float)(d > 0 ? d : 1);
    }
}

/* ---- Atlas frames (FUN_001ec360 / FUN_001ec210) ---------------------------- */
/* An A record: +2 frames a picture, +3 the last picture, +4 loop, +5 where a loop restarts; pictures from +6, 10 bytes
 * each: x, y, width, height (s16), then a centre (s8 x2). */
static void atlas_step(Inst *s, float dt) {
    const unsigned char *a = s->atlas;
    if (!a || !a[2]) return;
    s->atimer -= dt;
    while (s->atimer <= 0.0f) {
        if (s->frame < (short)a[3]) ++s->frame;
        else if (a[4] == 1) s->frame = a[5];
        else { s->atimer = 0.0f; s->atlas = 0; return; }   /* the last picture holds (FUN_001ec210 returns 0) */
        s->atimer += (float)a[2];
    }
}

/* ---- Starting ------------------------------------------------------------ */
static int sub_count(const unsigned char *rec, const unsigned char *p) {
    (void)rec;
    return (int)rd16(p + 0xc0);
}
static const unsigned char *sub_at(const unsigned char *rec, int i) {
    return rec + 0x10 + rd32(rec + 0x10 + 4 * (2 + i));
}
/* A point of a hollow box (FUN_001b4990): q inside the inner half sizes is pushed out to a face picked at random. */
static void hollow(float q[3], const float inner[3], const float box[3], const float r[3]) {
    int c, axis, tries;
    if (!(fabsf(q[0]) <= inner[0] && fabsf(q[1]) <= inner[1] && fabsf(q[2]) <= inner[2])) return;
    c = (int)(irand() % 6u);
    for (tries = 0; tries < 6 && box[c % 3] * 0.5f <= inner[c % 3]; ++tries) c = (c + 1) % 6;
    if (tries == 6) return;
    axis = c % 3;
    if (c < 3) q[axis] = (0.5f - r[axis]) * inner[axis] + (r[axis] + 0.5f) * (box[axis] * 0.5f);
    else q[axis] = -(r[axis] + 0.5f) * (box[axis] * 0.5f) - (0.5f - r[axis]) * inner[axis];
}
/* One particle's start: a point of the box and a speed out from the middle plus a base speed. The game's code takes
 * q away again: vel = base + q * speed / max(box, 1) - q (checked in the MIPS, 0x1bc954). */
static void emit_point(const unsigned char *p, int box_at, int inner_at, int speed_at, int base_at, int clamp_box,
                       float q[3], float vel[3]) {
    float box[3], inner[3], r[3];
    int k;
    for (k = 0; k < 3; ++k) {
        box[k] = s16f(p + box_at + 2 * k, 16.0f);
        inner[k] = s16f(p + inner_at + 2 * k, 16.0f) * 0.5f;
        r[k] = frand() - 0.5f;
        q[k] = box[k] * r[k];
    }
    hollow(q, inner, box, r);
    for (k = 0; k < 3; ++k) {
        float b = clamp_box ? (box[k] > 1.0f ? box[k] : 1.0f) : (box[k] != 0.0f ? box[k] : 1.0f);
        vel[k] = s16f(p + base_at + 2 * k, 16.0f) + q[k] * (s16f(p + speed_at + 2 * k, 16.0f) / b) - q[k];
    }
}
static void normal_of(const float v[3], float n[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    int k;
    for (k = 0; k < 3; ++k) n[k] = l > 1e-6f ? v[k] / l : 0.0f;
}
static void picture(Inst *s, const unsigned char *p) {
    s->atlas = nfx_record(s->bank, 'A', rs16(p + 0xf6));
    s->tex = rs16(p + 0xd0);
    s->frame = 0;
    s->atimer = s->atlas ? (float)s->atlas[2] : 0.0f;
    if (s->atlas && p[0x118] == 1) s->frame = (short)(irand() % (unsigned int)(s->atlas[3] + 1));
}
static void start_main(Inst *s) {
    const unsigned char *p = s->p;
    int i, k;
    for (k = 0; k < 3; ++k) { s->tr[k] = rdf(p + 0x20 + 4 * k); s->rot[k] = rdf(p + 0x30 + 4 * k); s->sc[k] = rdf(p + 0x40 + 4 * k); }
    s->life = (float)rd32(p + 0x68);
    keys_start(s, p + 0x6c);
    switch (s->type) {
    case 3:   /* sprites */
        s->count = 12; s->colours = 4;
        s->billboard = p[0x50] == 1;
        picture(s, p);
        for (k = 0; k < 3; ++k) s->drag[k] = rdf(p + 0xe4 + 4 * k);
        s->hw = (float)rd16(p + 0xf8) / 16.0f; s->hh = (float)rd16(p + 0xfa) / 16.0f;
        for (i = 0; i < s->count; ++i) emit_point(p, 0xf0, 0x106, 0xd2, 0xde, 1, s->pos[i], s->vel[i]);
        break;
    case 1:   /* triangles */
        s->count = 16; s->colours = 3;
        for (k = 0; k < 3; ++k) s->drag[k] = rdf(p + 0xf8 + 4 * k);
        for (i = 0; i < s->count; ++i) {
            float q[3], n[3], *v = s->pos[i];
            emit_point(p, 0xe4, 0xf0, 0xd8, 0xde, p[0xea] == 1, q, s->vel[i]);
            memcpy(v + 6, q, sizeof(q));
            if (p[0xea] == 1) {
                float jit = s16f(p + 0xec, 16.0f);
                for (k = 0; k < 6; ++k) v[k] = q[k % 3] + jit * (frand() - 0.5f);
            } else {
                float a = s16f(p + 0xd4, 16.0f), b = s16f(p + 0xd6, 16.0f);
                normal_of(s->vel[i], n);
                for (k = 0; k < 3; ++k) v[k] = q[k] + n[k] * a;
                /* the second corner: x and z along the normal by +0xd6, y by +0xd4 (as the code reads) */
                v[3] = q[0] + n[0] * b; v[4] = q[1] + n[1] * a; v[5] = q[2] + n[2] * b;
            }
        }
        break;
    case 2:   /* ring */
        s->count = 16; s->colours = 3;
        s->hw = rdf(p + 0xd0); s->hh = rdf(p + 0xd4);
        break;
    case 0:   /* single-line streaks: head and a tail +0xd4 behind it */
        s->count = 24; s->colours = 2;
        for (i = 0; i < s->count; ++i) {
            float n[3], a = s16f(p + 0xd4, 16.0f);
            emit_point(p, 0xe2, 0xea, 0xd6, 0xdc, 1, s->pos[i], s->vel[i]);
            normal_of(s->vel[i], n);
            for (k = 0; k < 3; ++k) s->pos[i][k] += n[k] * a;
        }
        break;
    case 4:   /* streaks */
        s->count = 12; s->colours = 2;
        for (i = 0; i < s->count; ++i) {
            float n[3], a = s16f(p + 0xd4, 16.0f);
            emit_point(p, 0xe4, 0xec, 0xd8, 0xde, 1, s->pos[i], s->vel[i]);
            normal_of(s->vel[i], n);
            for (k = 0; k < 3; ++k) s->pos[i][k] += n[k] * a;   /* the head, +0xd4 ahead of the start */
        }
        break;
    }
}
static void start_fake(Inst *s) {
    const unsigned char *u = s->sub;
    const Inst *m = &inst[s->parent];
    int k;
    for (k = 0; k < 3; ++k) { s->tr[k] = rdf(u + 4 * k); s->rot[k] = rdf(u + 0x10 + 4 * k); s->sc[k] = rdf(u + 0x20 + 4 * k); }
    keys_start(s, u + 0x48);
    s->count = m->count; s->colours = m->colours; s->billboard = m->billboard;
    if (s->type == 3) picture(s, s->p);
}

static int new_inst(void) {
    int i;
    for (i = 0; i < MAX_INST; ++i) if (!inst[i].active) {
        memset(&inst[i], 0, sizeof(inst[i]));
        inst[i].active = 1;
        inst[i].gen = generation++;
        if (!generation) generation = 1;
        inst[i].parent = -1;
        return i;
    }
    return -1;
}

int pfx_spawn(int bank, const unsigned char *rec, const PfxFrame *place, int owner, int root, int loop) {
    const unsigned char *p;
    int i, j, n, type;
    if (!enabled || !rec || rd32(rec) != 2) return 0;
    p = rec + 0x10 + rd32(rec + 0x10);
    type = p[1];
    if (type > 4) return 0;   /* Line02: not ported */
    n = sub_count(rec, p);
    i = new_inst();
    if (i < 0) { ++dropped; return 0; }
    inst[i].bank = (unsigned char)bank; inst[i].type = (unsigned char)type; inst[i].rec = rec; inst[i].p = p;
    inst[i].owner = owner; inst[i].root = root; inst[i].loop = (unsigned char)loop;
    inst[i].place = *place;
    if (!owner || !nfx_frame(owner, inst[i].last.m, inst[i].last.p)) {
        memset(&inst[i].last, 0, sizeof(inst[i].last));
        inst[i].last.m[0][0] = inst[i].last.m[1][1] = inst[i].last.m[2][2] = 1.0f;
        inst[i].owner = 0;
    }
    start_main(&inst[i]);
    ++started;
    { int live = pfx_live(); if (live > peak) peak = live; }
    for (j = 0; j < n && j < 8; ++j) {
        int f = new_inst();
        if (f < 0) break;
        inst[f] = inst[i];
        inst[f].gen = generation++;
        if (!generation) generation = 1;
        inst[f].parent = (short)i;
        inst[f].parent_gen = inst[i].gen;
        inst[f].sub = sub_at(rec, j);
        start_fake(&inst[f]);
    }
    return 1;
}

/* A main instance ends: its owner's timeline is let go (nfx_unref). Its fakes go on their next update. */
static void end_inst(Inst *s) {
    if (s->parent < 0 && s->owner) nfx_unref(s->owner);
    s->active = 0;
}
void pfx_kill_root(int root) {
    int i;
    if (!root) return;
    for (i = 0; i < MAX_INST; ++i) if (inst[i].active && inst[i].root == root) end_inst(&inst[i]);
}

/* ---- Running --------------------------------------------------------------- */
/* FUN_001b3b70: each speed goes towards 0 by the drag, without passing it. */
static void slow(float v[3], const float d[3]) {
    int k;
    for (k = 0; k < 3; ++k) {
        float o = v[k];
        if (o == 0.0f) continue;
        v[k] = o < 0.0f ? o + d[k] : o - d[k];
        if (v[k] * o < 0.0f) v[k] = 0.0f;
    }
}
/* FUN_001b38c0: the emitter's rates, at +0x52 (translation /16), +0x58 (turn, 1/65536 of a circle), +0x5e (scale
 * /4096); a fake's at its sub-record's +0x30 / +0x36 / +0x3c. */
static void emitter_step(Inst *s, const unsigned char *rates, float dt) {
    int k;
    for (k = 0; k < 3; ++k) {
        float a;
        s->tr[k] += s16f(rates + 2 * k, 16.0f) * dt;
        a = s->rot[k] + s16f(rates + 6 + 2 * k, 65536.0f) * 6.283185f * dt;
        if (a >= PI) a -= 2.0f * PI;
        if (a < -PI) a += 2.0f * PI;
        s->rot[k] = a;
        s->sc[k] += s16f(rates + 12 + 2 * k, 4096.0f) * dt;
    }
}
/* Gravity a frame (0, g, 0), in the emitter's axes; with the flag, along the world's (the owner's turn undone). */
static void gravity(const Inst *s, float g, int world, float out[3]) {
    out[0] = 0.0f; out[1] = g; out[2] = 0.0f;
    if (world) {
        const PfxFrame *o = &s->last;
        float c[3];
        int k;
        for (k = 0; k < 3; ++k) {
            float l = column(o, k);
            c[k] = l > 1e-6f ? (o->m[0][k] * out[0] + o->m[1][k] * out[1] + o->m[2][k] * out[2]) / (l * l) : 0.0f;
        }
        memcpy(out, c, sizeof(c));
    }
}
static void tick_main(Inst *s, float dt) {
    const unsigned char *p = s->p;
    float g[3], d[3];
    int i, k;
    emitter_step(s, p + 0x52, dt);
    switch (s->type) {
    case 3:
        keys_step(s, dt, 4);
        gravity(s, rdf(p + 0xfc) * dt, p[0x100] == 1, g);
        s->hw += s16f(p + 0x102, 2048.0f) * dt;
        s->hh += s16f(p + 0x104, 2048.0f) * dt;
        for (k = 0; k < 3; ++k) { d[k] = s->drag[k] * dt; s->drag[k] -= rdf(p + 0x10c + 4 * k) * dt; }
        for (i = 0; i < s->count; ++i) {
            slow(s->vel[i], d);
            for (k = 0; k < 3; ++k) { s->vel[i][k] += g[k]; s->pos[i][k] += s->vel[i][k] * dt; }
        }
        if (p[0x118] == 0) atlas_step(s, dt);
        break;
    case 1:
        keys_step(s, dt, 3);
        gravity(s, rdf(p + 0xd0) * dt, p[0xee] == 1, g);
        for (k = 0; k < 3; ++k) { d[k] = s->drag[k] * dt; s->drag[k] -= rdf(p + 0x104 + 4 * k) * dt; }
        for (i = 0; i < s->count; ++i) {
            float *v = s->pos[i], step[3], n[3];
            for (k = 0; k < 3; ++k) step[k] = s->vel[i][k] * dt;
            if (p[0xea] == 1) {
                for (k = 0; k < 9; ++k) v[k] += step[k % 3];
            } else {
                float a = s16f(p + 0xd4, 16.0f), b = s16f(p + 0xd6, 16.0f);
                for (k = 0; k < 3; ++k) v[k] += step[k];
                normal_of(s->vel[i], n);
                for (k = 0; k < 3; ++k) v[6 + k] = v[k] - n[k] * a;
                v[3] = v[6] + n[0] * b; v[4] = v[7] + n[1] * a; v[5] = v[8] + n[2] * b;
            }
            slow(s->vel[i], d);
            for (k = 0; k < 3; ++k) s->vel[i][k] += g[k];
        }
        break;
    case 2:
        keys_step(s, dt, 3);
        s->hw += rdf(p + 0xd8) * dt;
        s->hh += rdf(p + 0xdc) * dt;
        break;
    case 0:
        keys_step(s, dt, 2);
        gravity(s, rdf(p + 0xd0) * dt, p[0xe8] == 1, g);
        for (i = 0; i < s->count; ++i)
            for (k = 0; k < 3; ++k) { s->pos[i][k] += s->vel[i][k] * dt; s->vel[i][k] += g[k]; }
        break;
    case 4:
        keys_step(s, dt, 2);
        gravity(s, rdf(p + 0xd0) * dt, p[0xea] == 1, g);
        for (i = 0; i < s->count; ++i)
            for (k = 0; k < 3; ++k) { s->pos[i][k] += s->vel[i][k] * dt; s->vel[i][k] += g[k]; }
        break;
    }
}

void pfx_update(float frames) {
    int i;
    for (i = 0; i < MAX_INST; ++i) {
        Inst *s = &inst[i];
        if (!s->active) continue;
        if (s->owner) {
            PfxFrame f;
            if (nfx_frame(s->owner, f.m, f.p)) s->last = f;
            else { s->owner = 0; s->loop = 0; }
        }
        if (s->parent >= 0) {
            const Inst *m = &inst[s->parent];
            if (!m->active || m->gen != s->parent_gen) { s->active = 0; continue; }
            s->last = m->last;
            emitter_step(s, s->sub + 0x30, frames);
            keys_step(s, frames, s->colours);
            if (s->type == 3 && s->p[0x118] == 0) atlas_step(s, frames);
            continue;
        }
        /* a life under -10000 never runs out (the game counts it down only from -10000 up): Nevan's bats' -20000 */
        if (s->life >= -10000.0f) s->life -= frames;
        if (s->life < 0.0f && s->life >= -10000.0f) {
            if (s->loop) {
                /* +0x98: it starts again (state 2, the class's start once more), the fakes with it */
                int j;
                start_main(s);
                for (j = 0; j < MAX_INST; ++j) if (inst[j].active && inst[j].parent == i) start_fake(&inst[j]);
            } else {
                int j;
                end_inst(s);
                for (j = 0; j < MAX_INST; ++j) if (inst[j].active && inst[j].parent == i) inst[j].active = 0;
            }
            continue;
        }
        tick_main(s, frames);
    }
}

int pfx_live(void) { int i, n = 0; for (i = 0; i < MAX_INST; ++i) n += inst[i].active; return n; }
int pfx_live_prims(void) {
    int i, n = 0;
    for (i = 0; i < MAX_INST; ++i) if (inst[i].active) n += inst[i].type == 4 ? inst[i].count * 2 : inst[i].count;
    return n;
}

/* ---- Drawing --------------------------------------------------------------- */
typedef struct { float u, v; unsigned int color; float x, y, z; } TexVertex;
typedef struct { unsigned int color; float x, y, z; } ColVertex;

static unsigned int psp_colour(const unsigned char *c) {
    unsigned int out = 0;
    int k;
    for (k = 0; k < 4; ++k) { int v = c[k] * 2; out |= (unsigned int)(v > 255 ? 255 : v) << (k * 8); }
    return out;
}
/* +0x64: bit 0 blends (the GS's ABE); its second byte is the GS ALPHA register: 0x44 (Cs - Cd) As + Cd, 0x48
 * Cs As + Cd. 0x41, (Cd - Cs) As + Cd, has no PSP equation: it takes Cd - Cs As [stand-in]. */
static void blend(const unsigned char *p) {
    unsigned int flags = (unsigned int)rd32(p + 0x64), alpha = flags >> 8 & 0xff;
    if (!(flags & 1)) { sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0); return; }
    if (alpha == 0x48) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
    else if ((alpha & 0x3) == 1) sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
    else sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}
/* The instance's frame: the owner's, then its place, then the emitter (FUN_001b2dc0). Billboard emitters (sprites'
 * +0x50) face the camera at the owner's point instead of taking its turn. */
static void world_frame(const Inst *s, const float eye[3], PfxFrame *out) {
    PfxFrame owner, em;
    frame_mul(&owner, &s->last, &s->place);
    if (s->billboard) {
        float f[3] = {eye[0] - owner.p[0], eye[1] - owner.p[1], eye[2] - owner.p[2]}, r[3], u[3], l;
        int k;
        normal_of(f, f);
        r[0] = f[2]; r[1] = 0.0f; r[2] = -f[0];   /* up (0, 1, 0) x f */
        l = sqrtf(r[0] * r[0] + r[2] * r[2]);
        if (l < 1e-4f) { r[0] = 1.0f; r[2] = 0.0f; l = 1.0f; }
        r[0] /= l; r[2] /= l;
        u[0] = f[1] * r[2] - f[2] * r[1]; u[1] = f[2] * r[0] - f[0] * r[2]; u[2] = f[0] * r[1] - f[1] * r[0];
        for (k = 0; k < 3; ++k) {
            float sc = column(&owner, k);
            owner.m[k][0] = r[k] * sc; owner.m[k][1] = u[k] * sc; owner.m[k][2] = f[k] * sc;
        }
    }
    frame_local(&em, s->tr, s->rot, s->sc);
    frame_mul(out, &owner, &em);
}

static void draw_sprites(const Inst *s, const Inst *m, const PfxFrame *f, const float eye[3]) {
    const unsigned char *a = s->atlas ? s->atlas : nfx_record(s->bank, 'A', rs16(s->p + 0xf6));
    const unsigned char *pic;
    int w, h, i, k, frame = s->frame;
    float u0, v0, u1, v1, sx, sy;
    unsigned int col[4];
    TexVertex *v;
    static const int corner[6] = {0, 1, 2, 2, 1, 3};
    if (!a || !nfx_texture_size(s->bank, s->tex, &w, &h) || !nfx_bind_texture(s->bank, s->tex)) return;
    if (frame > a[3]) frame = a[3];
    pic = a + 6 + 10 * frame;
    u0 = (float)rs16(pic) / (float)w; v0 = (float)rs16(pic + 2) / (float)h;
    u1 = ((float)rs16(pic) + (float)(rs16(pic + 4) - 1)) / (float)w;
    v1 = ((float)rs16(pic + 2) + (float)(rs16(pic + 6) - 1)) / (float)h;
    for (k = 0; k < 4; ++k) col[k] = psp_colour(s->col + 4 * k);
    if (!((col[0] | col[1] | col[2] | col[3]) >> 24)) return;
    sx = m->hw * column(f, 0); sy = m->hh * column(f, 1);
    v = sceGuGetMemory(sizeof(TexVertex) * 6 * m->count);
    for (i = 0; i < m->count; ++i) {
        float c[3], view[3], r[3], up[3], l, q[4][3];
        apply(f, m->pos[i], c);
        for (k = 0; k < 3; ++k) view[k] = c[k] - eye[k];
        r[0] = view[2]; r[1] = 0.0f; r[2] = -view[0];
        l = sqrtf(r[0] * r[0] + r[2] * r[2]);
        if (l < 1e-4f) { r[0] = 1.0f; r[2] = 0.0f; l = 1.0f; }
        r[0] /= l; r[2] /= l;
        up[0] = view[1] * r[2] - view[2] * r[1]; up[1] = view[2] * r[0] - view[0] * r[2]; up[2] = view[0] * r[1] - view[1] * r[0];
        l = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (l < 1e-4f) l = 1.0f;
        for (k = 0; k < 3; ++k) up[k] /= l;
        /* corners in the strip's order, the sheet's top (v0) at the screen's top, as nfx's sprites */
        for (k = 0; k < 3; ++k) {
            q[0][k] = c[k] - r[k] * sx + up[k] * sy; q[1][k] = c[k] + r[k] * sx + up[k] * sy;
            q[2][k] = c[k] - r[k] * sx - up[k] * sy; q[3][k] = c[k] + r[k] * sx - up[k] * sy;
        }
        for (k = 0; k < 6; ++k) {
            TexVertex *o = &v[i * 6 + k];
            int n = corner[k];
            o->u = (n & 1) ? u1 : u0; o->v = (n & 2) ? v1 : v0;
            o->color = col[n];
            o->x = q[n][0]; o->y = q[n][1]; o->z = q[n][2];
        }
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6 * m->count, 0, v);
}
static void draw_untextured(const Inst *s, const Inst *m, const PfxFrame *f) {
    unsigned int col[4];
    ColVertex *v;
    int i, k, n = 0, prim;
    for (k = 0; k < 4; ++k) col[k] = psp_colour(s->col + 4 * k);
    if (!((col[0] | col[1] | col[2] | col[3]) >> 24)) return;
    if (m->type == 0) {
        /* one line a streak: head (c0) to tail (c1), +0xd4 behind along the speed */
        prim = GU_LINES;
        v = sceGuGetMemory(sizeof(ColVertex) * 2 * m->count);
        for (i = 0; i < m->count; ++i) {
            float nrm[3], tail[3], a = s16f(m->p + 0xd4, 16.0f);
            normal_of(m->vel[i], nrm);
            for (k = 0; k < 3; ++k) tail[k] = m->pos[i][k] - nrm[k] * a;
            v[n].color = col[0]; apply(f, m->pos[i], &v[n].x); ++n;
            v[n].color = col[1]; apply(f, tail, &v[n].x); ++n;
        }
    } else if (m->type == 4) {
        /* two lines a streak: head (c0) to tail (c1), head (c1) to front (c0) (FUN_001f6e40's four points) */
        prim = GU_LINES;
        v = sceGuGetMemory(sizeof(ColVertex) * 4 * m->count);
        for (i = 0; i < m->count; ++i) {
            float nrm[3], pt[4][3];
            float a = s16f(m->p + 0xd4, 16.0f), b = s16f(m->p + 0xd6, 16.0f);
            normal_of(m->vel[i], nrm);
            for (k = 0; k < 3; ++k) {
                float head = m->pos[i][k];
                pt[0][k] = head; pt[1][k] = head - nrm[k] * a; pt[2][k] = head; pt[3][k] = head + nrm[k] * b;
            }
            for (k = 0; k < 4; ++k) { v[n].color = col[k]; apply(f, pt[k], &v[n].x); ++n; }
        }
    } else if (m->type == 2) {
        /* the ring: 8 segments of PI/16 between the inner and outer radius, in the emitter's XZ plane */
        prim = GU_TRIANGLES;
        v = sceGuGetMemory(sizeof(ColVertex) * 6 * 8);
        for (i = 0; i < 8; ++i) {
            float a0 = (float)i * 0.19634953f, a1 = (float)(i + 1) * 0.19634953f;
            float p[6][3] = {{m->hh * cosf(a0), 0, m->hh * sinf(a0)}, {m->hw * cosf(a0), 0, m->hw * sinf(a0)},
                             {m->hw * cosf(a1), 0, m->hw * sinf(a1)}, {m->hw * cosf(a1), 0, m->hw * sinf(a1)},
                             {m->hh * cosf(a0), 0, m->hh * sinf(a0)}, {m->hh * cosf(a1), 0, m->hh * sinf(a1)}};
            static const int slot[6] = {0, 1, 2, 2, 0, 1};
            for (k = 0; k < 6; ++k) { v[n].color = col[slot[k]]; apply(f, p[k], &v[n].x); ++n; }
        }
    } else {
        prim = GU_TRIANGLES;
        v = sceGuGetMemory(sizeof(ColVertex) * 3 * m->count);
        for (i = 0; i < m->count; ++i)
            for (k = 0; k < 3; ++k) { v[n].color = col[k]; apply(f, &m->pos[i][3 * k], &v[n].x); ++n; }
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGumDrawArray(prim, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
}

void pfx_draw(const float eye[3]) {
    int i;
    if (!pfx_live()) return;
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuColor(0xffffffffu);
    for (i = 0; i < MAX_INST; ++i) {
        const Inst *s = &inst[i], *m;
        PfxFrame f;
        if (!s->active) continue;
        m = s->parent >= 0 ? &inst[s->parent] : s;
        world_frame(s, eye, &f);
        blend(m->p);
        if (m->type == 3) draw_sprites(s, m, &f, eye);
        else draw_untextured(s, m, &f);
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
}
