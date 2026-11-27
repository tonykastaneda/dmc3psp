#pragma once
#include "chess.h"
enum {BISHOP_M_IDLE,BISHOP_M_CAST,BISHOP_M_HEAL,BISHOP_M_DEATH,BISHOP_M_COUNT};
extern const char *const bishop_motion_names[BISHOP_M_COUNT];
enum {BISHOP_ORBS=16}; /* independent waves, native900-frame shell lifetime */
typedef struct {int stage,event;float pos[3],yaw,age,clock;} BishopOrb;
typedef struct {
 float pos[3],yaw,floor_y,frame,timer,wait,vx,vz,vy,health,max_health,previous_hp;
 int done,action,step,want,restart,land,remove,cast,heal,blocked;
 BishopOrb orbs[BISHOP_ORBS];
} BishopBrain;
void bishop_spawn(BishopBrain *b,int health);
void bishop_think(BishopBrain *b,ChessPiece *piece,float f,float x,float y,float z,float (*unit)(void),int attacks);
int bishop_hit(BishopBrain *b,int heavy,float x,float z,int mortal);
int bishop_loops(int motion);
void bishop_force(BishopBrain *b,ChessPiece *p,int action,float (*unit)(void)); /* scene QA */
