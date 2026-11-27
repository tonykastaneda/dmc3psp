/* EventTbl21 floor flow, kept apart from the story mission state. */
#include "bloody_palace.h"
#include "bloody_palace_runtime.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int active, level, previous, visits, room_number, group;
static unsigned int groups_used, bosses_used;
static int next_boss, forced_boss = -1, forced_group = -1;
static float clear_frames;
static int reward, pending_reward;
static float reward_pos[3], reward_vy;
static const int boss_rooms[17] = {433,434,435,436,437,438,439,440,441,442,443,444,445,446,422,448,449};
static const int increments[3] = {1,10,100};
static int ordinary(int room) { return room >= 423 && room <= 432; }
static int pick(unsigned int *used, int count) {
    int n = rand() % count, tries = 0;
    while (*used & (1u << n)) {
        n = (n + 1) % count;
        if (++tries == count) { *used = 0; break; }
    }
    return n;
}
void bp_reset(int enabled) {
    active=enabled; level=previous=visits=0; groups_used=bosses_used=0;
    room_number=-1; group=0; next_boss=0; clear_frames=0;
    forced_boss=forced_group=-1;
    reward=pending_reward=0;reward_vy=0;
}
int bp_active(void) { return active; }
int bp_floor(void) { return level; }
void bp_test_start(int floor, int count, int boss, int encounter) {
    if (!active) return;
    level=previous=floor<0?0:floor>9999?9999:floor;
    visits=count<0?0:count; forced_boss=boss; forced_group=encounter;
}
void bp_enter(int room) {
    int i;
    if (!active) return;
    room_number=room; clear_frames=0;
    reward=0;reward_vy=0;
    for (i=0;i<BP_PICKUP_COUNT && pending_reward;++i) {
        const PalacePickup *p=&palace_pickups[i];
        if (p->room==room && p->kind==pending_reward) {
            reward=p->kind; reward_pos[0]=p->pos[0];reward_pos[1]=p->pos[1];reward_pos[2]=p->pos[2];break;
        }
    }
    pending_reward=0;
    if (ordinary(room)) {
        if (previous/1000 != level/1000) groups_used=0;
        group=forced_group>=0 && forced_group<20?forced_group:pick(&groups_used,20);
        groups_used |= 1u<<group;
        ++visits;
        next_boss=forced_boss>=0 && forced_boss<17?forced_boss:pick(&bosses_used,17);
    } else {
        group=room==446 || room==448?1:room==449?2:0;
        visits=0;
    }
}
int bp_op(int index, PalaceOp *out) {
    int i;
    if (!active || index<0) return 0;
    for (i=0;i<BP_SCRIPT_COUNT;++i) {
        const PalaceScript *s=&palace_scripts[i];
        if (s->room==room_number && s->group==group) {
            if (index>=s->count) return 0;
            *out=s->ops[index]; return 1;
        }
    }
    return 0;
}
void bp_update(float frames, int cleared) {
    if (active) clear_frames=cleared?clear_frames+frames:0;
}
int bp_portal_ready(int portal) {
    if (!active || portal<0 || portal >= (ordinary(room_number)?3:1)) return 0;
    return clear_frames >= (ordinary(room_number)?60.0f*(portal+1):180.0f);
}
int bp_route(int portal, int *room, int *start) {
    if (!bp_portal_ready(portal) || bp_finished()) return 0;
    *start=0;
    if (ordinary(room_number)) {
        previous=level;
        level+=increments[portal]; if (level>9999) level=9999;
        pending_reward=portal==0?12:portal==1?15:0;
        if (level==9999) *room=444;
        else if (visits>=10) {
            *room=boss_rooms[next_boss]; bosses_used|=1u<<next_boss;
            if (*room==438) *start=2;
        } else *room=423+level/1000;
    } else *room=423+level/1000; /* Preserve the pre-increment thousand across a boss visit. */
    return 1;
}
int bp_finished(void) { return active && level==9999 && room_number==444 && clear_frames>=180.0f; }
void bp_draw(void) {
    char text[64];
    if (!active || !menu_loaded()) return;
    snprintf(text,sizeof text,"Level %d",level);
    menu_font_draw(12,237,18,text,0xffffffffu);
    if (clear_frames>=60.0f && ordinary(room_number)) {
        snprintf(text,sizeof text,"PORTALS  +1   +10   +100");
        menu_font_draw(120,238,15,text,0xffffffffu);
    }
}
void bp_status(char *out, int size) {
    snprintf(out,size,"palace on=%d level=%d room=%d visits=%d group=%d clear=%.0f boss=%d finished=%d pickup=%d",
             active,level,room_number,visits,group,clear_frames,next_boss,bp_finished(),reward);
}
int bp_pickup_position(float out[3]) {
    if (!active || !reward) return 0;
    out[0]=reward_pos[0];out[1]=reward_pos[1];out[2]=reward_pos[2];return reward;
}
void bp_pickup_fall(float frames, float floor_y) {
    if (!active || !reward) return;
    reward_vy-=0.6f*frames;reward_pos[1]+=reward_vy*frames;
    if (reward_pos[1]<floor_y+40) { reward_pos[1]=floor_y+40;reward_vy=0; }
}
int bp_pickup_collect(float x, float y, float z) {
    float dx=x-reward_pos[0],dz=z-reward_pos[2];int kind=reward;
    if (!active || !reward || dx*dx+dz*dz>120.0f*120.0f || fabsf(y+70-reward_pos[1])>150) return 0;
    reward=0;return kind;
}
