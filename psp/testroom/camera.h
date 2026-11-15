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
/* A boss fight is on: the game's per-boss cameras aren't ported, so the follow camera (main.c). */
extern int camera_boss;
/* Index of the room's zone in use (-1: the follow camera). */
int camera_zone(void);

#endif
