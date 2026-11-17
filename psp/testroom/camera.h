#ifndef DMC3PSP_TESTROOM_CAMERA_H
#define DMC3PSP_TESTROOM_CAMERA_H

/* The game's camera, room by room (docs/CAMERA.md): the room's CAM zones (fixed Pan cameras and Rail
 * cameras, chosen by where Dante stands, blended or cut) and, outside them, the follow camera with the
 * room's pitch, distance and FOV (# GAME cam_rotx / cam_len / cam_aov). Writes the eye, target, FOV and roll
 * into the FollowCamera that the rest of the test room reads. */
#include "player.h"

/* The room whose camera data to use, by its file name ("ROOM_001.BIN"); NULL or unknown: the game's
 * defaults and no zones. start: the POS index Dante starts at (its camera yaw offset). */
void camera_room(const char *room_file, int start);
/* Place the camera as the game does when Dante appears (FUN_0024e7d0) and forget the zones. */
void camera_start(const Player *player, FollowCamera *camera);
/* One update: dt seconds (the game steps once per 1/60 s). */
void camera_update(const Player *player, FollowCamera *camera, int reset_pressed, float dt);
/* The yaw the stick is read against: the camera's, except just after a cut, when it keeps the old one while
 * the stick is held and isn't turned more than 15 degrees (FUN_00254990 / FUN_00254670). */
float camera_stick_yaw(FollowCamera *camera, float stick_x, float stick_y, int stick_active);
/* Dante is in his combat stance: the rooms' "battle" zones apply (main.c: enemies alive in the room). */
extern int camera_battle;
/* A boss fight is on (main.c): the boss camera's id (FUN_003595b0: 3 Cerberus, 9 Vergil), -1 for a boss whose
 * camera isn't ported (the follow camera), 0 none. */
extern int camera_boss;
extern float camera_boss_at[3];   /* the boss's feet, for the bosses' cameras that frame him */
/* Index of the room's zone in use (-1: the follow camera). */
int camera_zone(void);

/* The event scripts' camera (messages.c runs the ops). A cut of the room's DCA chunk (the data PAC's entry 8):
 * eye and target splines with FOV and roll per point, played over its frame count. hold: 4e, the cut stays on
 * its first frame. Returns 0 if the room hasn't that cut. */
int camera_event_cut(int cut, int hold);
int camera_event_done(void);
int camera_start_count(void);     /* the room's POS records (up to 8) */      /* 4f: the cut has reached its last frame (or none is up) */
int camera_event_on(void);
void camera_event_end(const Player *player);   /* 4c: back to the zones and the follow camera */
void camera_zone_enable(int zone, int off);    /* 75 zone 1 turns it off, 0 back on (until the next room) */
void camera_follow_set(float pitch_deg, float len, float battle, float aov_deg);   /* 77 */
void camera_follow_turn(const Player *player, float deg);                         /* 76: Dante's yaw + deg */

#endif
