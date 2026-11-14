#pragma once
enum {BISHOP_M_IDLE,BISHOP_M_CAST,BISHOP_M_DEATH,BISHOP_M_COUNT};
extern const char *const bishop_motion_names[BISHOP_M_COUNT];
typedef struct {float pos[3],yaw,floor_y,frame,timer,target[3],vel;int done,action,step,health,want,restart,land,remove,cast;} BishopBrain;
void bishop_spawn(BishopBrain *b,int health);
void bishop_think(BishopBrain *b,float f,float x,float y,float z,float (*unit)(void),int attacks);
int bishop_hit(BishopBrain *b,int heavy,float x,float z,int mortal);
int bishop_loops(int motion);
