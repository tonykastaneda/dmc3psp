#ifndef DMC3PSP_TESTROOM_SKELETON_H
#define DMC3PSP_TESTROOM_SKELETON_H

#include "player.h"

/* Dante's 24-bone skeleton (record indices from the MOD skeleton block). */
enum {
    BONE_ROOT = 0, BONE_HIPS = 1, BONE_SPINE0 = 2, BONE_SPINE1 = 3, BONE_CHEST = 4, BONE_NECK = 5,
    BONE_L_CLAVICLE = 6, BONE_L_UPPERARM = 7, BONE_L_FOREARM = 8, BONE_L_HAND = 9,
    BONE_R_CLAVICLE = 10, BONE_R_UPPERARM = 11, BONE_R_FOREARM = 12, BONE_R_HAND = 13,
    BONE_PELVIS = 14,
    BONE_L_THIGH = 15, BONE_L_SHIN = 16, BONE_L_FOOT = 17, BONE_L_TOE = 18,
    BONE_R_THIGH = 19, BONE_R_SHIN = 20, BONE_R_FOOT = 21, BONE_R_TOE = 22,
    BONE_HEAD = 23
};
/* Note: "left" is the bone on Dante's -X side; Dante faces +Z, so it is his
 * right-hand side anatomically. The names follow the data, not anatomy. */

typedef struct { float m[3][3]; } Mat3;

typedef struct {
    Mat3 local[32];        /* local rotation per bone (identity = rest pose) */
    Mat3 world_rot[32];    /* model-space rotation */
    float world_pos[32][3];/* model-space joint position */
    float root_offset[3];  /* extra translation of the hips (bob) */
} Pose;

void pose_identity(Pose *pose);
void pose_evaluate(Pose *pose);
/* Procedural placeholder animation: idle, run cycle, airborne. */
void pose_procedural(Pose *pose, const Player *player, float time);

#endif
