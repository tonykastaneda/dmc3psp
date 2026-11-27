/* Host checks for pfx.c (P effects, docs/NATIVE_FX.md "P effects"): a synthetic CPtclSprt00 record's life, motion,
 * colour keys, hollow box and the owner reference. GPU calls are no-ops; rendering isn't tested.
 * Run: python tools/tests/run_pfx_logic.py */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pfx.c"

/* ---- nfx stand-ins ---- */
static int owner_alive = 1, unrefs;
const unsigned char *nfx_record(int bank, int kind, int id) { (void)bank; (void)kind; (void)id; return 0; }
int nfx_texture_size(int bank, int id, int *w, int *h) { (void)bank; (void)id; *w = *h = 64; return 1; }
int nfx_bind_texture(int bank, int id) { (void)bank; (void)id; return 1; }
int nfx_frame(int handle, float m[3][3], float p[3]) {
    (void)handle;
    if (!owner_alive) return 0;
    memset(m, 0, sizeof(float) * 9); m[0][0] = m[1][1] = m[2][2] = 1.0f;
    p[0] = 100.0f; p[1] = 0.0f; p[2] = 0.0f;
    return 1;
}
void nfx_unref(int handle) { (void)handle; ++unrefs; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put16(unsigned char *p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, int v) { memcpy(p, &v, 4); }
static void putf(unsigned char *p, float v) { memcpy(p, &v, 4); }

/* A record laid out as FUN_001b3bf0 reads it: u32 2, offsets from +0x10, the block at +0x20, no sub-records. */
static unsigned char rec[0x140] __attribute__((aligned(16)));
static unsigned char *make_sprite(int life, float gravity, int box16, int inner16) {
    unsigned char *p = rec + 0x20;
    int k;
    memset(rec, 0, sizeof(rec));
    put32(rec, 2);
    put32(rec + 0x10, 0x10); put32(rec + 0x14, 0x10);
    p[1] = 3;
    for (k = 0; k < 3; ++k) putf(p + 0x40 + 4 * k, 1.0f);   /* scale 1 */
    put32(p + 0x64, 0x4401);
    put32(p + 0x68, life);
    /* colour keys: 10 frames from grey (0x40) to white (0x80), on, no loop, one colour */
    put16(p + 0x6c, 10); memset(p + 0x6c + 4, 0x40, 16);
    put16(p + 0x6c + 0x14, 10); memset(p + 0x6c + 0x14 + 4, 0x80, 16);
    put16(p + 0x6c + 0x28, 10); memset(p + 0x6c + 0x28 + 4, 0x80, 16);
    put16(p + 0x6c + 0x3c, 10); memset(p + 0x6c + 0x3c + 4, 0x80, 16);
    p[0xbc] = 1; p[0xbd] = 0; p[0xbe] = 0;
    for (k = 0; k < 3; ++k) { put16(p + 0xf0 + 2 * k, box16); put16(p + 0x106 + 2 * k, inner16); }
    put16(p + 0xd2 + 2, 0); put16(p + 0xde + 2, 16);   /* base speed (0, 1, 0) */
    put16(p + 0xf8, 160); put16(p + 0xfa, 160);
    putf(p + 0xfc, gravity);
    return rec;
}

int main(void) {
    PfxFrame place;
    int i, k;
    memset(&place, 0, sizeof(place));
    place.m[0][0] = place.m[1][1] = place.m[2][2] = 1.0f;

    /* Palace P480 starts transparent, with unused bright corners. The very
     * first render must not see those unused corners before the first tick. */
    {
        unsigned char *record=(unsigned char *)make_sprite(20,0.0f,0,0);
        unsigned char *p=record+0x10+rd32(record+0x10);
        memset(p+0x6c+4,0xff,16);
        memset(p+0x6c+4,0,4);
        pfx_reset();pfx_spawn(0,record,&place,0,0,0);
        CHECK(inst[0].col[3]==0 && inst[0].col[7]==0 && inst[0].col[11]==0 && inst[0].col[15]==0,
              "fresh one-colour burst has transparent corners before update");
    }

    /* Life and the owner's reference. */
    pfx_reset();
    CHECK(pfx_spawn(0, make_sprite(20, 0.0f, 0, 0), &place, 7, 7, 0) == 1, "spawn");
    CHECK(pfx_live() == 1, "one instance, got %d", pfx_live());
    for (i = 0; i < 20; ++i) pfx_update(1.0f);
    CHECK(pfx_live() == 1, "alive at its last frame");
    pfx_update(1.0f);
    CHECK(pfx_live() == 0, "gone after its life");
    CHECK(unrefs == 1, "owner let go once, got %d", unrefs);

    /* Motion: base speed 1 a frame up, gravity -0.1 a frame, no box, no drag: y = sum of the speeds. */
    pfx_reset();
    pfx_spawn(0, make_sprite(100, -0.1f, 0, 0), &place, 0, 0, 0);
    for (i = 0; i < 10; ++i) pfx_update(1.0f);
    {
        float want = 0.0f, v = 1.0f;
        for (i = 0; i < 10; ++i) { v += -0.1f; want += v; }
        CHECK(fabsf(inst[0].pos[0][1] - want) < 1e-3f, "y %f want %f", inst[0].pos[0][1], want);
    }

    /* Colour: 5 frames into the first key, half way from 0x40 to 0x80. */
    pfx_reset();
    pfx_spawn(0, make_sprite(100, 0.0f, 0, 0), &place, 0, 0, 0);
    for (i = 0; i < 6; ++i) pfx_update(1.0f);
    CHECK(inst[0].col[0] >= 0x5e && inst[0].col[0] <= 0x62, "colour 0x%x", inst[0].col[0]);
    CHECK(inst[0].col[12] == inst[0].col[0], "one colour fills every corner");

    /* Hollow box (FUN_001b4990): a 4 cm box with a 2 cm inner one; every start is outside the inner half sizes. */
    for (k = 0; k < 20; ++k) {
        pfx_reset();
        pfx_spawn(0, make_sprite(100, 0.0f, 64, 32), &place, 0, 0, 0);
        for (i = 0; i < 12; ++i) {
            const float *q = inst[0].pos[i];
            CHECK(fabsf(q[0]) >= 1.0f - 1e-4f || fabsf(q[1]) >= 1.0f - 1e-4f || fabsf(q[2]) >= 1.0f - 1e-4f,
                  "particle %d inside the inner box (%f %f %f)", i, q[0], q[1], q[2]);
        }
    }

    /* A looping burst starts again while its owner lasts, and stops looping once it's gone. */
    pfx_reset();
    owner_alive = 1;
    pfx_spawn(0, make_sprite(5, 0.0f, 0, 0), &place, 9, 9, 1);
    for (i = 0; i < 30; ++i) pfx_update(1.0f);
    CHECK(pfx_live() == 1, "looping");
    owner_alive = 0;
    for (i = 0; i < 10; ++i) pfx_update(1.0f);
    CHECK(pfx_live() == 0, "a loop ends with its owner");

    /* nfx_kill on the root ends it at once. */
    pfx_reset();
    owner_alive = 1;
    pfx_spawn(0, make_sprite(100, 0.0f, 0, 0), &place, 3, 3, 0);
    pfx_kill_root(3);
    CHECK(pfx_live() == 0, "killed with its root");

    printf(failures ? "pfx_logic: %d failures\n" : "pfx_logic: ok\n", failures);
    return failures != 0;
}
