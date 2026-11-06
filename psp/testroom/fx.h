#ifndef DMC3PSP_TESTROOM_FX_H
#define DMC3PSP_TESTROOM_FX_H

/* Effects: additive sprites and ribbons using the game's own sheets from
 * static_effect.pac. The game's effect scripts (FUN_00208590 and its data) aren't
 * ported; each effect here is built by hand to match the real game side by side, with
 * a low cap on sprites to stay inside the PSP's frame time. */

void fx_reset(void);
unsigned int fx_random_u32(void);   /* visual randomness, not the AI stream */
void fx_update(float dt);
/* Original em008 V timeline IDs: 702 departure, 499 arrival, 516 uppercut arrival. */
void fx_vanguard_event(int id, const float at[3]);
/* In the display list, after the world and before the 2D. eye: the camera position. */
void fx_draw(const float eye[3]);

/* A soft dark disc on the floor. Before the characters, in the display list. */
void fx_draw_shadow(float x, float y, float z, float radius, unsigned int alpha);

void fx_screen_tint(unsigned int color);   /* a flat wash over the whole picture */
void fx_screen_negative(void);
void fx_devil_burst(const float at[3]);   /* Devil Trigger's change */
void fx_devil_aura(const float at[3]);    /* each frame while it lasts */
void fx_muzzle(const float at[3], const float dir[3], unsigned int color);
void fx_tracer(const float from[3], const float to[3], unsigned int color, float width);
void fx_projectile(const float tail[3], const float head[3], unsigned int color); /* one frame */
void fx_hit_spark(const float at[3]);
/* A soft additive dot drifting at vel (cm/s), from size0 to size1 cm over life seconds. */
void fx_mote(const float at[3], const float vel[3], float size0, float size1, float life, unsigned int color);
void fx_wall_spark(const float at[3]);
void fx_explosion(const float at[3]);
void fx_dust(const float at[3]);              /* landing, a roll */
void fx_jump_burst(const float at[3]);        /* Enemy Step */
void fx_hike_seal(const float at[3], unsigned int color);
void fx_star_seal(const float at[3], const float direction[3], unsigned int color);
void fx_streak(const float from[3], const float to[3]);   /* Trickster dash, Sky Star */
void fx_guard_flash(const float at[3]);       /* Royalguard */

/* A blade's streak: call each frame with the blade's base and tip while it's swinging. */
void fx_trail_add(const float base[3], const float tip[3], unsigned int color);

#endif
