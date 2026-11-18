/* The missions' music (docs/MUSIC.md): two stream channels, as the game's event op 0x70 drives them (FUN_0032cef0), set
 * per room from tools/gen_music.py's table and played from BGM_nnn.BIN (nnn: the track's SDATA.AFS index; the movies'
 * IMA ADPCM, looping) by a thread each on a hardware channel.
 *
 * Commands, per channel: 0 nothing; 1 / 2 stop; 3-6 fade out; 12 hold the track until a fight (state 5), then fade it
 * in, and out again once the room is clear [the deferred states' handling: inferred from the scripts, where channel 0
 * always holds the room's battle track this way]; the rest play it (7, 8 at once; 9-11, 13-16 fading in, keeping it if
 * it's already on). The other channel (the stage or its ambience) dips while battle music plays [inferred]. Everything
 * is silent while a cutscene or movie plays (music_mute). */
#include "music.h"

#include <pspaudio.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

#include "music_runtime.h"

#define CHUNK 1024
#define FADE_SECONDS 1.5f

typedef struct {
    int ch;                     /* hardware channel, -1 none */
    SceUID thread;
    volatile int track;         /* SDATA index playing (-1 none) */
    volatile int want;          /* the track it should be playing (-1 none): the thread switches */
    volatile float volume;      /* 0-1, set by music_update */
    float target;
    int hold;                   /* command 12: waits for a fight */
    int predicted[2], index[2];
    SceUID fd;
    unsigned int pairs, left;
    unsigned char bytes[CHUNK];
    short __attribute__((aligned(64))) pcm[2][CHUNK * 2];
} Channel;

static Channel channels[2];
static volatile int running;
static int muted, ready;
static unsigned int last_us;
static char dir[256];

static const short steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const signed char index_step[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static short decode(Channel *c, int s, int nibble) {
    int step = steps[c->index[s]], change = step >> 3;
    if (nibble & 4) change += step;
    if (nibble & 2) change += step >> 1;
    if (nibble & 1) change += step >> 2;
    c->predicted[s] += nibble & 8 ? -change : change;
    if (c->predicted[s] > 32767) c->predicted[s] = 32767;
    if (c->predicted[s] < -32768) c->predicted[s] = -32768;
    c->index[s] += index_step[nibble & 7];
    if (c->index[s] < 0) c->index[s] = 0;
    if (c->index[s] > 88) c->index[s] = 88;
    return (short)c->predicted[s];
}

static void rewind_track(Channel *c) {
    sceIoLseek(c->fd, 12, PSP_SEEK_SET);
    c->left = c->pairs;
    c->predicted[0] = c->predicted[1] = c->index[0] = c->index[1] = 0;
}

static void open_track(Channel *c, int track) {
    char path[300];
    struct { char magic[4]; unsigned int rate, pairs; } h;
    if (c->fd >= 0) { sceIoClose(c->fd); c->fd = -1; }
    c->track = -1;
    if (track < 0) return;
    snprintf(path, sizeof path, "%s/BGM_%03d.BIN", dir, track);
    c->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (c->fd < 0) return;
    if (sceIoRead(c->fd, &h, sizeof h) != (int)sizeof h || memcmp(h.magic, "BGM1", 4) != 0 || !h.pairs) {
        sceIoClose(c->fd); c->fd = -1; return;
    }
    c->pairs = h.pairs;
    rewind_track(c);
    c->track = track;
}

static int channel_main(SceSize args, void *argp) {
    Channel *c = *(Channel **)argp;
    int which = 0;
    (void)args;
    while (running) {
        short *out = c->pcm[which];
        float v = c->volume;
        int i, got = 0;
        which = !which;
        if (c->want != c->track) open_track(c, c->want);
        if (c->fd >= 0 && v > 0.001f) {   /* silent: the track waits where it is */
            unsigned int n = c->left < CHUNK ? c->left : CHUNK;
            got = sceIoRead(c->fd, c->bytes, n);
            if (got < 0) got = 0;
            c->left -= (unsigned int)got;
            if (!c->left) rewind_track(c);   /* it loops */
        }
        for (i = 0; i < CHUNK; ++i) {
            if (i < got) {
                out[i * 2] = decode(c, 0, c->bytes[i] & 15);
                out[i * 2 + 1] = decode(c, 1, c->bytes[i] >> 4);
            } else out[i * 2] = out[i * 2 + 1] = 0;
        }
        {
            int level = (int)(v * (float)PSP_AUDIO_VOLUME_MAX * 0.8f);
            if (sceAudioOutputPannedBlocking(c->ch, level, level, out) < 0) sceKernelDelayThread(20000);
        }
    }
    return 0;
}

void music_init(const char *game_dir) {
    int k;
    if (ready) return;
    snprintf(dir, sizeof dir, "%s", game_dir);
    if (dir[0] && dir[strlen(dir) - 1] == '/') dir[strlen(dir) - 1] = 0;
    running = 1;
    for (k = 0; k < 2; ++k) {
        Channel *c = &channels[k];
        memset(c, 0, sizeof *c);
        c->fd = -1; c->track = c->want = -1;
        c->ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, CHUNK, PSP_AUDIO_FORMAT_STEREO);
        if (c->ch < 0) continue;
        c->thread = sceKernelCreateThread(k ? "music 1" : "music 0", channel_main, 0x13, 0x4000, PSP_THREAD_ATTR_USER, NULL);
        if (c->thread >= 0) { Channel *p = c; sceKernelStartThread(c->thread, sizeof p, &p); }
    }
    ready = 1;
}

/* A room's on-enter setting (its mission's event script, op 0x70). */
void music_room(int mission, int room) {
    int i, k;
    if (!ready) return;
    for (i = 0; i < MUSIC_ROOM_COUNT; ++i) {
        const MusicRoom *r = &music_rooms[i];
        if (r->mission != mission || r->room != room) continue;
        for (k = 0; k < 2; ++k) {
            Channel *c = &channels[k];
            int cmd = r->command[k];
            if (!cmd) continue;
            if (cmd <= 2) { c->target = 0.0f; c->hold = 0; c->want = -1; c->volume = 0.0f; }
            else if (cmd <= 6) { c->target = 0.0f; c->hold = 0; }
            else {
                if (c->want != r->track[k]) { c->want = r->track[k]; c->volume = cmd <= 8 ? 1.0f : 0.0f; }
                c->hold = cmd == 12;
                c->target = c->hold ? 0.0f : 1.0f;
            }
        }
        return;
    }
}

void music_mute(int on) {
    int k;
    muted = on;
    if (on) for (k = 0; k < 2; ++k) channels[k].volume = 0.0f;   /* at once: a movie's sound takes over */
}

void music_stop(void) {
    int k;
    for (k = 0; k < 2; ++k) { channels[k].target = 0.0f; channels[k].hold = 0; channels[k].want = -1; channels[k].volume = 0.0f; }
}

/* Each frame: a held battle track comes in while there's a fight, and the stage channel dips under it. */
void music_update(int fight) {
    unsigned int now = sceKernelGetSystemTimeLow();
    float dt = last_us ? (float)(now - last_us) / 1000000.0f : 0.0f, step;
    int k, battle;
    last_us = now;
    if (!ready) return;
    if (dt > 0.1f) dt = 0.1f;
    step = dt / FADE_SECONDS;
    battle = fight && channels[0].hold && channels[0].want >= 0;
    for (k = 0; k < 2; ++k) {
        Channel *c = &channels[k];
        float target = c->hold ? (fight ? 1.0f : 0.0f) : c->target;
        if (k == 1 && battle) target *= 0.15f;
        if (muted) target = 0.0f;
        if (c->volume < target) { c->volume += step; if (c->volume > target) c->volume = target; }
        else if (c->volume > target) { c->volume -= muted ? step * 4.0f : step; if (c->volume < target) c->volume = target; }
    }
}
