#ifndef DMC3PSP_TESTROOM_HUD_H
#define DMC3PSP_TESTROOM_HUD_H

/* The game's cockpit (CUIDCockpit00) and lock-on reticle (CUIDLockOnCursor), drawn
 * from its own meshes and textures (id100.pac, tools/prepare_hud.py). */

#include "player.h"

/* Inside the display list. reticle_x / y: screen position, scale from hud_reticle_scale. */
void hud_draw(const Player *player);
void hud_draw_reticle(float x, float y, float scale);
/* FUN_0035b610: full size within 300 cm of the camera, half beyond 1000 cm. */
float hud_reticle_scale(float distance);

#endif
