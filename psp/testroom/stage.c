#include "stage.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <psputils.h>

#include <malloc.h>
#include <math.h>
#include <string.h>

typedef struct {
    char magic[4];
    unsigned int version, textures, batches, vertices, floor_tris, grid_w, grid_h;
    float spawn[3], spawn_yaw;
    float min[3], max[3];
    unsigned int tex_table, batch_table, vertex_offset, floor_offset, grid_offset, index_offset;
    float grid_x0, grid_z0;
    unsigned int grid_cell, chunk_table;
} Header;

typedef struct { unsigned int w, h, levels, alpha, level_offset[8]; } TexEntry;
typedef struct { unsigned int texture, first, count, flags, chunk_first, chunk_count, pad[2]; } Batch;
/* A batch's triangles in one XZ cell, with their bounds (culled per frame). */
typedef struct { unsigned int first, count; float min[3], max[3]; } Chunk;
typedef struct { unsigned int first, count; } Cell;

#define BATCH_ALPHA 1
#define BATCH_DECAL 2
#define BATCH_BLEND 4
/* Depth bias for coplanar overlays (prepare_stage.py flags them). With the
 * depth range 65535 (near) .. 0 (far), a positive offset pulls them toward
 * the camera just enough to win against their base surface. */
#define DECAL_DEPTH_OFFSET 64

typedef struct { unsigned int offset, walls, floors, wall_grid, floor_grid; } HitsHeader;

static unsigned char *blob;
static const Header *header;
static const HitsHeader *hits;
static unsigned char group_hidden[256];   /* by breakable number */
void stage_hide_group(int number) { if (number > 0 && number < 256) group_hidden[number] = 1; }
#define LIFT_TAG 254   /* tools/gimmicks.py: a lift's batches */
static float lift_raise;
void stage_set_lift(float raise) { lift_raise = raise; }
static StageInfo info;
static unsigned char *chunk_visible;   /* per chunk, this frame */
static unsigned int chunk_total;

int stage_load(const char *path) {
    memset(group_hidden, 0, sizeof(group_hidden));
    lift_raise = 0.0f;
    stage_clear_boxes();   /* main.c adds the new room's */
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    if (fd < 0) return 0;
    /* Replacing a stage: nothing draws during this. */
    if (blob) { free(blob); blob = 0; header = 0; }
    if (chunk_visible) { free(chunk_visible); chunk_visible = 0; }
    chunk_total = 0;
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    blob = memalign(64, (size_t)size);
    if (!blob) { sceIoClose(fd); return 0; }
    got = sceIoRead(fd, blob, (unsigned int)size);
    sceIoClose(fd);
    header = (const Header *)blob;
    if (got != size || memcmp(header->magic, "STG1", 4) != 0 || header->version != 3) {
        free(blob);
        blob = 0;
        header = 0;
        return 0;
    }
    {
        const Batch *bt = (const Batch *)(blob + header->batch_table);
        unsigned int i;
        for (i = 0; i < header->batches; ++i)
            if (bt[i].chunk_first + bt[i].chunk_count > chunk_total) chunk_total = bt[i].chunk_first + bt[i].chunk_count;
        chunk_visible = malloc(chunk_total ? chunk_total : 1);
    }
    sceKernelDcacheWritebackAll();
    hits = (const HitsHeader *)(blob + 0x80);
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
    /* Swizzled (prepare_stage.py) and mipmapped. Bilinear within the nearest
     * mip level: trilinear reads two levels per pixel, too slow on the GE. */
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(256 / 8, blob + t->level_offset[7]);
    sceGuTexMode(GU_PSM_T8, (int)t->levels - 1, 0, 1);
    for (level = 0; level < t->levels; ++level) {
        int w = (int)(t->w >> level), h = (int)(t->h >> level);
        sceGuTexImage((int)level, w, h, w, blob + t->level_offset[level]);
    }
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0.0f);
    sceGuTexFilter(GU_LINEAR_MIPMAP_NEAREST, GU_LINEAR);
    sceGuTexFlush();
}

/* Clip-space transform for culling: view * projection, read back from GUM. */
static ScePspFMatrix4 cull_view, cull_proj;
static int stage_chunks_drawn;

static void to_clip(const float p[3], float out[4]) {
    const ScePspFMatrix4 *v = &cull_view, *m = &cull_proj;
    float e[4];
    e[0] = v->x.x * p[0] + v->y.x * p[1] + v->z.x * p[2] + v->w.x;
    e[1] = v->x.y * p[0] + v->y.y * p[1] + v->z.y * p[2] + v->w.y;
    e[2] = v->x.z * p[0] + v->y.z * p[1] + v->z.z * p[2] + v->w.z;
    e[3] = 1.0f;
    out[0] = m->x.x * e[0] + m->y.x * e[1] + m->z.x * e[2] + m->w.x * e[3];
    out[1] = m->x.y * e[0] + m->y.y * e[1] + m->z.y * e[2] + m->w.y * e[3];
    out[2] = m->x.z * e[0] + m->y.z * e[1] + m->z.z * e[2] + m->w.z * e[3];
    out[3] = m->x.w * e[0] + m->y.w * e[1] + m->z.w * e[2] + m->w.w * e[3];
}

/* 1 if the box is entirely outside one side of the view (or behind the eye). */
static int chunk_hidden(const Chunk *c) {
    int out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_n = 0, i;
    for (i = 0; i < 8; ++i) {
        float p[3], q[4];
        p[0] = (i & 1) ? c->max[0] : c->min[0];
        p[1] = (i & 2) ? c->max[1] : c->min[1];
        p[2] = (i & 4) ? c->max[2] : c->min[2];
        to_clip(p, q);
        out_l += q[0] < -q[3];
        out_r += q[0] > q[3];
        out_b += q[1] < -q[3];
        out_t += q[1] > q[3];
        out_n += q[3] <= 0.0f;
    }
    return out_l == 8 || out_r == 8 || out_b == 8 || out_t == 8 || out_n == 8;
}

int stage_last_chunks_drawn(void) { return stage_chunks_drawn; }

void stage_draw(void) {
    const TexEntry *textures;
    const Batch *batches;
    const Chunk *chunks;
    const unsigned char *vertices;
    unsigned int i;
    int bound = -1, decal_mode = 0, blend_mode = 0;
    if (!header) return;
    textures = (const TexEntry *)(blob + header->tex_table);
    batches = (const Batch *)(blob + header->batch_table);
    vertices = blob + header->vertex_offset;
    chunks = (const Chunk *)(blob + header->chunk_table);
    sceGumMatrixMode(GU_VIEW);
    sceGumStoreMatrix(&cull_view);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumStoreMatrix(&cull_proj);
    stage_chunks_drawn = 0;
    for (i = 0; i < chunk_total; ++i) chunk_visible[i] = !chunk_hidden(&chunks[i]);

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
        unsigned int k, visible = 0;
        if (b->pad[0] && b->pad[0] < 256 && group_hidden[b->pad[0]]) continue;   /* a broken prop */
        int lift = b->pad[0] == LIFT_TAG;
        for (k = 0; k < b->chunk_count; ++k) visible += chunk_visible[b->chunk_first + k];
        if (!visible && !lift) continue;   /* a lift isn't where its chunks were baked */
        if (lift) {
            ScePspFVector3 up = {0.0f, lift_raise, 0.0f};
            sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); sceGumTranslate(&up);
        }
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
        for (k = 0; k < b->chunk_count; ++k) {
            const Chunk *c = &chunks[b->chunk_first + k];
            unsigned int done = 0;
            if (!chunk_visible[b->chunk_first + k] && !lift) continue;
            ++stage_chunks_drawn;
            /* The GE takes at most 65535 vertices per draw. */
            while (done < c->count) {
                unsigned int n = c->count - done;
                if (n > 65535 - 65535 % 3) n = 65535 - 65535 % 3;
                sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)n, 0, vertices + (size_t)(c->first + done) * 24);
                done += n;
            }
        }
        if (lift) { sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); }
    }
    sceGuDepthOffset(0);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
}

/* ---- collision (the stage's HITS triangles) ---- */

#define HIT_FLOATS 13  /* v0 v1 v2 (9), normal (3), d */

static const Cell *grid_cell(unsigned int grid_offset, float x, float z) {
    int cx = (int)((x - header->grid_x0) / (float)header->grid_cell);
    int cz = (int)((z - header->grid_z0) / (float)header->grid_cell);
    if (cx < 0 || cz < 0 || cx >= (int)header->grid_w || cz >= (int)header->grid_h) return 0;
    return (const Cell *)(blob + grid_offset) + cz * header->grid_w + cx;
}

static const unsigned int *grid_indices(unsigned int grid_offset) {
    return (const unsigned int *)(blob + grid_offset + 8 * header->grid_w * header->grid_h);
}

static const float *hit_tri(unsigned int index) {
    return (const float *)(blob + hits->offset) + HIT_FLOATS * index;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5). */
static void closest_on_triangle(const float *p, const float *a, const float *b, const float *c, float *out) {
    float ab[3], ac[3], ap[3], bp[3], cp[3], d1, d2, d3, d4, d5, d6, va, vb, vc, v, w, denom;
    int k;
    for (k = 0; k < 3; ++k) { ab[k] = b[k] - a[k]; ac[k] = c[k] - a[k]; ap[k] = p[k] - a[k]; }
    d1 = dot3(ab, ap); d2 = dot3(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { memcpy(out, a, 12); return; }
    for (k = 0; k < 3; ++k) bp[k] = p[k] - b[k];
    d3 = dot3(ab, bp); d4 = dot3(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { memcpy(out, b, 12); return; }
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        v = d1 / (d1 - d3);
        for (k = 0; k < 3; ++k) out[k] = a[k] + v * ab[k];
        return;
    }
    for (k = 0; k < 3; ++k) cp[k] = p[k] - c[k];
    d5 = dot3(ab, cp); d6 = dot3(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { memcpy(out, c, 12); return; }
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        w = d2 / (d2 - d6);
        for (k = 0; k < 3; ++k) out[k] = a[k] + w * ac[k];
        return;
    }
    va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (k = 0; k < 3; ++k) out[k] = b[k] + w * (c[k] - b[k]);
        return;
    }
    denom = 1.0f / (va + vb + vc);
    v = vb * denom; w = vc * denom;
    for (k = 0; k < 3; ++k) out[k] = a[k] + ab[k] * v + ac[k] * w;
}

/* The room's solid boxes (COLLISION walls, breakable props; main.c from rooms_runtime.h): turned rectangles in
 * XZ over a height range. A box whose tag is hidden (a broken prop) doesn't stop anything. */
#define STAGE_BOX_MAX 96
static struct { float x, y, z, hx, hy, hz, c, s; int tag; } boxes[STAGE_BOX_MAX];
static int box_count;
void stage_clear_boxes(void) { box_count = 0; }
void stage_add_box(float x, float y, float z, float hx, float hy, float hz, float yaw, int tag) {
    if (box_count >= STAGE_BOX_MAX) return;
    boxes[box_count].x = x; boxes[box_count].y = y; boxes[box_count].z = z;
    boxes[box_count].hx = hx; boxes[box_count].hy = hy; boxes[box_count].hz = hz;
    boxes[box_count].c = cosf(yaw); boxes[box_count].s = sinf(yaw); boxes[box_count].tag = tag;
    ++box_count;
}

/* Out of the boxes: the circle's centre against each box in its own frame (world = Ry * local, as the sets
 * place props: x = c*lx + s*lz, z = -s*lx + c*lz). */
static int collide_boxes(float *x, float *z, float y, float radius) {
    int i, moved = 0;
    for (i = 0; i < box_count; ++i) {
        float dx = *x - boxes[i].x, dz = *z - boxes[i].z, lx, lz, qx, qz, ex, ez, d2;
        if (boxes[i].tag > 0 && boxes[i].tag < 256 && group_hidden[boxes[i].tag]) continue;
        if (y + 160.0f < boxes[i].y - boxes[i].hy || y + 20.0f > boxes[i].y + boxes[i].hy) continue;
        lx = boxes[i].c * dx - boxes[i].s * dz;
        lz = boxes[i].s * dx + boxes[i].c * dz;
        if (fabsf(lx) >= boxes[i].hx + radius || fabsf(lz) >= boxes[i].hz + radius) continue;
        qx = lx < -boxes[i].hx ? -boxes[i].hx : (lx > boxes[i].hx ? boxes[i].hx : lx);
        qz = lz < -boxes[i].hz ? -boxes[i].hz : (lz > boxes[i].hz ? boxes[i].hz : lz);
        ex = lx - qx; ez = lz - qz; d2 = ex * ex + ez * ez;
        if (d2 > 1e-6f) {
            float l = sqrtf(d2);
            if (l >= radius) continue;
            lx = qx + ex / l * radius; lz = qz + ez / l * radius;
        } else if (boxes[i].hx - fabsf(lx) < boxes[i].hz - fabsf(lz)) {
            lx = (lx < 0.0f ? -1.0f : 1.0f) * (boxes[i].hx + radius);   /* inside: out the nearer side */
        } else {
            lz = (lz < 0.0f ? -1.0f : 1.0f) * (boxes[i].hz + radius);
        }
        *x = boxes[i].x + boxes[i].c * lx + boxes[i].s * lz;
        *z = boxes[i].z - boxes[i].s * lx + boxes[i].c * lz;
        moved = 1;
    }
    return moved;
}

int stage_collide(float *x, float *z, float y, float radius) {
    static const float heights[3] = {45.0f, 105.0f, 160.0f};
    int moved = 0, iteration, h;
    unsigned int k;
    if (!header) return 0;
    for (iteration = 0; iteration < 3; ++iteration) {
        const Cell *cell = grid_cell(hits->wall_grid, *x, *z);
        const unsigned int *indices = grid_indices(hits->wall_grid);
        int pushed = 0;
        if (!cell) break;
        for (k = 0; k < cell->count; ++k) {
            const float *t = hit_tri(indices[cell->first + k]);
            for (h = 0; h < 3; ++h) {
                float p[3] = {*x, y + heights[h], *z}, q[3], dx, dz, dy, dist2;
                closest_on_triangle(p, t, t + 3, t + 6, q);
                dx = p[0] - q[0]; dy = p[1] - q[1]; dz = p[2] - q[2];
                if (dy * dy > radius * radius) continue;       /* not at this height */
                dist2 = dx * dx + dz * dz;
                if (dist2 >= radius * radius) continue;
                if (dist2 < 1e-4f) {                            /* centre on the wall: use its normal */
                    dx = t[9]; dz = t[11];
                    dist2 = dx * dx + dz * dz;
                    if (dist2 < 1e-6f) continue;
                    {
                        float l = sqrtf(dist2);
                        *x += dx / l * radius; *z += dz / l * radius;
                    }
                } else {
                    float l = sqrtf(dist2), push = radius - l;
                    *x += dx / l * push; *z += dz / l * push;
                }
                pushed = moved = 1;
                break;
            }
        }
        if (collide_boxes(x, z, y, radius)) pushed = moved = 1;
        if (!pushed) break;
    }
    return moved;
}

static int ray_triangle(const float *o, const float *d, const float *t, float *hit) {
    /* Moller-Trumbore, two-sided. */
    float e1[3], e2[3], pv[3], tv[3], qv[3], det, inv, u, v, s;
    int k;
    for (k = 0; k < 3; ++k) { e1[k] = t[3 + k] - t[k]; e2[k] = t[6 + k] - t[k]; }
    pv[0] = d[1] * e2[2] - d[2] * e2[1]; pv[1] = d[2] * e2[0] - d[0] * e2[2]; pv[2] = d[0] * e2[1] - d[1] * e2[0];
    det = dot3(e1, pv);
    if (det > -1e-6f && det < 1e-6f) return 0;
    inv = 1.0f / det;
    for (k = 0; k < 3; ++k) tv[k] = o[k] - t[k];
    u = dot3(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    qv[0] = tv[1] * e1[2] - tv[2] * e1[1]; qv[1] = tv[2] * e1[0] - tv[0] * e1[2]; qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
    v = dot3(d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return 0;
    s = dot3(e2, qv) * inv;
    if (s < 0.0f || s > 1.0f) return 0;
    *hit = s;
    return 1;
}

int stage_raycast(const float a[3], const float b[3], float *t) {
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, best = 2.0f;
    float x0 = a[0] < b[0] ? a[0] : b[0], x1 = a[0] < b[0] ? b[0] : a[0];
    float z0 = a[2] < b[2] ? a[2] : b[2], z1 = a[2] < b[2] ? b[2] : a[2];
    float cell = (float)header->grid_cell, x, z;
    int grid;
    if (!header) return 0;
    /* The segment is short (camera boom): scan every cell its box covers in
     * both grids; a triangle in several cells is just tested twice. */
    for (grid = 0; grid < 2; ++grid) {
        unsigned int offset = grid ? hits->floor_grid : hits->wall_grid;
        unsigned int base = grid ? hits->walls : 0;
        const unsigned int *indices = grid_indices(offset);
        for (z = z0; z <= z1 + cell; z += cell) {
            for (x = x0; x <= x1 + cell; x += cell) {
                const Cell *c = grid_cell(offset, x > x1 ? x1 : x, z > z1 ? z1 : z);
                unsigned int k;
                if (!c) continue;
                for (k = 0; k < c->count; ++k) {
                    float h;
                    if (ray_triangle(a, d, hit_tri(base + indices[c->first + k]), &h) && h < best) best = h;
                }
            }
        }
    }
    if (best > 1.0f) return 0;
    *t = best;
    return 1;
}

int stage_walkable_below(float x, float z) {
    const Cell *cell;
    if (!header) return 0;
    cell = grid_cell(hits->floor_grid, x, z);
    return cell && cell->count > 0;
}

int stage_floor(float x, float y, float z, float step, float *out) {
    const float *tris;
    const Cell *cells;
    const unsigned int *indices;
    int cx, cz, found = 0;
    unsigned int k;
    float best = -1.0e30f;
    if (!header) return 0;
    {
        /* Collision floors first (the game's own walkable surfaces). */
        const Cell *cell = grid_cell(hits->floor_grid, x, z);
        const unsigned int *idx = grid_indices(hits->floor_grid);
        if (cell) {
            for (k = 0; k < cell->count; ++k) {
                const float *t = hit_tri(hits->walls + idx[cell->first + k]);
                float x0 = t[0], z0 = t[2], x1 = t[3], z1 = t[5], x2 = t[6], z2 = t[8];
                float d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2), a, b, c, h;
                if (d > -1e-6f && d < 1e-6f) continue;
                a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d;
                b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d;
                c = 1.0f - a - b;
                if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
                h = a * t[1] + b * t[4] + c * t[7];
                if (h <= y + step && h > best) { best = h; found = 1; }
            }
        }
        if (found) { *out = best; return 1; }
    }
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
