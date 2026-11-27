#ifndef DMC3_NEAR_CLIP_H
#define DMC3_NEAR_CLIP_H
/* Near-plane clipping for close models. The PSP's GE drops a triangle that crosses the near plane, so a camera close
 * to a model (a cutscene's over-the-shoulder shot, a gameplay camera against Dante) left holes in it. The GE still
 * draws every triangle that doesn't cross, so the model's own indexed draw stays as it is; near_clip_draw then adds,
 * for the crossing triangles only, their part in front of the plane (one or two triangles each).
 *
 * Use: near_clip_begin() once the model matrix is set (it reads GUM's model, view and projection); per model or
 * object, near_clip_sphere() (a dot product: does the near plane reach its bounding sphere?) and only then
 * near_clip_draw() after the normal draw. Vertices: {u, v, nx, ny, nz, x, y, z} floats, 16-bit indices. */

void near_clip_begin(void);
int near_clip_sphere(const float centre[3], float radius);   /* centre in model space */
/* Each vertex's depth once for a model whose sphere passed: returns 0 when all of them are on one side of the plane
 * (then near_clip_draw on these vertices returns at once). Later draws on the same vertices use the cache. */
int near_clip_prepare(const void *vertices, int count);
/* Adds the clipped crossing triangles of indices[0..count) (a triangle list). vtype: the model's draw type without
 * GU_INDEX_16BIT. Returns the triangles added. */
int near_clip_draw(const void *vertices, const unsigned short *indices, int count, int vtype);

/* The PS2's way with a stage near the eye (demo 3's frame 1240: a wall 25-50 cm from the camera, which the PS2 doesn't
 * show): a triangle reaching behind the near plane is dropped whole, not clipped. near_cull_box: does a chunk's box
 * straddle the plane (call near_clip_begin first)? near_cull_draw: draws a non-indexed {u, v, colour, x, y, z}
 * triangle list without those triangles; returns how many it dropped. */
int near_cull_box(const float mn[3], const float mx[3]);
int near_cull_draw(const void *vertices, int count, int vtype);

/* For the cost tests: models tested, models that needed clipping and triangles added since the last call. */
void near_clip_stats(int *tested, int *clipped, int *added);

#endif
