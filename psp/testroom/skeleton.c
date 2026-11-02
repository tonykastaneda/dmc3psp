#include "skeleton.h"

#include <math.h>
#include <string.h>

/* Only the bone tables are used here; the unused mesh/texture arrays are
 * static and discarded by the compiler. */
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"

#define DEG 0.0174532925f

static void mat_identity(Mat3 *m) {
    memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = 1.0f;
}

static void mat_mul(Mat3 *out, const Mat3 *a, const Mat3 *b) {
    Mat3 r;
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    *out = r;
}

static void mat_rot(Mat3 *m, int axis, float angle) {
    float c = cosf(angle), s = sinf(angle);
    mat_identity(m);
    if (axis == 0) { m->m[1][1] = c; m->m[1][2] = -s; m->m[2][1] = s; m->m[2][2] = c; }
    else if (axis == 1) { m->m[0][0] = c; m->m[0][2] = s; m->m[2][0] = -s; m->m[2][2] = c; }
    else { m->m[0][0] = c; m->m[0][1] = -s; m->m[1][0] = s; m->m[1][1] = c; }
}

/* local = Rx(x) * Ry(y) * Rz(z): Rz applies first (in the parent frame). */
static void set_euler(Pose *pose, int bone, float x, float y, float z) {
    Mat3 rx, ry, rz, t;
    mat_rot(&rx, 0, x);
    mat_rot(&ry, 1, y);
    mat_rot(&rz, 2, z);
    mat_mul(&t, &ry, &rz);
    mat_mul(&pose->local[bone], &rx, &t);
}

void pose_identity(Pose *pose) {
    int i;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) mat_identity(&pose->local[i]);
    pose->root_offset[0] = pose->root_offset[1] = pose->root_offset[2] = 0.0f;
}

void pose_evaluate(Pose *pose) {
    int i, k;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) {
        int parent = dmc3_bone_parent[i];
        const Dmc3Vec3 *offset = &dmc3_bone_local[i];
        if (parent < 0) {
            pose->world_rot[i] = pose->local[i];
            pose->world_pos[i][0] = offset->x;
            pose->world_pos[i][1] = offset->y;
            pose->world_pos[i][2] = offset->z;
            continue;
        }
        mat_mul(&pose->world_rot[i], &pose->world_rot[parent], &pose->local[i]);
        for (k = 0; k < 3; ++k) {
            const float *row = pose->world_rot[parent].m[k];
            pose->world_pos[i][k] = pose->world_pos[parent][k] +
                row[0] * offset->x + row[1] * offset->y + row[2] * offset->z;
        }
        if (i == BONE_HIPS) {
            for (k = 0; k < 3; ++k) pose->world_pos[i][k] += pose->root_offset[k];
        }
    }
}

void pose_procedural(Pose *pose, const Player *player, float time) {
    float run = player->speed / PLAYER_RUN_SPEED;
    float phase = player->run_phase;
    float s = sinf(phase);
    float arm_down = 74.0f * DEG;
    float elbow = (25.0f + 55.0f * run) * DEG;
    float breathe = sinf(time * 2.0f) * 1.5f * DEG;
    float thigh, knee_l, knee_r, arm_swing;
    int b;

    for (b = 0; b < DMC3_BONE_COUNT; ++b) set_euler(pose, b, 0.0f, 0.0f, 0.0f);
    pose->root_offset[0] = pose->root_offset[1] = pose->root_offset[2] = 0.0f;

    if (!player->on_ground) {
        /* Airborne tuck: knees up, arms out a little. */
        float tuck = player->vy > 0.0f ? 1.0f : 0.6f;
        set_euler(pose, BONE_L_THIGH, -55.0f * DEG * tuck, 0.0f, 0.0f);
        set_euler(pose, BONE_R_THIGH, -35.0f * DEG * tuck, 0.0f, 0.0f);
        set_euler(pose, BONE_L_SHIN, 80.0f * DEG * tuck, 0.0f, 0.0f);
        set_euler(pose, BONE_R_SHIN, 60.0f * DEG * tuck, 0.0f, 0.0f);
        set_euler(pose, BONE_L_UPPERARM, 0.0f, 0.0f, 45.0f * DEG);
        set_euler(pose, BONE_R_UPPERARM, 0.0f, 0.0f, -45.0f * DEG);
        set_euler(pose, BONE_L_FOREARM, 0.0f, 35.0f * DEG, 0.0f);
        set_euler(pose, BONE_R_FOREARM, 0.0f, -35.0f * DEG, 0.0f);
        set_euler(pose, BONE_SPINE1, 12.0f * DEG, 0.0f, 0.0f);
        return;
    }

    /* Legs: thigh swing (negative X rotation = forward), knee bends most
     * while the leg recovers forward. */
    thigh = 38.0f * DEG * run * s;
    knee_l = (6.0f + 70.0f * run * (0.5f + 0.5f * sinf(phase - 1.4f))) * DEG;
    knee_r = (6.0f + 70.0f * run * (0.5f - 0.5f * sinf(phase - 1.4f))) * DEG;
    set_euler(pose, BONE_L_THIGH, -thigh, 0.0f, 0.0f);
    set_euler(pose, BONE_R_THIGH, thigh, 0.0f, 0.0f);
    set_euler(pose, BONE_L_SHIN, knee_l * run + 4.0f * DEG, 0.0f, 0.0f);
    set_euler(pose, BONE_R_SHIN, knee_r * run + 4.0f * DEG, 0.0f, 0.0f);

    /* Arms down from the T-pose, swinging opposite to the legs. */
    arm_swing = 32.0f * DEG * run * s;
    set_euler(pose, BONE_L_UPPERARM, arm_swing, 0.0f, arm_down);
    set_euler(pose, BONE_R_UPPERARM, -arm_swing, 0.0f, -arm_down);
    set_euler(pose, BONE_L_FOREARM, 0.0f, elbow, 0.0f);
    set_euler(pose, BONE_R_FOREARM, 0.0f, -elbow, 0.0f);

    /* Lean into the run, counter-rotate the chest, bob twice per cycle. */
    set_euler(pose, BONE_SPINE1, 14.0f * DEG * run + breathe, 9.0f * DEG * run * s, 0.0f);
    set_euler(pose, BONE_NECK, -8.0f * DEG * run, -6.0f * DEG * run * s, 0.0f);
    pose->root_offset[1] = -4.0f * run + 3.0f * run * fabsf(cosf(phase));
}
