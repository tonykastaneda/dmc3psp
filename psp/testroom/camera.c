/* The game's camera (docs/CAMERA.md): CCameraCtrl's zones, the Pan and Rail cameras, and CCameraPlayer. */
#include "camera.h"

#include <math.h>
#include <string.h>

#include "camera_runtime.h"
#include "stage.h"

#define PI_F 3.14159265f
#define DEG 0.0174532925f
/* FUN_0024e5e0's defaults. */
#define FOLLOW_HEIGHT 140.0f
#define FOLLOW_PITCH 0.25307274f
#define FOLLOW_LEN 460.0f
#define FOLLOW_FOV 0.95993f
#define FOLLOW_EASE 1000.0f          /* +0x1e0: a step of d*d / (2 * this) per frame */
#define CAM_RADIUS 50.0f             /* FUN_0024f560 / FUN_0024f640 */
#define RAIL_STEP 25.0f              /* cm per frame along the path (FUN_002af5a0) */
#define MAX_RAIL 32

int camera_battle, camera_boss;

static const CamRoom *room;
static int room_start;
static float pitch = FOLLOW_PITCH, len = FOLLOW_LEN, fov_follow = FOLLOW_FOV;

/* The follow camera. */
static float f_eye[3], f_target[3], f_bearing, f_turn_speed, f_turn_goal, f_moved[3];
static int f_turning, f_frozen;

/* The zones. */
static int zone = -1, zone_prev = -1, fixed_on;
static float out_eye[3], out_target[3], out_fov, out_roll;
static float blend_eye[3], blend_target[3], blend_fov, blend_roll;
static int blend_n, blend_k;
/* The fixed camera in use. */
static float x_target[3], x_lock = 1.0f, x_jump_y, x_jump_off;
static int x_jumping, was_on_ground = 1;
/* Its rails, built when it starts: per segment the length (m) and the cubic's coefficients. */
static int r_count, r_seg;
static float r_t, r_len[MAX_RAIL], r_len_eye[MAX_RAIL];   /* r_len: the path's segments, which the progress uses */
static float r_eye[MAX_RAIL][4][3], r_path[MAX_RAIL][4][3];
static float frame_accum;
static float written[6];   /* what camera_update last left in the FollowCamera */

static float wrap(float a) {
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}
static float dist3(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static void copy3(float d[3], const float s[3]) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }

/* Move p toward goal by d*d*k (k = 1/2000 for both the follow camera and the fixed ones); never past it. */
static void ease(float p[3], const float goal[3], float k) {
    float d = dist3(p, goal), step;
    int i;
    if (d < 1e-4f) return;
    step = d * d * k;
    if (step > d) step = d;
    for (i = 0; i < 3; ++i) p[i] += (goal[i] - p[i]) / d * step;
}

/* The camera's collision: a 50 cm ball from a to b, stopping in front of what it hits. */
static int sweep(const float a[3], float b[3]) {
    float d = dist3(a, b), far[3], t;
    int i;
    if (!stage_loaded() || d < 1e-3f) return 0;
    for (i = 0; i < 3; ++i) far[i] = a[i] + (b[i] - a[i]) * (d + CAM_RADIUS) / d;
    if (!stage_raycast(a, far, &t) || t >= 1.0f) return 0;
    t = t * (d + CAM_RADIUS) - CAM_RADIUS;
    if (t < 0.0f) t = 0.0f;
    if (t >= d) return 0;
    for (i = 0; i < 3; ++i) b[i] = a[i] + (b[i] - a[i]) * t / d;
    return 1;
}

void camera_room(const char *room_file, int start) {
    int i;
    room = 0;
    room_start = start;
    for (i = 0; room_file && i < CAM_ROOM_COUNT; ++i)
        if (strcmp(cam_rooms[i].file, room_file) == 0) room = &cam_rooms[i];
    /* FUN_0024e6a0: only the values the room gives (not negative). */
    pitch = room && room->pitch >= 0.0f ? room->pitch * DEG : FOLLOW_PITCH;
    len = room && room->len >= 0.0f ? room->len : FOLLOW_LEN;
    fov_follow = room && room->aov > 0.0f ? room->aov * DEG : FOLLOW_FOV;
}

int camera_zone(void) { return fixed_on ? zone : -1; }

/* FUN_0024e7d0: the follow camera placed at a bearing, the collision sweep starting from the room's nearest
 * safe point (cam_init; the default is (2500, 200, 2500)). */
static void follow_place(const Player *player, float bearing) {
    float h = cosf(pitch) * len, from[3] = {2500.0f, 200.0f, 2500.0f};
    f_target[0] = player->x; f_target[1] = player->y + FOLLOW_HEIGHT; f_target[2] = player->z;
    f_eye[0] = f_target[0] + sinf(bearing) * h;
    f_eye[1] = f_target[1] + sinf(pitch) * len;
    f_eye[2] = f_target[2] + cosf(bearing) * h;
    if (room && room->init_count) {
        int i, best = 0;
        for (i = 1; i < room->init_count; ++i)
            if (dist3(room->init[i], f_eye) < dist3(room->init[best], f_eye)) best = i;
        copy3(from, room->init[best]);
    }
    sweep(from, f_eye);
    f_bearing = bearing;
    f_turning = 0;
    f_frozen = 1;
    f_moved[0] = player->x; f_moved[1] = player->y; f_moved[2] = player->z;
}

void camera_start(const Player *player, FollowCamera *camera) {
    float off = room && room_start >= 0 && room_start < room->start_count ? room->start_yaw[room_start] * DEG : 0.0f;
    follow_place(player, player->yaw + off);
    zone = zone_prev = -1;
    fixed_on = 0;
    blend_n = 0;
    frame_accum = 0.0f;
    copy3(out_eye, f_eye); copy3(out_target, f_target);
    out_fov = fov_follow; out_roll = 0.0f;
    camera->x = out_eye[0]; camera->y = out_eye[1]; camera->z = out_eye[2];
    camera->tx = out_target[0]; camera->ty = out_target[1]; camera->tz = out_target[2];
    camera->yaw = camera->target_yaw = atan2f(camera->tx - camera->x, camera->tz - camera->z);
    camera->fov = out_fov; camera->roll = 0.0f;
    camera->stick_hold = 0;
    camera->snapping = 0;
    written[0] = camera->x; written[1] = camera->y; written[2] = camera->z;
    written[3] = camera->tx; written[4] = camera->ty; written[5] = camera->tz;
}

/* One frame of CCameraPlayer (FUN_0024e9c0, mode 0). */
static void follow_frame(const Player *player, int reset_pressed) {
    float old_eye[3], goal_t[3], goal_e[3], v[3], h, l;
    int i;
    if (f_frozen) {
        float p[3] = {player->x, player->y, player->z};
        if (dist3(p, f_moved) >= 1.0f) f_frozen = 0;
        copy3(f_moved, p);
        return;
    }
    copy3(old_eye, f_eye);
    /* FUN_0024f2a0: the goal keeps the eye's bearing, at the room's pitch and distance. */
    goal_t[0] = player->x; goal_t[1] = player->y + FOLLOW_HEIGHT; goal_t[2] = player->z;
    v[0] = f_eye[0] - goal_t[0]; v[2] = f_eye[2] - goal_t[2];
    h = sqrtf(v[0] * v[0] + v[2] * v[2]);
    if (h < 1e-3f) { v[0] = sinf(f_bearing); v[2] = cosf(f_bearing); h = 1.0f; }
    v[1] = tanf(pitch) * h;
    l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (i = 0; i < 3; ++i) goal_e[i] = goal_t[i] + v[i] / l * len;
    ease(f_eye, goal_e, 1.0f / (2.0f * FOLLOW_EASE));
    ease(f_target, goal_t, 1.0f / (2.0f * FOLLOW_EASE));
    /* The camera reset: behind him, a sixth of the angle each frame. */
    if (reset_pressed && !f_turning) {
        f_turning = 1;
        f_turn_goal = wrap(player->yaw + PI_F);
        f_turn_speed = -1.0f;
    }
    if (f_turning) {
        float dx = f_eye[0] - f_target[0], dz = f_eye[2] - f_target[2], hd = sqrtf(dx * dx + dz * dz);
        float b = atan2f(dx, dz), diff;
        if (f_turn_speed < 0.0f) {
            diff = wrap(f_turn_goal - b);
            if (fabsf(diff) < DEG) { b = f_turn_goal; f_turning = 0; }
            else f_turning = diff > 0.0f ? 1 : -1;
            f_turn_speed = fabsf(diff / 6.0f);
        }
        if (f_turning) {
            b += f_turn_speed * (float)f_turning;
            if (fabsf(wrap(f_turn_goal - b)) < f_turn_speed) { b = f_turn_goal; f_turning = 0; }
        }
        f_eye[0] = f_target[0] + sinf(b) * hd;
        f_eye[2] = f_target[2] + cosf(b) * hd;
    }
    /* FUN_0024f560: the ball from the old eye to the new; pushed back, from the target out instead. */
    {
        float want[3];
        copy3(want, f_eye);
        if (sweep(old_eye, f_eye) && dist3(want, f_eye) > 1.0f) {
            copy3(f_eye, want);
            sweep(f_target, f_eye);
        }
    }
    f_bearing = atan2f(f_eye[0] - f_target[0], f_eye[2] - f_target[2]);
}

/* ---- Rails: C2 cubic Hermite through the points, parametrised by chord length in metres (FUN_002cbd50). ---- */
static void build_spline(float coef[][4][3], const float (*pts)[3], int n, float *lens) {
    float a[MAX_RAIL], b[MAX_RAIL], c[MAX_RAIL], r[MAX_RAIL], m[MAX_RAIL][3];
    int i, k;
    if (n < 2 || n > MAX_RAIL) return;
    for (k = 0; k < 3; ++k) {
        for (i = 0; i < n; ++i) {
            if (i == 0) { a[i] = 0.0f; b[i] = 2.0f * lens[0]; c[i] = lens[0]; r[i] = 3.0f * (pts[1][k] - pts[0][k]); }
            else if (i == n - 1) { a[i] = lens[n - 2]; b[i] = 2.0f * lens[n - 2]; c[i] = 0.0f; r[i] = 3.0f * (pts[n - 1][k] - pts[n - 2][k]); }
            else {
                float l0 = lens[i - 1], l1 = lens[i];
                a[i] = l1; b[i] = 2.0f * (l0 + l1); c[i] = l0;
                r[i] = 3.0f * (l0 / l1 * (pts[i + 1][k] - pts[i][k]) + l1 / l0 * (pts[i][k] - pts[i - 1][k]));
            }
        }
        /* Thomas algorithm (FUN_002cc280). */
        for (i = 1; i < n; ++i) {
            float w = a[i] / b[i - 1];
            b[i] -= w * c[i - 1];
            r[i] -= w * r[i - 1];
        }
        m[n - 1][k] = r[n - 1] / b[n - 1];
        for (i = n - 2; i >= 0; --i) m[i][k] = (r[i] - c[i] * m[i + 1][k]) / b[i];
    }
    for (i = 0; i < n - 1; ++i) {
        float hh = lens[i];
        for (k = 0; k < 3; ++k) {
            coef[i][0][k] = 2.0f * (pts[i][k] - pts[i + 1][k]) / (hh * hh * hh) + (m[i][k] + m[i + 1][k]) / (hh * hh);
            coef[i][1][k] = 3.0f * (pts[i + 1][k] - pts[i][k]) / (hh * hh) - (2.0f * m[i][k] + m[i + 1][k]) / hh;
            coef[i][2][k] = m[i][k];
            coef[i][3][k] = pts[i][k];
        }
    }
}
static void spline_at(float coef[][4][3], const float *lens, int seg, float t, float out[3]) {
    float s;
    int k;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    s = t * lens[seg];
    for (k = 0; k < 3; ++k) out[k] = ((coef[seg][0][k] * s + coef[seg][1][k]) * s + coef[seg][2][k]) * s + coef[seg][3][k];
}
/* Nearest point of one path segment to p: t in [0,1]; *side -1/+1 when it's an end (FUN_002cb6a0). */
static float seg_nearest(int seg, const float p[3], int *side, float *dist) {
    float best = 1e30f, bt = 0.0f, lo, hi, q[3];
    int i, it;
    for (i = 0; i <= 16; ++i) {
        float t = (float)i / 16.0f, d;
        spline_at(r_path, r_len, seg, t, q);
        d = dist3(q, p);
        if (d < best) { best = d; bt = t; }
    }
    lo = bt - 1.0f / 16.0f; hi = bt + 1.0f / 16.0f;
    if (lo < 0.0f) lo = 0.0f;
    if (hi > 1.0f) hi = 1.0f;
    for (it = 0; it < 20; ++it) {
        float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f, q2[3];
        spline_at(r_path, r_len, seg, m1, q);
        spline_at(r_path, r_len, seg, m2, q2);
        if (dist3(q, p) < dist3(q2, p)) hi = m2; else lo = m1;
    }
    bt = (lo + hi) * 0.5f;
    spline_at(r_path, r_len, seg, bt, q);
    *dist = dist3(q, p);
    *side = bt <= 1e-4f ? -1 : bt >= 1.0f - 1e-4f ? 1 : 0;
    return bt;
}
/* FUN_002cb530: the segment from the nearest point, then its neighbour if it's off the end. */
static void path_project(const CamZone *z, const float p[3], int *seg_out, float *t_out) {
    const CamPoint *pt = &cam_points[z->point_first];
    float d[MAX_RAIL], dd, t;
    int i, seg = 0, side;
    for (i = 0; i < r_count; ++i) d[i] = dist3(pt[i].path, p);
    for (i = 1; i < r_count; ++i) if (d[i] < d[seg]) seg = i;
    if (seg >= r_count - 1) seg = r_count - 2;
    else if (seg > 0 && d[seg - 1] < d[seg + 1]) seg -= 1;
    t = seg_nearest(seg, p, &side, &dd);
    if (side < 0 && seg > 0) { float t2, d2; int s2; t2 = seg_nearest(seg - 1, p, &s2, &d2); if (s2 >= 0) { seg -= 1; t = t2; } }
    else if (side > 0 && seg < r_count - 2) { float t2, d2; int s2; t2 = seg_nearest(seg + 1, p, &s2, &d2); if (s2 <= 0) { seg += 1; t = t2; } }
    *seg_out = seg; *t_out = t;
}

static float zoom_factor(const CamZone *z, float d) {
    if (z->zoom[2] == 0.0f || z->zoom[3] == 0.0f) return 1.0f;
    if (d <= z->zoom[0]) return z->zoom[2];
    if (d >= z->zoom[1]) return z->zoom[3];
    if (z->zoom[1] == z->zoom[0]) return (z->zoom[2] + z->zoom[3]) * 0.5f;
    return z->zoom[2] + (z->zoom[3] - z->zoom[2]) * (d - z->zoom[0]) / (z->zoom[1] - z->zoom[0]);
}

/* The Pan or Rail camera's goal (FUN_002af2a0 / FUN_002af5a0). */
static void fixed_goal(const CamZone *z, const Player *player, float target[3], float eye[3], float *fov, float *roll) {
    float base[3], feet[3] = {player->x, player->y, player->z};
    if (z->kind == 1 && r_count < 2) {
        const CamPoint *pt = &cam_points[z->point_first];
        copy3(base, z->point_count ? pt->path : feet);
        copy3(eye, z->point_count ? pt->eye : feet);
        *fov = z->point_count ? pt->fov : FOLLOW_FOV;
        *roll = 0.0f;
    } else if (z->kind == 1) {
        int seg;
        float t, want, have;
        const CamPoint *pt = &cam_points[z->point_first];
        path_project(z, feet, &seg, &t);
        /* Along the path, at most 25 cm a frame. */
        want = r_len[seg] * t * 100.0f;
        have = r_len[r_seg] * r_t * 100.0f;
        if (seg == r_seg) {
            if (fabsf(want - have) >= RAIL_STEP) want = have + (want > have ? RAIL_STEP : -RAIL_STEP);
        } else if (seg > r_seg) {
            float budget = RAIL_STEP;
            for (;;) {
                float rest = r_len[r_seg] * 100.0f - have;
                if (budget < rest) { want = have + budget; seg = r_seg; break; }
                budget -= rest; have = 0.0f; r_seg++;
                if (r_seg >= seg) { if (budget < want) want = budget; break; }
            }
        } else {
            float budget = RAIL_STEP;
            for (;;) {
                if (budget < have) { want = have - budget; seg = r_seg; break; }
                budget -= have; r_seg--; have = r_len[r_seg] * 100.0f;
                if (r_seg <= seg) { if (budget < have - want) want = have - budget; break; }
            }
        }
        r_t = want * 0.01f / r_len[r_seg];
        spline_at(r_path, r_len, r_seg, r_t, base);
        spline_at(r_eye, r_len_eye, r_seg, r_t, eye);
        *fov = pt[r_seg].fov + (pt[r_seg + 1].fov - pt[r_seg].fov) * r_t;
        *roll = pt[r_seg].roll + (pt[r_seg + 1].roll - pt[r_seg].roll) * r_t;
    } else {
        copy3(base, z->fixed);
        copy3(eye, z->eye);
        *fov = z->fov;
        *roll = z->roll;
    }
    if (z->target == 2) { target[0] = feet[0] + z->world_off[0]; target[1] = feet[1] + z->world_off[1]; target[2] = feet[2] + z->world_off[2]; }
    else if (z->target == 1) {
        /* Dante's matrix times the offset: x right, z forward. */
        float s = sinf(player->yaw), c = cosf(player->yaw);
        target[0] = feet[0] + z->mat_off[0] * c + z->mat_off[2] * s;
        target[1] = feet[1] + z->mat_off[1];
        target[2] = feet[2] - z->mat_off[0] * s + z->mat_off[2] * c;
    } else copy3(target, base);
    if (z->axis == 2) { target[0] = base[0]; target[2] = base[2]; }
    else if (z->axis == 1) target[1] = base[1];
    /* Jumps: 1 keeps the eye's height over the target, 2 holds the target's height, 3 both. */
    if (z->axis != 1 && z->jump) {
        int start = was_on_ground && !player->on_ground;
        if (start) { x_jump_y = target[1]; x_jump_off = eye[1] - target[1]; x_jumping = 1; }
        if (player->on_ground) x_jumping = 0;
        if (x_jumping) {
            if (z->jump == 2 || z->jump == 3) target[1] = x_jump_y;
            if (z->jump == 1 || z->jump == 3) eye[1] = target[1] + x_jump_off;
        }
    }
}

static void fixed_begin(const CamZone *z, const Player *player) {
    float e[3], fv, rl;
    x_jumping = 0;
    x_lock = 1.0f;
    if (z->kind == 1) {
        const CamPoint *pt = &cam_points[z->point_first];
        float eyes[MAX_RAIL][3], paths[MAX_RAIL][3], feet[3] = {player->x, player->y, player->z};
        int i;
        r_count = z->point_count > MAX_RAIL ? MAX_RAIL : z->point_count;
        for (i = 0; i < r_count; ++i) { copy3(eyes[i], pt[i].eye); copy3(paths[i], pt[i].path); }
        for (i = 0; i < r_count - 1; ++i) {
            /* FUN_002cb300: each spline's own segment lengths, in metres. */
            float l = dist3(eyes[i + 1], eyes[i]) / 100.0f, lp = dist3(paths[i + 1], paths[i]) / 100.0f;
            r_len_eye[i] = l > 1e-3f ? l : 1e-3f;
            r_len[i] = lp > 1e-3f ? lp : 1e-3f;
        }
        build_spline(r_eye, (const float (*)[3])eyes, r_count, r_len_eye);
        build_spline(r_path, (const float (*)[3])paths, r_count, r_len);
        path_project(z, feet, &r_seg, &r_t);
    }
    fixed_goal(z, player, x_target, e, &fv, &rl);
}

/* One frame of the fixed camera (FUN_002aea60). */
static void fixed_frame(const CamZone *z, const Player *player) {
    float goal_t[3], eye[3], fv, rl, v[3], d, zf, lock_goal, diff;
    int i;
    fixed_goal(z, player, goal_t, eye, &fv, &rl);
    ease(x_target, goal_t, 0.0005f);
    for (i = 0; i < 3; ++i) v[i] = eye[i] - x_target[i];
    d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    zf = zoom_factor(z, d);
    if (z->zoom_mode == 1 && d > 1e-3f) {
        float k = tanf(fv * zf) / tanf(fv);
        for (i = 0; i < 3; ++i) eye[i] = x_target[i] + v[i] * k;
    } else fv *= zf;
    lock_goal = player->locked_on && z->lock_fov != 0.0f ? z->lock_fov : 1.0f;
    diff = (lock_goal - x_lock) * (lock_goal - x_lock) * 0.5f;
    if (lock_goal < x_lock) { x_lock -= diff; if (x_lock < lock_goal) x_lock = lock_goal; }
    else if (lock_goal > x_lock) { x_lock += diff; if (x_lock > lock_goal) x_lock = lock_goal; }
    copy3(out_eye, eye);
    copy3(out_target, x_target);
    out_fov = fv * x_lock;
    out_roll = rl;
}

/* ---- Boss cameras (FUN_003204c0). Only Cerberus's (id 3, FUN_004fbdf0) is ported. ---- */
static int boss_on;
static float b_pitch, b_yaw, b_raise, b_goal[3];
static const float cerberus_centre[3] = {2520.0f, 200.0f, 3000.0f};   /* 0x5e0730 */

/* FUN_00321190: the eye d out from the target, turned by pitch then yaw (FUN_0042ebd0 on (0, 0, -d)). */
static void boss_orbit(const float t[3], float d, float out[3]) {
    out[0] = t[0] - d * cosf(b_pitch) * sinf(b_yaw);
    out[1] = t[1] + d * sinf(b_pitch);
    out[2] = t[2] - d * cosf(b_pitch) * cosf(b_yaw);
}
/* FUN_0042ec50: x/z and y each eased by their own factor. */
static void boss_ease(float cur[3], const float goal[3], float fxz, float fy) {
    cur[0] = cur[0] * fxz + goal[0] * (1.0f - fxz);
    cur[2] = cur[2] * fxz + goal[2] * (1.0f - fxz);
    cur[1] = cur[1] * fy + goal[1] * (1.0f - fy);
}
static void boss_target(const Player *player, float t[3]) {
    t[0] = player->x; t[1] = player->y + 150.0f; t[2] = player->z;   /* FUN_00320a00 */
}
/* FUN_00320850: the first frame, 400 out and 3 degrees up, no easing. */
static void boss_begin(const Player *player) {
    float t[3];
    boss_target(player, t);
    b_pitch = 0.0523599f;
    b_yaw = atan2f(t[0] - out_eye[0], t[2] - out_eye[2]) + 5.0f * DEG;
    boss_orbit(t, 400.0f, out_eye);
    sweep(t, out_eye);
    copy3(out_target, t);
    copy3(b_goal, out_eye);
    b_raise = 0.0f;
    out_fov = 50.0f * DEG;   /* +0x220 */
    out_roll = 0.0f;
    boss_on = 1;
}
static void boss_cerberus(const Player *player) {
    float t[3], e[3], prev[3], dx, dz;
    copy3(prev, b_goal);
    boss_target(player, t);
    if (!player->locked_on) {
        b_pitch = 4.0f * DEG;
        b_yaw = atan2f(cerberus_centre[0] - t[0], cerberus_centre[2] - t[2]);
    } else {
        /* Turned toward what he's locked on to, only when it's moved well off (15 degrees up/down, 35 round). */
        float l[3] = {player->target_x, player->target_y - 50.0f, player->target_z + 300.0f}, h, np, ny;
        if (l[1] < 50.0f) l[1] = 50.0f;
        h = sqrtf((l[0] - t[0]) * (l[0] - t[0]) + (l[2] - t[2]) * (l[2] - t[2]));
        np = atan2f(-(l[1] - t[1]), h);
        ny = atan2f(l[0] - t[0], l[2] - t[2]);
        if (fabsf(b_pitch - np) >= 0.2617994f) b_pitch = np;
        if (fabsf(b_yaw - ny) >= 0.61086524f) b_yaw = ny;
        if (b_pitch < -0.34906584f) b_pitch = -0.34906584f;
        else if (b_pitch > 0.2617994f) b_pitch = 0.2617994f;
    }
    boss_orbit(t, 400.0f, e);
    /* Blocked: the eye stops at the wall, and both rise by up to 20 (3 a frame). */
    if (sweep(t, e)) { b_raise += 3.0f; if (b_raise > 20.0f) b_raise = 20.0f; }
    else { b_raise -= 3.0f; if (b_raise < 0.0f) b_raise = 0.0f; }
    t[1] += b_raise; e[1] += b_raise;
    if (dist3(out_eye, t) > 800.0f) {
        b_pitch = 4.0f * DEG;
        b_yaw = atan2f(cerberus_centre[0] - t[0], cerberus_centre[2] - t[2]);
    } else {
        /* It doesn't come into the middle of the arena. */
        dx = e[0] - cerberus_centre[0]; dz = e[2] - cerberus_centre[2];
        if (dx * dx + dz * dz < 800.0f * 800.0f) copy3(e, prev);
    }
    copy3(b_goal, e);
    boss_ease(out_eye, e, 0.9f, 0.9f);
    boss_ease(out_target, t, 0.9f, 0.9f);
    if (player->on_ground && out_eye[1] - player->y < 20.0f) out_eye[1] = player->y + 20.0f;
    out_fov = 50.0f * DEG;
}

static int area_inside(const CamArea *a, const float p[3]) {
    int i, pos = 0, neg = 0;
    if (p[1] < a->c[0][1] || p[1] > a->c[0][1] + a->height) return 0;
    for (i = 0; i < 4; ++i) {
        const float *u = a->c[i], *w = a->c[(i + 1) & 3];
        float cr = (w[0] - u[0]) * (p[2] - u[2]) - (w[2] - u[2]) * (p[0] - u[0]);
        if (cr > 0.0f) pos++; else if (cr < 0.0f) neg++;
    }
    return !(pos && neg);
}
static int zone_inside(const CamZone *z, const float p[3]) {
    int i;
    for (i = 0; i < z->area_count; ++i) if (area_inside(&cam_areas[z->area_first + i], p)) return 1;
    return 0;
}

/* FUN_002bcbd0's choice. */
static int choose_zone(const float p[3]) {
    const CamZone *zs = room ? &cam_zones[room->zone_first] : 0;
    int n = room ? room->zone_count : 0, cur = -1, best = 0x100, kept = 0, i;
    if (zone >= 0 && zone < n && (!zs[zone].battle || camera_battle) && zone_inside(&zs[zone], p)) {
        cur = zone; best = zs[zone].pri; kept = 1;
    }
    for (i = 0; i < n; ++i) {
        const CamZone *z = &zs[i];
        if (i == zone || !zone_inside(z, p)) continue;
        if (z->battle) {
            if (camera_battle && (!kept || !zs[cur].battle)) { cur = i; best = z->pri; }
        } else if (z->pri == best) {
            if ((cur < 0 || !zs[cur].battle) && !kept && i <= cur) { cur = i; best = z->pri; }
        } else if (z->pri < best && (cur < 0 || !zs[cur].battle)) {
            cur = i; best = z->pri;
        }
    }
    return cur;
}

static void frame(const Player *player, FollowCamera *camera, int reset_pressed) {
    float feet[3] = {player->x, player->y, player->z};
    int next = choose_zone(feet), cut = 0;
    if (camera_boss == 3) {
        if (!boss_on) boss_begin(player);
        boss_cerberus(player);
        zone = -1; fixed_on = 0; blend_n = 0;
        camera->x = out_eye[0]; camera->y = out_eye[1]; camera->z = out_eye[2];
        camera->tx = out_target[0]; camera->ty = out_target[1]; camera->tz = out_target[2];
        camera->fov = out_fov; camera->roll = 0.0f;
        camera->yaw = atan2f(camera->tx - camera->x, camera->tz - camera->z);
        was_on_ground = player->on_ground;
        return;
    }
    if (boss_on) {
        /* The boss camera hands back (0x61ac1c = 0x80): the follow camera from where its eye is. */
        boss_on = 0;
        follow_place(player, atan2f(out_eye[0] - player->x, out_eye[2] - player->z));
        f_frozen = 0;
    }
    if (camera_boss) {
        /* The game hands a boss fight to that boss's own camera (FUN_003204c0, 0x61ac1c); until those are
         * ported, the follow camera, taken over from where the eye is. */
        if (fixed_on) follow_place(player, atan2f(out_eye[0] - player->x, out_eye[2] - player->z));
        if (fixed_on) f_frozen = 0;
        fixed_on = 0;
        zone = -1;
        next = -1;
        blend_n = 0;
    }
    if (next != zone) {
        const CamZone *zs = room ? &cam_zones[room->zone_first] : 0;
        zone_prev = zone;
        zone = next;
        blend_n = 0;
        if (zone >= 0) {
            const CamZone *z = &zs[zone];
            int i;
            for (i = 0; i < z->trans_count; ++i)
                if (z->trans[i][0] == zone_prev && z->trans[i][1]) {
                    blend_n = z->trans[i][1]; blend_k = blend_n - 1;
                    copy3(blend_eye, out_eye); copy3(blend_target, out_target);
                    blend_fov = out_fov; blend_roll = out_roll;
                    break;
                }
            if (z->kind == 0 || z->kind == 1) { fixed_begin(z, player); fixed_on = 1; }
            else {
                /* Type 4 restarts the follow camera from where the eye is unless it came from another; 5 keeps it. */
                if (z->kind == 4 && (zone_prev < 0 || zs[zone_prev].kind != 4) && fixed_on)
                    follow_place(player, atan2f(out_eye[0] - player->x, out_eye[2] - player->z));
                fixed_on = 0;
            }
            if (!blend_n) cut = 1;
        }
        /* Out of every zone: the last camera stays. */
    }
    if (fixed_on) fixed_frame(&cam_zones[room->zone_first + zone], player);
    else {
        follow_frame(player, reset_pressed);
        copy3(out_eye, f_eye); copy3(out_target, f_target);
        out_fov = fov_follow; out_roll = 0.0f;
    }
    camera->x = out_eye[0]; camera->y = out_eye[1]; camera->z = out_eye[2];
    camera->tx = out_target[0]; camera->ty = out_target[1]; camera->tz = out_target[2];
    camera->fov = out_fov; camera->roll = out_roll;
    if (blend_n) {
        float f = (float)blend_k / (float)blend_n;
        f *= f;
        camera->x = blend_eye[0] * f + camera->x * (1.0f - f);
        camera->y = blend_eye[1] * f + camera->y * (1.0f - f);
        camera->z = blend_eye[2] * f + camera->z * (1.0f - f);
        camera->tx = blend_target[0] * f + camera->tx * (1.0f - f);
        camera->ty = blend_target[1] * f + camera->ty * (1.0f - f);
        camera->tz = blend_target[2] * f + camera->tz * (1.0f - f);
        camera->fov = blend_fov * f + camera->fov * (1.0f - f);
        camera->roll = blend_roll * f + camera->roll * (1.0f - f);
        if (--blend_k < 1) blend_n = 0;
    }
    camera->yaw = atan2f(camera->tx - camera->x, camera->tz - camera->z);
    if (cut) { camera->stick_hold = 2; }   /* camera_stick_yaw latches the stick's angle */
    was_on_ground = player->on_ground;
}

void camera_update(const Player *player, FollowCamera *camera, int reset_pressed, float dt) {
    /* Placed from outside (a test's spot, a reset, a cutscene): the follow camera goes on from there. */
    if (fabsf(camera->x - written[0]) + fabsf(camera->y - written[1]) + fabsf(camera->z - written[2]) +
        fabsf(camera->tx - written[3]) + fabsf(camera->ty - written[4]) + fabsf(camera->tz - written[5]) > 1.0f) {
        f_eye[0] = camera->x; f_eye[1] = camera->y; f_eye[2] = camera->z;
        f_target[0] = camera->tx; f_target[1] = camera->ty; f_target[2] = camera->tz;
        f_frozen = f_turning = 0;
        fixed_on = 0;
        zone = -1;
        blend_n = 0;
    }
    frame_accum += dt * 60.0f;
    if (frame_accum > 4.0f) frame_accum = 4.0f;
    while (frame_accum >= 1.0f) {
        frame(player, camera, reset_pressed);
        reset_pressed = 0;
        frame_accum -= 1.0f;
    }
    written[0] = camera->x; written[1] = camera->y; written[2] = camera->z;
    written[3] = camera->tx; written[4] = camera->ty; written[5] = camera->tz;
}

float camera_stick_yaw(FollowCamera *camera, float stick_x, float stick_y, int stick_active) {
    float angle = atan2f(stick_x, stick_y);
    if (camera->stick_hold == 2) {
        /* A cut: keep the old reference while the stick stays where it is. */
        camera->stick_hold = stick_active;
        camera->stick_angle = angle;
    }
    if (camera->stick_hold && (!stick_active || fabsf(wrap(angle - camera->stick_angle)) > 15.0f * DEG))
        camera->stick_hold = 0;
    if (!camera->stick_hold) camera->stick_yaw = camera->yaw;
    return camera->stick_yaw;
}
