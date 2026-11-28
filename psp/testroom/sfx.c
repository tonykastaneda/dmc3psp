/* The game's sound effects (docs/MUSIC.md, Sound effects): banks resolved by tools/gen_sfx.py (SFX_SYS.BIN, the
 * system bank; SFX_DANTE.BIN, his own: per id a sample, volume, pan, playback step and the id it chains to), played by
 * a mixer of its own on one hardware channel, and his motions' sound events (sfx_runtime.h) as their frames pass.
 * Samples are the SPU's ADPCM (16-byte frames of 28 nibbles: a shift and one of five filters), decoded as they play;
 * they're read from the memory stick when first wanted and kept in a small cache (rooms leave little memory). */
#include "sfx.h"
#include "motion.h"
#include "sfx_runtime.h"

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

typedef struct {
    SfxId *ids;
    SfxSample *samples;
    int id_count, sample_count, *cached_at;   /* cached_at: per sample, its place in the pool (-1 not there) */
    unsigned int body_at;
    SceUID fd;
} Bank;
static const char *const bank_files[SFX_BANKS] = {"SFX_SYS.BIN", "SFX_DANTE.BIN", "SFX_W0.BIN", "SFX_W1.BIN",
    "SFX_W2.BIN", "SFX_W3.BIN", "SFX_W4.BIN", "SFX_W5.BIN", "SFX_W6.BIN", "SFX_W7.BIN", "SFX_W8.BIN", "SFX_W9.BIN"};
static Bank banks[SFX_BANKS + 1];   /* + the room's footsteps (SFX_ST<room>.BIN, sfx_room) */
#define BANK_ROOM SFX_BANKS
#define STEP_EVENT 255             /* gen_sfx.py STEP_BANK: an event that's a footstep, its id the foot's kind */
static char sfx_dir[256];
static int floor_material, room_open = -1;
static int ready;
static SceUID thread, lock;
/* The motion events, by motion index (sfx_runtime.h's names resolved at start). */
static short *event_motion;
static int channel = -1;
static Voice voices[VOICES];
static volatile int running;
static float master = 1.0f;
static short __attribute__((aligned(64))) pcm[2][CHUNK * 2];

/* The cache: samples by index, packed into one pool; full, it starts over (the voices using it stop). */
static unsigned char *pool;
static unsigned int pool_used;

static const int coef[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

static const unsigned char *sample_data(Bank *b, int s) {
    unsigned int size;
    int k, j;
    if (s < 0 || s >= b->sample_count) return 0;
    if (b->cached_at[s] >= 0) return pool + b->cached_at[s];
    size = (b->samples[s].size + 15u) & ~15u;
    if (size > CACHE_BYTES) return 0;
    if (pool_used + size > CACHE_BYTES) {
        for (k = 0; k < VOICES; ++k) voices[k].active = 0;
        for (j = 0; j <= SFX_BANKS; ++j) for (k = 0; k < banks[j].sample_count; ++k) banks[j].cached_at[k] = -1;
        pool_used = 0;
    }
    sceIoLseek(b->fd, b->body_at + b->samples[s].offset, PSP_SEEK_SET);
    if (sceIoRead(b->fd, pool + pool_used, b->samples[s].size) != (int)b->samples[s].size) return 0;
    b->cached_at[s] = (int)pool_used;
    pool_used += size;
    return pool + b->cached_at[s];
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

static int bank_open(Bank *b, const char *game_dir, const char *file) {
    char path[300];
    struct { char magic[4]; unsigned int ids, samples, body; } h;
    int k;
    snprintf(path, sizeof path, "%s%s", game_dir, file);
    b->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (b->fd < 0) return 0;
    if (sceIoRead(b->fd, &h, sizeof h) != (int)sizeof h || memcmp(h.magic, "SFX1", 4) != 0) return 0;
    b->id_count = (int)h.ids; b->sample_count = (int)h.samples; b->body_at = h.body;
    b->ids = malloc(sizeof(SfxId) * b->id_count);
    b->samples = malloc(sizeof(SfxSample) * b->sample_count);
    b->cached_at = malloc(sizeof(int) * b->sample_count);
    if (!b->ids || !b->samples || !b->cached_at) return 0;
    if (sceIoRead(b->fd, b->ids, sizeof(SfxId) * b->id_count) != (int)(sizeof(SfxId) * b->id_count)) return 0;
    if (sceIoRead(b->fd, b->samples, sizeof(SfxSample) * b->sample_count) != (int)(sizeof(SfxSample) * b->sample_count))
        return 0;
    for (k = 0; k < b->sample_count; ++k) b->cached_at[k] = -1;
    return 1;
}

int sfx_init(const char *game_dir) {
    int k;
    if (ready) return 1;
    snprintf(sfx_dir, sizeof sfx_dir, "%s", game_dir);
    pool = memalign(64, CACHE_BYTES);
    if (!pool) return 0;
    for (k = 0; k < SFX_BANKS; ++k)
        if (!bank_open(&banks[k], game_dir, bank_files[k])) { banks[k].id_count = banks[k].sample_count = 0; if (!k) goto fail; }
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
    return 0;
}

/* One id of a bank and those it chains to. pan: -1 left to 1 right, added to its own. */
void sfx_play_bank(int bank, int id, float pan, float volume) {
    int guard;
    Bank *b;
    if (!ready || bank < 0 || bank > SFX_BANKS) return;
    b = &banks[bank];
    sceKernelWaitSema(lock, 1, 0);
    for (guard = 0; guard < 4 && id >= 0 && id < b->id_count; ++guard) {
        const SfxId *e = &b->ids[id];
        if (e->sample != 0xffff && e->step > 0.0f) {
            const unsigned char *data = sample_data(b, e->sample);
            int k, best = 0;
            for (k = 0; k < VOICES; ++k) if (!voices[k].active) { best = k; break; }
            if (data) {
                Voice *v = &voices[best];
                float p = (float)(e->pan - 64) / 64.0f + pan, gain = volume * (float)e->volume / 127.0f;
                if (p < -1.0f) p = -1.0f;
                if (p > 1.0f) p = 1.0f;
                memset(v, 0, sizeof *v);
                v->data = data; v->size = (b->samples[e->sample].size) & ~15u; v->step = e->step;
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

void sfx_play(int id, float pan, float volume) { sfx_play_bank(SFX_BANK_SYS, id, pan, volume); }

/* The room's footstep bank (type 8, FUN_001b0ab0; tools/gen_sfx.py room_footsteps): ids material x 10 + step. */
void sfx_room(int room) {
    Bank *b = &banks[BANK_ROOM];
    char file[32];
    int k;
    if (!ready || room == room_open) return;
    sceKernelWaitSema(lock, 1, 0);
    for (k = 0; k < VOICES; ++k) if (voices[k].active && b->sample_count) voices[k].active = 0;   /* they may read it */
    if (b->fd > 0) sceIoClose(b->fd);
    free(b->ids); free(b->samples); free(b->cached_at);
    memset(b, 0, sizeof *b);
    room_open = room;
    snprintf(file, sizeof file, "SFX_ST%03d.BIN", room);
    if (room < 0 || !bank_open(b, sfx_dir, file)) {
        if (b->fd > 0) sceIoClose(b->fd);
        free(b->ids); free(b->samples); free(b->cached_at);
        memset(b, 0, sizeof *b);
    }
    sceKernelSignalSema(lock, 1);
}
void sfx_floor(int material) { floor_material = material; }

/* FUN_0033ef00: a step of kind 0-3 plays rand(2) + 2 kind, kinds 4 and 5 ids 8 and 9, on the program of the floor's
 * material (none on material 0). */
static int steps_played, steps_missing, step_material;
void sfx_step_stats(int out[3]) { out[0] = steps_played; out[1] = step_material; out[2] = steps_missing; }
static void footstep(int kind) {
    int id;
    if (floor_material <= 0 || kind < 0 || kind > 5) { ++steps_missing; return; }
    ++steps_played; step_material = floor_material;   /* tests: missing below counts those the bank can't play */
    id = floor_material * 10 + (kind < 4 ? (int)(sceKernelGetSystemTimeLow() >> 4 & 1) + 2 * kind : kind + 4);
    if (id >= banks[BANK_ROOM].id_count || banks[BANK_ROOM].ids[id].sample == 0xffff) { ++steps_missing; return; }
    sfx_play_bank(BANK_ROOM, id, 0.0f, 1.0f);
}

void sfx_volume(float v) { master = v; }

/* A motion's sound events whose frames it passed this frame (after from, up to to; from < 0: it just began, so
 * those at to and before it too). */
void sfx_motion(int motion, float from, float to) {
    int k;
    if (!ready || motion < 0) return;
    if (!event_motion) {
        event_motion = malloc(sizeof(short) * SFX_EVENT_COUNT);
        if (!event_motion) return;
        for (k = 0; k < SFX_EVENT_COUNT; ++k) event_motion[k] = (short)motion_find(sfx_events[k].motion);
    }
    if (to < from) return;   /* it went back (a loop): nothing this time */
    for (k = 0; k < SFX_EVENT_COUNT; ++k) {
        float f = (float)sfx_events[k].frame;
        if (event_motion[k] == motion && f <= to && (from < 0.0f ? 1 : f > from)) {
            if (sfx_events[k].bank == STEP_EVENT) footstep(sfx_events[k].id);
            else sfx_play_bank(sfx_events[k].bank, sfx_events[k].id, 0.0f, 1.0f);
        }
    }
}
