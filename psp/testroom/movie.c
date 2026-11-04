#include "movie.h"

#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspjpeg.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psputility.h>
#include <psputils.h>

#include <malloc.h>
#include <string.h>

typedef struct {
    char magic[4];
    unsigned int frames, fps_x1000, width, height, audio_rate, audio_frames, block_bytes, index_at;
} MovieHeader;
typedef struct { unsigned int offset, adpcm, jpeg; } MovieIndex;

#define OUT_STRIDE 512
#define OUT_ROWS 272
#define READ_MAX (512 * 1024)
#define AUDIO_CHUNK 1024              /* stereo sample pairs handed over at a time */
#define AUDIO_RING (64 * 1024)        /* ADPCM bytes: over a second */

static unsigned int __attribute__((aligned(16))) list[1024];
static int stat_shown, stat_late, stat_decode_max, stat_error, stat_audio_error;
static unsigned int stat_elapsed_ms, stat_audio_ms;

/* ---- Sound: a ring of ADPCM bytes, decoded and played by its own thread. ---- */

static unsigned char audio_ring[AUDIO_RING];
static volatile unsigned int ring_in, ring_out;
static volatile int audio_running;
static volatile unsigned int audio_played;   /* sample pairs handed to the hardware: the movie's clock */
static int audio_ready;

static const short step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const signed char index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static int predicted[2], step_index[2];

static short ima_decode(int channel, int nibble) {
    int step = step_table[step_index[channel]];
    int change = step >> 3;
    if (nibble & 4) change += step;
    if (nibble & 2) change += step >> 1;
    if (nibble & 1) change += step >> 2;
    predicted[channel] += nibble & 8 ? -change : change;
    if (predicted[channel] > 32767) predicted[channel] = 32767;
    if (predicted[channel] < -32768) predicted[channel] = -32768;
    step_index[channel] += index_table[nibble & 7];
    if (step_index[channel] < 0) step_index[channel] = 0;
    if (step_index[channel] > 88) step_index[channel] = 88;
    return (short)predicted[channel];
}

static int audio_thread(SceSize args, void *argp) {
    static short __attribute__((aligned(64))) pcm[2][AUDIO_CHUNK * 2];
    int which = 0;
    (void)args; (void)argp;
    while (audio_running) {
        short *out = pcm[which];
        int i;
        which = !which;
        for (i = 0; i < AUDIO_CHUNK; ++i) {
            if (ring_out != ring_in) {
                int b = audio_ring[ring_out % AUDIO_RING];
                ++ring_out;
                out[i * 2] = ima_decode(0, b & 15);
                out[i * 2 + 1] = ima_decode(1, b >> 4);
            } else {
                out[i * 2] = out[i * 2 + 1] = 0;
            }
        }
        {
            int r = sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, out);
            if (r < 0) { stat_audio_error = r; sceKernelDelayThread(AUDIO_CHUNK * 22); }
        }
        audio_played += AUDIO_CHUNK;
    }
    return 0;
}

static void ring_put(const unsigned char *data, unsigned int bytes) {
    unsigned int i;
    for (i = 0; i < bytes; ++i) {
        /* Full: the sound is well ahead, so wait for it. */
        while (ring_in - ring_out >= AUDIO_RING && audio_running) sceKernelDelayThread(2000);
        audio_ring[ring_in % AUDIO_RING] = data[i];
        ++ring_in;
    }
}

/* ---- Decoder: loaded the first time, kept after. ---- */

static int decoder_ready(unsigned int width, unsigned int height) {
    static int loaded;
    if (!loaded) {
        if (sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC) < 0) return 0;
        if (sceJpegInitMJpeg() < 0) return 0;
        loaded = 1;
    }
    return sceJpegCreateMJpeg((int)width, (int)height) >= 0;
}

/* ---- Playing. ---- */

/* A straight copy into the frame: a texture this size, unswizzled, costs the GPU far more than the
 * decoding does (15 FPS against 7 ms a frame). */
static void show(const void *pixels, const MovieHeader *h, void **draw_buffer) {
    sceGuStart(GU_DIRECT, list);
    sceGuCopyImage(GU_PSM_8888, 0, 0, (int)h->width, (int)h->height, (int)h->width, (void *)pixels, 0, 0, OUT_STRIDE,
                   (void *)((unsigned int)sceGeEdramGetAddr() + (unsigned int)*draw_buffer));
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    *draw_buffer = sceGuSwapBuffers();
}

int movie_play(const char *path, void **draw_buffer, int stop_after, MovieFrame on_frame) {
    MovieHeader h;
    MovieIndex *index = NULL;
    unsigned char *read_buffer = NULL;
    unsigned int *pixels = NULL;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0), thread = -1;
    unsigned int frame = 0, held = 0xffffffffu, start = 0;
    int played = 0;
    stat_shown = stat_late = stat_decode_max = stat_error = stat_audio_error = 0;
    memset(&h, 0, sizeof(h));
    if (fd < 0) return 0;
    if (sceIoRead(fd, &h, sizeof(h)) != (int)sizeof(h) || memcmp(h.magic, "DMV1", 4) != 0 || !h.frames) goto done;
    index = malloc(sizeof(MovieIndex) * h.frames);
    read_buffer = memalign(64, READ_MAX);
    pixels = memalign(64, OUT_STRIDE * OUT_ROWS * 4);
    if (!index || !read_buffer || !pixels) goto done;
    sceIoLseek(fd, h.index_at, PSP_SEEK_SET);
    if (sceIoRead(fd, index, sizeof(MovieIndex) * h.frames) != (int)(sizeof(MovieIndex) * h.frames)) goto done;
    if (!decoder_ready(h.width, h.height)) goto done;
    /* Sound comes out at the hardware's rate through the sample-rate converter. */
    ring_in = ring_out = audio_played = 0;
    memset(predicted, 0, sizeof(predicted));
    memset(step_index, 0, sizeof(step_index));
    audio_ready = sceAudioSRCChReserve(AUDIO_CHUNK, (int)h.audio_rate, 2) >= 0;
    if (audio_ready) audio_running = 1;
    played = 1;
    start = sceKernelGetSystemTimeLow();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    while (frame < h.frames && (stop_after <= 0 || (int)frame < stop_after)) {
        const MovieIndex *row = &index[frame];
        unsigned int bytes = row->adpcm + row->jpeg;
        SceCtrlData pad;
        unsigned int t0;
        int d;
        if (bytes > READ_MAX) break;
        sceIoLseek(fd, row->offset, PSP_SEEK_SET);
        if (sceIoRead(fd, read_buffer, bytes) != (int)bytes) break;
        if (audio_ready) ring_put(read_buffer, row->adpcm);
        if (audio_ready && thread < 0 && (ring_in >= AUDIO_CHUNK * 4 || frame + 1 == h.frames)) {
            /* A little sound buffered before the clock starts. */
            thread = sceKernelCreateThread("movie audio", audio_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
            if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
            start = sceKernelGetSystemTimeLow();
        }
        t0 = sceKernelGetSystemTimeLow();
        sceKernelDcacheWritebackInvalidateRange(pixels, OUT_STRIDE * OUT_ROWS * 4);
        d = sceJpegDecodeMJpeg(read_buffer + row->adpcm, row->jpeg, (u8 *)pixels, 0);
        t0 = sceKernelGetSystemTimeLow() - t0;
        if ((int)t0 > stat_decode_max) stat_decode_max = (int)t0;
        if (d < 0) stat_error = d;
        if (d >= 0) {
            /* Wait for this frame's time, by the sound's clock if there is sound. */
            unsigned int due_us = (unsigned int)((unsigned long long)frame * 1000000000ull / h.fps_x1000);
            for (;;) {
                unsigned int now_us;
                if (audio_ready && thread >= 0)
                    now_us = (unsigned int)((unsigned long long)audio_played * 1000000ull / h.audio_rate);
                else
                    now_us = sceKernelGetSystemTimeLow() - start;
                if (now_us + 8000u >= due_us) {
                    if (now_us > due_us + 40000u) ++stat_late;
                    break;
                }
                sceKernelDelayThread(2000);
            }
            sceKernelDcacheWritebackRange(pixels, OUT_STRIDE * OUT_ROWS * 4);
            show(pixels, &h, draw_buffer);
            ++stat_shown;
        }
        if (on_frame) on_frame((int)frame);
        if (frame % 300 == 0) scePowerTick(PSP_POWER_TICK_ALL);
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & ~held & (PSP_CTRL_START | PSP_CTRL_CROSS)) break;
        held = pad.Buttons;
        ++frame;
    }
done:
    stat_elapsed_ms = (sceKernelGetSystemTimeLow() - start) / 1000u;
    stat_audio_ms = h.audio_rate ? (unsigned int)((unsigned long long)audio_played * 1000ull / h.audio_rate) : 0;
    if (audio_ready) {
        audio_running = 0;
        if (thread >= 0) {
            sceKernelWaitThreadEnd(thread, NULL);
            sceKernelDeleteThread(thread);
        }
        /* What's already queued has to finish before the channel can be released. */
        while (sceAudioSRCChRelease() < 0) sceKernelDelayThread(1000);
        audio_ready = 0;
    }
    if (played) sceJpegDeleteMJpeg();
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    free(index);
    free(read_buffer);
    free(pixels);
    sceIoClose(fd);
    return played;
}

int movie_last_error(void) { return stat_error ? stat_error : stat_audio_error; }
void movie_timing(unsigned int *elapsed_ms, unsigned int *audio_ms) { *elapsed_ms = stat_elapsed_ms; *audio_ms = stat_audio_ms; }

void movie_stats(int *shown, int *late, int *decode_us_max) {
    *shown = stat_shown;
    *late = stat_late;
    *decode_us_max = stat_decode_max;
}
