#include "player.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318531f
#define STRIDE_LENGTH 165.0f /* cm covered per full run cycle */

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
    *ey = CAMERA_HEIGHT;
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
    player->z = -300.0f;
    player->on_ground = 1;
    player->style = STYLE_TRICKSTER;
    memset(camera, 0, sizeof(*camera));
    camera_goal(player, camera, &camera->x, &camera->y, &camera->z);
    camera->tx = player->x;
    camera->ty = CAMERA_TARGET_HEIGHT;
    camera->tz = player->z;
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
    if (input->style_action && player->style != STYLE_SWORDMASTER) set_action(player, ACTION_STYLE);

    /* SELECT: swing the camera behind Dante's current facing. */
    if (input->snap_camera) {
        camera->target_yaw = player->yaw;
        camera->snapping = 1;
        set_action(player, ACTION_CAMERA_SNAP);
    }
    if (camera->snapping) {
        float delta = wrap_angle(camera->target_yaw - camera->yaw);
        camera->yaw = wrap_angle(camera->yaw + delta * (1.0f - expf(-CAMERA_SNAP_RATE * dt)));
        if (fabsf(delta) < 0.002f) { camera->yaw = camera->target_yaw; camera->snapping = 0; }
    }

    magnitude = sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y);
    if (magnitude > 1.0f) magnitude = 1.0f;
    player->speed = 0.0f;
    player->camera_yaw = camera->yaw;
    if (player->attacking) magnitude = 0.0f;  /* root motion drives Dante instead */
    if (magnitude > 0.0f) {
        /* Camera-relative: stick up runs away from the camera. Right-handed
         * view (gumLookAt), so right = forward x up. */
        float forward_x = sinf(camera->yaw), forward_z = cosf(camera->yaw);
        float right_x = -cosf(camera->yaw), right_z = sinf(camera->yaw);
        float move_x = forward_x * input->stick_y + right_x * input->stick_x;
        float move_z = forward_z * input->stick_y + right_z * input->stick_x;
        float length = sqrtf(move_x * move_x + move_z * move_z);
        float limit = ROOM_HALF_SIZE - PLAYER_RADIUS;
        move_x /= length;
        move_z /= length;
        /* Lock-on holds the facing (strafe); otherwise turn toward travel. */
        if (!player->locked_on) {
            float delta = wrap_angle(atan2f(move_x, move_z) - player->yaw);
            float max_turn = PLAYER_TURN_RATE * dt;
            player->yaw = wrap_angle(player->yaw + clampf(delta, -max_turn, max_turn));
        }
        player->speed = PLAYER_RUN_SPEED * magnitude;
        player->x = clampf(player->x + move_x * player->speed * dt, -limit, limit);
        player->z = clampf(player->z + move_z * player->speed * dt, -limit, limit);
        if (player->on_ground) {
            player->run_phase += player->speed * dt / STRIDE_LENGTH * TWO_PI_F;
            if (player->run_phase > TWO_PI_F) player->run_phase -= TWO_PI_F;
        }
    }

    if (input->jump && player->on_ground && !player->attacking) {
        if (player->locked_on && magnitude > 0.0f) {
            set_action(player, ACTION_ROLL);
        } else {
            player->vy = PLAYER_JUMP_SPEED;
            player->on_ground = 0;
            set_action(player, ACTION_JUMP);
        }
    }
    if (!player->on_ground && player->attacking) {
        player->vy = 0.0f;  /* aerial attacks hang in the air, as in DMC */
    } else if (!player->on_ground) {
        player->vy -= PLAYER_GRAVITY * dt;
        player->y += player->vy * dt;
        if (player->y <= 0.0f) { player->y = 0.0f; player->vy = 0.0f; player->on_ground = 1; }
    }

    /* Frame-rate independent exponential follow of position. */
    follow = 1.0f - expf(-8.0f * dt);
    camera_goal(player, camera, &ex, &ey, &ez);
    camera->x += (ex - camera->x) * follow;
    camera->y += (ey - camera->y) * follow;
    camera->z += (ez - camera->z) * follow;
    camera->tx += (player->x - camera->tx) * follow;
    camera->ty += (CAMERA_TARGET_HEIGHT + player->y * 0.5f - camera->ty) * follow;
    camera->tz += (player->z - camera->tz) * follow;
}
