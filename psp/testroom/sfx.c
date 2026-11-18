/* The game's sound effects (docs/MUSIC.md, Sound effects): a bank resolved by tools/gen_sfx.py (SFX_SYS.BIN: per id a
 * sample, volume, pan, playback step and the id it chains to), played by a mixer of its own on one hardware channel.
 * Samples are the SPU's ADPCM (16-byte frames of 28 nibbles: a shift and one of five filters), decoded as they play;
 * they're read from the memory stick when first wanted and kept in a small cache (rooms leave little memory). */
#include "sfx.h"

#include <malloc.h>
#include <pspaudio.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

#define CHUNK 512
#define VOICES 8
#define CACHE_BYTES (192 * 1024)

typedef struct { unsigned short sample; unsigned char volume, pan; float step; short next; short pad; } SfxId;
typedef struct { unsigned int offset, size; } SfxSample;
typedef struct {
    int active;
    const unsigned char *data;
    unsigned int size, frame;     /* bytes; the frame (16 bytes) being played */
    float pos;                    /* within the frame's 28 samples */
    float step;
    int s1, s2, decoded[28], have;
    int left, right;              /* 0-0x8000 */
} Voice;

static SfxId *ids;
static SfxSample *samples;
static int id_count, sample_count, ready;
static unsigned int body_at;
static SceUID fd = -1, thread, lock;
static int channel = -1;
static Voice voices[VOICES];
static volatile int running;
static float master = 1.0f;
static short __attribute__((aligned(64))) pcm[2][CHUNK * 2];

/* The cache: samples by index, packed into one pool; full, it starts over (the voices using it stop). */
static unsigned char *pool;
static unsigned int pool_used;
static int *cached_at;            /* per sample: its place in the pool, -1 not there */

static const int coef[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

static const unsigned char *sample_data(int s) {
    unsigned int size;
    int k;
    if (s < 0 || s >= sample_count) return 0;
    if (cached_at[s] >= 0) return pool + cached_at[s];
    size = (samples[s].size + 15u) & ~15u;
    if (size > CACHE_BYTES) return 0;
    if (pool_used + size > CACHE_BYTES) {
        for (k = 0; k < VOICES; ++k) voices[k].active = 0;
        for (k = 0; k < sample_count; ++k) cached_at[k] = -1;
        pool_used = 0;
    }
    sceIoLseek(fd, body_at + samples[s].offset, PSP_SEEK_SET);
    if (sceIoRead(fd, pool + pool_used, samples[s].size) != (int)samples[s].size) return 0;
    cached_at[s] = (int)pool_used;
    pool_used += size;
    return pool + cached_at[s];
}

static int decode_frame(Voice *v) {
    const unsigned char *f;
    int shift, filter, k;
    if (v->frame * 16 + 16 > v->size) return 0;
    f = v->data + v->frame * 16;
    if (v->frame > 0 && (f[1] & 1) && (f[1] & 2) == 0 && f[0] == 0 && f[2] == 0) return 0;   /* an end with nothing in it */
    shift = f[0] & 15; filter = f[0] >> 4;
    if (filter > 4) filter = 0;
    for (k = 0; k < 28; ++k) {
        int nib = (f[2 + k / 2] >> ((k & 1) * 4)) & 15, s = nib >= 8 ? nib - 16 : nib;
        int value = ((s << 12) >> shift) + ((v->s1 * coef[filter][0] + v->s2 * coef[filter][1]) >> 6);
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        v->s2 = v->s1; v->s1 = value;
        v->decoded[k] = value;
    }
    v->have = 1;
    return 1;
}

static int mixer_main(SceSize args, void *argp) {
    int which = 0;
    (void)args; (void)argp;
    while (running) {
        static int mix[CHUNK * 2];
        short *out = pcm[which];
        int i, k;
        which = !which;
        memset(mix, 0, sizeof mix);
        sceKernelWaitSema(lock, 1, 0);
        for (k = 0; k < VOICES; ++k) {
            Voice *v = &voices[k];
            if (!v->active) continue;
            for (i = 0; i < CHUNK; ++i) {
                int s;
                while (!v->have || v->pos >= 28.0f) {
                    if (v->have) { v->pos -= 28.0f; ++v->frame; v->have = 0; }
                    if (!decode_frame(v)) { v->active = 0; break; }
                    /* The frame's end flag without loop: the last frame. */
                    if ((v->data[v->frame * 16 + 1] & 1) && !(v->data[v->frame * 16 + 1] & 2)) v->size = v->frame * 16 + 16;
                }
                if (!v->active) break;
                s = v->decoded[(int)v->pos];
                mix[i * 2] += (s * v->left) >> 15;
                mix[i * 2 + 1] += (s * v->right) >> 15;
                v->pos += v->step;
            }
        }
        sceKernelSignalSema(lock, 1);
        for (i = 0; i < CHUNK * 2; ++i) {
            int s = (int)((float)mix[i] * master);
            out[i] = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        if (sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, out) < 0) sceKernelDelayThread(10000);
    }
    return 0;
}

int sfx_init(const char *game_dir) {
    char path[300];
    struct { char magic[4]; unsigned int ids, samples, body; } h;
    int k;
    if (ready) return 1;
    snprintf(path, sizeof path, "%sSFX_SYS.BIN", game_dir);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    if (sceIoRead(fd, &h, sizeof h) != (int)sizeof h || memcmp(h.magic, "SFX1", 4) != 0) goto fail;
    id_count = (int)h.ids; sample_count = (int)h.samples; body_at = h.body;
    ids = malloc(sizeof(SfxId) * id_count);
    samples = malloc(sizeof(SfxSample) * sample_count);
    cached_at = malloc(sizeof(int) * sample_count);
    pool = memalign(64, CACHE_BYTES);
    if (!ids || !samples || !cached_at || !pool) goto fail;
    if (sceIoRead(fd, ids, sizeof(SfxId) * id_count) != (int)(sizeof(SfxId) * id_count)) goto fail;
    if (sceIoRead(fd, samples, sizeof(SfxSample) * sample_count) != (int)(sizeof(SfxSample) * sample_count)) goto fail;
    for (k = 0; k < sample_count; ++k) cached_at[k] = -1;
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, CHUNK, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) goto fail;
    lock = sceKernelCreateSema("sfx", 0, 1, 1, NULL);
    running = 1;
    thread = sceKernelCreateThread("sfx", mixer_main, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (thread < 0) goto fail;
    sceKernelStartThread(thread, 0, NULL);
    ready = 1;
    return 1;
fail:
    if (fd >= 0) sceIoClose(fd);
    fd = -1;
    return 0;
}

/* One id and those it chains to. pan: -1 left to 1 right, added to its own. */
void sfx_play(int id, float pan, float volume) {
    int guard;
    if (!ready) return;
    sceKernelWaitSema(lock, 1, 0);
    for (guard = 0; guard < 4 && id >= 0 && id < id_count; ++guard) {
        const SfxId *e = &ids[id];
        if (e->sample != 0xffff && e->step > 0.0f) {
            const unsigned char *data = sample_data(e->sample);
            int k, best = 0;
            for (k = 0; k < VOICES; ++k) if (!voices[k].active) { best = k; break; }
            if (data) {
                Voice *v = &voices[best];
                float p = (float)(e->pan - 64) / 64.0f + pan, gain = volume * (float)e->volume / 127.0f;
                if (p < -1.0f) p = -1.0f;
                if (p > 1.0f) p = 1.0f;
                memset(v, 0, sizeof *v);
                v->data = data; v->size = (samples[e->sample].size) & ~15u; v->step = e->step;
                v->left = (int)(gain * (p > 0.0f ? 1.0f - p : 1.0f) * 32768.0f);
                v->right = (int)(gain * (p < 0.0f ? 1.0f + p : 1.0f) * 32768.0f);
                v->active = 1;
            }
        }
        if (e->next < 0 || e->next == id) break;
        id = e->next;
    }
    sceKernelSignalSema(lock, 1);
}

void sfx_volume(float v) { master = v; }
