/* Dante's shadow from the game's SHW hulls (shadow.h). The game builds a shadow volume from these (each face's
 * neighbours are in the chunk, for the silhouette); the port drops the posed hulls straight down onto the floor
 * under him and fills what they cover once [estimate: the light's direction and the volume's draw aren't traced; a
 * flat floor under his feet, as the round shadow took]. The fill is the round shadow's black at alpha. */
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>

#include "shadow.h"
#include "dante_shadow_runtime.h"

typedef struct { unsigned int color; float x, y, z; } HullVertex;

int dante_shadow_draw(const Player *player, const Pose *pose, const float (*rest)[3], float floor_y, unsigned int alpha) {
    static float points[sizeof dante_hull_points / sizeof dante_hull_points[0]][2];
    const int tri_count = (int)(sizeof dante_hull_tris / sizeof dante_hull_tris[0]);
    float c = cosf(player->yaw), s = sinf(player->yaw), y = floor_y + 2.0f;
    unsigned int dark = (alpha > 255u ? 255u : alpha) << 24;
    HullVertex *v;
    int h, i, k, n = 0;
    if (!DANTE_HULL_COUNT || !alpha) return DANTE_HULL_COUNT != 0;
    /* Each hull's points on its bone (skin_mesh's rigid case), then turned by his yaw and placed at him (bone_point). */
    for (h = 0; h < DANTE_HULL_COUNT; ++h) {
        const DanteHull *hull = &dante_hulls[h];
        int b = hull->bone < 32 ? hull->bone : 0;
        const float (*r)[3] = pose->world_rot[b].m;
        for (i = hull->first; i < hull->first + hull->count; ++i) {
            const float *p = dante_hull_points[i];
            float d[3] = {p[0] - rest[b][0], p[1] - rest[b][1], p[2] - rest[b][2]}, m[3];
            for (k = 0; k < 3; ++k) m[k] = r[k][0] * d[0] + r[k][1] * d[1] + r[k][2] * d[2] + pose->world_pos[b][k];
            points[i][0] = player->x + c * m[0] + s * m[2];
            points[i][1] = player->z - s * m[0] + c * m[2];
        }
    }
    v = sceGuGetMemory(sizeof(HullVertex) * 3 * tri_count);
    for (h = 0; h < DANTE_HULL_COUNT; ++h) {
        const DanteHull *hull = &dante_hulls[h];
        for (i = hull->first_tri; i < hull->first_tri + hull->tris; ++i)
            for (k = 0; k < 3; ++k) {
                const float *p = points[hull->first + dante_hull_tris[i][k]];
                v[n++] = (HullVertex){dark, p[0], y, p[1]};
            }
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuDepthOffset(96);   /* over the floors' decals, as the round shadow (fx.c) */
    /* The framebuffer's alpha is the stencil. First zero it wherever the hulls fall (colour masked off), then fill
     * where it's still zero, marking it, so each pixel darkens once however many hulls cover it. */
    sceGuEnable(GU_STENCIL_TEST);
    sceGuPixelMask(0x00ffffffu);
    sceGuStencilFunc(GU_ALWAYS, 0, 0xff);
    sceGuStencilOp(GU_REPLACE, GU_REPLACE, GU_REPLACE);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    sceGuPixelMask(0);
    sceGuStencilFunc(GU_EQUAL, 0, 0xff);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_INCR);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuDepthOffset(0);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    return 1;
}
