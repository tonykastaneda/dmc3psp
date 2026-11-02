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
#include "rebellion_runtime.h"

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

static void render_scene(const Player *player, const FollowCamera *camera,
                         const char *hud[HUD_LINES], int show_bones) {
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
    sceGumPerspective(60.0f, (float)SCR_WIDTH / (float)SCR_HEIGHT, 10.0f, 6000.0f);
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
    sceGuDepthMask(GU_TRUE);
    sceGumDrawArray(GU_TRIANGLES, COLOR_VTYPE, FLOOR_VERTEX_COUNT, 0, floor_vertices);
    sceGumDrawArray(GU_LINES, COLOR_VTYPE, GRID_VERTEX_COUNT, 0, grid_vertices);
    sceGuDepthMask(GU_FALSE);

    sceGumLoadIdentity();
    {
        ScePspFVector3 position = {player->x, player->y, player->z};
        sceGumTranslate(&position);
        sceGumRotateY(player->yaw + MODEL_YAW_OFFSET);
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
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
    if (show_bones) draw_bones();

    draw_hud(hud);
    sceGuFinish();
    sceGuSync(0, 0);
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
    float value = ((float)raw - 128.0f) / 127.0f;
    const float dead = 0.2f;
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    if (value > -dead && value < dead) return 0.0f;
    return value > 0.0f ? (value - dead) / (1.0f - dead) : (value + dead) / (1.0f - dead);
}

static void read_pad(PlayerInput *input, unsigned int *previous_buttons) {
    SceCtrlData pad;
    unsigned int pressed;
    sceCtrlPeekBufferPositive(&pad, 1);
    pressed = pad.Buttons & ~*previous_buttons;
    *previous_buttons = pad.Buttons;
    memset(input, 0, sizeof(*input));
    input->stick_x = stick_axis(pad.Lx);
    input->stick_y = -stick_axis(pad.Ly);
    input->style = -1;
    if (pressed & PSP_CTRL_UP) input->style = STYLE_TRICKSTER;
    if (pressed & PSP_CTRL_DOWN) input->style = STYLE_ROYALGUARD;
    if (pressed & PSP_CTRL_LEFT) input->style = STYLE_SWORDMASTER;
    if (pressed & PSP_CTRL_RIGHT) input->style = STYLE_GUNSLINGER;
    input->lock_on = (pad.Buttons & PSP_CTRL_RTRIGGER) != 0;
    input->weapon_wheel = (pad.Buttons & PSP_CTRL_LTRIGGER) != 0;
    input->melee = (pressed & PSP_CTRL_TRIANGLE) != 0;
    input->gun = (pressed & PSP_CTRL_SQUARE) != 0;
    input->style_action = (pressed & PSP_CTRL_CIRCLE) != 0;
    input->jump = (pressed & PSP_CTRL_CROSS) != 0;
    input->snap_camera = (pressed & PSP_CTRL_SELECT) != 0;
    input->toggle_viewer = (pressed & PSP_CTRL_START) != 0;
}

/* Scripted input for the automated test (240 frames). */
static void scripted_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    input->style = -1;
    if (frame >= 30 && frame < 120) input->stick_y = 1.0f;          /* run away */
    else if (frame >= 120 && frame < 200) input->stick_x = 1.0f;    /* run right */
    else if (frame >= 200 && frame < 240) input->stick_y = 1.0f;
    if (frame == 50) input->style = STYLE_GUNSLINGER;
    if (frame == 180) input->snap_camera = 1;                       /* SELECT */
    if (frame == 205) input->jump = 1;
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
    if (frame == 320 || frame == 345 || frame == 375) input->melee = 1;       /* Combo II: tap, pause, tap */
    if (frame >= 420 && frame < 640 && (frame - 420) % 6 == 0) input->melee = 1; /* mash: Million Stab */
    if (frame >= 1000 && frame < 1050) input->lock_on = 1;                   /* Stinger */
    if (frame >= 1000 && frame < 1006) input->stick_y = 1.0f;
    if (frame == 1003) input->melee = 1;
    if (frame >= 1150 && frame < 1200) input->lock_on = 1;                   /* High Time */
    if (frame >= 1150 && frame < 1156) input->stick_y = -1.0f;
    if (frame == 1153) input->melee = 1;
    if (frame == 1260) input->jump = 1;                                      /* Aerial Rave */
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

/* ---- Main --------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    Player player;
    FollowCamera camera;
    PlayerInput input;
    void *draw_buffer = (void *)0;
    unsigned int last_time, previous_buttons = 0;
    unsigned int frame_us_total = 0, frame_us_max = 0, last_frame_us = 0, skin_us_max = 0;
    unsigned int rest_error;
    int frames = 0;
    int autotest, recording, show_bones, tour = 0, tour_done = 0, motions_loaded, combat_ready = 0;
    int combo_moves_seen = 0, stinger_seen = 0, style_before_reset = -1;
    float moved_before_reset = 0.0f;
    float attack_start_x = 0.0f, attack_start_z = 0.0f, attack_travel = 0.0f;
    int style_switched = 0, snapped = 0;
    float start_x, start_z, max_height = 0.0f, time = 0.0f;

    if (argc > 0 && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (slash && (size_t)(slash - argv[0] + 1) < sizeof(game_dir)) {
            memcpy(game_dir, argv[0], (size_t)(slash - argv[0] + 1));
            game_dir[slash - argv[0] + 1] = '\0';
        }
    }

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    if (!init_model() || !coat_init() || !init_rebellion()) {
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

    autotest = game_file_exists("AUTOEXIT.TXT");
    recording = game_file_exists("RECORD.TXT");
    show_bones = game_file_exists("BONES.TXT");
    if (recording) {
        char path[320], text[16] = {0};
        FILE *f;
        path_in_game_dir(path, sizeof(path), "RECORD.TXT");
        f = fopen(path, "r");
        if (f) { fread(text, 1, sizeof(text) - 1, f); fclose(f); }
        tour = (strncmp(text, "motions", 7) == 0 || strncmp(text, "rebellion", 9) == 0) && motions_loaded > 0;
        if (tour) {
            tour_motion = 0;
            if (text[0] == 'r') {  /* Rebellion moves only */
                while (tour_motion < motion_count() && strncmp(motion_get(tour_motion)->name, "rb-", 3) != 0) ++tour_motion;
            }
            anim.viewer = 1; anim.viewer_motion = tour_motion; anim.viewer_frame = 0.0f;
        }
    }
    if (recording) autotest = 0;
    if (autotest || recording) capture_buffer = memalign(64, FRAMEBUFFER_BYTES);
    player_reset(&player, &camera);
    if (tour) camera.yaw = 2.5f;  /* front three-quarter view of Dante for the tour */
    else if (recording) {
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
    last_time = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int frame_start = sceKernelGetSystemTimeLow();
        unsigned int frame_us, skin_start;
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
            record_pad(frames, &input);
        } else if (autotest) {
            dt = 1.0f / 60.0f;
            scripted_pad(frames, &input);
            scripted_combat(frames, &input);
        } else {
            dt = (float)(frame_start - last_time) / 1000000.0f;
            if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
            read_pad(&input, &previous_buttons);
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
        if (combat_ready && !anim.viewer) {
            MoveId before = combat.move;
            combat_update(&combat, &player, &input, dt);
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
        player_update(&player, &camera, &input, dt);
        if (input.style >= 0) style_switched = 1;
        if (player.y > max_height) max_height = player.y;

        skin_start = sceKernelGetSystemTimeLow();
        if (motions_loaded > 0) {
            anim_update(&anim, &player, dt);
            anim_pose(&anim, &player, &combat, &pose, time);
        } else {
            pose_procedural(&pose, &player, time);
        }
        pose_evaluate(&pose);
        skin_model(&pose);
        coat_update(&pose, &player, dt);
        if (sceKernelGetSystemTimeLow() - skin_start > skin_us_max) skin_us_max = sceKernelGetSystemTimeLow() - skin_start;

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
                 player.locked_on ? " [LOCK-ON: no target] " : "",
                 player.weapon_wheel ? "[WEAPON WHEEL] " : "",
                 player.action_timer > 0.0f ? player_action_name(player.last_action) : "");
        hud[0] = line0;
        hud[1] = line1;
        hud[2] = anim.viewer ? "dpad left/right: previous/next motion" : "stick move  dpad style  R lock  L weapons";
        hud[3] = anim.viewer ? "START: back to play" : "X jump /\\ melee [] guns O style SELECT cam";
        render_scene(&player, &camera, hud, bones_now);

        frame_us = sceKernelGetSystemTimeLow() - frame_start;
        last_frame_us = frame_us;
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
            if ((!tour && frames + 1 >= RECORD_FRAMES) || tour_done) {
                char path[320];
                FILE *done;
                path_in_game_dir(path, sizeof(path), "RECORD_DONE.TXT");
                done = fopen(path, "w");
                if (done) { fprintf(done, "frames=%d\n", frames / 2); fclose(done); }
                running = 0;
            }
        }
        if (autotest && frames == 25) capture_frame(draw_buffer, "SHOT_IDLE.BMP");
        if (autotest && frames == 26) capture_frame(draw_buffer, "SHOT_BONES.BMP");
        if (autotest && frames == 100) capture_frame(draw_buffer, "SHOT_RUN.BMP");
        if (autotest && frames == 150) capture_frame(draw_buffer, "SHOT_SIDE.BMP");
        if (autotest && frames == 215) snapped = fabsf(camera.yaw - camera.target_yaw) < 0.02f;
        if (autotest && frames == 222) capture_frame(draw_buffer, "SHOT_JUMP.BMP");
        if (autotest && frames == 249) {
            /* Locomotion checks are taken before the combat section resets Dante. */
            float bx = player.x - start_x, bz = player.z - start_z;
            moved_before_reset = sqrtf(bx * bx + bz * bz);
            style_before_reset = player.style;
        }
        if (autotest && frames == 285) capture_frame(draw_buffer, "SHOT_COMBO.BMP");
        if (autotest && frames == 535) capture_frame(draw_buffer, "SHOT_STINGER.BMP");
        if (autotest && frames == 600) {
            char path[320];
            FILE *report;
            float moved = moved_before_reset;
            int ok = motions_loaded == motion_count() && combat_ready && combo_moves_seen == 3 &&
                     stinger_seen && attack_travel > 150.0f && moved > 500.0f && frame_us_max < 33333u && rest_error < 1000u &&
                     max_height > 50.0f && style_switched && style_before_reset == STYLE_GUNSLINGER && snapped;
            path_in_game_dir(path, sizeof(path), ok ? "TESTROOM_OK.TXT" : "TESTROOM_FAIL.TXT");
            report = fopen(path, "w");
            if (report) {
                fprintf(report,
                        "%s\nframes=%d\nmoved=%d\nstart=%d,%d\nend=%d,%d\nyaw_mdeg=%d\ncamera_yaw_mdeg=%d\n"
                        "camera_snapped=%d\njump_max_height=%d\nstyle=%s\nrest_pose_error_um=%u\n"
                        "avg_frame_us=%u\nmax_frame_us=%u\nmax_skin_us=%u\nmotions_loaded=%d\n"
                        "combat_ready=%d\ncombo_moves=%d\nstinger=%d\nstinger_travel=%d\n",
                        ok ? "DMC3PSP_TESTROOM_OK" : "DMC3PSP_TESTROOM_FAIL", frames + 1, (int)moved,
                        (int)start_x, (int)start_z, (int)player.x, (int)player.z,
                        (int)(player.yaw * 57295.78f), (int)(camera.yaw * 57295.78f), snapped,
                        (int)max_height, player_style_name((Style)style_before_reset), rest_error,
                        frame_us_total / (unsigned int)(frames + 1), frame_us_max, skin_us_max, motions_loaded,
                        combat_ready, combo_moves_seen, stinger_seen, (int)attack_travel);
                fclose(report);
            }
            running = 0;
        }

        ++frames;
        sceDisplayWaitVblankStart();
        draw_buffer = sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
