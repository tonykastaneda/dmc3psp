#include "vergil_boss.h"

#include "demo_cloth.h"
#include "enemy.h"
#include "fx.h"
#include "motion.h"
#include "nfx.h"
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

enum { PART_BODY, PART_COAT, PART_YAMATO, PART_SCABBARD, PART_BEOWULF, PART_DT_BODY, PART_FORCE_EDGE, PART_M20_DT_BODY, PART_M20_DT_WINGS, PART_M20_DT_DETAIL, PART_M20_DT_CLOTH };
enum { OP_PLAY, OP_WAIT, OP_HIT, OP_GOTO, OP_EVENT, OP_ATTACK, OP_WINDOW4 };

/* AI states (+0xb9d4), from the switch in FUN_004d6770. */
enum {
    S_THINK = 0x17, S_DEATH = 0x18, S_STRAFE = 0x1d, S_IDLE = 0x1f, S_TAUNT = 0x20,
    S_COMBO_A = 0x21, S_COMBO_B = 0x22, S_COMBO_C = 0x25, S_UPPER = 0x29, S_DASH = 0x2d, S_BEHIND = 0x31,
    S_GUARD = 0x33, S_LEAP = 0x3d, S_AWAY = 0x3e, S_BRACE = 0x32, S_FLINCH = 0x36, S_FLINCH_BACK = 0x37, S_APPROACH = 0x19, S_BEOWULF_WARP=0x2b, S_BEOWULF=0x2f, S_FORCE_A=0x23, S_FORCE_B=0x24, S_JUDGMENT=0x30, S_ROUND_TRIP=0x3a, S_DIVE=0x3b, S_RETREAT=0x2e, S_CUT_RANDOM=0x3f, S_CUT_RING=0x40, S_STORM=0x3c, S_DT=0x39
};

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;
typedef struct {
    int bank, script, pc, motion, previous_motion, loop, stop, finish, done;
    float frame, previous_frame, fade, fade_rate, root[3];
    float cap_left[CAPS];
    unsigned char cap_record[CAPS], cap_hit[CAPS];
    unsigned char window0, window1, window4;   /* byte 0 of each: Yamato's place, the chain point */
} ScriptPlayer;

static struct {
    int active, dying, hidden, state, step, previous_state, last_wander, action, rank;
    float pos[3], yaw, hp, gauge, timer, block_time, action_time, floor_y, vel[3];
    float centre[3], travel[3], dante[3], dante_yaw;
    int dante_action, blocked, yamato_drawn, turned_back;
    ScriptPlayer body;
    int landed, landed_heavy, landed_kind;
    float landed_x, landed_z, landed_damage;
    int tick, variant, dt, dt_unlocked, m13_chain, m20_low, m20_temp_dt, repeats, waves, cut_index, sword, sword_hit;
    float dt_left,dt_cooldown, cut[3], dive_height, retreat_speed, retreat_distance;
    float sword_pos[3], sword_dir[3], sword_age, sword_spin, sword_wait, sword_rehit;
    /* The damage component (+0xb864, vtable 0x605730): the stun and flinch gauges (+0xb818 / +0xb81c) and the frames
     * since the last blow (+0x10). The flinch budget (4e52f0): frames flinching (+0xbb0c) against its limit (+0xbb10),
     * points taken while flinching (+0xbb14) against theirs (+0xbb18). The brace's clock (+0xbb08). */
    float stun_gauge, flinch_gauge, since_blow, flinch_time, flinch_limit, flinch_points, flinch_cap, brace_time;
    float cloth_clock;   /* 60 a second, for the coat's and the cord's chains */
    int posed_tick;      /* the tick the update last posed him in */
} vb;

static int motion_ids[4][100];
static Vertex *vertices[VERGIL_PART_COUNT];
static unsigned short *indices[VERGIL_PART_COUNT];
static void *textures[VERGIL_TEXTURE_COUNT];
static Transform body_pose[24], coat_pose[40], yamato_pose[2], scabbard_pose[7];
static char trace_path[320];
static int trace_on, boss_difficulty = 1, boss_variant;
static Transform beowulf_pose[11], force_edge_pose[2], m20_wing_pose[15], m20_detail_pose[4], m20_cloth_pose[31];
void vergil_boss_set_variant(int variant){boss_variant=variant>=0 && variant<=2?variant:0;}
int vergil_boss_variant(void){return vb.variant;}
int vergil_boss_devil(void){return vb.dt;}
static float max_health(void){return vb.variant==2?10000.0f:MAX_HEALTH;}
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
/* Variant-1 rank-0 lists: 5df400, 5dcd00 and 5dcd70. M7 lists above stay unchanged. */
static const Pick m13_wander[]={{S_APPROACH,20},{S_STRAFE,40},{S_IDLE,20},{S_TAUNT,20}};
static const Pick m13_near[]={{S_COMBO_A,35},{S_COMBO_B,35},{S_BEOWULF_WARP,30}};
static const Pick m13_mid[]={{S_COMBO_C,20},{S_BEOWULF_WARP,20},{S_DASH,30},{S_BEHIND,30}};
/* Variant 2, Force Edge, rank 0: 5ddd80 / 5ddf00 / 5de080. */
static const Pick m20_wander[]={{S_APPROACH,65},{S_STRAFE,20},{S_IDLE,15}};
static const Pick m20_near[]={{S_FORCE_A,40},{S_FORCE_B,40},{S_DASH,20}};
static const Pick m20_mid[]={{S_COMBO_C,20},{S_DASH,30},{S_JUDGMENT,20},{S_BEHIND,30}};
static const Pick m20_far[]={{S_JUDGMENT,40},{S_BEHIND,40},{S_ROUND_TRIP,20}};
static const Pick m20_low_near[]={{S_FORCE_A,20},{S_FORCE_B,40},{S_BEOWULF_WARP,20},{S_DASH,20}};
/* Late human phase uses the native human lists (5dde40 / 5ddfc0 / 5de120).
 * Its zero-weight placeholders are omitted. */
static const Pick m20_low_mid[]={{S_DASH,25},{S_JUDGMENT,25},{S_CUT_RANDOM,25},{S_CUT_RING,25}};
static const Pick m20_low_far[]={{S_JUDGMENT,30},{S_ROUND_TRIP,25},{S_CUT_RANDOM,25},{S_CUT_RING,20}};
static const Pick m20_dt_near[]={{S_FORCE_B,65},{S_STORM,35}};
static const Pick m20_dt_far[]={{S_STORM,35},{S_BEHIND,65}};
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
/* Hit reactions (docs/VERGIL_SPEC.md, "Hit reactions"). */
#define STUN_FULL 60.0f               /* param block 0x5dca50 +0x30 */
#define FLINCH_FULL 60.0f             /* +0x34 */
#define GAUGE_REFILL 180.0f           /* FUN_002f1750: both refill once 180 frames pass without a blow */
#define FLINCH_LIMIT 600.0f           /* gp-0x576c (0x61aa04): frames of flinching a window allows */
#define FLINCH_CAP 12.0f              /* gp-0x5764 (0x61aa0c): points taken while flinching before the brace */
#define BRACE_REACT 60.0f             /* DAT_0061aa20: frames into the brace before a lunge is answered */

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
    fprintf(f, "t=%d f=%d %s state=%#x step=%d action=%#x script=%d:%d frame=%.1f hp=%.0f gauge=%.1f d=%.0f pos=%.0f,%.0f,%.0f variant=%d low=%d dt=%d left=%.0f cooldown=%.0f\n",
            vb.tick, testroom_frame, event, vb.state, vb.step, vb.action, vb.body.bank, vb.body.script, vb.body.frame,
            vb.hp, vb.gauge, dante_distance(), vb.pos[0], vb.pos[1], vb.pos[2],vb.variant,vb.m20_low,vb.dt,vb.dt_left,vb.dt_cooldown);
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
        } else if(op->op==OP_WINDOW4){p->window4=(unsigned char)op->a;
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
    p->window1 = 0;p->window4=0;
    memset(p->cap_left, 0, sizeof(p->cap_left));
    memset(p->cap_hit, 0, sizeof(p->cap_hit));
    if (bank < 0 || bank > 3 || script < 0 || script >= VERGIL_BANK_WIDTH || vergil_script_starts[bank][script] < 0) {
        p->done = 1;
        return;
    }
    p->pc = vergil_script_starts[bank][script];
    script_run_ops(p);
}
/* Advances the script; returns the root's motion this tick in model space through *dx, *dy, *dz. */
static void script_tick(ScriptPlayer *p, float frames, float *dx, float *dy, float *dz) {
    const Motion *motion;
    float stop, before[3], after[3];
    int i;
    *dx = *dy = *dz = 0.0f;
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
            *dy += end[1] - before[1];
            *dz += end[2] - before[2];
            memcpy(before, start, sizeof(before));
        }
        p->frame = next;
    } else {
        p->frame = fminf(stop, p->frame + frames);
    }
    root_at(p->motion, p->frame, after);
    *dx += after[0] - before[0];
    *dy += after[1] - before[1];
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
static void action_effects(int action);
static void set_action(int action) {
    action_effects(action);
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
    /* 4dee00 ends M20 DT at a state boundary, not halfway through an attack. */
    if(vb.variant==2 && vb.dt && !vb.m20_temp_dt && vb.dt_left<=0 && state!=S_DT){
        vb.dt=0;vb.dt_cooldown=2700;trace("dt_end");
    }
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
/* Where a body bone is in the world (its pose from the last draw). */
static void bone_world(int bone, float out[3]) {
    local_to_world(body_pose[bone].p, out);
}
/* FUN_004dac60 / 004ddbf0: V0x338 on the body's second bone (model +4) as he goes, and turned half round
 * (FUN_00105760(pi)) as he comes back. */
static void hide(int on) {
    if (on != vb.hidden) {
        float at[3];
        bone_world(1, at);
        int fx = nfx_spawn(NFX_VERGIL, 0x338, at, on ? vb.yaw : wrap_angle(vb.yaw + PI), 0);
        vergil_boss_note("fx338", (float)fx, at[0], at[1], at[2]);
    }
    vb.hidden = on;
}
/* FUN_004e0fc0: the slash an action starts on his body (model +0's matrix, effect +0x3a held), ended by the next
 * action. */
static int slash_fx[2];
static void action_effects(int action) {
    static const unsigned short slashes[][3] = {{0x1a, 0x2fc, 0}, {0x1b, 0x2fd, 0}, {0x1c, 0x2fe, 0}, {0x47, 0x2ff, 0},
                                                {0x2a, 0x300, 0x32e}, {0x27, 0x34f, 0}};
    unsigned i;
    float at[3];
    for (i = 0; i < 2; ++i) if (slash_fx[i]) { nfx_kill(slash_fx[i]); slash_fx[i] = 0; }
    if (vb.variant != 0) return;   /* Mission 7's sword; the other variants' weapons aren't sorted out */
    for (i = 0; i < sizeof(slashes) / sizeof(slashes[0]); ++i) {
        if (slashes[i][0] != action) continue;
        bone_world(0, at);
        slash_fx[0] = nfx_spawn(NFX_VERGIL, slashes[i][1], at, vb.yaw, 1);
        if (slashes[i][2]) slash_fx[1] = nfx_spawn(NFX_VERGIL, slashes[i][2], at, vb.yaw, 1);
        vergil_boss_note("slash", (float)slashes[i][1], (float)slash_fx[0], (float)nfx_live_elements(), 0.0f);
    }
}
static void action_effects_follow(void) {
    float at[3];
    int i;
    bone_world(0, at);
    for (i = 0; i < 2; ++i) if (slash_fx[i]) {
        if (!nfx_alive(slash_fx[i])) slash_fx[i] = 0;
        else nfx_move(slash_fx[i], at, vb.yaw);
    }
}
/* FUN_004e5ff0: move toward a point at TRAVEL_SPEED a frame; 1 once there. */
static int travel(const float target[3], float frames) {
    float point[3]={target[0],target[1],target[2]},d[3],len,step=TRAVEL_SPEED*frames;
    if(vb.variant>=1){float x=point[0]-vb.centre[0],z=point[2]-vb.centre[2],r=sqrtf(x*x+z*z);
        if(r>(vb.variant==2?1650:1100)){float limit=vb.variant==2?1650:1100;point[0]=vb.centre[0]+x/r*limit;point[2]=vb.centre[2]+z/r*limit;}}
    d[0]=point[0]-vb.pos[0];d[1]=0;d[2]=point[2]-vb.pos[2];
    len = sqrtf(d[0] * d[0] + d[2] * d[2]);
    if (len <= step) {
        vb.pos[0] = point[0];
        vb.pos[2] = point[2];
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
    if(vb.variant==2){
        if(vb.dt)set_state(d<=350?pick(m20_dt_near,2,-1):pick(m20_dt_far,2,-1));
        else if(vb.m20_low)set_state(d<=350?pick(m20_low_near,4,-1):d<=500?pick(m20_low_mid,4,-1):pick(m20_low_far,4,-1));
        else set_state(d<=350?pick(m20_near,3,-1):d<=500?pick(m20_mid,4,-1):pick(m20_far,3,-1));
        return;
    }
    if(vb.variant==1 && d<=350) {set_state(pick(m13_near,3,-1));return;}
    if(vb.variant==1 && d<=500) {set_state(pick(m13_mid,4,-1));return;}
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
static void react(void) { set_state(vb.variant>=1 && unit_random()>=(vb.variant==2?.8f:.9f)?S_AWAY:S_GUARD); }

static void m20_roundtrip(float f){
    float x,y,z,d;
    if(vb.action==0x69 && vb.body.window4==3 && !vb.sword){
        vb.body.window4=0;vb.sword=1;vb.sword_age=0;vb.sword_spin=0;vb.sword_rehit=0;
        vb.sword_pos[0]=vb.pos[0];vb.sword_pos[1]=vb.pos[1]+120;vb.sword_pos[2]=vb.pos[2];
        vb.sword_dir[0]=sinf(vb.yaw);vb.sword_dir[1]=0;vb.sword_dir[2]=cosf(vb.yaw);trace("round_trip_release");
    }
    if(!vb.sword)return;
    vb.sword_spin+=f*(5461.0f/65536.0f)*2*PI;vb.sword_age+=f;vb.sword_rehit-=f;
    if(vb.sword==1){
        vb.sword_pos[0]+=vb.sword_dir[0]*20*f;vb.sword_pos[2]+=vb.sword_dir[2]*20*f;
        x=vb.sword_pos[0]-vb.dante[0];z=vb.sword_pos[2]-vb.dante[2];
        if(x*x+z*z<130*130 && fabsf(vb.sword_pos[1]-vb.dante[1]-100)<180){vb.sword=2;vb.sword_wait=100;}
        else if(vb.sword_age>=100)vb.sword=3; /* no-hit range cap: port approximation */
    }else if(vb.sword==2){if((vb.sword_wait-=f)<0)vb.sword=3;}
    else {
        x=vb.pos[0]-vb.sword_pos[0];y=vb.pos[1]+120-vb.sword_pos[1];z=vb.pos[2]-vb.sword_pos[2];d=sqrtf(x*x+y*y+z*z);
        if(d<100){vb.sword=0;trace("round_trip_return");return;}
        vb.sword_pos[0]+=x/d*20*f;vb.sword_pos[1]+=y/d*20*f;vb.sword_pos[2]+=z/d*20*f;
    }
    x=vb.sword_pos[0]-vb.dante[0];y=vb.sword_pos[1]-vb.dante[1]-100;z=vb.sword_pos[2]-vb.dante[2];
    if(x*x+y*y+z*z<130*130 && vb.sword_rehit<=0){vb.sword_rehit=10;vb.landed=1;vb.landed_x=vb.sword_pos[0];vb.landed_z=vb.sword_pos[2];vb.landed_damage=vergil_attacks[10].damage;vb.landed_heavy=1;}
}

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
        if((vb.variant==1 && vb.hp<max_health()*.75f && !vb.dt_unlocked) || (vb.variant==2 && vb.m20_low && !vb.dt && vb.dt_cooldown<=0)){set_state(S_DT);break;}
        if (dante_lunges()) { react(); break; }
        if (gauge_ready()) { start_attack(); break; }
        {
            /* FUN_004d7050: a pick that repeats the last wander state becomes the list's first other entry,
             * 0x19 (its weight is 0): the walk in. The PS2 does it (analysis/ref/vergil_ps2.txt). */
            int w = vb.variant==2?pick(m20_wander,3,-1):vb.variant==1?pick(m13_wander,4,-1):pick(wander_picks, 3, -1);
            if (w == vb.last_wander) w = S_APPROACH;
            vb.last_wander = w;
            set_state(w);
        }
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
    case S_APPROACH:                                /* FUN_004d7530: action 1 (walk) toward him, 360 frames */
        if (vb.step == 0) { vb.step = 1; set_action(1); vb.timer = 360.0f; break; }
        turn_toward_dante(f);
        if (act == ACT_DONE) set_action(1);
        if ((vb.timer -= f) < 0.0f) { set_state(S_THINK); break; }
        wander_checks();
        break;
    case S_IDLE:                                    /* FUN_004d81c0 */
        if (vb.step == 0) { vb.step = 1; set_action(0); vb.timer = IDLE_TIME; break; }
        turn_toward_dante(f);   /* action 0 runs out and he holds (0x77), as the PS2 does */
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
    case S_FORCE_A: /* 4d8830: 18 -> 19 -> 65, optional follow-up assumed. */
    case S_FORCE_B: { /* 4d8a20: 18 -> 19 -> 1b, then teleport/dive. */
        static const unsigned char a[]={0x18,0x19,0x65},b[]={0x18,0x19,0x1b};
        const unsigned char *seq=vb.state==S_FORCE_A?a:b;
        if(vb.step==0){vb.step=1;set_action(seq[0]);}
        else if(vb.step<3 && (vb.body.window1==1 || act==ACT_DONE))set_action(seq[vb.step++]);
        else if(vb.step==3 && act==ACT_DONE){if(vb.state==S_FORCE_B)set_state(S_DIVE);else set_state(S_THINK);}
        break;
    }
    case S_JUDGMENT: /* 4da300: rank-0 hold 8, three cut releases, final 2d. */
        if(vb.step==0){vb.step=1;vb.yaw=angle_to_dante();set_action(0x2b);vb.timer=8;vb.repeats=3;}
        else if(vb.step==1 && act==ACT_DONE){vb.step=2;memcpy(vb.cut,vb.dante,sizeof(vb.cut));set_action(0x2c);}
        else if(vb.step==2){
            fx_guard_flash(vb.cut);
            if((vb.timer-=f)<0 && act==ACT_DONE){float x=vb.dante[0]-vb.cut[0],z=vb.dante[2]-vb.cut[2];
                fx_hit_spark(vb.cut);
                if(x*x+z*z<180*180 && fabsf(vb.dante[1]-vb.cut[1])<220){vb.landed=1;vb.landed_x=vb.cut[0];vb.landed_z=vb.cut[2];vb.landed_damage=1000;vb.landed_heavy=1;}
                trace("judgment_cut");
                if(--vb.repeats>0){vb.timer=0;memcpy(vb.cut,vb.dante,sizeof(vb.cut));set_action(0x2c);}
                else {vb.step=3;set_action(0x2d);}
            }
        }else if(vb.step==3 && act==ACT_DONE)set_state(S_THINK);
        break;
    case S_CUT_RANDOM:
    case S_CUT_RING: /* 4ddff0: 6a -> 6b, patterned cuts every 15 frames, 6c -> 6e. */
        if(vb.step==0){vb.step=1;if(!vb.dt){vb.dt=1;vb.m20_temp_dt=1;}set_action(0x6a);}
        else if(vb.step==1 && act==ACT_DONE){vb.step=2;set_action(0x6b);}
        else if(vb.step==2 && act==ACT_DONE){vb.step=3;vb.timer=15;vb.cut_index=0;vb.waves=vb.state==S_CUT_RANDOM?3:1;vb.repeats=vb.state==S_CUT_RANDOM?6:15;}
        else if(vb.step==3 && (vb.timer-=f)<0){
            float x,z,angle=vb.cut_index*PI/6,radius;
            memcpy(vb.cut,vb.dante,sizeof(vb.cut));
            if(vb.state==S_CUT_RING && vb.cut_index<11){radius=250+(11-vb.cut_index)*40;vb.cut[0]+=sinf(angle)*radius;vb.cut[2]+=cosf(angle)*radius;}
            else if(vb.state==S_CUT_RANDOM){radius=unit_random()*150;angle=unit_random()*2*PI;vb.cut[0]+=sinf(angle)*radius;vb.cut[2]+=cosf(angle)*radius;}
            fx_hit_spark(vb.cut);trace("area_cut");x=vb.dante[0]-vb.cut[0];z=vb.dante[2]-vb.cut[2];
            if(x*x+z*z<180*180){vb.landed=1;vb.landed_x=vb.cut[0];vb.landed_z=vb.cut[2];vb.landed_damage=1000;vb.landed_heavy=1;}
            ++vb.cut_index;vb.timer=15;
            if(--vb.repeats<=0){if(--vb.waves>0){vb.repeats=6;vb.timer=0;}else{vb.step=4;vb.timer=60;}}
        }else if(vb.step==4 && (vb.timer-=f)<0){vb.step=5;set_action(0x6c);}
        else if(vb.step==5 && act==ACT_DONE){vb.step=6;if(vb.m20_temp_dt){vb.dt=0;vb.m20_temp_dt=0;}set_action(0x6e);}
        else if(vb.step==6 && act==ACT_DONE)set_state(S_THINK);
        break;
    case S_ROUND_TRIP: /* 4dcc80: action 69 is Round Trip, released by event byte 4 == 3. */
        if(vb.step==0){if(vb.sword){set_state(S_THINK);break;}vb.step=1;set_action(0x69);}
        else if(act==ACT_DONE)set_state(S_THINK);
        break;
    case S_DIVE:
    case S_STORM: /* 4dcdb0 / 4dd290: one dive / four dives at descending heights. */
        if(vb.step==0){vb.step=1;vb.repeats=vb.state==S_STORM?4:1;vb.dive_height=vb.state==S_STORM?1000:600;set_action(0x54);}
        else if(vb.step==1 && act==ACT_DONE){
            vb.step=2;hide(1);point_from_dante(100,vb.travel);vb.travel[1]=vb.floor_y+vb.dive_height;set_action(0x55);
        }else if(vb.step==2){
            float y=vb.travel[1]-vb.pos[1],dy=fminf(fabsf(y),50*f);int arrived=travel(vb.travel,f);
            vb.pos[1]+=y<0?-dy:dy;
            if(arrived && fabsf(y)<=50*f){vb.step=3;hide(0);vb.yaw=wrap_angle(vb.dante_yaw+PI);set_action(0x6f);vb.vel[1]=2;}
        }else if(vb.step==3){
            vb.pos[1]-=vb.vel[1]*f;vb.vel[1]=fminf(40,vb.vel[1]+.5f*f);
            if(vb.body.window4==2){vb.body.window4=0;vb.step=4;vb.vel[1]=40;}
        }else if(vb.step==4){
            vb.pos[1]-=vb.vel[1]*f;vb.vel[1]=fminf(100,vb.vel[1]+4*f);
            if(vb.pos[1]<=vb.floor_y){vb.pos[1]=vb.floor_y;vb.step=5;set_action(0x70);}
        }else if(vb.step==5 && act==ACT_DONE){
            trace("dive_landed");
            if(--vb.repeats>0){vb.dive_height-=(1000.0f-350)/3;vb.step=1;set_action(0x54);}
            else {vb.step=6;set_action(0x71);}
        }else if(vb.step==6 && act==ACT_DONE)set_state(vb.previous_state==S_FORCE_B?S_RETREAT:S_THINK);
        break;
    case S_RETREAT: /* 4d9bb0: 66 -> 67, accelerate back, 68 recovery. */
        if(vb.step==0){vb.step=1;set_action(0x66);}
        else if(vb.step==1 && act==ACT_DONE){vb.step=2;set_action(0x67);vb.retreat_speed=35;vb.retreat_distance=0;}
        else if(vb.step==2){
            vb.retreat_speed=fminf(45,vb.retreat_speed+2*f);vb.retreat_distance+=vb.retreat_speed*f;
            vb.pos[0]-=sinf(vb.yaw)*vb.retreat_speed*f;vb.pos[2]-=cosf(vb.yaw)*vb.retreat_speed*f;
            if(dante_distance()<150 || vb.retreat_distance>=1000){vb.step=3;set_action(0x68);}
        }else if(vb.step==3 && act==ACT_DONE)set_state(S_THINK);
        break;
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
        } else if ((vb.timer -= f) < 0.0f) set_state(S_THINK);   /* its test reads the current state (0x31): always */
        break;
    case S_UPPER:                                   /* FUN_004d9680: 0x27, chains into the leap */
        if (vb.step == 0) { vb.step = 1; set_action(0x27); }
        else if (vb.body.window1 == 1) set_state(S_LEAP);
        break;
    case S_LEAP:                                    /* FUN_004dd840: 0x5f toward Dante, 0x60 on landing */
        if (vb.step == 0) {
            float d = dante_distance(), pitch = atan2f(fabsf(vb.dante[1] - vb.pos[1]), d);
            pitch = fmaxf(25.0f * PI / 180.0f, fminf(75.0f * PI / 180.0f, pitch));   /* DAT_0061a8f4/f8 */
            vb.step = 1;
            vb.yaw = angle_to_dante();
            set_action(0x5f);
            vb.vel[0] = sinf(vb.yaw) * cosf(pitch) * LEAP_SPEED;
            vb.vel[1] = -sinf(pitch) * LEAP_SPEED;
            vb.vel[2] = cosf(vb.yaw) * cosf(pitch) * LEAP_SPEED;
            /* 4dd840 rotates (0,0,45) downward; velocity stays fixed while window 1 is set. */
        } else if (vb.step == 1) {
            if (vb.body.window1 == 1) {
                vb.pos[0] += vb.vel[0] * f;
                vb.pos[1] += vb.vel[1] * f;
                vb.pos[2] += vb.vel[2] * f;
            }
            /* 3599f0 runs even outside the travel window, with a ten-unit landing tolerance. */
            if (vb.pos[1] <= vb.floor_y+10.0f) {
                vb.pos[1] = vb.floor_y;
                vb.step = 2;
                set_action(0x60);
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
    case S_BEOWULF_WARP:
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
        } else if (act == ACT_DONE) set_state(vb.state==S_BEOWULF_WARP?S_BEOWULF:S_THINK);
        break;
    case S_BEOWULF:
        if(vb.variant==2){set_state(S_JUDGMENT);vb.step=1;vb.repeats=1;vb.timer=8;set_action(0x2b);break;}
        /* 4d9ef0: 2b windup, 2c hold, 2d release. */
        if(vb.step==0){vb.step=1;vb.yaw=angle_to_dante();set_action(0x2b);vb.timer=60;}
        else if(vb.step==1 && act==ACT_DONE){vb.step=2;set_action(0x2c);}
        else if(vb.step==2 && (vb.timer-=f)<0){vb.step=3;vb.yaw=angle_to_dante();set_action(0x2d);}
        else if(vb.step==3 && act==ACT_DONE)set_state(S_THINK);
        break;
    case S_DT: /* 4e6310: first activation below 75%; 4db950: 56 -> 57, hold 240. */
        if(vb.step==0){vb.step=1;set_action(0x56);}
        else if(vb.step==1 && act==ACT_DONE){vb.step=2;vb.dt=1;vb.dt_unlocked=1;vb.dt_left=vb.variant==2?660:840;vb.timer=vb.variant==2?60:240;set_action(0x57);}
        else if(vb.step==2 && (vb.timer-=f)<=0){if(vb.variant==2)vb.dt_left=600;set_state(S_THINK);}
        break;
    case S_FLINCH:                                  /* FUN_004db5d0: reaction 0x11 or 2, 50/50 */
    case S_FLINCH_BACK:                             /* FUN_004db750: reaction 3 (a blow from behind) */
        if (vb.step == 0) {
            vb.step = 1;
            set_action(vb.state == S_FLINCH_BACK ? 3 : (unit_random() < 0.5f ? 0x11 : 2));
        } else if (act == ACT_DONE) { set_state(S_THINK); break; }
        /* FUN_004e5540: the flinch's clock runs; past 12 points of blows he braces instead. */
        vb.flinch_time += f;
        if (vb.flinch_points > vb.flinch_cap) set_state(S_BRACE);
        break;
    case S_BRACE:                                   /* FUN_004daf90: action 0x3d (vr3-40), facing Dante */
        if (vb.step == 0) { vb.step = 1; vb.brace_time = 0.0f; set_action(0x3d); break; }
        vb.yaw = angle_to_dante();                  /* FUN_00359c00 at 16384 a frame: a quarter turn, so at once */
        vb.brace_time += f;
        if (vb.brace_time > BRACE_REACT && dante_lunges()) { react(); break; }
        vb.stun_gauge = STUN_FULL;                  /* he holds both gauges full while braced */
        vb.flinch_gauge = FLINCH_FULL;
        if (act == ACT_DONE) set_state(S_THINK);
        break;
    case S_DEATH:
        break;
    }
    if (vb.state == S_THINK || vb.state == S_STRAFE || vb.state == S_IDLE || vb.state == S_TAUNT || vb.state == S_APPROACH) fill_gauge(f);
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
        if (i == 0 && root_motion) { p[0] = p[1] = p[2] = 0.0f; }   /* 4e4570 consumes the complete root delta */
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
    Transform offset;
    /* As the PS2 shows it (room 144, the 100% save): sheathed, the scabbard rises behind his shoulder. */
    euler(&offset, rx, ry, rz);
    offset.p[0] = x; offset.p[1] = y; offset.p[2] = z;
    multiply(out, &body_pose[bone], &offset);
}
/* A piece's chains (demo_cloth.c, the game's chain step) in his model space: the game rides its chains with the
 * owner's root each frame (FUN_00230150), so only the body's own motion and gravity swing them, which model space
 * gives directly. The body capsules are the ones demo.c gives a coat on Dante's 24-bone body (FUN_00230130). */
static void run_cloth(int which, const VergilMesh *mesh, Transform *pose, const Transform *base) {
    static const struct { int bone; float a[3], b[3], r; } src[] = {
        {3, {0.0f, 20.0f, 10.0f}, {0.0f, -40.0f, 10.0f}, 15.0f},
        {2, {0.0f, -5.0f, 0.0f}, {0.0f, -15.0f, 0.0f}, 18.0f},
        {15, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
        {16, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
        {19, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
        {20, {0.0f, 0.0f, 0.0f}, {0.0f, -50.0f, 0.0f}, 10.0f},
    };
    float wp[40 * 3], wr[40 * 9], local[40 * 3], bp[3], br[9];
    ClothCapsule caps[6];
    int i, k, n = mesh->bone_count < 40 ? mesh->bone_count : 40;
    for (i = 0; i < 6; ++i) {
        transform_point(&body_pose[src[i].bone], src[i].a, caps[i].a);
        transform_point(&body_pose[src[i].bone], src[i].b, caps[i].b);
        caps[i].radius = src[i].r;
    }
    for (i = 0; i < n; ++i) {
        memcpy(wp + i * 3, pose[i].p, sizeof(float) * 3);
        memcpy(wr + i * 9, pose[i].m, sizeof(float) * 9);
        local[i * 3] = mesh->local[i].x; local[i * 3 + 1] = mesh->local[i].y; local[i * 3 + 2] = mesh->local[i].z;
    }
    memcpy(bp, base->p, sizeof(bp));
    memcpy(br, base->m, sizeof(br));
    demo_cloth_apply(DEMO_CLOTH_GAMEPLAY + which, bp, br, 0, 0, wp, wr, mesh->parents, local, n,
                     which ? vergil_cord_cloth : vergil_coat_cloth,
                     which ? sizeof(vergil_cord_cloth) : sizeof(vergil_coat_cloth), floorf(vb.cloth_clock), caps, 6);
    for (i = 0; i < n; ++i) {
        memcpy(pose[i].p, wp + i * 3, sizeof(float) * 3);
        for (k = 0; k < 3; ++k) memcpy(pose[i].m[k], wr + i * 9 + k * 3, sizeof(float) * 3);
    }
}
static void evaluate_poses(void) {
    static MotionSample current, previous;
    Transform at;
    if (vb.body.motion >= 0) motion_sample(motion_get(vb.body.motion), vb.body.frame, &current);
    else memset(&current, 0, sizeof(current));
    if (vb.body.fade > 0.0f && vb.body.previous_motion >= 0)
        motion_sample(motion_get(vb.body.previous_motion), vb.body.previous_frame, &previous);
    build_skeleton(&vergil_parts[vb.variant==2 && vb.dt?PART_M20_DT_BODY:PART_BODY], &current, vb.body.fade > 0.0f ? &previous : 0, vb.body.fade, 0, body_pose, 1);
    /* The coat hangs from body bone 3 (its cloth is bound to the bone list's entry 3, as Dante's is) and swings
     * as pl001_02.clt's chains. */
    memset(&current, 0, sizeof(current));
    build_skeleton(&vergil_parts[PART_COAT], &current, 0, 0.0f, &body_pose[3], coat_pose, 0);
    if (!vb.dt) run_cloth(0,&vergil_parts[PART_COAT], coat_pose, &body_pose[3]);
    {int b;for(b=0;b<11;++b)beowulf_pose[b]=body_pose[vergil_beowulf_bodymap[b]];}
    if(vb.variant==2 && vb.dt){
        /* Native DT attachments; cloth uses authored rest poses until its drivers are ported. */
        build_skeleton(&vergil_parts[PART_M20_DT_WINGS], &current,0,0,0,m20_wing_pose,0);
        build_skeleton(&vergil_parts[PART_M20_DT_DETAIL], &current,0,0,0,m20_detail_pose,0);
        build_skeleton(&vergil_parts[PART_M20_DT_CLOTH], &current,0,0,0,m20_cloth_pose,0);
    }
    /* Scabbard in the left hand (0x5dc010: bone 13); Yamato in it (0x5dbfe0) or drawn in the right (0x5dc0a0). */
    attach(13, 19.0f, -0.5f, 11.0f, 0.0f, 3.84f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_SCABBARD], &current, 0, 0.0f, &at, scabbard_pose, 0);
    /* Its cord (bones 3-6 off the knot at bone 2) swings as wp005.clt's chain. */
    run_cloth(1, &vergil_parts[PART_SCABBARD], scabbard_pose, &at);
    if (vb.yamato_drawn) attach(9, -7.5f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_YAMATO], &current, 0, 0.0f, &at, yamato_pose, 0);
    if(vb.variant==2){
        /* 5dc160: carried on bone 14; 5dc130: drawn in the left hand (13). */
        if(vb.action>=0x61 && vb.action<=0x71)attach(13,9,0,-1,0,.52359879f,0,&at);
        else attach(14,15,10,20,-3.05432606f,-.87266463f,-1.57079625f,&at);
        if(vb.sword){float x=vb.sword_pos[0]-vb.pos[0],z=vb.sword_pos[2]-vb.pos[2],c=cosf(vb.yaw),sn=sinf(vb.yaw);
            euler(&at,0,0,vb.sword_spin);at.p[0]=c*x-sn*z;at.p[1]=vb.sword_pos[1]-vb.pos[1];at.p[2]=sn*x+c*z;
        }
        build_skeleton(&vergil_parts[PART_FORCE_EDGE],&current,0,0,&at,force_edge_pose,0);
    }
}
static const Transform *part_transforms(int part) {
    switch (part) {
    case PART_FORCE_EDGE:return force_edge_pose;
    case PART_M20_DT_WINGS:return m20_wing_pose;
    case PART_M20_DT_DETAIL:return m20_detail_pose;
    case PART_M20_DT_CLOTH:return m20_cloth_pose;
    case PART_BEOWULF:return beowulf_pose;
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
static int part_visible(int i){
    if(i==PART_BEOWULF)return vb.variant==1;
    if(i==PART_FORCE_EDGE)return vb.variant==2;
    if(i==PART_DT_BODY)return vb.variant==1 && vb.dt;
    if(i>=PART_M20_DT_BODY)return vb.variant==2 && vb.dt;
    if(i==PART_BODY || i==PART_COAT)return !vb.dt;
    return 1;
}
/* Mission 19's partner Vergil (CPlVergil, ARKHAM_SPEC 1): his body (em035 [1], Dante's 24-bone layout) posed by the
 * bone transforms of a player Pose (main.c's partner, playing Dante's moves a frame late) and drawn where he stands.
 * His coat swings as the boss's does; Yamato sits in its scabbard on bone 13, in his right hand (bone 9, the boss's
 * drawn record 0x5dc0a0) while he attacks. His moves are still Dante's [stand-in: CPlVergil's moveset isn't ported]. */
static int load_data(void);   /* the model's buffers, made at the first spawn (below) */
static int partner_drawn, partner_fresh = 1;
void vergil_boss_partner_drawn(int drawn) { partner_drawn = drawn; }
void vergil_boss_partner_reset(void) { partner_fresh = 1; }
void vergil_boss_draw_partner(const float rot[][3][3], const float pos[][3], float x, float y, float z, float yaw) {
    static const int parts[] = {PART_BODY, PART_COAT, PART_SCABBARD, PART_YAMATO};
    MotionSample rest;
    Transform at;
    int b, k, j;
    if (vb.active) return;   /* the boss's own buffers are in use */
    if (!load_data()) return;
    if (partner_fresh) {
        partner_fresh = 0;
        vb.cloth_clock = 0.0f;
        demo_cloth_reset_slot(DEMO_CLOTH_GAMEPLAY);
        demo_cloth_reset_slot(DEMO_CLOTH_GAMEPLAY + 1);
    }
    vb.cloth_clock += 1.0f;   /* a draw a 60 Hz frame */
    for (b = 0; b < 24; ++b) {
        for (k = 0; k < 3; ++k) { for (j = 0; j < 3; ++j) body_pose[b].m[k][j] = rot[b][k][j]; body_pose[b].p[k] = pos[b][k]; }
    }
    memset(&rest, 0, sizeof(rest));
    build_skeleton(&vergil_parts[PART_COAT], &rest, 0, 0.0f, &body_pose[3], coat_pose, 0);
    run_cloth(0, &vergil_parts[PART_COAT], coat_pose, &body_pose[3]);
    attach(13, 19.0f, -0.5f, 11.0f, 0.0f, 3.84f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_SCABBARD], &rest, 0, 0.0f, &at, scabbard_pose, 0);
    run_cloth(1, &vergil_parts[PART_SCABBARD], scabbard_pose, &at);
    if (partner_drawn) attach(9, -7.5f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, &at);
    build_skeleton(&vergil_parts[PART_YAMATO], &rest, 0, 0.0f, &at, yamato_pose, 0);
    for (b = 0; b < 4; ++b) skin_part(parts[b]);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        ScePspFVector3 p = {x, y, z};
        sceGumTranslate(&p);
    }
    sceGumRotateY(yaw);
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
    for (b = 0; b < 4; ++b) draw_part(parts[b]);
    sceGuDisable(GU_ALPHA_TEST);
}

void vergil_boss_draw(void) {
    int i;
    if (!vb.active || vb.hidden) return;
    if (vb.posed_tick != vb.tick) evaluate_poses();   /* the update's pose, chains and all, when it's this tick's */
    for (i = 0; i < VERGIL_PART_COUNT; ++i) if(part_visible(i))skin_part(i);
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
    for (i = 0; i < VERGIL_PART_COUNT; ++i) {
        if(!part_visible(i))continue;
        draw_part(i);
    }
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
            const VergilAttack *a = &vergil_attacks[vb.body.cap_record[i] < VERGIL_ATTACK_COUNT ? vb.body.cap_record[i] : 0];
            vb.landed = 1;
            vb.landed_x = vb.pos[0];
            vb.landed_z = vb.pos[2];
            vb.landed_damage = a->damage;
            vb.landed_heavy = a->kind != 0 || a->reaction >= 500.0f;
    vb.landed_kind = a->kind + 1;   /* 0: this landing had no record */
            trace("landed");
        }
    }
}
int vergil_boss_attack_kind(void) { int k = vb.landed_kind - 1; vb.landed_kind = 0; return k; }   /* -1: no record */
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
    return vb.state == S_THINK || vb.state == S_STRAFE || vb.state == S_IDLE || vb.state == S_TAUNT || vb.state == S_APPROACH || vb.state == S_FLINCH || vb.state == S_FLINCH_BACK;
}
/* FUN_004e51e0 (with FUN_004e52f0) on a reaction code: 0 none, 1 the brace (state 0x32), 2 a flinch. */
static int reaction_gate(int melee) {
    if (!melee) return 0;                           /* +0xbb52: only a blow with byte 2 = 0, not a shot */
    /* +0xb9a0: 0x27 (the upper slash), 0x5f (the dive), 0x6f (the storm's dive) take blows without reacting. */
    if (vb.action == 0x27 || vb.action == 0x5f || vb.action == 0x6f) return 0;
    if (vb.dt) return 0;                            /* Devil Trigger's form doesn't flinch */
    if (vb.state == 0x44 || vb.state == S_BRACE) return 0;
    if (vb.flinch_time > vb.flinch_limit) return 1; /* the flinch has gone on too long */
    if (vb.state == S_FLINCH || vb.state == S_FLINCH_BACK) return 2;
    /* The body script's window 1 byte 4 = 1 marks a stretch he can be flinched in (the finishers' tails, 0x47,
     * the recovery of the dive): it opens a new budget, 600 frames and 12 points. */
    if (vb.body.window4 == 1) {
        vb.flinch_time = 0.0f; vb.flinch_limit = FLINCH_LIMIT;
        vb.flinch_points = 0.0f; vb.flinch_cap = FLINCH_CAP;
        return 2;
    }
    return 1;                                       /* anywhere else (standing, strafing, mid-attack): the brace */
}
/* FUN_004dee00 on the codes of FUN_0032a540 / FUN_0032a800: 1 and 3 (from behind) play reaction 3 (unless he's
 * already in it), the rest 0x11 or 2. */
static void reaction(int behind, int melee) {
    int gate = reaction_gate(melee);
    trace(gate == 2 ? "react_flinch" : gate == 1 ? "react_brace" : "react_none");
    if (gate == 1) set_state(S_BRACE);
    else if (gate == 2) set_state(behind && vb.state != S_FLINCH_BACK ? S_FLINCH_BACK : S_FLINCH);
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
    /* FUN_004e6d20 by the action's hit mode (+0xb9a3, set in FUN_004e0fc0): 2 for 0x56 / 0x57 / 0x61 / 0x62 (the
     * Devil Trigger change) turns the blow aside (effect 0x32d), 1 for 0x75 takes nothing. */
    if (vb.action == 0x56 || vb.action == 0x57 || vb.action == 0x61 || vb.action == 0x62 || vb.action == 0x75) {
        trace("deflected");
        return 0;
    }
    /* As Cerberus's: a light hit 42, a heavy one 84, x1.1; Easy x2. Guns: the param block's 20% [inferred]. */
    damage = kind == HIT_PISTOL ? 10.0f * 0.2f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    if(vb.variant>=1 && vb.dt)damage*=.5f; /* DT damage resistance: port approximation. */
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
    if(vb.variant==2 && vb.hp<max_health()*.39f)vb.m20_low=1; /* 4e6d20 / 5dfae8[2], latched. */
    if(vb.variant==1 && vb.hp<max_health()*.75f && !vb.dt_unlocked && vb.state!=S_DT){
        vb.m13_chain=0;set_state(S_DT);return 0;
    }
    /* M13's simplified reaction escape prevents indefinite heavy-shot flinches.
     * Three interrupting contacts trigger the Beowulf warp; native count not decoded. */
    if(vb.variant>=1 && kind!=HIT_PISTOL && interruptible() && ++vb.m13_chain>=3){
        vb.m13_chain=0;set_state(vb.variant==2?S_BEHIND:S_BEOWULF_WARP);return 0;
    }
    {
        /* FUN_002f17a0: each blow takes its record's +0xc from the stun gauge and +0x14 from the flinch gauge (by the
         * weapon's share: guns 20). Dante's records aren't decoded, so a light blow takes 20 / 30 and a heavy one
         * 60 / 60 [estimates]. The stun gauge emptying (FUN_0032a540) comes first and refills both; the flinch gauge
         * (FUN_0032a800) refills itself. Either sends a reaction code by where the blow came from; a blow that
         * empties neither does nothing to him (his +0x30, FUN_004e6b20, only reacts in the air). */
        int heavy = kind == HIT_HEAVY || kind == HIT_LAUNCH || kind == HIT_SLAM;
        float share = kind == HIT_PISTOL ? 0.2f : 1.0f, stun = (heavy ? 60.0f : 20.0f) * share;
        int behind = fabsf(wrap_angle(toward - vb.yaw)) >= PI / 2.0f;
        int code = 0;
        /* FUN_004e6d20: while he flinches, a blow adds 1 to the budget's points, 2 if its +0xc is 60 or more. */
        if (vb.state == S_FLINCH || vb.state == S_FLINCH_BACK) vb.flinch_points += stun >= 60.0f ? 2.0f : 1.0f;
        vb.since_blow = 0.0f;
        vb.stun_gauge -= stun;
        vb.flinch_gauge -= (heavy ? 60.0f : 30.0f) * share;
        if (vb.stun_gauge <= 0.0f) { vb.stun_gauge = STUN_FULL; vb.flinch_gauge = FLINCH_FULL; code = 1; }
        else if (vb.flinch_gauge <= 0.0f) { vb.flinch_gauge = FLINCH_FULL; code = 1; }
        vergil_boss_note("gauges", vb.stun_gauge, vb.flinch_gauge, (float)code, (float)behind);
        if (code) reaction(behind, kind != HIT_PISTOL);
    }
    return 0;
}

/* ---- Frame ------------------------------------------------------------------------------------------------ */
void vergil_boss_update(float frames, float px, float py, float pz, int attacks) {
    float dx, dy, dz, c, s;
    if (!vb.active) return;
    ++vb.tick;
    if(vb.variant>=1 && vb.dt && !vb.m20_temp_dt && !vb.dying){
        vb.dt_left-=frames;
        if(vb.dt_left<=0 && vb.variant==1){vb.dt=0;vb.dt_cooldown=1200;trace("dt_end");}
        else vb.hp=fminf(max_health(),vb.hp+frames*.5f); /* healing rate / duration approximated */
    }
    if(vb.variant==2 && !vb.dt && vb.dt_cooldown>0)vb.dt_cooldown=fmaxf(0,vb.dt_cooldown-frames);
    if(vb.variant==1 && vb.dt_unlocked && !vb.dt && vb.hp>0 && vb.hp<max_health()*.75f && (vb.dt_cooldown-=frames)<=0 && vb.state==S_THINK){vb.dt_unlocked=0;}

    vb.dante[0] = px; vb.dante[1] = py; vb.dante[2] = pz;
    /* FUN_002f1750: 180 frames without a blow refill both gauges. */
    if ((vb.since_blow += frames) > GAUGE_REFILL) { vb.stun_gauge = STUN_FULL; vb.flinch_gauge = FLINCH_FULL; }
    if (attacks && !vb.dying){ai_step(frames);if(vb.variant==2)m20_roundtrip(frames);}
    vb.action_time += frames;
    script_tick(&vb.body, frames*(vb.variant==2 && vb.dt?1.2f:1.0f), &dx, &dy, &dz);
    /* FUN_004e4570: the root's motion, turned by his facing, moves him. */
    c = cosf(vb.yaw); s = sinf(vb.yaw);
    vb.pos[0] += c * dx + s * dz;
    vb.pos[2] += -s * dx + c * dz;
    vb.pos[1] += dy;
    action_effects_follow();
    vb.cloth_clock += frames;
    /* 4e27a0: fixed downward step, not accelerated gravity. +b9a0 suppresses it
     * for uppercut/Helm Breaker (27/5f) and airborne M20 entry (6f); 55 is hidden travel. */
    if (vb.action!=0x27 && vb.action!=0x5f && vb.action!=0x6f && vb.action!=0x55)
        vb.pos[1]-=10.0f*frames;
    /* M13's room floor is bounded about (2500,2500); keep root-driven charges
     * and escape teleports on its combat deck. Conservative port boundary. */
    if(vb.variant>=1){float x=vb.pos[0]-vb.centre[0],z=vb.pos[2]-vb.centre[2],r=sqrtf(x*x+z*z);
        if(r>(vb.variant==2?1650:1100)){float limit=vb.variant==2?1650:1100;vb.pos[0]=vb.centre[0]+x/r*limit;vb.pos[2]=vb.centre[2]+z/r*limit;}}
    if (vb.body.window0 == 1) vb.yamato_drawn = 1;
    else if (vb.body.window0 == 2) vb.yamato_drawn = 0;
    vb.body.window0 = 0;
    if (floor_fn) {
        float y;
        if (floor_fn(vb.pos[0], vb.pos[1] + 100.0f, vb.pos[2], &y)) vb.floor_y = y;
    }
    if (vb.pos[1]<vb.floor_y) vb.pos[1]=vb.floor_y;
    if (vb.state==S_UPPER || vb.state==S_LEAP) trace("air");
    if (!vb.hidden && !vb.dying) {
        evaluate_poses();
        vb.posed_tick = vb.tick;
        test_attack_caps(px, py, pz);
    }
}
void vergil_boss_spawn(float x, float y, float z) {
    if (!load_data()) return;   /* its model's buffers, first time (load_data) */
    memset(&vb, 0, sizeof(vb));
    slash_fx[0] = slash_fx[1] = 0;
    vb.posed_tick = -1;
    demo_cloth_reset_slot(DEMO_CLOTH_GAMEPLAY);
    demo_cloth_reset_slot(DEMO_CLOTH_GAMEPLAY + 1);
    vb.active = 1;
    vb.variant=boss_variant;
    vb.pos[0] = x; vb.pos[1] = vb.floor_y = y; vb.pos[2] = z;
    /* 0x5df970: Mission 7's arena centre, where 0x3e goes when there's no room ahead of Dante. */
    if (fabsf(x - 2700.0f) < 1.0f && fabsf(z - 3500.0f) < 1.0f) { vb.centre[0] = 3500.0f; vb.centre[2] = 3500.0f; }
    else { vb.centre[0] = x; vb.centre[2] = z; }
    if(vb.variant==1){vb.centre[0]=2500;vb.centre[2]=2500;}
    if(vb.variant==2){vb.centre[0]=2500;vb.centre[2]=3750;}
    vb.centre[1] = y;
    vb.yaw = PI;
    vb.hp = max_health();
    vb.stun_gauge = STUN_FULL; vb.flinch_gauge = FLINCH_FULL;   /* the budget starts at 0 / 0 (FUN_004d27d0) */
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
float vergil_boss_max_health(void) { return max_health(); }
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
/* Its model's buffers (skinned vertices, indices, swizzled textures) are made at its first spawn, not at boot, and
 * freed by vergil_boss_data_release when a room is left without it (docs/MEMORY.md): bosses held about 2.7 MB in every mission. */
static int data_ready;
static void free_data(void) {
    int i;
    for (i = 0; i < VERGIL_PART_COUNT; ++i) { free(vertices[i]); free(indices[i]); vertices[i] = 0; indices[i] = 0; }
    for (i = 0; i < VERGIL_TEXTURE_COUNT; ++i) { free(textures[i]); textures[i] = 0; }
    data_ready = 0;
}
void vergil_boss_data_release(void) { if (!vb.active) free_data(); }
static int load_data(void) {
    int i, j;
    if (data_ready) return 1;
    for (i = 0; i < VERGIL_PART_COUNT; ++i) {
        const VergilMesh *m = &vergil_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices);
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) { free_data(); return 0; }
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
        if (!textures[i]) { free_data(); return 0; }
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) { free_data(); return 0; }
    }
    sceKernelDcacheWritebackAll();
    data_ready = 1;
    return 1;
}
int vergil_boss_init(void) {
    int file, i;
    for (file = 0; file < 4; ++file)
        for (i = 0; i < 100; ++i) {
            char name[8];
            snprintf(name, sizeof(name), "vr%d-%02d", file, i);
            motion_ids[file][i] = motion_find(name);
        }
    if (motion_ids[0][0] < 0 || motion_ids[3][3] < 0) return 0;
    return 1;
}
int vergil_boss_data_load(void) { return load_data(); }   /* the room load makes it ahead of the spawn (enemy_preload_class) */

int vergil_boss_test_state(int state) { if (!vb.active || state<0 || state>0x40) return 0; set_state(state); return 1; }
