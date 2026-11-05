/* DMC3 PSP test room: hardware-rendered (sceGu) skinned Dante on a grey floor
 * with a white grid. Controls are listed in player.h; HOME quits.
 *
 * Dev flags (files next to the EBOOT):
 *   AUTOEXIT.TXT  scripted autotest: screenshots + TESTROOM_OK.TXT / _FAIL.TXT
 *   RECORD.TXT    scripted demo, every other frame saved as REC####.BMP;
 *                 if it contains "motions", a tour of every MOT instead
 *   BONES.TXT     draw the skeleton overlay
 */

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdebug.h>
#include <psputils.h>
#include <psppower.h>

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model_runtime.h"
#include "player.h"
#include "skeleton.h"
#include "motion.h"
#include "coat.h"
#include "menu.h"
#include "movie.h"
#include "rooms_runtime.h"
#include "combat.h"
#include "stage.h"
#include "swizzle.h"
#include "enemy.h"
#include "bullets.h"
#include "fx.h"
#include "hud.h"
#include "orbs.h"
#include "rebellion_runtime.h"
#include "ebony_runtime.h"
#include "ivory_runtime.h"
#include "shotgun_runtime.h"
#include "artemis_runtime.h"
#include "spiral_runtime.h"
#include "kalina_runtime.h"
#include "cerberus_runtime.h"
#include "agni_runtime.h"
#include "nevan_runtime.h"
#include "beowulf_runtime.h"
#include "beowulf_groups.h"
#include "devil_runtime.h"

PSP_MODULE_INFO("DMC3TestRoom", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define FRAMEBUFFER_BYTES (BUF_WIDTH * SCR_HEIGHT * 4)

/* Verified in PPSSPP screenshots and by the skeleton (toes at z=+9.9): the
 * MOD front is +Z, so no extra rotation is needed. */
#define MODEL_YAW_OFFSET 0.0f

/* HUD font: the PSPSDK debug font (8x8, MSB = leftmost pixel) baked into a
 * 128x128 RGBA texture, 16x16 glyphs. Drawn through the GU, not by CPU writes
 * into VRAM, so it works under PPSSPP's hardware renderer too. */
#define FONT_TEX_SIZE 128
extern u8 msx[];

#define ROOM_TILE 100.0f
#define ROOM_TILES 20
#define FLOOR_COLOR 0xff6e6e6eu
#define GRID_COLOR 0xffffffffu
#define CLEAR_COLOR 0xff2a2420u
#define BONE_COLOR 0xff00e0ffu  /* yellow (ABGR) */
#define COAT_BONE_COLOR 0xffff8040u  /* light blue */

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } ModelVertex;
typedef struct { unsigned int color; float x, y, z; } ColorVertex;
typedef struct { float u, v; float x, y, z; } TextVertex;

#define MODEL_VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)
#define COLOR_VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

#define FLOOR_VERTEX_COUNT (ROOM_TILES * ROOM_TILES * 6)
#define GRID_VERTEX_COUNT ((ROOM_TILES + 1) * ROOM_TILES * 2 * 2)

static unsigned int __attribute__((aligned(16))) display_list[65536];
static ColorVertex __attribute__((aligned(16))) floor_vertices[FLOOR_VERTEX_COUNT];
static ColorVertex __attribute__((aligned(16))) grid_vertices[GRID_VERTEX_COUNT];
static ModelVertex *model_vertices;   /* skinned every frame */
static unsigned short *model_indices;
static int t8_load(void **out, const unsigned char *source, int width, int height);
static void t8_bind(int width, int height, const void *texture);
static void *model_textures[DMC3_MODEL_TEXTURE_COUNT];
static unsigned int __attribute__((aligned(16))) font_texture[FONT_TEX_SIZE * FONT_TEX_SIZE];
static unsigned int *capture_buffer;

/* ---- On-device timing log (PERF.TXT) -------------------------------------
 * One line per second of normal play: frames shown, frames that missed a
 * vblank (gap > 20 ms), and average / worst microseconds for each part of
 * the frame. Written every 10 s of play, up to 60 s (each write may hitch once). */
#define PERF_SECONDS 60
typedef struct {
    unsigned int frames, missed, gap_max;
    unsigned int update_sum, update_max, skin_sum, skin_max;
    unsigned int draw_sum, draw_max, gpu_sum, gpu_max, chunks_sum;
} PerfSecond;
static PerfSecond perf_log[PERF_SECONDS];
static int perf_count;
static unsigned int perf_gpu_us;   /* sceGuSync wait in the last render_scene */
static unsigned int perf_draw_us;  /* display list build in the last render_scene */

static void perf_add(unsigned int gap, unsigned int update, unsigned int skin) {
    PerfSecond *p;
    if (perf_count >= PERF_SECONDS) return;
    p = &perf_log[perf_count];
    ++p->frames;
    if (gap > 20000u) ++p->missed;
    if (gap > p->gap_max) p->gap_max = gap;
    p->update_sum += update; if (update > p->update_max) p->update_max = update;
    p->skin_sum += skin; if (skin > p->skin_max) p->skin_max = skin;
    p->draw_sum += perf_draw_us; if (perf_draw_us > p->draw_max) p->draw_max = perf_draw_us;
    p->gpu_sum += perf_gpu_us; if (perf_gpu_us > p->gpu_max) p->gpu_max = perf_gpu_us;
    if (stage_loaded()) p->chunks_sum += (unsigned int)stage_last_chunks_drawn();
}
static Pose pose;
static AnimState anim;
static Combat combat;

/* Rebellion is drawn in Dante's right hand (data bone 9: the -X side, which
 * is anatomically his right because he faces +Z) for rb-* motions, and on
 * his back otherwise. */
#define WEAPON_HAND_BONE BONE_L_HAND
#define WEAPON_BACK_BONE BONE_CHEST

static volatile int running = 1;
static char game_dir[256] = "ms0:/PSP/GAME/DMC3TESTROOM/";

/* ---- HOME button exit --------------------------------------------------- */

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/* ---- Files -------------------------------------------------------------- */

static void path_in_game_dir(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s%s", game_dir, name);
}

static int game_file_exists(const char *name) {
    char path[320];
    SceUID fd;
    path_in_game_dir(path, sizeof(path), name);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    return 1;
}

static void save_bmp(const char *name, const unsigned int *pixels) {
    char path[320];
    unsigned char header[54];
    static unsigned char row[SCR_WIDTH * 3];
    unsigned int data_size = SCR_WIDTH * 3 * SCR_HEIGHT;
    unsigned int file_size = 54 + data_size;
    int w = SCR_WIDTH, h = SCR_HEIGHT;
    FILE *file;
    int y;

    path_in_game_dir(path, sizeof(path), name);
    file = fopen(path, "wb");
    if (!file) return;
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    memcpy(&header[2], &file_size, 4);
    header[10] = 54;
    header[14] = 40;
    memcpy(&header[18], &w, 4);
    memcpy(&header[22], &h, 4);
    header[26] = 1;
    header[28] = 24;
    memcpy(&header[34], &data_size, 4);
    fwrite(header, 1, sizeof(header), file);
    for (y = SCR_HEIGHT - 1; y >= 0; --y) {
        const unsigned int *source = pixels + y * BUF_WIDTH;
        int x;
        for (x = 0; x < SCR_WIDTH; ++x) {
            unsigned int pixel = source[x];
            row[x * 3 + 0] = (unsigned char)(pixel >> 16); /* B */
            row[x * 3 + 1] = (unsigned char)(pixel >> 8);  /* G */
            row[x * 3 + 2] = (unsigned char)pixel;         /* R */
        }
        fwrite(row, 1, sizeof(row), file);
    }
    fclose(file);
}

/* ---- Scene data --------------------------------------------------------- */

static int init_model(void) {
    int i;
    model_vertices = memalign(16, sizeof(ModelVertex) * DMC3_MODEL_VERTEX_COUNT);
    model_indices = memalign(16, sizeof(unsigned short) * DMC3_MODEL_INDEX_COUNT);
    if (!model_vertices || !model_indices) return 0;
    for (i = 0; i < DMC3_MODEL_VERTEX_COUNT; ++i) {
        model_vertices[i].u = dmc3_model_uvs[i].u;
        model_vertices[i].v = dmc3_model_uvs[i].v;
    }
    memcpy(model_indices, dmc3_model_indices, sizeof(unsigned short) * DMC3_MODEL_INDEX_COUNT);
    for (i = 0; i < DMC3_MODEL_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &dmc3_model_textures[i];
        if (!t8_load(&model_textures[i], t->rgba, t->width, t->height)) return 0;
    }
    return 1;
}

/* Linear blend skinning: v' = sum_k w_k * (R_k (v - rest_k) + P_k). The rest
 * pose has identity rotations, so the inverse bind matrix is a translation. */
/* Character textures are 8-bit: 1024 bytes of palette, then the pixels, swizzled once loaded. */
#define T8_PALETTE_BYTES 1024
static int t8_load(void **out, const unsigned char *source, int width, int height) {
    size_t bytes = T8_PALETTE_BYTES + (size_t)width * height;
    *out = memalign(16, bytes);
    if (!*out) return 0;
    memcpy(*out, source, bytes);
    if (!swizzle_texture8((unsigned char *)*out + T8_PALETTE_BYTES, width, height)) return 0;
    sceKernelDcacheWritebackRange(*out, bytes);
    return 1;
}
static void t8_bind(int width, int height, const void *texture) {
    sceGuClutLoad(256 / 8, texture);
    sceGuTexImage(0, width, height, width, (const unsigned char *)texture + T8_PALETTE_BYTES);
}

static void skin_mesh(const Pose *p, const Dmc3Vec3 *positions, const Dmc3Vec3 *normals, const Dmc3Vec3 *rest,
                      const unsigned char (*bones)[3], const unsigned char (*weights)[3], int count, ModelVertex *out) {
    int i, k;
    for (i = 0; i < count; ++i) {
        const Dmc3Vec3 *src = &positions[i];
        const Dmc3Vec3 *nrm = &normals[i];
        float px = 0.0f, py = 0.0f, pz = 0.0f, nx = 0.0f, ny = 0.0f, nz = 0.0f;
        ModelVertex *dst = &out[i];
        for (k = 0; k < 3; ++k) {
            int weight = weights[i][k];
            int bone;
            float w, dx, dy, dz;
            const float (*r)[3];
            if (!weight) continue;
            bone = bones[i][k];
            w = (float)weight * (1.0f / 31.0f);
            r = p->world_rot[bone].m;
            dx = src->x - rest[bone].x;
            dy = src->y - rest[bone].y;
            dz = src->z - rest[bone].z;
            px += w * (r[0][0] * dx + r[0][1] * dy + r[0][2] * dz + p->world_pos[bone][0]);
            py += w * (r[1][0] * dx + r[1][1] * dy + r[1][2] * dz + p->world_pos[bone][1]);
            pz += w * (r[2][0] * dx + r[2][1] * dy + r[2][2] * dz + p->world_pos[bone][2]);
            nx += w * (r[0][0] * nrm->x + r[0][1] * nrm->y + r[0][2] * nrm->z);
            ny += w * (r[1][0] * nrm->x + r[1][1] * nrm->y + r[1][2] * nrm->z);
            nz += w * (r[2][0] * nrm->x + r[2][1] * nrm->y + r[2][2] * nrm->z);
        }
        dst->x = px; dst->y = py; dst->z = pz;
        dst->nx = nx; dst->ny = ny; dst->nz = nz;
    }
    sceKernelDcacheWritebackRange(out, sizeof(ModelVertex) * count);
}

static void skin_model(const Pose *p) {
    skin_mesh(p, dmc3_model_positions, dmc3_model_normals, dmc3_bone_rest, dmc3_model_bones, dmc3_model_weights,
              DMC3_MODEL_VERTEX_COUNT, model_vertices);
}

/* Devil Trigger bodies, set up the first time each is used. */
typedef struct { ModelVertex *vertices; unsigned short *indices; void *textures[8]; } DevilRuntime;
static DevilRuntime devil_runtime[DMC3_DEVIL_COUNT];

static const DevilRuntime *devil_ready(int weapon) {
    const DevilModel *m = &dmc3_devils[weapon];
    DevilRuntime *r = &devil_runtime[weapon];
    int i;
    if (r->vertices) return r;
    r->vertices = memalign(16, sizeof(ModelVertex) * m->vertex_count);
    r->indices = memalign(16, sizeof(unsigned short) * m->index_count);
    if (!r->vertices || !r->indices) return NULL;
    for (i = 0; i < m->vertex_count; ++i) { r->vertices[i].u = m->uvs[i].u; r->vertices[i].v = m->uvs[i].v; }
    memcpy(r->indices, m->indices, sizeof(unsigned short) * m->index_count);
    for (i = 0; i < m->texture_count && i < 8; ++i) {
        if (!t8_load(&r->textures[i], m->textures[i].rgba, m->textures[i].width, m->textures[i].height)) return NULL;
    }
    return r;
}

/* Skins the rest pose and returns the largest deviation from the source
 * vertices in micro-units; proves the skeleton and weights are consistent. */
static unsigned int rest_pose_error_um(void) {
    float worst = 0.0f;
    int i;
    pose_identity(&pose);
    pose_evaluate(&pose);
    skin_model(&pose);
    for (i = 0; i < DMC3_MODEL_VERTEX_COUNT; ++i) {
        float dx = model_vertices[i].x - dmc3_model_positions[i].x;
        float dy = model_vertices[i].y - dmc3_model_positions[i].y;
        float dz = model_vertices[i].z - dmc3_model_positions[i].z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > worst) worst = d;
    }
    return (unsigned int)(worst * 1000000.0f);
}

/* The guns: each drawn in a hand while it's in use. */
typedef struct {
    const Dmc3Vec3 *positions, *normals;
    const Dmc3Uv *uvs;
    const unsigned short *source_indices;
    const unsigned char *rgba;
    int vertex_count, index_count, width, height;
    ModelVertex *vertices;
    unsigned short *indices;
    void *texture;
    const char *motion_prefix;    /* its motions, or NULL */
    const WeaponGroup *groups;
    int group_count, bone_count;
    const short *parents;
    const float (*rest)[3];
    /* What's playing: chosen by the placement record's mode. */
    const WeaponPlace *place;
    int motion;
    float frame;
} GunModel;

#define GUN_MODEL(n, N, prefix) {dmc3_##n##_positions, dmc3_##n##_normals, dmc3_##n##_uvs, dmc3_##n##_indices, dmc3_##n##_texture, \
                          DMC3_##N##_VERTEX_COUNT, DMC3_##N##_INDEX_COUNT, DMC3_##N##_TEXTURE_WIDTH, DMC3_##N##_TEXTURE_HEIGHT, 0, 0, 0, \
                          prefix, dmc3_##n##_groups, DMC3_##N##_GROUP_COUNT, DMC3_##N##_BONE_COUNT, dmc3_##n##_parents, dmc3_##n##_rest, 0, -1, 0.0f}
enum { GUN_REBELLION, GUN_CERBERUS, GUN_AGNI, GUN_NEVAN, GUN_BEOWULF,
       GUN_EBONY, GUN_SHOTGUN, GUN_ARTEMIS, GUN_SPIRAL, GUN_KALINA, GUN_IVORY, GUN_MODEL_COUNT };
static GunModel gun_models[GUN_MODEL_COUNT] = {
    GUN_MODEL(rebellion, REBELLION, "w0-"), GUN_MODEL(cerberus, CERBERUS, "w1-"), GUN_MODEL(agni, AGNI, "w2-"),
    GUN_MODEL(nevan, NEVAN, "w3-"), GUN_MODEL(beowulf, BEOWULF, NULL),
    GUN_MODEL(ebony, EBONY, NULL), GUN_MODEL(shotgun, SHOTGUN, "w6-"), GUN_MODEL(artemis, ARTEMIS, "w7-"),
    GUN_MODEL(spiral, SPIRAL, NULL), GUN_MODEL(kalina, KALINA, NULL), GUN_MODEL(ivory, IVORY, NULL),
};

static int init_guns(void) {
    int g, i;
    for (g = 0; g < GUN_MODEL_COUNT; ++g) {
        GunModel *m = &gun_models[g];
        m->vertices = memalign(16, sizeof(ModelVertex) * m->vertex_count);
        m->indices = memalign(16, sizeof(unsigned short) * m->index_count);
        if (!m->vertices || !m->indices) return 0;
        for (i = 0; i < m->vertex_count; ++i) {
            ModelVertex *v = &m->vertices[i];
            v->u = m->uvs[i].u; v->v = m->uvs[i].v;
            v->nx = m->normals[i].x; v->ny = m->normals[i].y; v->nz = m->normals[i].z;
            v->x = m->positions[i].x; v->y = m->positions[i].y; v->z = m->positions[i].z;
        }
        memcpy(m->indices, m->source_indices, sizeof(unsigned short) * m->index_count);
        if (!t8_load(&m->texture, m->rgba, m->width, m->height)) return 0;
    }
    return 1;
}

/* GU matrix from a rotation and translation (v' = R v + t). pspgum is
 * column-major: x/y/z hold the images of the basis axes (R's columns), as in
 * sceGumRotateY, and w holds the translation. */
static void to_gu_matrix(ScePspFMatrix4 *out, const float r[3][3], const float t[3]) {
    out->x.x = r[0][0]; out->x.y = r[1][0]; out->x.z = r[2][0]; out->x.w = 0.0f;
    out->y.x = r[0][1]; out->y.y = r[1][1]; out->y.z = r[2][1]; out->y.w = 0.0f;
    out->z.x = r[0][2]; out->z.y = r[1][2]; out->z.z = r[2][2]; out->z.w = 0.0f;
    out->w.x = t[0]; out->w.y = t[1]; out->w.z = t[2]; out->w.w = 1.0f;
}

static void init_room(void) {
    int i, j, n = 0;
    float start = -ROOM_HALF_SIZE;
    for (i = 0; i < ROOM_TILES; ++i) {
        for (j = 0; j < ROOM_TILES; ++j) {
            float x0 = start + i * ROOM_TILE, x1 = x0 + ROOM_TILE;
            float z0 = start + j * ROOM_TILE, z1 = z0 + ROOM_TILE;
            ColorVertex quad[6] = {
                {FLOOR_COLOR, x0, 0.0f, z0}, {FLOOR_COLOR, x1, 0.0f, z0}, {FLOOR_COLOR, x1, 0.0f, z1},
                {FLOOR_COLOR, x0, 0.0f, z0}, {FLOOR_COLOR, x1, 0.0f, z1}, {FLOOR_COLOR, x0, 0.0f, z1},
            };
            memcpy(&floor_vertices[n], quad, sizeof(quad));
            n += 6;
        }
    }
    /* Grid lines are split per tile so no single primitive spans the room;
     * the GE drops primitives that leave its guard band. */
    n = 0;
    for (i = 0; i <= ROOM_TILES; ++i) {
        float c = start + i * ROOM_TILE;
        for (j = 0; j < ROOM_TILES; ++j) {
            float a = start + j * ROOM_TILE, b = a + ROOM_TILE;
            grid_vertices[n++] = (ColorVertex){GRID_COLOR, c, 0.0f, a};
            grid_vertices[n++] = (ColorVertex){GRID_COLOR, c, 0.0f, b};
            grid_vertices[n++] = (ColorVertex){GRID_COLOR, a, 0.0f, c};
            grid_vertices[n++] = (ColorVertex){GRID_COLOR, b, 0.0f, c};
        }
    }
}

static void init_font(void) {
    int c, row, col;
    memset(font_texture, 0, sizeof(font_texture));
    for (c = 0; c < 256; ++c) {
        int ox = (c % 16) * 8, oy = (c / 16) * 8;
        for (row = 0; row < 8; ++row) {
            u8 bits = msx[c * 8 + row];
            for (col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    font_texture[(oy + row) * FONT_TEX_SIZE + ox + col] = 0xffffffffu;
                }
            }
        }
    }
}

/* ---- Rendering ---------------------------------------------------------- */

static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void *)FRAMEBUFFER_BYTES, BUF_WIDTH);
    sceGuDepthBuffer((void *)(FRAMEBUFFER_BYTES * 2), BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_CULL_FACE); /* MOD winding is not confirmed yet */
    sceGuEnable(GU_CLIP_PLANES);
    sceGuAlphaFunc(GU_GREATER, 0x7f, 0xff);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    {
        /* Key light plus a weaker opposite fill: some MOD normals face inward
         * (the software renderer lit with |n.z|), so one-sided lighting alone
         * leaves those faces black. */
        ScePspFVector3 key_dir = {0.45f, 0.8f, 0.4f};
        ScePspFVector3 fill_dir = {-0.45f, -0.3f, -0.4f};
        sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &key_dir);
        sceGuLightColor(0, GU_DIFFUSE, 0xffa0a0a0);
        sceGuLight(1, GU_DIRECTIONAL, GU_DIFFUSE, &fill_dir);
        sceGuLightColor(1, GU_DIFFUSE, 0xff505050);
        sceGuAmbient(0xff909090);
        sceGuModelColor(0, 0xffffffff, 0xffffffff, 0);
        sceGuEnable(GU_LIGHT0);
        sceGuEnable(GU_LIGHT1);
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* Call inside a display list with the font texture bound. */
static void draw_text(int x, int y, const char *text) {
    int count = (int)strlen(text);
    TextVertex *v;
    int i;
    if (count <= 0) return;
    v = sceGuGetMemory(sizeof(TextVertex) * 2 * count);
    for (i = 0; i < count; ++i) {
        unsigned char c = (unsigned char)text[i];
        float u = (float)((c % 16) * 8), t = (float)((c / 16) * 8);
        float px = (float)(x + i * 8);
        v[i * 2 + 0] = (TextVertex){u, t, px, (float)y, 0.0f};
        v[i * 2 + 1] = (TextVertex){u + 8.0f, t + 8.0f, px + 8.0f, (float)y + 8.0f, 0.0f};
    }
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count * 2, 0, v);
}

/* The menus' text (menu.h's MenuText): this font, in 2D, then what the menus expect back. */
static void menu_text_hook(int x, int y, const char *text, unsigned int color) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    sceGuColor(0xc0000000u);
    draw_text(x + 1, y + 1, text);
    sceGuColor(color);
    draw_text(x, y, text);
    sceGuColor(0xffffffffu);
}

/* START: the debug menu. The game stops while it's open. Turbo is the real game's mode:
 * everything runs 1.2x as fast. It's the default in play; test scenes run at normal speed. */
#define TURBO_SPEED 1.2f
enum { DEBUG_SPEED, DEBUG_DUMMIES, DEBUG_KIND, DEBUG_ACTION, DEBUG_ANIM, DEBUG_ORBS, DEBUG_QUIT, DEBUG_VIEWER, DEBUG_RESUME, DEBUG_ITEMS };
static int dummy_anim_auto = 1;
static void mission_gimmicks_draw(void);
static ShopState shop_state;   /* the Red Orbs and what's been bought, for this session */
/* Each kind's max health, from the class parameter blocks (docs/ENEMY_SPECS.md, docs/SHOP.md): what its red
 * orbs are worked out from. Lust, Wrath and Greed are inferred. */
static int kind_max_health(int kind) {
    switch (kind) {
    case ENEMY_PRIDE: return 250;
    case ENEMY_GLUTTONY: return 700;
    case ENEMY_LUST: return 750;
    case ENEMY_SLOTH: return 1200;
    case ENEMY_WRATH: return 500;
    case ENEMY_GREED: return 1000;
    case ENEMY_ENIGMA: return 600;
    case ENEMY_GIGAPEDE: return 2800;
    default: return 250;
    }
}
static int mission_difficulty(void);
static void enemy_died(int kind, float x, float y, float z) {
    orbs_burst(x, y, z, orbs_for_enemy(kind_max_health(kind), mission_difficulty()));
}
static int debug_open, debug_cursor, game_turbo = 1;
static int pause_active, mission_mode;
static int dummy_count = 1, dummy_attacks, dummy_kind;
static const char *const kind_names[] = {"PRIDE", "GLUTTONY", "LUST", "SLOTH", "WRATH", "GREED", "ENIGMA"};
static unsigned int debug_pressed;   /* buttons pressed this frame */
static int debug_quit;               /* the debug menu's Quit to menu was chosen */
static unsigned int debug_held;      /* buttons down this frame */
#define DEBUG_COMBO (PSP_CTRL_START | PSP_CTRL_SELECT)

/* Returns 1 when the animation viewer should toggle. */
static int debug_menu_update(unsigned int pressed) {
    int toggle_viewer = 0;
    if (!debug_open) {
        if (pressed & PSP_CTRL_START) { debug_open = 1; debug_cursor = 0; }
        return 0;
    }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) debug_open = 0;
    if (pressed & PSP_CTRL_UP) debug_cursor = (debug_cursor + DEBUG_ITEMS - 1) % DEBUG_ITEMS;
    if (pressed & PSP_CTRL_DOWN) debug_cursor = (debug_cursor + 1) % DEBUG_ITEMS;
    if (debug_cursor == DEBUG_SPEED && (pressed & (PSP_CTRL_CROSS | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) game_turbo = !game_turbo;
    if (debug_cursor == DEBUG_DUMMIES) {
        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_RIGHT)) dummy_count = (dummy_count + 1) % (ENEMY_MAX + 1);
        if (pressed & PSP_CTRL_LEFT) dummy_count = (dummy_count + ENEMY_MAX) % (ENEMY_MAX + 1);
    }
    if (debug_cursor == DEBUG_KIND && (pressed & (PSP_CTRL_CROSS | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
        /* A new kind: the dummies there are go, and come back as that kind. */
        dummy_kind = (dummy_kind + ((pressed & PSP_CTRL_LEFT) ? ENEMY_KIND_TOTAL - 1 : 1)) % ENEMY_KIND_TOTAL;
        enemy_set_dummy_kind(dummy_kind);
        if (!mission_mode) { enemy_set_count(0, 0.0f, 0.0f); }
    }
    if (debug_cursor == DEBUG_ACTION && (pressed & (PSP_CTRL_CROSS | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) dummy_attacks = !dummy_attacks;
    if (debug_cursor == DEBUG_ORBS && (pressed & (PSP_CTRL_CROSS | PSP_CTRL_RIGHT))) shop_state.red_orbs += 10000;   /* for testing the shop */
    if (debug_cursor == DEBUG_ORBS && (pressed & PSP_CTRL_LEFT)) shop_state.red_orbs = 0;
    if (debug_cursor == DEBUG_ANIM && (pressed & (PSP_CTRL_CROSS | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) dummy_anim_auto = !dummy_anim_auto;
    if (debug_cursor == DEBUG_VIEWER && (pressed & PSP_CTRL_CROSS)) toggle_viewer = 1;
    if (debug_cursor == DEBUG_RESUME && (pressed & PSP_CTRL_CROSS)) debug_open = 0;
    if (debug_cursor == DEBUG_QUIT && (pressed & PSP_CTRL_CROSS)) { debug_open = 0; debug_quit = 1; }
    return toggle_viewer;
}

#define HUD_LINES 4
static char fps_line[12];   /* top right, always on */
static void draw_hud(const char *lines[HUD_LINES]) {
    static const int ys[HUD_LINES] = {8, 18, SCR_HEIGHT - 26, SCR_HEIGHT - 16};
    int pass, i;
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    /* Shadow, then text. sceGuColor is the unlit material color. */
    for (pass = 0; pass < 2; ++pass) {
        sceGuColor(pass ? 0xffffffff : 0xff000000);
        for (i = 0; i < HUD_LINES; ++i) {
            if (lines[i]) draw_text(8 + (pass ? 0 : 1), ys[i] + (pass ? 0 : 1), lines[i]);
        }
        draw_text(SCR_WIDTH - 6 - (int)strlen(fps_line) * 8 + (pass ? 0 : 1), 6 + (pass ? 0 : 1), fps_line);
    }
}

/* ---- Weapon wheel ---------------------------------------------------------
 * Hold L: the outer ring is the melee weapons (the stick picks one), the inner
 * ring the guns (square cycles them). Sector 0 is at the top, going clockwise. */
#define WHEEL_SLOTS 5
#define WHEEL_ARC_STEPS 8
#define WHEEL_CX 240.0f
#define WHEEL_CY 132.0f
#define WHEEL_OUTER 74.0f
#define WHEEL_OUTER_IN 44.0f
#define WHEEL_INNER 38.0f
#define WHEEL_INNER_IN 20.0f
#define WHEEL_LABEL_RADIUS 84.0f
#define WHEEL_SLOT_COLOR 0xb0302828u   /* ABGR */
#define WHEEL_MELEE_COLOR 0xf02828d0u  /* red */
#define WHEEL_GUN_COLOR 0xf0d08828u    /* blue */

typedef struct { unsigned int color; float x, y, z; } WheelVertex;

static int wheel_add_wedge(WheelVertex *v, float slot, float r0, float r1, unsigned int color) {
    const float span = 6.2831853f / WHEEL_SLOTS, gap = 0.05f;
    float a0 = (slot - 0.5f) * span + gap, a1 = (slot + 0.5f) * span - gap;
    int i, n = 0;
    for (i = 0; i < WHEEL_ARC_STEPS; ++i) {
        float t0 = a0 + (a1 - a0) * (float)i / WHEEL_ARC_STEPS, t1 = a0 + (a1 - a0) * (float)(i + 1) / WHEEL_ARC_STEPS;
        float sa = sinf(t0), ca = cosf(t0), sb = sinf(t1), cb = cosf(t1);
        WheelVertex q[4] = {
            {color, WHEEL_CX + sa * r0, WHEEL_CY - ca * r0, 0.0f}, {color, WHEEL_CX + sa * r1, WHEEL_CY - ca * r1, 0.0f},
            {color, WHEEL_CX + sb * r1, WHEEL_CY - cb * r1, 0.0f}, {color, WHEEL_CX + sb * r0, WHEEL_CY - cb * r0, 0.0f},
        };
        v[n++] = q[0]; v[n++] = q[1]; v[n++] = q[2];
        v[n++] = q[0]; v[n++] = q[2]; v[n++] = q[3];
    }
    return n;
}

/* Call after draw_hud, inside the display list. */
static void draw_weapon_wheel(const Player *player) {
    WheelVertex *v = sceGuGetMemory(sizeof(WheelVertex) * WHEEL_SLOTS * 2 * WHEEL_ARC_STEPS * 6);
    int i, n = 0, pass;
    for (i = 0; i < WHEEL_SLOTS; ++i) {
        n += wheel_add_wedge(v + n, (float)i, WHEEL_OUTER_IN, WHEEL_OUTER,
                             i == player->melee_weapon ? WHEEL_MELEE_COLOR : WHEEL_SLOT_COLOR);
        n += wheel_add_wedge(v + n, (float)i, WHEEL_INNER_IN, WHEEL_INNER,
                             i == player->gun_weapon - 5 ? WHEEL_GUN_COLOR : WHEEL_SLOT_COLOR);
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_FLAT);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, v);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_BLEND);
    /* Labels: names around the ring, the current gun below it. */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    for (pass = 0; pass < 2; ++pass) {
        int shadow = pass ? 0 : 1;
        for (i = 0; i < WHEEL_SLOTS; ++i) {
            const char *name = player_weapon_name(i);
            float a = (float)i * 6.2831853f / WHEEL_SLOTS, s = sinf(a), c = cosf(a);
            float w = (float)strlen(name) * 8.0f;
            /* Grow outward from the ring: left of it on the left, right on the right. */
            float x = WHEEL_CX + s * WHEEL_LABEL_RADIUS + (s > 0.3f ? 0.0f : s < -0.3f ? -w : -w * 0.5f);
            float y = WHEEL_CY - c * WHEEL_LABEL_RADIUS - 4.0f - (c > 0.8f ? 6.0f : 0.0f);
            sceGuColor(pass ? (i == player->melee_weapon ? 0xff60a0ffu : 0xffffffffu) : 0xff000000u);
            draw_text((int)x + shadow, (int)y + shadow, name);
        }
        {
            const char *gun = player_weapon_name(player->gun_weapon);
            sceGuColor(pass ? 0xffffc080u : 0xff000000u);
            draw_text((int)(WHEEL_CX - (float)strlen(gun) * 4.0f) + shadow, (int)(WHEEL_CY + WHEEL_LABEL_RADIUS + 14.0f) + shadow, gun);
            sceGuColor(pass ? 0xffc0c0c0u : 0xff000000u);
            draw_text((int)WHEEL_CX - 76 + shadow, (int)(WHEEL_CY + WHEEL_LABEL_RADIUS + 26.0f) + shadow, "stick melee  [] gun");
        }
    }
}

/* While the wheel is open the stick picks a melee weapon and nothing else acts. */
static void apply_weapon_wheel(PlayerInput *input, Player *player) {
    if (!input->weapon_wheel) return;
    if (input->stick_x * input->stick_x + input->stick_y * input->stick_y > 0.25f) {
        float span = 6.2831853f / WHEEL_SLOTS;
        int slot = (int)floorf(atan2f(input->stick_x, input->stick_y) / span + 0.5f);
        player->melee_weapon = (slot + WHEEL_SLOTS) % WHEEL_SLOTS;
    }
    input->stick_x = input->stick_y = 0.0f;
    input->melee = input->melee_held = input->style_action = input->style_held = 0;
    input->gun = input->gun_held = input->jump = input->lock_on = 0;
    input->style = -1;
}

/* Skeleton overlay: a line from each bone to its parent, drawn over Dante. */
static void draw_bones(void) {
    ColorVertex *v = sceGuGetMemory(sizeof(ColorVertex) * 2 * (DMC3_BONE_COUNT + coat_joint_count()));
    int n = 0, i;
    for (i = 0; i < DMC3_BONE_COUNT; ++i) {
        int parent = dmc3_bone_parent[i];
        if (parent < 0) continue;
        v[n++] = (ColorVertex){BONE_COLOR, pose.world_pos[parent][0], pose.world_pos[parent][1], pose.world_pos[parent][2]};
        v[n++] = (ColorVertex){BONE_COLOR, pose.world_pos[i][0], pose.world_pos[i][1], pose.world_pos[i][2]};
    }
    for (i = 0; i < coat_joint_count(); ++i) {
        float a[3], b[3];
        int parent, unused;
        coat_joint(i, b, &parent);
        if (parent < 0) continue;
        coat_joint(parent, a, &unused);
        v[n++] = (ColorVertex){COAT_BONE_COLOR, a[0], a[1], a[2]};
        v[n++] = (ColorVertex){COAT_BONE_COLOR, b[0], b[1], b[2]};
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGumDrawArray(GU_LINES, COLOR_VTYPE, n, 0, v);
}

static void to_gu_matrix(ScePspFMatrix4 *out, const float r[3][3], const float t[3]);

static void place_rotation(const float e[3], float r[3][3]);
/* Call with Dante's model matrix current. */
static void draw_weapon_at(const GunModel *m, const float pr[3][3], const float pt[3], const float pos[3], const float rot[3]) {
    ScePspFMatrix4 matrix;
    float local[3][3], r[3][3], t[3];
    int a, b;
    place_rotation(rot, local);
    for (a = 0; a < 3; ++a) {
        for (b = 0; b < 3; ++b) r[a][b] = pr[a][0] * local[0][b] + pr[a][1] * local[1][b] + pr[a][2] * local[2][b];
        t[a] = pt[a] + pr[a][0] * pos[0] + pr[a][1] * pos[1] + pr[a][2] * pos[2];
    }
    to_gu_matrix(&matrix, r, t);
    sceGumPushMatrix();
    sceGumMultMatrix(&matrix);
    t8_bind(m->width, m->height, m->texture);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, m->index_count, m->indices, m->vertices);
    sceGumPopMatrix();
}

/* Starts the motion a new placement asks for, and runs it. */
static void weapon_motion_update(GunModel *m, const WeaponPlace *place, const Combat *c, float dt) {
    if (place != m->place) {
        m->place = place;
        m->motion = -1;
        if (m->motion_prefix && place->mode != 0 && place->mode != 255) {
            char name[16];
            snprintf(name, sizeof(name), "%s%02d", m->motion_prefix, place->mode);
            m->motion = motion_find(name);
            if (m->motion >= 0) m->frame = motion_get(m->motion)->loop_start;
        }
        return;
    }
    if (m->motion >= 0) {
        /* Authored against Dante's motion: on his bone 0, on his timeline. */
        if (c->move != MOVE_NONE && place->bone == 0) m->frame = motion_wrap(motion_get(m->motion), c->frame);
        else m->frame = motion_wrap(motion_get(m->motion), m->frame + dt * MOTION_FPS);
    }
}

/* The weapon's bones in its own space under the motion, each group drawn on its bone. */
static void draw_weapon_animated(const GunModel *m, const float base_r[3][3], const float base_t[3]) {
    static MotionSample sample;
    static float world_r[MOTION_MAX_BONES][3][3], world_t[MOTION_MAX_BONES][3];
    int bone, g, a, b, k;
    motion_sample(motion_get(m->motion), m->frame, &sample);
    for (bone = 0; bone < m->bone_count && bone < MOTION_MAX_BONES; ++bone) {
        int parent = m->parents[bone];
        float e[3], local[3][3], offset[3];
        /* A weapon's grip bone carries all six channels the other way round from Dante's:
         * 3-5 hold its position in his model space (cm), 6-8 its rotation. Bones with only
         * 3-5 (a pump, the sticks) rotate as usual. */
        int swapped = sample.has_rot[bone][0] && sample.has_rot[bone][1] && sample.has_rot[bone][2] &&
                      sample.has_trans[bone][0] && sample.has_trans[bone][1] && sample.has_trans[bone][2];
        for (k = 0; k < 3; ++k) {
            offset[k] = m->rest[bone][k] - (parent >= 0 ? m->rest[parent][k] : 0.0f);
            if (swapped) {
                e[k] = sample.trans[bone][k];
                offset[k] = sample.rot[bone][k];
            } else {
                e[k] = sample.has_rot[bone][k] ? sample.rot[bone][k] : 0.0f;
                /* The root's travel is Dante's own root motion, already in his position. */
                if (sample.has_trans[bone][k] && parent >= 0) offset[k] = sample.trans[bone][k];
            }
        }
        place_rotation(e, local);
        if (parent < 0) {
            memcpy(world_r[bone], local, sizeof(local));
            memcpy(world_t[bone], offset, sizeof(offset));
        } else {
            for (a = 0; a < 3; ++a) {
                for (b = 0; b < 3; ++b)
                    world_r[bone][a][b] = world_r[parent][a][0] * local[0][b] + world_r[parent][a][1] * local[1][b] + world_r[parent][a][2] * local[2][b];
                world_t[bone][a] = world_t[parent][a] + world_r[parent][a][0] * offset[0] + world_r[parent][a][1] * offset[1] + world_r[parent][a][2] * offset[2];
            }
        }
    }
    t8_bind(m->width, m->height, m->texture);
    sceGuTexFlush();
    for (g = 0; g < m->group_count; ++g) {
        const WeaponGroup *group = &m->groups[g];
        ScePspFMatrix4 matrix;
        float r[3][3], t[3], local_t[3];
        bone = group->bone;
        /* v' = base (W (v - rest) + P) */
        for (a = 0; a < 3; ++a)
            local_t[a] = world_t[bone][a] - (world_r[bone][a][0] * m->rest[bone][0] + world_r[bone][a][1] * m->rest[bone][1] + world_r[bone][a][2] * m->rest[bone][2]);
        for (a = 0; a < 3; ++a) {
            for (b = 0; b < 3; ++b)
                r[a][b] = base_r[a][0] * world_r[bone][0][b] + base_r[a][1] * world_r[bone][1][b] + base_r[a][2] * world_r[bone][2][b];
            t[a] = base_t[a] + base_r[a][0] * local_t[0] + base_r[a][1] * local_t[1] + base_r[a][2] * local_t[2];
        }
        to_gu_matrix(&matrix, r, t);
        sceGumPushMatrix();
        sceGumMultMatrix(&matrix);
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)group->count, m->indices + group->start, m->vertices);
        sceGumPopMatrix();
    }
}

/* The bone a record hangs from, in Dante's model space. */
static void place_parent(const GunModel *m, const WeaponPlace *place, float pr[3][3], float pt[3]) {
    int a, b, bone = place->bone;
    if (place->owner) {
        coat_bone_matrix(bone, pr, pt);   /* the holsters hang off the coat */
    } else {
        /* No motion to drive it: rest pose in the hand. */
        if (bone >= DMC3_BONE_COUNT || (place->mode && bone == 0 && m->motion < 0)) bone = BONE_L_HAND;
        for (a = 0; a < 3; ++a) {
            for (b = 0; b < 3; ++b) pr[a][b] = pose.world_rot[bone].m[a][b];
            pt[a] = pose.world_pos[bone][a];
        }
    }
}

/* Agni & Rudra: each sword is a bone of one model, with its own record. */
static void draw_two_swords(const GunModel *m, const WeaponPlace *first, const WeaponPlace *second) {
    int g, a, b;
    t8_bind(m->width, m->height, m->texture);
    sceGuTexFlush();
    for (g = 0; g < m->group_count; ++g) {
        const WeaponGroup *group = &m->groups[g];
        const WeaponPlace *place = group->bone == 2 ? second : first;
        const float *rest = m->rest[group->bone];
        ScePspFMatrix4 matrix;
        float pr[3][3], pt[3], local[3][3], r[3][3], t[3];
        if (place->mode == 255) continue;
        place_parent(m, place, pr, pt);
        place_rotation(place->rot, local);
        for (a = 0; a < 3; ++a) {
            for (b = 0; b < 3; ++b) r[a][b] = pr[a][0] * local[0][b] + pr[a][1] * local[1][b] + pr[a][2] * local[2][b];
            t[a] = pt[a] + pr[a][0] * place->pos[0] + pr[a][1] * place->pos[1] + pr[a][2] * place->pos[2];
        }
        for (a = 0; a < 3; ++a) t[a] -= r[a][0] * rest[0] + r[a][1] * rest[1] + r[a][2] * rest[2];
        to_gu_matrix(&matrix, r, t);
        sceGumPushMatrix();
        sceGumMultMatrix(&matrix);
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)group->count, m->indices + group->start, m->vertices);
        sceGumPopMatrix();
    }
}

static void draw_placed(const GunModel *m, const WeaponPlace *place) {
    float pr[3][3], pt[3];
    int a, b;
    if (place->mode == 255) return;
    place_parent(m, place, pr, pt);
    if (m->motion >= 0) {
        float local[3][3], r[3][3], t[3];
        place_rotation(place->rot, local);
        for (a = 0; a < 3; ++a) {
            for (b = 0; b < 3; ++b) r[a][b] = pr[a][0] * local[0][b] + pr[a][1] * local[1][b] + pr[a][2] * local[2][b];
            t[a] = pt[a] + pr[a][0] * place->pos[0] + pr[a][1] * place->pos[1] + pr[a][2] * place->pos[2];
        }
        draw_weapon_animated(m, r, t);
        return;
    }
    draw_weapon_at(m, pr, pt, place->pos, place->rot);
}

/* Beowulf is worn: its rest pose is Dante's, so each group follows its limb. */
static void draw_beowulf(void) {
    const GunModel *m = &gun_models[GUN_BEOWULF];
    int g;
    t8_bind(m->width, m->height, m->texture);
    sceGuTexFlush();
    for (g = 0; g < DMC3_BEOWULF_GROUP_COUNT; ++g) {
        const WeaponGroup *group = &dmc3_beowulf_limb_groups[g];
        const Dmc3Vec3 *rest = &dmc3_bone_rest[group->bone];
        const Mat3 *rot = &pose.world_rot[group->bone];
        ScePspFMatrix4 matrix;
        float t[3];
        int k;
        for (k = 0; k < 3; ++k)
            t[k] = pose.world_pos[group->bone][k] - (rot->m[k][0] * rest->x + rot->m[k][1] * rest->y + rot->m[k][2] * rest->z);
        to_gu_matrix(&matrix, rot->m, t);
        sceGumPushMatrix();
        sceGumMultMatrix(&matrix);
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)group->count, dmc3_beowulf_limb_indices + group->start, m->vertices);
        sceGumPopMatrix();
    }
}

/* The equipped melee weapon and gun, each where its placement state says. */
static void draw_weapons(const Player *player) {
    int melee = player->melee_weapon, gun = player->gun_weapon;
    if (melee == 4) draw_beowulf();
    else if (melee == 2 && gun_models[2].motion < 0)
        draw_two_swords(&gun_models[2], combat_weapon_place(&combat, player, 2, 0), combat_weapon_place(&combat, player, 2, 1));
    else draw_placed(&gun_models[melee], combat_weapon_place(&combat, player, melee, 0));
    draw_placed(&gun_models[gun], combat_weapon_place(&combat, player, gun, 0));
    if (gun == 5) draw_placed(&gun_models[GUN_IVORY], combat_weapon_place(&combat, player, gun, 1));
}

static unsigned int perf_render_start;
static unsigned int perf_second_us;
static void path_in_game_dir(char *out, size_t size, const char *name);
static void perf_write(void) {
    char path[320];
    FILE *f;
    int i;
    path_in_game_dir(path, sizeof(path), "PERF.TXT");
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "sec frames missed gap_max | update avg/max | skin avg/max | draw avg/max | gpu avg/max (us) | chunks\n");
    for (i = 0; i < perf_count; ++i) {
        const PerfSecond *p = &perf_log[i];
        unsigned int n = p->frames ? p->frames : 1;
        fprintf(f, "%3d %3u %3u %6u | %5u %6u | %5u %6u | %5u %6u | %5u %6u | %4u\n", i + 1, p->frames, p->missed,
                p->gap_max, p->update_sum / n, p->update_max, p->skin_sum / n, p->skin_max,
                p->draw_sum / n, p->draw_max, p->gpu_sum / n, p->gpu_max, p->chunks_sum / n);
    }
    fclose(f);
}
/* CUIDLockOnCursor (FUN_0035af50, FUN_0035b610): at the target's screen position
 * while R is held and there is one. Projected as sceGumLookAt + sceGumPerspective(60). */
#define RETICLE_AIM_HEIGHT 100.0f   /* the target's lock-on point isn't decoded for the dummy */
static void draw_lock_on_reticle(const Player *p, const FollowCamera *c) {
    float t[3] = {p->target_x, p->target_y + RETICLE_AIM_HEIGHT, p->target_z};
    float f[3] = {c->tx - c->x, c->ty - c->y, c->tz - c->z};
    float d[3] = {t[0] - c->x, t[1] - c->y, t[2] - c->z};
    float r[3], u[3], len, depth, dist, focal;
    if (!p->locked_on || !p->has_target) return;
    len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 0.001f) return;
    f[0] /= len; f[1] /= len; f[2] /= len;
    /* right = forward x up, up = right x forward (up is +Y). */
    r[0] = -f[2]; r[1] = 0.0f; r[2] = f[0];
    len = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (len < 0.001f) return;
    r[0] /= len; r[2] /= len;
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
    depth = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (depth < 1.0f) return;
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    focal = (SCR_HEIGHT * 0.5f) / tanf(30.0f * 3.14159265f / 180.0f);
    hud_draw_reticle(SCR_WIDTH * 0.5f + (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / depth * focal,
                     SCR_HEIGHT * 0.5f - (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / depth * focal,
                     hud_reticle_scale(dist));
}

#define DOPPEL_FRAMES 16
static float doppel_trail[DOPPEL_FRAMES][4];
static int doppel_head;

static int bench_no_stage, bench_no_texture, bench_no_lighting, bench_ring = 320;
static float door_prompt_time;
/* Standing at a door that wants a button. */
static void door_prompt_draw(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    if (menu_loaded()) {
        /* The menus' font. */
        menu_font_draw(SCR_WIDTH / 2 - menu_font_width(20.0f, "O  ENTER") * 0.5f, SCR_HEIGHT - 46, 20.0f, "O  ENTER", 0xffffffffu);
        sceGuDisable(GU_BLEND);
        return;
    }
    sceGuColor(0xff000000u);
    draw_text(SCR_WIDTH / 2 - 27, SCR_HEIGHT - 39, "O  ENTER");
    sceGuColor(0xffffffffu);
    draw_text(SCR_WIDTH / 2 - 28, SCR_HEIGHT - 40, "O  ENTER");
}

static void draw_debug_menu(int viewer);
/* The debug menu on its own frames, for the menus (menu.h's MenuDebug): until it's closed. */
static void debug_modal(void) {
    unsigned int previous = 0xffffffffu;
    debug_open = 1;
    debug_cursor = 0;
    while (debug_open) {
        SceCtrlData pad;
        unsigned int pressed;
        sceCtrlPeekBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~previous;
        previous = pad.Buttons;
        debug_menu_update(pressed);
        if (debug_quit) { debug_quit = 0; debug_open = 0; }   /* already in the menus */
        sceGuStart(GU_DIRECT, display_list);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        draw_debug_menu(0);
        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        menu_draw_buffer = sceGuSwapBuffers();
    }
}

static void draw_debug_menu(int viewer) {
    char lines[DEBUG_ITEMS][32];
    int i, x = SCR_WIDTH / 2 - 88, y = SCR_HEIGHT / 2 - 46;
    snprintf(lines[DEBUG_SPEED], sizeof(lines[0]), "Game speed     %s", game_turbo ? "TURBO" : "NORMAL");
    snprintf(lines[DEBUG_DUMMIES], sizeof(lines[0]), "Dummies        %d", dummy_count);
    snprintf(lines[DEBUG_KIND], sizeof(lines[0]), "Dummy kind     %s", kind_names[dummy_kind % ENEMY_KIND_TOTAL]);
    snprintf(lines[DEBUG_ORBS], sizeof(lines[0]), "Red orbs +10k  %d", shop_state.red_orbs);
    snprintf(lines[DEBUG_QUIT], sizeof(lines[0]), "Quit to menu");
    snprintf(lines[DEBUG_ACTION], sizeof(lines[0]), "Dummy action   %s", dummy_attacks ? "ATTACK" : "STAND");
    if (dummy_anim_auto) snprintf(lines[DEBUG_ANIM], sizeof(lines[0]), "Dummy anim     AUTO %d", enemy_anim_rate());
    else snprintf(lines[DEBUG_ANIM], sizeof(lines[0]), "Dummy anim     FULL");
    snprintf(lines[DEBUG_VIEWER], sizeof(lines[0]), "Anim viewer    %s", viewer ? "ON" : "OFF");
    snprintf(lines[DEBUG_RESUME], sizeof(lines[0]), "Resume");
    fx_screen_tint(0xa0000000u);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    sceGuColor(0xffffffffu);
    draw_text(x, y, "DEBUG");
    for (i = 0; i < DEBUG_ITEMS; ++i) {
        sceGuColor(i == debug_cursor ? 0xff40e0ffu : 0xffffffffu);
        if (i == debug_cursor) draw_text(x - 12, y + 16 + i * 12, ">");
        draw_text(x, y + 16 + i * 12, lines[i]);
    }
    sceGuColor(0xffa0a0a0u);
    draw_text(x, y + 16 + DEBUG_ITEMS * 12 + 8, "X change  O close");
    sceGuColor(0xffffffffu);
}

static void render_scene(const Player *player, const FollowCamera *camera_state,
                         const char *hud[HUD_LINES], int show_bones) {
    FollowCamera camera_copy = *camera_state;
    const FollowCamera *camera = &camera_copy;
    perf_render_start = sceKernelGetSystemTimeLow();
    int i;
    int bound_texture = -1;

    sceGuStart(GU_DIRECT, display_list);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuColor(0xffffffff);
    /* Material alpha: without this the lit vertex alpha is 0 and the alpha
     * test discards every Dante pixel. */
    sceGuAmbientColor(0xffffffff);
    sceGuClearColor(mission_mode ? 0xff000000u : CLEAR_COLOR);   /* past the edge of a room: dark, not grey-blue */
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    /* Near/far chosen for 16-bit depth precision: on the stage the near
     * plane sits at 25 cm (camera is 3 m back) and far covers the backdrop. */
    if (stage_loaded()) sceGumPerspective(60.0f, (float)SCR_WIDTH / (float)SCR_HEIGHT, 25.0f, 16000.0f);
    else sceGumPerspective(60.0f, (float)SCR_WIDTH / (float)SCR_HEIGHT, 10.0f, 6000.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    {
        ScePspFVector3 eye = {camera->x, camera->y, camera->z};
        ScePspFVector3 target = {camera->tx, camera->ty, camera->tz};
        ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
        sceGumLookAt(&eye, &target, &up);
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Floor and grid don't write depth: no z-fighting between them, and
     * Dante (drawn after) always wins over the floor plane. */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    if (stage_loaded()) {
        if (!bench_no_stage) stage_draw();
        mission_gimmicks_draw();
    {
        /* The real game's soft shadow: on the floor under him, fading as he rises. */
        float floor_y;
        if (stage_loaded() && stage_floor(player->x, player->y + 60.0f, player->z, 0.0f, &floor_y)) {
            float h = player->y - floor_y, k = 1.0f - h / 500.0f;
            if (k > 0.0f) fx_draw_shadow(player->x, floor_y, player->z, 42.0f, (unsigned int)(170.0f * k));
        }
    }
        sceGuDisable(GU_TEXTURE_2D);
    } else {
        sceGuDepthMask(GU_TRUE);
        sceGumDrawArray(GU_TRIANGLES, COLOR_VTYPE, FLOOR_VERTEX_COUNT, 0, floor_vertices);
        sceGumDrawArray(GU_LINES, COLOR_VTYPE, GRID_VERTEX_COUNT, 0, grid_vertices);
        sceGuDepthMask(GU_FALSE);
    }

    sceGumLoadIdentity();
    {
        ScePspFVector3 position = {player->x, player->y, player->z};
        sceGumTranslate(&position);
        sceGumRotateY(player->yaw + MODEL_YAW_OFFSET);
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);     /* character textures are 8-bit, swizzled at load */
    if (player->quicksilver) {
        /* Dante stays dark against the pale negative of everything else. */
        sceGumPushMatrix();
        enemy_draw();
        sceGumPopMatrix();
        fx_screen_negative();
        sceGuDisable(GU_LIGHTING);
        sceGuColor(0xff282830u);
    }
    if (player->devil && player->melee_weapon < DMC3_DEVIL_COUNT && devil_ready(player->melee_weapon)) {
        const DevilModel *m = &dmc3_devils[player->melee_weapon];
        const DevilRuntime *r = devil_ready(player->melee_weapon);
        for (i = 0; i < m->object_count; ++i) {
            const Dmc3ModelObject *object = &m->objects[i];
            if ((int)object->texture != bound_texture) {
                const Dmc3Texture *t = &m->textures[object->texture];
                bound_texture = (int)object->texture;
                t8_bind(t->width, t->height, r->textures[bound_texture]);
                sceGuTexFlush();
            }
            sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)object->index_count, r->indices + object->index_start, r->vertices);
        }
    } else {
    for (i = 0; i < DMC3_MODEL_OBJECT_COUNT; ++i) {
        const Dmc3ModelObject *object = &dmc3_model_objects[i];
        if ((int)object->texture != bound_texture) {
            const Dmc3Texture *t = &dmc3_model_textures[object->texture];
            bound_texture = (int)object->texture;
            t8_bind(t->width, t->height, model_textures[bound_texture]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)object->index_count,
                        model_indices + object->index_start, model_vertices);
    }
    {
        const CoatVertex *coat = coat_skin(player);
        const unsigned short *coat_idx = coat_indices();
        for (i = 0; i < coat_object_count(); ++i) {
            unsigned int start, count, texture;
            coat_object(i, &start, &count, &texture);
            if ((int)texture != bound_texture) {
                const Dmc3Texture *t = &dmc3_model_textures[texture];
                bound_texture = (int)texture;
                t8_bind(t->width, t->height, model_textures[bound_texture]);
                sceGuTexFlush();
            }
            sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)count, coat_idx + start, coat);
        }
    }
    }
    bound_texture = -1;
    if (player->doppelganger && !player->devil) {
        /* His shadow: the same skinned body half a second behind him, dark and see-through.
         * It costs no CPU; it doesn't play its own delayed moves as the real one does. */
        ScePspFVector3 at = {doppel_trail[doppel_head][0], doppel_trail[doppel_head][1], doppel_trail[doppel_head][2]};
        sceGumPushMatrix();
        sceGumLoadIdentity();
        sceGumTranslate(&at);
        sceGumRotateY(doppel_trail[doppel_head][3] + MODEL_YAW_OFFSET);
        sceGuDisable(GU_LIGHTING);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x90909090u, 0x70707070u);
        sceGuColor(0xff301818u);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
        t8_bind(dmc3_model_textures[0].width, dmc3_model_textures[0].height, model_textures[0]);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, DMC3_MODEL_INDEX_COUNT, model_indices, model_vertices);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuDisable(GU_BLEND);
        sceGuEnable(GU_LIGHTING);
        sceGuColor(0xffffffffu);
        sceGumPopMatrix();
    }
    draw_weapons(player);
    bound_texture = -1;
    if (player->quicksilver) {
        sceGuEnable(GU_LIGHTING);
        sceGuColor(0xffffffffu);
    } else {
        enemy_draw();
    }
    orbs_draw();
    {
        float eye[3] = {camera->x, camera->y, camera->z};
        fx_draw(eye);
    }
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);   /* HUD font and floor are linear */
    if (show_bones) draw_bones();

    if (!anim.viewer) {
        hud_draw(player);
        if (mission_mode && menu_loaded()) {
            /* The Red Orbs held, top right: brighter for a moment when one's collected. */
            char text[24];
            float since = orbs_since_collected(), k = since < 2.0f ? 1.0f : since < 3.0f ? 3.0f - since : 0.0f;
            unsigned int alpha = (unsigned int)(150.0f + 105.0f * k);
            snprintf(text, sizeof(text), "%d", shop_state.red_orbs);
            menu_font_draw(SCR_WIDTH - 10 - menu_font_width(20.0f, text), 24.0f, 20.0f, text, alpha << 24 | 0x3c3cffu);
            sceGuDisable(GU_BLEND);
        }
        draw_lock_on_reticle(player, camera);
    }
    draw_hud(hud);
    if (player->weapon_wheel) draw_weapon_wheel(player);
    if (debug_open) draw_debug_menu(anim.viewer);
    if (pause_active) pause_draw();
    if (door_prompt_time > 0.0f && !pause_active && !debug_open) door_prompt_draw();
    sceGuFinish();
    {
        unsigned int t = sceKernelGetSystemTimeLow();
        perf_draw_us = t - perf_render_start;
        sceGuSync(0, 0);
        perf_gpu_us = sceKernelGetSystemTimeLow() - t;
    }
}

/* Copies the finished draw buffer into RAM with a GE block transfer (PPSSPP
 * reads the real GPU framebuffer back for this; plain CPU reads of VRAM see
 * stale memory under its hardware renderer) and saves it as a BMP. */
static void capture_frame(void *draw_buffer, const char *name) {
    const unsigned int bytes = FRAMEBUFFER_BYTES;
    if (!capture_buffer) return;
    sceKernelDcacheWritebackInvalidateRange(capture_buffer, bytes);
    sceGuStart(GU_DIRECT, display_list);
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCR_WIDTH, SCR_HEIGHT, BUF_WIDTH,
                   (void *)((unsigned int)sceGeEdramGetAddr() + (unsigned int)draw_buffer),
                   0, 0, BUF_WIDTH, capture_buffer);
    sceGuTexSync();
    sceGuFinish();
    sceGuSync(0, 0);
    sceKernelDcacheInvalidateRange(capture_buffer, bytes);
    save_bmp(name, capture_buffer);
}

/* ---- Input -------------------------------------------------------------- */

static float stick_axis(unsigned char raw) {
    float value = ((float)raw - 128.0f) / STICK_TILT_SCALE;
    return value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
}

static void read_pad(PlayerInput *input, unsigned int *previous_buttons, int *previous_l) {
    SceCtrlData pad;
    unsigned int pressed;
    sceCtrlPeekBufferPositive(&pad, 1);
    pressed = pad.Buttons & ~*previous_buttons;
    *previous_buttons = pad.Buttons;
    memset(input, 0, sizeof(*input));
    input->stick_x = stick_axis(pad.Lx);
    input->stick_y = -stick_axis(pad.Ly);
    /* Radial dead zone at the game's own neutral threshold, unscaled above it,
     * so tilt stays in the units of the walk / run thresholds. */
    if (sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y) * STICK_TILT_SCALE < STICK_TILT_NEUTRAL)
        input->stick_x = input->stick_y = 0.0f;
    input->style = -1;
    if (pressed & PSP_CTRL_UP) input->style = (pad.Buttons & PSP_CTRL_LTRIGGER) ? STYLE_QUICKSILVER : STYLE_TRICKSTER;
    if (pressed & PSP_CTRL_DOWN) input->style = (pad.Buttons & PSP_CTRL_LTRIGGER) ? STYLE_DOPPELGANGER : STYLE_ROYALGUARD;
    if (pressed & PSP_CTRL_LEFT) input->style = STYLE_SWORDMASTER;
    if (pressed & PSP_CTRL_RIGHT) input->style = STYLE_GUNSLINGER;
    input->lock_on = (pad.Buttons & PSP_CTRL_RTRIGGER) != 0;
    input->weapon_wheel = (pad.Buttons & PSP_CTRL_LTRIGGER) != 0;
    input->melee = (pressed & PSP_CTRL_TRIANGLE) != 0;
    input->gun = (pressed & PSP_CTRL_SQUARE) != 0;
    input->gun_held = (pad.Buttons & PSP_CTRL_SQUARE) != 0;
    /* L tap cycles melee weapons; L + square cycles guns instead of firing. */
    {
        static int l_used;
        if (pad.Buttons & PSP_CTRL_LTRIGGER) {
            if (input->gun) { input->next_gun = 1; input->gun = 0; l_used = 1; }
            /* L + R together: Devil Trigger. */
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER) && (pad.Buttons & PSP_CTRL_RTRIGGER)) { input->devil = 1; l_used = 1; }
            input->gun_held = 0;
            if (input->stick_x != 0.0f || input->stick_y != 0.0f) l_used = 1;  /* picked on the wheel */
        } else if (*previous_l) {
            if (!l_used) input->next_melee = 1;
            l_used = 0;
        }
        *previous_l = (pad.Buttons & PSP_CTRL_LTRIGGER) != 0;
    }
    input->style_action = (pressed & PSP_CTRL_CIRCLE) != 0;
    input->style_held = (pad.Buttons & PSP_CTRL_CIRCLE) != 0;
    input->melee_held = (pad.Buttons & PSP_CTRL_TRIANGLE) != 0;
    input->jump = (pressed & PSP_CTRL_CROSS) != 0;
    input->jump_held = (pad.Buttons & PSP_CTRL_CROSS) != 0;
    input->taunt = (pressed & PSP_CTRL_SELECT) != 0;
    debug_pressed = pressed;
    debug_held = pad.Buttons;
}

/* Scripted input for the automated test (240 frames). */
#define AUTOTEST_FRAMES 2960
#define MOVE_TEST_START 2210
static void scripted_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = -1;
    if (frame >= 30 && frame < 120) input->stick_y = 1.0f;          /* run away */
    else if (frame >= 120 && frame < 200) input->stick_x = 1.0f;    /* run right */
    else if (frame >= 200 && frame < 240) input->stick_y = 1.0f;
    if (frame == 50) input->style = STYLE_GUNSLINGER;
    if (frame == 180) input->snap_camera = 1;                       /* SELECT */
    if (frame == 205) input->jump = 1;
    /* Movement check, in the open street: run sideways so the camera swings,
     * pull straight back and back-left without passing through neutral (the
     * stick has to keep meaning its on-screen direction), then partial tilt
     * (walk), full tilt (run), and lock-on with the stick held sideways. */
    if (frame >= MOVE_TEST_START && frame < 2300) input->stick_x = 1.0f;
    else if (frame >= 2300 && frame < 2380) input->stick_y = -1.0f;
    else if (frame >= 2380 && frame < 2440) { input->stick_x = -0.7f; input->stick_y = -0.7f; }
    else if (frame >= 2460 && frame < 2500) input->stick_y = 0.6f;
    else if (frame >= 2500 && frame < 2540) input->stick_y = 1.0f;
    else if (frame >= 2550 && frame < 2610) { input->stick_x = 1.0f; input->lock_on = 1; }
    /* Weapon wheel: open it and push the stick right, which is Cerberus' slot. */
    if (frame >= 2615 && frame < 2645) input->weapon_wheel = 1;
    if (frame >= 2620 && frame < 2640) input->stick_x = 1.0f;
    if (frame == 2630) input->next_gun = 1;
    /* Guns: two quick Ebony & Ivory shots at the dummy, then the shotgun with
     * lock-on, then Kalina Ann's flying rocket. */
    if (frame == 2665 || frame == 2674) input->gun = 1;
    if (frame >= 2700 && frame < 2705) input->lock_on = 1;
    if (frame == 2702) input->gun = 1;
    if (frame == 2860) input->gun = 1;
    /* Ebony & Ivory while walking forward, with no lock-on. */
    if (frame >= 2920 && frame < 2950) input->stick_y = 1.0f;
    if (frame == 2925 || frame == 2934 || frame == 2943) input->gun = 1;
}

/* Combat autotest: combo I by tapping (262-470), then SELECT so "forward"
 * matches Dante's facing, then Stinger (R + forward + triangle). */
static void scripted_combat(int frame, PlayerInput *input) {
    if (frame == 250) input->reset = 1;                                  /* back to the room centre */
    if (frame == 262 || frame == 272 || frame == 300) input->melee = 1;   /* triangle x3 */
    if (frame == 480) input->snap_camera = 1;
    if (frame >= 515 && frame < 560) input->lock_on = 1;
    if (frame >= 515 && frame < 520) input->stick_y = 1.0f;
    if (frame == 517) input->melee = 1;
    /* Swordmaster: switch style (d-pad left), Prop -> Shredder by mashing
     * circle, then R + back + circle (Dance Macabre). */
    if (frame == 610) input->style = STYLE_SWORDMASTER;
    /* (Stinger ends at ~640.) */
    if (frame >= 660 && frame < 780 && (frame - 660) % 8 == 0) input->style_action = 1;
    if (frame >= 900 && frame < 940) input->lock_on = 1;
    if (frame >= 900 && frame < 906) input->stick_y = -1.0f;
    if (frame == 903) input->style_action = 1;
    /* Trickster (d-pad up): Dash, then jump + Air Hike. Royalguard (d-pad
     * down): hold circle to block. */
    if (frame == 1025) input->reset = 1;                 /* (Dance Macabre is over by then) */
    if (frame == 1028) input->style = STYLE_TRICKSTER;
    if (frame == 1032) input->style_action = 1;
    if (frame == 1100) input->jump = 1;
    if (frame == 1125) input->jump = 1;
    if (frame == 1220) input->style = STYLE_ROYALGUARD;
    if (frame == 1230) input->style_action = 1;
    if (frame >= 1230 && frame < 1330) input->style_held = 1;
    /* Training dummy (placed 180 cm ahead at 1340): lock on, combo I, High
     * Time, then a roll. */
    if (frame >= 1345 && frame < 1790) input->lock_on = 1;
    if (frame == 1350 || frame == 1360 || frame == 1388) input->melee = 1;   /* (combo I ends ~1560) */
    if (frame >= 1590 && frame < 1596) input->stick_y = -1.0f;
    if (frame == 1593) input->melee = 1;                    /* High Time */
    if (frame >= 1730 && frame < 1736) input->stick_x = 1.0f;
    if (frame == 1733) input->jump = 1;                     /* roll */
    /* Trickster (real GDATA 43 data): three chained dashes (a fourth is
     * refused), Sky Star off a jump, then Air Trick onto the dummy (the stick
     * is aimed at it in main). */
    if (frame == 1805) input->style = STYLE_TRICKSTER;
    if (frame == 1810 || frame == 1840 || frame == 1870 || frame == 1900) input->style_action = 1;
    if (frame == 1990) input->jump = 1;
    if (frame == 2005) input->style_action = 1;
    if (frame >= 2080 && frame < 2150) input->lock_on = 1;
    if (frame == 2093) input->style_action = 1;
}

/* Demo for RECORD.TXT: run, then the Rebellion set -- Combo I, Combo II
 * into a mashed Million Stab and the finisher, Stinger, High Time, and a
 * jump into Aerial Rave. Camera starts at a three-quarter angle. */
#define RECORD_FRAMES 1450
static void record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = -1;
    if (frame >= 20 && frame < 80) input->stick_y = 1.0f;
    if (frame == 95 || frame == 105 || frame == 133) input->melee = 1;        /* Combo I */
    if (frame == 320 || frame == 355) input->melee = 1;   /* Combo II: tap, pause (gate 1 opens at frame 29), tap */
    if (frame >= 420 && frame < 640 && (frame - 420) % 6 == 0) input->melee = 1; /* mash: Million Stab */
    if (frame >= 1000 && frame < 1050) input->lock_on = 1;                   /* Stinger */
    if (frame >= 1000 && frame < 1006) input->stick_y = 1.0f;
    if (frame == 1003) input->melee = 1;
    if (frame >= 1150 && frame < 1200) input->lock_on = 1;                   /* High Time */
    if (frame >= 1150 && frame < 1156) input->stick_y = -1.0f;
    if (frame == 1153) input->melee = 1;
    if (frame == 1260) input->jump = 1;                                      /* Helm Breaker (TRI in the air) */
    if (frame == 1275) input->melee = 1;
}

/* Motion tour for RECORD.TXT "motions": every MOT played once in the viewer
 * (capped at 150 frames each, 20-frame minimum). Returns 0 when done. */
#define TOUR_MAX_FRAMES 150
static int tour_motion = 0, tour_frame = 0;
static int tour_step(void) {
    const Motion *m = motion_get(tour_motion);
    int length = (int)(m->end_frame + 1.0f);
    if (length > TOUR_MAX_FRAMES) length = TOUR_MAX_FRAMES;
    if (length < 20) length = 20;
    if (++tour_frame >= length) {
        tour_frame = 0;
        if (++tour_motion >= motion_count()) return 0;
        anim.viewer_motion = tour_motion;
        anim.viewer_frame = 0.0f;
    }
    return 1;
}

/* Scripted lock-on directions: +1 toward the target (or Dante's facing with
 * none), -1 away. Lock-on commands read the stick relative to the target, so
 * demos set pad_aim instead of a fixed camera-relative stick. */
static int pad_aim;
static void aim_stick(PlayerInput *input, const Player *player, int sign) {
    float dir = player->has_target ? atan2f(player->target_x - player->x, player->target_z - player->z) : player->yaw;
    float wx = sinf(dir) * (float)sign, wz = cosf(dir) * (float)sign;
    float cy = player->camera_yaw;
    input->stick_y = wx * sinf(cy) + wz * cosf(cy);      /* camera forward */
    input->stick_x = wx * -cosf(cy) + wz * sinf(cy);     /* camera right */
}

/* Swordmaster demo for RECORD.TXT "swordmaster". */
#define SM_RECORD_FRAMES 1100
static void sm_record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = frame == 5 ? STYLE_SWORDMASTER : -1;
    if (frame >= 30 && frame < 150 && (frame - 30) % 8 == 0) input->style_action = 1;  /* Prop -> Shredder */
    if (frame >= 255 && frame < 300) input->lock_on = 1;                                /* Dance Macabre: */
    if (frame >= 255 && frame < 261) pad_aim = -1;                                       /* R + back + CIR */
    if (frame == 258) input->style_action = 1;
    if (frame >= 300 && frame < 600 && (frame - 300) % 10 == 0) input->style_action = 1; /* steps, stabs -> Crazy Dance */
    if (frame >= 700 && frame < 740) input->lock_on = 1;                                /* Sword Pierce: */
    if (frame >= 700 && frame < 706) pad_aim = 1;                                        /* R + fwd + CIR */
    if (frame == 703) input->style_action = 1;
    if (frame == 790) input->jump = 1;                                                  /* Aerial Rave: */
    if (frame == 805 || frame == 825 || frame == 845 || frame == 865) input->style_action = 1; /* CIR x4 */
    if (frame == 930) input->jump = 1;                                                  /* Helm Breaker */
    if (frame == 945) input->melee = 1;
}

/* Trickster / Royalguard demo for RECORD.TXT "styles" (on the st001 street). */
#define STYLES_RECORD_FRAMES 900
static void styles_record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = frame == 5 ? STYLE_TRICKSTER : frame == 560 ? STYLE_ROYALGUARD : -1;
    if (frame >= 28 && frame < 34) input->stick_y = 1.0f;
    if (frame == 30) input->style_action = 1;                     /* Dash */
    if (frame == 130 || frame == 152) input->jump = 1;            /* jump, Air Hike */
    if (frame >= 170 && frame < 176) input->stick_y = 1.0f;
    if (frame == 172) input->style_action = 1;                    /* Sky Star */
    if (frame >= 300 && frame < 440) input->stick_y = 1.0f;       /* run into the rubble */
    if (frame == 430) input->style_action = 1;                    /* Wall Hike */
    if (frame >= 590 && frame < 700) input->style_held = 1;       /* Royal Block */
    if (frame == 590) input->style_action = 1;
    if (frame == 760) input->jump = 1;
    if (frame == 785) input->style_action = 1;                    /* Air Royal Block */
    if (frame >= 785 && frame < 850) input->style_held = 1;
}

/* Jump demo for RECORD.TXT "jumps": standing jump, running jump, jump + Air Hike. */
#define JUMPS_RECORD_FRAMES 520
static void jumps_record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = -1;
    if (frame == 30) input->jump = 1;                              /* standing jump */
    if (frame >= 150 && frame < 260) input->stick_y = 1.0f;        /* run ... */
    if (frame == 185) input->jump = 1;                             /* ... running jump */
    if (frame == 330) input->jump = 1;                             /* jump ... */
    if (frame == 352) input->jump = 1;                             /* ... Air Hike (Trickster) */
}

/* Training-dummy demo for RECORD.TXT "dummy" (Dante starts 2 m from it). */
#define DUMMY_RECORD_FRAMES 1300
static void dummy_record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = -1;
    input->lock_on = frame >= 10 && frame < 1250;
    if (frame == 20 || frame == 30 || frame == 58) input->melee = 1;          /* Combo I */
    if (frame >= 247 && frame < 253) input->stick_y = -1.0f;
    if (frame == 250) input->melee = 1;                                       /* High Time */
    if (frame == 274) input->jump = 1;                                        /* jump-cancel, follow it up */
    if (frame == 290 || frame == 308 || frame == 326) input->melee = 1;       /* Aerial Rave juggle */
    if (frame >= 467 && frame < 473) input->stick_y = 1.0f;
    if (frame == 470) input->melee = 1;                                       /* Stinger */
    if (frame >= 597 && frame < 603) input->stick_x = 1.0f;
    if (frame == 600) input->jump = 1;                                        /* roll right */
    if (frame >= 677 && frame < 683) input->stick_x = -1.0f;
    if (frame == 680) input->jump = 1;                                        /* roll left */
    if (frame >= 757 && frame < 763) input->stick_y = -1.0f;
    if (frame == 760) input->jump = 1;                                        /* backflip */
    if (frame == 880 || frame == 905 || frame == 935) input->melee = 1;       /* Combo II */
    if (frame >= 980 && frame < 1110 && (frame - 980) % 6 == 0) input->melee = 1; /* Million Stab */
}

/* MEMORY.TXT, written at every boot: what the real hardware has left once
 * everything is loaded, for sizing the rest of the moveset. The largest block
 * is found by allocating (and touching) it, not computed. */
static void write_memory_report(unsigned int boot_us, int motions_loaded) {
    char path[320];
    FILE *f;
    size_t step = 16u * 1024u * 1024u, total = 0, largest = 0;
    void *blocks[64];
    int count = 0, i;
    while (step >= 64u * 1024u && count < 64) {
        void *p = malloc(step);
        if (p) {
            memset(p, 0, 4096);
            memset((char *)p + step - 4096, 0, 4096);
            blocks[count++] = p;
            total += step;
            if (step > largest) largest = step;
        } else {
            step /= 2u;
        }
    }
    for (i = 0; i < count; ++i) free(blocks[i]);
    path_in_game_dir(path, sizeof(path), "MEMORY.TXT");
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "heap_free_kb=%u\nlargest_block_kb=%u\nmotions_loaded=%d\nboot_ms=%u\nstage_loaded=%d\n",
            (unsigned int)(total / 1024u), (unsigned int)(largest / 1024u), motions_loaded, boot_us / 1000u, stage_loaded());
    fclose(f);
}

/* Bullets leave from the gun hand, so they wait until this frame's pose is known.
 * Ebony & Ivory alternate hands; the other guns use the weapon hand. */
#define GUN_MUZZLE_FORWARD 30.0f   /* hand to muzzle: estimate until guns are drawn */
#define TARGET_AIM_HEIGHT 100.0f   /* the lock-on point (player+0x3c40) isn't decoded for the dummy */
/* R = Rz * Ry * Rx, applied X first (FUN_00131760). */
static void place_rotation(const float e[3], float r[3][3]) {
    float cx = cosf(e[0]), sx = sinf(e[0]), cy = cosf(e[1]), sy = sinf(e[1]), cz = cosf(e[2]), sz = sinf(e[2]);
    r[0][0] = cz * cy; r[0][1] = cz * sy * sx - sz * cx; r[0][2] = cz * sy * cx + sz * sx;
    r[1][0] = sz * cy; r[1][1] = sz * sy * sx + cz * cx; r[1][2] = sz * sy * cx - cz * sx;
    r[2][0] = -sy;     r[2][1] = cy * sx;                r[2][2] = cy * cx;
}

/* Dante's model space to the world, as his model matrix does. */
static void model_to_world(const Player *p, const float m[3], float out[3]) {
    float s = sinf(p->yaw), co = cosf(p->yaw);
    out[0] = p->x + co * m[0] + s * m[2];
    out[1] = p->y + m[1];
    out[2] = p->z - s * m[0] + co * m[2];
}

/* Effects tied to the moves and to the player's state, once a frame. */
/* Air Hike's seal takes the melee weapon's colour; only these three weapons have the move.
 * 0xAABBGGRR, from the HD Collection. Sky Star's is always red. */
static unsigned int weapon_fx_color(const Player *p) {
    return p->melee_weapon == 2 ? 0xff2090ffu : p->melee_weapon == 4 ? 0xffffffffu : 0xff2020ffu;
}

static void update_move_fx(const Combat *c, Player *p, float dt) {
    if (p->melee_weapon != 4) weapon_motion_update(&gun_models[p->melee_weapon], combat_weapon_place(c, p, p->melee_weapon, 0), c, dt);
    weapon_motion_update(&gun_models[p->gun_weapon], combat_weapon_place(c, p, p->gun_weapon, 0), c, dt);
    static MoveId last_move = MOVE_NONE;
    static int was_on_ground = 1;
    static float last_vy;
    float feet[3] = {p->x, p->y, p->z};
    if (c->move != last_move) {
        switch (c->move) {
        case MOVE_TR_SKY_STAR: {
            float chest[3] = {p->x, p->y + 100.0f, p->z};
            float way[3] = {sinf(p->yaw), 0.0f, cosf(p->yaw)};
            fx_star_seal(chest, way, 0xff2020ffu);
            break;
        }
        default: break;
        }
        last_move = c->move;
    }
    if (p->hike_fx) {
        if (p->hike_fx == 1) fx_hike_seal(feet, weapon_fx_color(p));
        else fx_jump_burst(feet);
        p->hike_fx = 0;
    }
    was_on_ground = p->on_ground;
    last_vy = p->vy;
    (void)was_on_ground; (void)last_vy;
    {
        /* Fast Trickster moves leave a streak behind him. */
        static float last_chest[3];
        float chest[3] = {p->x, p->y + 100.0f, p->z};
        float dx = chest[0] - last_chest[0], dy = chest[1] - last_chest[1], dz = chest[2] - last_chest[2];
        float step = dx * dx + dy * dy + dz * dz;
        switch (c->move) {
        case MOVE_TR_DASH: case MOVE_TR_DASH_BACK: case MOVE_TR_DASH_LEFT: case MOVE_TR_DASH_RIGHT:
        case MOVE_TR_DASH2: case MOVE_TR_DASH2_BACK: case MOVE_TR_DASH2_LEFT: case MOVE_TR_DASH2_RIGHT:
        case MOVE_TR_DASH_CHAIN: case MOVE_TR_SKY_STAR: case MOVE_TR_AIR_TRICK: case MOVE_TR_AIR_TRICK_AIR:
            if (step > 15.0f * 15.0f && step < 300.0f * 300.0f) fx_streak(last_chest, chest);
            break;
        default: break;
        }
        memcpy(last_chest, chest, sizeof(chest));
    }

    /* The blade's streak while the script says the attack is live. */
    if (c->active && !combat_move_is_gun(c->move) && p->melee_weapon <= 3 && p->melee_weapon != 1) {
        static const float length[4] = {137.0f, 0.0f, 92.0f, 169.0f};
        const WeaponPlace *place = combat_weapon_place(c, p, p->melee_weapon, 0);
        if (place->mode == 0 && !place->owner && place->bone < DMC3_BONE_COUNT) {
            /* The blade runs along the model's +Z: the third column of place_rotation. */
            const float *e = place->rot;
            float cx = cosf(e[0]), sx = sinf(e[0]), cy = cosf(e[1]), sy = sinf(e[1]), cz = cosf(e[2]), sz = sinf(e[2]);
            float axis[3] = {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx};
            float base[3], tip[3], wb[3], wt[3], len = length[p->melee_weapon];
            const float *bp = pose.world_pos[place->bone];
            const Mat3 *br = &pose.world_rot[place->bone];
            int k;
            for (k = 0; k < 3; ++k) {
                float l[3], t[3];
                int j;
                for (j = 0; j < 3; ++j) {
                    l[j] = place->pos[j] + axis[j] * len * 0.3f;
                    t[j] = place->pos[j] + axis[j] * len;
                }
                base[k] = bp[k] + br->m[k][0] * l[0] + br->m[k][1] * l[1] + br->m[k][2] * l[2];
                tip[k] = bp[k] + br->m[k][0] * t[0] + br->m[k][1] * t[1] + br->m[k][2] * t[2];
            }
            model_to_world(p, base, wb);
            model_to_world(p, tip, wt);
            fx_trail_add(wb, wt, 0x38f0f0ffu);
        }
    }
}

static void fire_pending_shots(Combat *c, const Player *p, float dt) {
    int shots = combat_take_shots(c);
    bullets_update(dt);
    while (shots-- > 0) {
        int bone = BONE_L_HAND;
        float s = sinf(p->yaw), co = cosf(p->yaw), hx, hz, muzzle[3], target[3];
        if (p->gun_weapon == 5 && (c->shot_hand++ & 1)) bone = BONE_R_HAND;
        hx = pose.world_pos[bone][0];
        hz = pose.world_pos[bone][2];
        muzzle[0] = p->x + co * hx + s * hz + s * GUN_MUZZLE_FORWARD;
        muzzle[1] = p->y + pose.world_pos[bone][1];
        muzzle[2] = p->z - s * hx + co * hz + co * GUN_MUZZLE_FORWARD;
        target[0] = p->target_x;
        target[1] = p->target_y + TARGET_AIM_HEIGHT;
        target[2] = p->target_z;
        bullets_fire(p->gun_weapon - 5, muzzle, p->yaw, c->aim_target && p->has_target, target);
    }
}

/* FOCUS.TXT: one short scene for what's being worked on (tools/test_focus.ps1).
 * "strafe": locked on to a dummy with Ebony & Ivory, firing while walking right,
 * left, back and forward; reports how far each row carried Dante. */
#define FOCUS_FRAMES 270
#define FOCUS_LEG 50
static const float focus_stick[4][2] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}};
#define FOCUS_FIRE_START 225
/* "coat": close-ups of Dante standing from four sides, running, and stopping. */
#define COAT_FRAMES 345
static int focus_coat;
static int focus_hud;   /* "hud": the cockpit, then locked on to a dummy, in the street */
/* "cmp": CMP_SCRIPT.TXT's inputs ("button first last" a line), Dante's position each frame
 * to CMP_OUT.TXT, to line up with the same script on the real game (tools/ref_compare.py). */
static int focus_cmp;
#define CMP_MAX_EVENTS 64
#define CMP_MAX_FRAMES 3000
static struct { char button[12]; int first, last; } cmp_events[CMP_MAX_EVENTS];
/* "walkto x z first last": over those frames the stick points at (x, z) from wherever he is, through the camera,
 * as a player steers; it lets go within 25 cm. Real walking: no teleports. */
#define CMP_MAX_WALKS 32
static struct { float x, z; int first, last; } cmp_walks[CMP_MAX_WALKS];
static int cmp_walk_count;
static float cmp_px, cmp_pz, cmp_cam_yaw;   /* where he is and the camera's yaw, for walkto */
static int cmp_event_count, cmp_frames;
static int cmp_game_shots[256];   /* saved by the game itself: works on the PSP and sees only its own picture */
static int cmp_shots[32], cmp_shot_count, cmp_enemy_distance = 3000, cmp_camera_yaw, cmp_turbo, cmp_fps = 60, cmp_face_yaw = 9999, cmp_spot_x = 2750, cmp_spot_z = 2450;   /* "spot x z": where Dante starts */   /* "faceyaw deg": Dante's facing at the start */

/* The dummy ramp (tools/psp_bench.py): one more dummy every step until the frame rate has clearly
 * gone, measuring the second half of each step, on the real PSP. Options in CMP_SCRIPT.TXT. */
#define BENCH_MIN_FPS 25.0f
#define BENCH_LOW_STEPS 2
typedef struct {
    int dummies;
    unsigned int frames, late, gap_max, elapsed, update, skin, draw, gpu, ai;
} BenchStep;
static BenchStep bench_steps[ENEMY_MAX + 1];
static int bench_step_frames, bench_count, bench_fire, bench_quicksilver, bench_devil, bench_attack = 1, bench_anim = 1, bench_start = 1, bench_max = ENEMY_MAX;
static int bench_low_steps, bench_done, bench_ai_only, bench_trace;
static int cmp_hits[16][2], cmp_hit_count;  /* "hit frame kind": the first dummy takes this hit */
static unsigned int bench_ai_us;
static int bench_measuring;
static unsigned int bench_began;

static void bench_add(unsigned int gap, unsigned int update, unsigned int skin) {
    BenchStep *b;
    if (!bench_measuring || bench_count >= ENEMY_MAX + 1) return;
    b = &bench_steps[bench_count];
    ++b->frames;
    if (gap > 34000u) ++b->late;
    if (gap > b->gap_max) b->gap_max = gap;
    b->update += update;
    b->ai += bench_ai_us;
    b->skin += skin;
    b->draw += perf_draw_us;
    b->gpu += perf_gpu_us;
}
static int cmp_melee = -1, cmp_gun = -1, cmp_lineup;
/* "mkey button frame" and "mshot frame" drive and picture the menus, for tests. */
#define MENU_SCRIPT_MAX 32
static struct { unsigned int button; int frame; } menu_keys[MENU_SCRIPT_MAX];
static int menu_key_count, menu_shots[MENU_SCRIPT_MAX], menu_shot_count, cmp_menu, cmp_movie, cmp_kill_at;
static struct { unsigned int button; int frame; } game_keys[MENU_SCRIPT_MAX];   /* "gkey button frame": menus in game */
static int game_key_count;
static int cmp_goto[16][2], cmp_goto_count;
static int cmp_place_frame[8], cmp_place_count; static float cmp_place[8][5];   /* "place frame x y z yaw [press]": Dante there (and O pressed if press is 1) */   /* "gotoroom frame number": Dante to that door, its room cleared */
static unsigned int menu_button(const char *name) {
    static const struct { const char *name; unsigned int button; } names[] = {
        {"cross", PSP_CTRL_CROSS}, {"circle", PSP_CTRL_CIRCLE}, {"square", PSP_CTRL_SQUARE}, {"up", PSP_CTRL_UP},
        {"down", PSP_CTRL_DOWN}, {"left", PSP_CTRL_LEFT}, {"right", PSP_CTRL_RIGHT}, {"start", PSP_CTRL_START}, {"select", PSP_CTRL_SELECT}};
    unsigned int i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (!strcmp(names[i].name, name)) return names[i].button;
    return 0;
}
static unsigned int menu_script_pad(int frame) {
    unsigned int held = 0;
    int i;
    for (i = 0; i < menu_key_count; ++i) if (frame >= menu_keys[i].frame && frame < menu_keys[i].frame + 3) held |= menu_keys[i].button;
    return held;
}
static struct { float x, y, z, yaw, dt, floor, cframe; short move, script, ground, room; } cmp_log[CMP_MAX_FRAMES];
static int cmp_realdt;   /* "realdt 1": the real clock, as in play, not a fixed step */
static int cmp_start_room = -1, cmp_start_pos;   /* "startroom number start": for a test, the mission begins in that room */
static void cmp_load(void) {
    char path[320], line[64];
    FILE *f;
    path_in_game_dir(path, sizeof(path), "CMP_SCRIPT.TXT");
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f) && cmp_event_count < CMP_MAX_EVENTS) {
        if (strncmp(line, "frames ", 7) == 0) cmp_frames = atoi(line + 7);
        else if (strncmp(line, "shot ", 5) == 0) { if (cmp_shot_count < 32) cmp_shots[cmp_shot_count++] = atoi(line + 5); }
        else if (strncmp(line, "enemy ", 6) == 0) cmp_enemy_distance = atoi(line + 6);
        else if (strncmp(line, "camyaw ", 7) == 0) cmp_camera_yaw = atoi(line + 7);
        else if (strncmp(line, "faceyaw ", 8) == 0) cmp_face_yaw = atoi(line + 8);
        else if (strncmp(line, "spot ", 5) == 0) sscanf(line + 5, "%d %d", &cmp_spot_x, &cmp_spot_z);
        else if (strncmp(line, "camlock ", 8) == 0) player_stick_yaw_lock = atoi(line + 8) ? (float)cmp_camera_yaw * 0.0174533f : 1000.0f;
        else if (strncmp(line, "turbo ", 6) == 0) cmp_turbo = atoi(line + 6);
        else if (strncmp(line, "ramp ", 5) == 0) bench_step_frames = atoi(line + 5);
        else if (strncmp(line, "rampfrom ", 9) == 0) bench_start = atoi(line + 9);
        else if (strncmp(line, "rampto ", 7) == 0) bench_max = atoi(line + 7);
        else if (strncmp(line, "attack ", 7) == 0) bench_attack = atoi(line + 7);
        else if (strncmp(line, "anim ", 5) == 0) bench_anim = atoi(line + 5);
        else if (strncmp(line, "fire ", 5) == 0) bench_fire = atoi(line + 5);
        else if (strncmp(line, "qs ", 3) == 0) bench_quicksilver = atoi(line + 3);
        else if (strncmp(line, "nostage ", 8) == 0) bench_no_stage = atoi(line + 8);
        else if (strncmp(line, "psp_shot ", 9) == 0) { if (cmp_shot_count < 256) cmp_game_shots[cmp_shot_count++] = atoi(line + 9); }
        else if (strncmp(line, "notex ", 6) == 0) bench_no_texture = atoi(line + 6);
        else if (strncmp(line, "nolight ", 8) == 0) bench_no_lighting = atoi(line + 8);
        else if (strncmp(line, "ring ", 5) == 0) bench_ring = atoi(line + 5);
        else if (strncmp(line, "devil ", 6) == 0) bench_devil = atoi(line + 6);
        else if (strncmp(line, "fps ", 4) == 0) cmp_fps = atoi(line + 4);
        else if (strncmp(line, "aionly ", 7) == 0) bench_ai_only = atoi(line + 7);
        else if (strncmp(line, "menu ", 5) == 0) cmp_menu = atoi(line + 5);
        else if (strncmp(line, "movie ", 6) == 0) cmp_movie = atoi(line + 6);
        else if (strncmp(line, "killevery ", 10) == 0) cmp_kill_at = atoi(line + 10);
        else if (strncmp(line, "mshot ", 6) == 0) { if (menu_shot_count < MENU_SCRIPT_MAX) menu_shots[menu_shot_count++] = atoi(line + 6); }
        else if (strncmp(line, "place ", 6) == 0) {
            if (cmp_place_count < 8 && sscanf(line + 6, "%d %f %f %f %f %f", &cmp_place_frame[cmp_place_count], &cmp_place[cmp_place_count][0], &cmp_place[cmp_place_count][1],
                                               &cmp_place[cmp_place_count][2], &cmp_place[cmp_place_count][3], &cmp_place[cmp_place_count][4]) >= 5) ++cmp_place_count;
        }
        else if (strncmp(line, "gotoroom ", 9) == 0) {
            if (cmp_goto_count < 16 && sscanf(line + 9, "%d %d", &cmp_goto[cmp_goto_count][0], &cmp_goto[cmp_goto_count][1]) == 2) ++cmp_goto_count;
        }
        else if (strncmp(line, "gkey ", 5) == 0) {
            char name[12];
            if (game_key_count < MENU_SCRIPT_MAX && sscanf(line + 5, "%11s %d", name, &game_keys[game_key_count].frame) == 2) {
                game_keys[game_key_count].button = menu_button(name);
                ++game_key_count;
            }
        }
        else if (strncmp(line, "mkey ", 5) == 0) {
            char name[12];
            if (menu_key_count < MENU_SCRIPT_MAX && sscanf(line + 5, "%11s %d", name, &menu_keys[menu_key_count].frame) == 2) {
                menu_keys[menu_key_count].button = menu_button(name);
                ++menu_key_count;
            }
        }
        else if (strncmp(line, "hit ", 4) == 0) {
            if (cmp_hit_count < 16 && sscanf(line + 4, "%d %d", &cmp_hits[cmp_hit_count][0], &cmp_hits[cmp_hit_count][1]) == 2) ++cmp_hit_count;
        }
        else if (strncmp(line, "aitrace ", 8) == 0) bench_trace = atoi(line + 8);
        else if (strncmp(line, "melee ", 6) == 0) cmp_melee = atoi(line + 6);
        else if (strncmp(line, "lineup ", 7) == 0) cmp_lineup = atoi(line + 7);
        else if (strncmp(line, "realdt ", 7) == 0) cmp_realdt = atoi(line + 7);
        else if (strncmp(line, "walkto ", 7) == 0 && cmp_walk_count < CMP_MAX_WALKS) {
            if (sscanf(line + 7, "%f %f %d %d", &cmp_walks[cmp_walk_count].x, &cmp_walks[cmp_walk_count].z,
                       &cmp_walks[cmp_walk_count].first, &cmp_walks[cmp_walk_count].last) == 4) ++cmp_walk_count;
        }
        else if (strncmp(line, "startroom ", 10) == 0) sscanf(line + 10, "%d %d", &cmp_start_room, &cmp_start_pos);
        else if (strncmp(line, "orbs ", 5) == 0) shop_state.red_orbs = atoi(line + 5);
        else if (strncmp(line, "kind ", 5) == 0) { dummy_kind = atoi(line + 5) % ENEMY_KIND_TOTAL; enemy_set_dummy_kind(dummy_kind); }
        else if (strncmp(line, "gun ", 4) == 0) cmp_gun = atoi(line + 4);
        else if (sscanf(line, "%11s %d %d", cmp_events[cmp_event_count].button, &cmp_events[cmp_event_count].first, &cmp_events[cmp_event_count].last) == 3) ++cmp_event_count;
    }
    fclose(f);
    if (cmp_frames > CMP_MAX_FRAMES) cmp_frames = CMP_MAX_FRAMES;
}
static void cmp_pad(int frame, PlayerInput *input) {
    int i;
    if (bench_fire && frame % bench_fire == 0) { input->gun = 1; input->gun_held = 1; }
    for (i = 0; i < cmp_walk_count; ++i) {
        float wx = cmp_walks[i].x - cmp_px, wz = cmp_walks[i].z - cmp_pz, l = sqrtf(wx * wx + wz * wz);
        float s = sinf(cmp_cam_yaw), c = cosf(cmp_cam_yaw);
        if (frame < cmp_walks[i].first || frame > cmp_walks[i].last || l < 25.0f) continue;
        wx /= l; wz /= l;
        /* player.c's stick_world, turned back into the stick */
        input->stick_y = s * wx + c * wz;
        input->stick_x = -c * wx + s * wz;
    }
    for (i = 0; i < cmp_event_count; ++i) {
        const char *b = cmp_events[i].button;
        int held = frame >= cmp_events[i].first && frame <= cmp_events[i].last;
        int pressed = frame == cmp_events[i].first;
        if (!held) continue;
        if (!strcmp(b, "lup")) input->stick_y = 1.0f;
        else if (!strcmp(b, "ldown")) input->stick_y = -1.0f;
        else if (!strcmp(b, "lleft")) input->stick_x = -1.0f;
        else if (!strcmp(b, "lright")) input->stick_x = 1.0f;
        else if (!strcmp(b, "r1")) input->lock_on = 1;
        else if (!strcmp(b, "devil") || !strcmp(b, "l3")) input->devil = pressed;
        else if (!strcmp(b, "menu_start") && pressed) debug_pressed |= PSP_CTRL_START;
        else if (!strcmp(b, "menu_cross") && pressed) debug_pressed |= PSP_CTRL_CROSS;
        else if (!strcmp(b, "menu_down") && pressed) debug_pressed |= PSP_CTRL_DOWN;
        else if (!strcmp(b, "menu_right") && pressed) debug_pressed |= PSP_CTRL_RIGHT;
        else if (!strcmp(b, "select")) input->taunt = pressed;
        else if (!strcmp(b, "cross")) { input->jump = pressed; input->jump_held = 1; }
        else if (!strcmp(b, "triangle")) { input->melee = pressed; input->melee_held = 1; }
        else if (!strcmp(b, "square")) { input->gun = pressed; input->gun_held = 1; }
        else if (!strcmp(b, "circle")) { input->style_action = pressed; input->style_held = 1; }
        else if (!strncmp(b, "style", 5) && pressed) input->style = atoi(b + 5);
        /* As on the real pad, a face button's press also reaches what reads the pad itself: doors, the
         * statue's prompt. */
        if (pressed) {
            if (!strcmp(b, "circle")) debug_pressed |= PSP_CTRL_CIRCLE;
            else if (!strcmp(b, "cross")) debug_pressed |= PSP_CTRL_CROSS;
            else if (!strcmp(b, "triangle")) debug_pressed |= PSP_CTRL_TRIANGLE;
            else if (!strcmp(b, "square")) debug_pressed |= PSP_CTRL_SQUARE;
        }
    }
}
static int focus_weapons;  /* "weapons": each melee weapon and gun, sheathed, then swung / drawn */
#define WEAPONS_STEP 100
#define WEAPONS_FRAMES (WEAPONS_STEP * 5 + 10)
/* "plink": locked on and standing, single shots with gaps; reports the biggest
 * per-frame jump of any bone, overall and around the shots. */
static int focus_plink;
static float plink_prev[DMC3_BONE_COUNT][3], plink_max_jump;
static int plink_max_frame, plink_max_bone;
static void plink_measure(const Pose *p, int frame) {
    int b, k;
    if (frame > 30) {
        for (b = 0; b < DMC3_BONE_COUNT; ++b) {
            float d = 0.0f;
            for (k = 0; k < 3; ++k) {
                float e = p->world_pos[b][k] - plink_prev[b][k];
                d += e * e;
            }
            d = sqrtf(d);
            if (d > plink_max_jump) { plink_max_jump = d; plink_max_frame = frame; plink_max_bone = b; }
        }
    }
    memcpy(plink_prev, p->world_pos, sizeof(plink_prev));
}
/* yaw relative to Dante's facing (0 = behind him), camera distance, and heights. */
static const struct { int frame; float yaw, dist, eye, look; const char *name; } coat_shots[] = {
    {60, 0.0f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_BACK.BMP"}, {70, 3.1416f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_FRONT.BMP"},
    {80, 1.5708f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_SIDE.BMP"}, {90, -1.5708f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_SIDE2.BMP"},
    /* Close-ups: the frame is only 480x272, so get near what's being judged. */
    {100, 3.1416f, 75.0f, 40.0f, 35.0f, "FOCUS_COAT_HEMFRONT.BMP"}, {110, 0.0f, 75.0f, 40.0f, 35.0f, "FOCUS_COAT_HEMBACK.BMP"},
    {120, 1.5708f, 75.0f, 40.0f, 35.0f, "FOCUS_COAT_HEMSIDE.BMP"}, {130, 0.0f, 70.0f, 125.0f, 120.0f, "FOCUS_COAT_WAISTBACK.BMP"},
    {140, 3.1416f, 70.0f, 125.0f, 120.0f, "FOCUS_COAT_WAISTFRONT.BMP"},
    {225, 1.5708f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_RUN.BMP"}, {240, 0.0f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_RUNBACK.BMP"},
    {262, 1.5708f, 75.0f, 40.0f, 35.0f, "FOCUS_COAT_RUNHEM.BMP"}, {290, 1.5708f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_STOP.BMP"},
    {340, 0.0f, 190.0f, 110.0f, 85.0f, "FOCUS_COAT_SETTLED.BMP"},
};
static void focus_coat_camera(const Player *p, FollowCamera *c, int frame) {
    /* A tripod near Dante, at an angle relative to his facing. */
    float yaw = p->yaw, dist = 190.0f, eye = 110.0f, look = 85.0f;
    unsigned int i;
    for (i = 0; i < sizeof(coat_shots) / sizeof(coat_shots[0]); ++i) {
        if (frame <= coat_shots[i].frame) {
            yaw += coat_shots[i].yaw;
            dist = coat_shots[i].dist;
            eye = coat_shots[i].eye;
            look = coat_shots[i].look;
            break;
        }
    }
    c->x = p->x - sinf(yaw) * dist;
    c->y = p->y + eye;
    c->z = p->z - cosf(yaw) * dist;
    c->tx = p->x;
    c->ty = p->y + look;
    c->tz = p->z;
}

static void focus_pad(int frame, PlayerInput *input) {
    int leg = (frame - 20) / FOCUS_LEG;
    memset(input, 0, sizeof(*input));
    input->style = -1;
    if (focus_coat) {
        if (frame >= 145 && frame < 265) input->stick_y = 1.0f;
        return;
    }
    if (focus_hud) {
        input->lock_on = frame >= 40;
        return;
    }
    if (focus_cmp) {
        cmp_pad(frame, input);
        return;
    }
    if (focus_weapons) {
        /* Each step: sheathed at frame 10, a melee press at 25, drawn gun from 45. */
        int t = frame % WEAPONS_STEP;
        if (t == 25) input->melee = 1;
        if (t >= 75) input->lock_on = 1;
        if (t == 5 && frame > WEAPONS_STEP) { input->next_melee = 1; input->next_gun = 1; }
        return;
    }
    if (focus_plink) {
        input->lock_on = frame >= 10;
        if (frame == 60 || frame == 120 || frame == 180) input->gun = 1;
        return;
    }
    if (frame < 10) return;
    input->lock_on = 1;
    /* R alone: strafe with no shots. Then standing shots, alternating guns. */
    if (frame >= FOCUS_FIRE_START && (frame - FOCUS_FIRE_START) % 8 == 0) input->gun = 1;
    if (frame < 20 || leg > 3) return;
    input->stick_x = focus_stick[leg][0];
    input->stick_y = focus_stick[leg][1];
}

/* Focus scenes: hold on this frame until tools/test_focus.ps1 has captured the
 * emulator's window (the renderer's full resolution, not the PSP's 480x272). */
static const char *focus_pending;
static void focus_hold(const char *name) {
    char path[320];
    FILE *f;
    int wait;
    sceDisplayWaitVblankStart();
    path_in_game_dir(path, sizeof(path), "FOCUS_SHOT.TXT");
    f = fopen(path, "w");
    if (!f) return;
    fputs(name, f);
    fclose(f);
    for (wait = 0; wait < 400 && game_file_exists("FOCUS_SHOT.TXT"); ++wait) sceKernelDelayThread(25000);
}

/* ---- Main --------------------------------------------------------------- */

/* Leave the menus for a mission: its stage, with Dante at its start and enemies around him. */
/* Starting camera: behind Dante if that's clear, otherwise the clearest nearby angle
 * (st001 spawns in the office doorway, where "behind" is inside the building). */
static void start_camera(const Player *player, FollowCamera *camera) {
    float best_yaw = camera->yaw, best_score = -1.0f, offset;
    if (!stage_loaded()) return;
    for (offset = 0.0f; offset < 6.28f; offset += 0.3927f) {
        int side;
        for (side = -1; side <= 1; side += 2) {
            float yaw = player->yaw + offset * (float)side, t = 1.0f, score;
            float a[3] = {player->x, player->y + CAMERA_TARGET_HEIGHT, player->z};
            float b[3] = {player->x - sinf(yaw) * CAMERA_DISTANCE, player->y + CAMERA_HEIGHT,
                          player->z - cosf(yaw) * CAMERA_DISTANCE};
            if (!stage_raycast(a, b, &t)) t = 1.0f;
            score = t - offset * 0.04f;  /* prefer angles near "behind" */
            /* Past a doorway there's a wall the collision doesn't see, and no floor. */
            {
                float gy;
                if (!stage_floor(b[0], player->y + 150.0f, b[2], 0.0f, &gy) || fabsf(gy - player->y) > 200.0f) score -= 1.0f;
            }
            if (score > best_score) { best_score = score; best_yaw = yaw; }
        }
    }
    camera->yaw = camera->target_yaw = best_yaw;
    camera->x = player->x - sinf(best_yaw) * CAMERA_DISTANCE;
    camera->z = player->z - cosf(best_yaw) * CAMERA_DISTANCE;
    camera->y = player->y + CAMERA_HEIGHT;
    /* A room change or scripted placement must discard the previous room's camera leash. */
    camera->free_x = camera->x;
    camera->free_z = camera->z;
    camera->clip = 1.0f;
    camera->tx = camera->last_x = player->x;
    camera->tz = camera->last_z = player->z;
    camera->ty = player->y + CAMERA_TARGET_HEIGHT;
    camera->lead_x = camera->lead_z = 0.0f;
}

/* The game's encounter scripts run here. All classes are Prides for now: CEm002, CEm004, CEm013 and the
 * bosses aren't in, so their spawns hold Prides. See docs/GAME_FLOW.md. */
static int mission_room = ROOM_NONE, script_at;
static float script_wait;
static int script_waiting_alive, doors_armed;
static unsigned long long rooms_cleared;   /* this mission: a room's fight doesn't come back */
static float mission_clear_wait;
static int mission_stage;
static MenuChoice mission_choice;
static int mission_difficulty(void) { return mission_choice.difficulty; }
static void menu_test_shot(int frame) {
    int i;
    for (i = 0; i < menu_shot_count; ++i) {
        if (menu_shots[i] == frame) {
            char name[32];
            snprintf(name, sizeof(name), "MENU_%d.BMP", frame);
            capture_frame(menu_draw_buffer, name);
        }
    }
}
static void movie_test_shot(int frame) {
    static int count;
    if (frame == cmp_movie - 5) {
        char name[32];
        snprintf(name, sizeof(name), "MENU_MOVIE%d.BMP", count++);
        capture_frame(menu_draw_buffer, name);
    }
}

/* A cutscene, if it's been converted (tools/prepare_movies.py); tests cut them short with "movie n". */
static void play_movie(const char *name) {
    char path[320];
    int shown, late, decode_max;
    unsigned int elapsed_ms, audio_ms;
    FILE *report;
    if (cmp_menu && !cmp_movie) return;
    path_in_game_dir(path, sizeof(path), name);
    if (!movie_play(path, &menu_draw_buffer, cmp_movie, cmp_menu ? movie_test_shot : NULL)) return;
    movie_stats(&shown, &late, &decode_max);
    movie_timing(&elapsed_ms, &audio_ms);
    path_in_game_dir(path, sizeof(path), "MOVIE_STATS.TXT");
    report = fopen(path, "a");
    if (report) { fprintf(report, "%s shown=%d late=%d decode_max_us=%d error=%08x elapsed=%ums audio=%ums\n", name, shown, late, decode_max, (unsigned int)movie_last_error(), elapsed_ms, audio_ms); fclose(report); }
}

/* ---- A room's gimmicks in a mission: its lift, the mission's goal item, doors held shut by the event
 * script's flags. See tools/gen_rooms.py. */
static int lift_index = -1, lift_on;   /* this room's lift, and whether its switch has been struck */
static float lift_y;
/* Where a start point has no floor under it (110's, at the top of 103's lift shaft, stands on a gimmick
 * that isn't in the stage's collision): a pad there, at the start's height. */
static int pad_on;
static float pad[3];
#define PAD_RADIUS 220.0f
#define LIFT_SPEED 400.0f              /* cm a second: an estimate, the game's isn't read */
#define LIFT_STEP 60.0f
static const RoomLift *room_lift(void) { return lift_index >= 0 ? &room_lifts[lift_index] : 0; }
/* Is (x, z) over the lift's top? */
static int over_lift(const RoomLift *l, float x, float z) {
    float a = -l->yaw * 0.0174533f, dx = x - l->x, dz = z - l->z;
    float lx = dx * cosf(a) - dz * sinf(a), lz = dx * sinf(a) + dz * cosf(a);
    return fabsf(lx) < l->half_x && fabsf(lz) < l->half_z;
}
/* The stage's floor, or the lift's top when that's higher and under the feet. */
static int mission_floor(float x, float y, float z, float step, float *out) {
    const RoomLift *l = room_lift();
    int found = stage_floor(x, y, z, step, out);
    if (l && over_lift(l, x, z) && lift_y <= y + step + LIFT_STEP && (!found || lift_y > *out)) { *out = lift_y; found = 1; }
    if (pad_on && (x - pad[0]) * (x - pad[0]) + (z - pad[2]) * (z - pad[2]) < PAD_RADIUS * PAD_RADIUS && pad[1] <= y + step + LIFT_STEP
        && (!found || pad[1] > *out)) { *out = pad[1]; found = 1; }
    return found;
}
/* A crest switch and the seal it takes down (CStageSetSwitch, CStageSetSeal; docs/BAR_CRANK_SPEC.md). Sword
 * damage spins the switch up (300), then lights its 8 flames (70 each); 2 s without a hit and it runs down.
 * Lit, it sets the room's flag and the seal fades, its wall gone at once. Once open it stays open this mission
 * (the event script's counter 1 = 2). */
#define SEAL_TAG 255                /* a stage box tag no stage batch uses */
static int seal_index = -1, sw_lit;
static int sw_log_state = -1;
static float sw_log_at;
static float sw_charge, sw_wing, sw_flames, sw_window, sw_spin, sw_spin2, seal_alpha, seal_time;
static unsigned int seal_open_bits;
static const RoomSwitchSeal *room_seal(void) { return seal_index >= 0 ? &room_switch_seals[seal_index] : 0; }
static void seal_enter(int room) {
    const RoomSwitchSeal *w;
    int i, open;
    seal_index = -1;
    for (i = 0; i < ROOM_SWITCH_SEAL_COUNT; ++i)
        if (room_switch_seals[i].mission == mission_choice.mission && room_switch_seals[i].room == room) seal_index = i;
    w = room_seal();
    if (!w) return;
    sw_log_state = -1;
    sw_log_at = 0.0f;
    seal_time = sw_spin = sw_spin2 = 0.0f;
    open = seal_open_bits >> seal_index & 1u;
    sw_lit = open;
    sw_charge = open ? SWITCH_CHARGE : 0.0f;
    sw_wing = open ? 200.0f : 0.0f;
    sw_flames = open ? SWITCH_FLAMES : 0.0f;
    sw_window = 0.0f;
    seal_alpha = open ? 0.0f : 1.0f;
    if (!open) stage_add_box(w->box[0], w->box[1], w->box[2], w->box[3], w->box[4], w->box[5], w->box[6], SEAL_TAG);
}
/* FUN_00306000: a melee hit adds its damage (the port's 42 a light hit, 84 a heavy one: Dante's real damage
 * isn't read), to the charge until it's spun up, then to the flames. */
static void switch_hit(const Player *player, float reach, int kind) {
    const RoomSwitchSeal *w = room_seal();
    float dx, dz, dy, d, amount;
    if (!w || sw_lit) return;
    dx = w->hit[0] - player->x; dz = w->hit[2] - player->z; dy = w->hit[1] - player->y;
    d = sqrtf(dx * dx + dz * dz);
    if (d > reach + w->hit[3] || dy < -w->hit[3] || dy > w->hit[3] + 200.0f) return;
    if (d > 1.0f && (dx * sinf(player->yaw) + dz * cosf(player->yaw)) / d < 0.2f) return;
    amount = kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f;
    if (sw_charge < SWITCH_CHARGE) {
        sw_charge = fminf(sw_charge + amount, SWITCH_CHARGE + 1.0f);
        if (sw_charge > 100.0f) sw_wing = fminf(sw_wing + amount, 201.0f);
    } else {
        sw_flames = fminf(sw_flames + amount, SWITCH_FLAMES + 1.0f);
    }
    sw_window = SWITCH_WINDOW;
    {
        float at[3] = {w->hit[0], w->hit[1], w->hit[2]};
        fx_hit_spark(at);
    }
}
/* FUN_00305820 each frame (frames = game frames this step). */
static void seal_update(float frames) {
    const RoomSwitchSeal *w = room_seal();
    if (!w) return;
    seal_time += frames;
    if (!sw_lit) {
        if (sw_window > 0.0f) sw_window -= frames;
        else {
            float drain = SWITCH_DRAIN * frames;
            sw_charge = fmaxf(sw_charge - drain, 0.0f);
            sw_wing = fmaxf(sw_wing - drain, 0.0f);
            if (sw_charge <= 100.0f) sw_flames = fmaxf(sw_flames - drain, 0.0f);
        }
        if (sw_charge >= SWITCH_CHARGE && sw_flames >= SWITCH_FLAMES) {
            sw_lit = 1;
            seal_open_bits |= 1u << seal_index;
            stage_hide_group(SEAL_TAG);   /* FUN_0031d420: the wall goes as the fade starts */
        }
    }
    if (focus_cmp) {
        /* Tests: flame transitions plus periodic samples, including after the gauges reach zero. */
        int now = sw_lit ? 99 : (int)(sw_flames / SWITCH_PER_FLAME) + ((int)(sw_charge / 100.0f) << 4);
        if (now != sw_log_state || seal_time >= sw_log_at) {
            char path[320];
            FILE *f;
            path_in_game_dir(path, sizeof(path), "SEAL_LOG.TXT");
            f = fopen(path, "a");
            if (f) { fprintf(f, "room=%d t=%.0f charge=%.0f flames=%.0f lit=%d\n", mission_room, seal_time, sw_charge, sw_flames, sw_lit); fclose(f); }
            sw_log_state = now;
            sw_log_at = seal_time + 60.0f;
        }
    }
    sw_spin += 60.0f * sw_charge / SWITCH_CHARGE * frames;
    sw_spin2 += 20.0f * sw_wing / 200.0f * frames;
    if (sw_lit && seal_alpha > 0.0f) seal_alpha = fmaxf(seal_alpha - frames / 32.0f, 0.0f);
}
static int door_gated(const RoomDoor *d) {
    int i;
    {
        const RoomSwitchSeal *w = room_seal();
        if (w && !sw_lit && d->number == w->next_number) return 1;   /* the seal's door: flag 33 */
    }
    for (i = 0; i < DOOR_GATE_COUNT; ++i) {
        const DoorGate *g = &door_gates[i];
        if (g->mission != mission_choice.mission || g->room != mission_room || g->next_number != d->number) continue;
        if (g->needs_room >= 64 || !(rooms_cleared >> g->needs_room & 1ull)) return 1;
    }
    return 0;
}
static void draw_box(float x, float y0, float y1, float z, float hx, float hz, float yaw, unsigned int top, unsigned int side) {
    float a = yaw * 0.0174533f, c = cosf(a), s = sinf(a), px[4], pz[4];
    static const float cx[4] = {-1, 1, 1, -1}, cz[4] = {-1, -1, 1, 1};
    int i, n = 0;
    ColorVertex *o = (ColorVertex *)sceGuGetMemory(sizeof(ColorVertex) * 30);
    for (i = 0; i < 4; ++i) {
        float lx = cx[i] * hx, lz = cz[i] * hz;
        px[i] = x + lx * c + lz * s;
        pz[i] = z - lx * s + lz * c;
    }
    {
        int q[6] = {0, 1, 2, 0, 2, 3};
        for (i = 0; i < 6; ++i) o[n++] = (ColorVertex){top, px[q[i]], y1, pz[q[i]]};
    }
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        o[n++] = (ColorVertex){side, px[i], y1, pz[i]};
        o[n++] = (ColorVertex){side, px[j], y1, pz[j]};
        o[n++] = (ColorVertex){side, px[j], y0, pz[j]};
        o[n++] = (ColorVertex){side, px[i], y1, pz[i]};
        o[n++] = (ColorVertex){side, px[j], y0, pz[j]};
        o[n++] = (ColorVertex){side, px[i], y0, pz[i]};
    }
    sceGumDrawArray(GU_TRIANGLES, COLOR_VTYPE, n, 0, o);
}
/* Stand-ins for their models, which aren't converted yet: the lift as a stone block, its switch as a
 * red glow until struck, the goal item as a turning gold block. */
static float goal_spin;
/* Breakable props (rooms_runtime.h's room_breakables): hits left each, and broken ones stay broken this mission. */
static short breakable_hits[ROOM_BREAKABLE_COUNT > 0 ? ROOM_BREAKABLE_COUNT : 1];
static unsigned char breakable_broken[ROOM_BREAKABLE_COUNT > 0 ? ROOM_BREAKABLE_COUNT : 1];
static void breakables_reset(void) {
    int i;
    for (i = 0; i < ROOM_BREAKABLE_COUNT; ++i) { breakable_hits[i] = room_breakables[i].hits; breakable_broken[i] = 0; }
}
static void breakables_enter(int room) {
    int i;
    for (i = 0; i < ROOM_BREAKABLE_COUNT; ++i)
        if (room_breakables[i].room == room && breakable_broken[i]) stage_hide_group(room_breakables[i].number);
    /* The room's solid boxes: its invisible walls and breakable props (tools/gimmicks.py collision_boxes). */
    stage_clear_boxes();
    for (i = 0; i < ROOM_BOX_COUNT; ++i)
        if (room_boxes[i].room == room)
            stage_add_box(room_boxes[i].x, room_boxes[i].y, room_boxes[i].z, room_boxes[i].hx, room_boxes[i].hy,
                          room_boxes[i].hz, room_boxes[i].yaw, room_boxes[i].tag);
    /* Story pieces, as the room's event script sets them for where the story is: 006 from Mission 4 is the
     * room after Cerberus is beaten (tools/gen_rooms.py STORY). */
    for (i = 0; i < ROOM_STORY_COUNT; ++i)
        if (room_story[i].room == room && mission_choice.mission >= room_story[i].first && mission_choice.mission <= room_story[i].last)
            stage_hide_group(room_story[i].tag);
}
static void breakable_hit(const Player *player, float reach, int kind) {
    int i, k;
    float fx = sinf(player->yaw), fz = cosf(player->yaw);
    if (!mission_mode || mission_room == ROOM_NONE) return;
    switch_hit(player, reach, kind);
    for (i = 0; i < ROOM_BREAKABLE_COUNT; ++i) {
        const RoomBreakable *b = &room_breakables[i];
        float dx = b->x - player->x, dz = b->z - player->z, d = sqrtf(dx * dx + dz * dz), dy = b->y - player->y;
        if (b->room != mission_room || breakable_broken[i]) continue;
        if (d > reach + b->radius || dy < -200.0f || dy > b->height + 100.0f) continue;
        if (d > 1.0f && (dx * fx + dz * fz) / d < 0.2f) continue;    /* in front of him, as for enemies */
        {
            float at[3] = {b->x, b->y + b->height * 0.5f, b->z};
            fx_hit_spark(at);
        }
        breakable_hits[i] -= kind == HIT_LIGHT || kind == HIT_AIR ? 1 : 2;
        if (b->crystal) orbs_burst(b->x, b->y + b->height * 0.5f, b->z, b->orbs[0]);   /* a crystal pays each hit */
        if (breakable_hits[i] > 0) continue;
        breakable_broken[i] = 1;
        stage_hide_group(b->number);
        if (!b->crystal)
            for (k = 0; k < 2; ++k)
                if (b->orbs[k] > 0 && rand() % 100 < b->chance[k]) orbs_burst(b->x, b->y, b->z, b->orbs[k]);
    }
}
static int shop_closed;   /* the shop had its own frames: the game's clock starts again */
/* Stand-ins until model 20 and the seal's board are drawn: the switch's hub, 8 blades turning with the charge
 * and its 8 flames on a 108 cm circle (red once lit, FUN_00305dd0); the seal as a blue, shimmering board. */
static void seal_draw(void) {
    const RoomSwitchSeal *w = room_seal();
    int i;
    if (!w) return;
    {
        float a = w->disc[3] * 0.0174533f, ux = sinf(a), uz = cosf(a);   /* the disc's normal; its plane is (side, up) */
        float sx = uz, sz = -ux, px = w->disc[0] + ux * 8.0f, py = w->disc[1], pz = w->disc[2] + uz * 8.0f;
        draw_box(w->disc[0], py - 22.0f, py + 22.0f, w->disc[2], 22.0f, 22.0f, w->disc[3], 0xff505860u, 0xff30363cu);
        for (i = 0; i < 8; ++i) {
            float b = (sw_spin + 45.0f * i) * 0.0174533f, r = 50.0f;
            draw_box(px + sx * sinf(b) * r, py + cosf(b) * r - 8.0f, py + cosf(b) * r + 8.0f, pz + sz * sinf(b) * r, 8.0f, 8.0f,
                     w->disc[3], 0xff8090a0u, 0xff506070u);
        }
        for (i = 0; i < 8; ++i) {
            float b = 45.0f * i * 0.0174533f, r = 108.0f;
            int lit = sw_flames >= SWITCH_PER_FLAME * (i + 1);
            draw_box(px + sx * sinf(b) * r, py + cosf(b) * r - 9.0f, py + cosf(b) * r + 9.0f, pz + sz * sinf(b) * r, 9.0f, 9.0f,
                     w->disc[3], lit ? 0xff3050ffu : 0xff202028u, lit ? 0xff2030c0u : 0xff101018u);
        }
    }
    if (seal_alpha > 0.0f) {
        /* CStageSetSeal: the board tinted by the colour (1 blue: RGB 0x30 0x40 0x80), alpha up to 0x40, shimmering. */
        float c = cosf(w->board[6]), sn = sinf(w->board[6]), hw = w->board[3], y0 = w->board[4], y1 = w->board[5];
        float x0 = w->board[0] - c * hw, z0 = w->board[2] + sn * hw, x1 = w->board[0] + c * hw, z1 = w->board[2] - sn * hw;
        float shimmer = 0.85f + 0.15f * sinf(seal_time * 0.21f);
        unsigned int alpha = (unsigned int)(150.0f * seal_alpha * shimmer), edge = (unsigned int)(230.0f * seal_alpha);
        unsigned int fill = w->colour == 0 ? 0x003040ffu : w->colour == 2 ? 0x00ff3080u : 0x00ff6030u;
        unsigned int col = alpha << 24 | fill, rim = edge << 24 | (fill | 0x00404040u);
        ColorVertex *o = (ColorVertex *)sceGuGetMemory(sizeof(ColorVertex) * 12);
        ColorVertex *e = (ColorVertex *)sceGuGetMemory(sizeof(ColorVertex) * 8);
        o[0] = (ColorVertex){col, x0, y0, z0}; o[1] = (ColorVertex){col, x1, y0, z1}; o[2] = (ColorVertex){col, x1, y1, z1};
        o[3] = (ColorVertex){col, x0, y0, z0}; o[4] = (ColorVertex){col, x1, y1, z1}; o[5] = (ColorVertex){col, x0, y1, z0};
        for (i = 0; i < 6; ++i) o[6 + i] = o[5 - i];   /* both faces */
        e[0] = (ColorVertex){rim, x0, y0, z0}; e[1] = (ColorVertex){rim, x1, y0, z1};
        e[2] = (ColorVertex){rim, x1, y0, z1}; e[3] = (ColorVertex){rim, x1, y1, z1};
        e[4] = (ColorVertex){rim, x1, y1, z1}; e[5] = (ColorVertex){rim, x0, y1, z0};
        e[6] = (ColorVertex){rim, x0, y1, z0}; e[7] = (ColorVertex){rim, x0, y0, z0};
        sceGuDepthMask(GU_TRUE);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGumDrawArray(GU_TRIANGLES, COLOR_VTYPE, 12, 0, o);
        sceGumDrawArray(GU_LINES, COLOR_VTYPE, 8, 0, e);
        sceGuDisable(GU_BLEND);
        sceGuDepthMask(GU_FALSE);
    }
}
static void mission_gimmicks_draw(void) {
    const RoomLift *l;
    const MissionInfo *info;
    if (!mission_mode || mission_room == ROOM_NONE) return;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    l = room_lift();
    if (l) {
        draw_box(l->x, l->low - 20.0f, lift_y, l->z, l->half_x, l->half_z, l->yaw, 0xff6a7078u, 0xff40464cu);
        draw_box(l->switch_pos[0], l->switch_pos[1], l->switch_pos[1] + 90.0f, l->switch_pos[2], 30.0f, 30.0f, 45.0f,
                 lift_on ? 0xffffc040u : 0xff2020ffu, lift_on ? 0xffa07020u : 0xff1010a0u);
    }
    seal_draw();
    info = &missions[mission_choice.mission - 1];
    if (info->goal_room == mission_room && (rooms_cleared >> info->goal_after & 1ull)) {
        goal_spin += 3.0f;
        draw_box(info->goal[0], info->goal[1] + 60.0f, info->goal[1] + 120.0f, info->goal[2], 25.0f, 25.0f, goal_spin, 0xff40e0ffu, 0xff2090c0u);
    }
    sceGuEnable(GU_TEXTURE_2D);
}

#define DOOR_MARGIN 40.0f      /* Dante stops at a wall about this far short of a door's box */
#define DOOR_BELOW 100.0f

/* One step of this room's script: spawns, waits. Returns 1 once it has finished. */
static int script_run(float dt, float floor_y) {
    const RoomScript *script = &missions[mission_choice.mission - 1].scripts[mission_choice.difficulty & 1][mission_room];
    while (script_at < script->count) {
        const RoomOp *op = &script->ops[script_at];
        if (script_wait > 0.0f) {
            if (script_waiting_alive >= 0 && enemy_alive_count() > script_waiting_alive) return 0;
            script_waiting_alive = -1;
            script_wait -= dt * 60.0f;
            if (script_wait > 0.0f) return 0;
        }
        switch (op->op) {
        case ROOM_OP_SPAWN:
            /* From the script's own height: Enigmas stand on ledges and walls. */
            enemy_spawn_height((float)op->c > floor_y ? (float)op->c : floor_y);
            enemy_add_kind(enemy_kind_for_class(op->a), (float)op->b, (float)op->d);
            break;
        case ROOM_OP_WAIT:
            script_wait = (float)op->a;
            break;
        default:
            script_wait = (float)op->b + 0.01f;
            script_waiting_alive = op->a;
            break;
        }
        ++script_at;
        if (script_wait > 0.0f) return 0;
    }
    return script_wait <= 0.0f;
}

/* Into another room: its stage, Dante at the start the door names, and the room's script from the top. */
static void enter_room(int room, int start, Player *player, FollowCamera *camera) {
    char path[320];
    const float *s = rooms[room].starts[start];
    menu_show_loading();
    enemy_set_count(0, player->x, player->z);
    orbs_clear();
    path_in_game_dir(path, sizeof(path), rooms[room].file);
    stage_load(path);
    breakables_enter(room);
    seal_enter(room);
    mission_room = room;
    doors_armed = 0;
    {
        int i;
        lift_index = -1;
        lift_on = 0;
        for (i = 0; i < ROOM_LIFT_COUNT; ++i) if (room_lifts[i].room == room && room_lifts[i].high > room_lifts[i].low) lift_index = i;
        if (lift_index >= 0) lift_y = room_lifts[lift_index].low;
    }
    {
        float g;
        pad_on = 0;
        if (!stage_floor(s[0], s[1] + 50.0f, s[2], 0.0f, &g) || g < s[1] - 100.0f) {
            pad_on = 1; pad[0] = s[0]; pad[1] = s[1]; pad[2] = s[2];
        }
    }
    /* Doors can face the wrong way for the camera, and nothing else stops him walking through a shut one. */
    script_at = (rooms_cleared >> room & 1ull) ? missions[mission_choice.mission - 1].scripts[mission_choice.difficulty & 1][room].count : 0;
    script_wait = 0.0f;
    script_waiting_alive = -1;
    player_spawn[0] = s[0];
    player_spawn[1] = s[1];
    player_spawn[2] = s[2];
    player_spawn[3] = s[3];
    combat_reset(&combat);
    player->x = s[0];
    player->y = s[1];
    player->z = s[2];
    player->yaw = s[3] * 0.0174533f;
    player->on_ground = 1;
    coat_reset();
    start_camera(player, camera);
}

/* Each frame in a mission. Returns 1 when it's cleared. Doors stay shut while there's a fight on. */
static int mission_update(float dt, unsigned int pressed, Player *player, FollowCamera *camera) {
    const MissionInfo *info = &missions[mission_choice.mission - 1];
    int done = script_run(dt, player->y), i, in_a_box = 0;
    const RoomLift *lift = room_lift();
    door_prompt_time = 0.0f;
    seal_update(dt * 60.0f);
    if (lift) {
        /* The switch: strike it or press O by it. Then the lift carries him up while he stands on it, and
         * comes back down when he's off it. */
        float sx = player->x - lift->switch_pos[0], sz = player->z - lift->switch_pos[2];
        int on = over_lift(lift, player->x, player->z) && fabsf(player->y - lift_y) < 40.0f;
        if (!lift_on && sx * sx + sz * sz < 220.0f * 220.0f && fabsf(player->y - lift->switch_pos[1]) < 250.0f) {
            door_prompt_time = 1.0f;
            if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE)) lift_on = 1;
        }
        if (lift_on && on && lift_y < lift->high) {
            lift_y += LIFT_SPEED * dt;
            if (lift_y > lift->high) lift_y = lift->high;
            player->y = lift_y;
        } else if (lift_on && !on && lift_y > lift->low) {
            lift_y -= LIFT_SPEED * dt;
            if (lift_y < lift->low) lift_y = lift->low;
        }
    }
    for (i = 0; i < ROOM_SHOP_COUNT; ++i) {
        /* A Divinity Statue: O by it opens the shop, when there's no fight on. */
        const RoomShop *s = &room_shops[i];
        float sx = player->x - s->x, sz = player->z - s->z;
        if (s->room != mission_room || !done || enemy_alive_count() > 0) continue;
        if (sx * sx + sz * sz > 260.0f * 260.0f || fabsf(player->y - s->y) > 300.0f) continue;
        door_prompt_time = 1.0f;
        if (pressed & PSP_CTRL_CIRCLE) {
            shop_run(&shop_state, cmp_menu ? menu_script_pad : NULL, cmp_menu ? menu_test_shot : NULL, 100000);
            shop_closed = 1;
            return 0;
        }
    }
    if (info->goal_room == mission_room && (rooms_cleared >> info->goal_after & 1ull) && done && enemy_alive_count() == 0) {
        /* The mission's item: walk up to it and press O. */
        float gx = player->x - info->goal[0], gz = player->z - info->goal[2];
        if (gx * gx + gz * gz < 160.0f * 160.0f && fabsf(player->y - info->goal[1]) < 300.0f) {
            door_prompt_time = 1.0f;
            if (pressed & PSP_CTRL_CIRCLE) return 1;
        }
    }
    if (!done || enemy_alive_count() > 0) {
        /* Shut until the fight's over: push him back out of a door's box. */
        for (i = 0; doors_armed && i < rooms[mission_room].door_count; ++i) {
            const RoomDoor *d = &rooms[mission_room].doors[i];
            float dx = player->x - d->box[0], dz = player->z - d->box[2];
            float hx = d->box[3] * 0.5f + DOOR_MARGIN, hz = d->box[5] * 0.5f + DOOR_MARGIN;
            if (fabsf(dx) >= hx || fabsf(dz) >= hz) continue;
            if (player->y < d->box[1] - DOOR_BELOW || player->y > d->box[1] + d->box[4]) continue;
            if (hx - fabsf(dx) < hz - fabsf(dz)) player->x = d->box[0] + (dx < 0.0f ? -hx : hx);
            else player->z = d->box[2] + (dz < 0.0f ? -hz : hz);
        }
        if (!doors_armed) {
            int inside = 0;
            for (i = 0; i < rooms[mission_room].door_count; ++i) {
                const RoomDoor *d = &rooms[mission_room].doors[i];
                if (fabsf(player->x - d->box[0]) < d->box[3] * 0.5f + DOOR_MARGIN && fabsf(player->z - d->box[2]) < d->box[5] * 0.5f + DOOR_MARGIN
                && player->y > d->box[1] - 400.0f && player->y < d->box[1] + d->box[4]) inside = 1;
            }
            if (!inside) doors_armed = 1;
        }
        return 0;
    }
    rooms_cleared |= 1ull << mission_room;
    if (info->end_room == ROOM_NONE && info->goal_room == ROOM_NONE) {
        mission_clear_wait += dt;
        return mission_clear_wait > 2.0f;
    }
    for (i = 0; i < rooms[mission_room].door_count; ++i) {
        const RoomDoor *d = &rooms[mission_room].doors[i];
        float mx = d->box[3] * 0.5f + DOOR_MARGIN, mz = d->box[5] * 0.5f + DOOR_MARGIN;
        if (mission_choice.mission < d->low || mission_choice.mission > d->high) continue;
        if (door_gated(d)) continue;
        if (player->y < d->box[1] - 400.0f || player->y > d->box[1] + d->box[4]) continue;   /* a door on another floor */
        if (player->x < d->box[0] - mx || player->x > d->box[0] + mx || player->z < d->box[2] - mz || player->z > d->box[2] + mz) continue;
        /* Dante often arrives standing in the door he came through: it waits until he's stepped out
         * sideways, since he can land and bounce through its height. */
        in_a_box = 1;
        if (!doors_armed) continue;
        /* A tower has doors at many heights. The temporary end of a mission can be above the floor. */
        if (d->number != info->end_room && (player->y < d->box[1] - DOOR_BELOW || player->y > d->box[1] + d->box[4])) continue;
        if (d->press) {
            door_prompt_time = 1.0f;
            if (!(pressed & PSP_CTRL_CIRCLE)) continue;
        }
        if (d->number == info->end_room) return 1;   /* the way out of the mission */
        if (d->next == ROOM_NONE) continue;           /* a room that isn't built */
        enter_room(d->next, d->start, player, camera);
        return 0;
    }
    if (!in_a_box) doors_armed = 1;
    return 0;
}

static void menu_clear_shot(int frame) {
    if (frame == 30) capture_frame(menu_draw_buffer, "MENU_CLEAR.BMP");
}

static void play_intro(int mission) {
    if (mission == 1) play_movie("MOVIES/M1_INTRO.MOV");
    if (mission == 3) { play_movie("MOVIES/M3_INTRO_A.MOV"); play_movie("MOVIES/M3_INTRO_B.MOV"); }
}

/* Mission Select's Debug Room: the Mission 2 street (STAGE.BIN), empty, with the debug menu's dummies. */
static int from_menu;   /* the Debug Room was chosen from the menu: the debug menu can go back to it */
static void start_debug_room(const MenuChoice *choice, Player *player, FollowCamera *camera) {
    char path[320];
    game_turbo = choice->turbo;
    mission_mode = 0;
    mission_room = ROOM_NONE;
    lift_index = -1;
    pad_on = 0;
    from_menu = 1;
    menu_show_loading();
    enemy_set_count(0, player->x, player->z);
    path_in_game_dir(path, sizeof(path), "STAGE.BIN");
    if (stage_load(path)) {
        const StageInfo *si = stage_info();
        player_spawn[0] = si->spawn[0];
        player_spawn[1] = si->spawn[1];
        player_spawn[2] = si->spawn[2];
        player_spawn[3] = si->spawn_yaw;
    }
    player_floor_fn = stage_floor;
    camera_auto_follow = 1;
    combat_reset(&combat);
    player_reset(player, camera);
    enemy_set_mortal(0);
    enemy_set_attacks(dummy_attacks);
    enemy_set_anim_auto(dummy_anim_auto);
    coat_reset();
    start_camera(player, camera);
}

static void start_mission(const MenuChoice *choice, Player *player, FollowCamera *camera) {
    if (choice->mission == MENU_DEBUG_ROOM) { start_debug_room(choice, player, camera); return; }
    mission_mode = 1;
    game_turbo = choice->turbo;
    player_floor_fn = mission_floor;
    combat_reset(&combat);
    player_reset(player, camera);
    dummy_attacks = 1;
    enemy_set_attacks(1);
    enemy_set_anim_auto(dummy_anim_auto);
    enemy_set_mortal(1);
    mission_clear_wait = 0.0f;
    rooms_cleared = 0;
    seal_open_bits = 0;
    seal_index = -1;
    breakables_reset();
    {
        /* A test can begin the mission in another room ("startroom"); play always starts where the game does. */
        int first = missions[choice->mission - 1].first_room, start = 0, i;
        for (i = 0; cmp_start_room >= 0 && i < (int)(sizeof(rooms) / sizeof(rooms[0])); ++i) {
            char want[16];
            snprintf(want, sizeof(want), "ROOM_%03d.BIN", cmp_start_room);
            if (!strcmp(rooms[i].file, want)) { first = i; start = cmp_start_pos; }
        }
        enter_room(first, start, player, camera);
    }
}

int main(int argc, char *argv[]) {
    Player player = {.melee_weapon = 0, .gun_weapon = 5};   /* player_reset keeps the weapons */
    FollowCamera camera;
    PlayerInput input;
    void *draw_buffer = (void *)0;
    unsigned int last_time, previous_buttons = 0, boot_start;
    int previous_l = 0, moves_total = 0, moves_finished = 0, moves_failed = -1, moves_fallbacks = 0;
    int inputs_total = 0, inputs_ok = 0, inputs_failed = -1, focus = 0;
    unsigned int frame_us_total = 0, frame_us_max = 0, last_frame_us = 0, skin_us_max = 0;
    unsigned int rest_error;
    int frames = 0;
    int dummy_ready = 0, dummy_hits_combo = 0, roll_seen = 0;
    float dummy_max_height = 0.0f, roll_travel = 0.0f, roll_x = 0.0f, roll_z = 0.0f;
    int autotest, recording, show_bones, tour = 0, sm_demo = 0, styles_demo = 0, jumps_demo = 0, dummy_demo = 0, tour_done = 0, motions_loaded, combat_ready = 0;
    int dash_seen = 0, hike_seen = 0, block_frames = 0;
    int tr_dashes = 0, sky_seen = 0, trick_seen = 0;
    float sky_x = 0.0f, sky_z = 0.0f, sky_travel = 0.0f, trick_start_dist = 0.0f, trick_end_dist = 0.0f;
    float dash_x = 0.0f, dash_z = 0.0f, dash_travel = 0.0f, hike_height = 0.0f;
    int combo_moves_seen = 0, stinger_seen = 0, style_before_reset = -1, prop_frames = 0, dance_seen = 0;
    float moved_before_reset = 0.0f;
    float attack_start_x = 0.0f, attack_start_z = 0.0f, attack_travel = 0.0f;
    int style_switched = 0, snapped = 0;
    int dir_frames = 0, lock_turned = 0, wheel_ok = 0, wheel_stayed = 0;
    int shots_start = 0, hits_start = 0, ei_shots = 0, ei_hits = 0, shotgun_hits = 0, rocket_hits = 0;
    int rocket_start_shots = 0, rocket_start_hits = 0, rocket_shots = 0, walk_shots = 0;
    float walk_x = 0.0f, walk_z = 0.0f, walk_travel = 0.0f;
    float dir_error = 0.0f, walk_speed = 0.0f, run_speed = 0.0f, prev_x = 0.0f, prev_z = 0.0f;
    float start_x, start_z, max_height = 0.0f, time = 0.0f;

    if (argc > 0 && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (slash && (size_t)(slash - argv[0] + 1) < sizeof(game_dir)) {
            memcpy(game_dir, argv[0], (size_t)(slash - argv[0] + 1));
            game_dir[slash - argv[0] + 1] = '\0';
        }
    }

    boot_start = sceKernelGetSystemTimeLow();
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);  /* full speed (homebrew starts at 222 MHz) */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    if (!init_model() || !coat_init() || !init_guns()) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Out of memory loading the Dante model.\n");
        sceKernelSleepThread();
    }
    init_room();
    init_font();
    sceKernelDcacheWritebackAll();
    init_gu();
    rest_error = rest_pose_error_um();
    motions_loaded = motion_init();
    anim_reset(&anim);
    combat_reset(&combat);
    combat_ready = motions_loaded > 0 && combat_init();
    dummy_ready = motions_loaded > 0 && enemy_init();
    if (combat_ready && game_file_exists("AUTOEXIT.TXT")) {   /* autotest only: it slows the boot */
        moves_finished = combat_selftest(&moves_total, &moves_failed, &moves_fallbacks);
        inputs_ok = combat_selftest_inputs(&inputs_total, &inputs_failed);
    }
    combat_reset(&combat);

    {
        /* The Mission 2 street (st001) if STAGE.BIN is next to the EBOOT. */
        char path[320];
        path_in_game_dir(path, sizeof(path), "STAGE.BIN");
        if (stage_load(path)) {
            const StageInfo *si = stage_info();
            player_spawn[0] = si->spawn[0];
            player_spawn[1] = si->spawn[1];
            player_spawn[2] = si->spawn[2];
            player_spawn[3] = si->spawn_yaw;
            player_room_limit = 1.0e9f;
            player_floor_fn = stage_floor;
            camera_auto_follow = 1;  /* st001's only camera zone is a follow zone */
        }
    }
    autotest = game_file_exists("AUTOEXIT.TXT");
    focus = game_file_exists("FOCUS.TXT");
    if (focus) {
        char path[320], text[16] = {0};
        FILE *f;
        path_in_game_dir(path, sizeof(path), "FOCUS.TXT");
        f = fopen(path, "r");
        if (f) { fread(text, 1, sizeof(text) - 1, f); fclose(f); }
        focus_coat = strncmp(text, "coat", 4) == 0;
        focus_plink = strncmp(text, "plink", 5) == 0;
        focus_hud = strncmp(text, "hud", 3) == 0;
        focus_weapons = strncmp(text, "weapons", 7) == 0;
        focus_cmp = strncmp(text, "cmp", 3) == 0;
        if (focus_cmp) cmp_load();
    }
    recording = game_file_exists("RECORD.TXT");
    show_bones = game_file_exists("BONES.TXT");
    if (recording) {
        char path[320], text[16] = {0};
        FILE *f;
        path_in_game_dir(path, sizeof(path), "RECORD.TXT");
        f = fopen(path, "r");
        if (f) { fread(text, 1, sizeof(text) - 1, f); fclose(f); }
        sm_demo = strncmp(text, "swordmaster", 11) == 0;
        styles_demo = strncmp(text, "styles", 6) == 0;
        jumps_demo = strncmp(text, "jumps", 5) == 0;
        dummy_demo = strncmp(text, "dummy", 5) == 0;
        tour = (strncmp(text, "motions", 7) == 0 || strncmp(text, "rebellion", 9) == 0 ||
                strncmp(text, "study", 5) == 0) && motions_loaded > 0;
        if (tour) {
            const char *prefix = text[0] == 'r' ? "rb-" : text[0] == 's' ? "s3" : 0;
            tour_motion = 0;
            if (prefix) {  /* start at the first motion of that set */
                while (tour_motion < motion_count() && strncmp(motion_get(tour_motion)->name, prefix, strlen(prefix)) != 0) ++tour_motion;
            }
            anim.viewer = 1; anim.viewer_motion = tour_motion; anim.viewer_frame = 0.0f;
        }
    }
    if (recording) autotest = 0;
    if (autotest || recording || focus) capture_buffer = memalign(64, FRAMEBUFFER_BYTES);
    player_reset(&player, &camera);
    /* Training dummy in the middle of the street (or ahead of Dante in the grid room). */
    if (dummy_ready) {
        if (stage_loaded()) enemy_spawn(2750.0f, 2450.0f);
        else enemy_spawn(0.0f, 200.0f);
    }
    if (stage_loaded()) {
        /* Starting camera: behind Dante if that's clear, otherwise the
         * clearest nearby angle (st001 spawns in the office doorway, where
         * "behind" is inside the building). */
        float best_yaw = camera.yaw, best_score = -1.0f, offset;
        for (offset = 0.0f; offset < 6.28f; offset += 0.3927f) {
            int side;
            for (side = -1; side <= 1; side += 2) {
                float yaw = player.yaw + offset * (float)side, t = 1.0f, score;
                float a[3] = {player.x, player.y + CAMERA_TARGET_HEIGHT, player.z};
                float b[3] = {player.x - sinf(yaw) * CAMERA_DISTANCE, player.y + CAMERA_HEIGHT,
                              player.z - cosf(yaw) * CAMERA_DISTANCE};
                if (!stage_raycast(a, b, &t)) t = 1.0f;
                score = t - offset * 0.04f;  /* prefer angles near "behind" */
                if (score > best_score) { best_score = score; best_yaw = yaw; }
            }
        }
        camera.yaw = camera.target_yaw = best_yaw;
        camera.x = player.x - sinf(best_yaw) * CAMERA_DISTANCE;
        camera.y = player.y + CAMERA_HEIGHT;
        camera.z = player.z - cosf(best_yaw) * CAMERA_DISTANCE;
    }
    if (dummy_demo && dummy_ready) {
        /* Start 2 m from the dummy, facing it, camera at a three-quarter angle. */
        float e[3];
        enemy_position(e);
        player.x = e[0] + 150.0f; player.z = e[2] + 140.0f;
        player.yaw = atan2f(e[0] - player.x, e[2] - player.z);
        {
            float gy;
            if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy;
        }
        camera.yaw = camera.target_yaw = player.yaw - 0.6f;
        camera.x = player.x - sinf(camera.yaw) * CAMERA_DISTANCE;
        camera.y = player.y + CAMERA_HEIGHT;
        camera.z = player.z - cosf(camera.yaw) * CAMERA_DISTANCE;
        camera.tx = player.x; camera.ty = player.y + CAMERA_TARGET_HEIGHT; camera.tz = player.z;
    }
    if (tour) camera.yaw = 2.5f;  /* front three-quarter view of Dante for the tour */
    else if (recording && !stage_loaded()) {
        /* Start in a corner facing the far one: the combos travel ~5 m. */
        player.x = player.z = -750.0f;
        player.yaw = camera.yaw = 0.785f;
        camera.x = player.x - sinf(camera.yaw) * CAMERA_DISTANCE;
        camera.z = player.z - cosf(camera.yaw) * CAMERA_DISTANCE;
        camera.tx = player.x;
        camera.tz = player.z;
    }
    start_x = player.x;
    start_z = player.z;
    write_memory_report(sceKernelGetSystemTimeLow() - boot_start, motions_loaded);
    /* The real flow, unless a test is running; tests can ask for it with "menu 1". */
    {
        char path[320];
        path_in_game_dir(path, sizeof(path), "MENU.BIN");
        menu_text = menu_text_hook;
        menu_debug = debug_modal;
        orbs_set_counter(&shop_state.red_orbs);
        enemy_died_fn = enemy_died;
        combat_hit_fn = breakable_hit;
        menu_shop = &shop_state;
        if (menu_load(path) && ((!focus && !autotest && !recording && !tour) || cmp_menu)) {
            mission_mode = 1;
            mission_stage = 2;   /* what's loaded */
            mission_choice.mission = 1;
            mission_choice.turbo = 1;
            menu_draw_buffer = draw_buffer;
            {
                /* BOOT_MISSION.TXT next to the EBOOT ("3", or "3 1" for Normal): straight into that mission. For testing. */
                char boot[320];
                FILE *bf;
                int boot_mission = 0, boot_difficulty = 0;
                path_in_game_dir(boot, sizeof(boot), "BOOT_MISSION.TXT");
                bf = fopen(boot, "r");
                if (bf) { if (fscanf(bf, "%d %d", &boot_mission, &boot_difficulty) < 1) boot_mission = 0; fclose(bf); }
                if ((boot_mission >= 1 && boot_mission <= MISSION_COUNT) || boot_mission == MENU_DEBUG_ROOM) {   /* 99: the Debug Room */
                    mission_choice.mission = boot_mission;
                    mission_choice.difficulty = boot_difficulty & 1;
                } else {
                    play_movie("MOVIES/OPENING.MOV");
                    menu_run(&mission_choice, cmp_menu ? menu_script_pad : NULL, cmp_menu ? menu_test_shot : NULL);
                    play_intro(mission_choice.mission);
                }
            }
            start_mission(&mission_choice, &player, &camera);
            draw_buffer = menu_draw_buffer;
        }
    }
    last_time = sceKernelGetSystemTimeLow();

    /* Scenes that measure things compare with normalised numbers. */
    if (((focus && !(focus_cmp && cmp_turbo)) || autotest || recording || tour) && !cmp_menu) game_turbo = 0;
    while (running) {
        unsigned int frame_start = sceKernelGetSystemTimeLow();
        unsigned int frame_us, skin_start, skin_us, gap = frame_start - last_time;
        float dt;
        char line0[64], line1[64];
        const char *hud[HUD_LINES];
        int bones_now = show_bones || (autotest && frames == 26);

        debug_pressed = 0;
        debug_held = 0;
        /* Keep the PSP awake and the screen on during long unattended runs. */
        if (frames % 300 == 0) scePowerTick(PSP_POWER_TICK_ALL);
        if (tour) {
            dt = 1.0f / 60.0f;
            memset(&input, 0, sizeof(input));
            input.style = -1;
        } else if (recording) {
            dt = 1.0f / 60.0f;
            if (dummy_demo) dummy_record_pad(frames, &input);
            else if (jumps_demo) jumps_record_pad(frames, &input);
            else if (styles_demo) styles_record_pad(frames, &input);
            else if (sm_demo) sm_record_pad(frames, &input);
            else record_pad(frames, &input);
            if (pad_aim) aim_stick(&input, &player, pad_aim);
            pad_aim = 0;
        } else if (focus) {
            dt = focus_cmp ? 1.0f / (float)cmp_fps : 1.0f / 60.0f;
            if (cmp_realdt) {
                dt = (float)(frame_start - last_time) / 1000000.0f;
                if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
            }
            cmp_px = player.x; cmp_pz = player.z; cmp_cam_yaw = camera.yaw;
            focus_pad(frames, &input);
        } else if (autotest) {
            dt = 1.0f / 60.0f;
            scripted_pad(frames, &input);
            if (frames >= 2090 && frames < 2096) aim_stick(&input, &player, 1);  /* R + fwd(target) + CIR */
            scripted_combat(frames, &input);
        } else {
            dt = (float)(frame_start - last_time) / 1000000.0f;
            if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
            read_pad(&input, &previous_buttons, &previous_l);
        }
        last_time = frame_start;
        {
            int was_open = debug_open || pause_active;
            {
                int k;
                for (k = 0; k < game_key_count; ++k) if (game_keys[k].frame == frames) { debug_pressed |= game_keys[k].button; debug_held |= game_keys[k].button; }
            }
            int toggle_viewer = 0;
            if ((debug_held & DEBUG_COMBO) == DEBUG_COMBO && (debug_pressed & DEBUG_COMBO) && !debug_open) {
                /* START and SELECT together: the debug menu, whatever's open. */
                pause_active = 0;
                debug_open = 1;
                debug_cursor = 0;
            } else if (pause_active) {
                PauseAction action = pause_update(debug_pressed);
                if (action == PAUSE_RESUME) pause_active = 0;
                else if (action == PAUSE_OPTIONS) { pause_active = 0; debug_open = 1; debug_cursor = 0; }
                else if (action == PAUSE_RESTART) { pause_active = 0; start_mission(&mission_choice, &player, &camera); }
                else if (action == PAUSE_QUIT) {
                    pause_active = 0;
                    menu_draw_buffer = draw_buffer;
                    menu_run_from_mission_select(&mission_choice, NULL, NULL);
                    play_intro(mission_choice.mission);
                    start_mission(&mission_choice, &player, &camera);
                    draw_buffer = menu_draw_buffer;
                    last_time = sceKernelGetSystemTimeLow();
                }
            } else if (mission_mode && !debug_open && (debug_pressed & PSP_CTRL_START)) {
                pause_open();
                pause_active = 1;
            } else {
                toggle_viewer = debug_menu_update(debug_pressed);
            }
            if (was_open || debug_open || pause_active) {
                /* Nothing reaches the game, including the press that closes the menu. */
                memset(&input, 0, sizeof(input));
                input.style = -1;
                input.toggle_viewer = toggle_viewer;
                dt = 0.0f;
            } else if (game_turbo) {
                dt *= TURBO_SPEED;
            }
            player_step_scale = game_turbo ? TURBO_SPEED : 1.0f;
        }
        time += dt;

        if (input.toggle_viewer && motions_loaded > 0) {
            anim.viewer = !anim.viewer;
            anim.viewer_frame = 0.0f;
            coat_reset();
        }
        if (anim.viewer) {
            /* Viewer: d-pad left/right steps through motions; Dante stands still. */
            if (input.style == STYLE_SWORDMASTER || input.style == STYLE_GUNSLINGER) {
                int n = motion_count();
                anim.viewer_motion = (anim.viewer_motion + (input.style == STYLE_GUNSLINGER ? 1 : n - 1)) % n;
                anim.viewer_frame = 0.0f;
            }
            input.stick_x = input.stick_y = 0.0f;
            input.style = -1;
            input.jump = 0;
        }
        if (debug_quit) {
            /* The debug menu's Quit to menu: back to Mission Select. */
            debug_quit = 0;
            menu_draw_buffer = draw_buffer;
            menu_run_from_mission_select(&mission_choice, NULL, NULL);
            play_intro(mission_choice.mission);
            start_mission(&mission_choice, &player, &camera);
            draw_buffer = menu_draw_buffer;
            last_time = sceKernelGetSystemTimeLow();
        }
        if (dummy_ready && !mission_mode) enemy_spawn_height(player.y);
        if (mission_mode && dummy_ready && !pause_active && !debug_open) {
            if (cmp_kill_at && (frames % cmp_kill_at) == cmp_kill_at - 1) enemy_kill_all();
            {
                int g, k;
                for (g = 0; g < cmp_place_count; ++g) {
                    if (frames != cmp_place_frame[g]) continue;
                    player.x = cmp_place[g][0]; player.y = cmp_place[g][1]; player.z = cmp_place[g][2]; player.yaw = cmp_place[g][3] * 0.0174533f;
                    if (cmp_place[g][4] > 0.5f) debug_pressed |= PSP_CTRL_CIRCLE;
                    start_camera(&player, &camera);
                }
                for (g = 0; g < cmp_goto_count; ++g) {
                    if (cmp_goto[g][0] != frames) continue;
                    script_at = missions[mission_choice.mission - 1].scripts[mission_choice.difficulty & 1][mission_room].count;
                    script_wait = 0.0f;
                    enemy_kill_all();
                    enemy_set_count(0, player.x, player.z);
                    if (cmp_goto[g][1] < 0) {
                        /* "gotoroom frame -1": to the mission's goal item, and press O. */
                        const MissionInfo *gi = &missions[mission_choice.mission - 1];
                        rooms_cleared |= 1ull << mission_room;
                        player.x = gi->goal[0]; player.y = gi->goal[1] + 10.0f; player.z = gi->goal[2];
                        debug_pressed |= PSP_CTRL_CIRCLE;
                    }
                    for (k = 0; k < rooms[mission_room].door_count; ++k) {
                        const RoomDoor *d = &rooms[mission_room].doors[k];
                        if (d->number == cmp_goto[g][1]) {
                            player.x = d->box[0];
                            player.y = d->box[1] + 10.0f;
                            player.z = d->box[2];
                            doors_armed = 1;
                            debug_pressed |= PSP_CTRL_CIRCLE;
                        }
                    }
                }
            }
            if (shop_closed) { shop_closed = 0; last_time = sceKernelGetSystemTimeLow(); }
            if (mission_update(dt, debug_pressed, &player, &camera)) {
                menu_draw_buffer = draw_buffer;
                menu_mission_clear(cmp_menu ? menu_script_pad : NULL, cmp_menu ? menu_clear_shot : NULL);
                menu_run_from_mission_select(&mission_choice, cmp_menu ? menu_script_pad : NULL, cmp_menu ? menu_test_shot : NULL);
                play_intro(mission_choice.mission);
                start_mission(&mission_choice, &player, &camera);
                draw_buffer = menu_draw_buffer;
                last_time = sceKernelGetSystemTimeLow();
            }
        }
        if (dummy_ready && debug_open && !pause_active) {
            if (!mission_mode) enemy_set_count(dummy_count, player.x, player.z);
            enemy_set_attacks(dummy_attacks);
            enemy_set_anim_auto(dummy_anim_auto);
        }
        /* Lock-on target: the nearest dummy, if it's within 25 m. */
        player.has_target = 0;
        if (dummy_ready) enemy_select_nearest(player.x, player.z);
        if (dummy_ready && enemy_active()) {
            float e[3], dx, dz;
            enemy_position(e);
            dx = e[0] - player.x; dz = e[2] - player.z;
            if (dx * dx + dz * dz < 2500.0f * 2500.0f) {
                player.has_target = 1;
                player.target_x = e[0];
                player.target_y = e[1];
                player.target_z = e[2];
            }
            /* Enemy Step's reach: the real test is against the enemy's collision spheres. */
            player.enemy_close = dx * dx + dz * dz < 170.0f * 170.0f && fabsf(e[1] + 100.0f - player.y) < 250.0f;
        } else {
            player.enemy_close = 0;
        }
        apply_weapon_wheel(&input, &player);
        /* Where he was before anything moved him this frame: combat (Trickster's dash, Stinger) and the
         * stick both move him, and the walls are swept from here. */
        float sweep_x = player.x, sweep_z = player.z;
        if (combat_ready && !anim.viewer) {
            MoveId before = combat.move;
            /* Devil Trigger runs him 1.1x as fast (HD Collection traces). */
            combat_update(&combat, &player, &input, player.devil ? dt * 1.1f : dt);
            if ((autotest || recording) && (combat.move != before || input.melee || input.style_action)) {
                /* COMBAT_TRACE.TXT: every move change, to check gate timings. */
                static FILE *trace;
                if (!trace) {
                    char path[320];
                    path_in_game_dir(path, sizeof(path), "COMBAT_TRACE.TXT");
                    trace = fopen(path, "w");
                }
                if (trace) {
                    fprintf(trace, "%d %s action=%d frame=%d hits=%d lock=%d dir=%d yaw=%d cam=%d\n", frames,
                            combat.move == MOVE_NONE ? "-" : combat_move_name(combat.move), combat.action,
                            (int)combat.frame, combat.hits_dealt, player.locked_on, combat.last_dir,
                            (int)(player.yaw * 57.3f), (int)(player.camera_yaw * 57.3f));
                    fflush(trace);
                }
            }
            if (combat.move != before && combat.move != MOVE_NONE) {
                if (combat.move <= MOVE_COMBO1_3) ++combo_moves_seen;
                if (combat.move == MOVE_STINGER_DASH) {
                    stinger_seen = 1;
                    attack_start_x = player.x;
                    attack_start_z = player.z;
                }
            }
            if (combat.move >= MOVE_STINGER_DASH && combat.move <= MOVE_STINGER_THRUST) {
                float ax = player.x - attack_start_x, az = player.z - attack_start_z;
                attack_travel = sqrtf(ax * ax + az * az);
            }
        }
        prev_x = player.x;
        prev_z = player.z;
        {
            float was_x = sweep_x, was_z = sweep_z;
            player_update(&player, &camera, &input, player.devil ? dt * 1.1f : dt);
            /* Walls along the whole way he moved this frame, not just where he ended: a dash or a long frame
             * covers more than his radius, and pushing out from the end alone lets him through a wall (Mission
             * 3's bar, its east wall at x 3000, during Trickster's dash). Steps of at most 15 cm, from where
             * he was before combat moved him. */
            player.wall_contact = 0;
            if (stage_loaded()) {
                float mx = player.x - was_x, mz = player.z - was_z, cx = was_x, cz = was_z;
                int steps = (int)(sqrtf(mx * mx + mz * mz) / 15.0f) + 1, s;
                for (s = 1; s <= steps; ++s) {
                    cx += mx / (float)steps; cz += mz / (float)steps;
                    if (stage_collide(&cx, &cz, player.y, PLAYER_RADIUS)) player.wall_contact = 1;
                }
                player.x = cx; player.z = cz;
            }
        }
        if (dummy_ready) {
            float from_x, from_z;
            int heavy;
            {
                unsigned int ai_start = sceKernelGetSystemTimeLow();
                enemy_update(player.quicksilver ? dt * 0.1f : dt, player.x, player.y, player.z);
                orbs_update(dt, player.x, player.y, player.z);
                bench_ai_us = sceKernelGetSystemTimeLow() - ai_start;
            }
            if (enemy_attack_landed(&from_x, &from_z, &heavy)) {
                float chest[3] = {player.x, player.y + 110.0f, player.z};
                if (combat_player_hit(&combat, &player, from_x, from_z, heavy)) fx_hit_spark(chest);
                else fx_guard_flash(chest);
            }
            enemy_push_out(&player.x, &player.z, PLAYER_RADIUS);
        }
        if (input.style >= 0) style_switched = 1;
        if (autotest && frames >= MOVE_TEST_START && frames < 2440 && !player.wall_contact && player.on_ground) {
            /* Dante's actual travel against the stick's on-screen direction. */
            float mx = player.x - prev_x, mz = player.z - prev_z;
            if (mx * mx + mz * mz > 0.25f) {
                float sf = sinf(camera.yaw), cf = cosf(camera.yaw);
                float wx = sf * input.stick_y - cf * input.stick_x, wz = cf * input.stick_y + sf * input.stick_x;
                float e = fabsf(atan2f(mx * wz - mz * wx, mx * wx + mz * wz));
                if (e > dir_error) dir_error = e;
                ++dir_frames;
            }
        }
        if (autotest && frames == 2495) walk_speed = player.speed;
        if (autotest && frames == 2535) run_speed = player.speed;
        if (autotest && frames == 2605) {
            /* Lock-on with the stick right: Dante faces the way he travels
             * (screen right is yaw - 90 degrees in this convention). */
            float e = player.yaw - (camera.yaw - 1.5708f);
            lock_turned = fabsf(atan2f(sinf(e), cosf(e))) < 0.2f;
        }
        if (player.y > max_height) max_height = player.y;

        skin_start = sceKernelGetSystemTimeLow();
        if (motions_loaded > 0) {
            anim_update(&anim, &player, dt);
            anim_pose(&anim, &player, &combat, &pose, time);
        } else {
            pose_procedural(&pose, &player, time);
        }
        pose_evaluate(&pose);
        if (focus && focus_plink) plink_measure(&pose, frames);
        fx_update(dt);
        fire_pending_shots(&combat, &player, dt);
        if (player.devil && player.melee_weapon < DMC3_DEVIL_COUNT && devil_ready(player.melee_weapon)) {
            const DevilModel *dm = &dmc3_devils[player.melee_weapon];
            skin_mesh(&pose, dm->positions, dm->normals, dm->rest, dm->bones, dm->weights, dm->vertex_count, devil_ready(player.melee_weapon)->vertices);
        } else {
            skin_model(&pose);
        }
        coat_update(&pose, &player, dt);
        {
            float chest[3] = {player.x, player.y + 110.0f, player.z};
            if (input.devil) fx_devil_burst(chest);
            if (player.devil) fx_devil_aura(chest);
        }
        update_move_fx(&combat, &player, dt);
        doppel_trail[doppel_head][0] = player.x; doppel_trail[doppel_head][1] = player.y;
        doppel_trail[doppel_head][2] = player.z; doppel_trail[doppel_head][3] = player.yaw;
        doppel_head = (doppel_head + 1) % DOPPEL_FRAMES;
        skin_us = sceKernelGetSystemTimeLow() - skin_start;
        if (skin_us > skin_us_max) skin_us_max = skin_us;

        snprintf(line0, sizeof(line0), "%-11s  frame %2u.%u ms  pos %5d %5d",
                 player_style_name(player.style), last_frame_us / 1000, (last_frame_us % 1000) / 100,
                 (int)player.x, (int)player.z);
        if (anim.viewer) {
            const Motion *m = motion_get(anim.viewer_motion);
            snprintf(line0, sizeof(line0), "MOTION VIEWER  #%-2d  %s   frame %3d / %d",
                     anim.viewer_motion, m->name, (int)anim.viewer_frame, (int)m->end_frame);
        }
        snprintf(line1, sizeof(line1), "%s%s%s%s%s",
                 combat.move != MOVE_NONE ? combat_move_name(combat.move) : "",
                 combat.move != MOVE_NONE && player.action_timer > 0.0f ? "  " : "",
                 player.locked_on ? (player.has_target ? " [LOCK-ON: DUMMY] " : " [LOCK-ON: no target] ") : "",
                 "",
                 player.action_timer > 0.0f ? player_action_name(player.last_action) : "");
        hud[0] = anim.viewer ? line0 : NULL;
        hud[1] = NULL;
        hud[2] = anim.viewer ? "dpad left/right: previous/next motion" : NULL;
        hud[3] = anim.viewer ? "START: back to play" : NULL;
        if (player.weapon_wheel) hud[2] = hud[3] = NULL;  /* the wheel uses that space */
        if (focus && focus_weapons) {
            /* A tripod three-quarters behind him, close: back mounts and hands both show. */
            int t = frames % WEAPONS_STEP;
            float yaw = player.yaw + (t >= 25 && t < 60 ? 1.8f : t >= 75 ? 2.4f : 0.7f);
            camera.x = player.x - sinf(yaw) * 170.0f;
            camera.y = player.y + 130.0f;
            camera.z = player.z - cosf(yaw) * 170.0f;
            camera.tx = player.x; camera.ty = player.y + 95.0f; camera.tz = player.z;
        }
        if (focus && focus_coat) {
            /* The dummy would only get in the way; a clean view of the coat. */
            if (frames == 1) enemy_spawn(player.x + 3000.0f, player.z);
            focus_coat_camera(&player, &camera, frames);
            hud[0] = hud[1] = hud[2] = hud[3] = NULL;
        }
        render_scene(&player, &camera, hud, bones_now);

        frame_us = sceKernelGetSystemTimeLow() - frame_start;
        last_frame_us = frame_us;
        if (!autotest && !recording && !tour && frames > 60) {
            perf_add(gap, skin_start - frame_start, skin_us);
            bench_add(gap, skin_start - frame_start, skin_us);
            perf_second_us += gap;
            if (perf_second_us >= 1000000u && perf_count < PERF_SECONDS) {
                perf_second_us = 0;
                ++perf_count;
            }
        }
        frame_us_total += frame_us;
        if (frame_us > frame_us_max) frame_us_max = frame_us;
        {
            /* Frames actually shown, counted over half a second. */
            static unsigned int fps_start;
            static int fps_frames;
            unsigned int elapsed = frame_start - fps_start;
            ++fps_frames;
            if (elapsed >= 500000u) {
                snprintf(fps_line, sizeof(fps_line), "%d FPS", (int)(((unsigned long long)fps_frames * 1000000u + elapsed / 2) / elapsed));
                fps_start = frame_start;
                fps_frames = 0;
            }
        }

        if (tour && !tour_step()) tour_done = 1;
        if (recording) {
            /* Every other frame -> 30 fps video. */
            if ((frames & 1) == 0) {
                char name[32];
                snprintf(name, sizeof(name), "REC%04d.BMP", frames / 2);
                capture_frame(draw_buffer, name);
            }
            if ((!tour && frames + 1 >= (dummy_demo ? DUMMY_RECORD_FRAMES : jumps_demo ? JUMPS_RECORD_FRAMES : styles_demo ? STYLES_RECORD_FRAMES : sm_demo ? SM_RECORD_FRAMES : RECORD_FRAMES)) || tour_done) {
                char path[320];
                FILE *done;
                path_in_game_dir(path, sizeof(path), "RECORD_DONE.TXT");
                done = fopen(path, "w");
                if (done) { fprintf(done, "frames=%d\n", frames / 2); fclose(done); }
                running = 0;
            }
        }
        if (focus && focus_cmp) {
            if (frames == 1 && !cmp_menu) { float gy; player.x = (float)cmp_spot_x; player.z = (float)cmp_spot_z; if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy; player.yaw = cmp_face_yaw != 9999 ? (float)cmp_face_yaw * 0.0174533f : -1.985f; coat_reset();
                /* The HD Collection test spot (tools/hd_ref.py), so the same stick goes the same way. */
                camera.x = player.x - sinf((float)cmp_camera_yaw * 0.0174533f) * 470.0f; camera.z = player.z - cosf((float)cmp_camera_yaw * 0.0174533f) * 470.0f; camera.y = player.y + 255.0f;
                camera.tx = player.x; camera.ty = player.y + 140.0f; camera.tz = player.z; camera.last_x = player.x; camera.last_z = player.z; camera.lead_x = camera.lead_z = 0.0f;
                enemy_spawn_height(player.y); enemy_spawn(player.x + sinf(player.yaw) * (float)cmp_enemy_distance, player.z + cosf(player.yaw) * (float)cmp_enemy_distance);
                if (bench_trace) enemy_trace_start();
                enemy_set_attacks(bench_attack);
                if (cmp_lineup) {
                    /* One of each kind in a row across the view, for pictures of the models. */
                    int k;
                    float fx = sinf(player.yaw), fz = cosf(player.yaw), d = (float)cmp_enemy_distance;
                    enemy_set_count(0, 0.0f, 0.0f);
                    for (k = 0; k < ENEMY_KIND_TOTAL; ++k) {
                        float side = ((float)k - (ENEMY_KIND_TOTAL - 1) * 0.5f) * 150.0f;
                        enemy_add_kind(k, player.x + fx * d + fz * side, player.z + fz * d - fx * side);
                    }
                }
                if (cmp_melee >= 0) player.melee_weapon = cmp_melee; if (cmp_gun >= 0) player.gun_weapon = cmp_gun; }
            {
                static char cmp_name[32];
                int shot;
                for (shot = 0; shot < cmp_shot_count; ++shot)
                    if (frames == cmp_game_shots[shot]) { snprintf(cmp_name, sizeof(cmp_name), "SHOT_%d.BMP", frames); capture_frame(draw_buffer, cmp_name); }
                    else if (frames == cmp_shots[shot]) { snprintf(cmp_name, sizeof(cmp_name), "FOCUS_C%d.BMP", frames); focus_pending = cmp_name; }
            }
            if (bench_step_frames > 0 && !bench_done) {
                int into = (frames - 2) % bench_step_frames;
                if (frames == 2) {
                    enemy_set_count(0, player.x, player.z);
                    enemy_set_attacks(bench_attack);
                    enemy_set_anim_auto(bench_anim);
                    enemy_set_test(bench_no_texture, bench_no_lighting, (float)bench_ring);
                    enemy_set_ai_only(bench_ai_only);
                    if (bench_trace) enemy_trace_start();
                    bench_steps[0].dummies = bench_start;
                    enemy_set_count(bench_start, player.x, player.z);
                }
                {
                    int h;
                    for (h = 0; h < cmp_hit_count; ++h) {
                        if (cmp_hits[h][0] == frames) { enemy_select(0); enemy_hit((HitKind)cmp_hits[h][1], player.x, player.z); }
                    }
                }
                if (bench_quicksilver) player.quicksilver = 1;
                if (bench_devil) player.devil = 1;
                if (frames >= 2 && into == bench_step_frames / 2) {
                    bench_measuring = 1;
                    bench_began = sceKernelGetSystemTimeLow();
                } else if (frames > 2 && into == 0) {
                    BenchStep *b = &bench_steps[bench_count];
                    float fps;
                    bench_measuring = 0;
                    b->elapsed = sceKernelGetSystemTimeLow() - bench_began;
                    fps = b->elapsed ? (float)b->frames * 1000000.0f / (float)b->elapsed : 0.0f;
                    bench_low_steps = fps < BENCH_MIN_FPS ? bench_low_steps + 1 : 0;
                    ++bench_count;
                    if (bench_low_steps >= BENCH_LOW_STEPS || b->dummies >= bench_max || bench_count > ENEMY_MAX) {
                        bench_done = 1;
                    } else {
                        bench_steps[bench_count].dummies = b->dummies + 1;
                        enemy_set_count(b->dummies + 1, player.x, player.z);
                        /* Out of memory for another one: that's the limit. */
                        if (enemy_count() < b->dummies + 1) bench_done = 1;
                    }
                }
                cmp_frames = bench_done ? frames : frames + 1;
            }
            if (frames < CMP_MAX_FRAMES) {
                cmp_log[frames].x = player.x; cmp_log[frames].y = player.y; cmp_log[frames].z = player.z;
                cmp_log[frames].yaw = player.yaw; cmp_log[frames].move = (short)combat.move; cmp_log[frames].script = (short)combat.script;
                cmp_log[frames].dt = dt; cmp_log[frames].ground = (short)player.on_ground; cmp_log[frames].cframe = combat.frame;
                {
                    float g = -99999.0f;
                    if (!player_floor_fn || !player_floor_fn(player.x, player.y + 45.0f, player.z, 0.0f, &g)) g = -99999.0f;
                    cmp_log[frames].floor = g;
                }
                cmp_log[frames].room = (short)(mission_mode && mission_room != ROOM_NONE ? atoi(rooms[mission_room].file + 5) : -1);
            }
            if (frames >= cmp_frames) {
                char path[320];
                FILE *report;
                int i;
                if (bench_step_frames > 0) {
                    path_in_game_dir(path, sizeof(path), "BENCH_OUT.TXT");
                    report = fopen(path, "w");
                    if (report) {
                        for (i = 0; i < bench_count; ++i) {
                            const BenchStep *b = &bench_steps[i];
                            unsigned int n = b->frames ? b->frames : 1;
                            fprintf(report, "%d %u %u %u %u %u %u %u %u %u %u\n", b->dummies, b->frames, b->late, b->gap_max, b->elapsed,
                                    b->update / n, b->skin / n, b->draw / n, b->gpu / n, (unsigned int)enemy_poses_done(), b->ai / n);
                        }
                        fclose(report);
                    }
                }
                if (bench_trace) {
                    path_in_game_dir(path, sizeof(path), "AI_TRACE.TXT");
                    enemy_trace_write(path);
                }
                path_in_game_dir(path, sizeof(path), "CMP_OUT.TXT");
                report = fopen(path, "w");
                if (report) {
                    for (i = 0; i < cmp_frames && i < CMP_MAX_FRAMES; ++i)
                        fprintf(report, "%d %.3f %.3f %.3f %.4f %s %s %.4f %d %d %.1f %.1f\n", i, cmp_log[i].x, cmp_log[i].y, cmp_log[i].z, cmp_log[i].yaw, combat_script_key(cmp_log[i].script),
                                cmp_log[i].move != MOVE_NONE ? combat_move_name(cmp_log[i].move) : "-", cmp_log[i].dt, cmp_log[i].ground, cmp_log[i].room, cmp_log[i].floor, cmp_log[i].cframe);
                    fclose(report);
                }
                path_in_game_dir(path, sizeof(path), "FOCUS_DONE.TXT");
                report = fopen(path, "w");
                if (report) { fprintf(report, "cmp done, frames=%d enemy_poses=%d\n", cmp_frames, enemy_poses_done()); fclose(report); }
                running = 0;
            }
        } else
        if (focus && focus_weapons) {
            static char shot_name[32];
            static const char *views[3] = {"SHEATH", "SWING", "GUN"};
            int step = frames / WEAPONS_STEP, t = frames % WEAPONS_STEP;
            int view = t == 18 ? 0 : t == 38 ? 1 : t == 95 ? 2 : -1;
            if (frames == 2) { float gy; player.x = 2750.0f; player.z = 2450.0f; if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy; coat_reset(); enemy_spawn(player.x + sinf(player.yaw) * 500.0f, player.z + cosf(player.yaw) * 500.0f); }
            if (view >= 0) { snprintf(shot_name, sizeof(shot_name), "FOCUS_W%d_%s.BMP", step, views[view]); focus_pending = shot_name; }
            if (frames == WEAPONS_FRAMES) {
                char path[320];
                FILE *report;
                path_in_game_dir(path, sizeof(path), "FOCUS_DONE.TXT");
                report = fopen(path, "w");
                if (report) { fprintf(report, "weapons scene done, max_frame_us=%u\n", frame_us_max); fclose(report); }
                running = 0;
            }
        } else
        if (focus && focus_coat) {
            unsigned int shot;
            for (shot = 0; shot < sizeof(coat_shots) / sizeof(coat_shots[0]); ++shot)
                if (frames == coat_shots[shot].frame) focus_pending = coat_shots[shot].name;
            if (frames == 2) {
                /* The open street. */
                float gy;
                player.x = 2750.0f; player.z = 2450.0f;
                if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy;
                coat_reset();   /* a teleport would fling the coat */
            }
            if (frames == COAT_FRAMES) {
                char path[320];
                FILE *report;
                path_in_game_dir(path, sizeof(path), "FOCUS_DONE.TXT");
                report = fopen(path, "w");
                if (report) { fprintf(report, "coat scene done, max_frame_us=%u\n", frame_us_max); fclose(report); }
                running = 0;
            }
        } else if (focus) {
            static float leg_x, leg_z, leg_dist[4], leg_side[4], leg_fwd[4];
            static int leg_rows[4];
            if (frames == 2 && dummy_ready) {
                /* The open street, the dummy 4 m ahead. */
                float gy;
                player.x = 2750.0f; player.z = 2450.0f;
                if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy;
                enemy_spawn(player.x + sinf(player.yaw) * 400.0f, player.z + cosf(player.yaw) * 400.0f);
                camera.yaw = camera.target_yaw = player.yaw;
                camera.x = player.x - sinf(camera.yaw) * CAMERA_DISTANCE;
                camera.y = player.y + CAMERA_HEIGHT;
                camera.z = player.z - cosf(camera.yaw) * CAMERA_DISTANCE;
                camera.tx = player.x; camera.ty = player.y + CAMERA_TARGET_HEIGHT; camera.tz = player.z;
                player.gun_weapon = 5;
            }
            if (focus_hud) {
                if (frames == 30) focus_pending = "FOCUS_HUD_PLAIN.BMP";
                if (frames == 70) focus_pending = "FOCUS_HUD_LOCK.BMP";
                if (frames == 90) {
                    char path[320];
                    FILE *report;
                    path_in_game_dir(path, sizeof(path), "FOCUS_DONE.TXT");
                    report = fopen(path, "w");
                    if (report) { fprintf(report, "hud scene done, max_frame_us=%u\n", frame_us_max); fclose(report); }
                    running = 0;
                }
            } else
            if (frames >= 20 && (frames - 20) % FOCUS_LEG == 0 && (frames - 20) / FOCUS_LEG <= 4) {
                int leg = (frames - 20) / FOCUS_LEG - 1;
                if (leg >= 0) {
                    /* Travel in the frame of the line to the dummy: + side = Dante's right. */
                    float mx = player.x - leg_x, mz = player.z - leg_z, s = sinf(player.yaw), co = cosf(player.yaw);
                    leg_dist[leg] = sqrtf(mx * mx + mz * mz);
                    leg_fwd[leg] = mx * s + mz * co;
                    leg_side[leg] = mx * -co + mz * s;
                }
                leg_x = player.x; leg_z = player.z;
            }
            if (!focus_hud && frames >= 20 && (frames - 20) / FOCUS_LEG < 4 && (frames - 20) % FOCUS_LEG == 30) {
                static const char *shots[4] = {"FOCUS_RIGHT.BMP", "FOCUS_LEFT.BMP", "FOCUS_BACK.BMP", "FOCUS_FWD.BMP"};
                leg_rows[(frames - 20) / FOCUS_LEG] = combat.lower_motion;
                focus_pending = shots[(frames - 20) / FOCUS_LEG];
            }
            if (frames == FOCUS_FRAMES - 1) {
                char path[320];
                FILE *report;
                int i;
                static const char *names[4] = {"right", "left", "back", "forward"};
                path_in_game_dir(path, sizeof(path), "FOCUS_DONE.TXT");
                report = fopen(path, "w");
                if (report) {
                    for (i = 0; i < 4; ++i)
                        fprintf(report, "%-8s legs=%s travel=%d side=%d forward=%d\n", names[i],
                                leg_rows[i] >= 0 ? motion_get(leg_rows[i])->name : "(none)", (int)leg_dist[i], (int)leg_side[i], (int)leg_fwd[i]);
                    fprintf(report, "shots=%d hits=%d stance=%d shot_count=%d max_frame_us=%u\n", bullets_shots(), bullets_hits(),
                            combat.stance_active, combat.shot_count, frame_us_max);
                    if (focus_plink)
                        fprintf(report, "biggest bone jump: %.1f cm at frame %d (bone %d)\n", plink_max_jump, plink_max_frame, plink_max_bone);
                    fclose(report);
                }
                running = 0;
            }
        }
        if (autotest && frames == 25) capture_frame(draw_buffer, "SHOT_IDLE.BMP");
        if (autotest && frames == 26) capture_frame(draw_buffer, "SHOT_BONES.BMP");
        if (autotest && frames == 100) capture_frame(draw_buffer, "SHOT_RUN.BMP");
        if (autotest && frames == 150) capture_frame(draw_buffer, "SHOT_SIDE.BMP");
        /* The SELECT snap (frame 180) must land on its target; auto follow may
         * keep turning the camera afterwards, so record the landing itself. */
        if (autotest && frames > 180 && frames <= 240 && !camera.snapping &&
            fabsf(camera.yaw - camera.target_yaw) < 0.02f) snapped = 1;
        if (autotest && frames == 227) capture_frame(draw_buffer, "SHOT_JUMP.BMP");
        /* Locomotion checks are taken before the combat section resets Dante.
         * Distance travelled, not displacement: the camera turning under a held
         * stick curves his path, and the script can bring him back near the start. */
        if (autotest && frames <= 249) {
            float bx = player.x - prev_x, bz = player.z - prev_z;
            moved_before_reset += sqrtf(bx * bx + bz * bz);
            if (frames == 249) style_before_reset = player.style;
        }
        if (autotest && frames == 285) capture_frame(draw_buffer, "SHOT_COMBO.BMP");
        if (autotest && frames == 535) capture_frame(draw_buffer, "SHOT_STINGER.BMP");
        if (autotest && combat.move >= MOVE_SM_PROP && combat.move <= MOVE_SM_SHREDDER_END) prop_frames++;
        if (autotest && combat.move == MOVE_SM_DANCE_START) dance_seen = 1;
        if (autotest && frames == 700) capture_frame(draw_buffer, "SHOT_PROP.BMP");
        if (autotest && frames < 1800 && combat.move == MOVE_TR_DASH) {
            if (!dash_seen) { dash_x = player.x; dash_z = player.z; }
            dash_seen = 1;
            dash_travel = sqrtf((player.x - dash_x) * (player.x - dash_x) + (player.z - dash_z) * (player.z - dash_z));
        }
        if (autotest && frames >= 1100 && frames < 1220 && player.y - player_spawn[1] > hike_height) hike_height = player.y - player_spawn[1];
        if (autotest && player.air_kind == AIR_HIKE) hike_seen = 1;
        if (autotest && combat.move == MOVE_RG_BLOCK) block_frames++;
        if (autotest && frames == 1290) capture_frame(draw_buffer, "SHOT_BLOCK.BMP");
        if (autotest && frames == 1338) { input.reset = 1; player_reset(&player, &camera); }
        if (autotest && frames == 1340 && dummy_ready)
            enemy_spawn(player.x + sinf(player.yaw) * 180.0f, player.z + cosf(player.yaw) * 180.0f);
        if (autotest && frames == 1585) dummy_hits_combo = enemy_hits_taken();
        if (autotest && frames > 1593 && frames < 1730 && enemy_height() > dummy_max_height) dummy_max_height = enemy_height();
        if (autotest && frames == 1375) capture_frame(draw_buffer, "SHOT_DUMMY_HIT.BMP");
        if (autotest && frames == 1640) capture_frame(draw_buffer, "SHOT_DUMMY_AIR.BMP");
        if (autotest && (combat.move == MOVE_ROLL_LEFT || combat.move == MOVE_ROLL_RIGHT)) {
            if (!roll_seen) { roll_x = player.x; roll_z = player.z; }
            roll_seen = 1;
            roll_travel = sqrtf((player.x - roll_x) * (player.x - roll_x) + (player.z - roll_z) * (player.z - roll_z));
        }
        if (autotest && combat.dashes > tr_dashes) tr_dashes = combat.dashes;
        if (autotest && combat.move == MOVE_TR_SKY_STAR) {
            if (!sky_seen) { sky_x = player.x; sky_z = player.z; }
            sky_seen = 1;
            sky_travel = sqrtf((player.x - sky_x) * (player.x - sky_x) + (player.z - sky_z) * (player.z - sky_z));
        }
        if (autotest && (combat.move == MOVE_TR_AIR_TRICK || combat.move == MOVE_TR_AIR_TRICK_AIR)) {
            float tdx = player.x - player.target_x, tdz = player.z - player.target_z;
            if (!trick_seen) trick_start_dist = sqrtf(tdx * tdx + tdz * tdz);
            trick_seen = 1;
            trick_end_dist = sqrtf(tdx * tdx + tdz * tdz);
        }
        if (autotest && frames == MOVE_TEST_START - 8) {
            input.reset = 1;
            player_reset(&player, &camera);
            if (stage_loaded()) {
                /* The dummy was moved next to the spawn at 1340; the street is clear. */
                float gy;
                player.x = 2750.0f; player.z = 2450.0f;
                if (stage_floor(player.x, 2000.0f, player.z, 0.0f, &gy)) player.y = gy;
                camera.x = player.x - sinf(camera.yaw) * CAMERA_DISTANCE;
                camera.y = player.y + CAMERA_HEIGHT;
                camera.z = player.z - cosf(camera.yaw) * CAMERA_DISTANCE;
                camera.tx = player.x; camera.ty = player.y + CAMERA_TARGET_HEIGHT; camera.tz = player.z;
            }
        }
        if (autotest && frames == 2635) capture_frame(draw_buffer, "SHOT_WHEEL.BMP");
        if (autotest && frames == 2640) wheel_stayed = fabsf(player.speed) < 1.0f;
        if (autotest && frames == 2650) {
            /* The dummy 3 m ahead of Dante, in the open street; start with Ebony & Ivory. */
            enemy_spawn(player.x + sinf(player.yaw) * 300.0f, player.z + cosf(player.yaw) * 300.0f);
            player.gun_weapon = 5;
            shots_start = bullets_shots();
            hits_start = bullets_hits();
        }
        if (autotest && frames == 2667) capture_frame(draw_buffer, "SHOT_GUN.BMP");
        if (autotest && frames == 2690) { ei_shots = bullets_shots() - shots_start; ei_hits = bullets_hits() - hits_start; player.gun_weapon = 6; }
        if (autotest && frames == 2725) shotgun_hits = bullets_hits() - hits_start - ei_hits;
        if (autotest && frames == 2850) {
            /* The shotgun's motion is over; a standing dummy again, and Kalina Ann. */
            enemy_spawn(player.x + sinf(player.yaw) * 300.0f, player.z + cosf(player.yaw) * 300.0f);
            player.gun_weapon = 9;
            rocket_start_shots = bullets_shots();
            rocket_start_hits = bullets_hits();
        }
        if (autotest && frames == 2868) capture_frame(draw_buffer, "SHOT_ROCKET.BMP");
        if (autotest && frames == 2910) player.gun_weapon = 5;
        if (autotest && frames == 2924) { walk_x = player.x; walk_z = player.z; walk_shots = bullets_shots(); }
        if (autotest && frames == 2938) capture_frame(draw_buffer, "SHOT_WALKFIRE.BMP");
        if (autotest && frames == 2950) {
            float wx = player.x - walk_x, wz = player.z - walk_z;
            walk_travel = sqrtf(wx * wx + wz * wz);
            walk_shots = bullets_shots() - walk_shots;
        }
        if (autotest && frames == 2895) {
            rocket_shots = bullets_shots() - rocket_start_shots;
            rocket_hits = bullets_hits() - rocket_start_hits;
        }
        if (autotest && frames == 2645) wheel_ok = wheel_stayed && player.melee_weapon == 1 && player.gun_weapon == 6;
        if (autotest && frames == 1050) capture_frame(draw_buffer, "SHOT_DASH.BMP");
        if (autotest && frames == 2010) capture_frame(draw_buffer, "SHOT_SKY_STAR.BMP");
        if (autotest && frames == AUTOTEST_FRAMES - 1) {
            char path[320];
            FILE *report;
            float moved = moved_before_reset;
            int ok = motions_loaded == motion_count() && combat_ready && combo_moves_seen >= 3 &&
                     stinger_seen && attack_travel > 150.0f && prop_frames > 85 && dance_seen && moved > 500.0f &&
                     dash_seen && dash_travel > 150.0f && hike_seen && hike_height > 300.0f && block_frames > 90 &&
                     dummy_ready && dummy_hits_combo >= 3 && dummy_max_height > 150.0f && roll_seen && roll_travel > 150.0f && frame_us_max < 33333u && rest_error < 1000u &&
                     max_height > 200.0f && style_switched && style_before_reset == STYLE_GUNSLINGER && snapped &&
                     tr_dashes == 3 && sky_seen && sky_travel > 300.0f && trick_seen &&
                     (trick_start_dist <= 200.0f || trick_end_dist < trick_start_dist - 100.0f) &&
                     dir_frames > 120 && dir_error < 0.15f && lock_turned &&
                     moves_total > 0 && moves_finished == moves_total && inputs_total > 0 && inputs_ok == inputs_total && wheel_ok &&
                     ei_shots == 2 && ei_hits == 2 && shotgun_hits == 1 && rocket_shots == 1 && rocket_hits == 1 && walk_shots == 3 && walk_travel > 30.0f &&
                     fabsf(walk_speed - PLAYER_WALK_SPEED) < 1.0f && fabsf(run_speed - PLAYER_RUN_SPEED) < 1.0f;
            path_in_game_dir(path, sizeof(path), ok ? "TESTROOM_OK.TXT" : "TESTROOM_FAIL.TXT");
            report = fopen(path, "w");
            if (report) {
                fprintf(report,
                        "%s\nframes=%d\nmoved=%d\nstart=%d,%d\nend=%d,%d\nyaw_mdeg=%d\ncamera_yaw_mdeg=%d\n"
                        "camera_snapped=%d\njump_max_height=%d\nstyle=%s\nrest_pose_error_um=%u\n"
                        "avg_frame_us=%u\nmax_frame_us=%u\nmax_skin_us=%u\nmotions_loaded=%d\n"
                        "combat_ready=%d\ncombo_moves=%d\nstinger=%d\nstinger_travel=%d\n"
                        "prop_frames=%d\ndance=%d\ndash_travel=%d\nair_hike=%d\nhike_height=%d\nblock_frames=%d\n"
                        "dummy=%d\ndummy_combo_hits=%d\ndummy_launch_height=%d\nroll_travel=%d\n"
                        "trickster_dashes=%d\nsky_star_travel=%d\nair_trick=%d\nair_trick_dist=%d,%d\n"
                        "stick_dir_frames=%d\nstick_dir_error_mdeg=%d\nwalk_speed=%d\nrun_speed=%d\nlock_turned=%d\n"
                        "selftest_actions=%d/%d\nselftest_first_failure=%d\nselftest_flag_fallbacks=%d\n"
                        "selftest_inputs=%d/%d\nselftest_input_first_failure=%d\nweapon_wheel=%d\n"
                        "ei_shots=%d\nei_hits=%d\nshotgun_hits=%d\nrocket_shots=%d\nrocket_hits=%d\nwalk_fire_shots=%d\nwalk_fire_travel=%d\n",
                        ok ? "DMC3PSP_TESTROOM_OK" : "DMC3PSP_TESTROOM_FAIL", frames + 1, (int)moved,
                        (int)start_x, (int)start_z, (int)player.x, (int)player.z,
                        (int)(player.yaw * 57295.78f), (int)(camera.yaw * 57295.78f), snapped,
                        (int)max_height, player_style_name((Style)style_before_reset), rest_error,
                        frame_us_total / (unsigned int)(frames + 1), frame_us_max, skin_us_max, motions_loaded,
                        combat_ready, combo_moves_seen, stinger_seen, (int)attack_travel, prop_frames, dance_seen,
                        (int)dash_travel, hike_seen, (int)hike_height, block_frames,
                        dummy_ready, dummy_hits_combo, (int)dummy_max_height, (int)roll_travel,
                        tr_dashes, (int)sky_travel, trick_seen, (int)trick_start_dist, (int)trick_end_dist,
                        dir_frames, (int)(dir_error * 57295.78f), (int)walk_speed, (int)run_speed, lock_turned,
                        moves_finished, moves_total, moves_failed, moves_fallbacks, inputs_ok, inputs_total, inputs_failed, wheel_ok, ei_shots, ei_hits, shotgun_hits, rocket_shots, rocket_hits, walk_shots, (int)walk_travel);
                fclose(report);
            }
            running = 0;
        }

        ++frames;
        sceDisplayWaitVblankStart();
        draw_buffer = sceGuSwapBuffers();
        if (focus_pending) { focus_hold(focus_pending); focus_pending = NULL; }
    }

    /* Only on the way out: a memory stick write mid-game costs frames. */
    perf_write();
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
