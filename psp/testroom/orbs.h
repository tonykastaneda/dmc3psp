#ifndef DMC3PSP_TESTROOM_ORBS_H
#define DMC3PSP_TESTROOM_ORBS_H

/* Red orbs: what enemies and breakables drop. They burst out, fall to the floor, then fly to Dante,
 * and each one collected adds its value to the count orbs_set_counter points at. */

void orbs_set_counter(int *red_orbs);
/* A burst worth this many red orbs in all, from here (cm, the game's world). */
void orbs_burst(float x, float y, float z, int total);
void orbs_clear(void);
/* dt in seconds; (px, py, pz) is Dante's feet. */
void orbs_update(float dt, float px, float py, float pz);
/* With the view and projection set, inside an open display list. */
void orbs_draw(void);
/* Seconds since the last one was collected, for the HUD's count to show and fade. */
float orbs_since_collected(void);

/* What the game drops for a dead enemy: its max health x 5 (the lowest style rank's multiplier; the port has
 * no style rank yet) x the difficulty's (Easy 0.5, Normal 1) / 100. See docs/SHOP.md. */
int orbs_for_enemy(int max_health, int difficulty);

#endif
