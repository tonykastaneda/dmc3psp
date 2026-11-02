#ifndef DMC3PSP_TESTROOM_COAT_H
#define DMC3PSP_TESTROOM_COAT_H

/* Dante's coat tails (pl000_02): a 33-bone rig of six strands hanging from
 * the pelvis, driven by a Verlet cloth simulation in world space, then
 * skinned into Dante's model space so it draws with his model matrix. */

#include "player.h"
#include "skeleton.h"

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } CoatVertex;

int coat_init(void);  /* allocates buffers; 0 on failure */
void coat_reset(void);
/* Steps the cloth (fixed 60 Hz substeps) against the current pose. */
void coat_update(const Pose *pose, const Player *player, float dt);
/* Skins the coat into model space; returns the vertex buffer (dcache flushed). */
const CoatVertex *coat_skin(const Player *player);
const unsigned short *coat_indices(void);
int coat_object_count(void);
void coat_object(int index, unsigned int *index_start, unsigned int *index_count, unsigned int *texture);
/* Strand joint positions in model space, for the debug overlay. */
int coat_joint_count(void);
void coat_joint(int index, float out[3], int *parent_index);

#endif
