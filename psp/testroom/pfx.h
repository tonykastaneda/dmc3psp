#ifndef DMC3PSP_TESTROOM_PFX_H
#define DMC3PSP_TESTROOM_PFX_H

/* The game's P effects (particle programs: CPtclLine01 / CPtclPoly00 / CPtclPoly01 / CPtclSprt00, FUN_001b45e0), run
 * from their P records (docs/NATIVE_FX.md, "P effects"). nfx.c starts them for its V timelines' and G generators'
 * P children and draws them after its own elements, so the effect code elsewhere needs no change. Times are 60 Hz
 * frames, distances centimetres. */

/* A frame as nfx.c's: m's columns are the axes (scaled), p the origin. */
typedef struct { float m[3][3], p[3]; } PfxFrame;

void pfx_reset(void);
/* Start P record `rec` (bank b's). place: its frame about the owner (nfx handle `owner`, followed while it lives;
 * 0: place is in the world). root: the owner's root handle (nfx_kill on it ends the particles). loop: as the game's
 * +0x98, a burst that ends starts again while its owner lasts. Returns 1 if it started. */
int pfx_spawn(int bank, const unsigned char *rec, const PfxFrame *place, int owner, int root, int loop);
/* nfx_kill on a root: its particles go at once (FUN_001ad290). */
void pfx_kill_root(int root);
void pfx_update(float frames);
void pfx_draw(const float eye[3]);
int pfx_live(void);           /* instances running */
void pfx_enable(int on);      /* tests ("nopfx 1"): 0 starts no P effects */
int pfx_live_prims(void);     /* their primitives (sprites, triangles, lines) */

#endif
