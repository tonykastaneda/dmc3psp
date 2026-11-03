#include "enemy.h"

#include "fx.h"

#include <pspgu.h>
#include <pspgum.h>
#include <psputils.h>

#include <malloc.h>
#include <math.h>
#include <string.h>

#include "motion.h"
#include "player.h"

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "swizzle.h"
#include "model_runtime.h"   /* Dmc3Vec3 / Dmc3Uv / Dmc3ModelObject / Dmc3Texture types */
#include "enemy_runtime.h"

/* Motions (Seven Hells shared set), chosen from the data:
 *   hl-00 10-frame loop, hips steady          -> idle
 *   hl-37 33 frames, pushed back 21 cm         -> flinch (light hit)
 *   hl-11 knocked back 95 cm (then hl-13)      -> knock-back (heavy hit)
 *   hl-22 root rises 2.5 m in 41 frames        -> launched
 *   hl-23 looped, held in the air              -> airborne
 *   hl-24 19 frames, small pop                 -> air hit
 *   hl-55 looped, hips at 17 (lying)           -> down
 *   hl-20 hips 17 -> 119 over 60 frames        -> get up */
#define M_IDLE "hl-00"
#define M_FLINCH "hl-37"
#define M_KNOCK "hl-11"
#define M_KNOCK_RECOVER "hl-13"
#define M_LAUNCH "hl-22"
#define M_AIR "hl-23"
#define M_AIR_HIT "hl-24"
#define M_DOWN "hl-55"
#define M_GETUP "hl-20"

#define ENEMY_GRAVITY 900.0f       /* floatier than Dante: juggles hang */
#define LAUNCH_HANG 0.45f          /* seconds held at the top of a launch */
#define ENEMY_RADIUS 45.0f
#define AIR_POP_SPEED 380.0f
#define LAUNCH_HEIGHT 250.0f
#define DOWN_SECONDS 1.0f
#define HAND_BONE 9                 /* -X hand holds the scythe */
#define MAX_BONES 24

typedef struct { float u, v; float nx, ny, nz; float x, y, z; } Vertex;
typedef struct { float m[3][3]; } M3;

static int m_idle, m_flinch, m_knock, m_knock_recover, m_launch, m_air, m_air_hit, m_down, m_getup;
static Vertex *vertices, *weapon_vertices;
static unsigned short *indices, *weapon_indices;
static void *textures[ENEMY_TEXTURE_COUNT];
static int active;

static EnemyState state;
static float pos[3], yaw, vy, floor_y, state_time, frame;
static int motion = -1, hits;
static float launch_base;
/* Short cross-fade between motions. */
static int prev_motion = -1;
static float prev_frame, fade;

static M3 world_rot[MAX_BONES];
static float world_pos[MAX_BONES][3];

static void m_identity(M3 *m) { memset(m, 0, sizeof(*m)); m->m[0][0] = m->m[1][1] = m->m[2][2] = 1.0f; }
static void m_mul(M3 *o, const M3 *a, const M3 *b) {
    M3 r; int i, j;
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j)
        r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    *o = r;
}
static void m_rot(M3 *m, int axis, float a) {
    float c = cosf(a), s = sinf(a);
    m_identity(m);
    if (axis == 0) { m->m[1][1] = c; m->m[1][2] = -s; m->m[2][1] = s; m->m[2][2] = c; }
    else if (axis == 1) { m->m[0][0] = c; m->m[0][2] = s; m->m[2][0] = -s; m->m[2][2] = c; }
    else { m->m[0][0] = c; m->m[0][1] = -s; m->m[1][0] = s; m->m[1][1] = c; }
}
/* MOT order: X first, R = Rz * Ry * Rx (same as Dante). */
static void m_euler(M3 *o, float x, float y, float z) {
    M3 rx, ry, rz, t;
    m_rot(&rx, 0, x); m_rot(&ry, 1, y); m_rot(&rz, 2, z);
    m_mul(&t, &ry, &rx); m_mul(o, &rz, &t);
}

static void set_motion(int m, float f) {
    if (m == motion) return;
    prev_motion = motion;
    prev_frame = frame;
    fade = prev_motion >= 0 ? 1.0f : 0.0f;
    motion = m;
    frame = f;
}

int enemy_init(void) {
    int i;
    m_idle = motion_find(M_IDLE); m_flinch = motion_find(M_FLINCH); m_knock = motion_find(M_KNOCK);
    m_knock_recover = motion_find(M_KNOCK_RECOVER); m_launch = motion_find(M_LAUNCH); m_air = motion_find(M_AIR);
    m_air_hit = motion_find(M_AIR_HIT); m_down = motion_find(M_DOWN); m_getup = motion_find(M_GETUP);
    if (m_idle < 0 || m_flinch < 0 || m_knock < 0 || m_knock_recover < 0 || m_launch < 0 || m_air < 0 ||
        m_air_hit < 0 || m_down < 0 || m_getup < 0) return 0;
    vertices = memalign(16, sizeof(Vertex) * ENEMY_VERTEX_COUNT);
    indices = memalign(16, sizeof(unsigned short) * ENEMY_INDEX_COUNT);
    weapon_vertices = memalign(16, sizeof(Vertex) * ENEMY_WEAPON_VERTEX_COUNT);
    weapon_indices = memalign(16, sizeof(unsigned short) * ENEMY_WEAPON_INDEX_COUNT);
    if (!vertices || !indices || !weapon_vertices || !weapon_indices) return 0;
    memcpy(indices, enemy_indices, sizeof(unsigned short) * ENEMY_INDEX_COUNT);
    memcpy(weapon_indices, enemy_weapon_indices, sizeof(unsigned short) * ENEMY_WEAPON_INDEX_COUNT);
    for (i = 0; i < ENEMY_VERTEX_COUNT; ++i) { vertices[i].u = enemy_uvs[i].u; vertices[i].v = enemy_uvs[i].v; }
    for (i = 0; i < ENEMY_WEAPON_VERTEX_COUNT; ++i) {
        Vertex *v = &weapon_vertices[i];
        v->u = enemy_weapon_uvs[i].u; v->v = enemy_weapon_uvs[i].v;
        v->nx = enemy_weapon_normals[i].x; v->ny = enemy_weapon_normals[i].y; v->nz = enemy_weapon_normals[i].z;
        v->x = enemy_weapon_positions[i].x; v->y = enemy_weapon_positions[i].y; v->z = enemy_weapon_positions[i].z;
    }
    for (i = 0; i < ENEMY_TEXTURE_COUNT; ++i) {
        size_t bytes = (size_t)enemy_textures[i].width * enemy_textures[i].height * 4;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], enemy_textures[i].rgba, bytes);
        if (!swizzle_texture32(textures[i], enemy_textures[i].width, enemy_textures[i].height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}

static int floor_under(float x, float y, float z, float *out) {
    if (!player_floor_fn) { *out = 0.0f; return 1; }
    return player_floor_fn(x, y + 200.0f, z, 0.0f, out);
}

void enemy_spawn(float x, float z) {
    pos[0] = x; pos[2] = z;
    pos[1] = 0.0f;
    if (!floor_under(x, 2000.0f, z, &pos[1])) pos[1] = 0.0f;
    floor_y = pos[1];
    yaw = 0.0f;
    vy = 0.0f;
    state = ENEMY_IDLE;
    state_time = 0.0f;
    motion = -1;
    set_motion(m_idle, 0.0f);
    fade = 0.0f;
    active = 1;
}

int enemy_active(void) { return active; }
EnemyState enemy_state(void) { return state; }
void enemy_position(float out[3]) { memcpy(out, pos, sizeof(pos)); }
float enemy_height(void) { return pos[1] - floor_y; }
int enemy_hits_taken(void) { return hits; }

const char *enemy_state_name(void) {
    static const char *names[] = {"IDLE", "FLINCH", "KNOCKBACK", "LAUNCHED", "AIRBORNE", "DOWN", "GET UP"};
    return names[state];
}

static void enter(EnemyState s, int m) {
    state = s;
    state_time = 0.0f;
    set_motion(m, 0.0f);
}

void enemy_hit(HitKind kind, float from_x, float from_z) {
    if (!active) return;
    ++hits;  /* infinite health: hits only cause reactions */
    {
        /* On the side facing the attacker, at chest height. */
        float d[2] = {from_x - pos[0], from_z - pos[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1]);
        float at[3] = {pos[0], pos[1] + 110.0f, pos[2]};
        if (len > 1.0f) { at[0] += d[0] / len * 40.0f; at[2] += d[1] / len * 40.0f; }
        fx_hit_spark(at);
    }
    /* Face the attacker. */
    yaw = atan2f(from_x - pos[0], from_z - pos[2]);
    if (state == ENEMY_LAUNCHED || state == ENEMY_AIRBORNE) kind = kind == HIT_SLAM ? HIT_SLAM : HIT_AIR;
    switch (kind) {
    case HIT_LIGHT: if (state != ENEMY_DOWN && state != ENEMY_GETUP) enter(ENEMY_FLINCH, m_flinch); frame = 0.0f; break;
    case HIT_HEAVY: enter(ENEMY_KNOCKBACK, m_knock); frame = 0.0f; break;
    case HIT_LAUNCH:
        launch_base = floor_y;
        enter(ENEMY_LAUNCHED, m_launch);
        frame = 0.0f;
        break;
    case HIT_AIR:
        /* Juggle: pop up and hang. */
        vy = AIR_POP_SPEED;
        enter(ENEMY_AIRBORNE, m_air_hit);
        frame = 0.0f;
        break;
    case HIT_SLAM:
        vy = -1800.0f;
        enter(ENEMY_AIRBORNE, m_air_hit);
        break;
    }
}

void enemy_push_out(float *x, float *z, float r) {
    float dx, dz, d, min;
    if (!active || state == ENEMY_DOWN) return;
    dx = *x - pos[0]; dz = *z - pos[2];
    d = sqrtf(dx * dx + dz * dz);
    min = r + ENEMY_RADIUS;
    if (d < min && d > 1e-3f && fabsf(enemy_height()) < 150.0f) {
        *x = pos[0] + dx / d * min;
        *z = pos[2] + dz / d * min;
    }
}

void enemy_update(float dt) {
    const Motion *m;
    float root[3];
    if (!active) return;
    state_time += dt;
    if (fade > 0.0f) fade -= dt / 0.12f;
    if (fade < 0.0f) fade = 0.0f;
    frame += dt * MOTION_FPS;
    m = motion_get(motion);

    switch (state) {
    case ENEMY_IDLE:
        frame = motion_wrap(m, frame);
        break;
    case ENEMY_FLINCH:
        if (frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    case ENEMY_KNOCKBACK:
        /* Slide back along the hit direction, then recover. */
        if (motion == m_knock) {
            float push = 140.0f * dt;
            pos[0] -= sinf(yaw) * push; pos[2] -= cosf(yaw) * push;
            if (frame >= m->end_frame) set_motion(m_knock_recover, 0.0f);
        } else if (frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    case ENEMY_LAUNCHED:
        /* Height from the launch motion's root Y (2.5 m), then hang. */
        motion_root(m, frame < m->end_frame ? frame : m->end_frame, root);
        pos[1] = launch_base + root[1] * (LAUNCH_HEIGHT / 252.0f);
        if (frame >= m->end_frame) { vy = 0.0f; enter(ENEMY_AIRBORNE, m_air); }
        break;
    case ENEMY_AIRBORNE:
        if (motion == m_air_hit && frame >= m->end_frame) set_motion(m_air, 0.0f);
        if (motion == m_air) frame = motion_wrap(m, frame);
        if (state_time > LAUNCH_HANG || vy != 0.0f) vy -= ENEMY_GRAVITY * dt;
        pos[1] += vy * dt;
        if (pos[1] <= floor_y) { pos[1] = floor_y; vy = 0.0f; enter(ENEMY_DOWN, m_down); }
        break;
    case ENEMY_DOWN:
        frame = motion_wrap(m, frame);
        if (state_time > DOWN_SECONDS) enter(ENEMY_GETUP, m_getup);
        break;
    case ENEMY_GETUP:
        if (frame >= m->end_frame) enter(ENEMY_IDLE, m_idle);
        break;
    }
    /* Track the floor while grounded (knock-back slides). */
    if (state != ENEMY_LAUNCHED && state != ENEMY_AIRBORNE) {
        float g;
        if (floor_under(pos[0], pos[1], pos[2], &g)) floor_y = g;
        pos[1] = floor_y;
    }
}

/* Sample motion -> local rotations/offsets -> world transforms. */
static void evaluate_pose(void) {
    static MotionSample a, b;
    M3 local;
    int i, k;
    motion_sample(motion_get(motion), frame, &a);
    if (fade > 0.0f && prev_motion >= 0) motion_sample(motion_get(prev_motion), prev_frame, &b);
    for (i = 0; i < ENEMY_BONE_COUNT; ++i) {
        float r[3], t[3] = {enemy_bone_local[i].x, enemy_bone_local[i].y, enemy_bone_local[i].z};
        int p = enemy_bone_parent[i];
        for (k = 0; k < 3; ++k) {
            float ra = i < MOTION_MAX_BONES && a.has_rot[i][k] ? a.rot[i][k] : 0.0f;
            float ta = i < MOTION_MAX_BONES && a.has_trans[i][k] ? a.trans[i][k] : t[k];
            if (fade > 0.0f && prev_motion >= 0 && i < MOTION_MAX_BONES) {
                float rb = b.has_rot[i][k] ? b.rot[i][k] : ra;
                float tb = b.has_trans[i][k] ? b.trans[i][k] : ta;
                ra += (rb - ra) * fade;
                ta += (tb - ta) * fade;
            }
            r[k] = ra;
            t[k] = ta;
        }
        if (i == 0) t[0] = t[1] = t[2] = 0.0f;  /* root motion handled by the state machine */
        m_euler(&local, r[0], r[1], r[2]);
        if (p < 0) {
            world_rot[i] = local;
            memcpy(world_pos[i], t, sizeof(t));
        } else {
            m_mul(&world_rot[i], &world_rot[p], &local);
            for (k = 0; k < 3; ++k)
                world_pos[i][k] = world_pos[p][k] + world_rot[p].m[k][0] * t[0] + world_rot[p].m[k][1] * t[1] + world_rot[p].m[k][2] * t[2];
        }
    }
}

static void skin(void) {
    int i, k;
    for (i = 0; i < ENEMY_VERTEX_COUNT; ++i) {
        const Dmc3Vec3 *s = &enemy_positions[i], *n = &enemy_normals[i];
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (k = 0; k < 3; ++k) {
            int w = enemy_weights[i][k], b;
            float wf, dx, dy, dz;
            const float (*r)[3];
            if (!w) continue;
            b = enemy_bones[i][k];
            wf = (float)w * (1.0f / 31.0f);
            r = world_rot[b].m;
            dx = s->x - enemy_bone_rest[b].x; dy = s->y - enemy_bone_rest[b].y; dz = s->z - enemy_bone_rest[b].z;
            px += wf * (r[0][0] * dx + r[0][1] * dy + r[0][2] * dz + world_pos[b][0]);
            py += wf * (r[1][0] * dx + r[1][1] * dy + r[1][2] * dz + world_pos[b][1]);
            pz += wf * (r[2][0] * dx + r[2][1] * dy + r[2][2] * dz + world_pos[b][2]);
            nx += wf * (r[0][0] * n->x + r[0][1] * n->y + r[0][2] * n->z);
            ny += wf * (r[1][0] * n->x + r[1][1] * n->y + r[1][2] * n->z);
            nz += wf * (r[2][0] * n->x + r[2][1] * n->y + r[2][2] * n->z);
        }
        vertices[i].x = px; vertices[i].y = py; vertices[i].z = pz;
        vertices[i].nx = nx; vertices[i].ny = ny; vertices[i].nz = nz;
    }
    sceKernelDcacheWritebackRange(vertices, sizeof(Vertex) * ENEMY_VERTEX_COUNT);
}

#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

void enemy_draw(void) {
    int i, bound = -1;
    if (!active) return;
    evaluate_pose();
    skin();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {pos[0], pos[1], pos[2]};
        sceGumTranslate(&p);
        sceGumRotateY(yaw);
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 1);   /* swizzled at load */
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    for (i = 0; i < ENEMY_OBJECT_COUNT; ++i) {
        const Dmc3ModelObject *o = &enemy_objects[i];
        if ((int)o->texture != bound) {
            bound = (int)o->texture;
            sceGuTexImage(0, enemy_textures[bound].width, enemy_textures[bound].height, enemy_textures[bound].width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices + o->index_start, vertices);
    }
    {
        /* Scythe in the -X hand. */
        ScePspFMatrix4 mm;
        const float (*r)[3] = world_rot[HAND_BONE].m;
        mm.x.x = r[0][0]; mm.x.y = r[1][0]; mm.x.z = r[2][0]; mm.x.w = 0.0f;
        mm.y.x = r[0][1]; mm.y.y = r[1][1]; mm.y.z = r[2][1]; mm.y.w = 0.0f;
        mm.z.x = r[0][2]; mm.z.y = r[1][2]; mm.z.z = r[2][2]; mm.z.w = 0.0f;
        mm.w.x = world_pos[HAND_BONE][0]; mm.w.y = world_pos[HAND_BONE][1]; mm.w.z = world_pos[HAND_BONE][2]; mm.w.w = 1.0f;
        sceGumPushMatrix();
        sceGumMultMatrix(&mm);
        bound = ENEMY_WEAPON_TEXTURE;
        sceGuTexImage(0, enemy_textures[bound].width, enemy_textures[bound].height, enemy_textures[bound].width, textures[bound]);
        sceGuTexFlush();
        sceGumDrawArray(GU_TRIANGLES, VTYPE, ENEMY_WEAPON_INDEX_COUNT, weapon_indices, weapon_vertices);
        sceGumPopMatrix();
    }
}
