#include "cloth.h"

#include <math.h>
#include <string.h>

/* The chain's damping (+0x54, FUN_0022f1d0's 0.99): no script sets it. */
#define CLOTH_DAMPING 0.99f

static void m_mul(ClothM3 *o, const ClothM3 *a, const ClothM3 *b) {
    ClothM3 r;
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    *o = r;
}

static void m_apply(const ClothM3 *m, const float v[3], float out[3]) {
    float x = v[0], y = v[1], z = v[2];
    out[0] = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z;
    out[1] = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z;
    out[2] = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z;
}

/* Rotation taking unit vector a onto unit vector b (Rodrigues). */
static void m_align(ClothM3 *m, const float a[3], const float b[3]) {
    float v[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float k;
    int i, j;
    if (c < -0.9999f) {
        memset(m, 0, sizeof(*m));
        m->m[0][0] = 1.0f; m->m[1][1] = -1.0f; m->m[2][2] = -1.0f;
        return;
    }
    k = 1.0f / (1.0f + c);
    {
        float vx[3][3] = {{0, -v[2], v[1]}, {v[2], 0, -v[0]}, {-v[1], v[0], 0}};
        for (i = 0; i < 3; ++i)
            for (j = 0; j < 3; ++j) {
                float vx2 = vx[i][0] * vx[0][j] + vx[i][1] * vx[1][j] + vx[i][2] * vx[2][j];
                m->m[i][j] = (i == j ? 1.0f : 0.0f) + vx[i][j] + vx2 * k;
            }
    }
}

static void normalize(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

/* Push p out of the capsule a-b of radius r (FUN_001be780). */
static int push_capsule(float p[3], const float a[3], const float b[3], float r) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = len2 > 0.0f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f;
    float c[3], d[3], dist;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    c[0] = a[0] + ab[0] * t; c[1] = a[1] + ab[1] * t; c[2] = a[2] + ab[2] * t;
    d[0] = p[0] - c[0]; d[1] = p[1] - c[1]; d[2] = p[2] - c[2];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist < r && dist > 1e-4f) {
        float k = r / dist;
        p[0] = c[0] + d[0] * k; p[1] = c[1] + d[1] * k; p[2] = c[2] + d[2] * k;
        return 1;
    }
    return 0;
}

void cloth_rest(ClothJoint *joint, const float parent_pos[3], const ClothM3 *parent_rot, const float rest_offset[3]) {
    float r[3];
    m_apply(parent_rot, rest_offset, r);
    joint->pos[0] = parent_pos[0] + r[0];
    joint->pos[1] = parent_pos[1] + r[1];
    joint->pos[2] = parent_pos[2] + r[2];
    memset(joint->vel, 0, sizeof(joint->vel));
    joint->rot = *parent_rot;
}

/* FUN_0022f270 for one bone (no wind), as coat.c's step. */
void cloth_step(ClothJoint *joint, const float parent_pos[3], const ClothM3 *parent_rot, const float rest_offset[3],
                const ClothParams *params, const ClothCapsule *capsules, int capsule_count) {
    /* The axis the bone points back to its parent along: Y for a hanging chain; for an 'X' one (wings spread
     * sideways), X toward the parent's side. */
    const float up[3] = {params->axis_x ? (rest_offset[0] < 0.0f ? 1.0f : -1.0f) : 0.0f, params->axis_x ? 0.0f : 1.0f, 0.0f};
    const float stiffness = params->stiffness, keep = 1.0f - params->stiffness;
    float *p = joint->pos, *v = joint->vel;
    float rest_pos[3], rest_up[3], predicted[3], sim_up[3], blended[3], d[3], len, speed;
    float limit = sqrtf(rest_offset[0] * rest_offset[0] + rest_offset[1] * rest_offset[1] + rest_offset[2] * rest_offset[2]);
    ClothM3 align;
    int k, c, hit = 0;
    /* Where the parent wants it, and where its velocity takes it. */
    m_apply(parent_rot, rest_offset, rest_pos);
    m_apply(parent_rot, up, rest_up);
    for (k = 0; k < 3; ++k) {
        rest_pos[k] += parent_pos[k];
        predicted[k] = p[k] + v[k];
        sim_up[k] = parent_pos[k] - predicted[k];
    }
    normalize(sim_up);
    /* Stiffness blends the authored pose back in (FUN_00133ab0). */
    for (k = 0; k < 3; ++k) {
        p[k] = rest_pos[k] * stiffness + predicted[k] * keep;
        blended[k] = rest_up[k] * stiffness + sim_up[k] * keep;
    }
    normalize(blended);
    m_align(&align, rest_up, blended);
    m_mul(&joint->rot, &align, parent_rot);
    for (c = 0; c < capsule_count; ++c) hit |= push_capsule(p, capsules[c].a, capsules[c].b, capsules[c].radius);
    if (hit) v[0] = v[2] = 0.0f;
    v[1] += params->gravity;
    /* LimitLength holds the length; the spring still sees the stretch. */
    for (k = 0; k < 3; ++k) d[k] = p[k] - parent_pos[k];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 0.0f && limit > 0.0f) {
        float stretch = len - limit;
        for (k = 0; k < 3; ++k) {
            float dir = d[k] / len;
            if (params->limit) p[k] = parent_pos[k] + dir * limit;
            v[k] -= params->spring * dir * stretch;
        }
    }
    speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (speed > params->max_speed) for (k = 0; k < 3; ++k) v[k] *= params->max_speed / speed;
    for (k = 0; k < 3; ++k) v[k] *= CLOTH_DAMPING;
}
