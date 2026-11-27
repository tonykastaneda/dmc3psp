#pragma once
#include "chess.h"
enum {ROOK_M_IDLE,ROOK_M_ATTACK,ROOK_M_SPECIAL,ROOK_M_COUNT};
extern const char *const rook_motion_names[ROOK_M_COUNT];
typedef struct {
 float pos[3],yaw,floor_y,frame,timer,wait,vx,vz,vy,health,alternate_wait;
 float beam_pos[4][3],beam_age[4];
 int beam_hit[4],beam_event,action,step,want,restart,done,remove,land;
 int flags,board,count,blocked,summon_turns,summon,castle,cast;
} RookBrain;
void rook_spawn(RookBrain *b,int health,int board,float (*unit)(void));
void rook_force(RookBrain *b,ChessPiece *p,int action,float (*unit)(void));
void rook_think(RookBrain *b,ChessPiece *p,float f,float x,float y,float z,float (*unit)(void),int attacks);
int rook_beam_hits(const float from[3],float yaw,float x,float y,float z);
