#ifndef DMC3PSP_TESTROOM_NFX_H
#define DMC3PSP_TESTROOM_NFX_H

/* Native PS2 effects: the game's own V timelines and E elements (tools/native_fx.py), played with their
 * authored delays, placements, scale and colour curves, sheets and MOD meshes. A subset of the PS2 effect
 * runtime (FUN_00208590): E sprites (kind 1 billboard, kind 2 placed) and E models (kind 5). P particle
 * programs and G generators are skipped. Times are 60 Hz frames, distances centimetres. */

enum { NFX_STATIC = 0, NFX_CERBERUS = 1 };

void nfx_reset(void);
/* Start a V timeline at pos, turned by yaw (the PSP's RotateY convention). loop: as the game's
 * effect +0x3a flag, its elements keep playing until nfx_kill. Returns a handle, 0 if none. */
int nfx_spawn(int bank, int v_id, const float pos[3], float yaw, int loop);
/* One E element on its own. */
int nfx_spawn_e(int bank, int e_id, const float pos[3], float yaw, int loop);
/* Follow a moving owner (the game's effect +0x90 matrix pointer). */
void nfx_move(int handle, const float pos[3], float yaw);
void nfx_scale(int handle, float scale);
/* The orbs' colour override (+0x9b mode 2, +0x9c..0x9e): PS2 RGB, 0x80 = 1.0. */
void nfx_tint(int handle, unsigned int r, unsigned int g, unsigned int b);
void nfx_kill(int handle);
int nfx_alive(int handle);
void nfx_update(float frames);
/* After the world and characters, before the 2D. eye: the camera position. */
void nfx_draw(const float eye[3]);
int nfx_live_elements(void);

#endif
