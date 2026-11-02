#ifndef DMC3PSP_TESTROOM_PLAYER_H
#define DMC3PSP_TESTROOM_PLAYER_H

/* Test-room player controller and follow camera. Pure C, no PSP calls.
 *
 * Control scheme (PSP):
 *   analog stick  move (camera-relative)
 *   d-pad         style switch: up Trickster, down Royalguard,
 *                 left Swordmaster, right Gunslinger
 *   R             lock-on (no enemies yet: holds Dante's facing, strafe)
 *   L             weapon wheel (placeholder)
 *   triangle      melee (placeholder)   square  guns (placeholder)
 *   circle        style action (placeholder)
 *   cross         jump (implemented)
 *   SELECT        snap the camera behind Dante
 *   START         motion viewer (d-pad left/right picks a motion)
 */

#define ROOM_HALF_SIZE 1000.0f   /* room spans -1000..1000 on X and Z */
#define PLAYER_RADIUS 40.0f
#define PLAYER_RUN_SPEED 510.0f  /* cm/s at full stick; matches the run motion (408 cm / 48 frames) */
#define PLAYER_TURN_RATE 12.0f   /* radians per second */
#define PLAYER_JUMP_SPEED 620.0f
#define PLAYER_GRAVITY 1900.0f
#define CAMERA_DISTANCE 300.0f
#define CAMERA_HEIGHT 190.0f
#define CAMERA_TARGET_HEIGHT 95.0f
#define CAMERA_SNAP_RATE 10.0f   /* exponential rate of the SELECT snap */

typedef enum { STYLE_TRICKSTER, STYLE_SWORDMASTER, STYLE_GUNSLINGER, STYLE_ROYALGUARD, STYLE_COUNT } Style;

typedef enum {
    ACTION_NONE, ACTION_MELEE, ACTION_GUN, ACTION_STYLE, ACTION_JUMP, ACTION_ROLL,
    ACTION_STYLE_SWITCH, ACTION_CAMERA_SNAP
} Action;

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
    float camera_yaw;   /* copied from the camera each update (combat aims with it) */
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
    int lock_on;       /* R held */
    int weapon_wheel;  /* L held */
    int melee, gun, style_action, jump, snap_camera, reset, toggle_viewer; /* pressed this frame */
} PlayerInput;

/* World hooks (set by main when a stage is loaded). */
extern float player_spawn[4];       /* x, y, z, yaw used by player_reset */
extern float player_room_limit;     /* |x|,|z| clamp; huge on a stage */
/* Floor height under (x, z) no higher than y + step; 0 if none. NULL = flat floor at 0. */
extern int (*player_floor_fn)(float x, float y, float z, float step, float *out);

void player_reset(Player *player, FollowCamera *camera);
void player_update(Player *player, FollowCamera *camera, const PlayerInput *input, float dt);
const char *player_style_name(Style style);
const char *player_action_name(Action action);

#endif
