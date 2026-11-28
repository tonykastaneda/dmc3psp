#ifndef DMC3PSP_TESTROOM_NFX_H
#define DMC3PSP_TESTROOM_NFX_H

/* Native PS2 effects: the game's own V timelines and E elements (tools/native_fx.py), played with their
 * authored delays, placements, scale and colour curves, sheets and MOD meshes. A subset of the PS2 effect
 * runtime (FUN_00208590): E sprites (kind 1 billboard, kind 2 placed) and E models (kind 5), G generators, and P
 * particle programs (pfx.c, started here for V / G children). Times are 60 Hz frames, distances centimetres. */

enum { NFX_STATIC = 0, NFX_CERBERUS = 1, NFX_LEVIATHAN = 2, NFX_DEMO = 3, NFX_AGNI_RUDRA = 4, NFX_JESTER = 5,
       NFX_VANGUARD = 6 /* em008's P312 only */, NFX_NEVAN = 7, NFX_LADY = 8, NFX_ARACHNE = 9, NFX_FALLEN = 10, NFX_MIRROR = 11, NFX_CRYSTAL = 12, NFX_NAUSIKA = 13,
       NFX_GUN_EI = 14, NFX_GUN_SHOTGUN = 15, NFX_GUN_ARTEMIS = 16, NFX_GUN_SPIRAL = 17, NFX_GUN_KALINA = 18, NFX_PALACE = 19, NFX_DANTE = 20,
       NFX_ARM_CERBERUS = 21, NFX_ARM_AGNI = 22, NFX_ARM_NEVAN = 23, NFX_ARM_BEOWULF = 24 /* the Devil Arms' packs, GDATA 98-101 */, NFX_CHESS = 25,
       NFX_ROYAL = 26 /* em021's, the Queen's and King's */, NFX_VERGIL = 27 /* em035's */ };
/* A cutscene's own effects (demo.c): its bank's description at blob + at (0, 0: none). */
void nfx_set_demo_bank(const unsigned char *blob, unsigned int at);

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
int nfx_live_seqs(void);      /* tests: timelines running (of 255) */

/* For pfx.c: bank b's record of kind 'P' or 'A' by id (0 if none); a texture's size and binding (0 if none); a live
 * handle's frame (columns the axes, as pfx's PfxFrame; 0 once it's gone or killed). */
const unsigned char *nfx_record(int bank, int kind, int id);
int nfx_texture_size(int bank, int id, int *width, int *height);
int nfx_bind_texture(int bank, int id);
int nfx_frame(int handle, float m[3][3], float p[3]);
/* pfx.c lets go of the timeline a P child keeps alive (and following its owner) while it plays. */
void nfx_unref(int handle);
/* A P record started on its own at pos, turned by yaw (as nfx_spawn), in the world; loop: bursts until nfx_reset. */
int nfx_spawn_p(int bank, int p_id, const float pos[3], float yaw, int loop);
/* A struck crystal (CStageSetCrystal, FUN_00420450): V0x1c and V4 (type 2) or V554 (type 3), at it. */
void nfx_crystal_break(const float pos[3], int type);

#endif
