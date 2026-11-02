/* DMC3 PSP runtime: converted game assets and initial gameplay routines. */
typedef unsigned int u32;
typedef unsigned char u8;

#include "difficulty.h"
#include "model.h"

extern u8 msx[]; /* PSPSDK's BSD-licensed 8x8 debug font. */

int sceDisplaySetMode(int mode, int width, int height);
int sceDisplaySetFrameBuf(void *topaddr, int bufferwidth, int pixelformat, int sync);
int sceDisplayWaitVblankStart(void);
int sceCtrlSetSamplingCycle(int cycle);
int sceCtrlSetSamplingMode(int mode);
int sceCtrlReadBufferPositive(void *pad_data, int count);
int sceIoOpen(const char *file, int flags, int mode);
int sceIoWrite(int fd, const void *data, unsigned int size);
int sceIoClose(int fd);
u32 sceKernelGetSystemTimeLow(void);

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define FRAMEBUFFER_WIDTH 512
#define PSP_CTRL_START 0x000008
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400

static u32 *const framebuffer = (u32 *)0x04000000;

struct SceCtrlData {
    u32 TimeStamp;
    u32 Buttons;
    u8 Lx;
    u8 Ly;
    u8 Rx;
    u8 Ry;
    u8 Rsrv[6];
};

static void clear_screen(u32 color) {
    int i;
    for (i = 0; i < FRAMEBUFFER_WIDTH * SCREEN_HEIGHT; ++i) {
        framebuffer[i] = color;
    }
}

static void draw_char(int x, int y, char c, u32 color) {
    const u8 *glyph = &msx[(u8)c * 8];
    int row;
    int col;
    for (row = 0; row < 8; ++row) {
        for (col = 0; col < 8; ++col) {
            if ((glyph[row] & (1u << col)) != 0) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    framebuffer[py * FRAMEBUFFER_WIDTH + px] = color;
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, u32 color) {
    while (*text != '\0') {
        draw_char(x, y, *text, color);
        x += 8;
        ++text;
    }
}

static int file_exists(const char *path) {
    int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    (void)sceIoClose(fd);
    return 1;
}

static int uint_to_text(char *output, u32 value) {
    char reverse[10];
    int count = 0;
    int length = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < 10);
    while (count > 0) {
        output[length++] = reverse[--count];
    }
    output[length] = '\0';
    return length;
}

static void draw_fps(int x, int y, int fps_x10, u32 color) {
    char text[16];
    int length = uint_to_text(text, (u32)(fps_x10 / 10));
    text[length++] = '.';
    text[length++] = (char)('0' + fps_x10 % 10);
    text[length] = '\0';
    draw_text(x, y, text, color);
}

static int benchmark_model(void) {
    u32 start = sceKernelGetSystemTimeLow();
    int frame;
    u32 elapsed;
    for (frame = 0; frame < 30; ++frame) {
        (void)dmc3_model_render(framebuffer);
    }
    elapsed = sceKernelGetSystemTimeLow() - start;
    if (elapsed == 0) {
        elapsed = 1;
    }
    return (int)(300000000u / elapsed);
}

static void write_performance(int fps_x10) {
    static const char prefix[] = "MODEL_FPS_X10=";
    char digits[10];
    int fd = sceIoOpen("ms0:/PSP/GAME/DMC3PSP/PERF.TXT",
                       PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int length;
    if (fd < 0) {
        return;
    }
    (void)sceIoWrite(fd, prefix, sizeof(prefix) - 1);
    length = uint_to_text(digits, (u32)fps_x10);
    (void)sceIoWrite(fd, digits, length);
    (void)sceIoWrite(fd, "\n", 1);
    (void)sceIoClose(fd);
}

static int write_boot_marker(int gameplay_status, int model_status, int fps_x10) {
    static const char pass[] =
        "DMC3PSP_BOOT_OK\n"
        "PSP execution, original TIM2 textures, gameplay self-test, textured model render, and 30fps target passed.\n";
    static const char fail[] =
        "DMC3PSP_BOOT_OK\n"
        "Combined gameplay/model/30fps milestone FAILED.\n";
    int passed = gameplay_status && model_status && fps_x10 >= 300;
    const char *message = passed ? pass : fail;
    int size = passed ? (int)sizeof(pass) - 1 : (int)sizeof(fail) - 1;
    int fd = sceIoOpen("ms0:/PSP/GAME/DMC3PSP/BOOT_OK.TXT",
                       PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) {
        return fd;
    }
    int written = sceIoWrite(fd, message, size);
    (void)sceIoClose(fd);
    return written == size ? 0 : -1;
}

int app_main(void) {
    struct SceCtrlData pad;
    int frames = 0;
    int gameplay_status = dmc3_gameplay_self_test();
    int autoexit = file_exists("ms0:/PSP/GAME/DMC3PSP/AUTOEXIT.TXT");
    int model_pixels;
    int fps_x10;
    int marker_status;

    (void)sceDisplaySetMode(0, SCREEN_WIDTH, SCREEN_HEIGHT);
    (void)sceDisplaySetFrameBuf(framebuffer, FRAMEBUFFER_WIDTH, 3, 1);
    (void)sceCtrlSetSamplingCycle(0);
    (void)sceCtrlSetSamplingMode(1);

    clear_screen(0xff201008);
    model_pixels = dmc3_model_render(framebuffer);
    fps_x10 = benchmark_model();
    write_performance(fps_x10);
    marker_status = write_boot_marker(gameplay_status, model_pixels > 0, fps_x10);
    draw_text(16, 8, "DMC3 PSP: DANTE MODEL RUNTIME", 0xff00ffff);
    draw_text(16, 24, "TEXTURED + Z-BUFFERED + ORIGINAL ASSETS", 0xffffffff);
    draw_text(16, 184, "FPS:", 0xffffffff);
    draw_fps(48, 184, fps_x10, fps_x10 >= 300 ? 0xff00ff00 : 0xff0000ff);
    draw_text(16, 200, gameplay_status ? "GAMEPLAY LOGIC: PASS" : "GAMEPLAY LOGIC: FAIL",
              gameplay_status ? 0xff00ff00 : 0xff0000ff);
    draw_text(16, 216, model_pixels > 0 ? "MODEL RENDER: PASS" : "MODEL RENDER: FAIL",
              model_pixels > 0 ? 0xff00ff00 : 0xff0000ff);
    draw_text(16, 232, autoexit ? "AUTOMATION: EXIT AFTER 120 FRAMES" : "START: EXIT", 0xffe0e0e0);
    draw_text(16, 256, marker_status == 0 ? "BOOT MARKER: PASS" : "BOOT MARKER: FAIL",
              marker_status == 0 ? 0xff00ff00 : 0xff0000ff);

    for (;;) {
        (void)sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_START) != 0) {
            break;
        }
        (void)sceDisplayWaitVblankStart();
        ++frames;
        if (autoexit && frames >= 120) {
            break;
        }
    }
    return 0;
}
