#include "vergil_boss.h"

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
#include "boss_vergil_runtime.h"
#include "boss_vergil_scripts.h"

/* CEm035, variant 0 (Mission 7), AI rank 0. Frames and distances keep the original 60 Hz / centimetre
 * units. Addresses are SLUS_213.61's; see docs/VERGIL_SPEC.md. */
#define PI 3.14159265358979323846f
#define MAX_HEALTH 8000.0f            /* param block 0x5dca50 +0x2c, variant 0 */
#define DANTE_RADIUS 40.0f
#define DANTE_LOW 20.0f
#define DANTE_HIGH 160.0f
#define BODY_RADIUS 45.0f             /* push caps 11 and 12: spheres of 45 on bones 0 and 3 */
#define ACT_DONE 0x77                 /* what vfunc 0x2ec reads once an action's script is over */
#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)
#define CAPS VERGIL_CAP_COUNT

extern unsigned int enemy_ai_random(void);
extern int testroom_frame;

enum { PART_BODY, PART_COAT, PART_YAMATO, PART_SCABBARD };
enum { OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO, OP_EVENT, OP_ATTACK };

/* AI states (+0xb9d4), from the switch in FUN_004d6770. */
enum {
    S_THINK = 0x17, S_DEATH = 0x18, S_STRAFE = 0x1d, S_IDLE = 0x1f, S_TAUNT = 0x20,
    S_COMBO_A = 0x21, S_COMBO_B = 0x22, S_COMBO_C = 0x25, S_UPPER = 0x29, S_DASH = 0x2d, S_BEHIND = 0x31,
    S_GUARD = 0x33, S_LEAP = 0x3d, S_AWAY = 0x3e, S_FLINCH = 0x50
};

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;
typedef struct {
    int bank, script, pc, motion, previous_motion, loop, stop, finish, done;
    float frame, previous_frame, fade, fade_rate, root[3];
    float cap_left[CAPS];
    unsigned char cap_record[CAPS], cap_hit[CAPS];
    unsigned char window0, window1;   /* byte 0 of each: Yamato's place, the chain point */
} ScriptPlayer;

static struct {
    int active, dying, hidden, state, step, previous_state, last_wander, action, rank;
    float pos[3], yaw, hp, gauge, timer, block_time, action_time, floor_y, vel[3], gravity;
    float centre[3], travel[3], dante[3], dante_yaw;
    int dante_action, blocked, yamato_drawn, turned_back;
    ScriptPlayer body;
    int landed, landed_heavy;
    float landed_x, landed_z, landed_damage;
    int tick;
} vb;

static int motion_ids[4][100];
static Vertex *vertices[VERGIL_PART_COUNT];
static unsigned short *indices[VERGIL_PART_COUNT];
static void *textures[VERGIL_TEXTURE_COUNT];
static Transform body_pose[24], coat_pose[40], yamato_pose[2], scabbard_pose[7];
static char trace_path[320];
static int trace_on, boss_difficulty = 1;
static int (*floor_fn)(float x, float y, float z, float *out);

void vergil_boss_set_difficulty(int difficulty) { boss_difficulty = difficulty; }
void vergil_boss_set_floor(int (*fn)(float x, float y, float z, float *out)) { floor_fn = fn; }
void vergil_boss_dante(int action, float yaw) { vb.dante_action = action; vb.dante_yaw = yaw; }

/* ---- Tables (rank 0, variant 0) -------------------------------------------------------------------- */
typedef struct { unsigned char state, weight; } Pick;
/* Wandering, when the attack gauge isn't full (0x5df3d0): strafe 40, stand 35, taunt 25; 0x19 is 0. */
static const Pick wander_picks[] = {{S_STRAFE, 40}, {S_IDLE, 35}, {S_TAUNT, 25}};
/* The attack scheduler (+0x130, config 0x5df3b0): bands by distance (0x5df308: 350, 500, beyond). */
static const Pick near_picks[] = {{S_COMBO_A, 50}, {S_COMBO_B, 50}};          /* 0x5dccc0 */
static const Pick mid_picks[] = {{S_COMBO_C, 50}, {S_DASH, 50}};              /* 0x5dcd40 (0x2b is 0) */
static const Pick far_picks[] = {{S_BEHIND, 100}};                             /* 0x5dcda0 */
/* The counter after a guard has held 15 frames (0x5dfad0[0]): list 0x5dcde0. */
static const Pick counter_picks[] = {{S_COMBO_C, 50}, {S_DASH, 50}};
/* Gauge fill while Dante is inside a band (0x5df360: distance, half angle, rate a frame). */
static const float gauge_bands[3][2] = {{350.0f, 20.0f}, {400.0f, 4.0f}, {500.0f, 1.0f}};
#define GAUGE_READY 100.0f            /* 0x5df340[rank 0] */
#define GAUGE_SLOW 0.167f             /* DAT_0061a8c0: outside every band */
#define GAUGE_CLOSE 300.0f            /* DAT_0061a8c4: nearer than this, the gauge is full at once */
#define REACT_RANGE 500.0f            /* DAT_0061aa24 */
#define STRAFE_TIME 360.0f            /* DAT_0061a8cc */
#define IDLE_TIME 240.0f              /* DAT_0061a8d4 */
#define TURN_RATE (10.0f * PI / 180.0f) /* 0x5df650: 0x71c a frame over an action's first 30 frames */
#define TRAVEL_SPEED 50.0f            /* DAT_0061a91c */
#define BEHIND 100.0f                 /* 0x31: (0, 0, -100) in Dante's frame */
#define AWAY 1000.0f                  /* 0x3e: DAT_0061a8ac ahead of Dante */
#define AWAY_MIN 600.0f               /* DAT_0061a928 */
#define DASH_NEAR 300.0f              /* DAT_0061aa3c */
#define BLOCK_HOLD 15.0f              /* 0x5dfad0[rank 0] */
#define LEAP_SPEED 45.0f              /* DAT_0061a8f0 */
#define GRAVITY 1.0f                  /* [inferred] the enemies' -1 a frame (docs/ENEMY_SPECS.md, Enigma) */

static float wrap_angle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static float unit_random(void) { return (float)enemy_ai_random() * 2.3283064e-10f; }
static int pick(const Pick *list, int count, int avoid) {
    int i, total = 0, roll;
    for (i = 0; i < count; ++i) if (list[i].state != avoid) total += list[i].weight;
    if (!total) return list[0].state;
    roll = (int)(unit_random() * (float)total);
    for (i = 0; i < count; ++i) {
        if (list[i].state == avoid) continue;
        if ((roll -= list[i].weight) < 0) return list[i].state;
    }
    return list[count - 1].state;
}
static float dante_distance(void) {
    float dx = vb.dante[0] - vb.pos[0], dz = vb.dante[2] - vb.pos[2];
    return sqrtf(dx * dx + dz * dz);
}
static float angle_to_dante(void) { return atan2f(vb.dante[0] - vb.pos[0], vb.dante[2] - vb.pos[2]); }

static void trace(const char *event) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "t=%d f=%d %s state=%#x step=%d action=%#x script=%d:%d frame=%.1f hp=%.0f gauge=%.1f d=%.0f pos=%.0f,%.0f,%.0f\n",
            vb.tick, testroom_frame, event, vb.state, vb.step, vb.action, vb.body.bank, vb.body.script, vb.body.frame,
            vb.hp, vb.gauge, dante_distance(), vb.pos[0], vb.pos[1], vb.pos[2]);
    fclose(f);
}
void vergil_boss_note(const char *event, float a, float b, float c, float d) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "f=%d %s %.1f %.1f %.1f %.1f\n", testroom_frame, event, a, b, c, d);
    fclose(f);
}
void vergil_boss_trace(const char *path) {
    trace_on = path && *path;
    if (trace_on) snprintf(trace_path, sizeof(trace_path), "%s", path);
}

/* ---- Transforms -------------------------------------------------------------------------------------- */
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
    float c = cosf(vb.yaw), s = sinf(vb.yaw);
    out[0] = vb.pos[0] + c * p[0] + s * p[2];
    out[1] = vb.pos[1] + p[1];
    out[2] = vb.pos[2] - s * p[0] + c * p[2];
}

/* ---- Motion scripts (em035 chunk 11) ------------------------------------------------------------------ */
static int script_motion(int id) {
    int file = id / 100, index = id % 100;
    return file >= 0 && file < 4 ? motion_ids[file][index] : -1;
}
static void root_at(int motion, float frame, float out[3]) {
    const Motion *m = motion_get(motion);
    if (!m) { out[0] = out[1] = out[2] = 0.0f; return; }
    motion_root(m, frame, out);
}
static void script_run_ops(ScriptPlayer *p) {
    int guard;
    for (guard = 0; guard < 160; ++guard) {
        const VergilOp *op = &vergil_ops[p->pc++];
        if (op->op == OP_PLAY) {
            int blend = op->c & 0xFF;
            p->previous_motion = p->motion;
            p->previous_frame = p->frame;
            p->fade = blend > 0 && p->motion >= 0 ? 1.0f : 0.0f;
            p->fade_rate = blend > 0 ? 1.0f / (float)blend : 1.0f;
            p->motion = script_motion(op->a);
            p->loop = (op->c >> 8) & 1;
            if (p->motion < 0) { p->done = 1; return; }
            p->frame = op->b < 0 ? motion_get(p->motion)->loop_start : (float)op->b;
            root_at(p->motion, p->frame, p->root);
        } else if (op->op == OP_WAIT) {
            p->stop = op->b;
            p->finish = op->a;
            return;
        } else if (op->op == OP_HIT) {
            if (op->a >= 0 && op->a < CAPS) {
                p->cap_left[op->a] = op->b < 0 ? 1000000.0f : (float)op->b;
                p->cap_record[op->a] = (unsigned char)op->c;
                p->cap_hit[op->a] = 0;
            }
        } else if (op->op == OP_GOTO) {
            p->pc = op->b;
        } else if (op->op == OP_EVENT) {
            if (op->a == 0) { if (op->b) p->window0 = (unsigned char)op->b; }
            else p->window1 = (unsigned char)op->b;
        }
    }
    p->done = 1;
}
static void script_play(ScriptPlayer *p, int bank, int script) {
    p->bank = bank;
    p->script = script;
    p->done = 0;
    p->stop = -1;
    p->finish = 0;
    p->window1 = 0;
    memset(p->cap_left, 0, sizeof(p->cap_left));
    memset(p->cap_hit, 0, sizeof(p->cap_hit));
    if (bank < 0 || bank > 3 || script < 0 || script >= VERGIL_BANK_WIDTH || vergil_script_starts[bank][script] < 0) {
        p->done = 1;
        return;
    }
    p->pc = vergil_script_starts[bank][script];
    script_run_ops(p);
}
/* Advances the script; returns the root's motion this tick in model space (x, z) through *dx, *dz. */
static void script_tick(ScriptPlayer *p, float frames, float *dx, float *dz) {
    const Motion *motion;
    float stop, before[3], after[3];
    int i;
    *dx = *dz = 0.0f;
    p->fade = fmaxf(0.0f, p->fade - p->fade_rate * frames);
    for (i = 0; i < CAPS; ++i) p->cap_left[i] = fmaxf(0.0f, p->cap_left[i] - frames);
    if (p->done || p->motion < 0) return;
    motion = motion_get(p->motion);
    if (!motion) { p->done = 1; return; }
    memcpy(before, p->root, sizeof(before));
    stop = p->stop < 0 ? motion->end_frame : (float)p->stop;
    if (p->loop && p->stop < 0) {
        float next = p->frame + frames;
        if (next > stop) {
            float end[3], start[3];
            root_at(p->motion, motion->end_frame, end);
            next = motion_wrap(motion, next);
            root_at(p->motion, motion->loop_start, start);
            *dx += end[0] - before[0];
            *dz += end[2] - before[2];
            memcpy(before, start, sizeof(before));
        }
        p->frame = next;
    } else {
        p->frame = fminf(stop, p->frame + frames);
    }
    root_at(p->motion, p->frame, after);
    *dx += after[0] - before[0];
    *dz += after[2] - before[2];
    memcpy(p->root, after, sizeof(after));
    if (!p->loop || p->stop >= 0) {
        if (p->frame >= stop) {
            int motion_was = p->motion;
            if (p->finish) p->done = 1;
            else script_run_ops(p);
            if (p->motion != motion_was) root_at(p->motion, p->frame, p->root);
        }
    }
}

/* ---- Actions: FUN_004e0fc0's banks ---------------------------------------------------------------------- */
static void set_action(int action) {
    int bank, script;
    vb.action = action;
    vb.action_time = 0.0f;
    if (action <= 1) { bank = 0; script = action; }
    else if (action <= 5 || (action >= 0x11 && action <= 0x13)) { bank = 1; script = action - 2; }
    else if (action == 0x14) { bank = 2; script = 0; }
    else { bank = 3; script = action - 0x15; }
    script_play(&vb.body, bank, script);
    trace("action");
}
static int current_action(void) { return vb.body.done ? ACT_DONE : vb.action; }
static void set_state(int state) {
    vb.previous_state = vb.state;
    vb.state = state;
    vb.step = 0;
    trace("state");
}
static void turn_toward_dante(float frames) {
    float want = angle_to_dante(), diff = wrap_angle(want - vb.yaw), step = TURN_RATE * frames;
    if (vb.action_time >= 30.0f) return;
    vb.yaw = wrap_angle(vb.yaw + fmaxf(-step, fminf(step, diff)));
}
static void hide(int on) {
    if (on != vb.hidden) {
        float at[3] = {vb.pos[0], vb.pos[1] + 100.0f, vb.pos[2]};
        fx_hit_spark(at);   /* stands in for effect 0x338 [approximation] */
    }
    vb.hidden = on;
}
/* FUN_004e5ff0: move toward a point at TRAVEL_SPEED a frame; 1 once there. */
static int travel(const float target[3], float frames) {
    float d[3] = {target[0] - vb.pos[0], 0.0f, target[2] - vb.pos[2]}, len, step = TRAVEL_SPEED * frames;
    len = sqrtf(d[0] * d[0] + d[2] * d[2]);
    if (len <= step) {
        vb.pos[0] = target[0];
        vb.pos[2] = target[2];
        return 1;
    }
    vb.pos[0] += d[0] / len * step;
    vb.pos[2] += d[2] / len * step;
    return 0;
}
static void point_from_dante(float forward, float out[3]) {
    out[0] = vb.dante[0] + sinf(vb.dante_yaw) * forward;
    out[1] = vb.pos[1];
    out[2] = vb.dante[2] + cosf(vb.dante_yaw) * forward;
}

/* ---- The attack scheduler (+0x130) -------------------------------------------------------------------- */
static void fill_gauge(float frames) {
    float d = dante_distance();
    int i;
    for (i = 0; i < 3; ++i) if (d < gauge_bands[i][0]) { vb.gauge += gauge_bands[i][1] * frames; break; }
    if (i == 3) vb.gauge += GAUGE_SLOW * frames;
    if (d < GAUGE_CLOSE) vb.gauge = GAUGE_READY + 1.0f;
}
static int gauge_ready(void) { return vb.gauge > GAUGE_READY; }
static void start_attack(void) {
    float d = dante_distance();
    vb.gauge = 0.0f;
    if (d <= 350.0f) set_state(pick(near_picks, 2, -1));
    else if (d <= 500.0f) set_state(pick(mid_picks, 2, -1));
    else set_state(pick(far_picks, 1, -1));
}
/* FUN_004e5c60: Dante lunges at him (Stinger, actions 7/8; 17, 38/39, 58-60, 116) within 500 cm and
 * within 45 degrees of where he faces. */
static int dante_lunges(void) {
    static const unsigned char lunges[] = {7, 8, 17, 38, 39, 58, 59, 60, 116};
    unsigned i;
    int match = 0;
    for (i = 0; i < sizeof(lunges); ++i) if (vb.dante_action == lunges[i]) match = 1;
    if (!match || dante_distance() >= REACT_RANGE) return 0;
    return fabsf(wrap_angle(angle_to_dante() - vb.yaw)) < PI / 4.0f;
}
/* FUN_004deca0: rank 0, variant 0 guards every time (0x5df8f0: 100%). */
static void react(void) { set_state(S_GUARD); }

/* ---- AI states (FUN_004d6770's handlers) --------------------------------------------------------------- */
static void wander_checks(void) {
    if (dante_lunges()) { react(); return; }
    if (gauge_ready()) start_attack();
}
static void ai_step(float f) {
    int act = current_action();
    switch (vb.state) {
    case S_THINK:                                   /* FUN_004d7050 */
        if (vb.step == 0) { vb.step = 1; set_action(0); }
        if (dante_lunges()) { react(); break; }
        if (gauge_ready()) { start_attack(); break; }
        vb.last_wander = pick(wander_picks, 3, vb.last_wander);
        set_state(vb.last_wander);
        break;
    case S_STRAFE:                                  /* FUN_004d7b60: walk sideways, drift in from beyond 500 */
        if (vb.step == 0) {
            vb.step = 1;
            vb.turned_back = unit_random() < 0.5f;
            set_action(vb.turned_back ? 0x5a : 0x59);
            vb.timer = STRAFE_TIME;
            break;
        }
        if (dante_distance() > 500.0f) {
            vb.pos[0] += sinf(vb.yaw) * f;
            vb.pos[2] += cosf(vb.yaw) * f;
        }
        turn_toward_dante(f);
        if (act == ACT_DONE) set_action(vb.turned_back ? 0x5a : 0x59);
        if ((vb.timer -= f) < 0.0f) { set_state(S_THINK); break; }
        wander_checks();
        break;
    case S_IDLE:                                    /* FUN_004d81c0 */
        if (vb.step == 0) { vb.step = 1; set_action(0); vb.timer = IDLE_TIME; break; }
        turn_toward_dante(f);
        if (act == ACT_DONE) set_action(0);
        if ((vb.timer -= f) < 0.0f) { set_state(S_THINK); break; }
        wander_checks();
        break;
    case S_TAUNT:                                   /* FUN_004d82f0 */
        if (vb.step == 0) { vb.step = 1; set_action(0x14); break; }
        turn_toward_dante(f);
        if (act == ACT_DONE) { set_state(S_THINK); break; }
        wander_checks();
        break;
    case S_COMBO_A:                                 /* FUN_004d8410: 0x18 0x19 0x1a, then 0x47 */
    case S_COMBO_B: {                               /* FUN_004d85f0: 0x18 0x19 0x1b 0x1c, then 0x47 */
        static const unsigned char a[] = {0x18, 0x19, 0x1a}, b[] = {0x18, 0x19, 0x1b, 0x1c};
        const unsigned char *seq = vb.state == S_COMBO_A ? a : b;
        int n = vb.state == S_COMBO_A ? 3 : 4;
        if (vb.step == 0) { vb.step = 1; set_action(seq[0]); break; }
        if (vb.step < n) {
            if (vb.body.window1 == 1) set_action(seq[vb.step++]);
        } else if (vb.step == n) {
            if (act == ACT_DONE) { vb.step++; set_action(0x47); }
        } else if (act == ACT_DONE) set_state(S_THINK);
        break;
    }
    case S_COMBO_C:                                 /* FUN_004d9220: 0x1b 0x1c, then 0x47 */
        if (vb.step == 0) { vb.step = 1; set_action(0x1b); }
        else if (vb.step == 1) { if (vb.body.window1 == 1) { vb.step = 2; set_action(0x1c); } }
        else if (vb.step == 2) { if (act == ACT_DONE) { vb.step = 3; set_action(0x47); } }
        else if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_DASH:                                    /* FUN_004d99c0: 0x28, 0x29 unless near, 0x2a */
        if (vb.step == 0) { vb.step = 1; set_action(0x28); }
        else if (vb.step == 1) {
            if (act == ACT_DONE) {
                if (dante_distance() < DASH_NEAR) { vb.step = 3; set_action(0x2a); }
                else { vb.step = 2; set_action(0x29); }
            }
        } else if (vb.step == 2) {
            if (dante_distance() < DASH_NEAR || act == ACT_DONE) { vb.step = 3; set_action(0x2a); }
        } else if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_BEHIND:                                  /* FUN_004dac60: vanish, reappear behind him, upper slash */
        if (vb.step == 0) { vb.step = 1; vb.yaw = angle_to_dante(); set_action(0x54); }
        else if (vb.step == 1) {
            if (act == ACT_DONE) { vb.step = 2; hide(1); set_action(0x55); }
        } else if (vb.step == 2) {
            point_from_dante(-BEHIND, vb.travel);
            if (travel(vb.travel, f)) { vb.step = 3; hide(0); }
        } else if (vb.step == 3) {
            vb.step = 4;
            vb.yaw = vb.dante_yaw;
            set_action(0x54);
        } else if (vb.step == 4) {
            if (act == ACT_DONE) { vb.step = 5; vb.timer = 5.0f; }
        } else if ((vb.timer -= f) < 0.0f) set_state(vb.previous_state == S_BEHIND ? S_THINK : S_UPPER);
        break;
    case S_UPPER:                                   /* FUN_004d9680: 0x27, chains into the leap */
        if (vb.step == 0) { vb.step = 1; set_action(0x27); }
        else if (vb.body.window1 == 1) set_state(S_LEAP);
        break;
    case S_LEAP:                                    /* FUN_004dd840: 0x5f toward Dante, 0x60 on landing */
        if (vb.step == 0) {
            float d = dante_distance(), pitch = atan2f(vb.dante[1] - vb.pos[1], d);
            pitch = fmaxf(25.0f * PI / 180.0f, fminf(75.0f * PI / 180.0f, pitch));   /* DAT_0061a8f4/f8 */
            vb.step = 1;
            vb.yaw = angle_to_dante();
            set_action(0x5f);
            vb.vel[0] = sinf(vb.yaw) * cosf(pitch) * LEAP_SPEED;
            vb.vel[1] = sinf(pitch) * LEAP_SPEED;
            vb.vel[2] = cosf(vb.yaw) * cosf(pitch) * LEAP_SPEED;
            /* FUN_003599f0 only snaps him to the floor; what brings him down isn't decoded. The arc is solved
             * to come down where Dante stands [approximation]. */
            vb.gravity = fmaxf(GRAVITY, 2.0f * vb.vel[1] * cosf(pitch) * LEAP_SPEED / fmaxf(d, 50.0f));
        } else if (vb.step == 1) {
            if (vb.body.window1 == 1) {
                vb.pos[0] += vb.vel[0] * f;
                vb.pos[1] += vb.vel[1] * f;
                vb.pos[2] += vb.vel[2] * f;
                vb.vel[1] -= vb.gravity * f;
                if (vb.vel[1] < 0.0f && vb.pos[1] <= vb.floor_y) {
                    vb.pos[1] = vb.floor_y;
                    vb.step = 2;
                    set_action(0x60);
                }
            }
        } else if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_GUARD:                                   /* FUN_004db1d0 */
        if (vb.step == 0) { vb.step = 1; set_action(0x51); vb.blocked = 0; vb.block_time = 0.0f; break; }
        if (vb.blocked) {
            vb.block_time += f;
            if (vb.block_time > BLOCK_HOLD) set_state(pick(counter_picks, 2, -1));
        } else if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_AWAY:                                    /* FUN_004ddbf0: vanish, reappear 1000 ahead of Dante */
        if (vb.step == 0) { vb.step = 1; vb.yaw = angle_to_dante(); set_action(0x54); }
        else if (vb.step == 1) { if (act == ACT_DONE) { vb.step = 2; hide(1); set_action(0x55); } }
        else if (vb.step == 2) {
            point_from_dante(AWAY, vb.travel);
            if (travel(vb.travel, f)) {
                if (dante_distance() > AWAY_MIN) { vb.step = 3; hide(0); }
                else vb.step = 10;
            }
        } else if (vb.step == 10) {
            if (travel(vb.centre, f)) { vb.step = 3; hide(0); }
        } else if (vb.step == 3) {
            vb.step = 4;
            vb.yaw = wrap_angle(vb.dante_yaw + PI);
            set_action(0x54);
        } else if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_FLINCH:                                  /* [approximation] the hit reaction isn't decoded */
        if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_DEATH:
        break;
    }
    if (vb.state == S_THINK || vb.state == S_STRAFE || vb.state == S_IDLE || vb.state == S_TAUNT) fill_gauge(f);
}

/* ---- Skeletal animation and drawing ------------------------------------------------------------------- */
static void build_skeleton(const VergilMesh *mesh, const MotionSample *sample, const MotionSample *previous,
                           float fade, const Transform *attachment, Transform *out, int root_motion) {
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
        if (i == 0 && root_motion) { p[0] = 0.0f; p[2] = 0.0f; }   /* the root's x/z moved him instead */
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
/* An attach record (FUN_004e4770): {position, rotation, bone}. */
static void attach(int bone, float x, float y, float z, float rx, float ry, float rz, Transform *out) {
    Transform offset, r;
    int i, j;
    /* The EE's matrices act on row vectors: its rotation is this one's transpose. */
    euler(&r, rx, ry, rz);
    identity(&offset);
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) offset.m[i][j] = r.m[j][i];
    offset.p[0] = x; offset.p[1] = y; offset.p[2] = z;
    multiply(out, &body_pose[bone], &offset);
}
static void evaluate_poses(void) {
    static MotionSample current, previous;
    Transform at;
    if (vb.body.motion >= 0) motion_sample(motion_get(vb.body.motion), vb.body.frame, &current);
    else memset(&current, 0, sizeof(current));
    if (vb.body.fade > 0.0f && vb.body.previous_motion >= 0)
        motion_sample(motion_get(vb.body.previous_motion), vb.body.previous_frame, &previous);
    build_skeleton(&vergil_parts[PART_BODY], &current, vb.body.fade > 0.0f ? &previous : 0, vb.body.fade, 0, body_pose, 1);
    /* The coat hangs from body bone 3 (its cloth is bound to the bone list's entry 3, as Dante's is);
     * drawn rigid [approximation: no cloth]. */
    memset(&current, 0, sizeof(current));
    build_skeleton(&vergil_parts[PART_COAT], &current, 0, 0.0f, &body_pose[3], coat_pose, 0);
    /* Scabbard in the left hand (0x5dc010: bone 13); Yamato in it (0x5dbfe0) or drawn in the right (0x5dc0a0). */
    attach(13, 19.0f, -0.5f, 11.0f, 0.0f, 3.84f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_SCABBARD], &current, 0, 0.0f, &at, scabbard_pose, 0);
    {
        /* Its cord (bones 2-6, cloth wp005.clt) runs along -x from the knot at bone 2: hang it straight down
         * from there [approximation: no cloth]. Rz(90) takes -x to -y. */
        const VergilMesh *m = &vergil_parts[PART_SCABBARD];
        int i, k;
        for (i = 3; i < m->bone_count && i < 7; ++i) {
            float d[3] = {m->rest[i].x - m->rest[2].x, m->rest[i].y - m->rest[2].y, m->rest[i].z - m->rest[2].z};
            identity(&scabbard_pose[i]);
            scabbard_pose[i].m[0][0] = 0.0f; scabbard_pose[i].m[0][1] = -1.0f;
            scabbard_pose[i].m[1][0] = 1.0f; scabbard_pose[i].m[1][1] = 0.0f;
            for (k = 0; k < 3; ++k)
                scabbard_pose[i].p[k] = scabbard_pose[2].p[k] + scabbard_pose[i].m[k][0] * d[0] +
                                        scabbard_pose[i].m[k][1] * d[1] + scabbard_pose[i].m[k][2] * d[2];
        }
    }
    if (vb.yamato_drawn) attach(9, -7.5f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_YAMATO], &current, 0, 0.0f, &at, yamato_pose, 0);
}
static const Transform *part_transforms(int part) {
    switch (part) {
    case PART_COAT: return coat_pose;
    case PART_YAMATO: return yamato_pose;
    case PART_SCABBARD: return scabbard_pose;
    default: return body_pose;
    }
}
static void skin_part(int part) {
    const VergilMesh *m = &vergil_parts[part];
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
    const VergilMesh *m = &vergil_parts[part];
    int i, bound = -1;
    for (i = 0; i < m->objects_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if ((int)o->texture != bound) {
            const Dmc3Texture *t;
            bound = (int)o->texture;
            if (bound >= VERGIL_TEXTURE_COUNT) bound = 0;
            t = &vergil_textures[bound];
            sceGuClutLoad(32, vergil_palettes[bound]);
            sceGuTexImage(0, t->width, t->height, t->width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[part] + o->index_start, vertices[part]);
    }
}
void vergil_boss_draw(void) {
    int i;
    if (!vb.active || vb.hidden) return;
    evaluate_poses();
    for (i = 0; i < VERGIL_PART_COUNT; ++i) skin_part(i);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {vb.pos[0], vb.pos[1], vb.pos[2]};
        sceGumTranslate(&p);
    }
    sceGumRotateY(vb.yaw);
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
    for (i = 0; i < VERGIL_PART_COUNT; ++i) draw_part(i);
    sceGuDisable(GU_ALPHA_TEST);
}

/* ---- Hits both ways --------------------------------------------------------------------------------------- */
static int sphere_hit(const float pos[3], float radius, float px, float py, float pz) {
    float near_y = fmaxf(py + DANTE_LOW, fminf(py + DANTE_HIGH, pos[1]));
    float dx = pos[0] - px, dy = pos[1] - near_y, dz = pos[2] - pz, r = radius + DANTE_RADIUS;
    return dx * dx + dy * dy + dz * dz <= r * r;
}
static void test_attack_caps(float px, float py, float pz) {
    int i;
    for (i = 0; i < CAPS; ++i) {
        const VergilCap *cap = &vergil_caps[i];
        float local[3] = {cap->x, cap->y, cap->z}, posed[3], world[3];
        if (vb.body.cap_left[i] <= 0.0f || vb.body.cap_hit[i] || cap->kind != 1) continue;
        transform_point(&body_pose[cap->bone < 24 ? cap->bone : 0], local, posed);
        local_to_world(posed, world);
        if (!sphere_hit(world, cap->radius, px, py, pz)) continue;
        vb.body.cap_hit[i] = 1;
        if (!vb.landed) {
            const VergilAttack *a = &vergil_attacks[vb.body.cap_record[i] < 20 ? vb.body.cap_record[i] : 0];
            vb.landed = 1;
            vb.landed_x = vb.pos[0];
            vb.landed_z = vb.pos[2];
            vb.landed_damage = a->damage;
            vb.landed_heavy = a->kind != 0 || a->reaction >= 500.0f;
            trace("landed");
        }
    }
}
int vergil_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage) {
    if (!vb.landed) return 0;
    vb.landed = 0;
    *from_x = vb.landed_x;
    *from_z = vb.landed_z;
    *heavy = vb.landed_heavy;
    *damage = vb.landed_damage;
    return 1;
}
static int interruptible(void) {
    return vb.state == S_THINK || vb.state == S_STRAFE || vb.state == S_IDLE || vb.state == S_TAUNT || vb.state == S_FLINCH;
}
int vergil_boss_hit(int kind, float from_x, float from_z, int mortal) {
    float at[3] = {vb.pos[0], vb.pos[1] + 110.0f, vb.pos[2]}, damage, toward;
    if (!vb.active || vb.dying || vb.hidden) return 0;
    fx_hit_spark(at);
    /* The guard (action 0x51) turns back what comes from the front: +0xbb51 starts its counter clock. */
    toward = atan2f(from_x - vb.pos[0], from_z - vb.pos[2]);
    if (vb.state == S_GUARD && vb.action == 0x51 && fabsf(wrap_angle(toward - vb.yaw)) < PI / 2.0f) {
        vb.blocked = 1;
        trace("blocked");
        return 0;
    }
    /* As Cerberus's: a light hit 42, a heavy one 84, x1.1; Easy x2. Guns: the param block's 20% [inferred]. */
    damage = kind == HIT_PISTOL ? 10.0f * 0.2f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    if (!mortal) damage = 0.0f;
    vb.hp = fmaxf(0.0f, vb.hp - damage);
    trace("hit");
    if (vb.hp <= 0.0f) {
        vb.dying = 1;
        set_state(S_DEATH);
        script_play(&vb.body, 1, 11);   /* the long reaction (motion 111) [approximation: the demo takes over] */
        vb.action = 0x13;
        return 1;
    }
    /* Flinches only between attacks [approximation: CEm035's reaction table 0x5dcc90 isn't decoded]. */
    if (kind != HIT_PISTOL && interruptible()) {
        set_state(S_FLINCH);
        vb.step = 1;
        set_action(kind == HIT_LIGHT || kind == HIT_AIR ? ((vb.tick & 1) ? 3 : 2) : 0x12);
    }
    return 0;
}

/* ---- Frame ------------------------------------------------------------------------------------------------ */
void vergil_boss_update(float frames, float px, float py, float pz, int attacks) {
    float dx, dz, c, s;
    if (!vb.active) return;
    ++vb.tick;
    vb.dante[0] = px; vb.dante[1] = py; vb.dante[2] = pz;
    if (attacks && !vb.dying) ai_step(frames);
    vb.action_time += frames;
    script_tick(&vb.body, frames, &dx, &dz);
    /* FUN_004e4570: the root's motion, turned by his facing, moves him. */
    c = cosf(vb.yaw); s = sinf(vb.yaw);
    vb.pos[0] += c * dx + s * dz;
    vb.pos[2] += -s * dx + c * dz;
    if (vb.body.window0 == 1) vb.yamato_drawn = 1;
    else if (vb.body.window0 == 2) vb.yamato_drawn = 0;
    vb.body.window0 = 0;
    if (floor_fn && vb.state != S_LEAP) {
        float y;
        if (floor_fn(vb.pos[0], vb.pos[1] + 100.0f, vb.pos[2], &y)) vb.floor_y = y;
        vb.pos[1] = vb.floor_y;
    } else if (vb.state != S_LEAP) vb.pos[1] = vb.floor_y;
    if (!vb.hidden && !vb.dying) {
        evaluate_poses();
        test_attack_caps(px, py, pz);
    }
}
void vergil_boss_spawn(float x, float y, float z) {
    memset(&vb, 0, sizeof(vb));
    vb.active = 1;
    vb.pos[0] = x; vb.pos[1] = vb.floor_y = y; vb.pos[2] = z;
    /* 0x5df970: Mission 7's arena centre, where 0x3e goes when there's no room ahead of Dante. */
    if (fabsf(x - 2700.0f) < 1.0f && fabsf(z - 3500.0f) < 1.0f) { vb.centre[0] = 3500.0f; vb.centre[2] = 3500.0f; }
    else { vb.centre[0] = x; vb.centre[2] = z; }
    vb.centre[1] = y;
    vb.yaw = PI;
    vb.hp = MAX_HEALTH;
    vb.body.motion = vb.body.previous_motion = -1;
    vb.last_wander = -1;
    vb.state = S_THINK;
    set_action(0);
    trace("spawn");
}
void vergil_boss_kill(void) { if (vb.active && !vb.dying) { vb.hp = 0.0f; vb.dying = 1; set_state(S_DEATH); } }
int vergil_boss_dying(void) { return vb.dying; }
int vergil_boss_hidden(void) { return vb.hidden; }
float vergil_boss_health(void) { return vb.hp; }
float vergil_boss_max_health(void) { return MAX_HEALTH; }
void vergil_boss_position(float out[3]) { memcpy(out, vb.pos, sizeof(vb.pos)); }
void vergil_boss_target_position(float out[3]) { out[0] = vb.pos[0]; out[1] = vb.pos[1] + 110.0f; out[2] = vb.pos[2]; }
void vergil_boss_push_out(float *x, float *z, float y, float r) {
    float dx, dz, d, min = BODY_RADIUS + r;
    if (!vb.active || vb.hidden || vb.dying) return;
    if (y > vb.pos[1] + 190.0f || y + 180.0f < vb.pos[1]) return;
    dx = *x - vb.pos[0]; dz = *z - vb.pos[2];
    d = sqrtf(dx * dx + dz * dz);
    if (d >= min) return;
    if (d < 0.01f) { dx = 0.0f; dz = 1.0f; d = 1.0f; }
    *x = vb.pos[0] + dx / d * min;
    *z = vb.pos[2] + dz / d * min;
}
int vergil_boss_init(void) {
    int file, i, j;
    for (file = 0; file < 4; ++file)
        for (i = 0; i < 100; ++i) {
            char name[8];
            snprintf(name, sizeof(name), "vr%d-%02d", file, i);
            motion_ids[file][i] = motion_find(name);
        }
    if (motion_ids[0][0] < 0 || motion_ids[3][3] < 0) return 0;
    for (i = 0; i < VERGIL_PART_COUNT; ++i) {
        const VergilMesh *m = &vergil_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices);
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) return 0;
        memcpy(indices[i], m->indices, sizeof(unsigned short) * m->indices_count);
        for (j = 0; j < m->vertices; ++j) {
            vertices[i][j].u = m->uvs[j].u;
            vertices[i][j].v = m->uvs[j].v;
        }
    }
    for (i = 0; i < VERGIL_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &vergil_textures[i];
        size_t bytes = (size_t)t->width * t->height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}
