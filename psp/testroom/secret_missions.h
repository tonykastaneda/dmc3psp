#ifndef DMC3PSP_TESTROOM_SECRET_MISSIONS_H
#define DMC3PSP_TESTROOM_SECRET_MISSIONS_H
/* The Secret Missions: entered from a main mission's room (an EVE area of kind 7, its id the Secret Mission's number),
 * played in their own room (st600-st611: Secret Mission n is room 599 + n), and left back where they were entered.
 * See secret_missions.c. Rooms are given by number. */

typedef struct {
    int room;                 /* the room he's in */
    float x, y, z;
    int on_ground;
    float health;
    int alive;                /* enemies not yet dead */
    int fight_done;           /* the room's script has run out */
    int breakables_left;      /* this room's unbroken props */
    int lift_top;             /* this room's lift is at its top */
    unsigned int pressed;     /* buttons pressed this frame */
} SmView;

void sm_reset(void);                  /* a mission starts */
int sm_active(void);                  /* in a Secret Mission now */
/* Each frame. Returns 1 when he's to go elsewhere: room *go_room, at its start *go_start, or (go_place[3] != 0) at
 * go_place's x, y, z. prompt: set when O does something here. */
int sm_update(float dt, const SmView *v, int *prompt, int *go_room, int *go_start, float go_place[4]);
const char *sm_caption(float *alpha);   /* a line to show, or 0 */
void sm_draw(void);                     /* the entry markers and goals, world space */
void sm_status(char *out, int size);

#endif
