#ifndef DMC3PSP_TESTROOM_DEMO_CLOTH_H
#define DMC3PSP_TESTROOM_DEMO_CLOTH_H

#include "cloth.h"

/* A cutscene slot's cloth (docs/CUTSCENES.md). The block prepare_demo writes at the actor's last 8 bytes is the
 * .clt chains; this steps them with cloth.c after the slot is posed. Joints stay in world space. ClothMode 1
 * rides owner (the parent model's root) first, the way FUN_00230150 does. The .clt names no capsules; a coat
 * piece on Dante still gets his body capsules so the chains can't hang through him. */

void demo_cloth_reset(void);
/* Forget one slot's chains (they hang at rest again on its next apply): a gameplay user (vergil_boss.c, slots
 * DEMO_CLOTH_GAMEPLAY and up) starting over. */
void demo_cloth_reset_slot(int actor);
#define DEMO_CLOTH_GAMEPLAY 60

/* base is the piece's root (a CnsPos bone for a coat), used when a free bone hangs off the root. owner is the
 * parent model's root, the ride target when ClothMode is 1. Worlds are the posed bones, overwritten for the
 * free ones. block is the baked cloth (0: none). clock is the demo clock, 60 a second. */
void demo_cloth_apply(int actor, const float *base_pos, const float *base_rot,
                      const float *owner_pos, const float *owner_rot,
                      float *world_pos, float *world_rot,
                      const signed char *parents, const float *local, int bones,
                      const void *block, unsigned int size, float clock,
                      const ClothCapsule *capsules, int capsule_count);

#endif
