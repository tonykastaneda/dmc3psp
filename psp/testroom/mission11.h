#ifndef DMC3PSP_TESTROOM_MISSION11_H
#define DMC3PSP_TESTROOM_MISSION11_H
/* Mission 11, "Revenge": 214's slide blocks (EventTbl11, docs/GAME_FLOW.md). Rooms by number, doors by index. */

void m11_reset(void);
void m11_enter(int room);
int m11_door_shut(int room, int door);
int m11_script_held(int room);
void m11_update(float dt);
void m11_hit(float x, float y, float z, float yaw, float reach, int heavy);
void m11_draw(void);
void m11_status(char *out, int size);   /* for tests */

#endif
