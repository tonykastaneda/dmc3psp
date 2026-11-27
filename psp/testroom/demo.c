/* The game's real-time cutscenes ("demos"; docs/CUTSCENES.md), as tools/prepare_demo.py bakes them: DEMO_nnn.BIN
 * holds the camera per demo frame (the camera track's clips, each a .cam played by CCameraClip / CCameraMotion:
 * eye = channels 6-8 + 0-2, target = 9-11 + 0-2, field of view = 12), the cast (each model slot's MOD, its motion
 * clips as raw MOT, its CnsPos parent bone) and their textures; DSTG_nnn.BIN the demo's stage built as a room.
 * The demo's demo_clock runs at 60 frames a second. Not yet: hide tracks, effects, messages, fades and sound. */
#include "demo.h"
#include "demo_cloth.h"

#include "motion.h"
#include "nfx.h"
#include "stage.h"

#include <malloc.h>
#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspaudio.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

typedef struct { char magic[4]; unsigned int frames, camera, textures, texture_count, actors, actor_count, reserved;
                 unsigned int subtitles, subtitle_count, fades, fade_count, fx_bank, fx_clips, fx_count, pad; } DemoHeader;
typedef struct { float start; unsigned int kind, id; float pos[3], rot[3], scale; int actor, bone; } DemoEffect;
typedef struct { float from, length; unsigned int out; unsigned char rgba[4]; } DemoFade;
typedef struct { float from, to; short x, y, size, pad; char text[112]; } DemoSubtitle;
typedef struct { float eye[3], target[3], fov, roll; } DemoCamera;
typedef struct { unsigned int width, height, pixels, palette; } DemoTexture;
typedef struct {
    int slot, parent, parent_bone, bones, vertices, indices, objects, motions;
    unsigned int parents_at, local_at, rest_at, vertex_at, skin_at, index_at, object_at, motion_at;
    unsigned int centre_bone;   /* its bounding sphere: about this bone, */
    float radius;               /* this far */
    float pos[3], rot[3], shown_from, shown_to;
    unsigned int switches, switch_at;   /* its hide track: {f32 frame, u32 on} (DispOn / DispOff) */
    unsigned int order_at;    /* a stage piece's (SCM) node order to pose in, parents first (u8 per bone; 0: 0, 1, ...) */
    unsigned int rest_rot_at; /* its nodes' rest rotations (f32 x3 per bone), where a motion doesn't key them (0: none) */
    unsigned int pad[2];   /* 128 bytes, as written */
} DemoActorData;
typedef struct { float set, end, scale, skip; unsigned int mot, size; unsigned int pad[2]; } DemoMotionClip;
typedef struct { float u, v, nx, ny, nz, x, y, z; } DemoVertex;
typedef struct { unsigned char bone[3], weight[3], pad[2]; } DemoSkin;
typedef struct { unsigned int texture, first, count; } DemoObject;
typedef struct { float start, scale, first; unsigned int mode, table, objects; } DemoHideClip;   /* 0 off, 1 on, 2 per object */
typedef struct { float m[3][3]; } M3;

#define DEMO_MAX_BONES 48
#define DEMO_VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

typedef struct {
    const DemoActorData *d;
    Motion *motions;           /* each clip's, parsed while it's loaded */
    unsigned char **motion_data;   /* each clip's MOT, read from the file as the clip comes up (0: not loaded) */
    DemoVertex *skinned;       /* drawn this frame */
    M3 world_rot[DEMO_MAX_BONES];
    float world_pos[DEMO_MAX_BONES][3];
    int posed, visible, skinned_heap;
} Actor;

static unsigned char *blob, *blob_raw;   /* blob: blob_raw aligned to 64 (blob_raw 0: blob is in the spare) */

/* The demo's own data goes in the stage buffer's unused tail (stage_spare: the buffer is sized for the largest room,
 * a cutscene's stage leaves room), the heap only past it: a room leaves under 2 MB of heap. */
static unsigned char *spare;
static unsigned int spare_left;
static void *demo_alloc(unsigned int size, int *heap) {
    unsigned int pad = (unsigned int)(((unsigned int)spare + 63u) & ~63u) - (unsigned int)spare;
    if (spare && pad + size <= spare_left) {
        void *p = spare + pad;
        spare += pad + size; spare_left -= pad + size;
        *heap = 0;
        return p;
    }
    *heap = 1;
    return memalign(64, size);
}

static const DemoHeader *head;
static Actor *actors;   /* one per row of the file's actor table (a row that didn't load: d 0), in the spare */
static int actors_heap;
static int actor_count;
static float demo_clock;
static int playing, id = -1;
/* The Effect tracks' effects running (nfx handles), those on a bone moved with it each frame. */
#define DEMO_FX_LIVE 32
static struct { int handle; const DemoEffect *e; } fx_live[DEMO_FX_LIVE];
static float fx_clock;   /* the demo clock the effects have been started up to */
static unsigned int demo_last_us;   /* the real time at the last update (0: none yet) */

/* ---- Its sound: DAUD_nnn.BIN (tools/prepare_demo.py bake_audio), the movies' IMA ADPCM, a byte per stereo sample
 * pair, streamed from the memory stick by its own thread from the Sound clip's frame. Both it and the demo clock run on
 * real time, so they keep together. */
#define SOUND_CHUNK 1024
static SceUID sound_fd = -1, sound_thread = -1;
static volatile int sound_on;
static int sound_reserved;
static unsigned int sound_left;
static float sound_start;
static int sound_predicted[2], sound_index[2];
static const short sound_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const signed char sound_index_step[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static short sound_decode(int ch, int nibble) {
    int step = sound_steps[sound_index[ch]], change = step >> 3;
    if (nibble & 4) change += step;
    if (nibble & 2) change += step >> 1;
    if (nibble & 1) change += step >> 2;
    sound_predicted[ch] += nibble & 8 ? -change : change;
    if (sound_predicted[ch] > 32767) sound_predicted[ch] = 32767;
    if (sound_predicted[ch] < -32768) sound_predicted[ch] = -32768;
    sound_index[ch] += sound_index_step[nibble & 7];
    if (sound_index[ch] < 0) sound_index[ch] = 0;
    if (sound_index[ch] > 88) sound_index[ch] = 88;
    return (short)sound_predicted[ch];
}
static int sound_main(SceSize args, void *argp) {
    static short __attribute__((aligned(64))) pcm[2][SOUND_CHUNK * 2];
    static unsigned char bytes[SOUND_CHUNK];
    int which = 0;
    (void)args; (void)argp;
    while (sound_on && demo_clock < sound_start) sceKernelDelayThread(2000);
    while (sound_on && sound_left) {
        unsigned int n = sound_left < SOUND_CHUNK ? sound_left : SOUND_CHUNK, i;
        short *out = pcm[which];
        int got = sceIoRead(sound_fd, bytes, n);
        which = !which;
        if (got <= 0) break;
        for (i = 0; i < SOUND_CHUNK; ++i) {
            if (i < (unsigned int)got) {
                out[i * 2] = sound_decode(0, bytes[i] & 15);
                out[i * 2 + 1] = sound_decode(1, bytes[i] >> 4);
            } else out[i * 2] = out[i * 2 + 1] = 0;
        }
        sound_left -= (unsigned int)got;
        if (sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, out) < 0) sceKernelDelayThread(20000);
    }
    return 0;
}
static void sound_stop(void) {
    if (sound_thread >= 0) {
        sound_on = 0;
        sceKernelWaitThreadEnd(sound_thread, NULL);
        sceKernelDeleteThread(sound_thread);
        sound_thread = -1;
    }
    if (sound_reserved) { while (sceAudioSRCChRelease() < 0) sceKernelDelayThread(1000); sound_reserved = 0; }
    if (sound_fd >= 0) { sceIoClose(sound_fd); sound_fd = -1; }
}
static void sound_begin(const char *dir, int demo) {
    char path[256];
    struct { char magic[4]; unsigned int rate, pairs; float start; } h;
    snprintf(path, sizeof path, "%s/DAUD_%03d.BIN", dir, demo);
    sound_fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (sound_fd < 0) return;
    if (sceIoRead(sound_fd, &h, sizeof h) != (int)sizeof h || memcmp(h.magic, "DAU1", 4) != 0 ||
        sceAudioSRCChReserve(SOUND_CHUNK, (int)h.rate, 2) < 0) { sceIoClose(sound_fd); sound_fd = -1; return; }
    sound_reserved = 1;
    sound_left = h.pairs;
    sound_start = h.start;
    sound_predicted[0] = sound_predicted[1] = sound_index[0] = sound_index[1] = 0;
    sound_on = 1;
    sound_thread = sceKernelCreateThread("demo sound", sound_main, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (sound_thread >= 0) sceKernelStartThread(sound_thread, 0, NULL);
    else sound_stop();
}
static char demo_path[256];   /* the DEMO file: its motions are read from it as they come up */
static SceUID demo_fd = -1;   /* kept open while it plays (opening it for each read cost a frame) */
static void demo_stream(void);

static int read_error;
/* The first `limit` bytes (all of it if 0). */
static void *read_file(const char *path, int *size_out, int limit) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int size, got;
    void *data;
    if (fd < 0) { read_error = -3; return 0; }
    size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    if (limit > 0 && limit < size) size = limit;
    {
        int heap;
        data = demo_alloc((unsigned int)size, &heap);
        blob_raw = heap ? data : 0;
    }
    if (!data) { sceIoClose(fd); read_error = -4; return 0; }
    got = sceIoRead(fd, data, (unsigned int)size);
    sceIoClose(fd);
    if (got != size) { if (blob_raw) free(blob_raw); blob_raw = 0; read_error = -5 - (got < 0 ? 0 : 1); return 0; }
    if (size_out) *size_out = size;
    return data;
}

static void free_actors(void) {
    int i, k;
    for (i = 0; i < actor_count; ++i) {
        if (!actors[i].d) continue;
        if (actors[i].motions) {
            for (k = 0; k < actors[i].d->motions; ++k) {
                free(actors[i].motions[k].channels);
                free(actors[i].motion_data[k]);
            }
            free(actors[i].motions);
            free(actors[i].motion_data);
        }
        if (actors[i].skinned_heap) free(actors[i].skinned);
    }
    if (actors && actors_heap) free(actors);
    actors = 0;
    actor_count = 0;
}

static int load_actors(void) {
    unsigned int i;
    int k;
    const DemoActorData *table = (const DemoActorData *)(blob + head->actors);
    actors = demo_alloc(sizeof(Actor) * head->actor_count, &actors_heap);
    if (!actors) return 0;
    memset(actors, 0, sizeof(Actor) * head->actor_count);
    actor_count = (int)head->actor_count;
    for (i = 0; i < head->actor_count; ++i) {
        Actor *a = &actors[i];
        const DemoMotionClip *clips = (const DemoMotionClip *)(blob + table[i].motion_at);
        if (table[i].bones > DEMO_MAX_BONES) { continue; }
        a->d = &table[i];
        a->skinned = demo_alloc(sizeof(DemoVertex) * (unsigned int)table[i].vertices, &a->skinned_heap);
        a->motions = table[i].motions ? calloc((size_t)table[i].motions, sizeof(Motion)) : 0;
        a->motion_data = table[i].motions ? calloc((size_t)table[i].motions, sizeof(unsigned char *)) : 0;
        if (!a->skinned || (table[i].motions && (!a->motions || !a->motion_data))) {
            if (a->skinned_heap) free(a->skinned);
            free(a->motions);
            free(a->motion_data);
            memset(a, 0, sizeof *a);   /* this row isn't cast */
            continue;
        }
        memcpy(a->skinned, blob + table[i].vertex_at, sizeof(DemoVertex) * (size_t)table[i].vertices);
        (void)clips; (void)k;
    }
    return actor_count;
}

int demo_start(int demo, const char *dir) {
    char path[256];
    int size = 0;
    demo_stop();
    snprintf(path, sizeof path, "%s/DSTG_%03d.BIN", dir, demo);
    {
        SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
        if (fd >= 0) { sceIoClose(fd); stage_load(path); }   /* its own stage, read into the room's buffer (stage.c) */
    }
    spare = stage_spare(&spare_left);
    snprintf(demo_path, sizeof demo_path, "%s/DEMO_%03d.BIN", dir, demo);
    {
        /* Its header says how much stays resident (all but the motions). */
        DemoHeader h;
        SceUID fd = sceIoOpen(demo_path, PSP_O_RDONLY, 0);
        int got;
        if (fd < 0) return -3;
        got = sceIoRead(fd, &h, sizeof h);
        sceIoClose(fd);
        if (got != (int)sizeof h || memcmp(h.magic, "DMO3", 4) != 0) return -2;
        blob = read_file(demo_path, &size, (int)h.reserved);
    }
    if (!blob) return read_error;
    head = (const DemoHeader *)blob;
    if (size < (int)sizeof(DemoHeader) || head->camera + head->frames * sizeof(DemoCamera) > (unsigned int)size) {
        demo_stop();
        return 0;
    }
    load_actors();   /* a demo without its own stage plays over the room's */
    nfx_reset();
    nfx_set_demo_bank(blob, head->fx_bank);
    memset(fx_live, 0, sizeof fx_live);
    fx_clock = -1.0f;
    demo_last_us = 0;
    sound_begin(dir, demo);
    demo_clock = 0.0f;
    playing = 1;
    id = demo;
    return 1;
}

void demo_stop(void) {
    sound_stop();
    demo_cloth_reset();
    if (blob) { nfx_reset(); nfx_set_demo_bank(0, 0); }
    free_actors();
    if (demo_fd >= 0) { sceIoClose(demo_fd); demo_fd = -1; }
    if (blob_raw) free(blob_raw);
    blob_raw = 0;
    spare = 0; spare_left = 0;
    blob = 0;
    head = 0;
    playing = 0;
    id = -1;
}

int demo_active(void) { return playing; }
int demo_id(void) { return id; }
float demo_frame(void) { return demo_clock; }
int demo_actor_count(void) { return actor_count; }

/* The Fade track: how much of its colour covers the screen now (the last fade begun: in, from it to clear; out, to
 * it and staying), as ABGR with alpha 0-255; 0 none. */
unsigned int demo_fade(void) {
    const DemoFade *f;
    unsigned int i, at = 0xffffffffu;
    float k;
    if (!playing || !head || !head->fade_count) return 0;
    f = (const DemoFade *)(blob + head->fades);
    for (i = 0; i < head->fade_count; ++i) if (f[i].from <= demo_clock) at = i;
    if (at == 0xffffffffu) return 0;
    k = (demo_clock - f[at].from) / f[at].length;
    if (k > 1.0f) k = 1.0f;
    if (!f[at].out) k = 1.0f - k;
    k *= (float)f[at].rgba[3] / 128.0f;
    if (k <= 0.0f) return 0;
    if (k > 1.0f) k = 1.0f;
    return (unsigned int)(k * 255.0f) << 24 | (unsigned int)f[at].rgba[2] << 16 | (unsigned int)f[at].rgba[1] << 8 | f[at].rgba[0];
}

/* The Message track's line now (in English: tools/prepare_demo.py), its place on the PS2's 640 x 448 screen. */
const char *demo_subtitle(int *x, int *y, int *size) {
    const DemoSubtitle *s;
    unsigned int i;
    if (!playing || !head || !head->subtitle_count) return 0;
    s = (const DemoSubtitle *)(blob + head->subtitles);
    for (i = 0; i < head->subtitle_count; ++i)
        if (demo_clock >= s[i].from && demo_clock < s[i].to) { *x = s[i].x; *y = s[i].y; *size = s[i].size; return s[i].text; }
    return 0;
}

/* Where an effect goes: its actor's bone (as posed last frame) plus its offset in that bone's space, or the world. */
static void effect_place(const DemoEffect *e, float out[3]) {
    int k;
    if (e->actor >= 0 && e->actor < actor_count && actors[e->actor].d && actors[e->actor].posed) {
        const Actor *a = &actors[e->actor];
        int b = e->bone >= 0 && e->bone < a->d->bones ? e->bone : 0;
        for (k = 0; k < 3; ++k)
            out[k] = a->world_pos[b][k] + a->world_rot[b].m[k][0] * e->pos[0] + a->world_rot[b].m[k][1] * e->pos[1] +
                     a->world_rot[b].m[k][2] * e->pos[2];
    } else {
        for (k = 0; k < 3; ++k) out[k] = e->pos[k];
    }
}

/* The Effect tracks' V, E, G and P effects (nfx's NFX_DEMO bank), started as the clock passes their frame. */
static void demo_effects(float dt) {
    const DemoEffect *fx;
    unsigned int i;
    int k;
    if (!head->fx_count) return;
    fx = (const DemoEffect *)(blob + head->fx_clips);
    for (i = 0; i < head->fx_count; ++i) {
        float at[3];
        int h;
        if (!(fx[i].start > fx_clock && fx[i].start <= demo_clock)) continue;
        effect_place(&fx[i], at);
        h = fx[i].kind == 2 ? nfx_spawn_p(NFX_DEMO, (int)fx[i].id, at, fx[i].rot[1], 0)   /* P: pfx.c */
          : fx[i].kind ? nfx_spawn_e(NFX_DEMO, (int)fx[i].id, at, fx[i].rot[1], 0)
                       : nfx_spawn(NFX_DEMO, (int)fx[i].id, at, fx[i].rot[1], 0);
        if (!h) continue;
        if (fx[i].scale > 0.0f && fx[i].scale != 1.0f) nfx_scale(h, fx[i].scale);
        if (fx[i].actor >= 0)
            for (k = 0; k < DEMO_FX_LIVE; ++k)
                if (!fx_live[k].handle || !nfx_alive(fx_live[k].handle)) { fx_live[k].handle = h; fx_live[k].e = &fx[i]; break; }
    }
    fx_clock = demo_clock;
    for (k = 0; k < DEMO_FX_LIVE; ++k) {
        float at[3];
        if (!fx_live[k].handle) continue;
        if (!nfx_alive(fx_live[k].handle)) { fx_live[k].handle = 0; continue; }
        effect_place(fx_live[k].e, at);
        nfx_move(fx_live[k].handle, at, fx_live[k].e->rot[1]);
    }
    nfx_update(dt * 60.0f);
}

int demo_update(float dt, FollowCamera *camera) {
    const DemoCamera *cams, *c;
    unsigned int f, now = sceKernelGetSystemTimeLow();
    if (!playing) return 0;
    /* Cutscenes always run at normal speed: their clock is the PSP's real time (60 demo frames a second), not the
     * game's dt (Turbo scales that; tests fix it), at most a 15th of a second a frame. */
    dt = demo_last_us ? (float)(now - demo_last_us) / 1000000.0f : 1.0f / 60.0f;
    if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
    demo_last_us = now;
    demo_clock += dt * 60.0f;
    if (demo_clock >= (float)head->frames) { playing = 0; return 0; }
    demo_stream();
    demo_effects(dt);
    cams = (const DemoCamera *)(blob + head->camera);
    f = (unsigned int)demo_clock;
    c = &cams[f];
    camera->x = c->eye[0]; camera->y = c->eye[1]; camera->z = c->eye[2];
    camera->tx = c->target[0]; camera->ty = c->target[1]; camera->tz = c->target[2];
    camera->fov = c->fov * (3.14159265f / 180.0f);   /* [inferred: vertical, as the port's] */
    camera->roll = 0.0f;
    return 1;
}

/* ---- The cast ---------------------------------------------------------------------------------------------- */
static void m_euler(M3 *o, float x, float y, float z) {   /* R = Rz * Ry * Rx, as motion.h says */
    float cx = cosf(x), sx = sinf(x), cy = cosf(y), sy = sinf(y), cz = cosf(z), sz = sinf(z);
    o->m[0][0] = cz * cy; o->m[0][1] = cz * sy * sx - sz * cx; o->m[0][2] = cz * sy * cx + sz * sx;
    o->m[1][0] = sz * cy; o->m[1][1] = sz * sy * sx + cz * cx; o->m[1][2] = sz * sy * cx - cz * sx;
    o->m[2][0] = -sy;     o->m[2][1] = cy * sx;                o->m[2][2] = cy * cx;
}
static void m_mul(M3 *o, const M3 *a, const M3 *b) {
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) o->m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
}

/* A clip's MOT, read from the file (as the clip comes up: demo_stream) and parsed. */
static void load_motion(Actor *a, int k) {
    const DemoMotionClip *c = (const DemoMotionClip *)(blob + a->d->motion_at) + k;
    SceUID fd;
    unsigned char *data;
    if (a->motion_data[k] || !c->size) return;
    data = malloc(c->size);
    if (!data) return;
    if (demo_fd < 0) demo_fd = sceIoOpen(demo_path, PSP_O_RDONLY, 0);
    fd = demo_fd;
    if (fd < 0) { free(data); return; }
    sceIoLseek(fd, (SceOff)c->mot, PSP_SEEK_SET);
    if (sceIoRead(fd, data, c->size) != (int)c->size || !motion_parse_blob("demo", data, c->size, &a->motions[k], 1)) {
        free(data);
        a->motions[k].channel_count = 0;
        return;
    }
    a->motion_data[k] = data;
}

static void demo_stream(void) {
    int i, k, loads = 0, best_i = -1, best_k = -1;
    float best = 1e9f;
    /* Frees what's over; then reads the clips coming up within 40 frames, the soonest first, one a frame (a
     * cut's dozen motions read in one frame missed a vblank or two). pose() still reads one that's due and missing. */
    for (i = 0; i < actor_count; ++i) {
        Actor *a = &actors[i];
        const DemoMotionClip *c;
        if (!a->d) continue;
        c = (const DemoMotionClip *)(blob + a->d->motion_at);
        for (k = 0; k < a->d->motions; ++k) {
            int wanted = demo_clock >= c[k].set - 40.0f && demo_clock < c[k].end;
            if (wanted && !a->motion_data[k] && c[k].set < best) { best = c[k].set; best_i = i; best_k = k; }
            else if (!wanted && a->motion_data[k]) {
                free(a->motions[k].channels); a->motions[k].channels = 0; a->motions[k].channel_count = 0;
                free(a->motion_data[k]); a->motion_data[k] = 0;
            }
        }
    }
    while (best_i >= 0 && loads < 1) {
        load_motion(&actors[best_i], best_k);
        ++loads;
        best_i = -1; best = 1e9f;
        for (i = 0; i < actor_count; ++i) {
            const DemoMotionClip *c;
            if (!actors[i].d) continue;
            c = (const DemoMotionClip *)(blob + actors[i].d->motion_at);
            for (k = 0; k < actors[i].d->motions; ++k)
                if (demo_clock >= c[k].set - 40.0f && demo_clock < c[k].end && !actors[i].motion_data[k] && c[k].set < best) {
                    best = c[k].set; best_i = i; best_k = k;
                }
        }
    }
}

static void pose(Actor *a, int depth) {
    static MotionSample s;
    const DemoActorData *d = a->d;
    const signed char *parents = (const signed char *)(blob + d->parents_at);
    const float (*local)[3] = (const float (*)[3])(blob + d->local_at);
    const DemoMotionClip *clips = (const DemoMotionClip *)(blob + d->motion_at);
    M3 base_rot, r;
    float base_pos[3] = {d->pos[0], d->pos[1], d->pos[2]};
    int i, j, k, clip = -1, attached = 0;
    const unsigned char *order = d->order_at ? blob + d->order_at : 0;
    const float (*rest_rot)[3] = d->rest_rot_at ? (const float (*)[3])(blob + d->rest_rot_at) : 0;
    if (a->posed) return;
    a->posed = 1;
    m_euler(&base_rot, d->rot[0], d->rot[1], d->rot[2]);
    if (d->parent >= 0 && d->parent < actor_count && actors[d->parent].d && depth < 4) {
        /* CnsPos: on another slot's bone. The piece's root takes that bone's place (its own rest offset dropped: a
         * head is modelled where the neck bone rests, a hand at the origin of its forearm bone), the clip's Pos / Rot
         * in that bone's space [inferred from the pieces' rests]. */
        Actor *p = &actors[d->parent];
        int b = d->parent_bone < p->d->bones ? d->parent_bone : 0;
        M3 t;
        pose(p, depth + 1);
        m_mul(&t, &p->world_rot[b], &base_rot);
        for (k = 0; k < 3; ++k)
            base_pos[k] = p->world_pos[b][k] + p->world_rot[b].m[k][0] * d->pos[0] + p->world_rot[b].m[k][1] * d->pos[1] + p->world_rot[b].m[k][2] * d->pos[2];
        base_rot = t;
        attached = 1;
    }
    for (k = 0; k < d->motions; ++k)
        if (demo_clock >= clips[k].set && demo_clock < clips[k].end) clip = k;
    if (clip >= 0 && !a->motion_data[clip]) load_motion(a, clip);
    if (clip >= 0 && a->motion_data[clip] && a->motions[clip].channel_count) {
        const Motion *m = &a->motions[clip];
        float local_frame = m->loop_start + clips[clip].skip + (demo_clock - clips[clip].set) / (clips[clip].scale > 0.0f ? clips[clip].scale : 1.0f);
        if (local_frame > m->end_frame) local_frame = m->end_frame;
        motion_sample(m, local_frame, &s);
    } else {
        memset(&s, 0, sizeof s);
    }
    for (j = 0; j < d->bones; ++j) {
        float rot[3], t[3];
        int p;
        i = order ? order[j] : j;
        p = parents[i];
        for (k = 0; k < 3; ++k) {
            rot[k] = i < MOTION_MAX_BONES && s.has_rot[i][k] ? s.rot[i][k] : rest_rot ? rest_rot[i][k] : 0.0f;
            t[k] = i < MOTION_MAX_BONES && s.has_trans[i][k] ? s.trans[i][k] : local[i][k];
            if (attached && p < 0) t[k] = 0.0f;   /* its root is the parent's bone */
        }
        m_euler(&r, rot[0], rot[1], rot[2]);
        if (p < 0 || p >= d->bones || (!order && p >= i)) {
            m_mul(&a->world_rot[i], &base_rot, &r);
            for (k = 0; k < 3; ++k)
                a->world_pos[i][k] = base_pos[k] + base_rot.m[k][0] * t[0] + base_rot.m[k][1] * t[1] + base_rot.m[k][2] * t[2];
        } else {
            m_mul(&a->world_rot[i], &a->world_rot[p], &r);
            for (k = 0; k < 3; ++k)
                a->world_pos[i][k] = a->world_pos[p][k] + a->world_rot[p].m[k][0] * t[0] + a->world_rot[p].m[k][1] * t[1] + a->world_rot[p].m[k][2] * t[2];
        }
    }
    /* Its .clt, if it has one. Joints are in world space; ClothMode 1 rides the parent model's root. */
    if (d->pad[0] && d->pad[1]) {
        float owner_pos[3], owner_rot[9];
        const float *op = base_pos, *orot = &base_rot.m[0][0];
        if (d->parent >= 0 && d->parent < actor_count && actors[d->parent].d && actors[d->parent].posed) {
            Actor *p = &actors[d->parent];
            memcpy(owner_pos, p->world_pos[0], sizeof owner_pos);
            memcpy(owner_rot, p->world_rot[0].m, sizeof owner_rot);
            op = owner_pos;
            orot = owner_rot;
        }
        demo_cloth_apply((int)(a - actors), base_pos, &base_rot.m[0][0], op, orot,
                         &a->world_pos[0][0], &a->world_rot[0].m[0][0],
                         parents, &local[0][0], d->bones, blob + d->pad[0], d->pad[1], demo_clock);
    }
}

static void skin(Actor *a) {
    const DemoActorData *d = a->d;
    const DemoVertex *src = (const DemoVertex *)(blob + d->vertex_at);
    const DemoSkin *sk = (const DemoSkin *)(blob + d->skin_at);
    const float (*rest)[3] = (const float (*)[3])(blob + d->rest_at);
    int i, k;
    for (i = 0; i < d->vertices; ++i) {
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (k = 0; k < 3; ++k) {
            int w = sk[i].weight[k], b = sk[i].bone[k];
            float wf, dx, dy, dz;
            const float (*r)[3];
            if (!w || b >= d->bones) continue;
            wf = (float)w * (1.0f / 31.0f);
            r = a->world_rot[b].m;
            dx = src[i].x - rest[b][0]; dy = src[i].y - rest[b][1]; dz = src[i].z - rest[b][2];
            px += wf * (r[0][0] * dx + r[0][1] * dy + r[0][2] * dz + a->world_pos[b][0]);
            py += wf * (r[1][0] * dx + r[1][1] * dy + r[1][2] * dz + a->world_pos[b][1]);
            pz += wf * (r[2][0] * dx + r[2][1] * dy + r[2][2] * dz + a->world_pos[b][2]);
            nx += wf * (r[0][0] * src[i].nx + r[0][1] * src[i].ny + r[0][2] * src[i].nz);
            ny += wf * (r[1][0] * src[i].nx + r[1][1] * src[i].ny + r[1][2] * src[i].nz);
            nz += wf * (r[2][0] * src[i].nx + r[2][1] * src[i].ny + r[2][2] * src[i].nz);
        }
        a->skinned[i].x = px; a->skinned[i].y = py; a->skinned[i].z = pz;
        a->skinned[i].nx = nx; a->skinned[i].ny = ny; a->skinned[i].nz = nz;
    }
    sceKernelDcacheWritebackRange(a->skinned, sizeof(DemoVertex) * (size_t)d->vertices);
}

/* Whether a sphere is in the view (clip = projection * view, read from GUM). */
static int sphere_visible(const ScePspFMatrix4 *m, const float c[3], float r) {
    float x = m->x.x * c[0] + m->y.x * c[1] + m->z.x * c[2] + m->w.x;
    float y = m->x.y * c[0] + m->y.y * c[1] + m->z.y * c[2] + m->w.y;
    float z = m->x.z * c[0] + m->y.z * c[1] + m->z.z * c[2] + m->w.z;
    float w = m->x.w * c[0] + m->y.w * c[1] + m->z.w * c[2] + m->w.w;
    /* r in clip units, loosely: the projection's largest scale. */
    float s = r * 2.5f;
    return w > -s && x > -w - s && x < w + s && y > -w - s && y < w + s && z < w + s;
}

/* A cut's hide channel for object k: its last key at or before the clip's own frame (stepped); shown without one. */
static int object_shown(const DemoHideClip *h, unsigned int k) {
    const unsigned int (*table)[2] = (const unsigned int (*)[2])(blob + h->table);
    const struct { float frame; unsigned int shown; } *keys;
    float local = h->first + (demo_clock - h->start) / (h->scale > 0.0f ? h->scale : 1.0f);
    unsigned int i, shown = 1;
    if (k >= h->objects || !table[k][0]) return 1;
    keys = (const void *)(blob + table[k][1]);
    for (i = 0; i < table[k][0] && keys[i].frame <= local; ++i) shown = keys[i].shown;
    if (keys[0].frame > local) shown = keys[0].shown;
    return (int)shown;
}

void demo_draw(void) {
    const DemoTexture *tex;
    int i, k, bound = -1;
    ScePspFMatrix4 view, proj, clip;
    if (!playing || !actor_count) return;
    sceGumMatrixMode(GU_VIEW); sceGumStoreMatrix(&view);
    sceGumMatrixMode(GU_PROJECTION); sceGumStoreMatrix(&proj);
    gumMultMatrix(&clip, &proj, &view);
    tex = (const DemoTexture *)(blob + head->textures);
    for (i = 0; i < actor_count; ++i) actors[i].posed = 0;
    for (i = 0; i < actor_count; ++i) if (actors[i].d) pose(&actors[i], 0);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0x08, 0xff);
    /* Blended: pieces carry smoke and glow textures (graded alpha) as the stages do; opaque texels are unchanged. */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuColor(0xffffffffu);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    for (i = 0; i < actor_count; ++i) {
        Actor *a = &actors[i];
        const DemoObject *obj;
        const unsigned short *idx;
        const DemoHideClip *hide;
        if (!a->d) continue;
        obj = (const DemoObject *)(blob + a->d->object_at);
        idx = (const unsigned short *)(blob + a->d->index_at);
        if (demo_clock < a->d->shown_from || demo_clock >= a->d->shown_to) continue;
        hide = 0;
        {   /* its hide track: the last clip begun (DispOff hides it all, a cut's own .hid object by object) */
            const DemoHideClip *sw = (const DemoHideClip *)(blob + a->d->switch_at);
            unsigned int n;
            for (n = 0; n < a->d->switches && sw[n].start <= demo_clock; ++n) hide = &sw[n];
            if (hide && hide->mode == 0) continue;
            if (hide && hide->mode != 2) hide = 0;
        }
        {
            int cb = (int)a->d->centre_bone < a->d->bones ? (int)a->d->centre_bone : 0;
            if (!sphere_visible(&clip, a->world_pos[cb], a->d->radius)) continue;
        }
        skin(a);
        for (k = 0; k < a->d->objects; ++k) {
            unsigned int texture = obj[k].texture & 0xffffu, mode = (obj[k].texture >> 16) & 0xfu;
            if (texture < head->texture_count && (int)texture != bound) {
                const DemoTexture *t = &tex[texture];
                bound = (int)texture;
                sceGuClutLoad(256 / 8, blob + t->palette);
                sceGuTexImage(0, (int)t->width, (int)t->height, (int)t->width, blob + t->pixels);
                sceGuTexFlush();
            }
            if (hide && !object_shown(hide, (unsigned int)k)) continue;
            /* The object's blend (its MOD record's byte 8, the GS ALPHA table 0x566ec0 [inferred]): 2 adds
             * (Cs*As + Cd, eye highlights), 3 subtracts (Cd - Cs*As, Arkham's veins), else normal. */
            if (mode == 2) sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffffu);
            else if (mode == 3) sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffffu);
            if (mode >= 2) sceGuDepthMask(GU_TRUE);   /* an overlay: it doesn't hide what's drawn after it */
            sceGumDrawArray(GU_TRIANGLES, DEMO_VTYPE, (int)obj[k].count, idx + obj[k].first, a->skinned);
            if (mode >= 2) { sceGuDepthMask(GU_FALSE); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
        }
    }
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
}
