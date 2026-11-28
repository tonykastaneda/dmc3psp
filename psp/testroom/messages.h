#ifndef DMC3PSP_TESTROOM_MESSAGES_H
#define DMC3PSP_TESTROOM_MESSAGES_H

/* The event scripts' room blocks (messages.c): the text box the room scripts open with `message`, and their
 * in-engine events with the event camera's cuts. */
#include <pspctrl.h>

#include "player.h"

#define MSG_BUTTON_NEXT (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)
#define MSG_LINES 4
#define MSG_LINE_CHARS 96

/* What the blocks ask about, from the mission's side. */
typedef struct {
    float x, y, z;                 /* Dante's feet */
    int examine;                   /* O pressed this frame and nothing else took it */
    int enemies_left;
    int (*item_got)(int item);     /* 1 if he holds it */
    int cleared;                   /* op 17: a fight was on in this visit and it's over (the PS2 needs one started) */
    int prev_room;                 /* op 1e: the room he came from (-1 none) */
    const Player *player;          /* for the camera's ops */
    void (*warp_pos)(int pos);     /* op 59: Dante to the room's POS record */
    void (*warp)(float x, float y, float z, float yaw_deg);   /* op 5b */
} MsgEnv;

void msg_reset(void);                     /* a new mission: every block may run again */
void msg_enter(int mission, int room);    /* mission 0: none run */
/* Each frame in a mission. Returns 1 while the box is open: nothing else should take the buttons. */
int msg_update(float dt, unsigned int pressed, const MsgEnv *env);
int msg_busy(void);
/* Whether a place is inside this room's EVE area a (event op 0x0f, sw_in_area). */
int msg_in_area(int a, float x, float y, float z);
/* An in-engine event is on (49 .. 4c): Dante takes no input, and the mission holds the enemies. */
int msg_event_on(void);
int msg_event_skippable(void);
/* START during a skippable event: on to its end, keeping only what changes state (FUN_0020f880). force: any event
 * (the test runs). */
void msg_event_skip(const MsgEnv *env, int force);
/* The open page's lines (colour codes inside: \x01 r g b, each 1 + value / 2 of 0-128, and \x02 back). */
int msg_page(char lines[MSG_LINES][MSG_LINE_CHARS]);
void msg_status(char *out, int size);

#endif
