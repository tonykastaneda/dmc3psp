#include "coat.h"

#include <malloc.h>
#include <math.h>
#include <string.h>

#include <psputils.h>

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "coat_runtime.h"

/* The coat hangs from Dante's pelvis (bone 14, at hip height in the rest
 * pose; the coat rig's root sits at its origin). */
#define ATTACH_BONE BONE_PELVIS

/* Simulation constants. The .clt values (gravity -0.01, spring 0.02,
 * stiffness 0.25, max speed 50) are in the PS2 engine's own per-frame units,
 * which are not calibrated yet; these are tuned by eye in centimetres. */
#define SIM_HZ 60.0f
#define SIM_GRAVITY (-980.0f / (SIM_HZ * SIM_HZ))  /* cm per frame^2 */
#define SIM_DAMPING 0.90f
#define SIM_PULL_ROOT 0.10f   /* pull toward the rigid pose near the waist ... */
#define SIM_PULL_TIP 0.025f   /* ... tapering to the hem */
#define SIM_MAX_STEP 25.0f    /* cm per frame */
#define SIM_ITERATIONS 3
#define SIM_DRAG_TIP 0.20f    /* air drag vs. the waist's motion, scaled up toward the hem */
#define LEG_RADIUS 12.0f
#define HIP_RADIUS 15.0f

typedef struct { float m[3][3]; } M3;

static CoatVertex *vertices;
static unsigned short *indices;
static float joints[DMC3_COAT_STRAND_COUNT][DMC3_COAT_STRAND_MAX][3];   /* world space */
static float previous[DMC3_COAT_STRAND_COUNT][DMC3_COAT_STRAND_MAX][3];
static float rest_length[DMC3_COAT_STRAND_COUNT][DMC3_COAT_STRAND_MAX];
static int strand_length[DMC3_COAT_STRAND_COUNT];
static float accumulator;
static float last_attach[3];
static int initialised;
/* Last pose-derived transforms (world space). */
static M3 attach_rot;
static float attach_pos[3];
static M3 bone_rot[DMC3_COAT_BONE_COUNT];   /* world */
static float bone_pos[DMC3_COAT_BONE_COUNT][3];
static float player_yaw, player_pos[3];
static float floor_y;  /* ground under Dante, for the hem */

static void m_mul(M3 *o, const M3 *a, const M3 *b) {
    M3 r;
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    *o = r;
}

static void m_apply(const M3 *m, const float v[3], float out[3]) {
    float x = v[0], y = v[1], z = v[2];
    out[0] = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z;
    out[1] = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z;
    out[2] = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z;
}

static void m_yaw(M3 *m, float yaw) {
    float c = cosf(yaw), s = sinf(yaw);
    memset(m, 0, sizeof(*m));
    m->m[0][0] = c; m->m[0][2] = s; m->m[1][1] = 1.0f; m->m[2][0] = -s; m->m[2][2] = c;
}

/* Rotation taking unit vector a onto unit vector b (Rodrigues). */
static void m_align(M3 *m, const float a[3], const float b[3]) {
    float v[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float k;
    int i, j;
    if (c < -0.9999f) { /* opposite: 180 degrees about X (strands are near-vertical) */
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

int coat_init(void) {
    int s, i;
    vertices = memalign(16, sizeof(CoatVertex) * DMC3_COAT_VERTEX_COUNT);
    indices = memalign(16, sizeof(unsigned short) * DMC3_COAT_INDEX_COUNT);
    if (!vertices || !indices) return 0;
    memcpy(indices, dmc3_coat_indices, sizeof(unsigned short) * DMC3_COAT_INDEX_COUNT);
    for (i = 0; i < DMC3_COAT_VERTEX_COUNT; ++i) {
        vertices[i].u = dmc3_coat_uvs[i].u;
        vertices[i].v = dmc3_coat_uvs[i].v;
    }
    sceKernelDcacheWritebackRange(indices, sizeof(unsigned short) * DMC3_COAT_INDEX_COUNT);
    for (s = 0; s < DMC3_COAT_STRAND_COUNT; ++s) {
        strand_length[s] = 0;
        for (i = 0; i < DMC3_COAT_STRAND_MAX && dmc3_coat_strands[s][i] >= 0; ++i) {
            strand_length[s] = i + 1;
            if (i > 0) {
                const Dmc3Vec3 *a = &dmc3_coat_bone_rest[dmc3_coat_strands[s][i - 1]];
                const Dmc3Vec3 *b = &dmc3_coat_bone_rest[dmc3_coat_strands[s][i]];
                float dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
                rest_length[s][i] = sqrtf(dx * dx + dy * dy + dz * dz);
            }
        }
    }
    return 1;
}

void coat_reset(void) { initialised = 0; }

/* Rigid (unsimulated) world position of a coat bone under the attachment. */
static void rigid(int bone, float out[3]) {
    float rest[3] = {dmc3_coat_bone_rest[bone].x, dmc3_coat_bone_rest[bone].y, dmc3_coat_bone_rest[bone].z};
    m_apply(&attach_rot, rest, out);
    out[0] += attach_pos[0]; out[1] += attach_pos[1]; out[2] += attach_pos[2];
}

/* Push p out of the capsule a-b of radius r. */
static void push_capsule(float p[3], const float a[3], const float b[3], float r) {
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
    }
}

static void to_world(const Pose *pose, int bone, float out[3]) {
    M3 yaw;
    m_yaw(&yaw, player_yaw);
    m_apply(&yaw, pose->world_pos[bone], out);
    out[0] += player_pos[0]; out[1] += player_pos[1]; out[2] += player_pos[2];
}

static void step(const Pose *pose) {
    static const int legs[4][2] = {
        {BONE_L_THIGH, BONE_L_SHIN}, {BONE_L_SHIN, BONE_L_FOOT},
        {BONE_R_THIGH, BONE_R_SHIN}, {BONE_R_SHIN, BONE_R_FOOT},
    };
    float capsule[4][2][3], hip_a[3], hip_b[3];
    int s, i, k, it, l;
    for (l = 0; l < 4; ++l) {
        to_world(pose, legs[l][0], capsule[l][0]);
        to_world(pose, legs[l][1], capsule[l][1]);
    }
    to_world(pose, BONE_PELVIS, hip_a);
    to_world(pose, BONE_SPINE0, hip_b);
    hip_a[1] -= 18.0f;
    {
        /* Waist motion this step drives air drag on the free joints. */
        float wind[3] = {attach_pos[0] - last_attach[0], attach_pos[1] - last_attach[1], attach_pos[2] - last_attach[2]};
        memcpy(last_attach, attach_pos, sizeof(last_attach));
        for (s = 0; s < DMC3_COAT_STRAND_COUNT; ++s) {
            int n = strand_length[s];
            for (i = 1; i < n; ++i) {
                float k2 = SIM_DRAG_TIP * (float)i / (float)(n - 1);
                for (k = 0; k < 3; ++k) joints[s][i][k] -= wind[k] * k2;
            }
        }
    }

    for (s = 0; s < DMC3_COAT_STRAND_COUNT; ++s) {
        int n = strand_length[s];
        rigid(dmc3_coat_strands[s][0], joints[s][0]);
        for (i = 1; i < n; ++i) {
            float *p = joints[s][i], *q = previous[s][i];
            float target[3], v[3], speed, pull = SIM_PULL_ROOT + (SIM_PULL_TIP - SIM_PULL_ROOT) * (float)(i - 1) / (float)(n > 2 ? n - 2 : 1);
            rigid(dmc3_coat_strands[s][i], target);
            for (k = 0; k < 3; ++k) v[k] = (p[k] - q[k]) * SIM_DAMPING;
            v[1] += SIM_GRAVITY;
            speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (speed > SIM_MAX_STEP) for (k = 0; k < 3; ++k) v[k] *= SIM_MAX_STEP / speed;
            for (k = 0; k < 3; ++k) {
                q[k] = p[k];
                p[k] += v[k];
                p[k] += (target[k] - p[k]) * pull;
            }
        }
        for (it = 0; it < SIM_ITERATIONS; ++it) {
            for (i = 1; i < n; ++i) {
                float *a = joints[s][i - 1], *b = joints[s][i];
                float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (len > 1e-4f) {
                    float k2 = rest_length[s][i] / len;
                    /* Anchor-side joint is fixed: move only the child. */
                    b[0] = a[0] + d[0] * k2; b[1] = a[1] + d[1] * k2; b[2] = a[2] + d[2] * k2;
                }
                for (l = 0; l < 4; ++l) push_capsule(b, capsule[l][0], capsule[l][1], LEG_RADIUS);
                push_capsule(b, hip_a, hip_b, HIP_RADIUS);
                if (b[1] < floor_y) b[1] = floor_y; /* floor */
            }
        }
    }
}

void coat_update(const Pose *pose, const Player *player, float dt) {
    M3 yaw;
    float local_attach[3];
    int s, i;
    player_yaw = player->yaw;
    floor_y = (player->on_ground ? player->y : player->y - 250.0f) + 1.0f;
    player_pos[0] = player->x; player_pos[1] = player->y; player_pos[2] = player->z;
    m_yaw(&yaw, player_yaw);
    m_mul(&attach_rot, &yaw, (const M3 *)&pose->world_rot[ATTACH_BONE]);
    memcpy(local_attach, pose->world_pos[ATTACH_BONE], sizeof(local_attach));
    m_apply(&yaw, local_attach, attach_pos);
    attach_pos[0] += player_pos[0]; attach_pos[1] += player_pos[1]; attach_pos[2] += player_pos[2];

    if (!initialised) {
        for (s = 0; s < DMC3_COAT_STRAND_COUNT; ++s)
            for (i = 0; i < strand_length[s]; ++i) {
                rigid(dmc3_coat_strands[s][i], joints[s][i]);
                memcpy(previous[s][i], joints[s][i], sizeof(joints[s][i]));
            }
        accumulator = 0.0f;
        memcpy(last_attach, attach_pos, sizeof(last_attach));
        initialised = 1;
    }
    accumulator += dt;
    if (accumulator > 4.0f / SIM_HZ) accumulator = 4.0f / SIM_HZ;
    while (accumulator >= 1.0f / SIM_HZ) {
        step(pose);
        accumulator -= 1.0f / SIM_HZ;
    }

    /* Bone transforms: root = attachment; strand bones aim at the next joint. */
    bone_rot[0] = attach_rot;
    memcpy(bone_pos[0], attach_pos, sizeof(attach_pos));
    for (s = 0; s < DMC3_COAT_STRAND_COUNT; ++s) {
        int n = strand_length[s];
        for (i = 0; i < n; ++i) {
            int bone = dmc3_coat_strands[s][i];
            memcpy(bone_pos[bone], joints[s][i], sizeof(joints[s][i]));
            if (i + 1 < n) {
                int child = dmc3_coat_strands[s][i + 1];
                float rest_dir[3] = {dmc3_coat_bone_rest[child].x - dmc3_coat_bone_rest[bone].x,
                                     dmc3_coat_bone_rest[child].y - dmc3_coat_bone_rest[bone].y,
                                     dmc3_coat_bone_rest[child].z - dmc3_coat_bone_rest[bone].z};
                float world_rest[3], actual[3] = {joints[s][i + 1][0] - joints[s][i][0],
                                                  joints[s][i + 1][1] - joints[s][i][1],
                                                  joints[s][i + 1][2] - joints[s][i][2]};
                M3 align;
                m_apply(&attach_rot, rest_dir, world_rest);
                normalize(world_rest);
                normalize(actual);
                m_align(&align, world_rest, actual);
                m_mul(&bone_rot[bone], &align, &attach_rot);
            } else {
                bone_rot[bone] = bone_rot[dmc3_coat_strands[s][i - 1]];
            }
        }
    }
}

const CoatVertex *coat_skin(const Player *player) {
    M3 inv_yaw, rot[DMC3_COAT_BONE_COUNT];
    float pos[DMC3_COAT_BONE_COUNT][3];
    int b, i, k;
    (void)player;
    /* World -> Dante model space (the renderer applies the model matrix). */
    m_yaw(&inv_yaw, -player_yaw);
    for (b = 0; b < DMC3_COAT_BONE_COUNT; ++b) {
        float d[3] = {bone_pos[b][0] - player_pos[0], bone_pos[b][1] - player_pos[1], bone_pos[b][2] - player_pos[2]};
        m_mul(&rot[b], &inv_yaw, &bone_rot[b]);
        m_apply(&inv_yaw, d, pos[b]);
    }
    for (i = 0; i < DMC3_COAT_VERTEX_COUNT; ++i) {
        const Dmc3Vec3 *src = &dmc3_coat_positions[i], *nrm = &dmc3_coat_normals[i];
        float p[3] = {0, 0, 0}, n[3] = {0, 0, 0};
        for (k = 0; k < 3; ++k) {
            int weight = dmc3_coat_weights[i][k], bone;
            float w, d[3], r[3], rn[3], nv[3] = {nrm->x, nrm->y, nrm->z};
            if (!weight) continue;
            bone = dmc3_coat_bones[i][k];
            w = (float)weight * (1.0f / 31.0f);
            d[0] = src->x - dmc3_coat_bone_rest[bone].x;
            d[1] = src->y - dmc3_coat_bone_rest[bone].y;
            d[2] = src->z - dmc3_coat_bone_rest[bone].z;
            m_apply(&rot[bone], d, r);
            m_apply(&rot[bone], nv, rn);
            p[0] += w * (r[0] + pos[bone][0]); p[1] += w * (r[1] + pos[bone][1]); p[2] += w * (r[2] + pos[bone][2]);
            n[0] += w * rn[0]; n[1] += w * rn[1]; n[2] += w * rn[2];
        }
        vertices[i].x = p[0]; vertices[i].y = p[1]; vertices[i].z = p[2];
        vertices[i].nx = n[0]; vertices[i].ny = n[1]; vertices[i].nz = n[2];
    }
    sceKernelDcacheWritebackRange(vertices, sizeof(CoatVertex) * DMC3_COAT_VERTEX_COUNT);
    return vertices;
}

const unsigned short *coat_indices(void) { return indices; }
int coat_object_count(void) { return DMC3_COAT_OBJECT_COUNT; }

void coat_object(int index, unsigned int *index_start, unsigned int *index_count, unsigned int *texture) {
    *index_start = dmc3_coat_objects[index].index_start;
    *index_count = dmc3_coat_objects[index].index_count;
    *texture = dmc3_coat_objects[index].texture;
}

int coat_joint_count(void) { return DMC3_COAT_BONE_COUNT; }

void coat_joint(int index, float out[3], int *parent_index) {
    M3 inv_yaw;
    float d[3] = {bone_pos[index][0] - player_pos[0], bone_pos[index][1] - player_pos[1], bone_pos[index][2] - player_pos[2]};
    m_yaw(&inv_yaw, -player_yaw);
    m_apply(&inv_yaw, d, out);
    *parent_index = dmc3_coat_bone_parent[index];
}
