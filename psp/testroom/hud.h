#ifndef DMC3PSP_TESTROOM_HUD_H
#define DMC3PSP_TESTROOM_HUD_H

/* The game's cockpit (CUIDCockpit00) and lock-on reticle (CUIDLockOnCursor), drawn
 * from its own meshes and textures (id100.pac, tools/prepare_hud.py). */

#include "player.h"

/* Inside the display list. reticle_x / y: screen position, scale from hud_reticle_scale. */
void hud_draw(const Player *player);
/* NULL name / nonpositive health hides the gauge. No enemy-specific dependency. */
void hud_draw_boss(const char *name, float health, float maximum);
void hud_draw_reticle(float x, float y, float scale);
/* The kill counter of Mission 20's ending street (FUN_00313a40's mode 0): up to three red digits, top right. */
void hud_draw_kills(int count);
/* FUN_0035b610: full size within 300 cm of the camera, half beyond 1000 cm. */
float hud_reticle_scale(float distance);

#endif
