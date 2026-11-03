#ifndef DMC3PSP_TESTROOM_PLAYER_H
#define DMC3PSP_TESTROOM_PLAYER_H

/* Test-room player controller and follow camera. Pure C, no PSP calls.
 *
 * Control scheme (PSP):
 *   analog stick  move (camera-relative)
 *   d-pad         style switch: up Trickster, down Royalguard,
 *                 left Swordmaster, right Gunslinger
 *   R             lock-on: aims attacks at the target; movement is unchanged
 *   L             weapon wheel (placeholder)
 *   triangle      melee (placeholder)   square  guns (placeholder)
 *   circle        style action (placeholder)
 *   cross         jump (implemented)
 *   SELECT        snap the camera behind Dante
 *   START         motion viewer (d-pad left/right picks a motion)
 */

#define ROOM_HALF_SIZE 1000.0f   /* room spans -1000..1000 on X and Z */
#define PLAYER_RADIUS 40.0f
/* Dante's row of the movement table (0x5a85c0), authored per 60 fps frame:
 * walk 3.5 and run 8.5 cm/frame, turn 0x800 angle units/frame, 8 frame start
 * turn, 6 frame run acceleration, full tilt held 4 frames before running. */
#define PLAYER_WALK_SPEED 210.0f
#define PLAYER_RUN_SPEED 510.0f
#define PLAYER_TURN_RATE 11.781f /* radians per second */
#define LOCO_START_FRAMES 8.0f
#define LOCO_ACCEL_FRAMES 6.0f
#define LOCO_RUN_HOLD_FRAMES 4.0f
/* Stick tilt thresholds (FUN_002e2f20): below 53 neutral, 113 and up runs. */
#define STICK_TILT_SCALE 127.0f
#define STICK_TILT_NEUTRAL 53.0f
#define STICK_TILT_RUN 113.0f
/* Jumps follow the MOT root curve (2-04 standing / 2-05 running: 265 cm apex
 * at frame 27, airborne frames 7-52, landing 52-69). Free falls use the
 * gravity implied by that descent (265 cm in 25 frames at 60 fps). */
#define PLAYER_JUMP_SPEED 1590.0f   /* only for jumps without the motion */
#define PLAYER_GRAVITY 3050.0f
#define JUMP_LAND_FRAME 52.0f
#define JUMP_END_FRAME 69.0f
#define CAMERA_DISTANCE 300.0f
#define CAMERA_HEIGHT 190.0f
#define CAMERA_TARGET_HEIGHT 95.0f
#define CAMERA_SNAP_RATE 10.0f   /* exponential rate of the SELECT snap */

typedef enum { STYLE_TRICKSTER, STYLE_SWORDMASTER, STYLE_GUNSLINGER, STYLE_ROYALGUARD, STYLE_COUNT } Style;

typedef enum {
    ACTION_NONE, ACTION_MELEE, ACTION_GUN, ACTION_STYLE, ACTION_JUMP, ACTION_ROLL,
    ACTION_STYLE_SWITCH, ACTION_CAMERA_SNAP
} Action;

/* Ground movement states: FUN_002217a0 idle, FUN_00221ba0 start, FUN_00221d20 move. */
typedef enum { LOCO_IDLE, LOCO_START, LOCO_MOVE } LocoState;

typedef struct {
    float x, y, z;     /* y is height above the floor */
    float vy;
    int on_ground;
    float yaw;         /* facing; 0 faces +Z, positive turns toward +X */
    float speed;       /* current ground speed */
    float run_phase;   /* radians, advanced by distance covered */
    Style style;
    int locked_on;
    int weapon_wheel;
    int attacking;      /* set by combat: movement and turning are suspended */
    int free_fall;      /* combat move that keeps gravity (Air Hike, Wall Hike) */
    int air_hiked;      /* Air Hike used this airtime */
    int wall_contact;   /* collision pushed Dante out of a wall this frame */
    int has_target;     /* lock-on has something to face (the training dummy) */
    float target_x, target_y, target_z;
    int jump_motion;    /* motion driving the current jump/landing, -1 = none */
    float jump_frame;   /* frame in that motion */
    float jump_base_y;  /* floor height at takeoff */
    float camera_yaw;   /* input reference yaw: the camera's, every frame (combat aims with it) */
    LocoState loco;
    int running;        /* run motion and speed, else walk */
    float loco_timer;   /* frames left: start turn, or full tilt hold before running */
    float start_yaw;    /* direction latched when a start begins */
    float start_turn;   /* turn per frame during a start */
    float accel;        /* speed change per frame while ramping up to a run */
    Action last_action;
    float action_timer; /* seconds the HUD keeps showing last_action */
} Player;

typedef struct {
    float yaw;         /* camera sits behind -forward(yaw) */
    float target_yaw;
    int snapping;
    float x, y, z;     /* smoothed eye position */
    float tx, ty, tz;  /* smoothed look-at target */
} FollowCamera;

typedef struct {
    float stick_x;     /* -1..1, right is positive */
    float stick_y;     /* -1..1, up (away from the camera) is positive */
    int style;         /* Style pressed this frame, or -1 */
    int style_held;    /* circle held (Royalguard block) */
    int melee_held;    /* triangle held (Round Trip charge) */
    int lock_on;       /* R held */
    int weapon_wheel;  /* L held */
    int melee, gun, style_action, jump, snap_camera, reset, toggle_viewer; /* pressed this frame */
} PlayerInput;

/* World hooks (set by main when a stage is loaded). */
extern float player_spawn[4];       /* x, y, z, yaw used by player_reset */
extern float player_room_limit;     /* |x|,|z| clamp; huge on a stage */
/* Floor height under (x, z) no higher than y + step; 0 if none. NULL = flat floor at 0. */
extern int (*player_floor_fn)(float x, float y, float z, float step, float *out);
/* Stage camera zone says "follow" (st001's single CAM zone, type 0x400):
 * the camera swings behind Dante's direction of travel while he runs. */
extern int camera_auto_follow;

void player_reset(Player *player, FollowCamera *camera);
void player_update(Player *player, FollowCamera *camera, const PlayerInput *input, float dt);
const char *player_style_name(Style style);
const char *player_action_name(Action action);

#endif
