#include "player.h"

#include "stage.h"

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
    static const char *names[STYLE_COUNT] = {"TRICKSTER", "SWORDMASTER", "GUNSLINGER", "ROYALGUARD", "QUICKSILVER", "DOPPELGANGER"};
    return (unsigned)style < STYLE_COUNT ? names[style] : "?";
}

const char *player_weapon_name(int weapon) {
    static const char *names[10] = {"REBELLION", "CERBERUS", "AGNI&RUDRA", "NEVAN", "BEOWULF",
                                    "EBONY&IVORY", "SHOTGUN", "ARTEMIS", "SPIRAL", "KALINA ANN"};
    return weapon >= 0 && weapon < 10 ? names[weapon] : "?";
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
    int melee = player->melee_weapon, gun = player->gun_weapon;
    memset(player, 0, sizeof(*player));
    player->melee_weapon = melee;
    player->gun_weapon = gun;
    player->jump_motion = -1;
    player->x = player_spawn[0];
    player->y = player_spawn[1];
    player->z = player_spawn[2];
    player->yaw = player_spawn[3];
    player->on_ground = 1;
    player->style = STYLE_TRICKSTER;
    if (player->gun_weapon < 5) player->gun_weapon = 5;
    memset(camera, 0, sizeof(*camera));
    camera->yaw = camera->target_yaw = player->yaw;
    camera_goal(player, camera, &camera->x, &camera->y, &camera->z);
    camera->tx = player->x;
    camera->ty = player->y + CAMERA_TARGET_HEIGHT;
    camera->tz = player->z;
    camera->last_x = player->x;
    camera->last_z = player->z;
    player->camera_yaw = camera->yaw;
}

/* Height above the take-off after t frames (see JUMP_* in player.h). */
static float jump_height(float t) {
    float u = t - JUMP_CROUCH_FRAMES, w;
    if (u <= 0.0f) return 0.0f;
    if (u <= JUMP_RISE_FRAMES) return JUMP_RISE_SPEED * u - JUMP_RISE_LOSS * 0.5f * u * (u - 1.0f);
    w = u - JUMP_RISE_FRAMES;
    return JUMP_RISE_SPEED * JUMP_RISE_FRAMES - JUMP_RISE_LOSS * 0.5f * JUMP_RISE_FRAMES * (JUMP_RISE_FRAMES - 1.0f)
         + JUMP_FALL_SPEED * w - JUMP_FALL_LOSS * 0.5f * w * (w - 1.0f);
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
        /* Falling without a jump: the drift of a standing jump, facing unchanged. */
        player->loco = LOCO_IDLE;
        player->running = 1;
        player->speed = JUMP_DRIFT_SPEED * MOTION_FPS * magnitude;
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
    int wants_flip;
    float stick_world[2];

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
    if (input->devil) player->devil = !player->devil;
    if (input->next_melee) player->melee_weapon = (player->melee_weapon + 1) % 5;
    if (input->next_gun) player->gun_weapon = 5 + (player->gun_weapon - 5 + 1) % 5;

    /* SELECT: swing the camera behind Dante's current facing. */
    if (input->snap_camera) {
        camera->target_yaw = player->yaw;
        camera->snapping = 1;
        set_action(player, ACTION_CAMERA_SNAP);
    }
    /* SELECT's snap swings the eye round him. */
    if (camera->snapping) {
        float delta = wrap_angle(camera->target_yaw - camera->yaw);
        float turn = delta * (1.0f - expf(-CAMERA_SNAP_RATE * dt));
        float dx = camera->x - player->x, dz = camera->z - player->z, c = cosf(turn), s = sinf(turn);
        camera->x = player->x + dx * c + dz * s;
        camera->z = player->z - dx * s + dz * c;
        if (fabsf(delta) < 0.002f) camera->snapping = 0;
    }

    magnitude = sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y);
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude * STICK_TILT_SCALE < STICK_TILT_NEUTRAL) magnitude = 0.0f;
    /* The stick is read against the camera as it is now (FUN_00254670), so it
     * always means the same direction on screen, even while the camera turns. */
    player->camera_yaw = camera->yaw;
    wants_flip = player->locked_on && magnitude > 0.0f;
    stick_world[0] = stick_world[1] = 0.0f;
    if (magnitude > 0.0f) {
        stick_world[0] = sinf(player->camera_yaw) * input->stick_y - cosf(player->camera_yaw) * input->stick_x;
        stick_world[1] = cosf(player->camera_yaw) * input->stick_y + sinf(player->camera_yaw) * input->stick_x;
    }
    if (!player->on_ground && player->jump_motion >= 0) magnitude = 0.0f;  /* the arc moves him: below */
    if (player->attacking || player->gun_stance) magnitude = 0.0f;  /* combat moves Dante instead */
    if (player->land_stop > 0.0f) {
        player->land_stop -= dt * MOTION_FPS;
        magnitude = 0.0f;
    }
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
        if (wants_flip && !player->flip_request) {
            /* lock-on + left / right + cross: combat.c rolls */
        } else {
            int run_jump = player->speed > 50.0f;
            int m = motion_find(player->flip_request < 0 ? "2-09" : player->flip_request > 0 || run_jump ? "2-05" : "2-04");
            player->air_flip = player->flip_request != 0;
            player->air_yaw = player->flip_request < 0 ? wrap_angle(player->yaw + 3.14159265f) : player->yaw;
            player->air_carry = player->air_flip || run_jump ? JUMP_CARRY_SPEED : 0.0f;
            player->flip_request = 0;
            if (m >= 0) {
                player->jump_motion = m;
                player->jump_frame = 0.0f;
                player->jump_time = 0.0f;
                player->jump_base_y = player->y;
                player->speed = 0.0f;
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
            player->jump_frame += dt * MOTION_FPS;
            if (!player->on_ground) {
                float prev_y = player->y;
                player->jump_time += dt * MOTION_FPS;
                /* The pose holds before the landing frames however long the fall is. */
                if (player->jump_frame > JUMP_LAND_FRAME - 1.0f) player->jump_frame = JUMP_LAND_FRAME - 1.0f;
                player->y = player->jump_base_y + jump_height(player->jump_time);
                {
                    /* Measured on the real game: see JUMP_CARRY_* in player.h. */
                    float t = player->jump_time - JUMP_CROUCH_FRAMES, step = dt * MOTION_FPS, limit = player_room_limit;
                    if (t >= 0.0f) {
                        float carry = player->air_carry, stick = carry > 0.0f ? JUMP_STICK_SPEED : JUMP_DRIFT_SPEED;
                        if (!player->air_flip) carry -= JUMP_CARRY_SLOWING * t;
                        if (t > JUMP_CARRY_FADE_FRAME) carry -= JUMP_CARRY_FADE * (t - JUMP_CARRY_FADE_FRAME);
                        if (carry < 0.0f || player->air_carry <= 0.0f) carry = 0.0f;
                        player->x = clampf(player->x + (sinf(player->air_yaw) * carry + stick_world[0] * stick) * step, -limit, limit);
                        player->z = clampf(player->z + (cosf(player->air_yaw) * carry + stick_world[1] * stick) * step, -limit, limit);
                    }
                }
                player->vy = (player->y - prev_y) / (dt > 0.0f ? dt : 1.0f / 60.0f);
                has_ground = floor_at(player->x, player->y, player->z, &ground);
                if (player->vy < 0.0f && has_ground && player->y <= ground) {
                    /* Touchdown (possibly on something higher): landing frames. */
                    player->y = ground;
                    player->vy = 0.0f;
                    player->on_ground = 1;
                    player->air_hiked = 0;
                    player->land_stop = JUMP_LAND_STOP_FRAMES;
                    player->speed = 0.0f;
                    player->jump_frame = JUMP_LAND_FRAME;
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

    {
        /* The real camera: see CAMERA_* in player.h. */
        float frames = dt * MOTION_FPS, dx, dz, h, goal_x, goal_z, rate;
        goal_x = (player->x - camera->last_x) / (frames > 0.0f ? frames : 1.0f) * CAMERA_LEAD_FRAMES;
        goal_z = (player->z - camera->last_z) / (frames > 0.0f ? frames : 1.0f) * CAMERA_LEAD_FRAMES;
        camera->last_x = player->x;
        camera->last_z = player->z;
        if (goal_x * goal_x + goal_z * goal_z > 400.0f * 400.0f) goal_x = goal_z = 0.0f;  /* a warp */
        rate = goal_x * goal_x + goal_z * goal_z > camera->lead_x * camera->lead_x + camera->lead_z * camera->lead_z
             ? CAMERA_LEAD_RISE : CAMERA_LEAD_FALL;
        camera->lead_x += (goal_x - camera->lead_x) * (1.0f - expf(-rate * dt));
        camera->lead_z += (goal_z - camera->lead_z) * (1.0f - expf(-rate * dt));
        camera->tx = player->x + camera->lead_x;
        camera->tz = player->z + camera->lead_z;
        camera->ty += (player->y + CAMERA_TARGET_HEIGHT - camera->ty) * (1.0f - expf(-8.0f * dt));
        dx = camera->x - player->x;
        dz = camera->z - player->z;
        h = sqrtf(dx * dx + dz * dz);
        if (h > 1.0f) {
            float next = h + (CAMERA_DISTANCE - h) * (1.0f - expf(-CAMERA_LEASH_RATE * dt));
            camera->x = player->x + dx / h * next;
            camera->z = player->z + dz / h * next;
        }
        camera->y += (player->y + CAMERA_HEIGHT - camera->y) * (1.0f - expf(-8.0f * dt));
        if (stage_loaded()) {
            /* Pulled in when a wall comes between them, as on the real game near walls. */
            float from[3] = {player->x, player->y + CAMERA_TARGET_HEIGHT, player->z};
            float to[3] = {camera->x, camera->y, camera->z}, t;
            if (stage_raycast(from, to, &t) && t < 1.0f) {
                float reach = sqrtf((to[0] - from[0]) * (to[0] - from[0]) + (to[2] - from[2]) * (to[2] - from[2]));
                t *= 0.92f;
                /* It never comes closer than this on the real game. */
                if (reach * t < CAMERA_MIN_DISTANCE && reach > 0.0f) t = CAMERA_MIN_DISTANCE / reach;
                if (t < 1.0f) {
                    camera->x = from[0] + (to[0] - from[0]) * t;
                    camera->z = from[2] + (to[2] - from[2]) * t;
                }
            }
        }
        camera->yaw = atan2f(camera->tx - camera->x, camera->tz - camera->z);
    }
}
