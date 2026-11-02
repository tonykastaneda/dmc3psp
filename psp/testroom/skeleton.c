#include "skeleton.h"

#include <math.h>
#include <string.h>

/* Only the bone tables are used here; the unused mesh/texture arrays are
 * static and discarded by the compiler. */
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "motion.h"

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

static void reset_offsets(Pose *pose) {
    int i;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) {
        pose->local_t[i][0] = dmc3_bone_local[i].x;
        pose->local_t[i][1] = dmc3_bone_local[i].y;
        pose->local_t[i][2] = dmc3_bone_local[i].z;
    }
    pose->root_offset[0] = pose->root_offset[1] = pose->root_offset[2] = 0.0f;
}

void pose_identity(Pose *pose) {
    int i;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) mat_identity(&pose->local[i]);
    reset_offsets(pose);
}

void pose_evaluate(Pose *pose) {
    int i, k;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) {
        int parent = dmc3_bone_parent[i];
        const float *offset = pose->local_t[i];
        if (parent < 0) {
            pose->world_rot[i] = pose->local[i];
            for (k = 0; k < 3; ++k) pose->world_pos[i][k] = offset[k];
            continue;
        }
        mat_mul(&pose->world_rot[i], &pose->world_rot[parent], &pose->local[i]);
        for (k = 0; k < 3; ++k) {
            const float *row = pose->world_rot[parent].m[k];
            pose->world_pos[i][k] = pose->world_pos[parent][k] +
                row[0] * offset[0] + row[1] * offset[1] + row[2] * offset[2];
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
    reset_offsets(pose);

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

/* ---- Motion-driven animation ------------------------------------------- */

#define RUN_BLEND_RATE 10.0f
/* The run motion covers 408 cm in 48 frames at 60 fps = 510 cm/s, which is
 * PLAYER_RUN_SPEED; playback is scaled by actual speed so feet don't slide. */

/* MOT rotation order: X applied first, R = Rz * Ry * Rx (verified on the
 * idle: ankles stay exactly planted only with this order). */
static void set_motion_euler(Mat3 *out, float x, float y, float z) {
    Mat3 rx, ry, rz, t;
    mat_rot(&rx, 0, x);
    mat_rot(&ry, 1, y);
    mat_rot(&rz, 2, z);
    mat_mul(&t, &ry, &rx);
    mat_mul(out, &rz, &t);
}

void anim_reset(AnimState *anim) {
    int i;
    memset(anim, 0, sizeof(*anim));
    anim->viewer_motion = MOTION_RUN;
    for (i = 0; i < motion_count(); ++i) {
        if (strncmp(motion_get(i)->name, "rb-", 3) == 0) { anim->viewer_motion = i; break; }
    }
}

int anim_weapon_in_hand(const AnimState *anim, const Player *player, const Combat *combat) {
    /* Viewer: base motions are named "<chunk>-NN"; anything else is a weapon set. */
    if (anim->viewer) return motion_get(anim->viewer_motion)->name[1] != '-';
    return combat->move != MOVE_NONE || (combat->sword_out > 0.0f && player->speed < 1.0f);
}

void anim_update(AnimState *anim, const Player *player, float dt) {
    const Motion *idle = motion_get(MOTION_IDLE), *run = motion_get(MOTION_RUN);
    float target = player->speed > 1.0f ? 1.0f : 0.0f;
    float rate;
    if (anim->viewer) {
        const Motion *m = motion_get(anim->viewer_motion);
        anim->viewer_frame = motion_wrap(m, anim->viewer_frame + dt * MOTION_FPS);
        return;
    }
    anim->run_blend += (target - anim->run_blend) * (1.0f - expf(-RUN_BLEND_RATE * dt));
    anim->idle_frame = motion_wrap(idle, anim->idle_frame + dt * MOTION_FPS);
    rate = player->speed / PLAYER_RUN_SPEED;
    if (rate < 0.35f && target > 0.0f) rate = 0.35f;
    anim->run_frame = motion_wrap(run, anim->run_frame + dt * MOTION_FPS * rate);
    if (anim->run_blend < 0.01f) anim->run_frame = 0.0f; /* restart the stride from the motion start */
}

static void apply_sample(Pose *pose, const MotionSample *a, const MotionSample *b, float w, int keep_root) {
    int bone, k;
    for (bone = 0; bone < DMC3_BONE_COUNT; ++bone) {
        float r[3];
        for (k = 0; k < 3; ++k) {
            float ra = a->has_rot[bone][k] ? a->rot[bone][k] : 0.0f;
            float rb = b && b->has_rot[bone][k] ? b->rot[bone][k] : ra;
            r[k] = ra + (rb - ra) * w;
            if (a->has_trans[bone][k] || (b && b->has_trans[bone][k])) {
                float rest = pose->local_t[bone][k];
                float ta = a->has_trans[bone][k] ? a->trans[bone][k] : rest;
                float tb = b && b->has_trans[bone][k] ? b->trans[bone][k] : ta;
                pose->local_t[bone][k] = ta + (tb - ta) * w;
            }
        }
        set_motion_euler(&pose->local[bone], r[0], r[1], r[2]);
    }
    if (!keep_root) pose->local_t[0][0] = pose->local_t[0][1] = pose->local_t[0][2] = 0.0f;
}

/* out = a + (b - a) * w per channel; a channel missing on one side takes the
 * other side's value (rotation) or the rest offset (translation). */
static void blend_samples(MotionSample *out, const MotionSample *a, const MotionSample *b, float w) {
    int bone, k;
    for (bone = 0; bone < DMC3_BONE_COUNT; ++bone) {
        for (k = 0; k < 3; ++k) {
            float ra = a->has_rot[bone][k] ? a->rot[bone][k] : (b->has_rot[bone][k] ? b->rot[bone][k] : 0.0f);
            float rb = b->has_rot[bone][k] ? b->rot[bone][k] : ra;
            float rest = (&dmc3_bone_local[bone].x)[k];
            float ta = a->has_trans[bone][k] ? a->trans[bone][k] : rest;
            float tb = b->has_trans[bone][k] ? b->trans[bone][k] : rest;
            out->rot[bone][k] = ra + (rb - ra) * w;
            out->trans[bone][k] = ta + (tb - ta) * w;
            out->has_rot[bone][k] = a->has_rot[bone][k] | b->has_rot[bone][k];
            out->has_trans[bone][k] = a->has_trans[bone][k] | b->has_trans[bone][k];
        }
    }
}

static void locomotion_sample(const AnimState *anim, const Combat *combat, MotionSample *out) {
    static MotionSample idle, run;
    int idle_motion = MOTION_IDLE;
    /* Standing with the sword still drawn: Rebellion's ready stance. */
    if (combat && combat->sword_out > 0.0f && combat_idle_motion() >= 0) idle_motion = combat_idle_motion();
    motion_sample(motion_get(idle_motion), motion_wrap(motion_get(idle_motion), anim->idle_frame), &idle);
    motion_sample(motion_get(MOTION_RUN), anim->run_frame, &run);
    blend_samples(out, &idle, &run, anim->run_blend);
}

void anim_pose(const AnimState *anim, const Player *player, const Combat *combat, Pose *pose, float time) {
    static MotionSample sa, sb, sc;
    int attacking = combat && combat->move != MOVE_NONE;
    if (!anim->viewer && !player->on_ground && !attacking) {
        pose_procedural(pose, player, time);
        return;
    }
    if (!anim->viewer && combat && (attacking || combat->fade_weight > 0.0f)) {
        reset_offsets(pose);
        /* Target: the current move (or locomotion once the move ended). */
        if (attacking) motion_sample(motion_get(combat->motion), combat->frame, &sa);
        else locomotion_sample(anim, combat, &sa);
        if (combat->fade_weight > 0.0f) {
            if (combat->fade_motion >= 0) motion_sample(motion_get(combat->fade_motion), combat->fade_frame, &sb);
            else locomotion_sample(anim, combat, &sb);
            blend_samples(&sc, &sa, &sb, combat->fade_weight);
            apply_sample(pose, &sc, 0, 0.0f, 0);
        } else {
            apply_sample(pose, &sa, 0, 0.0f, 0);
        }
        return;
    }
    reset_offsets(pose);
    if (anim->viewer) {
        motion_sample(motion_get(anim->viewer_motion), anim->viewer_frame, &sa);
        apply_sample(pose, &sa, 0, 0.0f, 1);
        /* Keep vertical root motion (jumps) but stay in place horizontally so
         * lunges stay in frame. */
        pose->local_t[0][0] = pose->local_t[0][2] = 0.0f;
        return;
    }
    locomotion_sample(anim, combat, &sa);
    apply_sample(pose, &sa, 0, 0.0f, 0);
}
