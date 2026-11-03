#include "player.h"

#include <math.h>
#include <string.h>

#include "motion.h"

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318531f
#define STRIDE_LENGTH 165.0f /* cm covered per full run cycle */
#define STEP_HEIGHT 45.0f    /* curbs and steps Dante walks up without jumping */

float player_spawn[4] = {0.0f, 0.0f, -300.0f, 0.0f};
float player_room_limit = ROOM_HALF_SIZE - PLAYER_RADIUS;
int (*player_floor_fn)(float x, float y, float z, float step, float *out) = 0;
int camera_auto_follow = 0;
#define CAMERA_FOLLOW_RATE 1.6f  /* rad/s toward Dante's heading at full run speed */
#define CAMERA_FOLLOW_MAX_ANGLE 1.9f  /* rad off the camera; beyond this it holds still */

static int floor_at(float x, float y, float z, float *out) {
    if (!player_floor_fn) { *out = 0.0f; return 1; }
    return player_floor_fn(x, y, z, STEP_HEIGHT, out);
}

static float wrap_angle(float angle) {
    while (angle > PI_F) angle -= TWO_PI_F;
    while (angle < -PI_F) angle += TWO_PI_F;
    return angle;
}

static float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static void camera_goal(const Player *player, const FollowCamera *camera,
                        float *ex, float *ey, float *ez) {
    *ex = player->x - sinf(camera->yaw) * CAMERA_DISTANCE;
    *ey = player->y + CAMERA_HEIGHT;
    *ez = player->z - cosf(camera->yaw) * CAMERA_DISTANCE;
}

const char *player_style_name(Style style) {
    static const char *names[STYLE_COUNT] = {"TRICKSTER", "SWORDMASTER", "GUNSLINGER", "ROYALGUARD"};
    return (unsigned)style < STYLE_COUNT ? names[style] : "?";
}

const char *player_action_name(Action action) {
    switch (action) {
    case ACTION_MELEE: return "MELEE (no weapon yet)";
    case ACTION_GUN: return "GUNS (no weapon yet)";
    case ACTION_STYLE: return "STYLE ACTION (not yet)";
    case ACTION_JUMP: return "JUMP";
    case ACTION_ROLL: return "DODGE ROLL (not yet)";
    case ACTION_STYLE_SWITCH: return "STYLE SWITCH";
    case ACTION_CAMERA_SNAP: return "CAMERA SNAP";
    default: return "";
    }
}

static void set_action(Player *player, Action action) {
    player->last_action = action;
    player->action_timer = 1.2f;
}

void player_reset(Player *player, FollowCamera *camera) {
    memset(player, 0, sizeof(*player));
    player->jump_motion = -1;
    player->x = player_spawn[0];
    player->y = player_spawn[1];
    player->z = player_spawn[2];
    player->yaw = player_spawn[3];
    player->on_ground = 1;
    player->style = STYLE_TRICKSTER;
    memset(camera, 0, sizeof(*camera));
    camera->yaw = camera->target_yaw = player->yaw;
    camera_goal(player, camera, &camera->x, &camera->y, &camera->z);
    camera->tx = player->x;
    camera->ty = player->y + CAMERA_TARGET_HEIGHT;
    camera->tz = player->z;
    player->camera_yaw = camera->yaw;
}

static void turn_toward(Player *player, float yaw, float max_turn) {
    player->yaw = wrap_angle(player->yaw + clampf(wrap_angle(yaw - player->yaw), -max_turn, max_turn));
}

/* One frame of ground movement with the stick out of neutral. Turns Dante, sets
 * player->speed and returns the direction he travels. Lock-on plays no part:
 * none of the three state handlers reads R1 or the target. */
static float locomotion_step(Player *player, float stick_yaw, float magnitude, float dt) {
    float frames = dt * MOTION_FPS;
    int full_tilt = magnitude * STICK_TILT_SCALE >= STICK_TILT_RUN;
    if (!player->on_ground) {
        /* Air steering is the test room's own, not from the game's handlers. */
        player->loco = LOCO_IDLE;
        player->running = 1;
        player->speed = PLAYER_RUN_SPEED * magnitude;
        turn_toward(player, stick_yaw, PLAYER_TURN_RATE * dt);
        return stick_yaw;
    }
    if (player->loco == LOCO_IDLE) {
        /* Start: walk toward the direction held at this moment, turning to it
         * evenly over 8 frames. */
        player->loco = LOCO_START;
        player->running = 0;
        player->speed = PLAYER_WALK_SPEED;
        player->accel = 0.0f;
        player->loco_timer = LOCO_START_FRAMES;
        player->start_yaw = stick_yaw;
        player->start_turn = fabsf(wrap_angle(stick_yaw - player->yaw)) / LOCO_START_FRAMES;
        return stick_yaw;
    }
    if (player->loco == LOCO_START) {
        turn_toward(player, player->start_yaw, player->start_turn * frames);
        player->loco_timer -= frames;
        if (player->loco_timer <= 0.0f) {
            player->loco = LOCO_MOVE;
            player->loco_timer = LOCO_RUN_HOLD_FRAMES;
        }
        return player->start_yaw;
    }
    turn_toward(player, stick_yaw, PLAYER_TURN_RATE * dt);
    if (!player->running) {
        if (full_tilt) {
            player->loco_timer -= frames;
            if (player->loco_timer <= 0.0f) {
                player->running = 1;
                player->accel = (PLAYER_RUN_SPEED - player->speed) / LOCO_ACCEL_FRAMES;
            }
        } else {
            player->loco_timer = LOCO_RUN_HOLD_FRAMES;
        }
    } else if (!full_tilt) {
        player->running = 0;
        player->speed = PLAYER_WALK_SPEED;
        player->accel = 0.0f;
        player->loco_timer = LOCO_RUN_HOLD_FRAMES;
    }
    if (player->running) {
        player->speed += player->accel * frames;
        if (player->speed >= PLAYER_RUN_SPEED) { player->speed = PLAYER_RUN_SPEED; player->accel = 0.0f; }
    }
    return stick_yaw;
}

void player_update(Player *player, FollowCamera *camera, const PlayerInput *input, float dt) {
    float magnitude;
    float follow;
    float ex, ey, ez;

    if (input->reset) {
        player_reset(player, camera);
        return;
    }
    if (player->action_timer > 0.0f) player->action_timer -= dt;

    player->locked_on = input->lock_on;
    player->weapon_wheel = input->weapon_wheel;
    if (input->style >= 0 && input->style < STYLE_COUNT) {
        player->style = (Style)input->style;
        set_action(player, ACTION_STYLE_SWITCH);
    }
    if (input->gun) set_action(player, ACTION_GUN);
    /* Swordmaster's circle moves are handled by combat; other styles still placeholder. */
    /* Gunslinger's circle needs the guns (not in yet); the other styles are in combat.c. */
    if (input->style_action && player->style == STYLE_GUNSLINGER) set_action(player, ACTION_STYLE);

    /* SELECT: swing the camera behind Dante's current facing. */
    if (input->snap_camera) {
        camera->target_yaw = player->yaw;
        camera->snapping = 1;
        set_action(player, ACTION_CAMERA_SNAP);
    }
    /* DMC3-style auto follow: drift behind the running direction (not while
     * locked on or attacking, and never while a SELECT snap is running). */
    if (camera_auto_follow && !camera->snapping && !player->locked_on && !player->attacking &&
        player->speed > 1.0f && player->on_ground) {
        float delta = wrap_angle(player->yaw - camera->yaw);
        /* Running toward the camera: leave it, so Dante keeps coming at the
         * lens instead of the camera circling around behind him. */
        if (fabsf(delta) < CAMERA_FOLLOW_MAX_ANGLE) {
            float rate = CAMERA_FOLLOW_RATE * (player->speed / PLAYER_RUN_SPEED) * dt;
            camera->yaw = wrap_angle(camera->yaw + clampf(delta, -rate, rate));
        }
    }
    if (camera->snapping) {
        float delta = wrap_angle(camera->target_yaw - camera->yaw);
        camera->yaw = wrap_angle(camera->yaw + delta * (1.0f - expf(-CAMERA_SNAP_RATE * dt)));
        if (fabsf(delta) < 0.002f) { camera->yaw = camera->target_yaw; camera->snapping = 0; }
    }

    magnitude = sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y);
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude * STICK_TILT_SCALE < STICK_TILT_NEUTRAL) magnitude = 0.0f;
    /* The stick is read against the camera as it is now (FUN_00254670), so it
     * always means the same direction on screen, even while the camera turns. */
    player->camera_yaw = camera->yaw;
    if (player->attacking) magnitude = 0.0f;  /* root motion drives Dante instead */
    if (magnitude > 0.0f) {
        /* Camera-relative: stick up runs away from the camera. Right-handed
         * view (gumLookAt), so right = forward x up. */
        float forward_x = sinf(player->camera_yaw), forward_z = cosf(player->camera_yaw);
        float right_x = -cosf(player->camera_yaw), right_z = sinf(player->camera_yaw);
        float move_x = forward_x * input->stick_y + right_x * input->stick_x;
        float move_z = forward_z * input->stick_y + right_z * input->stick_x;
        float limit = player_room_limit;
        float move_yaw = locomotion_step(player, atan2f(move_x, move_z), magnitude, dt);
        player->x = clampf(player->x + sinf(move_yaw) * player->speed * dt, -limit, limit);
        player->z = clampf(player->z + cosf(move_yaw) * player->speed * dt, -limit, limit);
        if (player->on_ground) {
            player->run_phase += player->speed * dt / STRIDE_LENGTH * TWO_PI_F;
            if (player->run_phase > TWO_PI_F) player->run_phase -= TWO_PI_F;
        }
    } else {
        player->loco = LOCO_IDLE;
        player->running = 0;
        player->speed = 0.0f;
    }

    if (input->jump && player->on_ground && !player->attacking &&
        (player->jump_motion < 0 || player->jump_frame >= JUMP_LAND_FRAME)) {
        if (player->locked_on && magnitude > 0.0f) {
            /* lock-on + direction + cross: combat.c rolls / backflips */
        } else {
            /* Standing or running jump: the MOT's root Y drives the arc. */
            int m = motion_find(player->speed > 50.0f ? "2-05" : "2-04");
            if (m >= 0) {
                player->jump_motion = m;
                player->jump_frame = 0.0f;
                player->jump_base_y = player->y;
            } else {
                player->vy = PLAYER_JUMP_SPEED;
            }
            player->on_ground = 0;
            set_action(player, ACTION_JUMP);
        }
    }
    /* Air moves (Air Hike, Sky Star, aerial attacks) take over from the jump arc. */
    if (player->attacking && player->jump_motion >= 0 && !player->on_ground) player->jump_motion = -1;
    {
        float ground;
        int has_ground;
        if (player->jump_motion >= 0) {
            const Motion *jm = motion_get(player->jump_motion);
            player->jump_frame += dt * MOTION_FPS;
            if (!player->on_ground) {
                float root[3], prev_y = player->y;
                motion_root(jm, player->jump_frame < JUMP_LAND_FRAME ? player->jump_frame : JUMP_LAND_FRAME, root);
                player->y = player->jump_base_y + root[1];
                player->vy = (player->y - prev_y) / (dt > 0.0f ? dt : 1.0f / 60.0f);
                has_ground = floor_at(player->x, player->y, player->z, &ground);
                /* Only on the way down (apex at frame 27): the crouch frames 0-7 sit at root Y 0. */
                if (player->jump_frame > 27.0f && has_ground && player->y <= ground) {
                    /* Touchdown (possibly on something higher): landing frames. */
                    player->y = ground;
                    player->vy = 0.0f;
                    player->on_ground = 1;
                    player->air_hiked = 0;
                    if (player->jump_frame < JUMP_LAND_FRAME) player->jump_frame = JUMP_LAND_FRAME;
                } else if (player->jump_frame >= JUMP_LAND_FRAME) {
                    /* Arc finished in mid-air (jumped off a ledge): free fall. */
                    player->jump_motion = -1;
                }
            }
            if (player->on_ground && (player->jump_frame >= JUMP_END_FRAME || player->speed > 1.0f))
                player->jump_motion = -1;  /* landing done, or cancelled by moving */
        }
        has_ground = floor_at(player->x, player->y, player->z, &ground);
        if (player->jump_motion >= 0 && !player->on_ground) {
            /* the motion owns the height this frame */
        } else if (player->on_ground) {
            /* Follow the floor (curbs, slopes); walk off an edge -> fall. */
            if (has_ground && ground >= player->y - STEP_HEIGHT) player->y = ground;
            else { player->on_ground = 0; player->vy = 0.0f; }
        }
        if (player->jump_motion >= 0 && !player->on_ground) {
            /* height comes from the jump motion: no gravity this frame */
        } else if (!player->on_ground && player->attacking && !player->free_fall) {
            player->vy = 0.0f;  /* aerial attacks hang in the air, as in DMC */
        } else if (!player->on_ground) {
            player->vy -= PLAYER_GRAVITY * dt;
            player->y += player->vy * dt;
            if (has_ground && player->y <= ground && player->vy <= 0.0f) {
                player->y = ground; player->vy = 0.0f; player->on_ground = 1; player->air_hiked = 0;
                /* Every landing uses the jump motion's landing frames. */
                if (!player->attacking) {
                    int m = motion_find("2-04");
                    if (m >= 0) { player->jump_motion = m; player->jump_frame = JUMP_LAND_FRAME; }
                }
            }
            if (player->y < -5000.0f) player_reset(player, camera);  /* fell out of the world */
        }
    }

    /* Frame-rate independent exponential follow of position. */
    follow = 1.0f - expf(-8.0f * dt);
    camera_goal(player, camera, &ex, &ey, &ez);
    camera->x += (ex - camera->x) * follow;
    camera->y += (ey - camera->y) * follow;
    camera->z += (ez - camera->z) * follow;
    camera->tx += (player->x - camera->tx) * follow;
    camera->ty += (player->y + CAMERA_TARGET_HEIGHT - camera->ty) * follow;
    camera->tz += (player->z - camera->tz) * follow;
}
