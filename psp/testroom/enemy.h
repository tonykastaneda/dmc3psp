#ifndef DMC3PSP_TESTROOM_ENEMY_H
#define DMC3PSP_TESTROOM_ENEMY_H

/* Training dummies: Seven Hells enemies (GDATA slot 117, em000) in the test street with
 * infinite health, running the game's own decision logic and motion scripts. The debug menu sets
 * how many there are and whether they attack Dante.
 *
 * Calls that take no index act on the selected dummy: see enemy_select. */
#define ENEMY_MAX 100

/* The kinds there are models for, in tools/prepare_skinned_model.py's ENEMY_KINDS order. */
typedef enum {
    ENEMY_PRIDE, ENEMY_GLUTTONY, ENEMY_LUST, ENEMY_SLOTH, ENEMY_WRATH, ENEMY_GREED, ENEMY_ENIGMA, ENEMY_GOYLE, ENEMY_JESTER, ENEMY_AGNI, ENEMY_RUDRA, ENEMY_KIND_TOTAL
} EnemyKind;
/* Not a model kind: the Gigapede's head and segments, which take one enemy slot each. */
#define ENEMY_GIGAPEDE 100
#define ENEMY_VANGUARD 101
#define ENEMY_CERBERUS 102
#define ENEMY_VERGIL 103

typedef enum {
    HIT_LIGHT,    /* combo hits: flinch in place */
    HIT_HEAVY,    /* finishers, Stinger: knock back */
    HIT_LAUNCH,   /* High Time: launched into the air */
    HIT_AIR,      /* any hit while airborne: juggle pop */
    HIT_SLAM,     /* Helm Breaker: smashed to the floor */
    HIT_PISTOL    /* Ordinary E&I shot: distinct native damage/reaction record */
} HitKind;


int enemy_init(void);
void enemy_spawn(float x, float z);          /* one dummy, here, selected */
void enemy_spawn_height(float y);            /* look for the floor from about here, not the roofs */
/* In a mission enemies can die; the dummies of the debug menu have infinite health. */
void enemy_set_mortal(int on);
/* Called once as each enemy dies of its wounds in a mission (not for tests' kill-alls): its kind, its feet. */
extern void (*enemy_died_fn)(int kind, float x, float y, float z);
/* Add one here. Returns 0 if every slot is taken. */
int enemy_add(float x, float z);
int enemy_add_kind(int kind, float x, float z);
/* The kind for a room script's spawn class (the EST chunk's ids). */
int enemy_kind_for_class(int spawn_class);
int enemy_kind(void);                  /* the selected one's */
void enemy_set_dummy_kind(int kind);   /* what the debug menu's dummies are */
int enemy_alive_count(void);
void enemy_drop_below(float y);
void enemy_gun_aimed(float x, float z);
void enemy_set_hit_weapon(int melee);   /* the melee weapon of the hits that follow (0 Rebellion, 1 Cerberus...) */   /* Dante fired from here */      /* those fallen far below a riding lift: gone */          /* those not yet dead */
void enemy_kill_all(void);            /* for tests */
void enemy_trim(int keep);
void enemy_clear_minions(void);      /* tests: retain the boss and the script's alive-count gates */
void enemy_remove_defeated(void);    /* encounter transition: retire completed wave actors */
/* Keep the ones there are; add new ones in a ring around (x, z) or remove the last ones. */
void enemy_set_count(int count, float x, float z);
int enemy_count(void);
void enemy_select(int index);
void enemy_select_nearest(float x, float z);
void enemy_set_attacks(int attacks);
/* Bones update less often the more dummies there are; their positions still move every frame. */
void enemy_set_anim_auto(int on);
/* For tests: log every change of plan or action. */
void enemy_trace_start(void);
void enemy_trace_write(const char *path);
/* Run them without drawing them. */
void enemy_set_ai_only(int on);
/* For the PSP benchmark, to see what costs what. */
void enemy_set_test(int no_texture, int no_lighting, float ring);
int enemy_anim_rate(void);          /* poses a second each right now, 0 = every frame */
int enemy_poses_done(void);         /* running total, to measure the saving */
/* (px, py, pz) is Dante, whom they go for when attacking. */
void enemy_update(float dt, float px, float py, float pz);
/* An attack reached Dante this frame: returns 1 once per hit, from where, and if it's a heavy one. */
int enemy_attack_landed(float *from_x, float *from_z, int *heavy);
/* Original attack-record damage, or -1 for the generic 700/800 fallback. */
float enemy_attack_damage(void);
/* Boss arena constraints, if one is active. */
void enemy_clamp_player(float *z);
/* Draw with the view/projection set, inside an open display list. */
void enemy_draw(void);
int enemy_active(void);
void enemy_position(float out[3]);   /* feet */
float enemy_height(void);            /* feet height above its floor (air time) */
int enemy_hits_taken(void);
/* Current boss pool for the HUD; zero when there is no active boss. */
int enemy_boss_health(float *health, float *maximum);
/* The Gigapede's segments as floor (the mission's floor asks), and carrying him while he stands on one;
 * call after enemy_update. */
int enemy_giga_floor(float x, float y, float z, float step, float *out);
void enemy_giga_ride(float *x, float *y, float *z, int on_ground);
int enemy_giga_focus(float out[3]);
int enemy_giga_top_point(int part, float out[3]);
const char *enemy_giga_log(void);
/* A sword swing: the Gigapede's spheres within reach in front of him are batted back. */
void enemy_deflect(float px, float py, float pz, float yaw, float reach);
void enemy_test_orb(float x, float y, float z, float yaw);   /* tests: a Gigapede sphere 7 m in front of him */   /* its states, shells and throws so far, for tests */   /* for tests: the middle of a segment's back */   /* the middle of its parts in the room, for the camera; 0 if none */
const char *enemy_boss_name(void);
/* Apply a hit from a point (Dante's position) -- knock-back direction. */
void enemy_hit(HitKind kind, float from_x, float from_z);
/* Keep Dante (cylinder radius r at x,z) out of the dummy. */
void enemy_push_out(float *x, float *z, float y, float r);

#endif
