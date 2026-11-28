/* Cutscene cloth. The .clt is the gameplay chain (cloth.c, FUN_0022f270), chosen by the model clip and replaced
 * when a motion clip names another (docs/CUTSCENES.md). The step stores joints in world space and adds gravity on
 * world Y, so a bone that moves leaves the cloth behind. ClothMode 1 rides the parent model's root first
 * (FUN_00230150: new root matrix times the inverse of the last), and a clip that never sets it does not. ResetClt /
 * ResetClt2 snap to rest and then step the level when it is above 0 (FUN_002302d0; the update just before the snap
 * is overwritten by it). These scripts set no wind and name no capsules. A coat on Dante still
 * gets the gameplay body capsules the caller passes in. ClothSim1D is a different system and is not run. */
#include "demo_cloth.h"

#include "cloth.h"

#include <malloc.h>
#include <string.h>

#define CLOTH_ACTORS 64
#define CLOTH_BONES 48
#define CLOTH_SETTLE_MAX 32

typedef struct {
    ClothJoint *joint;
    int script, mode, inited, has_owner;
    float prev;
    float owner_pos[3];
    float owner_rot[9];
} SlotCloth;

static SlotCloth slots[CLOTH_ACTORS];

static unsigned int ru32(const unsigned char *p) {
    unsigned int v;
    memcpy(&v, p, 4);
    return v;
}
static int ri32(const unsigned char *p) {
    int v;
    memcpy(&v, p, 4);
    return v;
}
static float rf32(const unsigned char *p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}

static void mul_m(float *o, const float *a, const float *b) {
    float r[9];
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    memcpy(o, r, sizeof r);
}

static void mul_v(const float *m, const float v[3], float o[3]) {
    float x = v[0], y = v[1], z = v[2];
    o[0] = m[0] * x + m[1] * y + m[2] * z;
    o[1] = m[3] * x + m[4] * y + m[5] * z;
    o[2] = m[6] * x + m[7] * y + m[8] * z;
}

static void transpose(const float *m, float *o) {
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) o[i * 3 + j] = m[j * 3 + i];
}

void demo_cloth_reset(void) {
    int i;
    for (i = 0; i < CLOTH_ACTORS; ++i) {
        free(slots[i].joint);
        slots[i].joint = 0;
        slots[i].script = -1;
        slots[i].mode = -1;
        slots[i].inited = 0;
        slots[i].has_owner = 0;
        slots[i].prev = -1.0f;
    }
}

void demo_cloth_reset_slot(int actor) {
    if (actor < 0 || actor >= CLOTH_ACTORS) return;
    slots[actor].script = -1;
    slots[actor].mode = -1;
    slots[actor].inited = 0;
    slots[actor].has_owner = 0;
    slots[actor].prev = -1.0f;
}

/* One chain's bytes, or 0 when the script runs out. Advances p past it. */
static const unsigned char *next_chain(const unsigned char **p, const unsigned char *end, unsigned int *bones) {
    const unsigned char *at;
    unsigned int nb, tail;
    if (*p + 24 > end) return 0;
    nb = ru32(*p + 20);
    tail = (nb * 2u + 3u) & ~3u;
    if (nb > CLOTH_BONES || *p + 24 + tail > end) return 0;
    at = *p;
    *bones = nb;
    *p += 24 + tail;
    return at;
}

static const unsigned char *script_at(const unsigned char *block, const unsigned char *end, int index) {
    const unsigned char *p = block + 8;
    int s, c, nscripts, chains;
    if (block + 8 > end || index < 0) return 0;
    nscripts = (int)ru32(block);
    if (index >= nscripts) return 0;
    for (s = 0; s < index; ++s) {
        if (p + 4 > end) return 0;
        chains = (int)ru32(p);
        p += 4;
        for (c = 0; c < chains; ++c) {
            unsigned int nb;
            if (!next_chain(&p, end, &nb)) return 0;
        }
    }
    return p;
}

static const unsigned char *clips_at(const unsigned char *block, const unsigned char *end, int *count) {
    const unsigned char *p = block + 8;
    int s, c, nscripts, nclips, chains;
    *count = 0;
    if (block + 8 > end) return 0;
    nscripts = (int)ru32(block);
    nclips = (int)ru32(block + 4);
    for (s = 0; s < nscripts; ++s) {
        if (p + 4 > end) return 0;
        chains = (int)ru32(p);
        p += 4;
        for (c = 0; c < chains; ++c) {
            unsigned int nb;
            if (!next_chain(&p, end, &nb)) return 0;
        }
    }
    if (p + (unsigned)nclips * 24 > end) return 0;
    *count = nclips;
    return p;
}

static void axis_of(ClothParams *params, int code) {
    params->axis_x = 0;
    params->axis_sign = 1;
    if (code == 1 || code == 3) params->axis_x = 1;                 /* X, and NX (the rest offset picks the side) */
    else if (code == 2) params->axis_sign = -1;                     /* NY */
    else if (code == 4) params->axis_x = 2;                         /* Z */
    else if (code == 5) { params->axis_x = 2; params->axis_sign = -1; } /* NZ */
}

/* Parents first, in the .clt's order. A free parent is its joint (already in world space); any other parent is
 * the posed bone. A bone hung on the piece root uses the piece's root. rest hangs the bone; a step swings it. */
static void run_script(SlotCloth *st, const unsigned char *script, const unsigned char *end, int rest,
                       const signed char *parents, const float *local, int bones,
                       const float *base_pos, const float *base_rot, float *world_pos, float *world_rot,
                       unsigned char *is_free, const ClothCapsule *capsules, int capsule_count) {
    const unsigned char *p = script;
    unsigned int chains, c;
    if (!script || p + 4 > end) return;
    chains = ru32(p);
    p += 4;
    if (chains > 8) chains = 8;
    for (c = 0; c < chains; ++c) {
        unsigned int nb, i;
        const unsigned char *ch = next_chain(&p, end, &nb);
        float gravity, spring, speed, stiff;
        int limit;
        if (!ch) return;
        gravity = rf32(ch);
        spring = rf32(ch + 4);
        speed = rf32(ch + 8);
        stiff = rf32(ch + 12);
        limit = ri32(ch + 16);
        for (i = 0; i < nb; ++i) {
            int b = ch[24 + i];
            int axis = (signed char)ch[24 + nb + i];
            int parent, k;
            float rest_off[3], pp[3], pr[9];
            ClothParams params;
            ClothM3 rot;
            if (b < 0 || b >= bones) continue;
            is_free[b] = 1;
            parent = parents[b];
            for (k = 0; k < 3; ++k) rest_off[k] = local[b * 3 + k];
            if (parent >= 0 && parent < bones && is_free[parent]) {
                memcpy(pp, st->joint[parent].pos, sizeof pp);
                memcpy(pr, st->joint[parent].rot.m, sizeof pr);
            } else if (parent >= 0 && parent < bones) {
                memcpy(pp, world_pos + parent * 3, sizeof pp);
                memcpy(pr, world_rot + parent * 9, sizeof pr);
            } else {
                memcpy(pp, base_pos, sizeof pp);
                memcpy(pr, base_rot, sizeof pr);
            }
            memcpy(&rot, pr, sizeof rot);
            if (rest) cloth_rest(&st->joint[b], pp, &rot, rest_off);
            else {
                params.gravity = gravity;
                params.spring = spring;
                params.max_speed = speed;
                params.stiffness = stiff;
                params.limit = limit;
                axis_of(&params, axis);
                cloth_step(&st->joint[b], pp, &rot, rest_off, &params, capsules, capsule_count);
            }
        }
    }
}

static void write_back(SlotCloth *st, const unsigned char *is_free, int bones,
                       float *world_pos, float *world_rot) {
    int b, k;
    for (b = 0; b < bones; ++b) {
        if (!is_free[b]) continue;
        for (k = 0; k < 3; ++k) world_pos[b * 3 + k] = st->joint[b].pos[k];
        memcpy(world_rot + b * 9, st->joint[b].rot.m, sizeof(float) * 9);
    }
}

static void remember_owner(SlotCloth *st, const float *pos, const float *rot) {
    if (!pos || !rot) return;
    memcpy(st->owner_pos, pos, sizeof st->owner_pos);
    memcpy(st->owner_rot, rot, sizeof st->owner_rot);
    st->has_owner = 1;
}

/* FUN_00230150: move the joints by the owner's root change, so that root's travel and turn don't drag them.
 * The first frame only records the root. */
static void ride(SlotCloth *st, int bones, const float *pos, const float *rot) {
    float inv[9], delta[9], tmp[3], nrot[9];
    int b, k;
    if (!pos || !rot) return;
    if (!st->has_owner) {
        remember_owner(st, pos, rot);
        return;
    }
    transpose(st->owner_rot, inv);
    mul_m(delta, rot, inv);
    for (b = 0; b < bones && b < CLOTH_BONES; ++b) {
        float *p = st->joint[b].pos, *v = st->joint[b].vel;
        for (k = 0; k < 3; ++k) tmp[k] = p[k] - st->owner_pos[k];
        mul_v(delta, tmp, p);
        for (k = 0; k < 3; ++k) p[k] += pos[k];
        mul_v(delta, v, tmp);
        memcpy(v, tmp, sizeof tmp);
        mul_m(nrot, delta, (const float *)st->joint[b].rot.m);
        memcpy(st->joint[b].rot.m, nrot, sizeof nrot);
    }
    remember_owner(st, pos, rot);
}

/* A bone the chain does not run, hanging on one it does, keeps the offset the motion gave it. */
static void rehang(float *world_pos, float *world_rot, const float *old_pos, const float *old_rot,
                   const signed char *parents, const unsigned char *is_free, int bones) {
    unsigned char moved[CLOTH_BONES];
    int pass, i;
    memset(moved, 0, sizeof moved);
    for (i = 0; i < bones; ++i) if (is_free[i]) moved[i] = 1;
    for (pass = 0; pass < bones; ++pass) {
        int changed = 0;
        for (i = 0; i < bones; ++i) {
            int p = parents[i], k;
            float inv[9], d[3], off[3], lrot[9], tmp[3];
            if (is_free[i] || p < 0 || p >= bones || !moved[p] || moved[i]) continue;
            transpose(old_rot + p * 9, inv);
            for (k = 0; k < 3; ++k) d[k] = old_pos[i * 3 + k] - old_pos[p * 3 + k];
            mul_v(inv, d, off);
            mul_m(lrot, inv, old_rot + i * 9);
            mul_v(world_rot + p * 9, off, tmp);
            for (k = 0; k < 3; ++k) world_pos[i * 3 + k] = world_pos[p * 3 + k] + tmp[k];
            mul_m(world_rot + i * 9, world_rot + p * 9, lrot);
            moved[i] = 1;
            changed = 1;
        }
        if (!changed) break;
    }
}

void demo_cloth_apply(int actor, const float *base_pos, const float *base_rot,
                      const float *owner_pos, const float *owner_rot,
                      float *world_pos, float *world_rot,
                      const signed char *parents, const float *local, int bones,
                      const void *block, unsigned int size, float clock,
                      const ClothCapsule *capsules, int capsule_count) {
    SlotCloth *st;
    const unsigned char *bytes, *end, *clips, *script;
    unsigned char is_free[CLOTH_BONES];
    float old_pos[CLOTH_BONES * 3], old_rot[CLOTH_BONES * 9];
    int nclips, i, script_id, mode, reset_level, steps, n;
    if (!block || size < 8 || actor < 0 || actor >= CLOTH_ACTORS || bones <= 0) return;
    if (bones > CLOTH_BONES) bones = CLOTH_BONES;
    bytes = (const unsigned char *)block;
    end = bytes + size;
    if (ru32(bytes) > 64) return;
    st = &slots[actor];
    if (clock < st->prev) st->prev = -1.0f;
    clips = clips_at(bytes, end, &nclips);
    if (!clips) return;
    script_id = st->script;
    mode = st->mode;
    reset_level = -999;
    for (i = 0; i < nclips; ++i) {
        const unsigned char *c = clips + i * 24;
        float set = rf32(c);
        int which = ri32(c + 8), clip_mode = ri32(c + 12), reset = ri32(c + 16), level = ri32(c + 20);
        if (set > clock) break;
        if (set > st->prev) {
            if (which >= 0) script_id = which;
            if (clip_mode >= 0) mode = clip_mode;
            if (reset) reset_level = level;
        }
    }
    if (script_id != st->script) {
        st->script = script_id;
        st->inited = 0;
        st->has_owner = 0;
    }
    st->mode = mode; /* kept. Only 1 rides; the step runs either way (docs/CUTSCENES.md). */
    if (script_id < 0) {
        st->prev = clock;
        return;
    }
    script = script_at(bytes, end, script_id);
    if (!script) return;
    if (!st->joint) {
        st->joint = memalign(16, sizeof(ClothJoint) * CLOTH_BONES);
        if (!st->joint) return;
        memset(st->joint, 0, sizeof(ClothJoint) * CLOTH_BONES);
        st->inited = 0;
    }
    memset(is_free, 0, sizeof is_free);
    memcpy(old_pos, world_pos, sizeof(float) * 3 * (unsigned)bones);
    memcpy(old_rot, world_rot, sizeof(float) * 9 * (unsigned)bones);
    if (!st->inited || reset_level != -999) {
        /* A snap overwrites wherever the joints were, so the ride this frame would be thrown away. Record the
         * root instead, and the next frame rides from here. */
        run_script(st, script, end, 1, parents, local, bones, base_pos, base_rot, world_pos, world_rot, is_free, capsules, capsule_count);
        st->inited = 1;
        n = reset_level > 0 ? reset_level : 0;
        if (n > CLOTH_SETTLE_MAX) n = CLOTH_SETTLE_MAX;
        if (reset_level == -999) n = 1; /* the first frame it is on: one step, so it is moving */
        for (i = 0; i < n; ++i)
            run_script(st, script, end, 0, parents, local, bones, base_pos, base_rot, world_pos, world_rot, is_free, capsules, capsule_count);
        steps = st->prev < 0.0f ? 1 : (int)(clock - st->prev);
        if (steps > 4) steps = 4;
        if (reset_level != -999 && steps > 1)
            for (i = 1; i < steps; ++i)
                run_script(st, script, end, 0, parents, local, bones, base_pos, base_rot, world_pos, world_rot, is_free, capsules, capsule_count);
        if (st->mode == 1) remember_owner(st, owner_pos, owner_rot);
    } else if (clock > st->prev) {
        if (st->mode == 1) ride(st, bones, owner_pos, owner_rot);
        steps = (int)(clock - st->prev);
        if (steps < 1) steps = 1;
        if (steps > 4) steps = 4;
        for (i = 0; i < steps; ++i)
            run_script(st, script, end, 0, parents, local, bones, base_pos, base_rot, world_pos, world_rot, is_free, capsules, capsule_count);
    }
    st->prev = clock;
    write_back(st, is_free, bones, world_pos, world_rot);
    rehang(world_pos, world_rot, old_pos, old_rot, parents, is_free, bones);
}
