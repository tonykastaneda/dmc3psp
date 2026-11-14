#include <assert.h>
#include <math.h>
#include <stdio.h>
#define JESTER_LOGIC_ONLY
#include "../../psp/testroom/jester.c"
static float unit(void){return .5f;}
static void tick(JesterProjectiles*w,int n,float px,float py,float pz){jester_projectiles_update(w,(float)n,3500,500,3500,0,px,py,pz,1,unit);}
int main(void){JesterProjectiles w;float x,z,d;int heavy;
 jester_projectiles_reset(&w);jester_projectiles_begin(&w,0,0,3500,500,3500,3500,4300);
 tick(&w,140,3500,500,4300);assert(!w.spawned[0]);tick(&w,1,3500,500,4300);assert(w.spawned[0]==1 && !w.released);
 tick(&w,170,3500,500,4300);assert(w.released==1);tick(&w,25,3500,500,4300);assert(jester_projectiles_landed(&w,&x,&z,&d,&heavy) && d==1200 && heavy);
 jester_projectiles_reset(&w);jester_projectiles_begin(&w,1,0,3500,500,3500,3500,4000);tick(&w,181,3500,500,4000);assert(w.spawned[1]==1);
 jester_projectiles_stop(&w);tick(&w,54,3500,500,4000);assert(!w.landed);tick(&w,1,3500,500,4000);assert(w.landed && w.damage==650);
 jester_projectiles_reset(&w);jester_projectiles_begin(&w,1,0,3500,500,3500,3500,4000);tick(&w,181,3500,500,4000);tick(&w,65,3900,500,4000);assert(!w.landed);tick(&w,20,3500,500,4000);assert(!w.landed);
 jester_projectiles_reset(&w);jester_projectiles_begin(&w,2,0,3500,500,3500,3500,4300);tick(&w,1,3500,500,4300);assert(!w.landed);tick(&w,1,3900,500,3900);assert(w.landed && w.damage==800);assert(w.angular_speed==0);
 jester_projectiles_reset(&w);jester_projectiles_begin(&w,2,2,3500,500,3500,3500,4300);{float a=w.angle;tick(&w,60,3500,500,4300);assert(w.angle<a);}
 jester_projectiles_reset(&w);assert(!w.emitting && !w.shots[0].active);
 puts("Jester: emitter timing, ball travel/hit, trap warning/dodge/window, four-arm gaps and rank rotation passed");return 0;}
