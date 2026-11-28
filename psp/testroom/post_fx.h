#ifndef DMC3PSP_TESTROOM_POST_FX_H
#define DMC3PSP_TESTROOM_POST_FX_H

/* The cutscenes' full-screen ScrEfc passes on the GE (docs/CUTSCENES.md "ScrEfc blur and glow"): a quarter-size copy
 * of the frame, blurred by redrawing it with the GE's bilinear filter at offsets, then blended back. The blur goes
 * only on what's farther than a distance (drawn as 2D sprites at that distance's depth value with the depth test);
 * the glow is the copy's bright part, added back. */

typedef struct {
    int blur;                 /* FCS / DOF: blur what's beyond blur_from (half), full by blur_to */
    float blur_from, blur_to; /* view distances, cm */
    int blur_passes;          /* the clip's Power (1-3) */
    int glow;                 /* SGA / BGA */
    unsigned int glow_color;  /* ABGR, 0x80 = 1.0 */
    float glow_alpha;         /* Alpha / 128 */
    int glow_passes;          /* Feedback */
    float near, far;          /* the frame's projection */
} PostFx;

/* draw_buffer: the frame being drawn (relative VRAM address, as sceGuSwapBuffers returns). Call after the 3D scene,
 * before the 2D (bars, fades, subtitles). */
void post_fx_draw(void *draw_buffer, const PostFx *fx);

#endif
