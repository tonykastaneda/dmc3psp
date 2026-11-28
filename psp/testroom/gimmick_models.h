#ifndef GIMMICK_MODELS_H
#define GIMMICK_MODELS_H

/* Crest switches and seals drawn from the room's own models (tools/gimmicks.py switches_and_seals). */
#define GIM_SEAL 0
#define GIM_SWITCH 1
#define GIM_SWITCH_PARTS 11

/* A room's list, filled by main.c from rooms_runtime.h's room_gimmicks as the stage loads. */
void gim_clear(void);
void gim_add(int kind, int number, int tag, int attached, int door, const float o[3], const float r[9]);
int gim_has_switch(int number);
int gim_has_seal(int number);
/* The room's k-th switch set's number, or -1. */
int gim_switch_number(int k);
/* The seal set standing in door n's way, or -1. */
int gim_seal_at_door(int door);
/* Switch set `number`: the hub's turn (+0x850) and the disc's (+0x854) in degrees, the blades' push-out (+0x858[8],
 * NULL none), and for one on another set's joint where it is (x y z yaw degrees; NULL: where it was baked). */
void gim_switch_pose(int number, float spin, float wing, const float push[8], const float *at);
void gim_switch_hide(int number, int hidden);
/* Seal set `number`'s board: colour 0 red, 1 blue, 2 purple; alpha 0..1 of its full 0x40. */
void gim_seal_draw(int number, int colour, float alpha);

/* Key items: the game's own effect (V160) at each one lying, named every frame it's shown (0 if it couldn't start:
 * the caller's glow stands in). gim_items_end_frame stops the rest; gim_items_reset after nfx_reset. */
int gim_key_item(const float at[3]);
void gim_items_end_frame(void);
void gim_items_reset(void);

#endif
