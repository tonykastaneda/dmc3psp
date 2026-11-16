#ifndef DMC3PSP_TESTROOM_MESSAGES_H
#define DMC3PSP_TESTROOM_MESSAGES_H

/* The event scripts' on-screen messages (messages.c): the text box the room scripts open with `message`. */
#include <pspctrl.h>

#define MSG_BUTTON_NEXT (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)
#define MSG_LINES 4
#define MSG_LINE_CHARS 96

/* What the blocks ask about, from the mission's side. */
typedef struct {
    float x, y, z;                 /* Dante's feet */
    int examine;                   /* O pressed this frame and nothing else took it */
    int enemies_left;
    int (*item_got)(int item);     /* 1 if he holds it */
} MsgEnv;

void msg_reset(void);                     /* a new mission: every block may run again */
void msg_enter(int mission, int room);    /* mission 0: none run */
/* Each frame in a mission. Returns 1 while the box is open: nothing else should take the buttons. */
int msg_update(float dt, unsigned int pressed, const MsgEnv *env);
int msg_busy(void);
/* The open page's lines (colour codes inside: \x01 r g b, each 1 + value / 2 of 0-128, and \x02 back). */
int msg_page(char lines[MSG_LINES][MSG_LINE_CHARS]);
void msg_status(char *out, int size);

#endif
