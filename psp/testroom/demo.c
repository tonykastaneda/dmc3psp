/* The game's real-time cutscenes ("demos"; docs/CUTSCENES.md), as tools/prepare_demo.py bakes them: DEMO_nnn.BIN
 * holds the camera per demo frame (the camera track's clips, each a .cam played by CCameraClip / CCameraMotion:
 * eye = channels 6-8 + 0-2, target = 9-11 + 0-2, field of view = 12), the cast (each model slot's MOD, its motion
 * clips as raw MOT, its CnsPos parent bone) and their textures; DSTG_nnn.BIN the demo's stage built as a room.
 * The demo's demo_clock runs at 60 frames a second. Not yet: hide tracks, cloth, effects, messages, fades and sound. */
#include "demo.h"

#include "motion.h"
#include "stage.h"

#include <malloc.h>
#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

typedef struct { char magic[4]; unsigned int frames, camera, textures, texture_count, actors, actor_count, reserved; } DemoHeader;
typedef struct { float eye[3], target[3], fov, roll; } DemoCamera;
typedef struct { unsigned int width, height, pixels, palette; } DemoTexture;
typedef struct {
    int slot, parent, parent_bone, bones, vertices, indices, objects, motions;
    unsigned int parents_at, local_at, rest_at, vertex_at, skin_at, index_at, object_at, motion_at;
    unsigned int centre_bone;   /* its bounding sphere: about this bone, */
    float radius;               /* this far */
    float pos[3], rot[3], shown_from, shown_to;
    unsigned int switches, switch_at;   /* its hide track: {f32 frame, u32 on} (DispOn / DispOff) */
    unsigned int pad[4];   /* 128 bytes, as written */
} DemoActorData;
typedef struct { float set, end, scale, skip; unsigned int mot, size; unsigned int pad[2]; } DemoMotionClip;
typedef struct { float u, v, nx, ny, nz, x, y, z; } DemoVertex;
typedef struct { unsigned char bone[3], weight[3], pad[2]; } DemoSkin;
typedef struct { unsigned int texture, first, count; } DemoObject;
typedef struct { float m[3][3]; } M3;

#define DEMO_MAX_ACTORS 40
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
static Actor actors[DEMO_MAX_ACTORS];
static int actor_count;
static float demo_clock;
static int playing, id = -1;
static char demo_path[256];   /* the DEMO file: its motions are read from it as they come up */
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
    memset(actors, 0, sizeof actors);
    actor_count = 0;
}

static int load_actors(void) {
    unsigned int i;
    int k;
    const DemoActorData *table = (const DemoActorData *)(blob + head->actors);
    for (i = 0; i < head->actor_count && i < DEMO_MAX_ACTORS; ++i) {
        Actor *a = &actors[actor_count];
        const DemoMotionClip *clips = (const DemoMotionClip *)(blob + table[i].motion_at);
        if (table[i].bones > DEMO_MAX_BONES) { continue; }
        a->d = &table[i];
        a->skinned = demo_alloc(sizeof(DemoVertex) * (unsigned int)table[i].vertices, &a->skinned_heap);
        a->motions = table[i].motions ? calloc((size_t)table[i].motions, sizeof(Motion)) : 0;
        a->motion_data = table[i].motions ? calloc((size_t)table[i].motions, sizeof(unsigned char *)) : 0;
        if (!a->skinned || (table[i].motions && (!a->motions || !a->motion_data))) {
            if (a->skinned_heap) free(a->skinned); free(a->motions); free(a->motion_data); continue;
        }
        memcpy(a->skinned, blob + table[i].vertex_at, sizeof(DemoVertex) * (size_t)table[i].vertices);
        (void)clips; (void)k;
        ++actor_count;
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
        if (got != (int)sizeof h || memcmp(h.magic, "DMO2", 4) != 0) return -2;
        blob = read_file(demo_path, &size, (int)h.reserved);
    }
    if (!blob) return read_error;
    head = (const DemoHeader *)blob;
    if (size < (int)sizeof(DemoHeader) || head->camera + head->frames * sizeof(DemoCamera) > (unsigned int)size) {
        demo_stop();
        return 0;
    }
    load_actors();   /* a demo without its own stage plays over the room's */
    demo_clock = 0.0f;
    playing = 1;
    id = demo;
    return 1;
}

void demo_stop(void) {
    free_actors();
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

int demo_update(float dt, FollowCamera *camera) {
    const DemoCamera *cams, *c;
    unsigned int f;
    if (!playing) return 0;
    demo_clock += dt * 60.0f;
    if (demo_clock >= (float)head->frames) { playing = 0; return 0; }
    demo_stream();
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
    fd = sceIoOpen(demo_path, PSP_O_RDONLY, 0);
    if (fd < 0) { free(data); return; }
    sceIoLseek(fd, (SceOff)c->mot, PSP_SEEK_SET);
    if (sceIoRead(fd, data, c->size) != (int)c->size || !motion_parse_blob("demo", data, c->size, &a->motions[k], 1)) {
        free(data);
        a->motions[k].channel_count = 0;
        sceIoClose(fd);
        return;
    }
    sceIoClose(fd);
    a->motion_data[k] = data;
}

/* Motions coming up within 20 frames are read now; those over are freed. */
static void demo_stream(void) {
    int i, k;
    for (i = 0; i < actor_count; ++i) {
        Actor *a = &actors[i];
        const DemoMotionClip *c = (const DemoMotionClip *)(blob + a->d->motion_at);
        for (k = 0; k < a->d->motions; ++k) {
            int wanted = demo_clock >= c[k].set - 20.0f && demo_clock < c[k].end;
            if (wanted && !a->motion_data[k]) load_motion(a, k);
            else if (!wanted && a->motion_data[k]) {
                free(a->motions[k].channels); a->motions[k].channels = 0; a->motions[k].channel_count = 0;
                free(a->motion_data[k]); a->motion_data[k] = 0;
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
    int i, k, clip = -1, attached = 0;
    if (a->posed) return;
    a->posed = 1;
    m_euler(&base_rot, d->rot[0], d->rot[1], d->rot[2]);
    if (d->parent >= 0 && d->parent < actor_count && depth < 4) {
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
    for (i = 0; i < d->bones; ++i) {
        float rot[3], t[3];
        int p = parents[i];
        for (k = 0; k < 3; ++k) {
            rot[k] = i < MOTION_MAX_BONES && s.has_rot[i][k] ? s.rot[i][k] : 0.0f;
            t[k] = i < MOTION_MAX_BONES && s.has_trans[i][k] ? s.trans[i][k] : local[i][k];
            if (attached && p < 0) t[k] = 0.0f;   /* its root is the parent's bone */
        }
        m_euler(&r, rot[0], rot[1], rot[2]);
        if (p < 0 || p >= i) {
            m_mul(&a->world_rot[i], &base_rot, &r);
            for (k = 0; k < 3; ++k)
                a->world_pos[i][k] = base_pos[k] + base_rot.m[k][0] * t[0] + base_rot.m[k][1] * t[1] + base_rot.m[k][2] * t[2];
        } else {
            m_mul(&a->world_rot[i], &a->world_rot[p], &r);
            for (k = 0; k < 3; ++k)
                a->world_pos[i][k] = a->world_pos[p][k] + a->world_rot[p].m[k][0] * t[0] + a->world_rot[p].m[k][1] * t[1] + a->world_rot[p].m[k][2] * t[2];
        }
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
    for (i = 0; i < actor_count; ++i) pose(&actors[i], 0);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0x40, 0xff);
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
        const DemoObject *obj = (const DemoObject *)(blob + a->d->object_at);
        const unsigned short *idx = (const unsigned short *)(blob + a->d->index_at);
        if (demo_clock < a->d->shown_from || demo_clock >= a->d->shown_to) continue;
        {   /* its hide track: the last switch at or before now */
            const struct { float frame; unsigned int on; } *sw = (const void *)(blob + a->d->switch_at);
            unsigned int n, on = 1;
            for (n = 0; n < a->d->switches && sw[n].frame <= demo_clock; ++n) on = sw[n].on;
            if (a->d->switches && sw[0].frame > demo_clock) on = 1;
            if (!on) continue;
        }
        {
            int cb = (int)a->d->centre_bone < a->d->bones ? (int)a->d->centre_bone : 0;
            if (!sphere_visible(&clip, a->world_pos[cb], a->d->radius)) continue;
        }
        skin(a);
        for (k = 0; k < a->d->objects; ++k) {
            if (obj[k].texture < head->texture_count && (int)obj[k].texture != bound) {
                const DemoTexture *t = &tex[obj[k].texture];
                bound = (int)obj[k].texture;
                sceGuClutLoad(256 / 8, blob + t->palette);
                sceGuTexImage(0, (int)t->width, (int)t->height, (int)t->width, blob + t->pixels);
                sceGuTexFlush();
            }
            sceGumDrawArray(GU_TRIANGLES, DEMO_VTYPE, (int)obj[k].count, idx + obj[k].first, a->skinned);
        }
    }
    sceGuDisable(GU_ALPHA_TEST);
}
