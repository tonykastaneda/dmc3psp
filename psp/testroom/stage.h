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
const StageInfo *stage_info(void);
/* Draw inside an open display list with the view/projection already set. */
void stage_draw(void);
/* Highest floor at (x, z) that is no higher than y + step; returns 0 and
 * leaves *out untouched if there is no floor below. */
int stage_floor(float x, float y, float z, float step, float *out);

#endif
