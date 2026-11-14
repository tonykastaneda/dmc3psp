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

#define JESTER_SHOTS 12
/* One owner per Jester. Frames are native 60 Hz; coordinates are centimetres. */
typedef struct {
    int active, kind, phase, hit;
    float pos[3], velocity[3], age, yaw, animation;
} JesterShot;
typedef struct {
    int emitting, kind, phase, rank, held, contact;
    float timer, age, life, cross[3], angle, angular_speed;
    JesterShot shots[JESTER_SHOTS];
    int landed, heavy, spawned[3], released, armed;
    float from_x, from_z, damage;
} JesterProjectiles;
void jester_projectiles_reset(JesterProjectiles *world);
void jester_projectiles_begin(JesterProjectiles *world, int kind, int rank, float x, float y, float z, float px, float pz);
/* Stop the emitter; launched balls/traps finish their own lifetimes. A death/reset clears all. */
void jester_projectiles_stop(JesterProjectiles *world);
float jester_projectiles_minimum(const JesterProjectiles *world);
void jester_projectiles_update(JesterProjectiles *world, float frames, float x, float y, float z, float yaw,
                              float px, float py, float pz, int attacks, float (*unit)(void));
int jester_projectiles_landed(JesterProjectiles *world, float *x, float *z, float *damage, int *heavy);
int jester_projectiles_init(void);
void jester_projectiles_draw(const JesterProjectiles *world);
#endif
