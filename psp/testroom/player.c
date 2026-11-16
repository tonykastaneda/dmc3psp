#include "player.h"
#include "camera.h"

#include "stage.h"

#include <math.h>
#include <string.h>

#include "motion.h"

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318531f
#define STRIDE_LENGTH 165.0f /* cm covered per full run cycle */
#define STEP_HEIGHT 45.0f    /* curbs and steps Dante walks up without jumping */
#define LEDGE_DROP 30.0f     /* FUN_0021f470: FUN_00242200(-20) lands within 10 cm, so floor up to 30 cm down holds him */

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

/* Blue Orbs add 1000 HP; Purple Orbs add one rune. Purchases refill their gauge. */
void player_set_capacity(Player *p, int blue, int purple) {
    float hp, dt;
    if (blue < 0) blue = 0;
    if (blue > 10) blue = 10;
    if (purple < 0) purple = 0;
    if (purple > 7) purple = 7;
    hp = PLAYER_HEALTH_BASE + 1000.0f * blue;
    dt = 3.0f + purple;
    if (hp > p->health_max) p->health = hp;
    if (dt > p->dt_max) p->dt_energy = dt;
    p->health_max = hp; p->dt_max = dt;
    if (p->health > hp) p->health = hp;
    if (p->dt_energy > dt) p->dt_energy = dt;
}

void player_gain_dt(Player *p, float runes) {
    if (p->devil || p->quicksilver || p->doppelganger || runes <= 0) return;
    p->dt_energy = fminf(p->dt_max, p->dt_energy + runes);
}

void player_reset(Player *player, FollowCamera *camera) {
    int melee = player->melee_weapon, gun = player->gun_weapon;
    memset(player, 0, sizeof(*player));
    player->health = PLAYER_HEALTH_BASE;
    player->health_max = PLAYER_HEALTH_BASE;
    player->dt_energy = player->dt_max = 3.0f;
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
/* Dante's table at 0x5a85c0, row * 0xb4 + column * 0x24: 0 jump, 1 jump held, 2 Air Hike,
 * 3 Air Hike held, 6 Enemy Step, 7 Enemy Step held. Column 4 is the stick at rest: no
 * carry and twice the drift. The carry's acceleration is zero in all of these rows. */
static const struct { float speed, vy, g, late; } air_rows[11] = {
    [0] = {5.5f, 35.5f, -2.5f, -1.5f},
    [1] = {7.5f, 54.75f, -4.75f, -1.5f},
    [2] = {5.5f, 28.0f, -1.5f, 0.0f},
    [3] = {7.5f, 34.0f, -1.5f, 0.0f},
    [6] = {5.5f, 21.5f, -1.5f, 0.0f},
    [7] = {7.5f, 27.5f, -1.5f, 0.0f},
    [10] = {12.5f, 18.0f, -1.3f, 0.0f},
};

float player_step_scale = 1.0f;
float player_stick_yaw_lock = 1000.0f;   /* radians, or >= 100: unlocked */

/* FUN_002e2460 */
static void air_arc(Player *player, int row) {
    player->air_row = row;
    player->air_speed = player->air_col == 4 ? 0.0f : air_rows[row].speed;
    player->air_drift = player->air_col == 4 ? 4.25f : 2.125f;
    player->air_vy = air_rows[row].vy;
    player->air_g = air_rows[row].g;
}

/* Air Hike and Enemy Step start the same way: the stick picks the direction, or none. */
static void air_kick(Player *player, int kind, const float stick[2]) {
    int pushed = stick[0] != 0.0f || stick[1] != 0.0f;
    int m = motion_find("2-17");   /* 0:17 Air Hike, 0:33 Enemy Step: both 2-17 from its start (2-33 isn't a motion) */
    player->air_kind = kind;
    player->air_col = pushed ? 0 : 4;
    if (pushed) player->yaw = player->air_yaw = atan2f(stick[0], stick[1]);
    else player->air_yaw = player->yaw;
    player->air_hold = (float)AIR_HIKE_HOLD_FRAMES;
    player->air_accum = 0.0f;
    player->air_stage = 0;
    player->air_vy = player->air_g = player->air_speed = 0.0f;
    player->vy = 0.0f;
    player->jump_time = JUMP_CROUCH_FRAMES;
    player->jump_motion = m;
    player->jump_frame = 0.0f;
    player->hike_fx = kind == AIR_STEP ? 2 : 1;
}

/* Bank 0's indices past 0x1d aren't motions but scripts replaying the base ones from a frame (op 1;
 * analysis/moveset/motion_scripts.json): 0:36 / 0:37, the falls, 2-04 / 2-05 from 50; 0:38 / 0:39, Air Hike's rise,
 * 2-04 / 2-05 from 7; 0:40, the wall kick, 2-09 from 7; 0:31 / 0:32, the landings, 2-04 / 2-05 from 52. */
static void air_script_motion(Player *player, const char *name, float frame) {
    int m = motion_find(name);
    if (m < 0) m = motion_find("2-16");
    if (m < 0) return;
    player->jump_motion = m;
    player->jump_frame = frame;
}

/* A velocity-driven move whose motion ends in the air (Rain Storm's 18:17 / 18:18: FUN_0021df00(vy, -1.5, p, 8)):
 * forward speed stopped, the move's vertical speed kept, 1.5 cm/frame^2, 0:36 playing (2-04 from 50), no stick
 * drift (state 0xc), then the jump's landing (0:32) with its stop. */
void player_fall_from_move(Player *player, float vy, float g, float speed) {
    player->air_kind = AIR_NONE;
    player->air_col = 0;
    player->air_yaw = player->yaw;
    player->air_speed = speed;
    player->air_drift = 0.0f;
    player->air_vy = vy;
    player->air_g = g;
    player->air_hold = 0.0f;
    player->air_accum = 0.0f;
    player->jump_time = JUMP_CROUCH_FRAMES;
    player->vy = 0.0f;
    player->speed = 0.0f;
    air_script_motion(player, "2-04", 50.0f);   /* 0:36 */
}

/* Wall Hike's kick off the wall (FUN_00227fb0 step 3): row 10, on the arc at once. */
void player_wall_kick(Player *player, float away_yaw) {
    player->air_kind = AIR_HIKE;
    player->air_col = 0;
    player->air_hold = 0.0f;
    player->air_yaw = away_yaw;
    player->air_accum = 0.0f;
    player->jump_time = JUMP_CROUCH_FRAMES;
    air_arc(player, 10);
    player->vy = 0.0f;
    player->on_ground = 0;
    air_script_motion(player, "2-09", 7.0f);   /* 0:40 */
}

/* One 60 fps frame of the air states. */
static void air_step(Player *player, const float stick[2], int held) {
    float limit = player_room_limit, k = player_step_scale;
    if (player->air_kind == AIR_JUMP) {
        if (player->jump_time < JUMP_CROUCH_FRAMES) {
            /* FUN_002222c0: the press frame only sets the state up (step 0); from the next one he turns to
             * the stick's direction over 5 frames (FUN_002e3010, a fifth of the turn a frame) while the
             * crouch plays, until the motion's flag (+0x24d5) starts the arc. */
            float left = 6.0f - player->jump_time;
            if (player->jump_time >= 1.0f && left > 0.0f && player->air_col != 2)
                player->yaw = wrap_angle(player->yaw + wrap_angle(player->air_yaw - player->yaw) / left);
            player->jump_time += k;
            if (player->jump_time >= JUMP_CROUCH_FRAMES) {
                air_arc(player, 0);
                player->air_timer = 9.0f;
                player->air_stage = 0;
            }
            return;
        }
        /* FUN_002224a0: holding the button past the takeoff jumps higher. A release in its
         * first 5 frames cuts it short. After that the fall gets gentler, then the carry fades. */
        switch (player->air_stage) {
        case 0:
            if (held) { air_arc(player, 1); player->air_timer = 5.0f; }
            player->air_stage = 1;
            break;
        case 1:
            player->air_timer -= k;
            if (player->air_timer <= 0.0f) {
                if (!held) player->air_g = -10.0f;
                player->air_stage = 2;
            }
            break;
        case 2:
            if (player->air_vy < 18.5f) {
                player->air_g = -1.5f;
                player->air_timer = 14.0f;
                player->air_stage = 3;
            }
            break;
        case 3:
            player->air_timer -= k;
            if (player->air_timer < 0.0f) {
                player->air_g = player->air_row == 0 && player->air_col == 0 ? -1.1f : air_rows[player->air_row].late;
                player->air_stage = 4;
            }
            break;
        default:
            player->air_speed *= 0.95f;
            break;
        }
    } else if (player->air_hold > 0.0f) {
        /* FUN_00222700, FUN_00228950: he hangs where he was until the kick's motion ends. */
        player->air_hold -= k;
        if (player->air_hold <= 0.0f) {
            player->air_hold = 0.0f;
            int first = player->air_kind == AIR_STEP ? 6 : 2;
            air_arc(player, held ? first + 1 : first);
            air_script_motion(player, player->air_col == 4 ? "2-04" : "2-05", 7.0f);   /* 0:38 / 0:39 */
        }
        return;
    }
    /* FUN_002e2710 and FUN_002e2560. */
    player->x = clampf(player->x + (sinf(player->air_yaw) * player->air_speed + stick[0] * player->air_drift) * k, -limit, limit);
    player->z = clampf(player->z + (cosf(player->air_yaw) * player->air_speed + stick[1] * player->air_drift) * k, -limit, limit);
    player->y += player->air_vy * k;
    player->air_vy += player->air_g * k;
    if (player->air_vy < -70.0f) player->air_vy = -70.0f;
}

/* Rising into something overhead (a staircase's underside, a balcony): the PS2's body capsule meets it and he
 * drops from there. The port had no ceiling at all, and a jump under M4's spiral stair went up through the steps. */
#define HEAD_HEIGHT 185.0f
static void head_check(Player *player, float before) {
    float from[3], to[3], t;
    if (player->y <= before || !stage_loaded()) return;
    from[0] = to[0] = player->x; from[2] = to[2] = player->z;
    from[1] = before + 100.0f;
    to[1] = player->y + HEAD_HEIGHT;
    if (stage_raycast(from, to, &t)) {
        float top = from[1] + (to[1] - from[1]) * t - HEAD_HEIGHT;
        player->y = top > before ? top : before;
        if (player->air_vy > 0.0f) player->air_vy = 0.0f;
    }
}

static void turn_toward(Player *player, float yaw, float max_turn) {
    player->yaw = wrap_angle(player->yaw + clampf(wrap_angle(yaw - player->yaw), -max_turn, max_turn));
}

/* Air Raid: Nevan's Devil Trigger flight. The player states 0x25-0x28 (FUN_0022a840 take-off, FUN_0022a890
 * hovering, FUN_0022adf0 lock-on flight, FUN_0022af70 the end), entered from FUN_0021d280 with motion 6:52.
 * There's no gravity and no climb: he stays at the height he took off at, kept at least 5 cm over the floor below,
 * rising 1 cm a frame where it comes up under him (FUN_0022aae0). The stick moves him as on the ground, from the
 * same movement table (the walk, then the run once the stick has been held full), facing turning toward it;
 * motions 6:53 hovering, 6:57 flying. The flag that keeps him flying (+0x28a9) is set by code not decoded: in the
 * port a jump in the air with Nevan in Devil Trigger starts it (the walkthrough's "Devil Trigger + Jump"), and a
 * second jump, or Devil Trigger ending, stops it with 6:59 and he falls. Its attacks (actions 95 and 96) aren't
 * in: an attack in flight is the ordinary air one. */
static void raid_motion(Player *player, const char *name) {
    int m = motion_find(name);
    if (m >= 0 && m != player->jump_motion) { player->jump_motion = m; player->jump_frame = 0.0f; }
}

static void raid_start(Player *player) {
    player->air_kind = AIR_RAID;
    player->air_timer = LOCO_RUN_HOLD_FRAMES;
    player->running = 0;
    player->speed = 0.0f;
    player->vy = 0.0f;
    player->on_ground = 0;
    raid_motion(player, "s26-52");
}

static void raid_end(Player *player) {
    player->air_kind = AIR_NONE;
    player->jump_motion = -1;
    player->vy = 0.0f;
    player->speed = 0.0f;
    player->running = 0;
}

static void raid_step(Player *player, const float stick[2], float magnitude, float dt) {
    float frames = dt * MOTION_FPS, ground, limit = player_room_limit;
    const Motion *m;
    if (magnitude > 0.0f) {
        float yaw = atan2f(stick[0], stick[1]);
        int full = magnitude * STICK_TILT_SCALE >= STICK_TILT_RUN;
        turn_toward(player, yaw, PLAYER_TURN_RATE * dt);
        if (!player->running) {
            if (!full) player->air_timer = LOCO_RUN_HOLD_FRAMES;
            else if ((player->air_timer -= frames) <= 0.0f) player->running = 1;
        } else if (!full) {
            player->running = 0;
            player->air_timer = LOCO_RUN_HOLD_FRAMES;
        }
        player->speed = player->running ? PLAYER_DT_RUN_SPEED : PLAYER_WALK_SPEED;   /* always in Devil Trigger: row 1 */
        player->x = clampf(player->x + sinf(yaw) * player->speed * dt, -limit, limit);
        player->z = clampf(player->z + cosf(yaw) * player->speed * dt, -limit, limit);
        if (player->jump_motion != motion_find("s26-52") || player->jump_frame > 20.0f) raid_motion(player, "s26-57");
    } else {
        player->running = 0;
        player->speed = 0.0f;
        player->air_timer = LOCO_RUN_HOLD_FRAMES;
        if (player->jump_motion != motion_find("s26-52") || player->jump_frame > 20.0f) raid_motion(player, "s26-53");
    }
    if (floor_at(player->x, player->y + 60.0f, player->z, &ground)) {
        if (player->y - 5.0f < ground) player->y += frames;
        if (player->y < ground) player->y = ground;
    }
    player->vy = 0.0f;
    player->on_ground = 0;
    player->jump_frame += frames;
    m = player->jump_motion >= 0 ? motion_get(player->jump_motion) : 0;
    if (m && player->jump_frame >= m->end_frame) player->jump_frame -= m->end_frame;
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
        player->speed = 4.25f * MOTION_FPS * magnitude;  /* column 4 of the air table */
        player->lean = 0.0f;
        return stick_yaw;
    }
    if (player->loco != LOCO_MOVE) player->lean = 0.0f;   /* until the move state (FUN_00221ba0, FUN_0021e3c0 clear it) */
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
    {
        /* FUN_00221d20: he leans into the turn still to make, 10 degrees once it's 5 or more (none under 1),
         * the lean itself moving 0.25 degrees a frame. */
        float d = wrap_angle(stick_yaw - player->yaw), ad = fabsf(d), goal = 0.0f, rate = 0.004363323f * frames;
        if (ad >= 0.0174533f) goal = (d > 0.0f ? -1.0f : 1.0f) * fminf(ad, 0.0872665f) / 0.0872665f * 0.1745329f;
        if (fabsf(player->lean - goal) < rate) player->lean = goal;
        else player->lean += player->lean < goal ? rate : -rate;
    }
    turn_toward(player, stick_yaw, PLAYER_TURN_RATE * dt);
    if (!player->running) {
        if (full_tilt) {
            player->loco_timer -= frames;
            if (player->loco_timer <= 0.0f) {
                /* All at once: the walk, then the run, on the HD Collection's traces. */
                player->running = 1;
                player->speed = player->devil ? PLAYER_DT_RUN_SPEED : PLAYER_RUN_SPEED;
                player->accel = 0.0f;
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
        float run = player->devil ? PLAYER_DT_RUN_SPEED : PLAYER_RUN_SPEED;
        if (player->speed >= run) { player->speed = run; player->accel = 0.0f; }
    }
    return stick_yaw;
}

/* FUN_0021fe30 / FUN_0021f470, once the frame's moves and wall pushes are done: on the ground, with no floor within
 * 30 cm below him, he falls (state 9 from FUN_0021df00: no speed, 1.5 cm/frame^2, the stick's drift). */
void player_settle(Player *player, int walls) {
    float ground, above;
    int near;
    if (!player->on_ground) return;
    /* FUN_00242200 looks for the floor from 130 cm over his feet. The port's floor test reaches 45 up, enough for a
     * walk; a dash up a steep slope (M18 room 400's 58 degree bank, 34-60 cm a frame) outran it, left the slope
     * over him, found nothing below and dropped him through the hill and out of the level. So before a fall the
     * game's own reach is tried: a surface up to 130 cm over his feet takes him. */
    near = floor_at(player->x, player->y, player->z, &ground) && ground >= player->y - LEDGE_DROP;
    if (!near && floor_at(player->x, player->y + 130.0f - STEP_HEIGHT, player->z, &above) && above > player->y) {
        ground = above;
        near = 1;
    }
    if (near) {
        /* Up or down a slope the walls meet him at another height: push out again there. */
        if (walls && fabsf(ground - player->y) > 1.0f) stage_collide(&player->x, &player->z, ground, PLAYER_RADIUS);
        player->y = ground;
    } else {
        player->on_ground = 0;
        player->vy = 0.0f;
        if (!player->attacking) {
            /* State 9 as FUN_0021df00 sets it up, run by the air steps (one a 60th, the landing check first). */
            player->air_kind = AIR_NONE;
            player->air_col = 4;
            player->air_yaw = player->yaw;
            player->air_speed = 0.0f;
            player->air_drift = 4.25f;
            player->air_vy = 0.0f;
            player->air_g = -1.5f;
            player->air_hold = 0.0f;
            player->air_accum = 0.0f;
            player->jump_time = JUMP_CROUCH_FRAMES;
            air_script_motion(player, "2-04", 50.0f);   /* 0:36 */
            player->speed = 0.0f;
        }
    }
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
    if (input->devil && (player->devil || player->dt_energy >= 3.0f)) player->devil = !player->devil;
    if (player->devil || player->quicksilver || player->doppelganger) {
        player->dt_energy -= dt; /* one rune per second; tuning shared by all DT consumers */
        if (player->dt_energy <= 0.0f) {
            player->dt_energy = 0.0f;
            player->devil = player->quicksilver = player->doppelganger = 0;
        }
    }
    if (input->next_melee) player->melee_weapon = (player->melee_weapon + 1) % 5;
    if (input->next_gun) player->gun_weapon = 5 + (player->gun_weapon - 5 + 1) % 5;

    /* SELECT: swing the camera behind Dante's current facing. */
    if (input->snap_camera) {
        camera->target_yaw = player->yaw;
        camera->snapping = 1;
        set_action(player, ACTION_CAMERA_SNAP);
    }
    /* The turn itself is the game's camera reset (camera.c: behind him in 6 frames). */

    magnitude = sqrtf(input->stick_x * input->stick_x + input->stick_y * input->stick_y);
    if (magnitude > 1.0f) magnitude = 1.0f;
    if (magnitude * STICK_TILT_SCALE < STICK_TILT_NEUTRAL) magnitude = 0.0f;
    /* The stick is read against the camera as it is now (FUN_00254670), so it
     * always means the same direction on screen, even while the camera turns. */
    player->camera_yaw = camera_stick_yaw(camera, input->stick_x, input->stick_y, magnitude > 0.0f);
    /* A comparison test with the real game's fixed zone camera reads the stick against that, not the follow
     * camera (main.c "camlock"). */
    if (player_stick_yaw_lock < 100.0f) player->camera_yaw = player_stick_yaw_lock;
    wants_flip = player->locked_on && magnitude > 0.0f;
    stick_world[0] = stick_world[1] = 0.0f;
    if (magnitude > 0.0f) {
        stick_world[0] = sinf(player->camera_yaw) * input->stick_y - cosf(player->camera_yaw) * input->stick_x;
        stick_world[1] = cosf(player->camera_yaw) * input->stick_y + sinf(player->camera_yaw) * input->stick_x;
    }
    if (player->air_kind == AIR_RAID) {
        if (!player->devil || input->jump) raid_end(player);
        else if (!player->attacking) raid_step(player, stick_world, magnitude, dt);
        goto camera;
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
        player->lean = 0.0f;
    }

    if (input->jump && player->on_ground && !player->attacking &&
        (player->jump_motion < 0 || player->jump_frame >= JUMP_LAND_FRAME)) {
        if (wants_flip && !player->flip_request) {
            /* lock-on + left / right + cross: combat.c rolls */
        } else {
            int pushed = stick_world[0] != 0.0f || stick_world[1] != 0.0f;
            int m = motion_find(player->flip_request < 0 ? "2-09" : player->flip_request > 0 || pushed ? "2-05" : "2-04");
            /* Column 0 with the stick (forward), 2 for a back flip, 4 at rest. */
            player->air_col = player->flip_request < 0 ? 2 : player->flip_request > 0 || pushed ? 0 : 4;
            if (player->flip_request) player->air_yaw = player->flip_request < 0 ? wrap_angle(player->yaw + 3.14159265f) : player->yaw;
            else player->air_yaw = pushed ? atan2f(stick_world[0], stick_world[1]) : player->yaw;
            player->flip_request = 0;
            player->air_kind = AIR_JUMP;
            player->air_accum = 0.0f;
            player->air_vy = player->air_g = player->air_speed = 0.0f;
            player->jump_motion = m;
            player->jump_frame = 0.0f;
            player->jump_time = 0.0f;
            player->jump_base_y = player->y;
            player->speed = 0.0f;
            player->on_ground = 0;
            set_action(player, ACTION_JUMP);
        }
    } else if (input->jump && !player->on_ground && !player->attacking &&
               (player->air_kind != AIR_JUMP || player->jump_time >= JUMP_CROUCH_FRAMES)) {
        /* FUN_00242c60 comes first: an enemy close by gives an Enemy Step, which refills
         * everything (FUN_0021e1a0 mode 4). Otherwise the one Air Hike. */
        if (player->enemy_close && player->step_cooldown <= 0.0f) {
            air_kick(player, AIR_STEP, stick_world);
            player->step_cooldown = ENEMY_STEP_COOLDOWN;
            player->air_hikes = 1;
            player->air_stars = 1;
        } else if (player->devil && player->melee_weapon == 3) {
            raid_start(player);   /* Air Raid, above */
            goto camera;
        } else if (player->air_hikes > 0 && player->melee_weapon % 2 == 0) {
            /* Rebellion, Agni & Rudra and Beowulf have it; Cerberus and Nevan don't (FUN_00222700,
             * and Nevan checked on the HD Collection). */
            air_kick(player, AIR_HIKE, stick_world);
            --player->air_hikes;
        }
    }
    if (player->step_cooldown > 0.0f) player->step_cooldown -= dt * MOTION_FPS;
    if (player->on_ground) {
        player->air_hikes = 1;
        player->air_stars = 1;
    }
    /* Air moves (Air Hike, Sky Star, aerial attacks) take over from the jump arc. */
    /* A move that keeps physics (Royalguard's air block, 24:5) leaves the jump's own arc running: on the PS2 the
     * fall under it is the jump's, 30 frames in the air in all, not a slower generic one. */
    if (player->attacking && !player->free_fall && player->jump_motion >= 0 && !player->on_ground) {
        player->jump_motion = -1;
        player->air_kind = AIR_NONE;
    }
    {
        float ground;
        int has_ground;
        if (player->jump_motion >= 0) {
            player->jump_frame += dt * MOTION_FPS;
            if (!player->on_ground) {
                float prev_y = player->y;
                const Motion *held_motion = motion_get(player->jump_motion);
                /* The pose holds before the landing frames however long the fall is. */
                /* The jump motions (2-04, 2-05, 2-09) carry their landing from frame 52 on. */
                if (player->air_kind == AIR_JUMP || held_motion->end_frame > JUMP_LAND_FRAME) {
                    if (player->jump_frame > JUMP_LAND_FRAME - 1.0f) player->jump_frame = JUMP_LAND_FRAME - 1.0f;
                } else if (player->jump_frame > held_motion->end_frame - 1.0f) {
                    player->jump_frame = held_motion->end_frame - 1.0f;
                }
                /* One real game step every 60th of a second, however long this frame was. */
                player->air_accum += dt * MOTION_FPS / player_step_scale;
                while (player->air_accum >= 1.0f) {
                    float floor_y;
                    player->air_accum -= 1.0f;
                    /* FUN_00242200, before the step in every air state (FUN_002224a0, FUN_00222700, FUN_00228950):
                     * falling, and this step would take him to within 10 cm of the floor, he's put on it and lands
                     * without moving this frame. PS2 (PCSX2 runjump): touchdown with no carry, 6.4 cm sooner. */
                    if (player->air_vy < 0.0f && floor_at(player->x, player->y, player->z, &floor_y) &&
                        player->y + player->air_vy * player_step_scale <= floor_y + 10.0f) {
                        player->y = floor_y;
                        player->air_accum = 0.0f;
                        break;
                    }
                    {
                        float before = player->y;
                        air_step(player, stick_world, input->jump_held);
                        head_check(player, before);
                    }
                }
                player->vy = (player->y - prev_y) / (dt > 0.0f ? dt : 1.0f / 60.0f);
                /* From where he started this frame: at 30 FPS a fast fall can pass well below the floor in one. */
                has_ground = floor_at(player->x, prev_y, player->z, &ground);
                if (player->vy < 0.0f && has_ground && player->y <= ground) {
                    /* Touchdown (possibly on something higher): landing frames. */
                    player->y = ground;
                    player->vy = 0.0f;
                    player->on_ground = 1;
                    player->air_kind = AIR_NONE;
                    player->land_stop = JUMP_LAND_STOP_FRAMES;
                    player->speed = 0.0f;
                    /* FUN_0021ddb0: 0:31 (2-04 from 52) after a takeoff with the stick at rest (column 4), else
                     * 0:32 (2-05 from 52); PCSX2: 001f after standing jumps and Air Hikes, 0020 after running jumps
                     * and flips. */
                    player->jump_motion = motion_find(player->air_col == 4 ? "2-04" : "2-05");
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
            /* Follow the floor (curbs, slopes). Whether he's walked off an edge is decided after the walls have
             * pushed him back (player_settle): a run along a wall at a drop pokes past it here. */
            if (has_ground && ground >= player->y - LEDGE_DROP) player->y = ground;
        }
        if (player->jump_motion >= 0 && !player->on_ground) {
            /* height comes from the jump motion: no gravity this frame */
        } else if (!player->on_ground && player->attacking && !player->free_fall) {
            player->vy = 0.0f;  /* aerial attacks hang in the air, as in DMC */
        } else if (!player->on_ground) {
            player->vy -= PLAYER_GRAVITY * dt;
            player->y += player->vy * dt;
            if (has_ground && player->y <= ground && player->vy <= 0.0f) {
                player->y = ground; player->vy = 0.0f; player->on_ground = 1;
                /* Every landing uses the jump motion's landing frames. */
                if (!player->attacking) {
                    int m = motion_find("2-04");
                    if (m >= 0) { player->jump_motion = m; player->jump_frame = JUMP_LAND_FRAME; }
                }
            }
            if (player->y < -5000.0f) player_reset(player, camera);  /* fell out of the world */
        }
    }

camera:
    /* The game's camera, room by room: camera.c (docs/CAMERA.md). */
    /* One camera step a frame: main.c's Devil Trigger speed-up (dt * 1.1) is Dante's, not the camera's. */
    camera_update(player, camera, input->snap_camera, player->devil ? dt / 1.1f : dt);
}
