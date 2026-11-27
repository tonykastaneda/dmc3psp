#ifndef DMC3PSP_TESTROOM_STAGE_H
#define DMC3PSP_TESTROOM_STAGE_H

/* A DMC3 stage built by tools/prepare_stage.py (STAGE.BIN next to the
 * EBOOT): textured, vertex-lit world geometry plus a floor grid. */

typedef struct {
    float spawn[3], spawn_yaw;
    float min[3], max[3];
    int triangles;
} StageInfo;

int stage_load(const char *path);   /* 1 on success */
int stage_loaded(void);
void stage_unload(void);           /* frees it (a cutscene's stage goes in its place) */
void *stage_spare(unsigned int *size);   /* the stage buffer's unused tail, valid until the next load */
const StageInfo *stage_info(void);
/* Native collision bounds, independent of the visible scenery's extent. */
const StageInfo *stage_collision_info(void);
/* Draw inside an open display list with the view/projection already set. */
void stage_draw(void);
/* The GE's colour doubling on the stage's draw (sceGuTexFunc's bit 16): room 012's baked colours are halved for it. */
void stage_set_double(int on);
int stage_last_chunks_drawn(void);  /* chunks that passed culling last draw */
/* Highest floor at (x, z) that is no higher than y + step; returns 0 and
 * leaves *out untouched if there is no floor below. */
int stage_floor(float x, float y, float z, float step, float *out);
/* The drawn floor within 60 cm of y (a collision floor's height): where a shadow goes. 0 if none. */
int stage_shown_floor(float x, float y, float z, float *out);
/* Whether the game's collision has walkable floor near (x, z) (a grid cell with floor triangles): what's
 * outside it is in the walls. */
int stage_walkable_below(float x, float z);
/* A breakable prop's triangles (tagged with its number by tools/prepare_stage.py) aren't drawn once it's
 * broken. Loading a stage shows them all again. */
void stage_hide_group(int number);
/* The room's lift (its batches, tagged by tools/gimmicks.py) is drawn this far above where it was baked. */
void stage_set_lift(float raise);
/* Rigid story-piece pose, about a world-space XZ pivot. Loading resets it. */
void stage_set_group_pose(int tag,float px,float pz,float yaw,float raise);
/* A full pose about pivot: moved by move, turned by turn (radians, Rz * Ry * Rx). Loading resets it. */
void stage_set_group_motion(int tag, const float pivot[3], const float move[3], const float turn[3]);
/* The same, then scaled by scale about pivot2 first (the sand in Mission 19's hourglass). */
void stage_set_group_motion_scaled(int tag, const float pivot[3], const float move[3], const float turn[3],
                                   const float pivot2[3], const float scale[3]);
void stage_clear_boxes(void);
void stage_add_box(float x, float y, float z, float hx, float hy, float hz, float yaw, int tag);
/* A negative box tag enables its walkable top (permanent authored platforms). */
/* Push a vertical capsule (feet at y, height ~180) out of the stage's
 * collision walls; returns 1 if it moved. */
int stage_collide(float *x, float *z, float y, float radius);
/* First hit of segment a->b against collision walls/floors: *t in [0, 1]. */
int stage_raycast(const float a[3], const float b[3], float *t);
/* Read-only native triangle / oriented model-bounds intersection. */
int stage_intersects_bounds(const float origin[3], const float axes[3][3], const float bounds[2][3]);

#endif
