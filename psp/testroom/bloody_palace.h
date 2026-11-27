#ifndef DMC3_BLOODY_PALACE_H
#define DMC3_BLOODY_PALACE_H
/* PS2 EventTbl21; sources and remaining approximations: docs/BLOODY_PALACE.md. */
#define BLOODY_PALACE_MISSION 21
typedef struct { short op, a, b, c, d; } PalaceOp;
void bp_reset(int enabled);
void bp_enter(int room);
int bp_active(void);
int bp_floor(void);
int bp_op(int index, PalaceOp *out);
void bp_update(float frames, int cleared);
int bp_portal_ready(int portal);
int bp_route(int portal, int *room, int *start); /* mutates floor only when a ready portal is taken */
int bp_finished(void);
void bp_draw(void);
void bp_status(char *out, int size);
int bp_pickup_position(float out[3]); /* returns 12 green / 15 white, or 0 */
void bp_pickup_fall(float frames, float floor_y);
int bp_pickup_collect(float x, float y, float z);
void bp_test_start(int floor, int visits, int boss, int group);
#endif
