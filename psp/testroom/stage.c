#include "stage.h"

#include <pspgu.h>
#include <pspgum.h>
#include "model_blend.h"
#include "near_clip.h"
#include <pspiofilemgr.h>
#include <psputils.h>

#include <malloc.h>
#include <stdio.h>
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
#define BATCH_SKY 8     /* tools/prepare_stage.py: a STAY sky dome, drawn first past the far plane */
#define BATCH_ADD 16    /* the SCM object's blend (byte 8): 2 additive */
#define BATCH_SUB 32    /* 3 subtractive */
/* A batch's blend: 0 opaque, 1 alpha, 2 additive, 3 subtractive (model_blend.h's numbers). */
static int batch_blend(unsigned int flags) {
    return (flags & BATCH_ADD) ? 2 : (flags & BATCH_SUB) ? 3 : (flags & BATCH_BLEND) ? 1 : 0;
}
/* Depth bias for coplanar overlays (prepare_stage.py flags them). With the
 * depth range 65535 (near) .. 0 (far), a positive offset pulls them toward
 * the camera just enough to win against their base surface. */
#define DECAL_DEPTH_OFFSET 64

typedef struct { unsigned int offset, walls, floors, wall_grid, floor_grid, materials; } HitsHeader;   /* materials: 0 none */

static unsigned char *blob;
static const Header *header;
static const HitsHeader *hits;
static unsigned char group_hidden[256];   /* by breakable number */
void stage_hide_group(int number) { if (number > 0 && number < 256) group_hidden[number] = 1; }
static unsigned char object_hidden[512];   /* by a batch's pad[1] (SCM object + 1; 256 + a prop's index): a cutscene's hide tracks */
void stage_set_object_hidden(int number, int hidden) { if (number > 0 && number < 512) object_hidden[number] = (unsigned char)(hidden != 0); }
#define LIFT_TAG 254   /* tools/gimmicks.py: a lift's batches */
/* Crest switches' parts and seals' boards (tools/gimmicks.py switches_and_seals): tags GIM_TAG0.. Each can be posed by a
 * world-space matrix, hidden, or drawn only on request (a seal, tinted and faded by gimmick_models.c). */
#define GIM_TAG0 256
#define GIM_TAGS 64
static struct { unsigned char mode; ScePspFMatrix4 m; } gim[GIM_TAGS];   /* mode: 0 as baked, 1 posed, 2 hidden, 3 on request */
static int gim_tag(int tag) { return tag >= GIM_TAG0 && tag < GIM_TAG0 + GIM_TAGS ? tag - GIM_TAG0 : -1; }
void stage_set_group_matrix(int tag, const float m[12]) {
    int g = gim_tag(tag);
    if (g < 0) return;
    if (!m) { gim[g].mode = 0; return; }
    gim[g].m.x.x = m[0]; gim[g].m.x.y = m[4]; gim[g].m.x.z = m[8];  gim[g].m.x.w = 0.0f;
    gim[g].m.y.x = m[1]; gim[g].m.y.y = m[5]; gim[g].m.y.z = m[9];  gim[g].m.y.w = 0.0f;
    gim[g].m.z.x = m[2]; gim[g].m.z.y = m[6]; gim[g].m.z.z = m[10]; gim[g].m.z.w = 0.0f;
    gim[g].m.w.x = m[3]; gim[g].m.w.y = m[7]; gim[g].m.w.z = m[11]; gim[g].m.w.w = 1.0f;
    gim[g].mode = 1;
}
void stage_set_group_mode(int tag, int mode) { int g = gim_tag(tag); if (g >= 0) gim[g].mode = (unsigned char)mode; }
static float lift_raise;
static struct {float x,z,yaw,raise;int active;float pivot[3],move[3],turn[3],pivot2[3],scale[3];} group_pose[256];
void stage_set_group_pose(int tag,float px,float pz,float yaw,float raise) {
    if(tag<=0 || tag>=256)return;
    group_pose[tag].x=px;group_pose[tag].z=pz;group_pose[tag].yaw=yaw;group_pose[tag].raise=raise;group_pose[tag].active=1;
}
/* A full pose (Secret Mission 12's blocks, blocks.c): moved by move, turned by turn (Rz * Ry * Rx, as the SCM's
 * nodes) about pivot. */
void stage_set_group_motion(int tag, const float pivot[3], const float move[3], const float turn[3]) {
    int k;
    if (tag <= 0 || tag >= 256) return;
    for (k = 0; k < 3; ++k) { group_pose[tag].pivot[k] = pivot[k]; group_pose[tag].move[k] = move[k]; group_pose[tag].turn[k] = turn[k]; }
    for (k = 0; k < 3; ++k) { group_pose[tag].pivot2[k] = pivot[k]; group_pose[tag].scale[k] = 1.0f; }
    group_pose[tag].active = 2;
}
void stage_set_group_motion_scaled(int tag, const float pivot[3], const float move[3], const float turn[3],
                                   const float pivot2[3], const float scale[3]) {
    int k;
    stage_set_group_motion(tag, pivot, move, turn);
    if (tag <= 0 || tag >= 256) return;
    for (k = 0; k < 3; ++k) { group_pose[tag].pivot2[k] = pivot2[k]; group_pose[tag].scale[k] = scale[k]; }
}
void stage_set_lift(float raise) { lift_raise = raise; }
static StageInfo info;
static StageInfo collision_info;
static unsigned char *chunk_visible;   /* per chunk, this frame */
static unsigned int chunk_total;
/* An alpha-tested page whose opaque texels are a near-black field along one edge and a cut-out with bright
 * veins on the rest (room 421 texture 6, the slab across demo 53). Alpha test draws that field solid black
 * over the stone; adding it leaves the stone and keeps the veins. */
#define GLOW_TEX_MAX 256
static unsigned char tex_glow[GLOW_TEX_MAX];

static void mark_glow_plates(void) {
    const TexEntry *textures;
    unsigned int i;
    memset(tex_glow, 0, sizeof tex_glow);
    if (!header || header->textures == 0) return;
    textures = (const TexEntry *)(blob + header->tex_table);
    for (i = 0; i < header->textures && i < GLOW_TEX_MAX; ++i) {
        const TexEntry *t = &textures[i];
        const unsigned char *sw;
        const unsigned int *pal;
        unsigned int x, y, n, area, clear = 0, black = 0, vein = 0, band[4];
        unsigned int bh, bhi;
        if (t->alpha != 1 || t->w < 16 || t->h < 32 || (t->w & 15) || (t->h & 7)) continue;
        if (!t->levels || !t->level_offset[0] || !t->level_offset[7]) continue;
        sw = blob + t->level_offset[0];
        pal = (const unsigned int *)(blob + t->level_offset[7]);
        n = t->w * t->h;
        bh = t->h / 4;
        area = bh * t->w;
        band[0] = band[1] = band[2] = band[3] = 0;
        for (y = 0; y < t->h; ++y) {
            bhi = y / bh;
            if (bhi > 3) bhi = 3;
            for (x = 0; x < t->w; ++x) {
                unsigned int block = ((y / 8) * (t->w / 16) + (x / 16)) * 128u + (y & 7) * 16u + (x & 15);
                unsigned int word = pal[sw[block]];
                unsigned int r = word & 255u, g = (word >> 8) & 255u, b = (word >> 16) & 255u, a = word >> 24;
                unsigned int mx = r > g ? r : g;
                if (b > mx) mx = b;
                if (a < 16u) ++clear;
                else if (a > 240u && mx < 32u) { ++black; ++band[bhi]; }
                else if (a > 240u && mx > 100u) ++vein;
            }
        }
        /* A cut-out, a real black field and bright veins, with the black piled on one edge of the page. */
        {
            unsigned int edge = band[0] > band[3] ? band[0] : band[3];
            unsigned int peak = edge, lo = band[0];
            int k;
            for (k = 1; k < 4; ++k) {
                if (band[k] > peak) peak = band[k];
                if (band[k] < lo) lo = band[k];
            }
            if (clear * 20u > n * 3u && black * 25u > n * 3u && vein * 25u > n &&
                edge * 20u > area * 11u && edge == peak && lo * 25u < area * 3u)
                tex_glow[i] = 1;
        }
    }
}

/* The stage's buffer is kept and a new stage read into it when it fits (a cutscene's stage in the room's place, then
 * the room's again): freed and taken again, a room's few MB don't always come back in one piece. */
static unsigned int blob_capacity, chunk_capacity, stage_loaded_size;
/* Taken once at the first load (the heap still whole then), big enough for every room (ROOM_100.BIN, 5.27 MB, is
 * the largest) and every cutscene's stage (tools/prepare_demo.py keeps them under it): later loads never need more. */
#define STAGE_RESERVE (5400u << 10)

int stage_load(const char *path) {
    memset(group_hidden, 0, sizeof(group_hidden));
    memset(object_hidden, 0, sizeof(object_hidden));
    memset(group_pose,0,sizeof(group_pose));
    memset(gim, 0, sizeof(gim));
    lift_raise = 0.0f;
    stage_clear_boxes();   /* main.c adds the new room's */
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    if (fd < 0) return 0;
    /* Replacing a stage: nothing draws during this. */
    header = 0;
    chunk_total = 0;
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    if (!blob || (unsigned int)size > blob_capacity) {
        /* Both freed first, so the freed space joins up for the bigger one. */
        if (chunk_visible) { free(chunk_visible); chunk_visible = 0; chunk_capacity = 0; }
        unsigned int want = (unsigned int)size > STAGE_RESERVE ? (unsigned int)size : STAGE_RESERVE;
        if (blob) free(blob);
        blob = memalign(64, want);
        if (!blob && want > (unsigned int)size) { want = (unsigned int)size; blob = memalign(64, want); }
        blob_capacity = blob ? want : 0;
    }
    if (!blob) { sceIoClose(fd); return 0; }
    got = sceIoRead(fd, blob, (unsigned int)size);
    stage_loaded_size = (unsigned int)size;
    sceIoClose(fd);
    header = (const Header *)blob;
    if (got != size || memcmp(header->magic, "STG1", 4) != 0 || header->version != 3) {
        header = 0;   /* the buffer stays for the next */
        return 0;
    }
    {
        const Batch *bt = (const Batch *)(blob + header->batch_table);
        unsigned int i;
        for (i = 0; i < header->batches; ++i)
            if (bt[i].chunk_first + bt[i].chunk_count > chunk_total) chunk_total = bt[i].chunk_first + bt[i].chunk_count;
        if (!chunk_visible || chunk_total > chunk_capacity) {
            if (chunk_visible) free(chunk_visible);
            chunk_visible = malloc(chunk_total ? chunk_total : 1);
            chunk_capacity = chunk_visible ? chunk_total : 0;
        }
    }
    sceKernelDcacheWritebackAll();
    mark_glow_plates();
    hits = (const HitsHeader *)(blob + 0x80);
    memcpy(info.spawn, header->spawn, sizeof(info.spawn));
    info.spawn_yaw = header->spawn_yaw;
    memcpy(info.min, header->min, sizeof(info.min));
    memcpy(info.max, header->max, sizeof(info.max));
    info.triangles = (int)(header->vertices / 3);
    collision_info = info;
    if (hits->walls + hits->floors) {
        const float *t = (const float *)(blob + hits->offset);
        unsigned int i, v;
        int k;
        for (k = 0; k < 3; ++k) collision_info.min[k] = collision_info.max[k] = t[k];
        for (i = 0; i < hits->walls + hits->floors; ++i, t += 13)
            for (v = 0; v < 3; ++v) for (k = 0; k < 3; ++k) {
                float a = t[v*3+k];
                if (a < collision_info.min[k]) collision_info.min[k] = a;
                if (a > collision_info.max[k]) collision_info.max[k] = a;
            }
    }
    return 1;
}

/* The stage buffer's unused tail (a cutscene keeps its own data there: demo.c). */
void *stage_spare(unsigned int *size) {
    unsigned int used;
    if (!blob || !header) { *size = 0; return 0; }
    used = (stage_loaded_size + 63u) & ~63u;
    *size = blob_capacity > used ? blob_capacity - used : 0;
    return *size ? blob + used : 0;
}

/* Frees the stage (a cutscene loads its own in its place). */
void stage_unload(void) {
    if (blob) { free(blob); blob = 0; header = 0; }
    if (chunk_visible) { free(chunk_visible); chunk_visible = 0; }
    blob_capacity = chunk_capacity = 0;
    chunk_total = 0;
}

int stage_loaded(void) { return header != 0; }
const StageInfo *stage_info(void) { return &info; }
const StageInfo *stage_collision_info(void) { return &collision_info; }

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
static int stage_double, sky_off, sky_unfog;
void stage_test_sky_off(int off) { sky_off = off; }   /* tests: "nosky 1", the sky pass's cost */
void stage_set_double(int on) { stage_double = on; }
void stage_set_sky_unfog(int on) { sky_unfog = on; }
static int stage_near_cull;
void stage_set_near_cull(int on) { stage_near_cull = on; }

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
    sceGuDisable(GU_LIGHTING);   /* baked vertex colour; a cutscene does not multiply the demo lights on */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    if (stage_double) sceGuEnable(GU_FRAGMENT_2X); else sceGuDisable(GU_FRAGMENT_2X);   /* the GE doubles the colour */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);   /* stage UVs tile */
    sceGuColor(0xffffffff);
    {
        /* The sky (BATCH_SKY, ordered first): under the same projection with its far plane moved out to 500000 (the
         * stage's is 16000; st012's dome reaches 45052, demo 5's st501_3 sky and far ground 185000 and more),
         * without depth, so everything else draws over it. */
        ScePspFMatrix4 far = cull_proj;
        float a = cull_proj.z.z, bz = cull_proj.w.z, n = bz / (a - 1.0f), f = 500000.0f;
        int sky = 0;
        for (i = 0; !sky_off && i < header->batches && (batches[i].flags & BATCH_SKY); ++i) {
            const Batch *b = &batches[i];
            unsigned int k;
            if (!sky) {
                far.z.z = (f + n) / (n - f); far.w.z = 2.0f * f * n / (n - f);
                sceGumMatrixMode(GU_PROJECTION); sceGumLoadMatrix(&far); sceGumMatrixMode(GU_MODEL);
                sceGuDisable(GU_DEPTH_TEST);
                sceGuEnable(GU_BLEND);
                sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
                if (sky_unfog) sceGuDisable(GU_FOG);   /* the dome is past the fog far; leave the texture */
                sky = 1;
            }
            bind_texture(&textures[b->texture]);
            bound = (int)b->texture;
            if (batch_blend(b->flags) >= 2) model_object_blend((unsigned int)batch_blend(b->flags), 1);
            else sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            for (k = 0; k < b->chunk_count; ++k) {
                const Chunk *c = &chunks[b->chunk_first + k];
                /* chunk_hidden tests the sides and the eye, not the far plane, so it holds for the sky's chunks too. */
                if (!chunk_visible[b->chunk_first + k]) continue;
                sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)c->count, 0, vertices + (size_t)c->first * 24);
            }
        }
        if (sky) {
            sceGumMatrixMode(GU_PROJECTION); sceGumLoadMatrix(&cull_proj); sceGumMatrixMode(GU_MODEL);
            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthMask(GU_FALSE);   /* a glow batch turned depth writes off */
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            sceGuDisable(GU_BLEND);
            if (sky_unfog) sceGuEnable(GU_FOG);
        }
    }
    if (stage_near_cull == 1) near_clip_begin();   /* the stage's model matrix is the identity here */
    if (stage_near_cull == 2) sceGuDisable(GU_CLIP_PLANES);   /* the GE drops near-crossing triangles itself */
    for (i = 0; i < header->batches; ++i) {
        const Batch *b = &batches[i];
        unsigned int k, visible = 0;
        if (b->flags & BATCH_SKY) continue;   /* drawn above */
        if (b->pad[0] && b->pad[0] < 256 && group_hidden[b->pad[0]]) continue;   /* a broken prop */
        if (b->pad[1] && b->pad[1] < 512 && object_hidden[b->pad[1]]) continue;   /* hidden by the cutscene */
        int g = gim_tag((int)b->pad[0]);
        if (g >= 0 && gim[g].mode >= 2) continue;   /* hidden, or drawn on request (stage_draw_group) */
        int posed=b->pad[0]<256 && group_pose[b->pad[0]].active;
        int lift = b->pad[0] == LIFT_TAG || posed || (g >= 0 && gim[g].mode == 1);
        for (k = 0; k < b->chunk_count; ++k) visible += chunk_visible[b->chunk_first + k];
        if (!visible && !lift) continue;   /* a lift isn't where its chunks were baked */
        if (lift) {
            ScePspFVector3 up = {0.0f, lift_raise, 0.0f};
            sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
            if (g >= 0) sceGumLoadMatrix(&gim[g].m);
            else if (posed && group_pose[b->pad[0]].active == 2) {
                int tag = b->pad[0];
                ScePspFVector3 at = {group_pose[tag].pivot[0] + group_pose[tag].move[0], group_pose[tag].pivot[1] + group_pose[tag].move[1],
                                     group_pose[tag].pivot[2] + group_pose[tag].move[2]};
                ScePspFVector3 back = {-group_pose[tag].pivot[0], -group_pose[tag].pivot[1], -group_pose[tag].pivot[2]};
                sceGumTranslate(&at);
                sceGumRotateZ(group_pose[tag].turn[2]); sceGumRotateY(group_pose[tag].turn[1]); sceGumRotateX(group_pose[tag].turn[0]);
                sceGumTranslate(&back);
                if (group_pose[tag].scale[0] != 1.0f || group_pose[tag].scale[1] != 1.0f || group_pose[tag].scale[2] != 1.0f) {
                    /* then scaled about its second pivot (Mission 19's sand) */
                    ScePspFVector3 p2 = {group_pose[tag].pivot2[0], group_pose[tag].pivot2[1], group_pose[tag].pivot2[2]};
                    ScePspFVector3 n2 = {-p2.x, -p2.y, -p2.z};
                    ScePspFVector3 sc = {group_pose[tag].scale[0], group_pose[tag].scale[1], group_pose[tag].scale[2]};
                    sceGumTranslate(&p2); sceGumScale(&sc); sceGumTranslate(&n2);
                }
            } else if(posed) {
                int tag=b->pad[0];ScePspFVector3 p={group_pose[tag].x,group_pose[tag].raise,group_pose[tag].z};
                sceGumTranslate(&p);sceGumRotateY(group_pose[tag].yaw);
                p.x=-p.x;p.y=0;p.z=-p.z;sceGumTranslate(&p);
            } else sceGumTranslate(&up);
        }
        if ((int)b->texture != bound) {
            bind_texture(&textures[b->texture]);
            bound = (int)b->texture;
        }
        if (b->flags & BATCH_ALPHA) sceGuEnable(GU_ALPHA_TEST); else sceGuDisable(GU_ALPHA_TEST);
        {
            /* A glow plate adds, like an SCM object's additive blend: its black field contributes nothing and the
             * veins sit on the stone. Other translucent batches blend. Neither writes depth. */
            int glow = (b->flags & BATCH_ALPHA) && b->texture < GLOW_TEX_MAX && tex_glow[b->texture];
            int want = glow ? 2 : batch_blend(b->flags);
            if (want != blend_mode) {
                blend_mode = want;
                if (want >= 2) model_object_blend((unsigned int)want, 1);
                else if (want) {
                    sceGuEnable(GU_BLEND);
                    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
                    sceGuDepthMask(GU_TRUE);
                } else {
                    sceGuDisable(GU_BLEND);
                    sceGuDepthMask(GU_FALSE);
                }
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
            if (stage_near_cull == 1 && !lift && c->count <= 12000 && near_cull_box(c->min, c->max)) {
                /* A cutscene's stage at the eye: the triangles reaching behind the near plane dropped, as the PS2
                 * does (near_clip.h), not clipped into a wall in front of the camera. */
                near_cull_draw(vertices + (size_t)c->first * 24, (int)c->count,
                               GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D);
                continue;
            }
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
    if (stage_near_cull == 2) sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_FRAGMENT_2X);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
}

/* One gimmick tag's batches on their own (a seal's board): the texture times the vertex colour, blended as
 * src * src_fix + dst * dst_fix (fixed colours 0xBBGGRR), without depth writes, posed if it has a matrix. */
void stage_draw_group(int tag, unsigned int src_fix, unsigned int dst_fix) {
    const TexEntry *textures;
    const Batch *batches;
    const Chunk *chunks;
    const unsigned char *vertices;
    unsigned int i, k;
    int g = gim_tag(tag);
    if (!header || g < 0) return;
    textures = (const TexEntry *)(blob + header->tex_table);
    batches = (const Batch *)(blob + header->batch_table);
    vertices = blob + header->vertex_offset;
    chunks = (const Chunk *)(blob + header->chunk_table);
    sceGumMatrixMode(GU_MODEL);
    if (gim[g].mode == 1) sceGumLoadMatrix(&gim[g].m); else sceGumLoadIdentity();
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_ALPHA_TEST);
    if (stage_double) sceGuEnable(GU_FRAGMENT_2X); else sceGuDisable(GU_FRAGMENT_2X);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, src_fix, dst_fix);
    /* A seal's board stands a few cm in front of its door (202's: 5 cm at z 3740): pulled toward the eye like a decal,
     * twice as far, so the door's own decals don't win at the PSP's depth precision. */
    sceGuDepthOffset(DECAL_DEPTH_OFFSET * 2);
    for (i = 0; i < header->batches; ++i) {
        const Batch *b = &batches[i];
        if ((int)b->pad[0] != tag) continue;
        bind_texture(&textures[b->texture]);
        for (k = 0; k < b->chunk_count; ++k) {
            const Chunk *c = &chunks[b->chunk_first + k];
            sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                            (int)c->count, 0, vertices + (size_t)c->first * 24);
        }
    }
    sceGumLoadIdentity();
    sceGuDepthOffset(0);
    sceGuDisable(GU_FRAGMENT_2X);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuDisable(GU_TEXTURE_2D);   /* main.c's gimmick pass draws untextured after it */
}

/* One gimmick tag's batches drawn once more, placed by a world matrix (3 x 4 row-major): the spike traps' panels. Opaque
 * batches as the stage's; alpha-tested ones tested, blended ones blended. */
void stage_draw_group_at(int tag, const float m[12]) {
    const TexEntry *textures;
    const Batch *batches;
    const Chunk *chunks;
    const unsigned char *vertices;
    ScePspFMatrix4 w;
    unsigned int i, k;
    if (!header) return;
    textures = (const TexEntry *)(blob + header->tex_table);
    batches = (const Batch *)(blob + header->batch_table);
    vertices = blob + header->vertex_offset;
    chunks = (const Chunk *)(blob + header->chunk_table);
    w.x.x = m[0]; w.x.y = m[4]; w.x.z = m[8];  w.x.w = 0.0f;
    w.y.x = m[1]; w.y.y = m[5]; w.y.z = m[9];  w.y.w = 0.0f;
    w.z.x = m[2]; w.z.y = m[6]; w.z.z = m[10]; w.z.w = 0.0f;
    w.w.x = m[3]; w.w.y = m[7]; w.w.z = m[11]; w.w.w = 1.0f;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&w);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_DEPTH_TEST);
    if (stage_double) sceGuEnable(GU_FRAGMENT_2X); else sceGuDisable(GU_FRAGMENT_2X);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuColor(0xffffffff);
    for (i = 0; i < header->batches; ++i) {
        const Batch *b = &batches[i];
        if ((int)b->pad[0] != tag) continue;
        bind_texture(&textures[b->texture]);
        if (b->flags & BATCH_ALPHA) sceGuEnable(GU_ALPHA_TEST); else sceGuDisable(GU_ALPHA_TEST);
        if (batch_blend(b->flags)) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); sceGuDepthMask(GU_TRUE); }
        else { sceGuDisable(GU_BLEND); sceGuDepthMask(GU_FALSE); }
        for (k = 0; k < b->chunk_count; ++k) {
            const Chunk *c = &chunks[b->chunk_first + k];
            sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                            (int)c->count, 0, vertices + (size_t)c->first * 24);
        }
    }
    sceGumLoadIdentity();
    sceGuDisable(GU_FRAGMENT_2X);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuDisable(GU_TEXTURE_2D);   /* main.c's gimmick pass draws untextured after it */
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
    for (iteration = 0; iteration < 8; ++iteration) {
        const unsigned int *indices = grid_indices(hits->wall_grid);
        int pushed = 0;
        int gx,gz,cell_reach=radius>60 ? (int)ceilf((radius-60)/(float)header->grid_cell) : 0;
        float bx=*x,bz=*z;
        /* Wall indices have 60 cm padding. Larger boss bodies must inspect
         * neighbouring cells rather than discovering a wall only at its face. */
        for(gz=-cell_reach;gz<=cell_reach;++gz)for(gx=-cell_reach;gx<=cell_reach;++gx) {
        const Cell *cell = grid_cell(hits->wall_grid,bx+gx*header->grid_cell,bz+gz*header->grid_cell);
        if (!cell) continue;
        for (k = 0; k < cell->count; ++k) {
            const float *t = hit_tri(indices[cell->first + k]);
            /* Out of reach by its bounds: the closest point lies inside them, so a triangle more than the radius
             * away across x or z, or above the top height or below the bottom one by more than it, fails the tests
             * below at every height. Skipping it here changes nothing (the street's rubble cells cost up to 8 ms). */
            if (*x < fminf(t[0], fminf(t[3], t[6])) - radius || *x > fmaxf(t[0], fmaxf(t[3], t[6])) + radius ||
                *z < fminf(t[2], fminf(t[5], t[8])) - radius || *z > fmaxf(t[2], fmaxf(t[5], t[8])) + radius ||
                y + heights[0] > fmaxf(t[1], fmaxf(t[4], t[7])) + radius || y + heights[2] < fminf(t[1], fminf(t[4], t[7])) - radius)
                continue;
            for (h = 0; h < 3; ++h) {
                float p[3] = {*x, y + heights[h], *z}, q[3], dx, dz, dy, dist2, reach;
                closest_on_triangle(p, t, t + 3, t + 6, q);
                dx = p[0] - q[0]; dy = p[1] - q[1]; dz = p[2] - q[2];
                if (dy * dy > radius * radius) continue;       /* not at this height */
                dist2 = dx * dx + dz * dz;
                /* A sphere: an edge dy above or below it reaches only sqrt(r^2 - dy^2) across. Pushing the full
                 * radius there shoved him 39 cm sideways off the top of a block he'd just stepped off (M4). */
                reach = sqrtf(radius * radius - dy * dy);
                if (dist2 >= reach * reach) continue;
                if (dist2 < 1e-4f) {                            /* centre on the wall: use its normal */
                    dx = t[9]; dz = t[11];
                    dist2 = dx * dx + dz * dz;
                    if (dist2 < 1e-6f) continue;
                    {
                        float l = sqrtf(dist2);
                        *x += dx / l * reach; *z += dz / l * reach;
                    }
                } else {
                    float l = sqrtf(dist2), push = reach - l, d = sqrtf(dist2 + dy * dy);
                    float nl = sqrtf(t[9] * t[9] + t[10] * t[10] + t[11] * t[11]);
                    /* On a sloped face (the separation along its normal) a sideways step slides the contact along
                     * the slope, so the edge's reach falls short and the passes only creep out (Leviathan's walls
                     * left him 9 cm in). There the step that puts the face r away is (r - d) / sin(slope). */
                    if (nl > 1e-6f && d > 1e-3f) {
                        float c = fabsf(dx * t[9] + dy * t[10] + dz * t[11]) / (nl * d), sinp = l / d;
                        if (c > 0.999f && sinp > 0.2f) {
                            float face = (radius - d) / sinp;
                            if (face > push) push = face < radius ? face : radius;
                        }
                    }
                    *x += dx / l * push; *z += dz / l * push;
                }
                pushed = moved = 1;
                break;
            }
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

static int bounds_axis_overlap(const float v[3][3],const float half[3],const float axis[3]) {
    float a=dot3(v[0],axis),b=dot3(v[1],axis),c=dot3(v[2],axis);
    float radius=half[0]*fabsf(axis[0])+half[1]*fabsf(axis[1])+half[2]*fabsf(axis[2]);
    return fminf(a,fminf(b,c))<=radius && fmaxf(a,fmaxf(b,c))>=-radius;
}
int stage_intersects_bounds(const float origin[3],const float axes[3][3],const float bounds[2][3]) {
    float half[3],centre[3];unsigned int index;int a,k,j;
    if(!header)return 0;
    for(a=0;a<3;++a){half[a]=(bounds[1][a]-bounds[0][a])*.5f;centre[a]=(bounds[0][a]+bounds[1][a])*.5f;}
    for(index=0;index<hits->walls+hits->floors;++index) {
        const float *t=hit_tri(index);float v[3][3],edge[3][3],normal[3];int overlap=1;
        for(k=0;k<3;++k)for(a=0;a<3;++a) {
            float d[3]={t[k*3]-origin[0],t[k*3+1]-origin[1],t[k*3+2]-origin[2]};
            v[k][a]=dot3(d,axes[a])-centre[a];
        }
        /* The three box normals, triangle normal, and nine edge-cross-axis
         * normals form the complete triangle/box separating-axis test. */
        for(a=0;a<3;++a)if(fminf(v[0][a],fminf(v[1][a],v[2][a]))>half[a] ||
            fmaxf(v[0][a],fmaxf(v[1][a],v[2][a])) < -half[a]){overlap=0;break;}
        if(!overlap)continue;
        for(k=0;k<3;++k)for(a=0;a<3;++a)edge[k][a]=v[(k+1)%3][a]-v[k][a];
        normal[0]=edge[0][1]*edge[1][2]-edge[0][2]*edge[1][1];
        normal[1]=edge[0][2]*edge[1][0]-edge[0][0]*edge[1][2];
        normal[2]=edge[0][0]*edge[1][1]-edge[0][1]*edge[1][0];
        if(!bounds_axis_overlap(v,half,normal))continue;
        for(k=0;k<3 && overlap;++k)for(j=0;j<3;++j) {
            float axis[3]={0,0,0};
            axis[(j+1)%3]=edge[k][(j+2)%3];axis[(j+2)%3]=-edge[k][(j+1)%3];
            if(!bounds_axis_overlap(v,half,axis)){overlap=0;break;}
        }
        if(overlap)return 1;
    }
    return 0;
}

int stage_walkable_below(float x, float z) {
    const Cell *cell;
    if (!header) return 0;
    cell = grid_cell(hits->floor_grid, x, z);
    return cell && cell->count > 0;
}

static int shown_floor(float x, float y, float z, float step, float *out);
/* The material (HITS byte 0) of the collision floor stage_floor would pick at (x, z) under y + step; 0 if none. */
int stage_floor_material(float x, float y, float z, float step) {
    const Cell *cell;
    const unsigned int *idx;
    unsigned int k;
    float best = -1.0e30f;
    int material = 0;
    if (!header || !hits->materials) return 0;
    cell = grid_cell(hits->floor_grid, x, z);
    idx = grid_indices(hits->floor_grid);
    if (!cell) return 0;
    for (k = 0; k < cell->count; ++k) {
        unsigned int i = hits->walls + idx[cell->first + k];
        const float *t = hit_tri(i);
        float x0 = t[0], z0 = t[2], x1 = t[3], z1 = t[5], x2 = t[6], z2 = t[8];
        float d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2), a, b, c, h;
        if (d > -1e-6f && d < 1e-6f) continue;
        a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d;
        b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d;
        c = 1.0f - a - b;
        if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
        h = a * t[1] + b * t[4] + c * t[7];
        if (h <= y + step && h > best) { best = h; material = blob[hits->materials + i]; }
    }
    return material;
}

int stage_floor(float x, float y, float z, float step, float *out) {
    int found = 0;
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
    }
    /* Negative tags mark permanent structural boxes with walkable tops
     * (gen_rooms.py: Lady's five bookcase crosspieces). Use their authored
     * centre, half sizes and yaw, shared with collide_boxes. */
    {
        int i;
        for (i = 0; i < box_count; ++i) {
            float top = boxes[i].y + boxes[i].hy, dx, dz, lx, lz;
            if (boxes[i].tag >= 0) continue;
            if (top > y + step || top <= best) continue;
            dx = x-boxes[i].x; dz = z-boxes[i].z;
            lx = boxes[i].c*dx-boxes[i].s*dz;
            lz = boxes[i].s*dx+boxes[i].c*dz;
            if (fabsf(lx) <= boxes[i].hx && fabsf(lz) <= boxes[i].hz) { best = top; found = 1; }
        }
    }
    if (found) { *out = best; return 1; }
    return shown_floor(x, y, z, step, out);
}

/* The drawn floor's own triangles (the grid prepare_stage.py builds from the room's mesh): what's seen, which can
 * sit some centimetres off the collision. */
static int shown_floor(float x, float y, float z, float step, float *out) {
    const float *tris;
    const Cell *cells;
    const unsigned int *indices;
    int cx, cz, found = 0;
    unsigned int k;
    float best = -1.0e30f;
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

int stage_shown_floor(float x, float y, float z, float *out) {
    float g;
    if (!header || !shown_floor(x, y + 60.0f, z, 0.0f, &g) || g < y - 60.0f) return 0;
    *out = g;
    return 1;
}
