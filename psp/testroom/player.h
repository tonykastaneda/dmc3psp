#ifndef DMC3PSP_TESTROOM_PLAYER_H
#define DMC3PSP_TESTROOM_PLAYER_H

/* Test-room player controller and follow camera. Pure C, no PSP calls, so it
 * can also be compiled on the host for quick checks. */

#define ROOM_HALF_SIZE 1000.0f   /* room spans -1000..1000 on X and Z */
#define PLAYER_RADIUS 40.0f
#define PLAYER_RUN_SPEED 450.0f  /* units (cm) per second at full stick */
#define PLAYER_TURN_RATE 12.0f   /* radians per second */
#define CAMERA_DISTANCE 300.0f
#define CAMERA_HEIGHT 190.0f
#define CAMERA_TARGET_HEIGHT 95.0f
#define CAMERA_ORBIT_RATE 2.2f   /* radians per second while L/R is held */

typedef struct {
    float x, z;        /* position on the floor */
    float yaw;         /* facing; 0 faces +Z, positive turns toward +X */
    float speed;       /* current ground speed, for the HUD */
} Player;

typedef struct {
    float yaw;         /* orbit angle around the player; camera sits behind -forward */
    float x, y, z;     /* smoothed eye position */
    float tx, ty, tz;  /* smoothed look-at target */
} FollowCamera;

typedef struct {
    float stick_x;     /* -1..1, right is positive */
    float stick_y;     /* -1..1, up (away from the player) is positive */
    int orbit;         /* -1 = L held, +1 = R held, 0 = neither */
    int reset;
} PlayerInput;

void player_reset(Player *player, FollowCamera *camera);
void player_update(Player *player, FollowCamera *camera, const PlayerInput *input, float dt);

#endif
