#include "doppelganger_boss.h"

#include "enemy.h"
#include "fx.h"
#include "motion.h"
#include "swizzle.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "boss_doppel_runtime.h"

/* CEm034 at AI rank 0. Frames and distances keep the original 60 Hz / centimetre units. Addresses are
 * SLUS_213.61's; see docs/DOPPELGANGER_SPEC.md. */
#define PI 3.14159265358979323846f
#define MAX_HEALTH 10000.0f           /* param block 0x5c2ab0 +0x2c (the reading Vergil's 8000 and Cerberus's use) */
#define DANTE_RADIUS 40.0f
#define DANTE_LOW 20.0f
#define DANTE_HIGH 160.0f
#define BODY_RADIUS 45.0f
#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

extern unsigned int enemy_ai_random(void);
extern int testroom_frame;

enum { PART_BODY, PART_COAT, PART_SWORD };
/* The states (+0x20d0, FUN_003be4f0's switch), by what they do. */
enum {
    S_START = 1, S_THINK = 2, S_WALK = 3, S_RUN = 4, S_COMBO = 5, S_COMBO2 = 8, S_COMBO3 = 10,
    S_METEOR = 0x0e, S_BLAST = 0x10, S_APPROACH = 0x17, S_STINGER = 0x1b, S_CREST_BLAST = 0x1d, S_SINK = 0x1e,
    S_EMERGE = 0x1f, S_ALL_LIT = 0x23, S_CREST_LIT = 0x24, S_STAGGER = 0x40, S_DEATH = 0x41
};

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;

/* The crests (room 139's # GAME: BROCKEN 0-5, hit sphere 160 at y 225; crest 2 starts open, lightflg 1). */
static const float crest_pos[6][2] = {
    {2500.0f, 1350.0f}, {3496.0f, 1925.0f}, {3496.0f, 3075.0f}, {2500.0f, 3650.0f}, {1504.0f, 3075.0f}, {1504.0f, 1925.0f}
};
#define CREST_Y 225.0f
#define CREST_R 160.0f
#define CENTRE_X 2500.0f
#define CENTRE_Z 2500.0f
#define CENTRE_R 550.0f               /* [estimate] inside this the middle region (7th, lit only with all six) */
static const short opposite[6] = {3, 4, 5, 0, 1, 2};   /* DAT_005b7a30 */

static struct {
    int active, dying, hidden, state, step, motion_slot, rank, lit[7], landed, landed_heavy, hit_done, flee_to;
    float pos[3], yaw, hp, timer, frame, floor_y, speed, landed_x, landed_z, landed_damage, stagger_cool, think_time;
    float dante[3], dante_yaw, previous_frame, fade;
    int motion, previous_motion, tick;
    float target[3];
} db;

static int motion_ids[34];
static Vertex *vertices[DOPPEL_PART_COUNT];
static unsigned short *indices[DOPPEL_PART_COUNT];
static void *textures[DOPPEL_TEXTURE_COUNT];
static Transform body_pose[24], coat_pose[9], sword_pose[1];
static char trace_path[320];
static int trace_on, boss_difficulty = 1;
static int (*floor_fn)(float x, float y, float z, float *out);

void doppelganger_boss_set_difficulty(int difficulty) { boss_difficulty = difficulty; }
void doppelganger_boss_set_floor(int (*fn)(float x, float y, float z, float *out)) { floor_fn = fn; }

static float wrap_angle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static float unit_random(void) { return (float)enemy_ai_random() * 2.3283064e-10f; }
static float dist2(const float a[3], const float b[3]) { float dx = a[0] - b[0], dz = a[2] - b[2]; return sqrtf(dx * dx + dz * dz); }
static float angle_to(const float at[3]) { return atan2f(at[0] - db.pos[0], at[2] - db.pos[2]); }
static void turn_toward(float yaw, float rate) {
    float d = wrap_angle(yaw - db.yaw);
    db.yaw = wrap_angle(db.yaw + fmaxf(-rate, fminf(rate, d)));
}
static void trace(const char *event) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "t=%d f=%d %s state=%#x step=%d motion=%d frame=%.1f hp=%.0f lit=%d%d%d%d%d%d%d region=%d hidden=%d pos=%.0f,%.0f,%.0f d=%.0f\n",
            db.tick, testroom_frame, event, db.state, db.step, db.motion_slot, db.frame, db.hp, db.lit[0], db.lit[1], db.lit[2],
            db.lit[3], db.lit[4], db.lit[5], db.lit[6], -1, db.hidden, db.pos[0], db.pos[1], db.pos[2], dist2(db.pos, db.dante));
    fclose(f);
}
void doppelganger_boss_trace(const char *path) {
    trace_on = path && *path;
    if (trace_on) snprintf(trace_path, sizeof(trace_path), "%s", path);
}

static void set_state(int state);

/* ---- Light ---------------------------------------------------------------------------------------------- */
/* Which sixth of the arena a point is in (the crest it's nearest by angle round the centre), or 6, the middle. */
static int region(float x, float z) {
    float dx = x - CENTRE_X, dz = z - CENTRE_Z, best = 1e9f;
    int i, r = 6;
    if (dx * dx + dz * dz < CENTRE_R * CENTRE_R) return 6;
    for (i = 0; i < 6; ++i) {
        float cx = crest_pos[i][0] - CENTRE_X, cz = crest_pos[i][1] - CENTRE_Z;
        float d = fabsf(wrap_angle(atan2f(dx, dz) - atan2f(cx, cz)));
        if (d < best) { best = d; r = i; }
    }
    return r;
}
static void update_middle(void) {
    int i, all = 1;
    for (i = 0; i < 6; ++i) all &= db.lit[i];
    db.lit[6] = all;
}
static int in_light(void) { return db.lit[region(db.pos[0], db.pos[2])]; }

void doppelganger_boss_strike(float x, float y, float z, float yaw, float reach) {
    int i;
    if (!db.active || db.dying) return;
    for (i = 0; i < 6; ++i) {
        float dx = crest_pos[i][0] - x, dz = crest_pos[i][1] - z, d = sqrtf(dx * dx + dz * dz), dy = CREST_Y - y;
        if (db.lit[i] || d > reach + CREST_R || dy < -150.0f || dy > 300.0f) continue;
        if (d > 1.0f && (dx * sinf(yaw) + dz * cosf(yaw)) / d < 0.2f) continue;
        {
            float at[3] = {crest_pos[i][0], CREST_Y, crest_pos[i][1]};
            fx_explosion(at);
        }
        db.lit[i] = 1;   /* the shutter opens: its LIGHT set shines on that sixth */
        update_middle();
        trace("crest open");
        /* Caught in it, he's stunned (the walkthrough; states 0x23/0x24 follow the light) [the stun's form is the
         * port's: a reeling motion, hittable] */
        if (!db.hidden && db.lit[region(db.pos[0], db.pos[2])] && db.state != S_STAGGER) set_state(S_STAGGER);
    }
}
static void put_out(int crest) {
    float at[3];
    if (crest < 0 || crest > 5 || !db.lit[crest]) return;
    at[0] = crest_pos[crest][0]; at[1] = CREST_Y; at[2] = crest_pos[crest][1];
    fx_hit_spark(at);
    db.lit[crest] = 0;
    update_middle();
    trace("crest shut");
}

/* ---- Motions: script ids from em034 [7]; motion n is PAC [5] slot n ------------------------------------- */
static void play(int slot) {
    db.previous_motion = db.motion;
    db.previous_frame = db.frame;
    db.fade = db.motion >= 0 ? 1.0f : 0.0f;
    db.motion_slot = slot;
    db.motion = slot >= 0 && slot < 34 ? motion_ids[slot] : -1;
    db.frame = 0.0f;
}
static int motion_done(void) {
    const Motion *m = motion_get(db.motion);
    return !m || db.frame >= m->end_frame;
}
static int loops(int slot) { return slot <= 2; }   /* 0:0-0:2: idle, walk, run */
static void set_state(int state) {
    db.state = state;
    db.step = 0;
    db.timer = 0.0f;
    db.hit_done = 0;
    trace("state");
}

/* ---- Hits on Dante (the scripts' attack records 0-4, 7, 0xb; their damage isn't read) -------------------- */
static void swing(float reach, float damage, int heavy) {
    float hx = db.pos[0] + sinf(db.yaw) * 80.0f - db.dante[0], hz = db.pos[2] + cosf(db.yaw) * 80.0f - db.dante[2];
    if (db.landed || db.hit_done) return;
    db.hit_done = 1;
    if (hx * hx + hz * hz > reach * reach || fabsf(db.dante[1] - db.pos[1]) > 200.0f) return;
    db.landed = 1;
    db.landed_x = db.pos[0]; db.landed_z = db.pos[2];
    db.landed_damage = damage;
    db.landed_heavy = heavy;
    trace("landed");
}
static void move_forward(float speed, float f) {
    db.pos[0] += sinf(db.yaw) * speed * f;
    db.pos[2] += cosf(db.yaw) * speed * f;
}
/* A dark place to come up in: the sixth behind Dante if it's dark, else any dark one [inferred: FUN_003c4dd0 picks
 * the nearest dark region]. */
static int dark_region_near(float x, float z) {
    int i, best = -1;
    float bd = 1e9f;
    for (i = 0; i < 6; ++i) {
        float dx = crest_pos[i][0] - x, dz = crest_pos[i][1] - z, d = dx * dx + dz * dz;
        if (!db.lit[i] && d < bd) { bd = d; best = i; }
    }
    return best;
}
static void point_in_region(int r, float out[3]) {
    if (r < 0 || r > 5) { out[0] = CENTRE_X; out[2] = CENTRE_Z; }
    else {
        out[0] = CENTRE_X + (crest_pos[r][0] - CENTRE_X) * 0.6f;
        out[2] = CENTRE_Z + (crest_pos[r][1] - CENTRE_Z) * 0.6f;
    }
    out[1] = db.floor_y;
}

/* FUN_003c3180, in its order: (1) he's in the light: sink and move to the nearest dark sixth (0x1e-0x22);
 * (2) Dante's in the dark and away from him: rise out of Dante's shadow (0x1f); (3) a crest's open next to a dark
 * one: go and shut it (0xd/0xe the meteor, 0x1d the blast at it if he's there); all six open: 0x23, the body blast;
 * (4) by distance (bands 300 470 700 879): within 470 a combo (list 0x5b7b50: 5 x6, 6 x5, 7 x1), to 700 the
 * Stinger (0x1b), beyond, the approach (0x17). */
static void think(void) {
    float d = dist2(db.pos, db.dante);
    int mine = region(db.pos[0], db.pos[2]), his = region(db.dante[0], db.dante[2]), i;
    if (db.lit[6]) { set_state(S_ALL_LIT); return; }
    if (db.lit[mine] && db.stagger_cool <= 0.0f) { set_state(S_SINK); db.flee_to = dark_region_near(db.pos[0], db.pos[2]); return; }
    if (!db.lit[his] && d > 700.0f) { set_state(S_EMERGE); return; }
    for (i = 0; i < 6; ++i) if (db.lit[i] && !db.lit[opposite[i]] && unit_random() < 0.35f) {
        db.flee_to = i;
        set_state(S_METEOR);
        return;
    }
    if (d < 470.0f) { set_state(S_COMBO); return; }
    if (d < 700.0f) { set_state(S_STINGER); return; }
    set_state(S_APPROACH);
}

static void ai_step(float f) {
    float d = dist2(db.pos, db.dante);
    db.timer += f;
    if (db.stagger_cool > 0.0f) db.stagger_cool -= f;
    switch (db.state) {
    case S_START:
        /* 0x1: rising out of the floor (scripts 9 then 10) */
        if (db.step == 0) { play(7); db.step = 1; }
        if (db.step == 1 && motion_done()) { play(8); db.step = 2; }
        if (db.step == 2 && motion_done()) set_state(S_THINK);
        break;
    case S_THINK:
        /* 0x2: 0:0 idle, turning 0x100 a frame */
        if (db.step == 0) { play(0); db.step = 1; db.think_time = 30.0f + unit_random() * 30.0f; }
        turn_toward(angle_to(db.dante), 0x100 * 2.0f * PI / 65536.0f * f);
        if (db.timer > db.think_time) think();
        break;
    case S_APPROACH:
        /* 0x17: toward Dante (scripts 0x1d-0x1f; effect 0x9f), then think again */
        if (db.step == 0) { play(2); db.step = 1; }
        turn_toward(angle_to(db.dante), 0x400 * 2.0f * PI / 65536.0f * f);
        move_forward(9.0f, f);
        if (d < 400.0f || db.timer > 150.0f) set_state(d < 470.0f ? S_COMBO : S_THINK);
        break;
    case S_COMBO:
        /* 5-7 -> 8-9 -> 10: scripts 3, 4, 5 (motions 3-5), records 0, 1, 2 on their windows */
        if (db.step == 0) { play(3); db.step = 1; }
        if (db.step == 1) {
            if (db.frame < 10.0f) turn_toward(angle_to(db.dante), 0.15f * f);
            if (db.frame >= 13.0f) swing(170.0f, 300.0f, 0);
            if (db.frame >= 30.0f) { db.hit_done = 0; if (d < 300.0f) { play(4); db.step = 2; } else if (motion_done()) set_state(S_THINK); }
        } else if (db.step == 2) {
            if (db.frame >= 37.0f) swing(170.0f, 300.0f, 0);
            if (db.frame >= 45.0f) { db.hit_done = 0; if (d < 300.0f) { play(5); db.step = 3; } else if (motion_done()) set_state(S_THINK); }
        } else if (db.step == 3) {
            if (db.frame >= 63.0f) swing(190.0f, 600.0f, 1);
            if (motion_done()) set_state(S_THINK);
        }
        break;
    case S_STINGER:
        /* 0x1b: script 0x23 (motion 23), record 4 at 30, the lunge (effect 0x293) */
        if (db.step == 0) { play(23); db.step = 1; turn_toward(angle_to(db.dante), PI); }
        if (db.frame >= 18.0f && db.frame < 40.0f) { move_forward(28.0f, f); if (db.frame >= 30.0f) swing(160.0f, 500.0f, 1); }
        if (motion_done()) set_state(S_THINK);
        break;
    case S_SINK:
        /* 0x1e: into his shadow (script 0x25, motion 25), across to the dark (+0x2200), up again */
        if (db.step == 0) { play(25); db.step = 1; }
        if (db.step == 1 && db.frame > 20.0f) { db.hidden = 1; db.step = 2; db.timer = 0.0f; }
        if (db.step == 2 && db.timer > 40.0f) {
            point_in_region(db.flee_to, db.pos);
            db.yaw = angle_to(db.dante);
            db.hidden = 0;
            play(8);
            db.step = 3;
        }
        if (db.step == 3 && motion_done()) set_state(S_THINK);
        break;
    case S_EMERGE:
        /* 0x1f: the same sinking, and up out of Dante's own shadow behind him, straight into the combo */
        if (db.step == 0) { play(25); db.step = 1; }
        if (db.step == 1 && db.frame > 20.0f) { db.hidden = 1; db.step = 2; db.timer = 0.0f; }
        if (db.step == 2 && db.timer > 50.0f) {
            db.pos[0] = db.dante[0] - sinf(db.dante_yaw) * 150.0f;
            db.pos[2] = db.dante[2] - cosf(db.dante_yaw) * 150.0f;
            db.yaw = angle_to(db.dante);
            db.hidden = 0;
            set_state(S_COMBO);
        }
        break;
    case S_METEOR:
        /* 0xe: up (0:15, motion 9), across to the open crest, down on it (0:16-0:18): it shuts (FUN_003bf750) */
        if (db.step == 0) {
            play(9);
            db.step = 1;
            db.target[0] = crest_pos[db.flee_to][0] + (CENTRE_X - crest_pos[db.flee_to][0]) * 0.15f;
            db.target[2] = crest_pos[db.flee_to][1] + (CENTRE_Z - crest_pos[db.flee_to][1]) * 0.15f;
            db.yaw = angle_to(db.target);
        }
        if (db.step == 1) {
            float t = fminf(1.0f, db.frame / 40.0f), dd = dist2(db.pos, db.target);
            if (dd > 20.0f) move_forward(fminf(dd, 35.0f * f) / f, f);
            db.pos[1] = db.floor_y + sinf(t * PI) * 300.0f;
            if (db.frame >= 40.0f) { db.pos[1] = db.floor_y; put_out(db.flee_to); db.step = 2; play(10); }
        }
        if (db.step == 2 && motion_done()) set_state(S_THINK);
        break;
    case S_ALL_LIT:
    case S_BLAST:
        /* 0x23 -> 0x10-0x12: all six open; the body blast (0:19, motion 10) shuts every one, and hurts Dante near */
        if (db.step == 0) { play(10); db.step = 1; }
        if (db.step == 1 && db.frame >= 30.0f) {
            int i;
            float at[3] = {db.pos[0], db.pos[1] + 100.0f, db.pos[2]};
            fx_explosion(at);
            for (i = 0; i < 6; ++i) put_out(i);
            if (d < 400.0f) swing(400.0f, 800.0f, 1);
            db.step = 2;
        }
        if (db.step == 2 && motion_done()) set_state(S_THINK);
        break;
    case S_STAGGER:
        /* Caught in the light: he reels (0:37-0:40, motions 25-28) and can be hurt [the stun's length is an estimate] */
        if (db.step == 0) { play(26 + (db.tick % 3)); db.step = 1; }
        if (db.timer > 150.0f) { db.stagger_cool = 30.0f; set_state(S_THINK); }
        else if (motion_done()) play(26 + (db.tick % 3));
        break;
    default:
        break;
    }
}

/* ---- Skeletal animation and drawing ------------------------------------------------------------------- */
static void identity(Transform *t) { memset(t, 0, sizeof(*t)); t->m[0][0] = t->m[1][1] = t->m[2][2] = 1.0f; }
static void multiply(Transform *out, const Transform *a, const Transform *b) {
    Transform t;
    int i, j;
    for (i = 0; i < 3; ++i) {
        t.p[i] = a->p[i];
        for (j = 0; j < 3; ++j) {
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
            t.p[i] += a->m[i][j] * b->p[j];
        }
    }
    *out = t;
}
static void euler(Transform *t, float x, float y, float z) {
    float cx = cosf(x), sx = sinf(x), cy = cosf(y), sy = sinf(y), cz = cosf(z), sz = sinf(z);
    identity(t);
    t->m[0][0] = cz * cy; t->m[0][1] = cz * sy * sx - sz * cx; t->m[0][2] = cz * sy * cx + sz * sx;
    t->m[1][0] = sz * cy; t->m[1][1] = sz * sy * sx + cz * cx; t->m[1][2] = sz * sy * cx - cz * sx;
    t->m[2][0] = -sy;     t->m[2][1] = cy * sx;              t->m[2][2] = cy * cx;
}
static void build_skeleton(const DoppelgangerMesh *mesh, const MotionSample *s, const MotionSample *prev, float fade,
                           const Transform *attachment, Transform *out) {
    int i, k;
    for (i = 0; i < mesh->bone_count; ++i) {
        float r[3] = {0.0f, 0.0f, 0.0f};
        float p[3] = {mesh->local[i].x, mesh->local[i].y, mesh->local[i].z};
        Transform local;
        for (k = 0; k < 3 && s; ++k) {
            if (s->has_rot[i][k]) r[k] = s->rot[i][k];
            if (s->has_trans[i][k]) p[k] = s->trans[i][k];
            if (prev && fade > 0.0f) {
                float old_r = prev->has_rot[i][k] ? prev->rot[i][k] : r[k];
                float old_p = prev->has_trans[i][k] ? prev->trans[i][k] : p[k];
                r[k] += wrap_angle(old_r - r[k]) * fade;
                p[k] += (old_p - p[k]) * fade;
            }
        }
        if (i == 0 && s) { p[0] = 0.0f; p[2] = 0.0f; }   /* the AI moves him */
        euler(&local, r[0], r[1], r[2]);
        memcpy(local.p, p, sizeof(p));
        if (mesh->parents[i] < 0) { if (attachment) multiply(&out[i], attachment, &local); else out[i] = local; }
        else multiply(&out[i], &out[mesh->parents[i]], &local);
    }
}
static void evaluate_poses(void) {
    static MotionSample current, previous;
    Transform at;
    if (db.motion >= 0) motion_sample(motion_get(db.motion), db.frame, &current);
    else memset(&current, 0, sizeof(current));
    if (db.fade > 0.0f && db.previous_motion >= 0) motion_sample(motion_get(db.previous_motion), db.previous_frame, &previous);
    build_skeleton(&doppel_parts[PART_BODY], &current, db.fade > 0.0f ? &previous : 0, db.fade, 0, body_pose);
    /* The coat (its cloth, em031_01.clt) hangs from the hips, bone 1 [approximation: rigid] */
    at = body_pose[1];
    at.p[1] -= 0.0f;
    build_skeleton(&doppel_parts[PART_COAT], 0, 0, 0.0f, &at, coat_pose);
    /* His sword in the right hand, bone 9, as Vergil's drawn Yamato (-7.5, -2, 0) [inferred] */
    {
        Transform offset;
        euler(&offset, 0.0f, 0.0f, 0.0f);
        offset.p[0] = -7.5f; offset.p[1] = -2.0f; offset.p[2] = 0.0f;
        multiply(&at, &body_pose[9], &offset);
        build_skeleton(&doppel_parts[PART_SWORD], 0, 0, 0.0f, &at, sword_pose);
    }
}
static const Transform *part_transforms(int part) {
    return part == PART_COAT ? coat_pose : part == PART_SWORD ? sword_pose : body_pose;
}
static void skin_part(int part) {
    const DoppelgangerMesh *m = &doppel_parts[part];
    const Transform *bones = part_transforms(part);
    int i, j, k;
    for (i = 0; i < m->vertices; ++i) {
        Vertex *v = &vertices[part][i];
        float p[3] = {0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
        for (j = 0; j < 3; ++j) {
            int b = m->bones[i][j], w = m->weights[i][j];
            const Transform *t;
            float src[3], ns[3], weight;
            if (!w) continue;
            if (b >= m->bone_count) b = 0;
            t = &bones[b];
            weight = (float)w / 31.0f;
            src[0] = m->positions[i].x - m->rest[b].x;
            src[1] = m->positions[i].y - m->rest[b].y;
            src[2] = m->positions[i].z - m->rest[b].z;
            ns[0] = m->normals[i].x; ns[1] = m->normals[i].y; ns[2] = m->normals[i].z;
            for (k = 0; k < 3; ++k) {
                p[k] += (t->p[k] + t->m[k][0] * src[0] + t->m[k][1] * src[1] + t->m[k][2] * src[2]) * weight;
                n[k] += (t->m[k][0] * ns[0] + t->m[k][1] * ns[1] + t->m[k][2] * ns[2]) * weight;
            }
        }
        v->x = p[0]; v->y = p[1]; v->z = p[2];
        v->nx = n[0]; v->ny = n[1]; v->nz = n[2];
    }
    sceKernelDcacheWritebackRange(vertices[part], sizeof(Vertex) * m->vertices);
}
static void draw_part(int part) {
    const DoppelgangerMesh *m = &doppel_parts[part];
    int i, bound = -1;
    for (i = 0; i < m->objects_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if ((int)o->texture != bound) {
            const Dmc3Texture *t;
            bound = (int)o->texture;
            if (bound >= DOPPEL_TEXTURE_COUNT) bound = 0;
            t = &doppel_textures[bound];
            sceGuClutLoad(32, doppel_palettes[bound]);
            sceGuTexImage(0, t->width, t->height, t->width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[part] + o->index_start, vertices[part]);
    }
}
void doppelganger_boss_draw(void) {
    int i;
    if (!db.active) return;
    /* The open crests: a glow on the wall [approximation: the LIGHT sets' lighting isn't drawn] */
    for (i = 0; i < 6; ++i) if (db.lit[i] && ((db.tick + i * 7) % 20) == 0) {
        float at[3] = {crest_pos[i][0], CREST_Y, crest_pos[i][1]};
        fx_hit_spark(at);
    }
    if (db.hidden) return;
    evaluate_poses();
    for (i = 0; i < DOPPEL_PART_COUNT; ++i) skin_part(i);
    if (trace_on && (db.tick % 120) == 1) {
        char line[200];
        snprintf(line, sizeof(line), "draw b0=%.0f,%.0f,%.0f b500=%.0f,%.0f,%.0f c0=%.0f,%.0f,%.0f s0=%.0f,%.0f,%.0f",
                 vertices[0][0].x, vertices[0][0].y, vertices[0][0].z, vertices[0][500].x, vertices[0][500].y, vertices[0][500].z,
                 vertices[1][0].x, vertices[1][0].y, vertices[1][0].z, vertices[2][0].x, vertices[2][0].y, vertices[2][0].z);
        trace(line);
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {db.pos[0], db.pos[1], db.pos[2]};
        sceGumTranslate(&p);
    }
    sceGumRotateY(db.yaw);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    /* A shadow: dark unless the light's on him */
    sceGuColor(in_light() || db.state == S_STAGGER ? 0xffffffffu : 0xff404040u);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    for (i = 0; i < DOPPEL_PART_COUNT; ++i) draw_part(i);
    sceGuColor(0xffffffffu);
    sceGuDisable(GU_ALPHA_TEST);
}

/* ---- Being hit ---------------------------------------------------------------------------------------- */
int doppelganger_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage) {
    if (!db.landed) return 0;
    db.landed = 0;
    *from_x = db.landed_x; *from_z = db.landed_z; *heavy = db.landed_heavy; *damage = db.landed_damage;
    return 1;
}
int doppelganger_boss_hit(int kind, float from_x, float from_z, int mortal) {
    float at[3] = {db.pos[0], db.pos[1] + 110.0f, db.pos[2]}, damage;
    (void)from_x; (void)from_z;
    if (!db.active || db.dying || db.hidden) return 0;
    /* In the dark the blade goes through him (the walkthrough; his hit test follows the light) */
    if (!in_light() && db.state != S_STAGGER) { trace("through"); return 0; }
    fx_hit_spark(at);
    damage = kind == HIT_PISTOL ? 2.0f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    if (!mortal) damage = 0.0f;
    db.hp = fmaxf(0.0f, db.hp - damage);
    trace("hit");
    if (db.hp <= 0.0f) {
        db.dying = 1;
        play(32);   /* 0:50 */
        set_state(S_DEATH);
        return 1;
    }
    if (db.state == S_THINK || db.state == S_APPROACH || db.state == S_WALK || db.state == S_RUN) set_state(S_STAGGER);
    return 0;
}

/* ---- Frame -------------------------------------------------------------------------------------------- */
void doppelganger_boss_dante(float yaw) { db.dante_yaw = yaw; }
void doppelganger_boss_update(float frames, float px, float py, float pz, int attacks) {
    const Motion *m;
    if (!db.active) return;
    ++db.tick;
    db.dante[0] = px; db.dante[1] = py; db.dante[2] = pz;
    if (!db.dying && attacks) ai_step(frames);
    db.fade = fmaxf(0.0f, db.fade - frames / 8.0f);
    m = motion_get(db.motion);
    if (m) {
        db.frame += frames;
        if (db.frame >= m->end_frame) db.frame = loops(db.motion_slot) ? motion_wrap(m, db.frame) : m->end_frame;
    }
    if (floor_fn && db.state != S_METEOR) {
        float y;
        if (floor_fn(db.pos[0], db.pos[1] + 100.0f, db.pos[2], &y)) db.floor_y = y;
        db.pos[1] = db.floor_y;
    }
    /* Keep to the arena (radius about 1150 round the middle) */
    {
        float dx = db.pos[0] - CENTRE_X, dz = db.pos[2] - CENTRE_Z, r = sqrtf(dx * dx + dz * dz);
        if (r > 1050.0f) { db.pos[0] = CENTRE_X + dx / r * 1050.0f; db.pos[2] = CENTRE_Z + dz / r * 1050.0f; }
    }
    if (trace_on && (db.tick % 60) == 0) trace("tick");
}
void doppelganger_boss_spawn(float x, float y, float z) {
    memset(&db, 0, sizeof(db));
    db.active = 1;
    db.pos[0] = x; db.pos[1] = db.floor_y = y; db.pos[2] = z;
    db.yaw = PI;
    db.hp = MAX_HEALTH;
    db.motion = db.previous_motion = -1;
    db.lit[2] = 1;   /* crest 2: lightflg 1 */
    set_state(S_START);
    trace("spawn");
}
void doppelganger_boss_kill(void) { if (db.active && !db.dying) { db.hp = 0.0f; db.dying = 1; set_state(S_DEATH); } }
int doppelganger_boss_dying(void) { return db.dying; }
int doppelganger_boss_hidden(void) { return db.hidden; }
float doppelganger_boss_health(void) { return db.hp; }
float doppelganger_boss_max_health(void) { return MAX_HEALTH; }
void doppelganger_boss_position(float out[3]) { memcpy(out, db.pos, sizeof(db.pos)); }
void doppelganger_boss_target_position(float out[3]) { out[0] = db.pos[0]; out[1] = db.pos[1] + 110.0f; out[2] = db.pos[2]; }
void doppelganger_boss_push_out(float *x, float *z, float y, float r) {
    float dx, dz, d, min = BODY_RADIUS + r;
    if (!db.active || db.hidden || db.dying) return;
    if (y > db.pos[1] + 190.0f || y + 180.0f < db.pos[1]) return;
    dx = *x - db.pos[0]; dz = *z - db.pos[2];
    d = sqrtf(dx * dx + dz * dz);
    if (d >= min) return;
    if (d < 0.01f) { dx = 0.0f; dz = 1.0f; d = 1.0f; }
    *x = db.pos[0] + dx / d * min;
    *z = db.pos[2] + dz / d * min;
}
int doppelganger_boss_init(void) {
    int i, j;
    for (i = 0; i < 34; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "dp%02d", i);
        motion_ids[i] = motion_find(name);
    }
    if (motion_ids[0] < 0) return 0;
    for (i = 0; i < DOPPEL_PART_COUNT; ++i) {
        const DoppelgangerMesh *m = &doppel_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices);
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) return 0;
        memcpy(indices[i], m->indices, sizeof(unsigned short) * m->indices_count);
        for (j = 0; j < m->vertices; ++j) { vertices[i][j].u = m->uvs[j].u; vertices[i][j].v = m->uvs[j].v; }
    }
    for (i = 0; i < DOPPEL_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &doppel_textures[i];
        size_t bytes = (size_t)t->width * t->height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}
