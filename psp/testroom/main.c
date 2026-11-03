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
#include <string.h>

#include "model_runtime.h"
#include "player.h"
#include "skeleton.h"
#include "motion.h"
#include "coat.h"
#include "combat.h"
#include "stage.h"
#include "swizzle.h"
#include "enemy.h"
#include "bullets.h"
#include "rebellion_runtime.h"
#include "ebony_runtime.h"
#include "ivory_runtime.h"
#include "shotgun_runtime.h"
#include "artemis_runtime.h"
#include "spiral_runtime.h"
#include "kalina_runtime.h"

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
static ModelVertex *rebellion_vertices;
static unsigned short *rebellion_indices;
static void *rebellion_texture;

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
        size_t bytes = (size_t)t->width * t->height * 4;
        model_textures[i] = memalign(16, bytes);
        if (!model_textures[i]) return 0;
        memcpy(model_textures[i], t->rgba, bytes);
        if (!swizzle_texture32(model_textures[i], t->width, t->height)) return 0;
    }
    return 1;
}

/* Linear blend skinning: v' = sum_k w_k * (R_k (v - rest_k) + P_k). The rest
 * pose has identity rotations, so the inverse bind matrix is a translation. */
static void skin_model(const Pose *p) {
    int i, k;
    for (i = 0; i < DMC3_MODEL_VERTEX_COUNT; ++i) {
        const Dmc3Vec3 *src = &dmc3_model_positions[i];
        const Dmc3Vec3 *nrm = &dmc3_model_normals[i];
        float px = 0.0f, py = 0.0f, pz = 0.0f, nx = 0.0f, ny = 0.0f, nz = 0.0f;
        ModelVertex *dst = &model_vertices[i];
        for (k = 0; k < 3; ++k) {
            int weight = dmc3_model_weights[i][k];
            int bone;
            float w, dx, dy, dz;
            const float (*r)[3];
            if (!weight) continue;
            bone = dmc3_model_bones[i][k];
            w = (float)weight * (1.0f / 31.0f);
            r = p->world_rot[bone].m;
            dx = src->x - dmc3_bone_rest[bone].x;
            dy = src->y - dmc3_bone_rest[bone].y;
            dz = src->z - dmc3_bone_rest[bone].z;
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
    sceKernelDcacheWritebackRange(model_vertices, sizeof(ModelVertex) * DMC3_MODEL_VERTEX_COUNT);
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

static int init_rebellion(void) {
    int i;
    size_t bytes = (size_t)DMC3_REBELLION_TEXTURE_WIDTH * DMC3_REBELLION_TEXTURE_HEIGHT * 4;
    rebellion_vertices = memalign(16, sizeof(ModelVertex) * DMC3_REBELLION_VERTEX_COUNT);
    rebellion_indices = memalign(16, sizeof(unsigned short) * DMC3_REBELLION_INDEX_COUNT);
    rebellion_texture = memalign(16, bytes);
    if (!rebellion_vertices || !rebellion_indices || !rebellion_texture) return 0;
    for (i = 0; i < DMC3_REBELLION_VERTEX_COUNT; ++i) {
        ModelVertex *v = &rebellion_vertices[i];
        v->u = dmc3_rebellion_uvs[i].u; v->v = dmc3_rebellion_uvs[i].v;
        v->nx = dmc3_rebellion_normals[i].x; v->ny = dmc3_rebellion_normals[i].y; v->nz = dmc3_rebellion_normals[i].z;
        v->x = dmc3_rebellion_positions[i].x; v->y = dmc3_rebellion_positions[i].y; v->z = dmc3_rebellion_positions[i].z;
    }
    memcpy(rebellion_indices, dmc3_rebellion_indices, sizeof(unsigned short) * DMC3_REBELLION_INDEX_COUNT);
    memcpy(rebellion_texture, dmc3_rebellion_texture, bytes);
    if (!swizzle_texture32(rebellion_texture, DMC3_REBELLION_TEXTURE_WIDTH, DMC3_REBELLION_TEXTURE_HEIGHT)) return 0;
    return 1;
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
} GunModel;

#define GUN_MODEL(n, N) {dmc3_##n##_positions, dmc3_##n##_normals, dmc3_##n##_uvs, dmc3_##n##_indices, dmc3_##n##_texture, \
                          DMC3_##N##_VERTEX_COUNT, DMC3_##N##_INDEX_COUNT, DMC3_##N##_TEXTURE_WIDTH, DMC3_##N##_TEXTURE_HEIGHT, 0, 0, 0}
enum { GUN_EBONY, GUN_IVORY, GUN_SHOTGUN, GUN_ARTEMIS, GUN_SPIRAL, GUN_KALINA, GUN_MODEL_COUNT };
static GunModel gun_models[GUN_MODEL_COUNT] = {
    GUN_MODEL(ebony, EBONY), GUN_MODEL(ivory, IVORY), GUN_MODEL(shotgun, SHOTGUN),
    GUN_MODEL(artemis, ARTEMIS), GUN_MODEL(spiral, SPIRAL), GUN_MODEL(kalina, KALINA),
};
#define GUN_OUT_SECONDS 0.6f   /* how long a gun stays out after a shot (test room choice) */
static float gun_out_time;

static int init_guns(void) {
    int g, i;
    for (g = 0; g < GUN_MODEL_COUNT; ++g) {
        GunModel *m = &gun_models[g];
        size_t bytes = (size_t)m->width * m->height * 4;
        m->vertices = memalign(16, sizeof(ModelVertex) * m->vertex_count);
        m->indices = memalign(16, sizeof(unsigned short) * m->index_count);
        m->texture = memalign(16, bytes);
        if (!m->vertices || !m->indices || !m->texture) return 0;
        for (i = 0; i < m->vertex_count; ++i) {
            ModelVertex *v = &m->vertices[i];
            v->u = m->uvs[i].u; v->v = m->uvs[i].v;
            v->nx = m->normals[i].x; v->ny = m->normals[i].y; v->nz = m->normals[i].z;
            v->x = m->positions[i].x; v->y = m->positions[i].y; v->z = m->positions[i].z;
        }
        memcpy(m->indices, m->source_indices, sizeof(unsigned short) * m->index_count);
        memcpy(m->texture, m->rgba, bytes);
        if (!swizzle_texture32(m->texture, m->width, m->height)) return 0;
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

/* Sheathed: diagonal across the back, hilt over the right (-X) shoulder,
 * blade pointing down toward the left hip; expressed relative to the chest. */
static void back_mount(float r[3][3], float t[3]) {
    float d[3] = {0.62f, -0.78f, 0.0f};        /* blade (+Z of the sword) */
    float x[3] = {0.78f, 0.62f, 0.0f};         /* guard width (+X of the sword) */
    float y[3] = {d[1] * x[2] - d[2] * x[1], d[2] * x[0] - d[0] * x[2], d[0] * x[1] - d[1] * x[0]};
    int k;
    for (k = 0; k < 3; ++k) { r[k][0] = x[k]; r[k][1] = y[k]; r[k][2] = d[k]; }
    t[0] = -16.0f; t[1] = 16.0f; t[2] = -17.0f;
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

#define HUD_LINES 4
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

/* Call with Dante's model matrix current. */
static void draw_gun(const GunModel *m, int bone) {
    ScePspFMatrix4 matrix;
    float r[3][3], t[3];
    int a, b;
    for (a = 0; a < 3; ++a) {
        for (b = 0; b < 3; ++b) r[a][b] = pose.world_rot[bone].m[a][b];
        t[a] = pose.world_pos[bone][a];
    }
    to_gu_matrix(&matrix, r, t);
    sceGumPushMatrix();
    sceGumMultMatrix(&matrix);
    sceGuTexImage(0, m->width, m->height, m->width, m->texture);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, m->index_count, m->indices, m->vertices);
    sceGumPopMatrix();
}

static void draw_active_gun(const Player *player) {
    if (gun_out_time <= 0.0f) return;
    switch (player->gun_weapon) {
    case 5:
        draw_gun(&gun_models[GUN_EBONY], BONE_L_HAND);
        draw_gun(&gun_models[GUN_IVORY], BONE_R_HAND);
        break;
    case 6: draw_gun(&gun_models[GUN_SHOTGUN], BONE_L_HAND); break;
    case 7: draw_gun(&gun_models[GUN_ARTEMIS], BONE_L_HAND); break;
    case 8: draw_gun(&gun_models[GUN_SPIRAL], BONE_L_HAND); break;
    case 9: draw_gun(&gun_models[GUN_KALINA], BONE_L_HAND); break;
    default: break;
    }
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
static void render_scene(const Player *player, const FollowCamera *camera_state,
                         const char *hud[HUD_LINES], int show_bones) {
    FollowCamera camera_copy = *camera_state;
    const FollowCamera *camera = &camera_copy;
    perf_render_start = sceKernelGetSystemTimeLow();
    if (stage_loaded()) {
        /* Keep the follow camera on Dante's side of walls: pull the eye in to
         * just before the first collision hit along target -> eye. */
        float a[3] = {camera_copy.tx, camera_copy.ty, camera_copy.tz};
        float b[3] = {camera_copy.x, camera_copy.y, camera_copy.z}, t;
        if (stage_raycast(a, b, &t)) {
            /* Blocked: first try the boom raised over Dante (looking down),
             * which keeps the shot instead of shoving the lens into his back. */
            float high[3] = {a[0] + (b[0] - a[0]) * 0.55f, b[1] + 170.0f, a[2] + (b[2] - a[2]) * 0.55f}, th = 1.0f;
            float k = t - 0.08f < 0.15f ? 0.15f : t - 0.08f;
            int high_hit = stage_raycast(a, high, &th);
            if (!high_hit || th > k) {
                float kh = high_hit ? th - 0.08f : 1.0f;
                camera_copy.x = a[0] + (high[0] - a[0]) * kh;
                camera_copy.y = a[1] + (high[1] - a[1]) * kh;
                camera_copy.z = a[2] + (high[2] - a[2]) * kh;
            } else {
                camera_copy.x = a[0] + (b[0] - a[0]) * k;
                camera_copy.y = a[1] + (b[1] - a[1]) * k;
                camera_copy.z = a[2] + (b[2] - a[2]) * k;
            }
        }
    }
    int i;
    int bound_texture = -1;

    sceGuStart(GU_DIRECT, display_list);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuColor(0xffffffff);
    /* Material alpha: without this the lit vertex alpha is 0 and the alpha
     * test discards every Dante pixel. */
    sceGuAmbientColor(0xffffffff);
    sceGuClearColor(CLEAR_COLOR);
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
        stage_draw();
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
    sceGuTexMode(GU_PSM_8888, 0, 0, 1);   /* character textures are swizzled at load */
    for (i = 0; i < DMC3_MODEL_OBJECT_COUNT; ++i) {
        const Dmc3ModelObject *object = &dmc3_model_objects[i];
        if ((int)object->texture != bound_texture) {
            const Dmc3Texture *t = &dmc3_model_textures[object->texture];
            bound_texture = (int)object->texture;
            sceGuTexImage(0, t->width, t->height, t->width, model_textures[bound_texture]);
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
                sceGuTexImage(0, t->width, t->height, t->width, model_textures[bound_texture]);
                sceGuTexFlush();
            }
            sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, (int)count, coat_idx + start, coat);
        }
    }
    {
        /* Rebellion: hand or back mount, on top of Dante's model matrix. */
        ScePspFMatrix4 m;
        float r[3][3], t[3], local_r[3][3], local_t[3];
        int a, b, k;
        if (anim_weapon_in_hand(&anim, player, &combat)) {
            for (a = 0; a < 3; ++a) for (b = 0; b < 3; ++b) r[a][b] = pose.world_rot[WEAPON_HAND_BONE].m[a][b];
            for (k = 0; k < 3; ++k) t[k] = pose.world_pos[WEAPON_HAND_BONE][k];
        } else {
            back_mount(local_r, local_t);
            for (a = 0; a < 3; ++a)
                for (b = 0; b < 3; ++b)
                    r[a][b] = pose.world_rot[WEAPON_BACK_BONE].m[a][0] * local_r[0][b] +
                              pose.world_rot[WEAPON_BACK_BONE].m[a][1] * local_r[1][b] +
                              pose.world_rot[WEAPON_BACK_BONE].m[a][2] * local_r[2][b];
            for (k = 0; k < 3; ++k)
                t[k] = pose.world_pos[WEAPON_BACK_BONE][k] + pose.world_rot[WEAPON_BACK_BONE].m[k][0] * local_t[0] +
                       pose.world_rot[WEAPON_BACK_BONE].m[k][1] * local_t[1] + pose.world_rot[WEAPON_BACK_BONE].m[k][2] * local_t[2];
        }
        to_gu_matrix(&m, r, t);
        sceGumPushMatrix();
        sceGumMultMatrix(&m);
        sceGuTexImage(0, DMC3_REBELLION_TEXTURE_WIDTH, DMC3_REBELLION_TEXTURE_HEIGHT, DMC3_REBELLION_TEXTURE_WIDTH, rebellion_texture);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, MODEL_VTYPE, DMC3_REBELLION_INDEX_COUNT, rebellion_indices, rebellion_vertices);
        sceGumPopMatrix();
        bound_texture = -1;
    }
    draw_active_gun(player);
    enemy_draw();
    bullets_draw();
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);   /* HUD font and floor are linear */
    if (show_bones) draw_bones();

    draw_hud(hud);
    if (player->weapon_wheel) draw_weapon_wheel(player);
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
    if (pressed & PSP_CTRL_UP) input->style = STYLE_TRICKSTER;
    if (pressed & PSP_CTRL_DOWN) input->style = STYLE_ROYALGUARD;
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
    input->snap_camera = (pressed & PSP_CTRL_SELECT) != 0;
    input->toggle_viewer = (pressed & PSP_CTRL_START) != 0;
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

int main(int argc, char *argv[]) {
    Player player;
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
    if (!init_model() || !coat_init() || !init_rebellion() || !init_guns()) {
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
    last_time = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int frame_start = sceKernelGetSystemTimeLow();
        unsigned int frame_us, skin_start, skin_us, gap = frame_start - last_time;
        float dt;
        char line0[64], line1[64];
        const char *hud[HUD_LINES];
        int bones_now = show_bones || (autotest && frames == 26);

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
            dt = 1.0f / 60.0f;
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
        /* Lock-on target: the dummy, if it's within 25 m. */
        player.has_target = 0;
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
        }
        apply_weapon_wheel(&input, &player);
        if (combat_ready && !anim.viewer) {
            MoveId before = combat.move;
            combat_update(&combat, &player, &input, dt);
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
        player_update(&player, &camera, &input, dt);
        player.wall_contact = stage_loaded() && stage_collide(&player.x, &player.z, player.y, PLAYER_RADIUS);
        if (dummy_ready) {
            enemy_update(dt);
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
        fire_pending_shots(&combat, &player, dt);
        if (combat_move_is_gun(combat.move) || combat.stance_active) gun_out_time = GUN_OUT_SECONDS;
        else if (gun_out_time > 0.0f) gun_out_time -= dt;
        skin_model(&pose);
        coat_update(&pose, &player, dt);
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
        hud[0] = line0;
        hud[1] = line1;
        if (dummy_ready && !anim.viewer) {
            static char line2[64];
            snprintf(line2, sizeof(line2), "%s / %s  DUMMY hits %d", player_weapon_name(player.melee_weapon),
                     player_weapon_name(player.gun_weapon), enemy_hits_taken());
            hud[2] = line2;
        } else
        hud[2] = anim.viewer ? "dpad left/right: previous/next motion" : "stick move  dpad style  R lock  L melee  L+[] gun";
        hud[3] = anim.viewer ? "START: back to play" : "X jump /\\ melee [] guns O style SELECT cam";
        if (player.weapon_wheel) hud[2] = hud[3] = NULL;  /* the wheel uses that space */
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
            perf_second_us += gap;
            if (perf_second_us >= 1000000u && perf_count < PERF_SECONDS) {
                perf_second_us = 0;
                ++perf_count;
                if (perf_count % 10 == 0) perf_write();
            }
        }
        frame_us_total += frame_us;
        if (frame_us > frame_us_max) frame_us_max = frame_us;

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
            if (frames >= 20 && (frames - 20) / FOCUS_LEG < 4 && (frames - 20) % FOCUS_LEG == 30) {
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
        if (autotest && combat.move == MOVE_TR_AIR_HIKE) hike_seen = 1;
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

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
