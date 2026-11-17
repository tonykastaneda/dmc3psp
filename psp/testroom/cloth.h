#ifndef DMC3PSP_TESTROOM_CLOTH_H
#define DMC3PSP_TESTROOM_CLOTH_H

/* The game's chain (CCnsChain): one bone of a hanging chain, stepped once per 60 fps frame by FUN_0022f270 with
 * its .clt's values. coat.c runs Dante's coat the same way; this is the one an enemy's parts use (Nevan's hair and
 * dress), in the enemy's model space: the game moves its chains with their owner's root every frame (FUN_00230150),
 * so only the body's own motion and gravity swing them. */

typedef struct { float m[3][3]; } ClothM3;

typedef struct {
    float pos[3], vel[3];
    ClothM3 rot;
} ClothJoint;

typedef struct {
    float gravity, spring, max_speed, stiffness;
} ClothParams;

/* A capsule, both ends and the radius already where the body is now. */
typedef struct { float a[3], b[3], radius; } ClothCapsule;

/* One step of a free bone: its parent (where and how it's turned now), its rest offset from that parent, the
 * capsules it can't enter. */
void cloth_step(ClothJoint *joint, const float parent_pos[3], const ClothM3 *parent_rot, const float rest_offset[3],
                const ClothParams *params, const ClothCapsule *capsules, int capsule_count);

/* Hangs a bone at its rest place under its parent, still. */
void cloth_rest(ClothJoint *joint, const float parent_pos[3], const ClothM3 *parent_rot, const float rest_offset[3]);

#endif
