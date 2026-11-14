/* The Jester's decoded pieces (CEm044, SE; docs/ENEMY_SPECS.md E). */
#include "jester.h"

#include <math.h>

float jester_max_health(int encounter) {
    static const float health[3] = {5000.0f, 6500.0f, 10000.0f};   /* 0x5e2f60, a stride of 8 */
    return health[encounter >= 0 && encounter < 3 ? encounter : 0];
}

/* 0x5e2e40: {about Dante?, radius, random spread} a row. */
static const struct { unsigned char dante; float radius, spread; } rows[8] = {
    {0, 0.0f, 50.0f}, {1, 50.0f, 500.0f}, {1, 200.0f, 1000.0f}, {1, 50.0f, 250.0f},
    {1, 100.0f, 900.0f}, {1, 150.0f, 600.0f}, {1, 100.0f, 400.0f}, {1, 150.0f, 600.0f},
};

int jester_pick(int *last, float (*unit)(void)) {
    int n = (int)(unit() * 3.0f) % 3;
    if (n == *last) n = (n + 1) % 3;
    *last = n;
    return n;
}

void jester_reappear(int row, float px, float pz, float *x, float *z, float (*unit)(void)) {
    float cx = rows[row & 7].dante ? px : *x, cz = rows[row & 7].dante ? pz : *z;
    float r = rows[row & 7].radius + rows[row & 7].spread * unit();
    float a = unit() * 6.2831853f;
    int i;
    for (i = 0; i <= 0x23; ++i, a += 0.17453292f) {
        float tx = cx + sinf(a) * r, tz = cz + cosf(a) * r;
        *x = tx; *z = tz;
        if ((tx - 3500.0f) * (tx - 3500.0f) + (tz - 3500.0f) * (tz - 3500.0f) < 1000.0f * 1000.0f) return;
    }
}

/* Native emitter actors 55c380 / 55bf40 / 54d3a0 and children 54de00 / 54c660.
 * Their packet fields are independently represented here; no generic rolling-orb actor is used. */
#include <string.h>
#define JP_PI 3.14159265358979323846f
static const float ball_wait[4]={140,80,40,20}; /* 5e2fd0, stride 8; speed 70 in every rank */
static const float trap_wait[4]={180,120,90,60}; /* 5e3000 */
static const float cross_turn[4]={0,0,-.13f,-.25f}; /* 5e3010: AI rank, not encounter */

void jester_projectiles_reset(JesterProjectiles *w){memset(w,0,sizeof(*w));w->held=-1;}
void jester_projectiles_stop(JesterProjectiles *w){
    w->emitting=0;w->contact=0;
    if(w->held>=0 && w->held<JESTER_SHOTS && w->shots[w->held].phase==0)w->shots[w->held].active=0;
    w->held=-1;
}
void jester_projectiles_begin(JesterProjectiles*w,int kind,int rank,float x,float y,float z,float px,float pz){
    jester_projectiles_stop(w);w->kind=kind>=0 && kind<3?kind:0;w->rank=rank>=0 && rank<4?rank:0;
    w->emitting=1;w->phase=0;w->timer=0;w->age=0;w->life=10000;
    w->cross[0]=x;w->cross[1]=y;w->cross[2]=z;
    w->angle=atan2f(px-x,pz-z)+JP_PI/4;w->angular_speed=cross_turn[w->rank]*JP_PI/180;
    if(kind==2)++w->spawned[2];
}
float jester_projectiles_minimum(const JesterProjectiles*w){
    /* The body stand-in previously ended the throw before its emitter could release anything.
     * Hold its final pose long enough for one native emission cycle. */
    return w->kind==0?ball_wait[w->rank]+69+99+60+8:w->kind==1?trap_wait[w->rank]+4:0;
}
static void jp_land(JesterProjectiles*w,float x,float z,float damage,int heavy){
    if(w->landed)return;w->landed=1;w->from_x=x;w->from_z=z;w->damage=damage;w->heavy=heavy;
}
static int jp_sphere(const float p[3],float radius,float px,float py,float pz){
    float x=p[0]-px,z=p[2]-pz,y=p[1]-fmaxf(py+20,fminf(py+160,p[1]));
    radius+=40;return x*x+y*y+z*z<radius*radius;
}
static int jp_slot(JesterProjectiles*w){int i;for(i=0;i<JESTER_SHOTS;++i)if(!w->shots[i].active){memset(&w->shots[i],0,sizeof(w->shots[i]));w->shots[i].active=1;return i;}return -1;}
static void jp_ball_release(JesterProjectiles*w,float yaw,float px,float py,float pz){
    JesterShot*p;float dx,dz,d,pitch;
    if(w->held<0)return;p=&w->shots[w->held];dx=px-p->pos[0];dz=pz-p->pos[2];d=sqrtf(dx*dx+dz*dz);
    pitch=atan2f(py+100-p->pos[1],d);pitch=fmaxf(-JP_PI/12,fminf(JP_PI/12,pitch));
    p->phase=1;p->age=0;p->animation=0;p->yaw=yaw;
    p->velocity[0]=sinf(yaw)*cosf(pitch)*70;p->velocity[1]=sinf(pitch)*70;p->velocity[2]=cosf(yaw)*cosf(pitch)*70;
    ++w->released;w->held=-1;
}
static void jp_step(JesterProjectiles*w,float f,float x,float y,float z,float yaw,float px,float py,float pz,int attacks,float(*unit)(void)){
    int i;
    if(w->emitting){
        w->life-=f;w->age+=f;
        if(w->life<0)jester_projectiles_stop(w);
        else if(w->kind==0){
            w->timer+=f;
            if(w->phase==0 && w->timer>ball_wait[w->rank]){
                i=jp_slot(w);w->held=i;w->phase=1;w->timer=0;
                if(i>=0){w->shots[i].kind=0;w->shots[i].pos[0]=x+sinf(yaw)*120;w->shots[i].pos[1]=y+180;w->shots[i].pos[2]=z+cosf(yaw)*120;++w->spawned[0];}
            }else if(w->phase==1 && w->timer>69){w->phase=2;w->timer=0;}
            else if(w->phase==2 && w->timer>99){jp_ball_release(w,yaw,px,py,pz);w->phase=3;w->timer=0;}
            else if(w->phase==3 && w->timer>60){w->phase=0;w->timer=0;}
        }else if(w->kind==1){
            w->timer+=f;if(w->timer>trap_wait[w->rank]){
                w->timer=0;i=jp_slot(w);if(i>=0){JesterShot*p=&w->shots[i];p->kind=1;p->pos[0]=px;p->pos[1]=500;p->pos[2]=pz;p->yaw=unit()*2*JP_PI;++w->spawned[1];}
            }
        }else {
            int contacts=0;w->angle+=w->angular_speed*f;
            for(i=0;i<4;++i){float a=w->angle+i*JP_PI/2,dx=px-w->cross[0],dz=pz-w->cross[2];
                float along=dx*sinf(a)+dz*cosf(a),side=dx*cosf(a)-dz*sinf(a);
                /* 54db00: box half extents 600,1000,70 centred at (600,1000,0).
                 * Four perpendicular transforms, with the native 5-degree lean. */
                float up=py+90-w->cross[1],lean=(w->angular_speed>0?-5:5)*JP_PI/180;
                float lateral=side*cosf(lean)-up*sinf(lean),vertical=up*cosf(lean)+side*sinf(lean);
                if(along>-40 && along<1240 && fabsf(lateral)<110 && vertical>-90 && vertical<2090)contacts|=1<<i;
            }
            if(attacks && (contacts & ~w->contact))jp_land(w,w->cross[0],w->cross[2],800,0);
            w->contact=contacts;
        }
    }
    for(i=0;i<JESTER_SHOTS;++i){JesterShot*p=&w->shots[i];float previous[3],dx,dz,t,len,closest[3];if(!p->active)continue;
        p->age+=f;p->animation+=f;
        if(p->kind==1){
            if(p->age>55 && p->age<=65 && !p->hit){float at[3]={p->pos[0],650,p->pos[2]};
                if(attacks && jp_sphere(at,75,px,py,pz)){p->hit=1;jp_land(w,p->pos[0],p->pos[2],650,0);}}
            if(p->age-f<=55 && p->age>55)++w->armed;
            if(p->age>180)p->active=0;
        }else if(p->phase==0){p->pos[0]=x+sinf(yaw)*120;p->pos[1]=y+180;p->pos[2]=z+cosf(yaw)*120;}
        else if(p->phase==1){
            memcpy(previous,p->pos,sizeof(previous));p->pos[0]+=p->velocity[0]*f;p->pos[1]+=p->velocity[1]*f;p->pos[2]+=p->velocity[2]*f;
            dx=p->pos[0]-previous[0];dz=p->pos[2]-previous[2];len=dx*dx+dz*dz;t=len>0?((px-previous[0])*dx+(pz-previous[2])*dz)/len:0;t=fmaxf(0,fminf(1,t));
            closest[0]=previous[0]+dx*t;closest[1]=previous[1]+(p->pos[1]-previous[1])*t;closest[2]=previous[2]+dz*t;
            if(attacks && !p->hit && jp_sphere(closest,50,px,py,pz)){p->hit=1;jp_land(w,p->pos[0],p->pos[2],1200,1);p->phase=2;p->age=0;}
            dx=p->pos[0]-3500;dz=p->pos[2]-3500;
            if(dx*dx+dz*dz>2500*2500 || p->pos[1]<500 || p->age>600){p->phase=2;p->age=0;}
        }else {
            if(attacks && !p->hit && jp_sphere(p->pos,200,px,py,pz)){p->hit=1;jp_land(w,p->pos[0],p->pos[2],1200,1);}
            if(p->age>20)p->active=0;
        }
    }
}
void jester_projectiles_update(JesterProjectiles*w,float frames,float x,float y,float z,float yaw,float px,float py,float pz,int attacks,float(*unit)(void)){
    /* Fixed native ticks preserve strict > timing and swept hits at 30 Hz/turbo. */
    while(frames>0){float f=fminf(frames,1);jp_step(w,f,x,y,z,yaw,px,py,pz,attacks,unit);frames-=f;}
}
int jester_projectiles_landed(JesterProjectiles*w,float*x,float*z,float*damage,int*heavy){
    if(!w->landed)return 0;w->landed=0;*x=w->from_x;*z=w->from_z;*damage=w->damage;*heavy=w->heavy;return 1;
}
