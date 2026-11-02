#include "stage.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <psputils.h>

#include <malloc.h>
#include <string.h>

typedef struct {
    char magic[4];
    unsigned int version, textures, batches, vertices, floor_tris, grid_w, grid_h;
    float spawn[3], spawn_yaw;
    float min[3], max[3];
    unsigned int tex_table, batch_table, vertex_offset, floor_offset, grid_offset, index_offset;
    float grid_x0, grid_z0;
    unsigned int grid_cell, reserved;
} Header;

typedef struct { unsigned int w, h, levels, alpha, level_offset[8]; } TexEntry;
typedef struct { unsigned int texture, first, count, flags; } Batch;
typedef struct { unsigned int first, count; } Cell;

#define BATCH_ALPHA 1
#define BATCH_DECAL 2
#define BATCH_BLEND 4
/* Depth bias for coplanar overlays (prepare_stage.py flags them). With the
 * depth range 65535 (near) .. 0 (far), a positive offset pulls them toward
 * the camera just enough to win against their base surface. */
#define DECAL_DEPTH_OFFSET 64

static unsigned char *blob;
static const Header *header;
static StageInfo info;

int stage_load(const char *path) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    if (fd < 0) return 0;
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    blob = memalign(64, (size_t)size);
    if (!blob) { sceIoClose(fd); return 0; }
    got = sceIoRead(fd, blob, (unsigned int)size);
    sceIoClose(fd);
    header = (const Header *)blob;
    if (got != size || memcmp(header->magic, "STG1", 4) != 0 || header->version != 1) {
        free(blob);
        blob = 0;
        header = 0;
        return 0;
    }
    sceKernelDcacheWritebackAll();
    memcpy(info.spawn, header->spawn, sizeof(info.spawn));
    info.spawn_yaw = header->spawn_yaw;
    memcpy(info.min, header->min, sizeof(info.min));
    memcpy(info.max, header->max, sizeof(info.max));
    info.triangles = (int)(header->vertices / 3);
    return 1;
}

int stage_loaded(void) { return header != 0; }
const StageInfo *stage_info(void) { return &info; }

static void bind_texture(const TexEntry *t) {
    unsigned int level;
    /* Mipmapped, trilinear: distant tiles average instead of shimmering. */
    sceGuTexMode(GU_PSM_8888, (int)t->levels - 1, 0, 0);
    for (level = 0; level < t->levels; ++level) {
        int w = (int)(t->w >> level), h = (int)(t->h >> level);
        sceGuTexImage((int)level, w, h, w, blob + t->level_offset[level]);
    }
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0.0f);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR, GU_LINEAR);
    sceGuTexFlush();
}

void stage_draw(void) {
    const TexEntry *textures;
    const Batch *batches;
    const unsigned char *vertices;
    unsigned int i;
    int bound = -1, decal_mode = 0, blend_mode = 0;
    if (!header) return;
    textures = (const TexEntry *)(blob + header->tex_table);
    batches = (const Batch *)(blob + header->batch_table);
    vertices = blob + header->vertex_offset;

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);   /* stage UVs tile */
    sceGuColor(0xffffffff);
    for (i = 0; i < header->batches; ++i) {
        const Batch *b = &batches[i];
        unsigned int done = 0;
        if ((int)b->texture != bound) {
            bind_texture(&textures[b->texture]);
            bound = (int)b->texture;
        }
        if (b->flags & BATCH_ALPHA) sceGuEnable(GU_ALPHA_TEST); else sceGuDisable(GU_ALPHA_TEST);
        if (((b->flags & BATCH_BLEND) != 0) != blend_mode) {
            /* Translucent overlays (drawn last): blend, keep depth test, no depth writes. */
            blend_mode = (b->flags & BATCH_BLEND) != 0;
            if (blend_mode) {
                sceGuEnable(GU_BLEND);
                sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
                sceGuDepthMask(GU_TRUE);
            } else {
                sceGuDisable(GU_BLEND);
                sceGuDepthMask(GU_FALSE);
            }
        }
        if (((b->flags & BATCH_DECAL) != 0) != decal_mode) {
            decal_mode = (b->flags & BATCH_DECAL) != 0;
            sceGuDepthOffset(decal_mode ? DECAL_DEPTH_OFFSET : 0);
        }
        /* The GE takes at most 65535 vertices per draw; batches are split. */
        while (done < b->count) {
            unsigned int n = b->count - done;
            if (n > 65535 - 65535 % 3) n = 65535 - 65535 % 3;
            sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                            (int)n, 0, vertices + (size_t)(b->first + done) * 24);
            done += n;
        }
    }
    sceGuDepthOffset(0);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
}

int stage_floor(float x, float y, float z, float step, float *out) {
    const float *tris;
    const Cell *cells;
    const unsigned int *indices;
    int cx, cz, found = 0;
    unsigned int k;
    float best = -1.0e30f;
    if (!header) return 0;
    cx = (int)((x - header->grid_x0) / (float)header->grid_cell);
    cz = (int)((z - header->grid_z0) / (float)header->grid_cell);
    if (cx < 0 || cz < 0 || cx >= (int)header->grid_w || cz >= (int)header->grid_h) return 0;
    tris = (const float *)(blob + header->floor_offset);
    cells = (const Cell *)(blob + header->grid_offset);
    indices = (const unsigned int *)(blob + header->index_offset);
    for (k = 0; k < cells[cz * header->grid_w + cx].count; ++k) {
        const float *t = tris + 9 * indices[cells[cz * header->grid_w + cx].first + k];
        /* Barycentric test in XZ, then interpolate Y. */
        float x0 = t[0], z0 = t[2], x1 = t[3], z1 = t[5], x2 = t[6], z2 = t[8];
        float d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        float a, b, c, h;
        if (d > -1e-6f && d < 1e-6f) continue;
        a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d;
        b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d;
        c = 1.0f - a - b;
        if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
        h = a * t[1] + b * t[4] + c * t[7];
        if (h <= y + step && h > best) { best = h; found = 1; }
    }
    if (found) *out = best;
    return found;
}
