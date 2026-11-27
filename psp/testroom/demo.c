/* The game's real-time cutscenes ("demos"; docs/CUTSCENES.md), as tools/prepare_demo.py bakes them: DEMO_nnn.BIN
 * holds the camera per demo frame (the camera track's clips, each a .cam played by CCameraClip / CCameraMotion:
 * eye = channels 6-8 + 0-2, target = 9-11 + 0-2, field of view = 12), the cast (each model slot's MOD, its motion
 * clips as raw MOT, its CnsPos parent bone) and their textures; DSTG_nnn.BIN the demo's stage built as a room.
 * The demo's demo_clock runs at 60 frames a second. Not yet: hide tracks, effects, messages, fades and sound. */
#include "demo.h"
#include "demo_cloth.h"

#include "model_blend.h"
#include "near_clip.h"
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
                 unsigned int subtitles, subtitle_count, fades, fade_count, fx_bank, fx_clips, fx_count,
                 light_count; /* low 16: Light clips, right after the fades. High 16: ScrEfc GFG clips,
                                  * after the Program colour clips (0 in a bin baked before fog). */ } DemoHeader;
/* A Light clip (tools/prepare_demo.py bake_lights): its start and frames, ValidType, IsMinus, Type 1 (eased Src to Dst). */
typedef struct { float start, frames; unsigned char valid, minus, ease, pad;
                 float src_pos[3], src_force, src_range, src_rgb[3], dst_pos[3], dst_force, dst_range, dst_rgb[3], pad2; } DemoLight;
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

/* The stage's hide track (prepare_demo: after the Program clips and the fog records, u32 count then {f32 frame, u16
 * object, u16 hidden}; object 0xffff shows all): each object's state now, onto the stage's batches (tagged object + 1).
 * +0x3c holds the light count in its low half and the fog count in its high half; a fog record is 24 bytes. */
#define STAGE_HIDE_OBJECTS 511   /* 0-254 the stage's SCM objects, 255-510 the room's props (prepare_demo) */
static void stage_hide_apply(int clear) {
    unsigned char want[STAGE_HIDE_OBJECTS];
    int k;
    memset(want, 0, sizeof want);
    if (!clear && head) {
        /* after the fades, lights (low half of light_count), Program clips and fogs (its high half) */
        const unsigned int *programs = (const unsigned int *)(blob + head->fades + head->fade_count * sizeof(DemoFade) +
                                                              (head->light_count & 0xffffu) * sizeof(DemoLight));
        const unsigned int *count = programs + 1 + *programs * 6 +   /* each Program clip is 24 bytes */
                                    (head->light_count >> 16) * 6;   /* each fog record is 24 bytes (DemoFog) */
        const struct { float frame; unsigned short obj, hidden; } *e = (const void *)(count + 1);
        unsigned int i;
        for (i = 0; i < *count && e[i].frame <= demo_clock; ++i) {
            if (e[i].obj == 0xffff) memset(want, 0, 255);   /* the SCM objects only, not the props */
            else if (e[i].obj == 0xfffe) memset(want, 1, 255);   /* HideKind H: the stage's objects hidden */
            else if (e[i].obj < STAGE_HIDE_OBJECTS) want[e[i].obj] = (unsigned char)e[i].hidden;
        }
    }
    for (k = 0; k < STAGE_HIDE_OBJECTS; ++k)
        stage_set_object_hidden(k + 1, want[k]);   /* every frame: a stage reload clears them */
}

void demo_stop(void) {
    stage_hide_apply(1);
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
int demo_hide_slot = -1;   /* test "demohide N": that model slot isn't drawn (which piece is which); -2: no effects */
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
    stage_hide_apply(0);
    if (demo_hide_slot != -2) demo_effects(dt);   /* test "demohide -2": no Effect track */
    cams = (const DemoCamera *)(blob + head->camera);
    f = (unsigned int)demo_clock;
    c = &cams[f];
    camera->x = c->eye[0]; camera->y = c->eye[1]; camera->z = c->eye[2];
    camera->tx = c->target[0]; camera->ty = c->target[1]; camera->tz = c->target[2];
    camera->fov = c->fov * (3.14159265f / 180.0f);   /* channel 12, degrees; the projection takes it as vertical */
    camera->roll = c->roll;   /* channel 13, already radians */
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

/* The motion clip a slot plays now: the one running, else the last one begun, held at its end (a clip's end doesn't
 * drop the model to its rest pose at the origin: demo 59's sword, wp122, keeps its place on the desk after its one
 * 154-frame clip; ss506_00 between its clips). -1 before its first. */
static int demo_clip_now(const Actor *a) {
    const DemoMotionClip *c = (const DemoMotionClip *)(blob + a->d->motion_at);
    int k, clip = -1, held = -1;
    float latest = -1.0f;
    for (k = 0; k < a->d->motions; ++k) {
        if (demo_clock >= c[k].set && demo_clock < c[k].end) clip = k;
        if (c[k].set <= demo_clock && c[k].set > latest) { latest = c[k].set; held = k; }
    }
    return clip >= 0 ? clip : held;
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
            int wanted = (demo_clock >= c[k].set - 40.0f && demo_clock < c[k].end) || k == demo_clip_now(a);
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
    clip = demo_clip_now(a);
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
        /* Gameplay body capsules (coat.c, FUN_00230130). The .clt names none. A coat on Dante's
         * 23-bone body still gets the spine and leg capsules, so the chains can't hang through him.
         * Other rigs are left alone. The flat sheet came from riding bone 0 across a cut, below. */
        ClothCapsule caps[6];
        int ncaps = 0;
        if (d->parent >= 0 && d->parent < actor_count && actors[d->parent].d && actors[d->parent].posed) {
            Actor *p = &actors[d->parent];
            /* Bone 0 stays at the origin in these demos. The hip channel (bone 1) is the body's world
             * pose, and a cut moves it by metres. Riding bone 0 leaves the chains where they were, and
             * the length clamp then lays them out along that jump: the coat becomes a flat sheet. The
             * hip's world matrix already includes bone 0, so the ride follows both. */
            const signed char *bp = (const signed char *)(blob + p->d->parents_at);
            int hip = (p->d->bones > 1 && bp[1] == 0) ? 1 : 0;
            memcpy(owner_pos, p->world_pos[hip], sizeof owner_pos);
            memcpy(owner_rot, p->world_rot[hip].m, sizeof owner_rot);
            op = owner_pos;
            orot = owner_rot;
        }
        if (d->parent >= 0 && d->parent < actor_count && actors[d->parent].d && actors[d->parent].posed) {
            Actor *body = &actors[d->parent];
            static const struct { int bone; float a[3], b[3], r; } src[] = {
                {3, {0.0f, 20.0f, 10.0f}, {0.0f, -40.0f, 10.0f}, 15.0f},
                {2, {0.0f, -5.0f, 0.0f}, {0.0f, -15.0f, 0.0f}, 18.0f},
                {15, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
                {16, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
                {19, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
                {20, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
            };
            int n;
            if (body->d->bones >= 23 && body->d->bones <= 24) {
                for (n = 0; n < 6; ++n) {
                    const float (*r)[3];
                    int k;
                    if (src[n].bone >= body->d->bones) continue;
                    r = body->world_rot[src[n].bone].m;
                    for (k = 0; k < 3; ++k) {
                        caps[ncaps].a[k] = body->world_pos[src[n].bone][k] + r[k][0] * src[n].a[0] + r[k][1] * src[n].a[1] + r[k][2] * src[n].a[2];
                        caps[ncaps].b[k] = body->world_pos[src[n].bone][k] + r[k][0] * src[n].b[0] + r[k][1] * src[n].b[1] + r[k][2] * src[n].b[2];
                    }
                    caps[ncaps].radius = src[n].r;
                    ncaps++;
                }
            }
        }
        demo_cloth_apply((int)(a - actors), base_pos, &base_rot.m[0][0], op, orot,
                         &a->world_pos[0][0], &a->world_rot[0].m[0][0],
                         parents, &local[0][0], d->bones, blob + d->pad[0], d->pad[1], demo_clock,
                         caps, ncaps);
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
static int sphere_visible(const ScePspFMatrix4 *m, const float c[3], float r, float scale) {
    float x = m->x.x * c[0] + m->y.x * c[1] + m->z.x * c[2] + m->w.x;
    float y = m->x.y * c[0] + m->y.y * c[1] + m->z.y * c[2] + m->w.y;
    float z = m->x.z * c[0] + m->y.z * c[1] + m->z.z * c[2] + m->w.z;
    float w = m->x.w * c[0] + m->y.w * c[1] + m->z.w * c[2] + m->w.w;
    /* r in clip units: the projection's largest scale (a narrow fov's close-up scales it up), with room to spare. */
    float s = r * (scale > 2.5f ? scale : 2.5f) * 1.25f;
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

/* The demo's lights on one actor (FUN_0027f230). A CDraw's light flag defaults to 2 (FUN_00281010, the word at
 * +1476). The model clip's Light key is the only writer (FUN_00219dc0, short at +1762; the update at 0x2366cc):
 * 0 leaves 2, 1 sets 2, 2 sets 1. Class 3 needs flag 1. No stage slot sets Light, and demos 3, 5 and 59 set none,
 * so the stage keeps flag 2 and these ValidType 3 clips reach no model. The draw ORs 0xC (FUN_001aa110) and still
 * does not add that bit. The stage mesh stays the baked colour (stage.c): multiplying these class-1 and class-2
 * lights onto it misses the PS2 stills (docs/CUTSCENES.md). Clips running now whose ValidType, less bit 4, is 1 or
 * 2; the three best by 1 - d^2 / Range^2 (a 0 range reaches everywhere); a ValidType 8 one (the scene slot) first.
 * Strength 1 - d^2 / Force^2 (a 0 force: whole) and colour / 128 [inferred: the VU1 lighting isn't read]. On the
 * PSP, per actor, a directional light from its centre. IsMinus lights are left out. Returns 0 when there are no
 * Light clips. */
#define DEMO_AMBIENT 0xff303030u   /* [stand-in, by eye] when the demo's Program track sets no No 6 */
typedef struct { float start; unsigned int no; float arg[4]; } DemoProgram;
/* ScrEfc GFG, after the Program clips: depth fog from MinAlpha/128 at StartZ to MaxAlpha/128 at EndZ. */
typedef struct { float start, life, z0, z1; unsigned int color; unsigned char min_a, max_a, type, pad; } DemoFog;
static unsigned int demo_light_n(void) { return head->light_count & 0xffffu; }
static const DemoProgram *demo_programs(unsigned int *count) {
    const unsigned char *p = blob + head->fades + head->fade_count * sizeof(DemoFade) + demo_light_n() * sizeof(DemoLight);
    *count = *(const unsigned int *)p;
    return (const DemoProgram *)(p + 4);
}
/* The Program track's colour clips in force now (prepare_demo: No 5 the characters' colour scale, 128 = 1.0; No 6
 * their ambient) [inferred from their values: demo 5 sets 128 128 128 and 30 20 20, demo 1 210 200 180]. */
static void demo_program_colour(float scale[3], float ambient[3], int *has_ambient) {
    const DemoProgram *p;
    unsigned int i, count;
    int k;
    for (k = 0; k < 3; ++k) { scale[k] = 1.0f; ambient[k] = 0.0f; }
    *has_ambient = 0;
    p = demo_programs(&count);
    for (i = 0; i < count && p[i].start <= demo_clock; ++i) {
        for (k = 0; k < 3; ++k) {
            if (p[i].no == 5) scale[k] = p[i].arg[k] / 128.0f;
            else ambient[k] = p[i].arg[k] / 128.0f;
        }
        if (p[i].no == 6) *has_ambient = 1;
    }
}
static unsigned int colour_word(const float rgb[3]) {
    unsigned int out = 0xff000000u;
    int k;
    for (k = 0; k < 3; ++k) out |= (unsigned int)(rgb[k] >= 1.0f ? 255.0f : rgb[k] <= 0.0f ? 0.0f : rgb[k] * 255.0f) << (8 * k);
    return out;
}
static int demo_lights(const float c[3]) {
    const DemoLight *l;
    struct { float dir[3], rgb[3], rank; } pick[4];
    float scale[3], ambient[3];
    int i, k, n = 0, special = -1, has_ambient;
    if (!demo_light_n()) return 0;
    demo_program_colour(scale, ambient, &has_ambient);
    l = (const DemoLight *)(blob + head->fades + head->fade_count * sizeof(DemoFade));
    for (i = 0; i < (int)demo_light_n(); ++i) {
        const DemoLight *x = &l[i];
        float t, pos[3], force, range, rgb[3], d[3], d2, rank, strength, len;
        int cls = x->valid & 0xfb;
        if (demo_clock < x->start || demo_clock >= x->start + x->frames) continue;
        if (!(x->valid & 8) && cls != 1 && cls != 2) continue;
        if (x->minus) continue;
        t = x->ease ? (demo_clock - x->start) / x->frames : 0.0f;
        for (k = 0; k < 3; ++k) {
            pos[k] = x->src_pos[k] + (x->dst_pos[k] - x->src_pos[k]) * t;
            rgb[k] = x->src_rgb[k] + (x->dst_rgb[k] - x->src_rgb[k]) * t;
            d[k] = pos[k] - c[k];
        }
        force = x->src_force + (x->dst_force - x->src_force) * t;
        range = x->src_range + (x->dst_range - x->src_range) * t;
        d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        rank = range > 0.0f ? 1.0f - d2 / (range * range) : 1.0f;
        strength = force > 0.0f ? 1.0f - d2 / (force * force) : 1.0f;
        if (rank <= 0.0f || strength <= 0.0f) continue;
        len = sqrtf(d2); if (len < 1.0f) len = 1.0f;
        {
            int slot = n < 4 ? n++ : 3;
            if (x->valid & 8) { special = slot; rank = 2.0f; }   /* the scene's slot goes first */
            else if (slot == 3 && pick[3].rank >= rank) continue;
            for (k = 0; k < 3; ++k) { pick[slot].dir[k] = d[k] / len; pick[slot].rgb[k] = rgb[k] / 128.0f * strength; }
            pick[slot].rank = rank;
            /* keep them by rank, best first */
            for (k = slot; k > 0 && pick[k].rank > pick[k - 1].rank; --k) {
                __typeof__(pick[0]) s = pick[k]; pick[k] = pick[k - 1]; pick[k - 1] = s;
            }
        }
    }
    (void)special;
    if (has_ambient) {
        float a[3];
        for (k = 0; k < 3; ++k) a[k] = ambient[k] * scale[k];
        sceGuAmbient(colour_word(a));
    } else sceGuAmbient(DEMO_AMBIENT);
    for (i = 0; i < 4; ++i) {
        if (i < 3 && i < n) {
            ScePspFVector3 dir = {pick[i].dir[0], pick[i].dir[1], pick[i].dir[2]};
            float rgb[3];
            for (k = 0; k < 3; ++k) rgb[k] = pick[i].rgb[k] * scale[k];
            sceGuLight(i, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
            sceGuLightColor(i, GU_DIFFUSE, colour_word(rgb));
            sceGuEnable(GU_LIGHT0 + i);
        } else {
            sceGuDisable(GU_LIGHT0 + i);
        }
    }
    return 1;
}

/* The demo's frame (demo.h). A cutscene's projection is channel 12's angle raw, as FUN_001ac750 builds it (1/tan of
 * half of it; the screen height cancels). A 326-line fit (subjects a fifth larger) was used while the PS2 stills it
 * was judged against were mismatched moments of a recording that drifts up to ~2 s from the demo clock; matched by
 * picture, the PS2 frames zoomed onto the port's need 1.17-1.21 with the fit and 1.00-1.02 raw (demos 3, 5, 59).
 * DEMO_FRAME_H 272 is the raw angle; test line "rawfov 1" still forces it. The bars leave 0.712 of the height
 * (measured in PCSX2, demos 3, 5, 59). */
#define DEMO_FRAME_H 272.0f
#define DEMO_SHOWN 0.712f
int demo_raw_fov;
int demo_no_fog;
float demo_screen_fov(float fov) {
    return 2.0f * atanf(tanf(fov * 0.0087266f) * 272.0f / DEMO_FRAME_H) * 57.29578f;
}

/* ScrEfc GFG on this frame. Strength is MinAlpha/128 at StartZ and MaxAlpha/128 at EndZ (128 = 1, as a fade's
 * alpha). sceGuFog reaches full colour at its far, so that far is where the same line would hit 1 [inferred:
 * the GS draw that would clamp past EndZ isn't what was read]. A flat strength uses a very long ramp. The sky
 * pass is left unfogged. "nofog 1" skips it. */
int demo_fog(void) {
    const DemoFog *f;
    unsigned int i, nprog, at;
    float a0, a1, z0, z1, span, slope, near, far;
    if (demo_no_fog || !playing || !head || !(head->light_count >> 16)) return 0;
    f = (const DemoFog *)(demo_programs(&nprog) + nprog);
    at = 0xffffffffu;
    for (i = 0; i < (head->light_count >> 16); ++i)
        if (demo_clock >= f[i].start && demo_clock < f[i].start + f[i].life) at = i;
    if (at == 0xffffffffu) return 0;
    a0 = (float)f[at].min_a * (1.0f / 128.0f);
    a1 = (float)f[at].max_a * (1.0f / 128.0f);
    if (a0 < 0.0f) a0 = 0.0f;
    if (a1 < 0.0f) a1 = 0.0f;
    if (a0 > 1.0f) a0 = 1.0f;
    if (a1 > 1.0f) a1 = 1.0f;
    if (a0 <= 0.0f && a1 <= 0.0f) return 0;
    z0 = f[at].z0;
    z1 = f[at].z1;
    if (z1 < z0) {
        float swap = z0;
        z0 = z1;
        z1 = swap;
        swap = a0;
        a0 = a1;
        a1 = swap;
    }
    span = z1 - z0;
    if (span < 1.0f) span = 1.0f;
    slope = (a1 - a0) / span;
    if (slope < 0.00002f) {
        float level = a0 > a1 ? a0 : a1;
        if (level <= 0.0f) return 0;
        if (level >= 0.999f) { near = 0.0f; far = 1.0f; }
        else {
            slope = (1.0f - level) / 200000.0f;
            near = z0 - level / slope;
            far = near + 1.0f / slope;
        }
    } else {
        near = z0 - a0 / slope;
        far = near + 1.0f / slope;
    }
    sceGuFog(near, far, (f[at].color & 0x00ffffffu) | 0xff000000u);
    return 1;
}

void demo_letterbox(void) {
    struct { unsigned int color; short x, y, z, pad; } *v;
    int bar = (int)(272.0f * (1.0f - DEMO_SHOWN) * 0.5f + 0.5f);
    if (!playing || bar <= 0) return;
    v = sceGuGetMemory(4 * sizeof(*v));
    v[0].color = v[1].color = v[2].color = v[3].color = 0xff000000u;
    v[0].x = 0; v[0].y = 0; v[1].x = 480; v[1].y = (short)bar;
    v[2].x = 0; v[2].y = (short)(272 - bar); v[3].x = 480; v[3].y = 272;
    v[0].z = v[1].z = v[2].z = v[3].z = 0;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0, v);
    sceGuEnable(GU_TEXTURE_2D);
}

/* Back to the room's lights (main.c's init: a key and a fill, ambient 0x90). */
static void demo_lights_off(void) {
    ScePspFVector3 key_dir = {0.45f, 0.8f, 0.4f}, fill_dir = {-0.45f, -0.3f, -0.4f};
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &key_dir);
    sceGuLightColor(0, GU_DIFFUSE, 0xffa0a0a0);
    sceGuLight(1, GU_DIRECTIONAL, GU_DIFFUSE, &fill_dir);
    sceGuLightColor(1, GU_DIFFUSE, 0xff505050);
    sceGuAmbient(0xff909090);
    sceGuEnable(GU_LIGHT0);
    sceGuEnable(GU_LIGHT1);
    sceGuDisable(GU_LIGHT2);
    sceGuDisable(GU_LIGHT3);
}

void demo_draw(void) {
    const DemoTexture *tex;
    int lit = 0;
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
    near_clip_begin();   /* close shots: the near plane's crossing triangles redrawn clipped (near_clip.h) */
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
        int near = 0;
        if (!a->d) continue;
        if (a->d->slot == demo_hide_slot || demo_hide_slot == -3) continue;   /* test "demohide N" (-3: all) */
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
            if (!sphere_visible(&clip, a->world_pos[cb], a->d->radius, proj.x.x > proj.y.y ? proj.x.x : proj.y.y)) continue;
            lit |= demo_lights(a->world_pos[cb]);
            near = near_clip_sphere(a->world_pos[cb], a->d->radius);   /* does the near plane reach it? */
        }
        skin(a);
        if (near) near = near_clip_prepare(a->skinned, (int)a->d->vertices);   /* 0: wholly on one side */
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
            /* The object's blend (its MOD record's byte 8, model_blend.h): 2 adds (eye highlights), 3 subtracts
             * (Arkham's veins), 4 subtracts the colour whole, else normal. Blending stays on in demos. */
            if (mode >= 2) model_object_blend(mode, 1);
            if (obj[k].texture & (1u << 20)) {
                /* An SCM piece's object: scenery, unlit at its baked colour (prepare_demo scm_rig), as the stage. */
                unsigned int g = obj[k].texture >> 24;
                sceGuDisable(GU_LIGHTING);
                sceGuColor(0xff000000u | g << 16 | g << 8 | g);
            }
            sceGumDrawArray(GU_TRIANGLES, DEMO_VTYPE, (int)obj[k].count, idx + obj[k].first, a->skinned);
            if (near) near_clip_draw(a->skinned, idx + obj[k].first, (int)obj[k].count, DEMO_VTYPE & ~GU_INDEX_16BIT);
            if (obj[k].texture & (1u << 20)) { sceGuEnable(GU_LIGHTING); sceGuColor(0xffffffffu); }
            if (mode >= 2) model_object_blend(0, 1);
        }
    }
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    if (lit) demo_lights_off();
}
