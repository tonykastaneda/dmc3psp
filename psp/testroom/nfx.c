#include "nfx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>

#include "native_fx_runtime.h"

/* Record layouts, as em008's subset in vanguard_fx.inc reads them (00206f90, 001f12e0, 001f8ce0):
 *   V: u16 child count; children at 4 + 44 j: type (0 P, 1 E, 2 G, 3 V), u16 id at +2, i16 delay at +4,
 *      translation at +8, rotation in degrees at +20, scale at +32.
 *   E: kind at +1 (1 billboard, 2 placed quad, 5 model), u16 texture at +4, rect at +12, width +0x3c,
 *      height +0x44, spread +0x5c, u16 life +0x80 (0 = while its owner lasts), scale curves from +0x88,
 *      turn rates +0x144 (degrees a frame), colour keys from +0x174, u16 model at +0x28. */
#define MAX_SEQ 128          /* the orbs alone can hold 96 */
#define MAX_EL 320
#define PI 3.14159265f

typedef struct { float m[3][3], p[3]; } Frame;
typedef struct {
    unsigned short gen;
    unsigned char active, bank, loop, tinted, killed;
    short parent;
    const unsigned char *v;
    float age;
    unsigned int fired;
    float pos[3], yaw, scale;
    float tr[3], rot[3], sc[3];
    unsigned char tint[3];
    int refs;
} Seq;
typedef struct {
    unsigned char active, bank;
    short seq;
    const unsigned char *e;
    float age, life;
    float tr[3], rot[3], sc[3];
} El;
typedef struct { float u, v; unsigned int color; float x, y, z; } SpriteVertex;

static Seq seqs[MAX_SEQ];
static El els[MAX_EL];
static unsigned short generation = 1;
static unsigned int seed = 0x2545f491u;

static const NfxBank *bank_of(int b) { return b == NFX_CERBERUS ? &nfx1 : &nfx0; }
static float frand(void) { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; }
static unsigned int rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int rs16(const unsigned char *p) { return (short)rd16(p); }
static float rdf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }

static const unsigned char *find(const NfxRecord *r, int n, int id) {
    int i;
    for (i = 0; i < n; ++i) if (r[i].id == id) return r[i].data;
    return 0;
}

void nfx_reset(void) { memset(seqs, 0, sizeof(seqs)); memset(els, 0, sizeof(els)); }

static int new_seq(int bank, const unsigned char *v, int parent) {
    int i;
    if (!v) return -1;
    for (i = 0; i < MAX_SEQ; ++i) if (!seqs[i].active) {
        Seq *s = &seqs[i];
        memset(s, 0, sizeof(*s));
        s->active = 1; s->bank = (unsigned char)bank; s->v = v; s->parent = (short)parent;
        s->gen = generation++;
        if (!generation) generation = 1;
        s->scale = 1.0f; s->sc[0] = s->sc[1] = s->sc[2] = 1.0f;
        if (parent >= 0) {
            s->loop = seqs[parent].loop;
            ++seqs[parent].refs;
        }
        return i;
    }
    return -1;
}
static int handle_of(int i) { return i < 0 ? 0 : (int)(seqs[i].gen << 8 | (unsigned)(i + 1)); }
static Seq *from_handle(int h) {
    int i = (h & 0xff) - 1;
    if (h <= 0 || i < 0 || i >= MAX_SEQ || !seqs[i].active || seqs[i].gen != (unsigned short)(h >> 8)) return 0;
    return &seqs[i];
}
static const Seq *root_of(const Seq *s) { while (s->parent >= 0) s = &seqs[s->parent]; return s; }

static void place(float *tr, float *rot, float *sc, const unsigned char *child) {
    int k;
    for (k = 0; k < 3; ++k) {
        tr[k] = rdf(child + 8 + k * 4);
        rot[k] = rdf(child + 20 + k * 4) * (PI / 180.0f);
        sc[k] = rdf(child + 32 + k * 4);
    }
}
static void add_element(int bank, const unsigned char *e, int seq, const unsigned char *child) {
    int i, k;
    if (!e) return;
    for (i = 0; i < MAX_EL && els[i].active; ++i) {}
    if (i >= MAX_EL) return;
    memset(&els[i], 0, sizeof(els[i]));
    els[i].active = 1; els[i].bank = (unsigned char)bank; els[i].seq = (short)seq; els[i].e = e;
    els[i].life = (float)rd16(e + 0x80);
    if (child) place(els[i].tr, els[i].rot, els[i].sc, child);
    else els[i].sc[0] = els[i].sc[1] = els[i].sc[2] = 1.0f;
    /* The element's random spread, in the child's scale (as vanguard_fx.inc). */
    for (k = 0; k < 3; ++k) els[i].tr[k] += (frand() - 0.5f) * rdf(e + 0x5c + k * 4) * els[i].sc[k];
    ++seqs[seq].refs;
}

int nfx_spawn(int bank, int v_id, const float pos[3], float yaw, int loop) {
    const NfxBank *b = bank_of(bank);
    int i = new_seq(bank, find(b->v, b->v_count, v_id), -1);
    if (i < 0) return 0;
    memcpy(seqs[i].pos, pos, sizeof(seqs[i].pos));
    seqs[i].yaw = yaw; seqs[i].loop = (unsigned char)loop;
    return handle_of(i);
}
int nfx_spawn_e(int bank, int e_id, const float pos[3], float yaw, int loop) {
    /* A one-child timeline of our own: the V record format with a single E child at delay 0. */
    static unsigned char single[MAX_SEQ][48];
    const NfxBank *b = bank_of(bank);
    const unsigned char *e = find(b->e, b->e_count, e_id);
    int i;
    if (!e) return 0;
    for (i = 0; i < MAX_SEQ && seqs[i].active; ++i) {}
    if (i >= MAX_SEQ) return 0;
    memset(single[i], 0, sizeof(single[i]));
    single[i][0] = 1; single[i][4] = 1; single[i][6] = (unsigned char)e_id; single[i][7] = (unsigned char)(e_id >> 8);
    { float one = 1.0f; int k; for (k = 0; k < 3; ++k) memcpy(single[i] + 4 + 32 + k * 4, &one, 4); }
    i = new_seq(bank, single[i], -1);
    memcpy(seqs[i].pos, pos, sizeof(seqs[i].pos));
    seqs[i].yaw = yaw; seqs[i].loop = (unsigned char)loop;
    return handle_of(i);
}
void nfx_move(int h, const float pos[3], float yaw) {
    Seq *s = from_handle(h);
    if (s) { memcpy(s->pos, pos, sizeof(s->pos)); s->yaw = yaw; }
}
void nfx_scale(int h, float scale) { Seq *s = from_handle(h); if (s) s->scale = scale; }
void nfx_tint(int h, unsigned int r, unsigned int g, unsigned int b) {
    Seq *s = from_handle(h);
    if (s) { s->tinted = 1; s->tint[0] = (unsigned char)r; s->tint[1] = (unsigned char)g; s->tint[2] = (unsigned char)b; }
}
void nfx_kill(int h) { Seq *s = from_handle(h); if (s) s->killed = 1; }
int nfx_alive(int h) { return from_handle(h) != 0; }
int nfx_live_elements(void) { int i, n = 0; for (i = 0; i < MAX_EL; ++i) n += els[i].active; return n; }

static void release(int seq) {
    Seq *s = &seqs[seq];
    if (--s->refs < 0) s->refs = 0;
}

void nfx_update(float frames) {
    int i, j;
    for (i = 0; i < MAX_SEQ; ++i) {
        Seq *s = &seqs[i];
        int count;
        if (!s->active) continue;
        if (root_of(s)->killed) continue;
        s->age += frames;
        count = (int)rd16(s->v);
        for (j = 0; j < count && j < 32; ++j) {
            const unsigned char *child = s->v + 4 + j * 44;
            if (s->fired & (1u << j) || s->age <= (float)rs16(child + 4)) continue;
            s->fired |= 1u << j;
            if (child[0] == 1) {
                const NfxBank *b = bank_of(s->bank);
                add_element(s->bank, find(b->e, b->e_count, (int)rd16(child + 2)), i, child);
            } else if (child[0] == 3) {
                const NfxBank *b = bank_of(s->bank);
                int n = new_seq(s->bank, find(b->v, b->v_count, (int)rd16(child + 2)), i);
                if (n >= 0) place(seqs[n].tr, seqs[n].rot, seqs[n].sc, child);
            }
        }
    }
    for (i = 0; i < MAX_EL; ++i) {
        El *e = &els[i];
        const Seq *root;
        if (!e->active) continue;
        root = root_of(&seqs[e->seq]);
        if (root->killed) { e->active = 0; release(e->seq); continue; }
        e->age += frames;
        if (e->life > 0.0f && e->age >= e->life) {
            if (root->loop) e->age = fmodf(e->age, e->life);
            else { e->active = 0; release(e->seq); }
        }
    }
    /* A timeline ends once all its children have started and nothing it made is still showing. */
    for (i = 0; i < MAX_SEQ; ++i) {
        Seq *s = &seqs[i];
        int count, done;
        if (!s->active) continue;
        count = (int)rd16(s->v);
        done = s->fired == (count >= 32 ? 0xffffffffu : (1u << count) - 1u);
        if (root_of(s)->killed) done = 1;
        if (done && !s->refs && (!s->loop || root_of(s)->killed)) {
            s->active = 0;
            if (s->parent >= 0) release(s->parent);
        }
    }
}

/* ---- Placement ---------------------------------------------------------- */
static void frame_mul(Frame *out, const Frame *a, const Frame *b) {
    Frame t;
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
static void frame_local(Frame *f, const float tr[3], const float rot[3], const float sc[3]) {
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
static void seq_frame(const Seq *s, Frame *out) {
    if (s->parent >= 0) {
        Frame parent, local;
        seq_frame(&seqs[s->parent], &parent);
        frame_local(&local, s->tr, s->rot, s->sc);
        frame_mul(out, &parent, &local);
    } else {
        float c = cosf(s->yaw), sn = sinf(s->yaw);
        memset(out, 0, sizeof(*out));
        out->m[0][0] = c * s->scale; out->m[0][2] = sn * s->scale;
        out->m[1][1] = s->scale;
        out->m[2][0] = -sn * s->scale; out->m[2][2] = c * s->scale;
        memcpy(out->p, s->pos, sizeof(out->p));
    }
}

/* ---- Curves (001f12e0, 001f8ce0) ----------------------------------------- */
static float curve(const unsigned char *e, int axis, float age) {
    float value = rdf(e + 0xa8 + axis * 4);
    int i;
    if (!e[0x88 + axis]) return value;
    for (i = 0; i < 4; ++i) {
        int duration = rs16(e + 0x8e + i * 6 + axis * 2);
        float target = rdf(e + 0xb4 + i * 12 + axis * 4);
        if (duration < 0) return value;
        if (duration > 0 && age < (float)duration) return value + (target - value) * age / (float)duration;
        age -= (float)duration;
        value = target;
    }
    return value;
}
static unsigned int colour(const unsigned char *e, float age, const Seq *root) {
    const unsigned char *a = e + 0x174, *b = a;
    float t = 0.0f;
    unsigned int out = 0;
    int i, k;
    for (i = 0; i < 3; ++i) {
        int duration = rs16(a);
        b = a + 20;
        /* A key without a duration holds (E46, the orb, has only one). */
        if (duration <= 0) { b = a; break; }
        if (age < (float)duration) { t = age / (float)duration; break; }
        age -= (float)duration;
        a = b;
        t = 0.0f;
    }
    for (k = 0; k < 4; ++k) {
        float v = ((float)a[4 + k] + ((float)b[4 + k] - (float)a[4 + k]) * t) * 2.0f;
        if (k < 3 && root->tinted) v = (float)root->tint[k] * 2.0f;
        out |= (unsigned int)fmaxf(0.0f, fminf(255.0f, v)) << (k * 8);
    }
    return out;
}

/* ---- Drawing ------------------------------------------------------------ */
static const NfxTexture *texture(const NfxBank *b, int id) {
    int i;
    for (i = 0; i < b->texture_count; ++i) if (b->textures[i].id == id) return &b->textures[i];
    return 0;
}
static const NfxTexture *bound;
static void bind(const NfxTexture *t) {
    if (t == bound) return;
    bound = t;
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(32, t->palette);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, t->width, t->height, t->width, t->pixels);
    sceGuTexFlush();
}
static void element_frame(const El *e, Frame *out) {
    Frame f, local;
    seq_frame(&seqs[e->seq], &f);
    frame_local(&local, e->tr, e->rot, e->sc);
    frame_mul(out, &f, &local);
}
static float column(const Frame *f, int c) {
    return sqrtf(f->m[0][c] * f->m[0][c] + f->m[1][c] * f->m[1][c] + f->m[2][c] * f->m[2][c]);
}

static void draw_sprite(const El *el, const Frame *f, const float eye[3], unsigned int color) {
    const unsigned char *e = el->e;
    const NfxTexture *t = texture(bank_of(el->bank), (int)rd16(e + 4));
    float hw = rdf(e + 0x3c) * 0.5f * curve(e, 0, el->age), hh = rdf(e + 0x44) * 0.5f * curve(e, 1, el->age);
    float c[4][3], u0, v0, u1, v1;
    SpriteVertex *v;
    static const int corner[6] = {0, 1, 2, 2, 1, 3};
    int i, k;
    if (!t || (color >> 24) == 0) return;
    u0 = (float)rd16(e + 12) / (float)t->width; v0 = (float)rd16(e + 14) / (float)t->height;
    u1 = u0 + (float)rd16(e + 16) / (float)t->width; v1 = v0 + (float)rd16(e + 18) / (float)t->height;
    if (e[1] == 2) {
        /* Placed: a quad in its frame's XZ plane (E153's frost lies on the floor at rotation 0), turning
         * about its normal at its +0x148 rate. */
        float a = rdf(e + 0x148) * el->age * (PI / 180.0f), ca = cosf(a), sa = sinf(a);
        for (i = 0; i < 4; ++i) {
            float lx = (i & 1) ? hw : -hw, lz = (i & 2) ? -hh : hh, x = lx * ca + lz * sa, z = -lx * sa + lz * ca;
            for (k = 0; k < 3; ++k) c[i][k] = f->p[k] + f->m[k][0] * x + f->m[k][2] * z;
        }
    } else {
        /* Billboard, scaled by the frame and turned on screen at its +0x14c rate. */
        static const float up_world[3] = {0.0f, 1.0f, 0.0f};
        float view[3] = {f->p[0] - eye[0], f->p[1] - eye[1], f->p[2] - eye[2]}, r0[3], u0v[3], l, right[3], up[3];
        float a = el->rot[2] + rdf(e + 0x14c) * el->age * (PI / 180.0f), ca = cosf(a), sa = sinf(a);
        hw *= column(f, 0); hh *= column(f, 1);
        r0[0] = up_world[1] * view[2] - up_world[2] * view[1];
        r0[1] = up_world[2] * view[0] - up_world[0] * view[2];
        r0[2] = up_world[0] * view[1] - up_world[1] * view[0];
        l = sqrtf(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]); if (l < 1e-4f) l = 1.0f;
        for (k = 0; k < 3; ++k) r0[k] /= l;
        u0v[0] = view[1] * r0[2] - view[2] * r0[1];
        u0v[1] = view[2] * r0[0] - view[0] * r0[2];
        u0v[2] = view[0] * r0[1] - view[1] * r0[0];
        l = sqrtf(u0v[0] * u0v[0] + u0v[1] * u0v[1] + u0v[2] * u0v[2]); if (l < 1e-4f) l = 1.0f;
        for (k = 0; k < 3; ++k) { u0v[k] /= l; right[k] = r0[k] * ca + u0v[k] * sa; up[k] = u0v[k] * ca - r0[k] * sa; }
        for (i = 0; i < 4; ++i) {
            float sx = (i & 1) ? hw : -hw, sy = (i & 2) ? -hh : hh;
            for (k = 0; k < 3; ++k) c[i][k] = f->p[k] + right[k] * sx + up[k] * sy;
        }
    }
    bind(t);
    v = sceGuGetMemory(sizeof(SpriteVertex) * 6);
    for (i = 0; i < 6; ++i) {
        const float *p = c[corner[i]];
        v[i].u = (corner[i] & 1) ? u1 : u0; v[i].v = (corner[i] & 2) ? v1 : v0;
        v[i].color = color; v[i].x = p[0]; v[i].y = p[1]; v[i].z = p[2];
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, v);
}

static void draw_model(const El *el, const Frame *f, unsigned int color) {
    const unsigned char *e = el->e;
    const NfxBank *b = bank_of(el->bank);
    int id = (int)rd16(e + 0x28), i;
    const NfxModel *m = 0;
    ScePspFMatrix4 mat;
    Frame turned;
    float s[3];
    for (i = 0; i < b->model_count; ++i) if (b->models[i].id == id) m = &b->models[i];
    if (!m || m->texture < 0 || (color >> 24) == 0) return;
    for (i = 0; i < 3; ++i) s[i] = curve(e, i, el->age);
    {
        /* Its turn rates (+0x144, degrees a frame): E140, the ice sphere, rolls 10 a frame. */
        static const float zero[3] = {0.0f, 0.0f, 0.0f}, one[3] = {1.0f, 1.0f, 1.0f};
        float turn[3];
        Frame r;
        for (i = 0; i < 3; ++i) turn[i] = rdf(e + 0x144 + i * 4) * el->age * (PI / 180.0f);
        frame_local(&r, zero, turn, one);
        frame_mul(&turned, f, &r);
        f = &turned;
    }
    mat.x.x = f->m[0][0] * s[0]; mat.x.y = f->m[1][0] * s[0]; mat.x.z = f->m[2][0] * s[0]; mat.x.w = 0.0f;
    mat.y.x = f->m[0][1] * s[1]; mat.y.y = f->m[1][1] * s[1]; mat.y.z = f->m[2][1] * s[1]; mat.y.w = 0.0f;
    mat.z.x = f->m[0][2] * s[2]; mat.z.y = f->m[1][2] * s[2]; mat.z.z = f->m[2][2] * s[2]; mat.z.w = 0.0f;
    mat.w.x = f->p[0]; mat.w.y = f->p[1]; mat.w.z = f->p[2]; mat.w.w = 1.0f;
    bind(&b->textures[m->texture]);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&mat);
    sceGuColor(color);
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                    m->index_count, m->indices, m->vertices);
}

void nfx_draw(const float eye[3]) {
    int pass, i;
    bound = 0;
    sceGuDepthMask(GU_TRUE);    /* test against the world, don't write */
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    /* Models first, blended over the world; then the additive sprites over everything. */
    for (pass = 0; pass < 2; ++pass) {
        if (pass == 0) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        for (i = 0; i < MAX_EL; ++i) {
            const El *el = &els[i];
            Frame f;
            unsigned int color;
            int model;
            if (!el->active) continue;
            model = el->e[1] == 5;
            if (model != (pass == 0)) continue;
            element_frame(el, &f);
            color = colour(el->e, el->age, root_of(&seqs[el->seq]));
            if (model) {
                /* +0x1d or +0x56 set: added like the sprites (claw slashes, launch and icicle flashes). Inferred
                 * from which elements are glows; the GS blend setup itself isn't read. */
                int glow = el->e[0x1d] || el->e[0x56];
                if (glow) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
                draw_model(el, &f, color);
                if (glow) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            } else {
                sceGuColor(0xffffffffu);
                if (i == 0 || pass == 1) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
                draw_sprite(el, &f, eye, color);
            }
        }
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuColor(0xffffffffu);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
}
