#ifndef DMC3PSP_TESTROOM_SHADOW_H
#define DMC3PSP_TESTROOM_SHADOW_H

#include "player.h"
#include "skeleton.h"

/* Dante's shadow from the game's own SHW hulls (GDATA 0 chunk 8: 17 convex hulls, one per bone, tools/gen_dante_caps.py):
 * each hull posed on its bone and dropped flat onto the floor at floor_y, drawn once over (the stencil keeps the hulls'
 * overlaps from darkening twice). rest: the model's rest bone positions (the hulls' points are in the rest pose's model
 * space). alpha 0-255. Returns 0 if there are no hulls (the caller keeps the round shadow). */
int dante_shadow_draw(const Player *player, const Pose *pose, const float (*rest)[3], float floor_y, unsigned int alpha);

#endif
