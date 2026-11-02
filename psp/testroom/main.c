/* DMC3 PSP test room: hardware-rendered (sceGu) Dante on a grey floor with a
 * white grid, driven by the analog stick with a follow camera.
 *
 * Controls: analog stick / d-pad = run (camera-relative), L/R = orbit camera,
 * START = reset position, HOME = quit.
 *
 * If AUTOEXIT.TXT exists next to the EBOOT, the room runs a scripted input
 * sequence, saves two BMP screenshots, writes TESTROOM_OK.TXT (or
 * TESTROOM_FAIL.TXT) and exits. tools/test_testroom.ps1 relies on this. */

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

PSP_MODULE_INFO("DMC3TestRoom", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define FRAMEBUFFER_BYTES (BUF_WIDTH * SCR_HEIGHT * 4)

/* Rotation applied on top of the player yaw so the model's front matches the
 * direction of travel. Verified in PPSSPP screenshots: the MOD front is +Z. */
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
static ModelVertex *model_vertices;
static unsigned short *model_indices;
static void *model_textures[DMC3_MODEL_TEXTURE_COUNT];
static unsigned int __attribute__((aligned(16))) font_texture[FONT_TEX_SIZE * FONT_TEX_SIZE];
static unsigned int *capture_buffer;

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

static void save_bmp(const char *name, const unsigned int *vram) {
    char path[320];
    unsigned char header[54];
    static unsigned char row[SCR_WIDTH * 3];
    unsigned int data_size = SCR_WIDTH * 3 * SCR_HEIGHT;
    unsigned int file_size = 54 + data_size;
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
    { int w = SCR_WIDTH, h = SCR_HEIGHT; memcpy(&header[18], &w, 4); memcpy(&header[22], &h, 4); }
    header[26] = 1;
    header[28] = 24;
    memcpy(&header[34], &data_size, 4);
    fwrite(header, 1, sizeof(header), file);
    for (y = SCR_HEIGHT - 1; y >= 0; --y) {
        const unsigned int *source = vram + y * BUF_WIDTH;
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
        ModelVertex *v = &model_vertices[i];
        v->u = dmc3_model_uvs[i].u;
        v->v = dmc3_model_uvs[i].v;
        v->nx = dmc3_model_normals[i].x;
        v->ny = dmc3_model_normals[i].y;
        v->nz = dmc3_model_normals[i].z;
        v->x = dmc3_model_positions[i].x;
        v->y = dmc3_model_positions[i].y;
        v->z = dmc3_model_positions[i].z;
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

static void draw_hud(const char *top, const char *bottom) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexImage(0, FONT_TEX_SIZE, FONT_TEX_SIZE, FONT_TEX_SIZE, font_texture);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    /* Shadow, then text. sceGuColor is the unlit material color. */
    sceGuColor(0xff000000);
    draw_text(9, 9, top);
    draw_text(9, SCR_HEIGHT - 15, bottom);
    sceGuColor(0xffffffff);
    draw_text(8, 8, top);
    draw_text(8, SCR_HEIGHT - 16, bottom);
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
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
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
        /* Material alpha: without this the lit vertex alpha is 0 and the
         * alpha test discards every Dante pixel. */
        sceGuAmbientColor(0xffffffff);
        sceGuEnable(GU_LIGHT0);
        sceGuEnable(GU_LIGHT1);
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void render_scene(const Player *player, const FollowCamera *camera,
                         const char *hud_top, const char *hud_bottom) {
    int i;
    int bound_texture = -1;

    sceGuStart(GU_DIRECT, display_list);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuColor(0xffffffff);
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
        ScePspFVector3 position = {player->x, 0.0f, player->z};
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

    draw_hud(hud_top, hud_bottom);
    sceGuFinish();
    sceGuSync(0, 0);
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

static void read_pad(PlayerInput *input) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    input->stick_x = stick_axis(pad.Lx);
    input->stick_y = -stick_axis(pad.Ly);
    if (pad.Buttons & PSP_CTRL_UP) input->stick_y = 1.0f;
    if (pad.Buttons & PSP_CTRL_DOWN) input->stick_y = -1.0f;
    if (pad.Buttons & PSP_CTRL_RIGHT) input->stick_x = 1.0f;
    if (pad.Buttons & PSP_CTRL_LEFT) input->stick_x = -1.0f;
    input->orbit = ((pad.Buttons & PSP_CTRL_RTRIGGER) ? 1 : 0) - ((pad.Buttons & PSP_CTRL_LTRIGGER) ? 1 : 0);
    input->reset = (pad.Buttons & PSP_CTRL_START) != 0;
}

/* Scripted input for the automated test: idle, run away from the camera,
 * run right, then run while orbiting. */
static void scripted_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    if (frame >= 30 && frame < 120) input->stick_y = 1.0f;
    else if (frame >= 120 && frame < 180) input->stick_x = 1.0f;
    else if (frame >= 180 && frame < 240) { input->stick_y = 1.0f; input->orbit = 1; }
}

/* Longer demo for RECORD.TXT: run forward, strafe right, a full circle,
 * run while orbiting the camera, then stand. */
#define RECORD_FRAMES 480
static void record_pad(int frame, PlayerInput *input) {
    memset(input, 0, sizeof(*input));
    if (frame >= 20 && frame < 110) input->stick_y = 1.0f;
    else if (frame >= 110 && frame < 180) input->stick_x = 1.0f;
    else if (frame >= 180 && frame < 330) {
        float t = (float)(frame - 180) / 150.0f * 6.2831853f;
        input->stick_x = sinf(t);
        input->stick_y = cosf(t);
    } else if (frame >= 330 && frame < 440) { input->stick_y = 0.8f; input->orbit = 1; }
}

/* ---- Main --------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    Player player;
    FollowCamera camera;
    PlayerInput input;
    void *draw_buffer = (void *)0;
    unsigned int last_time;
    unsigned int frame_us_total = 0, frame_us_max = 0, last_frame_us = 0;
    int frames = 0;
    int autotest, recording;
    float start_x, start_z;

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
    if (!init_model()) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Out of memory loading the Dante model.\n");
        sceKernelSleepThread();
    }
    init_room();
    init_font();
    sceKernelDcacheWritebackAll();
    init_gu();

    autotest = game_file_exists("AUTOEXIT.TXT");
    recording = game_file_exists("RECORD.TXT");
    if (recording) autotest = 0;
    if (autotest || recording) capture_buffer = memalign(64, FRAMEBUFFER_BYTES);
    player_reset(&player, &camera);
    start_x = player.x;
    start_z = player.z;
    last_time = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int frame_start = sceKernelGetSystemTimeLow();
        unsigned int frame_us;
        float dt;
        char hud_top[64];

        if (recording) {
            dt = 1.0f / 60.0f;
            record_pad(frames, &input);
        } else if (autotest) {
            dt = 1.0f / 60.0f;
            scripted_pad(frames, &input);
        } else {
            dt = (float)(frame_start - last_time) / 1000000.0f;
            if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
            read_pad(&input);
        }
        last_time = frame_start;

        player_update(&player, &camera, &input, dt);
        /* HUD shows the previous frame's cost (this one isn't measured yet). */
        snprintf(hud_top, sizeof(hud_top), "DMC3 TEST ROOM  frame %2u.%u ms  pos %5d %5d",
                 last_frame_us / 1000, (last_frame_us % 1000) / 100, (int)player.x, (int)player.z);
        render_scene(&player, &camera, hud_top, "stick: run  L/R: camera  START: reset");

        frame_us = sceKernelGetSystemTimeLow() - frame_start;
        last_frame_us = frame_us;
        frame_us_total += frame_us;
        if (frame_us > frame_us_max) frame_us_max = frame_us;

        if (recording) {
            /* Every other frame -> 30 fps video. */
            if ((frames & 1) == 0) {
                char name[32];
                snprintf(name, sizeof(name), "REC%04d.BMP", frames / 2);
                capture_frame(draw_buffer, name);
            }
            if (frames + 1 >= RECORD_FRAMES) {
                char path[320];
                FILE *done;
                path_in_game_dir(path, sizeof(path), "RECORD_DONE.TXT");
                done = fopen(path, "w");
                if (done) { fprintf(done, "frames=%d\n", RECORD_FRAMES / 2); fclose(done); }
                running = 0;
            }
        }
        if (autotest && frames == 25) capture_frame(draw_buffer, "SHOT_IDLE.BMP");
        if (autotest && frames == 150) capture_frame(draw_buffer, "SHOT_SIDE.BMP");
        if (autotest && frames == 240) {
            char path[320];
            FILE *report;
            float dx = player.x - start_x, dz = player.z - start_z;
            float moved = sqrtf(dx * dx + dz * dz);
            int ok = moved > 500.0f && frame_us_max < 33333u;
            capture_frame(draw_buffer, "SHOT_RUN.BMP");
            path_in_game_dir(path, sizeof(path), ok ? "TESTROOM_OK.TXT" : "TESTROOM_FAIL.TXT");
            report = fopen(path, "w");
            if (report) {
                fprintf(report, "%s\nframes=%d\nmoved=%d\nstart=%d,%d\nend=%d,%d\nyaw_mdeg=%d\ncamera_yaw_mdeg=%d\n"
                                "avg_frame_us=%u\nmax_frame_us=%u\n",
                        ok ? "DMC3PSP_TESTROOM_OK" : "DMC3PSP_TESTROOM_FAIL", frames + 1, (int)moved,
                        (int)start_x, (int)start_z, (int)player.x, (int)player.z,
                        (int)(player.yaw * 57295.78f), (int)(camera.yaw * 57295.78f),
                        frame_us_total / (unsigned int)(frames + 1), frame_us_max);
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
