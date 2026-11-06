#include "cerberus_boss.h"

#include "enemy.h"
#include "fx.h"
#include "nfx.h"
#include "motion.h"
#include "stage.h"
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
#include "boss_cerberus_runtime.h"
#include "boss_cerberus_scripts.h"

/* CEm025, lowest AI rank (Easy and Normal). Frames and distances retain the
 * original 60 Hz / centimetre units. See docs/CERBERUS_SPEC.md. */
#define PI 3.14159265358979323846f
#define HOME_X 2500.0f
#define HOME_Z 3250.0f
#define BODY_HEALTH 2400.0f
#define MAX_HEALTH 7200.0f
#define DANTE_RADIUS 40.0f
#define DANTE_LOW 20.0f
#define DANTE_HIGH 160.0f
#define CAP_COUNT 20
#define SHELL_MAX 48
#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

extern unsigned int enemy_ai_random(void);
extern int testroom_frame;

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;
typedef struct {
    int script, pc, motion, previous_motion, loop, stop, finish, done;
    float frame, previous_frame, fade, fade_rate;
    float cap_left[CAP_COUNT];
    unsigned char cap_record[CAP_COUNT], cap_hit[CAP_COUNT];
    int event_count;
    short events[8][3];
} ScriptPlayer;

enum {
    CB_IDLE, CB_TURN, CB_STRUGGLE, CB_ROTATE, CB_BITE, CB_NECK,
    CB_PUNCH_LEFT, CB_PUNCH_RIGHT, CB_SCRATCH, CB_SWIPE,
    CB_BREATH, CB_ICICLES, CB_SPHERE, CB_DASH_WINDUP, CB_DASH_POUNCE, CB_DASH_RETURN,
    CB_CHAIN_BREAK, CB_REICE, CB_DOWN, CB_DEAD
};
enum { CB_OP_PLAY, CB_OP_WAIT, CB_OP_HIT, CB_OP_GOTO, CB_OP_EVENT };
enum { PART_BODY, PART_HEAD0, PART_HEAD1, PART_HEAD2, PART_BODY_ICE,
       PART_HEAD_ICE0, PART_HEAD_ICE1, PART_HEAD_ICE2, PART_COLLAR, PART_CHAIN,
       PART_SMALL0, PART_SMALL1, PART_SMALL2 };
enum { ARMOUR_LEFT, ARMOUR_RIGHT, ARMOUR_HEAD0, ARMOUR_HEAD1, ARMOUR_HEAD2 };
enum { SHELL_SPIKE, SHELL_BREATH, SHELL_BREATH_BOX, SHELL_ICICLE, SHELL_SPHERE, SHELL_SHARD };

typedef struct {
    int active, type, state, head, pattern;
    float pos[3], base[3], vel[3], dir[3];
    float age, life, timer, delay, scatter, gravity, stay, scale, speed, cooldown, puff;
    int fx;                         /* the shell's own native effect (V117 icicle, V89 sphere, V48 shard) */
} Shell;

static struct {
    int active, dying, phase, phase_changed, action, action_step, pending, special_head;
    int roles[3], head_alive[3], armour[5], ice_visible[5][13], chains_detached;
    int combo_active, combo_at, combo_len, combo[3], forced_bite, melee_head_hits;
    int reice_part, reice_tick, target_part, tick, last_head_damage_tick;
    float pos[3], yaw, body_hp, head_hp[3], down, reice, turn_gauge;
    float idle_timer, extra_wait, action_time, dash_time, dash_duration;
    float dante[3], dash_from[3], dash_to[3];
    ScriptPlayer body, heads[3];
    struct { int active, script, out, last; float frame, weight; } flinch[3];
    Shell shells[SHELL_MAX];
    int sphere_hold_fx, sphere_charge_fx, breath_fx;
    int landed, landed_heavy;
    float landed_x, landed_z, landed_damage;
} cb;

static int motion_ids[4][60];
static Vertex *vertices[CERBERUS_PART_COUNT];
static unsigned short *indices[CERBERUS_PART_COUNT];
static void *textures[CERBERUS_TEXTURE_COUNT];
static Transform body_pose[31], head_pose[3][5], collar_pose[4], small_pose[3][4], chain_pose[11];
static char trace_path[320];
static int trace_on;
static int boss_difficulty = 1;
void cerberus_boss_set_difficulty(int difficulty) { boss_difficulty = difficulty; }

static const float head_max_hp[3] = {1650.0f, 1600.0f, 1550.0f};
static const signed char head_pieces[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const signed char left_pieces[4] = {7, 6, 5, 4};
static const signed char right_pieces[4] = {12, 11, 10, 9};
static const float chain_anchors[5][3] = {
    {2500.0f, 1035.0f, 3420.0f}, {3200.0f, 900.0f, 2900.0f}, {1800.0f, 900.0f, 2900.0f},
    {3000.0f, 900.0f, 3250.0f}, {2000.0f, 900.0f, 3250.0f}
};

/* [move][phase][zone], zone 0..4 = the spec's zones 1..5. */
static const unsigned char decision_weights[20][3][5] = {
    [1][0] = {15, 10, 10, 0, 0},
    [2][0] = {10, 0, 60, 15, 5}, [2][1] = {0, 0, 40, 5, 5}, [2][2] = {5, 0, 10, 10, 0},
    [3][0] = {10, 60, 0, 15, 5}, [3][1] = {0, 40, 0, 5, 5}, [3][2] = {5, 10, 0, 10, 0},
    [4][1] = {5, 0, 40, 5, 0}, [4][2] = {5, 0, 10, 0, 0},
    [5][1] = {5, 40, 0, 5, 0}, [5][2] = {5, 10, 0, 0, 0},
    [6][0] = {40, 10, 10, 0, 0}, [6][1] = {50, 5, 5, 10, 10}, [6][2] = {5, 5, 5, 0, 0},
    [7][0] = {5, 10, 10, 10, 35}, [7][1] = {10, 5, 5, 10, 30}, [7][2] = {0, 5, 5, 10, 30},
    [8][0] = {0, 0, 0, 40, 25}, [8][1] = {0, 0, 0, 40, 15}, [8][2] = {0, 0, 0, 10, 20},
    [9][0] = {10, 10, 10, 20, 30}, [9][1] = {30, 10, 10, 20, 35}, [9][2] = {10, 15, 15, 20, 30},
    [12][2] = {10, 10, 10, 0, 0}, [14][2] = {10, 10, 10, 10, 20},
    [15][2] = {25, 35, 35, 30, 0}, [18][2] = {20, 0, 0, 0, 0}
};
static const unsigned char combo_moves[8][3] = {
    {0, 9, 0}, {2, 3, 8}, {2, 3, 9}, {5, 5, 4}, {7, 7, 7}, {7, 6, 7}, {0, 0, 0}, {8, 8, 0}
};
static const unsigned char extra_wait_weights[3][5] = {{10, 10, 20, 20, 40}, {10, 20, 20, 40, 10}, {20, 20, 40, 10, 10}};

static float wrap_angle(float angle) {
    while (angle > PI) angle -= 2.0f * PI;
    while (angle < -PI) angle += 2.0f * PI;
    return angle;
}
static float unit_random(void) { return (float)enemy_ai_random() * 2.3283064e-10f; }
static int weighted5(const unsigned char weights[5]) {
    int i, total = 0, roll;
    for (i = 0; i < 5; ++i) total += weights[i];
    if (!total) return 0;
    roll = (int)(enemy_ai_random() % (unsigned int)total);
    for (i = 0; i < 5; ++i) if ((roll -= weights[i]) < 0) return i;
    return 4;
}
static void identity(Transform *t) {
    memset(t, 0, sizeof(*t));
    t->m[0][0] = t->m[1][1] = t->m[2][2] = 1.0f;
}
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
static void transform_point(const Transform *t, const float p[3], float out[3]) {
    int i;
    for (i = 0; i < 3; ++i) out[i] = t->p[i] + t->m[i][0] * p[0] + t->m[i][1] * p[1] + t->m[i][2] * p[2];
}
static void local_to_world(const float p[3], float out[3]) {
    float c = cosf(cb.yaw), s = sinf(cb.yaw);
    out[0] = cb.pos[0] + c * p[0] + s * p[2];
    out[1] = cb.pos[1] + p[1];
    out[2] = cb.pos[2] - s * p[0] + c * p[2];
}
static void world_to_local(float x, float y, float z, float out[3]) {
    float c = cosf(cb.yaw), s = sinf(cb.yaw), dx = x - cb.pos[0], dz = z - cb.pos[2];
    out[0] = c * dx - s * dz;
    out[1] = y - cb.pos[1];
    out[2] = s * dx + c * dz;
}
static void bone_world(const Transform *t, const CerberusCap *cap, float out[3]) {
    float local[3] = {cap->x, cap->y, cap->z}, attached[3];
    transform_point(t, local, attached);
    local_to_world(attached, out);
}
/* em025's own effects (PNST chunk 23), at the ids its code passes to FUN_00208590. */
static int cfx(int v, const float world[3]) { return nfx_spawn(NFX_CERBERUS, v, world, cb.yaw, 0); }
static int cfx_local(int v, const float local[3]) { float w[3]; local_to_world(local, w); return cfx(v, w); }
static void cfx_bone(int v, int bone) { cfx_local(v, body_pose[bone].p); }
/* Where the head's mouth is now (head bone 4), for effects that leave from it. */
static void head_mouth(int head, float out[3]) { local_to_world(head_pose[head][4].p, out); }
static int shell_count(void) {
    int i, count = 0;
    for (i = 0; i < SHELL_MAX; ++i) count += cb.shells[i].active;
    return count;
}
static void trace(const char *event) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "t=%d f=%d event=%s action=%d step=%d phase=%d move=%d script=%d frame=%.2f hp=%.0f/%.0f/%.0f/%.0f roles=%d%d%d shells=%d\n",
            cb.tick, testroom_frame, event, cb.action, cb.action_step, cb.phase, cb.pending, cb.body.script, cb.body.frame,
            cb.body_hp, cb.head_hp[0], cb.head_hp[1], cb.head_hp[2], cb.roles[0], cb.roles[1], cb.roles[2], shell_count());
    fclose(f);
}
void cerberus_boss_trace(const char *path) {
    trace_on = path && *path;
    if (trace_on) snprintf(trace_path, sizeof(trace_path), "%s", path);
}

/* ---- Original grouped motion scripts ------------------------------------- */
static void script_run_ops(ScriptPlayer *p, int bank) {
    int guard;
    const CerberusScriptBank *scripts = &cerberus_script_banks[bank];
    for (guard = 0; guard < 128; ++guard) {
        const CerberusOp *op = &scripts->ops[p->pc++];
        if (op->op == CB_OP_PLAY) {
            int blend = op->c & 0xFF;
            p->previous_motion = p->motion;
            p->previous_frame = p->frame;
            p->fade = blend > 0 && p->motion >= 0 ? 1.0f : 0.0f;
            p->fade_rate = blend > 0 ? 1.0f / (float)blend : 1.0f;
            p->motion = motion_ids[bank][op->a];
            p->loop = (op->c >> 8) & 1;
            if (p->motion < 0) { p->done = 1; return; }
            p->frame = op->b < 0 ? motion_get(p->motion)->loop_start : (float)op->b;
        } else if (op->op == CB_OP_WAIT) {
            p->stop = op->b;
            p->finish = op->a;
            return;
        } else if (op->op == CB_OP_HIT) {
            if (op->a >= 0 && op->a < CAP_COUNT) {
                p->cap_left[op->a] = op->b < 0 ? 1000000.0f : (float)op->b;
                p->cap_record[op->a] = (unsigned char)op->c;
                p->cap_hit[op->a] = 0;
            }
        } else if (op->op == CB_OP_GOTO) {
            p->pc = op->b;
        } else if (op->op == CB_OP_EVENT) {
            if (p->event_count < 8) {
                p->events[p->event_count][0] = op->a;
                p->events[p->event_count][1] = op->b;
                p->events[p->event_count][2] = op->c;
                ++p->event_count;
            }
        }
    }
    p->done = 1;
}
static void script_play(ScriptPlayer *p, int bank, int script) {
    p->script = script;
    p->done = 0;
    p->stop = -1;
    p->finish = 0;
    p->event_count = 0;
    memset(p->cap_left, 0, sizeof(p->cap_left));
    memset(p->cap_hit, 0, sizeof(p->cap_hit));
    if (bank < 0 || bank > 3 || script < 0 || script >= 60 || cerberus_script_banks[bank].starts[script] < 0) {
        p->done = 1;
        return;
    }
    p->pc = cerberus_script_banks[bank].starts[script];
    script_run_ops(p, bank);
}
static void script_tick(ScriptPlayer *p, int bank, float frames) {
    const Motion *motion;
    float stop;
    int i;
    p->event_count = 0;
    p->fade = fmaxf(0.0f, p->fade - p->fade_rate * frames);
    for (i = 0; i < CAP_COUNT; ++i) p->cap_left[i] = fmaxf(0.0f, p->cap_left[i] - frames);
    if (p->done || p->motion < 0) return;
    motion = motion_get(p->motion);
    if (!motion) { p->done = 1; return; }
    stop = p->stop < 0 ? motion->end_frame : (float)p->stop;
    if (p->loop && p->stop < 0) {
        p->frame += frames;
        if (p->frame > stop) p->frame = motion_wrap(motion, p->frame);
        return;
    }
    p->frame = fminf(stop, p->frame + frames);
    if (p->frame >= stop) {
        if (p->finish) p->done = 1;
        else script_run_ops(p, bank);
    }
}
static void play_all(int script) {
    int i;
    script_play(&cb.body, 0, script);
    for (i = 0; i < 3; ++i) if (cb.head_alive[i]) script_play(&cb.heads[i], cb.roles[i] + 1, script);
}
static float action_speed(void) {
    if ((cb.action == CB_PUNCH_LEFT || cb.action == CB_PUNCH_RIGHT || cb.action == CB_DASH_WINDUP) && cb.phase == 2) return 1.5f;
    if (cb.action == CB_SPHERE) return cb.phase == 0 ? 1.0f : cb.phase == 1 ? 1.8f : 2.5f;
    return 1.0f;
}

/* ---- Skeletal animation and drawing -------------------------------------- */
static void build_skeleton(const CerberusMesh *mesh, const MotionSample *sample, const MotionSample *previous,
                           float fade, const Transform *attachment, Transform *out) {
    int i, k;
    for (i = 0; i < mesh->bone_count; ++i) {
        float r[3] = {0.0f, 0.0f, 0.0f};
        float p[3] = {mesh->local[i].x, mesh->local[i].y, mesh->local[i].z};
        Transform local;
        for (k = 0; k < 3; ++k) {
            if (sample && sample->has_rot[i][k]) r[k] = sample->rot[i][k];
            if (sample && sample->has_trans[i][k]) p[k] = sample->trans[i][k];
            if (previous && fade > 0.0f) {
                float old_r = previous->has_rot[i][k] ? previous->rot[i][k] : r[k];
                float old_p = previous->has_trans[i][k] ? previous->trans[i][k] : p[k];
                r[k] += wrap_angle(old_r - r[k]) * fade;
                p[k] += (old_p - p[k]) * fade;
            }
        }
        euler(&local, r[0], r[1], r[2]);
        memcpy(local.p, p, sizeof(p));
        if (mesh->parents[i] < 0) {
            if (attachment) multiply(&out[i], attachment, &local);
            else out[i] = local;
        } else {
            multiply(&out[i], &out[mesh->parents[i]], &local);
        }
    }
}
static void evaluate_poses(void) {
    static MotionSample current, previous;
    int i, k;
    motion_sample(motion_get(cb.body.motion), cb.body.frame, &current);
    if (cb.body.fade > 0.0f && cb.body.previous_motion >= 0)
        motion_sample(motion_get(cb.body.previous_motion), cb.body.previous_frame, &previous);
    build_skeleton(&cerberus_parts[PART_BODY], &current,
                   cb.body.fade > 0.0f ? &previous : 0, cb.body.fade, 0, body_pose);
    for (i = 0; i < 3; ++i) {
        const CerberusMesh *mesh = &cerberus_parts[PART_HEAD0 + i];
        const ScriptPlayer *p = &cb.heads[i];
        if (p->motion >= 0) {
            motion_sample(motion_get(p->motion), p->frame, &current);
            if (p->fade > 0.0f && p->previous_motion >= 0)
                motion_sample(motion_get(p->previous_motion), p->previous_frame, &previous);
            if (cb.flinch[i].active && cb.flinch[i].script >= 0) {
                static MotionSample flinch;
                int motion = motion_ids[cb.roles[i] + 1][cb.flinch[i].script];
                if (motion >= 0) {
                    motion_sample(motion_get(motion), cb.flinch[i].frame, &flinch);
                    for (k = 0; k < mesh->bone_count; ++k) {
                        int j;
                        for (j = 0; j < 3; ++j) {
                            if (flinch.has_rot[k][j]) {
                                current.rot[k][j] += flinch.rot[k][j] * cb.flinch[i].weight;
                                current.has_rot[k][j] = 1;
                            }
                            if (flinch.has_trans[k][j]) {
                                current.trans[k][j] += flinch.trans[k][j] * cb.flinch[i].weight;
                                current.has_trans[k][j] = 1;
                            }
                        }
                    }
                }
            }
            build_skeleton(mesh, &current, p->fade > 0.0f ? &previous : 0, p->fade,
                           &body_pose[6 + cb.roles[i]], head_pose[i]);
        }
        memset(&current, 0, sizeof(current));
        build_skeleton(&cerberus_parts[PART_SMALL0 + i], &current, 0, 0.0f, &head_pose[i][3], small_pose[i]);
    }
    memset(&current, 0, sizeof(current));
    build_skeleton(&cerberus_parts[PART_COLLAR], &current, 0, 0.0f, &body_pose[22], collar_pose);
    (void)k;
}
static const Transform *part_transforms(int part, int head) {
    if (part == PART_BODY || part == PART_BODY_ICE) return body_pose;
    if (part >= PART_HEAD0 && part <= PART_HEAD2) return head_pose[part - PART_HEAD0];
    if (part >= PART_HEAD_ICE0 && part <= PART_HEAD_ICE2) return head_pose[part - PART_HEAD_ICE0];
    if (part == PART_COLLAR) return collar_pose;
    if (part >= PART_SMALL0 && part <= PART_SMALL2) return small_pose[part - PART_SMALL0];
    if (part == PART_CHAIN) return chain_pose;
    (void)head;
    return body_pose;
}
static void skin_part(int part) {
    const CerberusMesh *m = &cerberus_parts[part];
    const Transform *bones = part_transforms(part, 0);
    int i, j, k;
    for (i = 0; i < m->vertices; ++i) {
        Vertex *v = &vertices[part][i];
        float p[3] = {0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
        for (j = 0; j < 3; ++j) {
            int b = m->bones[i][j], w = m->weights[i][j];
            const Transform *t;
            float src[3], ns[3], weight;
            if (!w) continue;
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
static void skin_visible_parts(void) {
    int i;
    skin_part(PART_BODY);
    skin_part(PART_BODY_ICE);
    skin_part(PART_COLLAR);
    for (i = 0; i < 3; ++i) {
        skin_part(PART_HEAD0 + i);
        skin_part(PART_HEAD_ICE0 + i);
        skin_part(PART_SMALL0 + i);
    }
}
static int object_visible(int part, int object) {
    if (part == PART_BODY_ICE) {
        if (object >= 4 && object <= 7) return cb.ice_visible[ARMOUR_LEFT][object];
        if (object >= 9 && object <= 12) return cb.ice_visible[ARMOUR_RIGHT][object];
    }
    if (part >= PART_HEAD_ICE0 && part <= PART_HEAD_ICE2) {
        int head = part - PART_HEAD_ICE0;
        return cb.head_alive[head] && cb.ice_visible[ARMOUR_HEAD0 + head][object];
    }
    return 1;
}
static void draw_part(int part) {
    const CerberusMesh *m = &cerberus_parts[part];
    int i, bound = -1;
    for (i = 0; i < m->objects_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if (!object_visible(part, i)) continue;
        if ((int)o->texture != bound) {
            const Dmc3Texture *t;
            bound = (int)o->texture;
            t = &cerberus_textures[bound];
            sceGuClutLoad(32, cerberus_palettes[bound]);
            sceGuTexImage(0, t->width, t->height, t->width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[part] + o->index_start, vertices[part]);
    }
}
static void chain_transforms(int chain) {
    static const int body_bones[3] = {11, 9, 10};
    float from[3], to[3], dx[3], x[3], y[3], z[3], length;
    int i, j, k;
    if (chain < 3) {
        const Transform *t = &body_pose[body_bones[chain]];
        memcpy(from, t->p, sizeof(from));
    } else {
        const Transform *t = &collar_pose[chain - 1]; /* collar bones 2 and 3 */
        memcpy(from, t->p, sizeof(from));
    }
    /* The anchors are fixed world points; all pose transforms are body-local. */
    world_to_local(chain_anchors[chain][0], chain_anchors[chain][1], chain_anchors[chain][2], to);
    for (k = 0; k < 3; ++k) dx[k] = to[k] - from[k];
    length = sqrtf(dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2]);
    if (length < 0.001f) length = 1.0f;
    for (k = 0; k < 3; ++k) x[k] = dx[k] / length; /* model chain extends along +X */
    y[0] = 0.0f; y[1] = 1.0f; y[2] = 0.0f;
    {
        float dot = x[1];
        for (k = 0; k < 3; ++k) y[k] -= dot * x[k];
    }
    length = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
    if (length < 0.01f) { y[0] = 0.0f; y[1] = 0.0f; y[2] = 1.0f; length = 1.0f; }
    for (k = 0; k < 3; ++k) y[k] /= length;
    z[0] = x[1] * y[2] - x[2] * y[1]; z[1] = x[2] * y[0] - x[0] * y[2]; z[2] = x[0] * y[1] - x[1] * y[0];
    for (i = 0; i < 11; ++i) {
        float t = (float)i / 10.0f;
        for (j = 0; j < 3; ++j) {
            chain_pose[i].m[j][0] = x[j]; chain_pose[i].m[j][1] = y[j]; chain_pose[i].m[j][2] = z[j];
            chain_pose[i].p[j] = from[j] + dx[j] * t;
        }
    }
}
void cerberus_boss_draw(void) {
    int i;
    Vertex *chain_vertices = vertices[PART_CHAIN];
    if (!cb.active) return;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {cb.pos[0], cb.pos[1], cb.pos[2]};
        sceGumTranslate(&p);
    }
    sceGumRotateY(cb.yaw);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuColor(0xffffffffu);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 1);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    draw_part(PART_BODY);
    draw_part(PART_BODY_ICE);
    draw_part(PART_COLLAR);
    for (i = 0; i < 3; ++i) {
        draw_part(PART_HEAD0 + i);
        if (cb.head_alive[i]) {
            draw_part(PART_HEAD_ICE0 + i);
            draw_part(PART_SMALL0 + i);
        }
    }
    for (i = 0; i < 5; ++i) {
        if (cb.chains_detached && i > 0) continue;
        /* GE reads vertex memory asynchronously: each queued chain needs its
         * own pose until the display list has finished. */
        vertices[PART_CHAIN] = chain_vertices + i * cerberus_parts[PART_CHAIN].vertices;
        chain_transforms(i);
        skin_part(PART_CHAIN);
        draw_part(PART_CHAIN);
    }
    vertices[PART_CHAIN] = chain_vertices;
    /* The shells (icicles, sphere, shards) are drawn by their native effects in nfx_draw. */
    sceGuDisable(GU_ALPHA_TEST);
}

/* ---- Hurt targets, armour and phases ------------------------------------- */
static int part_alive(int part) {
    return part < ARMOUR_HEAD0 || cb.head_alive[part - ARMOUR_HEAD0];
}
static void part_position(int part, float out[3]) {
    float attached[3] = {0.0f, 0.0f, 0.0f};
    if (part == ARMOUR_LEFT) memcpy(attached, body_pose[16].p, sizeof(attached));
    else if (part == ARMOUR_RIGHT) memcpy(attached, body_pose[21].p, sizeof(attached));
    else memcpy(attached, head_pose[part - ARMOUR_HEAD0][3].p, sizeof(attached));
    local_to_world(attached, out);
}
void cerberus_boss_select_target(float px, float pz) {
    int i, best = -1;
    float distance = 1.0e30f;
    if (!cb.active) return;
    for (i = 0; i < 5; ++i) {
        float p[3], dx, dz, d;
        if (!part_alive(i)) continue;
        part_position(i, p);
        dx = p[0] - px; dz = p[2] - pz;
        d = dx * dx + dz * dz;
        if (d < distance) { distance = d; best = i; }
    }
    if (best >= 0) cb.target_part = best;
}
void cerberus_boss_root_position(float out[3]) { memcpy(out, cb.pos, sizeof(cb.pos)); }
void cerberus_boss_target_position(float out[3]) {
    if (!cb.active) { memset(out, 0, sizeof(float) * 3); return; }
    if (!part_alive(cb.target_part)) cb.target_part = ARMOUR_LEFT;
    part_position(cb.target_part, out);
}
float cerberus_boss_segment_hit(const float a[3], const float b[3]) {
    int i;
    float best = -1.0f;
    if (!cb.active || cb.dying) return -1.0f;
    for (i = 0; i < 5; ++i) {
        float p[3], d[3], w[3], len2, t, q[3], radius = 120.0f;
        int k;
        if (!part_alive(i)) continue;
        part_position(i, p);
        for (k = 0; k < 3; ++k) { d[k] = b[k] - a[k]; w[k] = p[k] - a[k]; }
        len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        t = len2 > 0.001f ? (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / len2 : 0.0f;
        t = fmaxf(0.0f, fminf(1.0f, t));
        for (k = 0; k < 3; ++k) q[k] = a[k] + d[k] * t - p[k];
        if (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] <= radius * radius && (best < 0.0f || t < best)) {
            best = t;
            cb.target_part = i;
        }
    }
    return best;
}
static int armour_piece_count(int part) { return part < ARMOUR_HEAD0 ? 4 : 13; }
static signed char armour_piece(int part, int index) {
    if (part == ARMOUR_LEFT) return left_pieces[index];
    if (part == ARMOUR_RIGHT) return right_pieces[index];
    return head_pieces[index];
}
static int needs_reice(void) {
    int i;
    for (i = 0; i < 5; ++i) {
        int limit = i < ARMOUR_HEAD0 ? (cb.phase == 2 ? 3 : 4) : (cb.phase == 2 ? 10 : 13);
        if (part_alive(i) && cb.armour[i] >= limit) return 1;
    }
    return 0;
}
static void start_flinch(int head, float from_x, float from_z) {
    static const unsigned char scripts[3][8] = {
        {0x17, 0x0A, 0x0A, 0x0A, 0x0D, 0x0B, 0x0B, 0x0B},
        {0x16, 0x10, 0x10, 0x10, 0x14, 0x11, 0x11, 0x11},
        {0x18, 0x12, 0x12, 0x12, 0x15, 0x13, 0x13, 0x13}
    };
    float local[3], angle;
    int direction, script;
    if (cb.action != CB_IDLE && cb.action != CB_TURN && cb.action != CB_STRUGGLE) return;
    world_to_local(from_x, cb.pos[1], from_z, local);
    angle = wrap_angle(atan2f(local[0], local[2]) - PI * 0.5f);
    direction = ((int)floorf(angle / (PI * 0.25f) + 0.5f) & 7);
    script = scripts[cb.roles[head]][direction];
    if (script >= 0x19 && cb.flinch[head].last >= 0x19 && cb.flinch[head].last <= 0x1B) script = 0x1C;
    cb.flinch[head].active = 1;
    cb.flinch[head].out = 0;
    cb.flinch[head].script = script;
    cb.flinch[head].last = script;
    cb.flinch[head].frame = 0.0f;
    cb.flinch[head].weight = 0.0f;
}
static int begin_death(void) {
    int i;
    if (cb.dying) return 0;
    cb.dying = 1;
    cb.action = CB_DEAD;
    cb.body_hp = 0.0f;
    for (i = 0; i < SHELL_MAX; ++i) {
        if (cb.shells[i].active && cb.shells[i].fx) nfx_kill(cb.shells[i].fx);
        cb.shells[i].active = 0;
    }
    nfx_kill(cb.sphere_hold_fx); nfx_kill(cb.sphere_charge_fx); nfx_kill(cb.breath_fx);
    play_all(0);
    trace("death");
    return 1;
}
void cerberus_boss_kill(void) { if (cb.active) begin_death(); }
static void damage_head(int head, float damage) {
    if (!cb.head_alive[head]) return;
    cb.head_hp[head] = fmaxf(0.0f, cb.head_hp[head] - damage);
    if (cb.head_hp[head] <= 0.0f) {
        int i, alive = 0;
        cb.head_alive[head] = 0;
        memset(cb.ice_visible[ARMOUR_HEAD0 + head], 0, sizeof(cb.ice_visible[ARMOUR_HEAD0 + head]));
        for (i = 0; i < 3; ++i) alive += cb.head_alive[i];
        trace("head_death");
        if (!alive) begin_death();
    }
}
static int interruptible(void) {
    return cb.action == CB_IDLE || cb.action == CB_TURN || cb.action == CB_STRUGGLE || cb.action == CB_ROTATE ||
           cb.action == CB_DASH_RETURN || cb.action == CB_SCRATCH || cb.action == CB_SWIPE ||
           cb.action == CB_NECK || cb.action == CB_BITE;
}
static void start_down(void) {
    cb.action = CB_DOWN;
    cb.action_step = 0;
    cb.combo_active = 0;
    cb.down = 1000.0f;
    cb.reice = cb.phase == 0 ? 4000.0f : cb.phase == 1 ? 3600.0f : 3000.0f;
    cb.turn_gauge = 1000.0f;
    play_all(12);
    trace("down");
}
int cerberus_boss_hit(int kind, float from_x, float from_z, int mortal) {
    int part, count, index, head_hit = 0;
    float damage, at[3], scaled;
    if (!cb.active || cb.dying || !mortal) return 0;
    /* Keep the part chosen by lock-on or the projectile intersection. Picking
     * again from the muzzle would turn a head shot into a nearby paw hit. */
    part = cb.target_part;
    part_position(part, at);
    fx_hit_spark(at);
    if (part >= ARMOUR_HEAD0 && kind != HIT_PISTOL) {
        int head = part - ARMOUR_HEAD0;
        ++cb.melee_head_hits;
        start_flinch(head, from_x, from_z);
        head_hit = 1;
    }
    damage = kind == HIT_PISTOL ? 10.0f * 0.8f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    cb.turn_gauge = fmaxf(0.0f, cb.turn_gauge - damage);
    count = armour_piece_count(part);
    index = cb.armour[part];
    if (index < count) {
        int piece = armour_piece(part, index);
        cb.ice_visible[part][piece] = 0;
        cb.armour[part] = index + 1;
        /* 2a84b0: a piece breaks off, V126 (0x7e). */
        cfx(126, at);
        trace("armour_break");
        return 0;
    }
    scaled = damage * (head_hit ? 0.7f : 1.2f);
    cb.down -= scaled;
    cb.reice -= scaled;
    if (cb.phase == 0) {
        cb.body_hp = fmaxf(0.0f, cb.body_hp - damage);
        if (cb.body_hp <= 0.0f) {
            cb.phase = 1;
            cb.phase_changed = 1;
            trace("phase1");
        }
    } else if (part >= ARMOUR_HEAD0) {
        if (cb.last_head_damage_tick != cb.tick) {
            cb.last_head_damage_tick = cb.tick;
            damage_head(part - ARMOUR_HEAD0, damage);
        }
    } else {
        int i;
        /* A paw hit distributes its damage to all live heads. The direct-head
         * per-frame guard must not truncate this to the first head. */
        for (i = 0; i < 3; ++i) if (cb.head_alive[i]) damage_head(i, damage * 0.1f);
    }
    if (!cb.dying && cb.phase == 1) {
        float total = cb.head_hp[0] + cb.head_hp[1] + cb.head_hp[2];
        if (total <= 3200.0f) { cb.phase = 2; trace("phase2"); }
    }
    if (!cb.dying && cb.down < 0.0f && interruptible()) start_down();
    return cb.dying;
}

/* ---- Shells and hit tests ------------------------------------------------- */
static int sphere_hit(const float pos[3], float radius, float px, float py, float pz) {
    float near_y = fmaxf(py + DANTE_LOW, fminf(py + DANTE_HIGH, pos[1]));
    float dx = pos[0] - px, dy = pos[1] - near_y, dz = pos[2] - pz, r = radius + DANTE_RADIUS;
    return dx * dx + dy * dy + dz * dz <= r * r;
}
static void land_attack(const CerberusAttack *attack, const float at[3]) {
    if (cb.landed) return;
    cb.landed = 1;
    cb.landed_x = at[0];
    cb.landed_z = at[2];
    cb.landed_damage = attack->damage;
    cb.landed_heavy = attack->kind == 2 || attack->kind == 4 || attack->kind == 5;
}
int cerberus_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage) {
    if (!cb.landed) return 0;
    cb.landed = 0;
    *from_x = cb.landed_x;
    *from_z = cb.landed_z;
    *heavy = cb.landed_heavy;
    *damage = cb.landed_damage;
    return 1;
}
static Shell *new_shell(int type) {
    int i;
    for (i = 0; i < SHELL_MAX; ++i) if (!cb.shells[i].active) {
        Shell *s = &cb.shells[i];
        memset(s, 0, sizeof(*s));
        s->active = 1;
        s->type = type;
        s->scale = 1.0f;
        return s;
    }
    return 0;
}
static void test_body_caps(ScriptPlayer *p, const Transform *poses, float px, float py, float pz) {
    int i;
    for (i = 0; i < CAP_COUNT; ++i) {
        const CerberusCap *cap = &cerberus_caps[i];
        float at[3];
        if (cap->kind != 1 || p->cap_left[i] <= 0.0f || p->cap_hit[i]) continue;
        bone_world(&poses[cap->bone], cap, at);
        if (sphere_hit(at, cap->radius, px, py, pz)) {
            p->cap_hit[i] = 1;
            land_attack(&cerberus_attacks[p->cap_record[i]], at);
        }
    }
}
static void retire(Shell *s, int v) {
    if (s->fx) nfx_kill(s->fx);
    s->fx = 0;
    if (v) cfx(v, s->pos);
    s->active = 0;
}
static void spawn_punch_spikes(void) {
    static const float velocity[8][2] = {
        {0.0f, 0.0f}, {0.0f, 2.8f}, {0.0f, 5.6f}, {0.0f, 8.4f},
        {0.0f, 11.2f}, {0.0f, 14.0f}, {-5.0f, 3.0f}, {5.0f, 3.0f}
    };
    int paw = cb.action == CB_PUNCH_LEFT ? 16 : 21, i;
    float local[3];
    for (i = 0; i < 8; ++i) {
        Shell *s = new_shell(SHELL_SPIKE);
        float world[3];
        if (!s) break;
        memcpy(local, body_pose[paw].p, sizeof(local));
        local[1] = 100.0f;
        local_to_world(local, world);
        memcpy(s->pos, world, sizeof(s->pos));
        local[0] = velocity[i][0]; local[1] = 0.0f; local[2] = velocity[i][1];
        {
            float c = cosf(cb.yaw), sn = sinf(cb.yaw);
            s->vel[0] = c * local[0] + sn * local[2];
            s->vel[1] = 0.0f;
            s->vel[2] = -sn * local[0] + c * local[2];
        }
        s->life = 60.0f;
        s->puff = (float)(i & 1) * 4.0f;
    }
    /* FUN_002a0510: V166 (0xa6), the ice bursting up round the paw. */
    memcpy(local, body_pose[paw].p, sizeof(local));
    local[1] = 0.0f;
    cfx_local(166, local);
}
static void start_breath(void) {
    int head = cb.special_head, i;
    float mouth[3];
    if (!cb.head_alive[head]) return;
    local_to_world(head_pose[head][4].p, mouth);
    for (i = 0; i < 10; ++i) {
        Shell *s = new_shell(SHELL_BREATH);
        if (!s) break;
        memcpy(s->base, mouth, sizeof(s->base));
        memcpy(s->pos, mouth, sizeof(s->pos));
        s->pos[1] = cb.pos[1] - 50.0f;
        s->dir[0] = cerberus_breath_spread[i][0];
        s->dir[2] = cerberus_breath_spread[i][1];
        s->life = 100.0f;
        s->head = head;
        s->puff = (float)(i % 4);
    }
    if (cb.phase > 0) {
        Shell *s = new_shell(SHELL_BREATH_BOX);
        float local[3] = {-1700.0f, 0.0f, 1200.0f};
        if (s) {
            local_to_world(local, s->pos);
            {
                float c = cosf(cb.yaw), sn = sinf(cb.yaw);
                s->vel[0] = c * 23.0f; s->vel[1] = 0.0f; s->vel[2] = -sn * 23.0f;
            }
            s->life = 100.0f;
            s->head = head;
        }
    }
}
static void spawn_icicles(void) {
    int i, count = cb.phase == 0 ? 8 : 15;
    const CerberusIcicle *patterns = cb.phase == 0 ? cerberus_icicles_0 : cerberus_icicles_1;
    if (!cb.head_alive[cb.special_head]) return;
    for (i = 0; i < count; ++i) {
        Shell *s = new_shell(SHELL_ICICLE);
        if (!s) return;
        s->state = 0;
        s->timer = patterns[i].delay + 40.0f;
        s->scatter = patterns[i].scatter;
        s->gravity = patterns[i].gravity;
        s->stay = patterns[i].stay;
        s->scale = patterns[i].scale;
        s->head = cb.special_head;
    }
}
static void spawn_sphere(void) {
    Shell *s = new_shell(SHELL_SPHERE);
    float root[3] = {0.0f, 0.0f, 0.0f};
    if (!s) return;
    s->state = 0;
    s->head = cb.special_head;
    s->delay = 270.0f / action_speed();
    s->timer = 160.0f / action_speed();
    local_to_world((float[]){0.0f, 150.0f, 0.0f}, s->pos);
    /* FUN_0032c020: V101 (0x65) on the body while the sphere is held. */
    if (cb.sphere_hold_fx) nfx_kill(cb.sphere_hold_fx);
    cb.sphere_hold_fx = cfx_local(101, root);
}
static void shatter_icicle(Shell *s) {
    if (s->state == 3) return;
    s->state = 3;
    s->timer = 50.0f;
    /* FUN_0037f2a0: the icicle's model goes and V171 (0xab) breaks it. */
    if (s->fx) nfx_kill(s->fx);
    s->fx = 0;
    cfx(171, s->pos);
}
static void burst_sphere(Shell *sphere) {
    static const float fan[4][3] = {{-15.0f, 0.0f, 20.0f}, {15.0f, 0.0f, 20.0f}, {-3.0f, 0.0f, 30.0f}, {3.0f, 0.0f, 30.0f}};
    int i, horizontal = unit_random() < 0.1f;
    for (i = 0; i < 4; ++i) {
        Shell *s = new_shell(SHELL_SHARD);
        float x = horizontal ? fan[i][0] : 0.0f, y = horizontal ? 0.0f : fan[i][0], z = fan[i][2];
        float c = cosf(cb.yaw), sn = sinf(cb.yaw);
        if (!s) break;
        memcpy(s->pos, sphere->pos, sizeof(s->pos));
        s->vel[0] = c * x + sn * z; s->vel[1] = y; s->vel[2] = -sn * x + c * z;
        s->state = 1;
        s->life = 300.0f;
        s->head = sphere->head;
        /* FUN_0040db60: each shard is V48 (0x30), the sphere's model at half size. */
        s->fx = nfx_spawn(NFX_CERBERUS, 48, s->pos, cb.yaw, 1);
    }
    retire(sphere, 237);
}
static void update_shells(float f, float px, float py, float pz) {
    int i, k;
    for (i = 0; i < SHELL_MAX; ++i) {
        Shell *s = &cb.shells[i];
        float old[3];
        if (!s->active) continue;
        memcpy(old, s->pos, sizeof(old));
        s->age += f;
        s->puff -= f;
        if (s->cooldown > 0.0f) s->cooldown = fmaxf(0.0f, s->cooldown - f);
        if (s->type == SHELL_SPIKE) {
            for (k = 0; k < 3; ++k) s->pos[k] += s->vel[k] * f;
            s->life -= f;
            if (s->puff <= 0.0f && (s->vel[0] != 0.0f || s->vel[2] != 0.0f) && s->life > 8.0f) {
                /* The line of ice the spikes run along. Its PS2 particle programs (P58/P59, ids 0x3a/0x3b)
                 * aren't ported; this raises em025's own ice spike, E199 (model M55), along the line. */
                float ground[3] = {s->pos[0], cb.pos[1], s->pos[2]};
                /* E199 grows to about 5 m for V166's eruption at the paw; along the line they stay small. */
                nfx_scale(nfx_spawn_e(NFX_CERBERUS, 199, ground, cb.yaw + (unit_random() - 0.5f), 0),
                          0.3f + unit_random() * 0.1f);
                s->puff = 14.0f;
            }
            if (sphere_hit(s->pos, 50.0f, px, py, pz) && s->cooldown <= 0.0f) {
                land_attack(&cerberus_attacks[12], s->pos);
                s->cooldown = 1000000.0f;
            }
            if (s->life <= 0.0f) s->active = 0;
        } else if (s->type == SHELL_BREATH) {
            float t = 100.0f - s->life;
            s->pos[0] = s->base[0] + t * 0.08f * s->dir[0];
            s->pos[2] = s->base[2] + t * 0.002f * s->dir[2];
            s->life -= f;
            if (s->puff <= 0.0f) {
                /* The breath's mist is G44/G23 (generators, not ported): em025's E284 cloud stands in. */
                float mist[3] = {s->pos[0], cb.pos[1] + 40.0f, s->pos[2]};
                nfx_scale(nfx_spawn_e(NFX_CERBERUS, 284, mist, unit_random() * 6.28f, 0), 1.4f + unit_random() * 0.6f);
                s->puff = 9.0f;
            }
            if (s->cooldown <= 0.0f && sphere_hit(s->pos, 100.0f, px, py, pz)) {
                land_attack(&cerberus_attacks[6], s->pos);
                s->cooldown = 50.0f;
            }
            if (s->life <= 0.0f) s->active = 0;
        } else if (s->type == SHELL_BREATH_BOX) {
            float local[3];
            for (k = 0; k < 3; ++k) s->pos[k] += s->vel[k] * f;
            s->life -= f;
            if (s->puff <= 0.0f) {
                float c = cosf(cb.yaw), sn = sinf(cb.yaw), along = (unit_random() - 0.5f) * 1400.0f;
                float mist[3] = {s->pos[0] + sn * along, cb.pos[1] + 40.0f, s->pos[2] + c * along};
                nfx_scale(nfx_spawn_e(NFX_CERBERUS, 284, mist, unit_random() * 6.28f, 0), 1.6f + unit_random() * 0.6f);
                s->puff = 8.0f;
            }
            world_to_local(px, py + 90.0f, pz, local);
            {
                float centre[3];
                world_to_local(s->pos[0], s->pos[1], s->pos[2], centre);
                if (s->cooldown <= 0.0f && fabsf(local[0] - centre[0]) < 440.0f &&
                    fabsf(local[1] - centre[1]) < 120.0f && fabsf(local[2] - centre[2]) < 840.0f) {
                    land_attack(&cerberus_attacks[17], s->pos);
                    s->cooldown = 1000000.0f;
                }
            }
            if (s->life <= 0.0f) s->active = 0;
        } else if (s->type == SHELL_ICICLE) {
            if (s->state == 0) {
                s->timer -= 1.0f; /* the original delay is an unscaled per-frame counter */
                if (s->timer <= 0.0f) {
                    float shadow[3];
                    s->state = 1;
                    s->pos[0] = px + (unit_random() - 0.5f) * s->scatter;
                    s->pos[1] = 1000.0f;
                    s->pos[2] = pz + (unit_random() - 0.5f) * s->scatter;
                    s->vel[1] = 0.0f;
                    /* FUN_0037f080: the icicle is V117 (model M54); FUN_0037f450: E291 (0x123) marks where
                     * it will land. */
                    s->fx = nfx_spawn(NFX_CERBERUS, 117, s->pos, 0.0f, 1);
                    shadow[0] = s->pos[0]; shadow[1] = cb.pos[1] + 2.0f; shadow[2] = s->pos[2];
                    nfx_spawn_e(NFX_CERBERUS, 291, shadow, 0.0f, 0);
                }
            } else if (s->state == 1) {
                s->vel[1] -= s->gravity * f;
                s->pos[1] += s->vel[1] * f;
                if (sphere_hit((float[]){s->pos[0], s->pos[1] - 80.0f, s->pos[2]}, 50.0f, px, py, pz) ||
                    sphere_hit((float[]){s->pos[0], s->pos[1] + 150.0f, s->pos[2]}, 50.0f, px, py, pz)) {
                    land_attack(&cerberus_icicle_attack, s->pos);
                    shatter_icicle(s);
                } else if (s->pos[1] <= 0.0f) {
                    s->pos[1] = 0.0f;
                    s->state = 2;
                    if (s->stay <= 0.0f) shatter_icicle(s);
                }
            } else if (s->state == 2) {
                s->stay -= f;
                if (sphere_hit(s->pos, 70.0f, px, py, pz)) shatter_icicle(s);
                else if (s->stay <= 0.0f) shatter_icicle(s);
            } else {
                s->timer -= f;
                if (s->timer <= 0.0f) retire(s, 0);
            }
            if (s->active && s->fx) nfx_move(s->fx, s->pos, 0.0f);
        } else if (s->type == SHELL_SPHERE) {
            if (!cb.head_alive[s->head]) { retire(s, 237); continue; }
            if (s->state == 0) {
                local_to_world((float[]){0.0f, 150.0f, 0.0f}, s->pos);
                if (s->timer > 0.0f && (s->timer -= f) <= 0.0f) {
                    /* FUN_0032c320: V97 (0x61), the charge, after 160 / speed frames. */
                    float root[3] = {0.0f, 0.0f, 0.0f};
                    cb.sphere_charge_fx = cfx_local(97, root);
                }
                if (s->age >= s->delay) {
                    float root[3] = {0.0f, 0.0f, 0.0f};
                    local_to_world(head_pose[s->head][4].p, s->pos);
                    s->state = 1;
                    s->age = 0.0f;
                    s->life = cb.phase == 0 ? 300.0f : 10.0f;
                    s->speed = 0.7f;
                    s->dir[0] = sinf(cb.yaw); s->dir[1] = 0.0f; s->dir[2] = cosf(cb.yaw);
                    /* The launch: V89 (0x59) is the sphere itself, kept on it; V72 (0x48) on the body. */
                    s->fx = nfx_spawn(NFX_CERBERUS, 89, s->pos, cb.yaw, 1);
                    cfx_local(72, root);
                    if (cb.sphere_hold_fx) { nfx_kill(cb.sphere_hold_fx); cb.sphere_hold_fx = 0; }
                }
            } else {
                float speed2;
                s->life -= f;
                if (s->age <= 20.0f) {
                    float target[3] = {px - s->pos[0], py + 150.0f - s->pos[1], pz - s->pos[2]};
                    float length = sqrtf(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
                    if (length > 0.001f) for (k = 0; k < 3; ++k) s->dir[k] = s->dir[k] * 0.9f + target[k] / length * 0.1f;
                    s->dir[2] -= 0.01f;
                    length = sqrtf(s->dir[0] * s->dir[0] + s->dir[1] * s->dir[1] + s->dir[2] * s->dir[2]);
                    if (length > 0.001f) for (k = 0; k < 3; ++k) s->dir[k] /= length;
                }
                s->speed = fminf(6.0f, s->speed + 0.7f * f);
                speed2 = s->speed * s->speed;
                for (k = 0; k < 3; ++k) s->pos[k] += s->dir[k] * speed2 * f;
                if (sphere_hit(s->pos, 150.0f, px, py, pz)) { land_attack(&cerberus_sphere_attack, s->pos); retire(s, 237); }
                else if (s->pos[2] < 1300.0f) retire(s, 237);
                else if (stage_loaded() && stage_raycast(old, s->pos, &speed2)) retire(s, 237);
                else if (s->life <= 0.0f) {
                    if (cb.phase == 0) retire(s, 237);
                    else burst_sphere(s);
                }
            }
            if (s->active && s->fx) nfx_move(s->fx, s->pos, cb.yaw);
        } else if (s->type == SHELL_SHARD) {
            for (k = 0; k < 3; ++k) s->pos[k] += s->vel[k] * f;
            s->life -= f;
            if (sphere_hit(s->pos, 100.0f, px, py, pz)) { land_attack(&cerberus_shard_attack, s->pos); retire(s, 237); }
            else if (s->pos[2] < 1300.0f || s->life <= 0.0f) retire(s, 237);
            else if (stage_loaded() && stage_raycast(old, s->pos, &s->speed)) retire(s, 237);
            if (s->active && s->fx) nfx_move(s->fx, s->pos, cb.yaw);
        }
    }
}


/* ---- Decisions and actions ------------------------------------------------ */
static int player_zone(float px, float pz) {
    float local[3], distance, angle;
    world_to_local(px, cb.pos[1], pz, local);
    distance = sqrtf(local[0] * local[0] + local[2] * local[2]);
    if (distance < 1200.0f) {
        angle = fabsf(atan2f(local[0], local[2]));
        if (local[2] > 0.0f && angle < 0.270526f) return 0;
        return local[0] > 0.0f ? 1 : 2;
    }
    return local[2] < 1400.0f ? 3 : 4;
}
static void rotate_roles(int direction) {
    int old[3];
    memcpy(old, cb.roles, sizeof(old));
    if (direction == 0) {
        cb.roles[0] = old[1]; cb.roles[1] = old[2]; cb.roles[2] = old[0];
    } else {
        cb.roles[0] = old[2]; cb.roles[1] = old[0]; cb.roles[2] = old[1];
    }
    trace("roles");
}
static void start_move(int id);
static void begin_decision(int id) {
    int tries = 0;
    if (id >= 6 && id <= 8) {
        while (!cb.head_alive[id - 6] && tries++ < 3) id = 6 + (id - 5) % 3;
        if (!cb.head_alive[id - 6]) { start_move(9); return; }
        cb.special_head = id - 6;
        if (cb.roles[cb.special_head] != 0) {
            cb.pending = id;
            cb.action = CB_ROTATE;
            cb.action_step = cb.roles[cb.special_head] == 2 ? 0 : 1;
            play_all(cb.action_step == 0 ? 30 : 31);
            return;
        }
    }
    start_move(id);
}
static void start_move(int id) {
    cb.pending = id;
    cb.action_time = 0.0f;
    cb.action_step = 0;
    switch (id) {
    case 0: cb.action = CB_BITE; play_all(cb.dante[1] > cb.pos[1] + 100.0f ? 52 : 55); break;
    case 1: cb.action = CB_NECK; play_all(49); break;
    case 2: cb.action = CB_PUNCH_LEFT; play_all(50); break;
    case 3: cb.action = CB_PUNCH_RIGHT; play_all(51); break;
    case 4: cb.action = CB_SCRATCH; play_all(48); cfx_local(173, (float[]){0.0f, 0.0f, 0.0f}); break;
    case 5: cb.action = CB_SWIPE; play_all(53); cfx_local(174, (float[]){0.0f, 0.0f, 0.0f}); break;
    case 6:
        cb.action = CB_BREATH; cb.special_head = 0; play_all(40);
        /* FUN_002a2320: V320 (0x140) gathers at the mouth; from phase 1, V321 (0x141) freezes the floor
         * ahead (V314, models M116-M119) as the breath sweeps. */
        {
            float mouth[3];
            head_mouth(0, mouth);
            if (cb.breath_fx) nfx_kill(cb.breath_fx);
            cb.breath_fx = cfx(320, mouth);
            if (cb.phase > 0) cfx_local(321, (float[]){0.0f, 0.0f, 0.0f});
        }
        break;
    case 7: cb.action = CB_ICICLES; cb.special_head = 1; play_all(35); break;
    case 8: cb.action = CB_SPHERE; cb.special_head = 2; play_all(42); spawn_sphere(); break;
    case 9: cb.action = CB_DASH_WINDUP; play_all(56); cfx_local(172, (float[]){0.0f, 0.0f, 0.0f}); break;
    case 11: cb.action = CB_REICE; cb.reice_part = -1; play_all(57); break;
    default: cb.action = CB_IDLE; play_all(0); break;
    }
    trace("move");
}
static void enter_idle(int allow_struggle) {
    static const float initial[3] = {1000.0f, 700.0f, 300.0f};
    cb.action = CB_IDLE;
    cb.action_step = 0;
    cb.pending = -1;
    cb.idle_timer = initial[cb.phase];
    cb.extra_wait = (float)(weighted5(extra_wait_weights[cb.phase]) * 4);
    if (allow_struggle && cb.phase == 0 && (enemy_ai_random() & 1)) {
        cb.action = CB_STRUGGLE;
        play_all(32);
    } else {
        cb.pos[0] = HOME_X; cb.pos[2] = HOME_Z;
        play_all(0);
    }
}
static void finish_move(void) {
    int finished_action = cb.action;
    if (cb.dying) return;
    if (cb.combo_active) {
        ++cb.combo_at;
        if (cb.combo_at < cb.combo_len) { begin_decision(cb.combo[cb.combo_at]); return; }
        cb.combo_active = 0;
    }
    if (cb.phase_changed) {
        cb.phase_changed = 0;
        cb.action = CB_CHAIN_BREAK;
        cb.action_step = 0;
        play_all(32);
        trace("chain_break");
        return;
    }
    if (finished_action == CB_REICE) cb.reice = cb.phase == 0 ? 4000.0f : cb.phase == 1 ? 3600.0f : 3000.0f;
    if (cb.reice <= 0.0f && needs_reice()) { start_move(11); return; }
    /* The turn test uses the last Dante position. */
    {
        float target = atan2f(cb.dante[0] - cb.pos[0], cb.dante[2] - cb.pos[2]);
        float delta = wrap_angle(target - cb.yaw);
        if (cb.turn_gauge > 500.0f && fabsf(sinf(delta)) > 0.31f) {
            cb.action = CB_TURN;
            cb.action_step = delta > 0.0f ? 1 : -1;
            cb.turn_gauge = 0.0f;
            play_all(delta > 0.0f ? 2 : 3);
            return;
        }
    }
    enter_idle(1);
}
static void schedule_decision(int id) {
    if (id >= 12 && id < 20) {
        int i;
        cb.combo_active = 1;
        cb.combo_at = 0;
        cb.combo_len = id >= 13 && id <= 17 ? 3 : 2;
        for (i = 0; i < 3; ++i) cb.combo[i] = combo_moves[id - 12][i];
        begin_decision(cb.combo[0]);
    } else {
        cb.combo_active = 0;
        begin_decision(id);
    }
}
static void decide(float px, float pz) {
    int zone = player_zone(px, pz), id, total = 0, roll;
    if (cb.melee_head_hits > (cb.phase == 0 ? 30 : cb.phase == 1 ? 28 : 26) && !cb.combo_active) {
        cb.melee_head_hits = 0;
        cb.forced_bite = 1;
        begin_decision(0);
        return;
    }
    for (id = 0; id < 20; ++id) total += decision_weights[id][cb.phase][zone];
    if (!total) { enter_idle(0); return; }
    roll = (int)(enemy_ai_random() % (unsigned int)total);
    for (id = 0; id < 20; ++id) if ((roll -= decision_weights[id][cb.phase][zone]) < 0) break;
    if (id >= 20) id = 9;
    schedule_decision(id);
}
static void update_action(float f, float px, float py, float pz) {
    cb.action_time += f;
    cb.dante[0] = px; cb.dante[1] = py; cb.dante[2] = pz;
    if (cb.action == CB_DEAD) return;
    if (cb.action == CB_IDLE) {
        static const float rates[4] = {6.0f, 8.0f, 10.0f, 12.0f};
        float distance = hypotf(px - HOME_X, pz - HOME_Z);
        int band = distance < 1000.0f ? 0 : distance < 2000.0f ? 1 : distance < 3000.0f ? 2 : 3;
        cb.idle_timer -= rates[band] * f;
        if (cb.idle_timer + cb.extra_wait <= 0.0f) decide(px, pz);
    } else if (cb.action == CB_TURN) {
        if (cb.body.frame >= 6.0f && cb.body.frame <= 22.0f) {
            float target = atan2f(px - cb.pos[0], pz - cb.pos[2]);
            float delta = wrap_angle(target - cb.yaw), step = cb.action_step * 0.0273f * f;
            if (fabsf(delta) < fabsf(step)) step = delta;
            /* Clamp relative to home: wrapping the absolute angle at +pi
             * would snap a right turn directly to the opposite limit. */
            cb.yaw = PI + fmaxf(-0.436332f, fminf(0.436332f, wrap_angle(cb.yaw + step - PI)));
        }
        if (cb.body.done) finish_move();
    } else if (cb.action == CB_STRUGGLE) {
        if (cb.body.done) enter_idle(0);
    } else if (cb.action == CB_ROTATE) {
        if (cb.body.done) {
            int pending = cb.pending;
            rotate_roles(cb.action_step);
            if (pending >= 0) begin_decision(pending);
            else {
                int head;
                for (head = 0; head < 3 && cb.roles[head] != 0; ++head) {}
                if (head < 3 && !cb.head_alive[head]) {
                    cb.action_step = 0;
                    play_all(30); /* don't leave a dead head in the special-attack socket */
                } else finish_move();
            }
        }
    } else if (cb.action == CB_NECK) {
        if (cb.body.done) {
            cb.action = CB_ROTATE;
            cb.action_step = 0;
            cb.pending = -1;
            play_all(30);
        }
    } else if (cb.action == CB_ICICLES) {
        if (cb.body.done) {
            if (!cb.action_step) { cb.action_step = 1; play_all(36); }
            else finish_move();
        }
    } else if (cb.action == CB_DASH_WINDUP) {
        if (cb.body.done) {
            float dx = px - cb.pos[0], dz = pz - cb.pos[2], distance = hypotf(dx, dz);
            float reach = cb.phase == 0 ? 200.0f : distance < 500.0f ? distance : 200.0f;
            cb.action = CB_DASH_POUNCE;
            cb.dash_time = 0.0f;
            memcpy(cb.dash_from, cb.pos, sizeof(cb.dash_from));
            memcpy(cb.dash_to, cb.pos, sizeof(cb.dash_to));
            if (distance > 0.001f) {
                cb.dash_to[0] += dx / distance * reach;
                cb.dash_to[2] += dz / distance * reach;
            }
            play_all(cb.phase == 0 ? 46 : 47);
        }
    } else if (cb.action == CB_DASH_POUNCE) {
        cb.dash_time += f;
        if (cb.dash_time <= 17.0f) {
            float t = cb.dash_time / 17.0f;
            int k;
            for (k = 0; k < 3; ++k) cb.pos[k] = cb.dash_from[k] + (cb.dash_to[k] - cb.dash_from[k]) * t;
        }
        if (cb.body.done) {
            cb.action = CB_DASH_RETURN;
            cb.dash_time = 0.0f;
            cb.dash_duration = cb.phase == 0 ? 455.0f / 2.0f : cb.phase == 1 ? 129.0f / 2.0f : 129.0f;
            memcpy(cb.dash_from, cb.pos, sizeof(cb.dash_from));
            play_all(0);
        }
    } else if (cb.action == CB_DASH_RETURN) {
        float t;
        int k;
        cb.dash_time += f;
        t = fminf(1.0f, cb.dash_time / cb.dash_duration);
        for (k = 0; k < 3; ++k) {
            float home[3] = {HOME_X, cb.pos[1], HOME_Z};
            cb.pos[k] = cb.dash_from[k] + (home[k] - cb.dash_from[k]) * t;
        }
        if ((int)cb.dash_time % 84 == 0) fx_dust(cb.pos);
        if (t >= 1.0f) finish_move();
    } else if (cb.action == CB_CHAIN_BREAK) {
        if (cb.body.done) {
            if (!cb.action_step) { cb.action_step = 1; play_all(33); }
            else enter_idle(0);
        }
    } else if (cb.action == CB_DOWN) {
        if (cb.body.done) {
            if (!cb.action_step) { cb.action_step = 1; play_all(15); }
            else enter_idle(0);
        }
    } else if (cb.action != CB_BREATH && cb.action != CB_SPHERE) {
        if (cb.body.done) finish_move();
    } else if (cb.body.done) {
        finish_move();
    }
}
static void process_body_events(float px, float py, float pz) {
    int i;
    (void)px; (void)py; (void)pz;
    for (i = 0; i < cb.body.event_count; ++i) {
        int event = cb.body.events[i][0], sub = cb.body.events[i][1], op = cb.body.events[i][2];
        if (op != 3) continue;
        if ((cb.body.script == 50 || cb.body.script == 51) && event == 3) spawn_punch_spikes();
        else if ((cb.body.script == 50 || cb.body.script == 51) && event == 1) {
            /* FUN_002a0510 ev1: V115 (0x73), the ice bursting off the paw as it lands. */
            cfx_bone(115, cb.body.script == 50 ? 16 : 21);
        }
        else if (cb.body.script == 40 && event == 5) start_breath();
        else if (cb.body.script == 35 && event >= 3 && event <= 9) {
            /* FUN_002a3710: the ice rising round it before the icicles. V151 (0x97) under the body,
             * V150 (0x96) at each front paw (bones 16 and 21), then E251 (0xfb) at the paws and chest. */
            float under[3] = {0.0f, 0.0f, 0.0f};
            if (event == 3) cfx_local(151, under);
            else if (event == 4) cfx_bone(150, 16);
            else if (event == 5) cfx_bone(150, 21);
            else {
                int bone = event == 6 ? 16 : event == 8 ? 21 : event == 9 ? 3 : 16;
                float at[3];
                local_to_world(body_pose[bone].p, at);
                nfx_spawn_e(NFX_CERBERUS, 251, at, cb.yaw, 0);
                if (event == 7) {
                    local_to_world(body_pose[21].p, at);
                    nfx_spawn_e(NFX_CERBERUS, 251, at, cb.yaw, 0);
                }
            }
        }
        else if (cb.body.script == 35 && event == 10) {
            /* V118 (0x76), the roar of ice from the role-0 head, then the icicles. */
            float root[3] = {0.0f, 0.0f, 0.0f};
            cfx_local(118, root);
            spawn_icicles();
        }
        else if (cb.body.script == 32 && cb.action == CB_CHAIN_BREAK && !cb.action_step && event == 0) {
            cb.chains_detached = 1;
            trace("chains_off");
        } else if (cb.body.script == 33 && event == 2) {
            /* FUN_002a4e10: V180 (0xb4), the burst as the chains break. */
            float root[3] = {0.0f, 0.0f, 0.0f};
            cfx_local(180, root);
        } else if (cb.body.script == 57 && event >= 1 && event <= 5) {
            float at[3];
            cb.reice_part = event == 1 ? ARMOUR_LEFT : event == 2 ? ARMOUR_RIGHT : ARMOUR_HEAD0 + event - 3;
            cb.reice_tick = 0;
            /* FUN_002a56b0: V293 (0x125) on each part as its ice grows back. */
            if (part_alive(cb.reice_part)) { part_position(cb.reice_part, at); cfx(293, at); }
        } else if ((cb.body.script == 46 || cb.body.script == 47) && event <= 1) fx_dust(cb.pos);
        (void)sub;
    }
}
static void update_reice(float f) {
    int part = cb.reice_part;
    if (part < 0 || part >= 5 || !part_alive(part)) return;
    cb.reice_tick += (int)ceilf(f);
    if (cb.reice_tick < 2) return;
    cb.reice_tick = 0;
    if (cb.armour[part] > 0) {
        int piece;
        --cb.armour[part];
        piece = armour_piece(part, cb.armour[part]);
        cb.ice_visible[part][piece] = 1;
    }
    if (cb.armour[part] <= 0) cb.reice_part = -1;
}
static void update_flinches(float f) {
    int i;
    for (i = 0; i < 3; ++i) {
        if (!cb.flinch[i].active) continue;
        cb.flinch[i].frame += f;
        if (!cb.flinch[i].out) {
            cb.flinch[i].weight = fminf(1.0f, cb.flinch[i].weight + 0.25f * f);
            if (cb.flinch[i].frame >= 46.0f) cb.flinch[i].out = 1;
        } else {
            cb.flinch[i].weight -= 0.05f * f;
            if (cb.flinch[i].weight <= 0.0f) cb.flinch[i].active = 0;
        }
    }
}
static int boss_step(float f, float px, float py, float pz, int attacks) {
    int i;
    float speed = action_speed();
    ++cb.tick;
    cb.down = fminf(1000.0f, cb.down + (cb.phase == 0 ? 0.5f : cb.phase == 1 ? 1.0f : 1.4f) * f);
    cb.turn_gauge = fminf(1000.0f, cb.turn_gauge + (cb.phase == 0 ? 0.6f : cb.phase == 1 ? 0.8f : 1.0f) * f);
    if (cb.phase == 2) cb.reice -= 0.001f * f;
    update_flinches(f);
    if (!cb.dying) {
        script_tick(&cb.body, 0, f * speed);
        for (i = 0; i < 3; ++i) if (cb.head_alive[i]) script_tick(&cb.heads[i], cb.roles[i] + 1, f * speed);
        update_action(f, px, py, pz);
        /* V320 stays at the mouth while the breath lasts. */
        if (cb.breath_fx) {
            if (cb.action == CB_BREATH && cb.head_alive[0]) {
                float mouth[3];
                head_mouth(0, mouth);
                nfx_move(cb.breath_fx, mouth, cb.yaw);
            } else { nfx_kill(cb.breath_fx); cb.breath_fx = 0; }
        }
    } else {
        script_tick(&cb.body, 0, f);
    }
    evaluate_poses();
    if (!cb.dying) {
        process_body_events(px, py, pz);
        update_reice(f);
        update_shells(f, px, py, pz);
        if (attacks) {
            test_body_caps(&cb.body, body_pose, px, py, pz);
            for (i = 0; i < 3; ++i) if (cb.head_alive[i]) test_body_caps(&cb.heads[i], head_pose[i], px, py, pz);
        }
    }
    skin_visible_parts();
    return cb.landed;
}
int cerberus_boss_update(float frames, float px, float py, float pz, int attacks) {
    if (!cb.active) return 0;
    while (frames > 0.0001f) {
        float f = fminf(1.0f, frames);
        boss_step(f, px, py, pz, attacks);
        frames -= f;
    }
    return cb.landed;
}

/* ---- Lifetime ------------------------------------------------------------ */
void cerberus_boss_spawn(void) {
    int i, j;
    memset(&cb, 0, sizeof(cb));
    cb.active = 1;
    cb.pos[0] = HOME_X;
    cb.pos[2] = HOME_Z;
    cb.yaw = PI;
    cb.body_hp = BODY_HEALTH;
    cb.down = 1000.0f;
    cb.reice = 4000.0f;
    cb.turn_gauge = 1000.0f;
    cb.last_head_damage_tick = -1;
    cb.target_part = ARMOUR_LEFT;
    cb.body.motion = cb.body.previous_motion = motion_ids[0][0];
    for (i = 0; i < 3; ++i) {
        cb.roles[i] = i;
        cb.head_alive[i] = 1;
        cb.head_hp[i] = head_max_hp[i];
        cb.heads[i].motion = cb.heads[i].previous_motion = motion_ids[i + 1][0];
        cb.flinch[i].script = -1;
        cb.flinch[i].last = -1;
    }
    for (i = 0; i < 5; ++i) for (j = 0; j < 13; ++j) cb.ice_visible[i][j] = 1;
    enter_idle(0);
    evaluate_poses();
    skin_visible_parts();
    trace("spawn");
}
int cerberus_boss_dying(void) { return cb.dying; }
float cerberus_boss_health(void) {
    float total = cb.body_hp;
    int i;
    for (i = 0; i < 3; ++i) if (cb.head_alive[i]) total += cb.head_hp[i];
    return total;
}
float cerberus_boss_max_health(void) { return MAX_HEALTH; }
/* Closest points between segments p0-p1 and q0-q1 (Ericson 5.1.9); returns the squared distance. */
static float segment_closest(const float p0[3], const float p1[3], const float q0[3], const float q1[3],
                             float cp[3], float cq[3]) {
    float d1[3], d2[3], r[3], a, e, f, s = 0.0f, t = 0.0f, out = 0.0f;
    int k;
    for (k = 0; k < 3; ++k) { d1[k] = p1[k] - p0[k]; d2[k] = q1[k] - q0[k]; r[k] = p0[k] - q0[k]; }
    a = d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2];
    e = d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2];
    f = d2[0] * r[0] + d2[1] * r[1] + d2[2] * r[2];
    if (a > 1e-4f || e > 1e-4f) {
        float c = d1[0] * r[0] + d1[1] * r[1] + d1[2] * r[2];
        if (a <= 1e-4f) t = fmaxf(0.0f, fminf(1.0f, f / e));
        else if (e <= 1e-4f) s = fmaxf(0.0f, fminf(1.0f, -c / a));
        else {
            float b = d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2], denom = a * e - b * b;
            s = denom > 1e-4f ? fmaxf(0.0f, fminf(1.0f, (b * f - c * e) / denom)) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = fmaxf(0.0f, fminf(1.0f, -c / a)); }
            else if (t > 1.0f) { t = 1.0f; s = fmaxf(0.0f, fminf(1.0f, (b - c) / a)); }
        }
    }
    for (k = 0; k < 3; ++k) {
        cp[k] = p0[k] + d1[k] * s; cq[k] = q0[k] + d2[k] * t;
        out += (cp[k] - cq[k]) * (cp[k] - cq[k]);
    }
    return out;
}
/* CEm025's solid body: kind-8 caps (pack chunk 31) on the upper front legs, bones 13, 14, 18 and 19, each
 * capsule 0,0,0 -> 0,-200,0 r100, and the chest, bone 3, 0,0,300 -> 0,-50,-150 r180 (chunk 32). Dante is the
 * vertical segment from 20 to 160 cm over his feet. He's moved sideways only, out to touching. */
void cerberus_boss_push_out(float *x, float *z, float y, float r) {
    static const int bones[5] = {13, 14, 18, 19, 3};
    static const float caps[2][7] = {{0.0f, 0.0f, 0.0f, 0.0f, -200.0f, 0.0f, 100.0f},
                                     {0.0f, 0.0f, 300.0f, 0.0f, -50.0f, -150.0f, 180.0f}};
    int pass, i;
    if (!cb.active || cb.dying) return;
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < 5; ++i) {
            const float *cap = caps[i == 4];
            float local[3], a[3], b[3], q0[3] = {*x, y + 20.0f, *z}, q1[3] = {*x, y + 160.0f, *z};
            float cp[3], cq[3], reach = cap[6] + r, d2, dx, dz, h, need;
            transform_point(&body_pose[bones[i]], cap, local);
            local_to_world(local, a);
            transform_point(&body_pose[bones[i]], cap + 3, local);
            local_to_world(local, b);
            d2 = segment_closest(a, b, q0, q1, cp, cq);
            if (d2 >= reach * reach) continue;
            dx = cq[0] - cp[0]; dz = cq[2] - cp[2];
            h = sqrtf(dx * dx + dz * dz);
            if (h < 1.0f) {
                /* Dead on the axis: out away from the body's middle. */
                dx = *x - cb.pos[0]; dz = *z - cb.pos[2];
                h = sqrtf(dx * dx + dz * dz);
                if (h < 1.0f) { dx = -sinf(cb.yaw); dz = -cosf(cb.yaw); h = 1.0f; }
            }
            {
                float dy = cq[1] - cp[1];
                need = sqrtf(fmaxf(0.0f, reach * reach - dy * dy));
            }
            if (need <= h) continue;
            *x += dx / h * (need - h);
            *z += dz / h * (need - h);
        }
        /* Port addition: the PS2 gives the heads no push shape, so a lowered head (breath, bite) swallowed
         * Dante. Each live head pushes him off as a sphere, the size of its hurt cap (13: r100 on bone 4). */
        for (i = 0; i < 3; ++i) {
            float at[3], dx, dz, dy, h, need, reach = 100.0f + r;
            if (!cb.head_alive[i]) continue;
            local_to_world(head_pose[i][4].p, at);
            dy = fmaxf(y + 20.0f, fminf(y + 160.0f, at[1])) - at[1];
            if (dy * dy >= reach * reach) continue;
            dx = *x - at[0]; dz = *z - at[2];
            h = sqrtf(dx * dx + dz * dz);
            need = sqrtf(reach * reach - dy * dy);
            if (h >= need) continue;
            if (h < 1.0f) { dx = -sinf(cb.yaw); dz = -cosf(cb.yaw); h = 1.0f; }
            *x += dx / h * (need - h);
            *z += dz / h * (need - h);
        }
    }
}
void cerberus_boss_clamp_player(float *z) {
    if (!cb.active || cb.dying) return;
    *z = fmaxf(1400.0f, fminf(3000.0f, *z));
}
int cerberus_boss_init(void) {
    int bank, i, j;
    for (bank = 0; bank < 4; ++bank) {
        const char *prefix = bank == 0 ? "cb" : bank == 1 ? "c0" : bank == 2 ? "c1" : "c2";
        for (i = 0; i < 60; ++i) {
            char name[8];
            snprintf(name, sizeof(name), "%s-%02d", prefix, i);
            motion_ids[bank][i] = motion_find(name);
        }
        if (motion_ids[bank][0] < 0) return 0;
    }
    for (i = 0; i < CERBERUS_PART_COUNT; ++i) {
        const CerberusMesh *m = &cerberus_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices * (i == PART_CHAIN ? 5 : 1));
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) return 0;
        memcpy(indices[i], m->indices, sizeof(unsigned short) * m->indices_count);
        for (j = 0; j < m->vertices; ++j) {
            vertices[i][j].u = m->uvs[j].u;
            vertices[i][j].v = m->uvs[j].v;
        }
        if (i == PART_CHAIN) {
            for (j = 1; j < 5; ++j)
                memcpy(vertices[i] + j * m->vertices, vertices[i], sizeof(Vertex) * m->vertices);
        }
    }
    for (i = 0; i < CERBERUS_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &cerberus_textures[i];
        size_t bytes = (size_t)t->width * t->height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}
