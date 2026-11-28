#ifndef DMC3PSP_TESTROOM_DEMO_H
#define DMC3PSP_TESTROOM_DEMO_H
/* The game's real-time cutscenes (demo.c, docs/CUTSCENES.md). */
#include "player.h"

int demo_start(int demo, const char *dir);   /* loads DEMO_nnn.BIN and DSTG_nnn.BIN from dir; 1 on success */
void demo_stop(void);
int demo_active(void);
int demo_id(void);
float demo_frame(void);                       /* the demo's clock, 60 a second */
int demo_update(float dt, FollowCamera *camera);   /* 1 while it plays: the camera is the demo's */
void demo_draw(void);                         /* the cast, after the stage */
int demo_actor_count(void);
unsigned int demo_fade(void);   /* the Fade track's cover now, ABGR (0: none) */
/* A demo's frame: cinema bars (0.712 of the height shows). The projection uses demo_screen_fov,
 * the 326-line fit. demo_raw_fov (test line "rawfov 1") passes channel 12 raw. */
extern int demo_raw_fov;
extern int demo_no_fog;                     /* tests: "nofog 1" leaves a cutscene's GFG off */
float demo_screen_fov(float fov_degrees);
void demo_letterbox(void);                  /* the bars, in the 2D pass */
int demo_fog(void);                         /* GFG on this frame: 1, and sceGuFog is set */
const char *demo_subtitle(int *x, int *y, int *size);   /* the line shown now, or 0 (PS2 640 x 448 place) */
#include "post_fx.h"
/* The ScrEfc blur / glow on this frame (post_fx.c), for a projection's near and far: 1 if any. */
int demo_post_fx(PostFx *fx, float near, float far);
extern int demo_no_post;                    /* tests: "nopost 1" */
#endif
