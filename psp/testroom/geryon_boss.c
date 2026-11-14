#include "geryon_boss.h"

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
#include "boss_geryon_runtime.h"

/* CEm029 at AI rank 0. Frames and distances keep the original 60 Hz / centimetre units; angles in the PS2's
 * 65536-to-a-turn units are converted where they're quoted. Addresses are SLUS_213.61's; see
 * docs/GERYON_SPEC.md. */
#define PI 3.14159265358979323846f
#define UNITS (2.0f * PI / 65536.0f)
#define MAX_HEALTH 9000.0f            /* DAT_005d6c1c */
#define LEAVE_HEALTH 7200.0f          /* phase 0 leaves the bridge at 80% (carried in DAT_006193ac) */
#define DANTE_RADIUS 40.0f
#define DANTE_LOW 20.0f
#define DANTE_HIGH 160.0f
#define RUN_SPEED 25.0f               /* 4c0fd0 and the gallops: 25 a frame */
#define CHARGE_TIME 65.0f             /* 4c0fd0: 65 frames (30 with flag 0x800, 10 with 0x100) */
#define GALLOP_FAR 1800.0f            /* the gallop's ring (00385530's 0x10 / 0x11) */
#define GALLOP_NEAR 1700.0f
#define GALLOP_WIDE 1900.0f
#define GALLOP_LAPS 12                /* [approximation] +0x973 starts at 40 and counts script windows down */
#define SPHERE_RANGE 800.0f           /* 4c0c50: beyond this from the centre and facing it, the spheres */
#define HIDE_TIME 120.0f              /* 4c1560: hidden this long after the spheres */
#define VTYPE (GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

extern unsigned int enemy_ai_random(void);
extern int testroom_frame;

enum { PART_HORSE, PART_CART, PART_MISSILE, PART_SPHERE, PART_SMALL };
/* Actions (+5), both bodies play motion n for action n [inferred]. */
enum {
    A_IDLE = 0, A_WALK = 1, A_THINK = 3, A_CHARGE = 4, A_TURN_L = 5, A_TURN_R = 6, A_TURN_BACK = 7,
    A_SPHERES = 8, A_HIDE = 9, A_MISSILES = 0x0b, A_SHOCK = 0x0e, A_GALLOP_L = 0x10, A_GALLOP_R = 0x11,
    A_CAST = 0x12, A_DEATH = 0x13
};
/* What the port's states are doing (the PS2 keeps this in the action and its sub-step +6). */
enum { S_IDLE, S_CHARGE, S_TURN_BACK, S_SPHERES, S_HIDDEN, S_MISSILES, S_SHOCK, S_GALLOP, S_TURN, S_LEAVE, S_SWITCH, S_DEATH };

typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u, v, nx, ny, nz, x, y, z; } Vertex;
typedef struct { int motion, previous; float frame, previous_frame, fade; int loop, loops, done; } Player;
typedef struct { int on, kind; float pos[3], vel[3], life, rise; } Shell;   /* kind: PART_MISSILE / PART_SPHERE */

#define SHELLS 12
static struct {
    int active, dying, hidden, phase, state, action, think_count, laps, side, warp_ready, rank;
    float pos[3], yaw, hp, timer, near_time, floor_y, axis_dir;
    float dante[3], centre[3];
    Player horse, cart;
    Shell shells[SHELLS];
    float contact_wait, shock_at;
    int landed, landed_heavy;
    float landed_x, landed_z, landed_damage;
    int tick;
} gb;

static int motion_ids[2][21];
static Vertex *vertices[GERYON_PART_COUNT];
static unsigned short *indices[GERYON_PART_COUNT];
static void *textures[GERYON_TEXTURE_COUNT];
static Transform horse_pose[40], cart_pose[24], identity_pose[2];
static char trace_path[320];
static int trace_on, boss_difficulty = 1;
static int (*floor_fn)(float x, float y, float z, float *out);

/* 228's bridge (phase 0, 003853c0): he starts at its middle and runs along it. */
static const float bridge_a[3] = {400.0f, 7016.0f, 400.0f}, bridge_b[3] = {5600.0f, 7016.0f, 5600.0f};
static const float bridge_start[3] = {2950.0f, 7016.0f, 2950.0f};
/* Phase 1: placed here; the gallop rings the centre 0x5b60c0. */
static const float ground_start[3] = {2500.0f, 0.0f, 2500.0f};
/* 228's start 2, where the event script puts Dante after the bridge falls (demo 33). */
static const float dante_below[4] = {1437.8f, -0.1f, 2004.2f, 60.0f};

void geryon_boss_set_difficulty(int difficulty) { boss_difficulty = difficulty; }
void geryon_boss_set_floor(int (*fn)(float x, float y, float z, float *out)) { floor_fn = fn; }

static float wrap_angle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static float flat_distance(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dz = a[2] - b[2];
    return sqrtf(dx * dx + dz * dz);
}
static float angle_to(const float at[3]) { return atan2f(at[0] - gb.pos[0], at[2] - gb.pos[2]); }
static void turn_toward(float yaw, float rate) {
    float d = wrap_angle(yaw - gb.yaw);
    gb.yaw = wrap_angle(gb.yaw + fmaxf(-rate, fminf(rate, d)));
}

static void trace(const char *event) {
    FILE *f;
    if (!trace_on) return;
    f = fopen(trace_path, "a");
    if (!f) return;
    fprintf(f, "t=%d f=%d %s phase=%d state=%d action=%#x frame=%.1f hp=%.0f d=%.0f pos=%.0f,%.0f,%.0f dante=%.0f,%.0f,%.0f\n",
            gb.tick, testroom_frame, event, gb.phase, gb.state, gb.action, gb.horse.frame, gb.hp,
            flat_distance(gb.pos, gb.dante), gb.pos[0], gb.pos[1], gb.pos[2], gb.dante[0], gb.dante[1], gb.dante[2]);
    fclose(f);
}
void geryon_boss_trace(const char *path) {
    trace_on = path && *path;
    if (trace_on) snprintf(trace_path, sizeof(trace_path), "%s", path);
}

/* ---- Transforms --------------------------------------------------------------------------------------- */
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
/* Model space (x right, z forward) to the world. */
static void local_to_world(float x, float y, float z, float out[3]) {
    float c = cosf(gb.yaw), s = sinf(gb.yaw);
    out[0] = gb.pos[0] + c * x + s * z;
    out[1] = gb.pos[1] + y;
    out[2] = gb.pos[2] - s * x + c * z;
}

/* ---- Motions: action n plays gh n on the horse and gc n on the carriage -------------------------------- */
static void play(Player *p, int motion, int loop) {
    if (p->motion == motion && p->loop == loop && !p->done) return;
    p->previous = p->motion;
    p->previous_frame = p->frame;
    p->fade = p->motion >= 0 ? 1.0f : 0.0f;
    p->motion = motion;
    p->frame = 0.0f;
    p->loop = loop;
    p->loops = 0;
    p->done = motion < 0;
}
static void tick(Player *p, float frames) {
    const Motion *m = motion_get(p->motion);
    p->fade = fmaxf(0.0f, p->fade - frames / 8.0f);
    if (!m || p->done) return;
    p->frame += frames;
    if (p->frame >= m->end_frame) {
        if (p->loop) { p->frame = motion_wrap(m, p->frame); ++p->loops; }
        else { p->frame = m->end_frame; p->done = 1; }
    }
}
static void set_action(int action, int loop) {
    gb.action = action;
    play(&gb.horse, action < 21 ? motion_ids[0][action] : -1, loop);
    play(&gb.cart, action < 21 ? motion_ids[1][action] : -1, loop);
}
static void set_state(int state) {
    gb.state = state;
    gb.timer = 0.0f;
    trace("state");
}

/* ---- Shells ---------------------------------------------------------------------------------------------- */
static void shell_add(int kind, const float at[3], const float vel[3], float life) {
    int i;
    for (i = 0; i < SHELLS; ++i) if (!gb.shells[i].on) {
        Shell *s = &gb.shells[i];
        s->on = 1; s->kind = kind; s->life = life; s->rise = kind == PART_MISSILE ? 30.0f : 0.0f;
        memcpy(s->pos, at, sizeof(s->pos));
        memcpy(s->vel, vel, sizeof(s->vel));
        return;
    }
}
static void land(float x, float z, float damage, int heavy) {
    if (gb.landed) return;
    gb.landed = 1;
    gb.landed_x = x; gb.landed_z = z;
    gb.landed_damage = damage;
    gb.landed_heavy = heavy;
    trace("landed");
}
static void shells_update(float frames) {
    int i;
    for (i = 0; i < SHELLS; ++i) {
        Shell *s = &gb.shells[i];
        float to[3], d, k, speed;
        if (!s->on) continue;
        s->life -= frames;
        if (s->life <= 0.0f) { s->on = 0; continue; }
        to[0] = gb.dante[0] - s->pos[0]; to[1] = gb.dante[1] + 100.0f - s->pos[1]; to[2] = gb.dante[2] - s->pos[2];
        d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (s->kind == PART_MISSILE) {
            /* The cart's missiles (shell 0x215): up out of the roof, then after Dante [approximation: the
             * shell's own steering isn't decoded]. */
            if (s->rise > 0.0f) s->rise -= frames;
            else if (d > 1.0f) {
                speed = 18.0f;
                k = fminf(1.0f, 0.06f * frames);
                for (int j = 0; j < 3; ++j) s->vel[j] += (to[j] / d * speed - s->vel[j]) * k;
            }
        } else if (d > 1.0f) {
            /* The red spheres (4cbdb0) drift at him. */
            for (int j = 0; j < 3; ++j) s->vel[j] = to[j] / d * 7.0f;
        }
        for (int j = 0; j < 3; ++j) s->pos[j] += s->vel[j] * frames;
        if (d < (s->kind == PART_MISSILE ? 60.0f : 90.0f) + DANTE_RADIUS) {
            fx_hit_spark(s->pos);
            land(s->pos[0], s->pos[2], s->kind == PART_MISSILE ? 300.0f : 150.0f, s->kind == PART_MISSILE);
            s->on = 0;
        }
    }
}
static void fire_missiles(int count) {
    int i;
    for (i = 0; i < count; ++i) {
        float at[3], vel[3], side = (float)(i - (count - 1) * 0.5f);
        local_to_world(side * 120.0f, 450.0f, -500.0f, at);
        vel[0] = sinf(gb.yaw + PI / 2.0f) * side * 3.0f; vel[1] = 14.0f; vel[2] = cosf(gb.yaw + PI / 2.0f) * side * 3.0f;
        shell_add(PART_MISSILE, at, vel, 300.0f);
    }
}
static void cast_spheres(int count) {
    int i;
    for (i = 0; i < count; ++i) {
        float a = gb.yaw + (float)(i - (count - 1) * 0.5f) * 0.6f, at[3], zero[3] = {0.0f, 0.0f, 0.0f};
        at[0] = gb.pos[0] + sinf(a) * 450.0f; at[1] = gb.pos[1] + 250.0f; at[2] = gb.pos[2] + cosf(a) * 450.0f;
        shell_add(PART_SPHERE, at, zero, 360.0f);
    }
}

/* ---- The AI ---------------------------------------------------------------------------------------------- */
static float bridge_t(const float at[3]) {
    float ax = bridge_b[0] - bridge_a[0], az = bridge_b[2] - bridge_a[2];
    return ((at[0] - bridge_a[0]) * ax + (at[2] - bridge_a[2]) * az) / (ax * ax + az * az);
}
static void bridge_clamp(void) {
    float t = fmaxf(0.0f, fminf(1.0f, bridge_t(gb.pos)));
    gb.pos[0] = bridge_a[0] + (bridge_b[0] - bridge_a[0]) * t;
    gb.pos[2] = bridge_a[2] + (bridge_b[2] - bridge_a[2]) * t;
}
static void go_think(void) {
    set_action(A_IDLE, 1);
    set_state(S_IDLE);
}
static void start_charge(void) {
    if (gb.phase == 0) {
        /* Along the bridge toward Dante's side of it. */
        float axis = atan2f(bridge_b[0] - bridge_a[0], bridge_b[2] - bridge_a[2]);
        gb.axis_dir = bridge_t(gb.dante) >= bridge_t(gb.pos) ? 1.0f : -1.0f;
        gb.yaw = gb.axis_dir > 0.0f ? axis : wrap_angle(axis + PI);
    }
    set_action(A_CHARGE, 1);
    set_state(S_CHARGE);
}
/* Phase 0's think (4bfb20): at rank 0 every third pick (+0x975 % 3 == 0) is the charge, else 0x12, the cast
 * that leads to 8. A Dante who stays close gets the shockwave (0xe, effect 0x322) [inferred trigger]. */
static void think_bridge(void) {
    if (gb.near_time > 90.0f) { gb.near_time = 0.0f; set_action(A_SHOCK, 0); set_state(S_SHOCK); return; }
    if ((gb.think_count++ % 3) == 0) start_charge();
    else { set_action(A_CAST, 0); set_state(S_SPHERES); }
}
/* Phase 1's think (4c0c50): far from the centre and facing it, the spheres; else the gallop round it. */
static void think_ground(void) {
    float d = flat_distance(gb.pos, gb.centre);
    if (gb.near_time > 90.0f) { gb.near_time = 0.0f; set_action(A_SHOCK, 0); set_state(S_SHOCK); return; }
    if (d > SPHERE_RANGE && fabsf(wrap_angle(angle_to(gb.centre) - gb.yaw)) < PI / 2.0f) {
        set_action(A_SPHERES, 0);
        set_state(S_SPHERES);
        return;
    }
    gb.side = !gb.side;   /* +0x97a: the gallops alternate */
    gb.laps = GALLOP_LAPS;
    set_action(gb.side ? A_GALLOP_R : A_GALLOP_L, 1);
    set_state(S_GALLOP);
}
static void move_forward(float speed, float frames) {
    gb.pos[0] += sinf(gb.yaw) * speed * frames;
    gb.pos[2] += cosf(gb.yaw) * speed * frames;
}
static void contact(float frames) {
    /* Running into Dante: the horse and its carriage hit him [approximation: the attack records aren't
     * decoded; 600, heavy]. */
    float c = cosf(gb.yaw), s = sinf(gb.yaw), dx, dz, along, across;
    gb.contact_wait = fmaxf(0.0f, gb.contact_wait - frames);
    if (gb.contact_wait > 0.0f || gb.dante[1] > gb.pos[1] + 300.0f || gb.dante[1] + 150.0f < gb.pos[1]) return;
    dx = gb.dante[0] - gb.pos[0]; dz = gb.dante[2] - gb.pos[2];
    along = s * dx + c * dz;
    across = c * dx - s * dz;
    if (along > -950.0f && along < 380.0f && fabsf(across) < (along > -200.0f ? 110.0f : 330.0f) + DANTE_RADIUS) {
        land(gb.pos[0], gb.pos[2], 600.0f, 1);
        gb.contact_wait = 60.0f;
    }
}
static void ai_step(float f) {
    float d = flat_distance(gb.pos, gb.dante);
    gb.timer += f;
    if (d < 500.0f && fabsf(gb.dante[1] - gb.pos[1]) < 300.0f) gb.near_time += f;
    else gb.near_time = fmaxf(0.0f, gb.near_time - f);
    switch (gb.state) {
    case S_IDLE:
        if (gb.timer < (gb.phase ? 40.0f : 60.0f)) { turn_toward(angle_to(gb.dante), 182.0f * UNITS * f); break; }
        if (gb.phase == 0) think_bridge(); else think_ground();
        break;
    case S_CHARGE:
        move_forward(RUN_SPEED, f);
        contact(f);
        if (gb.phase == 0) {
            /* 4bfc40: on to 1000 past Dante, or the bridge's end; then he turns round (7). */
            float t = bridge_t(gb.pos), past = (t - bridge_t(gb.dante)) * gb.axis_dir * 7354.0f;
            bridge_clamp();
            if (past > 1000.0f || t <= 0.0f || t >= 1.0f) { set_action(A_TURN_BACK, 0); set_state(S_TURN_BACK); }
        } else {
            if (gb.timer < 20.0f) turn_toward(angle_to(gb.dante), 182.0f * UNITS * f);
            if (gb.timer >= CHARGE_TIME) go_think();
        }
        break;
    case S_TURN_BACK:
        if (gb.horse.done) { gb.yaw = wrap_angle(gb.yaw + PI); go_think(); }
        break;
    case S_SPHERES:
        /* 0x12 / 8: the red spheres (4cbdb0), 3 at rank 0 (5 above). Phase 1 then hides 120 frames and
         * reappears (9). */
        if (gb.timer >= 20.0f && gb.timer - f < 20.0f) cast_spheres(gb.rank ? 5 : 3);
        if (gb.horse.done) {
            if (gb.phase == 1) { gb.hidden = 1; set_action(A_HIDE, 1); set_state(S_HIDDEN); }
            else start_charge();   /* 9: then the charge */
        }
        break;
    case S_HIDDEN:
        if (gb.timer >= HIDE_TIME) {
            /* Back on the ring, behind Dante, facing the centre; then the missiles (10 -> 0xb). */
            float a = atan2f(gb.dante[0] - gb.centre[0], gb.dante[2] - gb.centre[2]) + PI * 0.5f;
            gb.pos[0] = gb.centre[0] + sinf(a) * GALLOP_NEAR;
            gb.pos[2] = gb.centre[2] + cosf(a) * GALLOP_NEAR;
            gb.yaw = angle_to(gb.dante);
            gb.hidden = 0;
            set_action(A_MISSILES, 0);
            set_state(S_MISSILES);
        }
        break;
    case S_MISSILES:
        if (gb.timer >= 60.0f && gb.timer - f < 60.0f) fire_missiles(4);
        if (gb.timer >= 150.0f && gb.timer - f < 150.0f) fire_missiles(4);
        if (gb.horse.done || gb.timer > 360.0f) go_think();
        break;
    case S_SHOCK:
        /* 0xe: effect 0x322 at 600 around him [the frame is inferred: 30]. */
        if (gb.timer >= 30.0f && gb.timer - f < 30.0f) {
            float at[3] = {gb.pos[0], gb.pos[1] + 50.0f, gb.pos[2]};
            fx_hit_spark(at);
            if (d < 600.0f + DANTE_RADIUS && fabsf(gb.dante[1] - gb.pos[1]) < 250.0f) land(gb.pos[0], gb.pos[2], 500.0f, 1);
        }
        if (gb.horse.done) go_think();
        break;
    case S_GALLOP: {
        /* 0x10 / 0x11: 25 a frame round the centre; steer to a heading 0xbd29 off the centre beyond 1800,
         * 0xc16d inside 1700; 728 units a frame beyond 1900, else 182. */
        float r = flat_distance(gb.pos, gb.centre), to_centre = angle_to(gb.centre), want = gb.yaw;
        float sign = gb.side ? -1.0f : 1.0f;
        /* The offsets are from the outward direction (centre to him): -94 deg leans in, -88 deg out. */
        if (r > GALLOP_FAR) want = to_centre + PI + sign * ((float)0xbd29 * UNITS - 2.0f * PI);
        else if (r < GALLOP_NEAR) want = to_centre + PI + sign * ((float)0xc16d * UNITS - 2.0f * PI);
        else want = to_centre + sign * PI * 0.5f;
        turn_toward(wrap_angle(want), (r > GALLOP_WIDE ? 728.0f : 182.0f) * UNITS * f);
        move_forward(RUN_SPEED, f);
        contact(f);
        if (gb.horse.loops >= gb.laps) {
            set_action(wrap_angle(angle_to(gb.dante) - gb.yaw) > 0.0f ? A_TURN_R : A_TURN_L, 0);
            set_state(S_TURN);
        }
        break;
    }
    case S_TURN:
        /* 5 / 6: round to Dante, then the straight run (4). */
        turn_toward(angle_to(gb.dante), 728.0f * UNITS * f);
        move_forward(RUN_SPEED * 0.5f, f);
        if (gb.horse.done) {
            /* On the ring, facing in: every other time that's 4c0c50's case for the spheres (then hidden, then
             * the missiles), else the straight run [approximation: the order is the port's]. */
            if (gb.think_count++ & 1) { set_action(A_SPHERES, 0); set_state(S_SPHERES); }
            else start_charge();
        }
        break;
    case S_LEAVE:
        /* Phase 0 at 80%: off the bridge's far end, and the bridge falls (demo 33). */
        move_forward(RUN_SPEED, f);
        if (gb.timer > 90.0f) {
            gb.hidden = 1;
            set_state(S_SWITCH);
        }
        break;
    case S_SWITCH:
        if (gb.timer > 30.0f) {
            gb.phase = 1;
            memcpy(gb.pos, ground_start, sizeof(gb.pos));
            gb.floor_y = ground_start[1];
            memcpy(gb.centre, ground_start, sizeof(gb.centre));
            gb.yaw = angle_to(dante_below);
            gb.hidden = 0;
            gb.warp_ready = 1;
            memset(gb.shells, 0, sizeof(gb.shells));
            go_think();
        }
        break;
    default:
        break;
    }
}

/* ---- Skeletal animation and drawing ------------------------------------------------------------------- */
static void build_skeleton(const GeryonMesh *mesh, const Player *p, Transform *out, int cart) {
    static MotionSample current, previous;
    int i, k, fading = p->fade > 0.0f && p->previous >= 0;
    if (p->motion >= 0) motion_sample(motion_get(p->motion), p->frame, &current);
    else memset(&current, 0, sizeof(current));
    if (fading) motion_sample(motion_get(p->previous), p->previous_frame, &previous);
    for (i = 0; i < mesh->bone_count; ++i) {
        float r[3] = {0.0f, 0.0f, 0.0f};
        float pos[3] = {mesh->local[i].x, mesh->local[i].y, mesh->local[i].z};
        Transform local;
        for (k = 0; k < 3; ++k) {
            if (current.has_rot[i][k]) r[k] = current.rot[i][k];
            if (current.has_trans[i][k]) pos[k] = current.trans[i][k];
            if (fading) {
                float old_r = previous.has_rot[i][k] ? previous.rot[i][k] : r[k];
                float old_p = previous.has_trans[i][k] ? previous.trans[i][k] : pos[k];
                r[k] += wrap_angle(old_r - r[k]) * p->fade;
                pos[k] += (old_p - pos[k]) * p->fade;
            }
        }
        if (cart && i <= 2 && current.has_rot[i][0] && current.has_trans[i][0]) {
            /* The carriage's bones 1 and 2 carry both triples the other way round: channels 3-5 are where the
             * bone is (bone 1: about (0, 0, -612), its body trailing the hitch), 6-8 its turn [inferred from the
             * values]. Bone 1 is turned to face the hitch. */
            float t[3] = {pos[0], pos[1], pos[2]};
            for (k = 0; k < 3; ++k) { pos[k] = r[k]; r[k] = t[k]; }
            if (i == 1) r[1] += atan2f(-pos[0], -pos[2]);
        }
        if (i == 0) { pos[0] = 0.0f; pos[2] = 0.0f; }   /* the AI moves him; the root's x/z don't */
        euler(&local, r[0], r[1], r[2]);
        memcpy(local.p, pos, sizeof(pos));
        if (mesh->parents[i] < 0) out[i] = local;
        else multiply(&out[i], &out[mesh->parents[i]], &local);
    }
}
static void skin_part(int part, const Transform *bones) {
    const GeryonMesh *m = &geryon_parts[part];
    int i, j, k;
    for (i = 0; i < m->vertices; ++i) {
        Vertex *v = &vertices[part][i];
        float p[3] = {0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
        for (j = 0; j < 3; ++j) {
            int b = m->bones[i][j], w = m->weights[i][j];
            const Transform *t;
            float src[3], ns[3], weight;
            if (!w) continue;
            t = &bones[b < m->bone_count ? b : 0];
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
    const GeryonMesh *m = &geryon_parts[part];
    int i, bound = -1;
    for (i = 0; i < m->objects_count; ++i) {
        const Dmc3ModelObject *o = &m->objects[i];
        if ((int)o->texture != bound) {
            const Dmc3Texture *t;
            bound = (int)o->texture;
            if (bound >= GERYON_TEXTURE_COUNT) bound = 0;
            t = &geryon_textures[bound];
            sceGuClutLoad(32, geryon_palettes[bound]);
            sceGuTexImage(0, t->width, t->height, t->width, textures[bound]);
            sceGuTexFlush();
        }
        sceGumDrawArray(GU_TRIANGLES, VTYPE, (int)o->index_count, indices[part] + o->index_start, vertices[part]);
    }
}
static void place(const float at[3], float yaw) {
    ScePspFVector3 p = {at[0], at[1], at[2]};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(yaw);
}
void geryon_boss_draw(void) {
    int i;
    if (!gb.active) return;
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
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    if (!gb.hidden) {
        build_skeleton(&geryon_parts[PART_HORSE], &gb.horse, horse_pose, 0);
        build_skeleton(&geryon_parts[PART_CART], &gb.cart, cart_pose, 1);
        skin_part(PART_HORSE, horse_pose);
        skin_part(PART_CART, cart_pose);
        place(gb.pos, gb.yaw);
        if (trace_on && (gb.tick % 120) == 1) {
            char line[200];
            const Vertex *v = vertices[PART_HORSE];
            snprintf(line, sizeof(line), "draw v0=%.0f,%.0f,%.0f v100=%.0f,%.0f,%.0f c0=%.0f,%.0f,%.0f",
                     v[0].x, v[0].y, v[0].z, v[100].x, v[100].y, v[100].z,
                     vertices[PART_CART][0].x, vertices[PART_CART][0].y, vertices[PART_CART][0].z);
            trace(line);
        }
        draw_part(PART_HORSE);
        draw_part(PART_CART);
    }
    for (i = 0; i < SHELLS; ++i) {
        const Shell *s = &gb.shells[i];
        if (!s->on) continue;
        place(s->pos, atan2f(s->vel[0], s->vel[2]));
        draw_part(s->kind);
    }
    sceGuDisable(GU_ALPHA_TEST);
}

/* ---- Hits from Dante --------------------------------------------------------------------------------------- */
int geryon_boss_attack_landed(float *from_x, float *from_z, int *heavy, float *damage) {
    if (!gb.landed) return 0;
    gb.landed = 0;
    *from_x = gb.landed_x;
    *from_z = gb.landed_z;
    *heavy = gb.landed_heavy;
    *damage = gb.landed_damage;
    return 1;
}
int geryon_boss_hit(int kind, float from_x, float from_z, int mortal) {
    float at[3], damage;
    (void)from_x; (void)from_z;
    if (!gb.active || gb.dying || gb.hidden || gb.state == S_LEAVE || gb.state == S_SWITCH) return 0;
    geryon_boss_target_position(at);
    fx_hit_spark(at);
    /* As Cerberus's and Vergil's: a light hit 42, a heavy one 84, x1.1; Easy x2; guns 20% [inferred]. */
    damage = kind == HIT_PISTOL ? 2.0f : (kind == HIT_LIGHT || kind == HIT_AIR ? 42.0f : 84.0f) * 1.1f;
    if (boss_difficulty == 0) damage *= 2.0f;
    if (!mortal) damage = 0.0f;
    gb.hp = fmaxf(0.0f, gb.hp - damage);
    trace("hit");
    if (gb.phase == 0 && gb.hp <= LEAVE_HEALTH) {
        /* He leaves the bridge for its far end, whichever is further from Dante. */
        float axis = atan2f(bridge_b[0] - bridge_a[0], bridge_b[2] - bridge_a[2]);
        gb.yaw = bridge_t(gb.dante) < 0.5f ? axis : wrap_angle(axis + PI);
        set_action(A_CHARGE, 1);
        set_state(S_LEAVE);
        return 0;
    }
    if (gb.hp <= 0.0f) {
        gb.dying = 1;
        memset(gb.shells, 0, sizeof(gb.shells));
        set_action(A_DEATH, 0);
        set_state(S_DEATH);
        return 1;
    }
    return 0;
}

/* ---- Frame ------------------------------------------------------------------------------------------------ */
void geryon_boss_update(float frames, float px, float py, float pz, int attacks) {
    if (!gb.active) return;
    ++gb.tick;
    gb.dante[0] = px; gb.dante[1] = py; gb.dante[2] = pz;
    if (!gb.dying && (attacks || gb.state == S_LEAVE || gb.state == S_SWITCH)) ai_step(frames);
    tick(&gb.horse, frames);
    tick(&gb.cart, frames);
    if (!gb.dying) shells_update(frames);
    if (gb.phase == 0) gb.pos[1] = bridge_start[1];
    else if (floor_fn) {
        float y;
        if (floor_fn(gb.pos[0], gb.pos[1] + 100.0f, gb.pos[2], &y)) gb.floor_y = y;
        gb.pos[1] = gb.floor_y;
    }
    if (trace_on && (gb.tick % 30) == 0) trace("tick");
}
int geryon_boss_take_warp(float out[4]) {
    if (!gb.warp_ready) return 0;
    gb.warp_ready = 0;
    memcpy(out, dante_below, sizeof(dante_below));
    return 1;
}
void geryon_boss_spawn(float x, float y, float z) {
    (void)x; (void)y; (void)z;
    memset(&gb, 0, sizeof(gb));
    gb.active = 1;
    memcpy(gb.pos, bridge_start, sizeof(gb.pos));
    gb.floor_y = bridge_start[1];
    /* Facing back down the bridge, toward Dante's end (the yaw from (400, 400) to (5600, 5600), turned). */
    gb.yaw = wrap_angle(atan2f(bridge_b[0] - bridge_a[0], bridge_b[2] - bridge_a[2]) + PI);
    gb.hp = MAX_HEALTH;
    gb.horse.motion = gb.horse.previous = gb.cart.motion = gb.cart.previous = -1;
    go_think();
    trace("spawn");
}
void geryon_boss_kill(void) { if (gb.active && !gb.dying) { gb.hp = 0.0f; gb.dying = 1; set_action(A_DEATH, 0); set_state(S_DEATH); } }
int geryon_boss_dying(void) { return gb.dying; }
int geryon_boss_hidden(void) { return gb.hidden; }
float geryon_boss_health(void) { return gb.hp; }
float geryon_boss_max_health(void) { return MAX_HEALTH; }
void geryon_boss_position(float out[3]) { memcpy(out, gb.pos, sizeof(gb.pos)); }
void geryon_boss_target_position(float out[3]) {
    /* The nearest point on his spine (horse's head 330 to the carriage's back -950) to Dante, brought out to
     * the body's side toward him. */
    float c = cosf(gb.yaw), s = sinf(gb.yaw), dx = gb.dante[0] - gb.pos[0], dz = gb.dante[2] - gb.pos[2];
    float along = fmaxf(-950.0f, fminf(330.0f, s * dx + c * dz)), across = c * dx - s * dz;
    float half = along > -200.0f ? 90.0f : 300.0f;
    across = fmaxf(-half, fminf(half, across));
    local_to_world(across, 150.0f, along, out);
}
void geryon_boss_push_out(float *x, float *z, float y, float r) {
    float c, s, dx, dz, along, across, half;
    if (!gb.active || gb.hidden || gb.dying) return;
    if (y > gb.pos[1] + 450.0f || y + 180.0f < gb.pos[1]) return;
    c = cosf(gb.yaw); s = sinf(gb.yaw);
    dx = *x - gb.pos[0]; dz = *z - gb.pos[2];
    along = s * dx + c * dz;
    across = c * dx - s * dz;
    if (along < -1000.0f - r || along > 380.0f + r) return;
    half = (along > -200.0f ? 90.0f : 330.0f) + r;
    if (fabsf(across) >= half) return;
    across = across < 0.0f ? -half : half;   /* out to the nearer side */
    *x = gb.pos[0] + c * across + s * along;
    *z = gb.pos[2] - s * across + c * along;
}
int geryon_boss_init(void) {
    int body, i, j;
    for (body = 0; body < 2; ++body)
        for (i = 0; i < 21; ++i) {
            char name[8];
            snprintf(name, sizeof(name), "%s%02d", body ? "gc" : "gh", i);
            motion_ids[body][i] = motion_find(name);
        }
    if (motion_ids[0][0] < 0 || motion_ids[1][0] < 0) return 0;
    for (i = 0; i < GERYON_PART_COUNT; ++i) {
        const GeryonMesh *m = &geryon_parts[i];
        vertices[i] = memalign(16, sizeof(Vertex) * m->vertices);
        indices[i] = memalign(16, sizeof(unsigned short) * m->indices_count);
        if (!vertices[i] || !indices[i]) return 0;
        memcpy(indices[i], m->indices, sizeof(unsigned short) * m->indices_count);
        for (j = 0; j < m->vertices; ++j) {
            vertices[i][j].u = m->uvs[j].u;
            vertices[i][j].v = m->uvs[j].v;
        }
    }
    /* The shells are rigid: posed once at rest. */
    identity(&identity_pose[0]);
    identity(&identity_pose[1]);
    for (i = PART_MISSILE; i < GERYON_PART_COUNT; ++i) {
        const GeryonMesh *m = &geryon_parts[i];
        for (j = 0; j < m->vertices; ++j) {
            vertices[i][j].x = m->positions[j].x; vertices[i][j].y = m->positions[j].y; vertices[i][j].z = m->positions[j].z;
            vertices[i][j].nx = m->normals[j].x; vertices[i][j].ny = m->normals[j].y; vertices[i][j].nz = m->normals[j].z;
        }
    }
    for (i = 0; i < GERYON_TEXTURE_COUNT; ++i) {
        const Dmc3Texture *t = &geryon_textures[i];
        size_t bytes = (size_t)t->width * t->height;
        textures[i] = memalign(16, bytes);
        if (!textures[i]) return 0;
        memcpy(textures[i], t->rgba, bytes);
        if (!swizzle_texture8(textures[i], t->width, t->height)) return 0;
    }
    sceKernelDcacheWritebackAll();
    return 1;
}
