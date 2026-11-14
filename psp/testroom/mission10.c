/* EventTbl10 item gates; native stage pieces, approximate rigid bridge motion
 * and deck collision. See docs/MISSION10_SPEC.md. */
#include "mission10.h"
#include "stage.h"
#include <math.h>
static int room,previous_room,mask,generator,bridge_target;
static float lake,bridge;
void mission10_reset(void){room=-1;mask=generator=0;lake=bridge=0;bridge_target=0;}
void mission10_enter(int number,int previous){room=number;previous_room=previous;}
int mission10_hold(void){return room==209 && previous_room!=206 && previous_room!=210;}
int mission10_clear_area(float x,float y,float z){
 static const float p[4][2]={{2590.89f,1678.12f},{2463.74f,1693.98f},{2766.53f,2991.68f},{2918.11f,2950.38f}};
 int i,pos=0,neg=0;if(y< -200 || y>2900)return 0;
 for(i=0;i<4;++i){float c=(p[(i+1)%4][0]-p[i][0])*(z-p[i][1])-(p[(i+1)%4][1]-p[i][1])*(x-p[i][0]);if(c>0)pos=1;else if(c<0)neg=1;}
 return !(pos&&neg);
}
void mission10_update(float f,int mask_used,int generator_used){
 if(generator_used && !generator)bridge_target=1;
 mask=mask_used;generator=generator_used;
 if(mask)lake=fminf(600,lake+f);
 if(generator)bridge=bridge_target?fminf(600,bridge+f):fmaxf(0,bridge-f);
 if(room==206)stage_set_group_pose(242,0,0,0,lake/600*829.36f);
 if(room==201){
  stage_set_group_pose(240,1250,3750,bridge/600*.78539816f,0);
  stage_set_group_pose(241,3750,1250,bridge/600*.78539816f,0);
 }
}
static int deck(float x,float z,float ax,float az,float bx,float bz,float width,float *t){
 float dx=bx-ax,dz=bz-az,l=dx*dx+dz*dz,q=((x-ax)*dx+(z-az)*dz)/l;
 if(q<0 || q>1)return 0;
 dx=x-ax-q*dx;dz=z-az-q*dz;
 if(dx*dx+dz*dz>width*width)return 0;
 *t=q;return 1;
}
static int height(float x,float z,float *h){
 float t;
 if(room==201 && generator && bridge>=590 && deck(x,z,1250,3750,3750,1250,180,&t)){*h=45;return 1;}
 if(room==206 && mask && lake>=590 && deck(x,z,14650,7396,12480,7396,250,&t)){*h=2575+t*565;return 1;}
 return 0;
}
int mission10_floor(float x,float y,float z,float step,float *h){float q;if(!height(x,z,&q)||q>y+step+60)return 0;*h=q;return 1;}
int mission10_bridge(float x,float y,float z){float h;return height(x,z,&h)&&fabsf(y-h)<220;}

void mission10_control(float x,float y,float z){
 float dx=x-1255,dz=z-3617;
 if(room==201 && generator && (bridge==0 || bridge==600) && dx*dx+dz*dz<180*180 && y>=-111 && y<=289)bridge_target=!bridge_target;
}
