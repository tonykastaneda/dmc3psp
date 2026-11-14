#ifndef DMC3PSP_JESTER_H
#define DMC3PSP_JESTER_H

/* The Jester (CEm044, docs/ENEMY_SPECS.md E): what's decoded of his class. enemy.c runs his body and moves. */

/* His health by encounter (+0x481e, from the spawn record): 0x5e2f60, 5000 / 6500 / 10000. Room 422 is the first. */
float jester_max_health(int encounter);

/* Where he reappears (FUN_00536230): table 0x5e2e40 row `row`, about Dante (mode 1) or himself (mode 0), at a
 * radius base + random spread, a random bearing turned 10 degrees at a time (up to 36) until the spot is within
 * 1000 cm of the arena's centre (3500, 3500). unit() gives uniform [0, 1). */
void jester_reappear(int row, float px, float pz, float *x, float *z, float (*unit)(void));

/* FUN_00534490: idling (actions 0x64, 0x67-0x6a) he attacks once his idle time reaches 0x5e2e30[rank]: 500 frames
 * on Easy and Normal (450, 400 above). FUN_0053e960 picks the attack (0x75, 0x76, 0x77: 0, 1, 2), a third each but
 * never the last one again; FUN_0053e720 picks what he throws the same way. */
#define JESTER_ATTACK_WAIT 500.0f
int jester_pick(int *last, float (*unit)(void));

#endif
