/* Crest switches (CStageSetSwitch) and seals (CStageSetSeal) drawn from the room's own models.
 *
 * tools/gimmicks.py switches_and_seals bakes them into the stage under their own tags (256 and up): a seal's board
 * as one tag, a switch's parts as eleven. This file poses the switch parts the way FUN_00306160 does and draws the
 * seal boards tinted and faded (FUN_0031d270's colours). The missions keep their own switch and seal logic and call
 * in here to show it. */
#include <math.h>
#include <string.h>

#include "gimmick_models.h"
#include "stage.h"

#define GIM_MAX 24
typedef struct { short kind, number, tag, attached, door; float o[3], r[9]; } Gim;
static Gim gims[GIM_MAX];
static int gim_count;

void gim_clear(void) { gim_count = 0; }

void gim_add(int kind, int number, int tag, int attached, int door, const float o[3], const float r[9]) {
    Gim *g;
    int p;
    if (gim_count >= GIM_MAX) return;
    g = &gims[gim_count++];
    g->kind = (short)kind; g->number = (short)number; g->tag = (short)tag; g->attached = (short)attached; g->door = (short)door;
    memcpy(g->o, o, sizeof g->o);
    memcpy(g->r, r, sizeof g->r);
    if (kind == GIM_SEAL) stage_set_group_mode(tag, 3);   /* drawn by gim_seal_draw only */
    else if (attached) for (p = 0; p < GIM_SWITCH_PARTS; ++p) stage_set_group_mode(tag + p, 2);   /* until placed */
}

static const Gim *find(int kind, int number) {
    int i;
    for (i = 0; i < gim_count; ++i) if (gims[i].kind == kind && gims[i].number == number) return &gims[i];
    return 0;
}
int gim_has_switch(int number) { return find(GIM_SWITCH, number) != 0; }
int gim_has_seal(int number) { return find(GIM_SEAL, number) != 0; }
int gim_switch_number(int k) {
    int i;
    for (i = 0; i < gim_count; ++i) if (gims[i].kind == GIM_SWITCH && k-- == 0) return gims[i].number;
    return -1;
}
int gim_seal_at_door(int door) {
    int i;
    for (i = 0; i < gim_count; ++i) if (gims[i].kind == GIM_SEAL && gims[i].door == door) return gims[i].number;
    return -1;
}

/* 3 x 4 row-major matrices: a = b * c. */
static void mul(float *a, const float *b, const float *c) {
    float t[12];
    int i, k;
    for (i = 0; i < 3; ++i) {
        for (k = 0; k < 3; ++k) t[i * 4 + k] = b[i * 4] * c[k] + b[i * 4 + 1] * c[4 + k] + b[i * 4 + 2] * c[8 + k];
        t[i * 4 + 3] = b[i * 4] * c[3] + b[i * 4 + 1] * c[7] + b[i * 4 + 2] * c[11] + b[i * 4 + 3];
    }
    memcpy(a, t, sizeof t);
}
static void place(float *m, const float o[3], const float r[9]) {
    int i;
    for (i = 0; i < 3; ++i) { m[i * 4] = r[i * 3]; m[i * 4 + 1] = r[i * 3 + 1]; m[i * 4 + 2] = r[i * 3 + 2]; m[i * 4 + 3] = o[i]; }
}
/* The inverse of a turn-and-move. */
static void unplace(float *m, const float o[3], const float r[9]) {
    int i;
    for (i = 0; i < 3; ++i) {
        m[i * 4] = r[i]; m[i * 4 + 1] = r[3 + i]; m[i * 4 + 2] = r[6 + i];
        m[i * 4 + 3] = -(r[i] * o[0] + r[3 + i] * o[1] + r[6 + i] * o[2]);
    }
}
static void turn(float *m, int axis, float deg) {
    float a = deg * 0.0174532925f, c = cosf(a), s = sinf(a);
    memset(m, 0, sizeof(float) * 12);
    m[0] = m[5] = m[10] = 1.0f;
    if (axis == 1) { m[0] = c; m[2] = s; m[8] = -s; m[10] = c; }
    else if (axis == 2) { m[0] = c; m[1] = -s; m[4] = s; m[5] = c; }
}

void gim_switch_pose(int number, float spin, float wing, const float push[8], const float *at) {
    const Gim *g = find(GIM_SWITCH, number);
    float now[12], back[12], local[12], m[12];
    int p;
    if (!g) return;
    /* now: where it stands this frame (at: x y z yaw for one on a joint); back: from where it was baked. */
    if (at) {
        float a = at[3] * 0.0174532925f, r[9] = {cosf(a), 0.0f, sinf(a), 0.0f, 1.0f, 0.0f, -sinf(a), 0.0f, cosf(a)};
        place(now, at, r);
    } else place(now, g->o, g->r);
    if (g->attached) { memset(back, 0, sizeof back); back[0] = back[5] = back[10] = 1.0f; }
    else unplace(back, g->o, g->r);
    for (p = 0; p < GIM_SWITCH_PARTS; ++p) {
        if (p == 0) turn(local, 1, spin);          /* record 1, the hub cube: about its y by +0x850 */
        else if (p == 1) turn(local, 2, wing);     /* record 2, the disc: about z by +0x854 */
        else if (p <= 9) {
            /* blade i, baked at Rz(-45 i): out along its own y by +0x858[i] */
            float a = -45.0f * (float)(p - 2) * 0.0174532925f, d = push ? push[p - 2] : 0.0f;
            turn(local, 0, 0.0f);
            local[3] = -sinf(a) * d; local[7] = cosf(a) * d;
        } else turn(local, 0, 0.0f);
        mul(m, now, local);
        mul(m, m, back);
        stage_set_group_matrix(g->tag + p, m);
    }
}

void gim_switch_hide(int number, int hidden) {
    const Gim *g = find(GIM_SWITCH, number);
    int p;
    if (!g) return;
    for (p = 0; p < GIM_SWITCH_PARTS; ++p) stage_set_group_mode(g->tag + p, hidden ? 2 : 0);
}

/* FUN_0031d270: the board's colour, alpha up to 0x40 (half, the PS2's 0x80 being whole). Its random shimmer
 * (FUN_0031c390) is the caller's. */
void gim_seal_draw(int number, int colour, float alpha) {
    static const unsigned char tint[3][3] = {{0x80, 0x02, 0x10}, {0x30, 0x40, 0x80}, {0x80, 0x00, 0x80}};
    const Gim *g = find(GIM_SEAL, number);
    const unsigned char *t;
    unsigned int src, dst, k, c[3], d;
    if (!g || alpha <= 0.004f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    t = tint[colour >= 0 && colour <= 2 ? colour : 0];
    for (k = 0; k < 3; ++k) {
        unsigned int v = (unsigned int)((float)t[k] * 2.0f * 0.5f * alpha);
        c[k] = v > 255 ? 255 : v;
    }
    d = (unsigned int)(255.0f * (1.0f - 0.5f * alpha));
    src = c[2] << 16 | c[1] << 8 | c[0];
    dst = d << 16 | d << 8 | d;
    stage_draw_group(g->tag, src, dst);
}

/* ---- Key items (CItemKey): the game's V160 from static_effect.pac, looping at the item (FUN_00428600). A caller
 * names each item lying this frame; gim_items_end_frame stops those not named (taken, or the room left). */
#include "nfx.h"
#define KEY_FX_MAX 8
static struct { float at[3]; int handle, seen; } key_fx[KEY_FX_MAX];

int gim_key_item(const float at[3]) {
    int i, free_slot = -1;
    for (i = 0; i < KEY_FX_MAX; ++i) {
        if (key_fx[i].handle && !nfx_alive(key_fx[i].handle)) key_fx[i].handle = 0;
        if (key_fx[i].handle && fabsf(key_fx[i].at[0] - at[0]) < 1.0f && fabsf(key_fx[i].at[1] - at[1]) < 1.0f
            && fabsf(key_fx[i].at[2] - at[2]) < 1.0f) { key_fx[i].seen = 1; return 1; }
        if (!key_fx[i].handle && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    key_fx[free_slot].handle = nfx_spawn(NFX_STATIC, 0xa0, at, 0.0f, 1);
    if (!key_fx[free_slot].handle) return 0;
    memcpy(key_fx[free_slot].at, at, sizeof key_fx[free_slot].at);
    key_fx[free_slot].seen = 1;
    return 1;
}

void gim_items_end_frame(void) {
    int i;
    for (i = 0; i < KEY_FX_MAX; ++i) {
        if (key_fx[i].handle && !key_fx[i].seen) { nfx_kill(key_fx[i].handle); key_fx[i].handle = 0; }
        key_fx[i].seen = 0;
    }
}

void gim_items_reset(void) { memset(key_fx, 0, sizeof key_fx); }

/* ---- A crest switch's sounds (CStageSetSwitch; the room's bank, type 8, played with bank 0x20). A hit plays 0x14 and
 * re-arms the run-down (+0x823); when the 120-frame window runs out, 0x1a once. While not lit it hums by its charge
 * (timer +0x840): none at 0, up to 100 0x17 every 25 frames, up to 200 0x18 every 15, above 0x19 every 5. */
#include "sfx.h"
void gim_switch_hit_sound(GimSwitchSound *s) {
    sfx_play_room(0x14, 0x20, 0.0f, 1.0f);
    s->down_armed = 1;
}
void gim_switch_sounds(GimSwitchSound *s, float charge, float window, int lit, float frames) {
    if (lit) return;
    if (window <= 0.0f && s->down_armed) { sfx_play_room(0x1a, 0x20, 0.0f, 1.0f); s->down_armed = 0; }
    s->hum -= frames;
    if (charge <= 0.0f || s->hum > 0.0f) return;
    if (charge <= 100.0f) { sfx_play_room(0x17, 0x20, 0.0f, 1.0f); s->hum = 25.0f; }
    else if (charge <= 200.0f) { sfx_play_room(0x18, 0x20, 0.0f, 1.0f); s->hum = 15.0f; }
    else { sfx_play_room(0x19, 0x20, 0.0f, 1.0f); s->hum = 5.0f; }
}

/* ---- Turning props (gimmicks.spin_parts: 205's and 207's bladed pillars): each part turned about y through its
 * set's place at its motion's rate, the motion's frame advancing a frame a frame (a loop of 240). */
#define SPIN_MAX 48
static struct { short tag; float x, z, rate; } spins[SPIN_MAX];
static int spin_count;
static float spin_frame;
void gim_spins_clear(void) { spin_count = 0; spin_frame = 0.0f; }
void gim_spin_add(int tag, float x, float z, float rate) {
    if (spin_count >= SPIN_MAX) return;
    spins[spin_count].tag = (short)tag; spins[spin_count].x = x; spins[spin_count].z = z; spins[spin_count].rate = rate;
    ++spin_count;
}
void gim_spins_update(float frames) {
    int i;
    spin_frame = fmodf(spin_frame + frames, 240.0f);
    for (i = 0; i < spin_count; ++i) {
        float a = spins[i].rate * spin_frame, c = cosf(a), s = sinf(a), x = spins[i].x, z = spins[i].z;
        /* Ry as gimmicks._rotation: (c 0 s; 0 1 0; -s 0 c), about (x, z). */
        float m[12] = {c, 0.0f, s, x - c * x - s * z, 0.0f, 1.0f, 0.0f, 0.0f, -s, 0.0f, c, z + s * x - c * z};
        stage_set_group_matrix(spins[i].tag, m);
    }
}
