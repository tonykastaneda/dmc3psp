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

/* Blend lengths in 60 fps frames, from the motion requests in FUN_00221ba0 and
 * FUN_00221d20 (start 3, walk <-> run 5). The stop length is passed in by the
 * caller of FUN_0021d280 and isn't decoded; 10 is a value idle requests elsewhere.
 * Motions play at their authored rate. */
#define START_BLEND_FRAMES 3.0f
#define STOP_BLEND_FRAMES 10.0f
#define WALK_RUN_BLEND_FRAMES 5.0f

static float approach(float value, float target, float step) {
    if (value < target) { value += step; return value > target ? target : value; }
    value -= step;
    return value < target ? target : value;
}

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
    if (combat->move >= MOVE_TR_DASH || combat->stance_active) return 0;  /* style moves and guns keep Rebellion on the back */
    return combat->move != MOVE_NONE || (combat->sword_out > 0.0f && player->speed < 1.0f);
}

void anim_update(AnimState *anim, const Player *player, float dt) {
    const Motion *idle = motion_get(MOTION_IDLE), *walk = motion_get(MOTION_WALK), *run = motion_get(MOTION_RUN);
    int moving = player->speed > 1.0f;
    float frames = dt * MOTION_FPS;
    if (anim->viewer) {
        const Motion *m = motion_get(anim->viewer_motion);
        anim->viewer_frame = motion_wrap(m, anim->viewer_frame + dt * MOTION_FPS);
        return;
    }
    /* A motion restarts from its first frame each time it is requested. */
    if (moving && anim->move_blend <= 0.0f) {
        anim->walk_frame = anim->run_frame = 0.0f;
        anim->run_blend = player->running ? 1.0f : 0.0f;
    } else if (player->running != anim->was_running) {
        if (player->running) anim->run_frame = 0.0f;
        else anim->walk_frame = 0.0f;
    }
    anim->was_running = player->running;
    anim->move_blend = approach(anim->move_blend, moving ? 1.0f : 0.0f,
                                frames / (moving ? START_BLEND_FRAMES : STOP_BLEND_FRAMES));
    anim->run_blend = approach(anim->run_blend, player->running ? 1.0f : 0.0f, frames / WALK_RUN_BLEND_FRAMES);
    anim->idle_frame = motion_wrap(idle, anim->idle_frame + frames);
    anim->walk_frame = motion_wrap(walk, anim->walk_frame + frames);
    anim->run_frame = motion_wrap(run, anim->run_frame + frames);
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

/* The gun stance's legs, with the blend between rows (FUN_002243e0). */
static void stance_legs_sample(const Combat *combat, MotionSample *out) {
    static MotionSample a, b;
    motion_sample(motion_get(combat->lower_motion), combat->lower_frame, &a);
    if (combat->lower_fade_weight > 0.0f && combat->lower_fade_motion >= 0) {
        motion_sample(motion_get(combat->lower_fade_motion), combat->lower_fade_frame, &b);
        blend_samples(out, &a, &b, combat->lower_fade_weight);
    } else {
        *out = a;
    }
}

/* What the arms' motion doesn't drive comes from the legs'. */
static void overlay_upper(MotionSample *upper, const MotionSample *legs) {
    int bone, k;
    for (bone = 0; bone < MOTION_MAX_BONES; ++bone) {
        for (k = 0; k < 3; ++k) {
            if (!upper->has_rot[bone][k] && legs->has_rot[bone][k]) { upper->rot[bone][k] = legs->rot[bone][k]; upper->has_rot[bone][k] = 1; }
            if (!upper->has_trans[bone][k] && legs->has_trans[bone][k]) { upper->trans[bone][k] = legs->trans[bone][k]; upper->has_trans[bone][k] = 1; }
        }
    }
}

static void locomotion_sample(const AnimState *anim, const Combat *combat, MotionSample *out) {
    static MotionSample idle, walk, run, move;
    const MotionSample *moving;
    int idle_motion = MOTION_IDLE;
    /* Standing with the sword still drawn: Rebellion's ready stance. */
    if (combat && combat->sword_out > 0.0f && combat_idle_motion() >= 0) idle_motion = combat_idle_motion();
    motion_sample(motion_get(idle_motion), motion_wrap(motion_get(idle_motion), anim->idle_frame), &idle);
    /* Only the motions with weight are sampled: this runs on the PSP every frame. */
    if (anim->move_blend <= 0.0f) { *out = idle; return; }
    if (anim->run_blend > 0.0f) motion_sample(motion_get(MOTION_RUN), anim->run_frame, &run);
    if (anim->run_blend < 1.0f) motion_sample(motion_get(MOTION_WALK), anim->walk_frame, &walk);
    if (anim->run_blend >= 1.0f) moving = &run;
    else if (anim->run_blend <= 0.0f) moving = &walk;
    else { blend_samples(&move, &walk, &run, anim->run_blend); moving = &move; }
    blend_samples(out, &idle, moving, anim->move_blend);
}

#define AIR_BLEND_RATE 14.0f

void anim_pose(const AnimState *anim, const Player *player, const Combat *combat, Pose *pose, float time) {
    static MotionSample sa, sb, sc, sj;
    static int fall_motion = -2;
    static float air_weight = 0.0f, fall_frame = 0.0f, last_time = 0.0f;
    int attacking = combat && combat->move != MOVE_NONE;
    float dt = time - last_time, target;
    int air_source = -1;
    float air_frame = 0.0f;
    last_time = time;
    if (dt < 0.0f || dt > 0.2f) dt = 1.0f / 60.0f;
    if (fall_motion == -2) fall_motion = motion_find("2-16");  /* in-air loop (6 frames, loops at 5) */
    (void)pose_procedural;

    /* Jump / landing / fall pose source, blended over locomotion. */
    if (!anim->viewer && !attacking) {
        if (player->jump_motion >= 0) {
            air_source = player->jump_motion;
            air_frame = player->jump_frame;
        } else if (!player->on_ground && fall_motion >= 0) {
            fall_frame = motion_wrap(motion_get(fall_motion), fall_frame + dt * MOTION_FPS);
            air_source = fall_motion;
            air_frame = fall_frame;
        }
    }
    target = air_source >= 0 ? 1.0f : 0.0f;
    air_weight += (target - air_weight) * (1.0f - expf(-AIR_BLEND_RATE * dt));
    if (air_source >= 0) {
        reset_offsets(pose);
        motion_sample(motion_get(air_source), air_frame, &sj);
        if (air_weight < 0.98f) {
            locomotion_sample(anim, combat, &sa);
            blend_samples(&sc, &sa, &sj, air_weight);
            apply_sample(pose, &sc, 0, 0.0f, 0);
        } else {
            apply_sample(pose, &sj, 0, 0.0f, 0);
        }
        return;
    }
    if (!anim->viewer && combat && (attacking || combat->stance_active || combat->fade_weight > 0.0f)) {
        reset_offsets(pose);
        /* Target: the current move (or locomotion once the move ended). */
        if (attacking) {
            motion_sample(motion_get(combat->motion), combat->frame, &sa);
            /* Upper-body shot over the legs. */
            if (combat->lower_motion >= 0 && combat->lower_motion != combat->motion) {
                static MotionSample legs;
                stance_legs_sample(combat, &legs);
                overlay_upper(&sa, &legs);
            }
        }
        else if (combat->stance_active) {
            int upper;
            float upper_frame;
            static MotionSample legs;
            combat_stance_motion(combat, &upper, &upper_frame);
            motion_sample(motion_get(upper), upper_frame, &sa);
            stance_legs_sample(combat, &legs);
            overlay_upper(&sa, &legs);
        } else locomotion_sample(anim, combat, &sa);
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
