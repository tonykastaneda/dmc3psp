#include "player.h"

#include <math.h>

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318531f

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

void player_reset(Player *player, FollowCamera *camera) {
    player->x = 0.0f;
    player->z = -300.0f;
    player->yaw = 0.0f;
    player->speed = 0.0f;
    camera->yaw = 0.0f;
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

    camera->yaw = wrap_angle(camera->yaw + (float)input->orbit * CAMERA_ORBIT_RATE * dt);

    magnitude = sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y);
    if (magnitude > 1.0f) magnitude = 1.0f;
    player->speed = 0.0f;
    if (magnitude > 0.0f) {
        /* Camera-relative: stick up runs away from the camera. */
        float forward_x = sinf(camera->yaw);
        float forward_z = cosf(camera->yaw);
        /* Right-handed view (gumLookAt): right = forward x up. */
        float right_x = -cosf(camera->yaw);
        float right_z = sinf(camera->yaw);
        float move_x = forward_x * input->stick_y + right_x * input->stick_x;
        float move_z = forward_z * input->stick_y + right_z * input->stick_x;
        float length = sqrtf(move_x * move_x + move_z * move_z);
        float target_yaw;
        float delta;
        float max_turn = PLAYER_TURN_RATE * dt;
        float limit = ROOM_HALF_SIZE - PLAYER_RADIUS;

        move_x /= length;
        move_z /= length;
        target_yaw = atan2f(move_x, move_z);
        delta = wrap_angle(target_yaw - player->yaw);
        player->yaw = wrap_angle(player->yaw + clampf(delta, -max_turn, max_turn));

        player->speed = PLAYER_RUN_SPEED * magnitude;
        player->x = clampf(player->x + move_x * player->speed * dt, -limit, limit);
        player->z = clampf(player->z + move_z * player->speed * dt, -limit, limit);
    }

    /* Critically damped-ish follow: frame-rate independent exponential smoothing. */
    follow = 1.0f - expf(-8.0f * dt);
    camera_goal(player, camera, &ex, &ey, &ez);
    camera->x += (ex - camera->x) * follow;
    camera->y += (ey - camera->y) * follow;
    camera->z += (ez - camera->z) * follow;
    camera->tx += (player->x - camera->tx) * follow;
    camera->ty += (CAMERA_TARGET_HEIGHT - camera->ty) * follow;
    camera->tz += (player->z - camera->tz) * follow;
}
