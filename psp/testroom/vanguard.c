#include "vanguard.h"
#include "enemy.h"
#include "motion.h"
#include "player.h"
#include "stage.h"
#include "swizzle.h"
#include "fx.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "model_runtime.h"
#include "vanguard_runtime.h"
#include "vanguard_ai.h"

/* CEm008 / CComEm000, lowest AI rank. Parameters and scripts are read from SLUS_213.61.
 * Plans 27, 40, 56 and 57 use em008's weighted rows. This remains a partial port:
 * stage collision, damage, cloth and effects do not reproduce the complete PS2 runtime. */
#define PI 3.1415926536f
#define ANGLE(x) ((x) * (2.0f * PI / 65536.0f))
typedef struct { float m[3][3], p[3]; } Transform;
typedef struct { float u,v,nx,ny,nz,x,y,z; } Vertex;
static struct {
    float pos[3], yaw, hp, gauge, timer, frame, previous_frame, fade, fade_step, vy, ground;
    float cap_left[24], token, strain;
    int active, hidden, visible, dying, plan, action, phase, chain, motion, previous_motion, script, pc, stop, finish, done, hit_done, loop;
    int ids[80], ticks, trace, dead_reported;
} vg;
static Transform body[48];
static Transform bind_body[48];
static Vertex *vertices[9];
static unsigned short *indices[9];
static void *textures[2];
static char trace_path[320];
static int no_texture,no_lighting;
static int vg_difficulty=1;
void vanguard_set_difficulty(int difficulty) {vg_difficulty=difficulty;}
void vanguard_render_test(int texture,int lighting) {no_texture=texture;no_lighting=lighting;}
extern unsigned int enemy_ai_random(void);

static float wrap(float x) { while (x > PI) x -= 2*PI; while (x < -PI) x += 2*PI; return x; }
static float toward(float x,float z) { return atan2f(x-vg.pos[0], z-vg.pos[2]); }
static void turn(float x,float z,float rate,float f) {
    float d=wrap(toward(x,z)-vg.yaw), limit=ANGLE(rate)*f;
    vg.yaw += fmaxf(-limit,fminf(limit,d));
}
static void identity(Transform *t) { int i; memset(t,0,sizeof(*t)); for(i=0;i<3;++i)t->m[i][i]=1; }
static void multiply(Transform *out,const Transform *a,const Transform *b) {
    Transform t; int i,j;
    for(i=0;i<3;++i) {
        t.p[i]=a->p[i];
        for(j=0;j<3;++j) {
            t.m[i][j]=a->m[i][0]*b->m[0][j]+a->m[i][1]*b->m[1][j]+a->m[i][2]*b->m[2][j];
            t.p[i]+=a->m[i][j]*b->p[j];
        }
    }
    *out=t;
}
static void euler(Transform *t,float x,float y,float z) {
    float cx=cosf(x),sx=sinf(x),cy=cosf(y),sy=sinf(y),cz=cosf(z),sz=sinf(z);
    identity(t);
    t->m[0][0]=cz*cy; t->m[0][1]=cz*sy*sx-sz*cx; t->m[0][2]=cz*sy*cx+sz*sx;
    t->m[1][0]=sz*cy; t->m[1][1]=sz*sy*sx+cz*cx; t->m[1][2]=sz*sy*cx-cz*sx;
    t->m[2][0]=-sy; t->m[2][1]=cy*sx; t->m[2][2]=cy*cx;
}
static void pose(void) {
    static MotionSample s, prev;
    const VanguardMesh *m=&vanguard_parts[0];
    int i,k;
    motion_sample(motion_get(vg.motion),vg.frame,&s);
    if(vg.fade>0)motion_sample(motion_get(vg.previous_motion),vg.previous_frame,&prev);
    for(i=0;i<m->bone_count;++i) {
        float r[3],p[3]={m->local[i].x,m->local[i].y,m->local[i].z}; Transform local;
        for(k=0;k<3;++k) {
            r[k]=s.has_rot[i][k]?s.rot[i][k]:0;
            if(s.has_trans[i][k])p[k]=s.trans[i][k];
            if(vg.fade>0) {
                if(prev.has_rot[i][k])r[k]+=wrap(prev.rot[i][k]-r[k])*vg.fade;
                if(prev.has_trans[i][k])p[k]+=(prev.trans[i][k]-p[k])*vg.fade;
            }
        }
        euler(&local,r[0],r[1],r[2]);
        if(i)memcpy(local.p,p,sizeof(p));
        if(m->parents[i]<0)body[i]=local; else multiply(&body[i],&body[m->parents[i]],&local);
    }
}
static void trace(const char *event) {
    FILE *f;
    if(!vg.trace)return;
    f=fopen(trace_path,"a");
    if(f) { fprintf(f,"t=%d event=%s plan=%d action=%d phase=%d script=%d frame=%.2f hp=%.0f hidden=%d x=%.1f y=%.1f z=%.1f\n",
        vg.ticks,event,vg.plan,vg.action,vg.phase,vg.script,vg.frame,vg.hp,vg.hidden,vg.pos[0],vg.pos[1],vg.pos[2]); fclose(f); }
}
void vanguard_note(const char *event,float value) {
    FILE *f;
    if(!vg.trace)return;
    f=fopen(trace_path,"a");
    if(f) {fprintf(f,"t=%d event=%s value=%.1f hp=%.0f\n",vg.ticks,event,value,vg.hp);fclose(f);}
}
void vanguard_trace(const char *path) { vg.trace=path && *path; if(vg.trace)snprintf(trace_path,sizeof(trace_path),"%s",path); }

static void run_ops(void) {
    int n;
    for(n=0;n<128;++n) {
        const VanguardOp *o=&vg_ops[vg.pc++];
        if(o->op==0) {
            int blend=o->c&255;
            vg.previous_motion=vg.motion; vg.previous_frame=vg.frame;
            vg.fade=blend>0?1:0; vg.fade_step=blend>0?1.0f/blend:1;
            vg.motion=vg.ids[o->a]; vg.frame=o->b==32767?motion_get(vg.motion)->loop_start:(float)o->b;
            vg.loop=(o->c>>8)&1;
        } else if(o->op==1) { vg.stop=o->b; vg.finish=o->a; return; }
        else if(o->op==2) { vg.cap_left[o->a]=o->b<0?100000:(float)o->b; vg.hit_done=0; }
        else vg.pc=o->b;
    }
    vg.done=1; trace("script_error");
}
static void play(int script) {
    vg.script=script; vg.done=0;
    memset(vg.cap_left,0,sizeof(vg.cap_left));
    if(script<0 || script>=108 || vg_scripts[script]<0) {vg.done=1;return;}
    vg.pc=vg_scripts[script];run_ops();
}
/* CEm008 command 3000 -> FUN_00254e80/FUN_00255420: script ID and START frame,
 * not a frame interval. Advance past waits preceding the requested start. */
static void play_from(int script,float frame) {
    int guard;
    play(script);vg.frame=frame;vg.fade=0;
    for(guard=0;guard<128 && !vg.done && vg.stop!=32767 && vg.stop<=frame;++guard) {
        if(vg.finish)vg.done=1;else run_ops();
    }
}
static void tick_script(float f,float root[3]) {
    const Motion *m=motion_get(vg.motion);
    float a[3],b[3],stop; int k;
    memset(root,0,3*sizeof(float));
    vg.fade=fmaxf(0,vg.fade-vg.fade_step*f);
    for(k=0;k<24;++k)vg.cap_left[k]=fmaxf(0,vg.cap_left[k]-f);
    if(vg.done || !m)return;
    stop=vg.stop==32767?m->end_frame:(float)vg.stop;
    motion_root(m,vg.frame,a);
    if(vg.loop && vg.stop==32767) {
        float next=vg.frame+f;
        if(next>stop) {
            next=motion_wrap(m,next);motion_root(m,stop,b);
            for(k=0;k<3;++k)root[k]=b[k]-a[k];
            motion_root(m,m->loop_start,a);
        }
        motion_root(m,next,b);
        for(k=0;k<3;++k)root[k]+=b[k]-a[k];
        vg.frame=next;return;
    }
    vg.frame=fminf(stop,vg.frame+f);
    motion_root(m,vg.frame,b);
    for(k=0;k<3;++k)root[k]=b[k]-a[k];
    if(vg.frame>=stop) {
        if(vg.finish)vg.done=1;
        else run_ops();
    }
}
static void idle(void) {
    vg.plan=24;vg.action=15;vg.phase=0;vg.timer=0;vg.hidden=0;vg.visible=1;vg.vy=0;
    play(1+(enemy_ai_random()%3));trace("idle");
}
static void action(int a) {
    vg.action=a;vg.phase=0;vg.timer=0;vg.hidden=0;
    if(a==16)play(47);
    else if(a==35)play(75);
    else if(a==72)play(102);
    else if(a==69 || a==70 || a==71 || a==73) {
        /* 0047e730/0047edf0 start in phase 10, distinct from the relocation wait. */
        vg.phase=(a==69 || a==70)?10:0;
        vg.hidden=1;vg.vy=0;memset(vg.cap_left,0,sizeof(vg.cap_left));
        fx_vanguard_event(702,vg.pos);trace("departure_fx");
    }
    trace("action");
}
static void decide(float distance,int alternate) {
    int band=distance<vg_bands[0]?0:distance<vg_bands[1]?1:2;
    const VanguardPick *r=&vg_picks[alternate][band];
    float roll=(float)enemy_ai_random()*(99.0f/4294967296.0f),total=0;int i;
    vg.gauge=0;
    /* The attack manager takes a three-second token only on a primary grant.
     * Renewing it on defensive plans starves the primary attack list forever. */
    if(!alternate)vg.token=180;
    for(i=0;i<r->count;++i) {total+=r->weight[i];if(roll<total)break;}
    if(i>=r->count)i=r->count-1;
    vg.plan=r->plan[i];vg.chain=0;
    action(vg.plan==27?16:vg.plan==40?35:vg.plan==56?72:71);
}
/* Select a valid floor at the original teleport radius. Keep the intended random direction first;
 * if blocked by stage geometry, try the remaining directions instead of placing inside a wall. */
static void warp_to(float px,float py,float pz,float radius) {
    float angle=ANGLE((float)(enemy_ai_random()&65535)),x,z,y,ox,oz;int i;
    for(i=0;i<16;++i) {
        float a=angle+i*(2*PI/16);
        x=px+sinf(a)*radius;z=pz+cosf(a)*radius;y=py;
        if(player_floor_fn && !player_floor_fn(x,py+400,z,0,&y))continue;
        /* Port collision adaptation: do not select another storey or a point
         * inside scenery. The original uppercut uses radius zero at Dante. */
        if(fabsf(y-py)>50)continue;
        ox=x;oz=z;stage_collide(&x,&z,y,vg_caps[0].radius);
        if((x-ox)*(x-ox)+(z-oz)*(z-oz)>100)continue;
        {
            float a[3]={px,py+100,pz},b[3]={x,y+100,z},t;
            /* Port safety: don't reappear behind solid scenery separating the
             * combatants. Keep the authored random angle when unobstructed. */
            if(stage_raycast(a,b,&t))continue;
        }
        vg.pos[0]=x;vg.pos[1]=y;vg.pos[2]=z;vg.ground=y;break;
    }
    vg.yaw=toward(px,pz);trace("warp");
}
/* Spatial substeps, rather than one endpoint push per animation frame. A rush
 * can cross a thin wall in a single authored frame. This is port collision,
 * not a translation of the PS2 collision engine. */
static void move_ground(float dx,float dz) {
    float distance=hypotf(dx,dz),radius=vg_caps[0].radius;
    int i,steps=(int)ceilf(distance/fmaxf(1,radius*.25f));
    if(steps<1)steps=1;
    for(i=0;i<steps;++i) {
        float x=vg.pos[0]+dx/steps,z=vg.pos[2]+dz/steps,y=vg.pos[1];
        stage_collide(&x,&z,y,radius);
        if(player_floor_fn) {
            if(!player_floor_fn(x,y+20,z,0,&y) || fabsf(y-vg.pos[1])>20)break;
        }
        vg.pos[0]=x;vg.pos[1]=y;vg.pos[2]=z;
    }
}
static void start_death(void) {vg.hp=0;vg.dying=1;vg.hidden=0;vg.visible=1;vg.plan=20;vg.action=26;vg.timer=0;play(29);trace("death");}
void vanguard_kill(void) {if(vg.active && !vg.dying)start_death();}
int vanguard_hit(int kind,int mortal) {
    int pistol=kind==HIT_PISTOL,stagger=1;
    float damage=pistol?VANGUARD_PISTOL_DAMAGE*(vg_difficulty==0?2:1):
        (kind==HIT_LIGHT || kind==HIT_AIR?42:84);
    if(!vg.active || vg.dying)return 0;
    if(vg.hidden) {trace("immune");return 0;}
    if(mortal)vg.hp=fmaxf(0,vg.hp-damage);
    if(pistol) {
        /* 002f17a0: tertiary strain, not a sword flinch for each bullet.
         * 0046c0f0 adds half the unscaled record damage on ordinary hits. */
        vg.strain-=VANGUARD_PISTOL_STRAIN;
        stagger=vg.strain<=0;
        if(stagger) {vg.strain=VANGUARD_STRAIN_MAX;trace("pistol_stagger");}
        else if(vg.action==15 || vg.action==35)vg.gauge+=VANGUARD_PISTOL_DAMAGE*.5f;
        trace("pistol_hit");
    } else vg.gauge+=kind==HIT_LIGHT?21:42;
    trace("hurt");
    if(vg.hp<=0) {start_death();return 1;}
    /* Attack states use reaction category 4. They keep their attack instead of being stun-locked. */
    if(stagger && (vg.action==15 || vg.action==35)) {vg.plan=0;vg.action=0;vg.phase=0;play(4+(enemy_ai_random()&1));trace("flinch");}
    return 0;
}
static int attack_test(float px,float py,float pz) {
    int i;
    if(vg.hidden || vg.dying || vg.hit_done)return 0;
    for(i=0;i<24;++i)if(vg.cap_left[i]>0) {
        const VanguardCap *c=&vg_caps[i];
        const Transform *t=&body[c->bone];
        float lx=t->p[0]+t->m[0][0]*c->x+t->m[0][1]*c->y+t->m[0][2]*c->z;
        float ly=t->p[1]+t->m[1][0]*c->x+t->m[1][1]*c->y+t->m[1][2]*c->z;
        float lz=t->p[2]+t->m[2][0]*c->x+t->m[2][1]*c->y+t->m[2][2]*c->z;
        float x=vg.pos[0]+cosf(vg.yaw)*lx+sinf(vg.yaw)*lz;
        float z=vg.pos[2]-sinf(vg.yaw)*lx+cosf(vg.yaw)*lz;
        float y=vg.pos[1]+ly,cy=fmaxf(py+20,fminf(py+160,y));
        float dx=px-x,dz=pz-z,dy=cy-y,r=c->radius+40;
        if(dx*dx+dz*dz+dy*dy<=r*r) {vg.hit_done=1;trace("landed");return 1;}
    }
    return 0;
}
static int step(float f,float px,float py,float pz,int attacks) {
    float root[3],dist=hypotf(px-vg.pos[0],pz-vg.pos[2]);int hit=0,k;
    ++vg.ticks;vg.timer+=f;vg.token=fmaxf(0,vg.token-f);
    tick_script(f,root);
    if(vg.dying) {pose();if(vg.done || vg.timer>180) {vg.active=0;trace("removed");}return 0;}
    if(vg.action==13) {if(vg.done)idle();}
    else if(vg.action==0) {
        turn(px,pz,512,f);
        /* FUN_00471ea0 ends the reaction at 30; FUN_0046cbf0 rank 0 selects plan 57. */
        if(vg.frame>=30 || vg.done) {vg.plan=57;action(71);}
    }
    else if(vg.action==15) {
        turn(px,pz,256,f);
        if(vg.done)play(1+enemy_ai_random()%3);
        if(attacks) {
            float d=fabsf(wrap(toward(px,pz)-vg.yaw));
            for(k=0;k<3;++k)if(dist<vg_zones[k][0] && d<vg_zones[k][1]*PI/180) {vg.gauge+=vg_zones[k][2]*f;break;}
            if(d>PI/2) {vg.plan=57;action(71);}
            else if(vg.gauge>100)decide(dist,vg.token>0);
        }
    } else if(vg.action==16) {
        if(vg.phase==0) {
            if(vg.frame>=20 && vg.frame<=40)turn(px,pz,546,f);
            if(vg.frame>=77) {vg.phase=1;play(101);}
        } else if(vg.done)idle();
    } else if(vg.action==35) {
        if(vg.done) {if(!vg.phase) {vg.phase=1;play(7);}else idle();}
    } else if(vg.action==72) {
        /* FUN_0047fbb0: wind-up ends at frame 70 of script 102, then one rush at rank 0. */
        if(vg.frame>=70)action((enemy_ai_random()&1)?69:70);
    } else if(vg.action==69 || vg.action==70) {
        /* Separate PS2 paths: 0047e840 (rush), 0047ef00 (uppercut).
         * Lowest-rank delays at 005cdd50/70 are 25 and 15, after phase 10's 30. */
        if(vg.phase==10 && vg.timer>30) {vg.visible=0;vg.phase=0;vg.timer=0;play(0);}
        else if(vg.phase==0 && vg.timer>25) {
            warp_to(px,py,pz,vg.action==69?400:0);vg.phase=1;vg.timer=0;
        }
        else if(vg.phase==1 && vg.timer>(vg.action==69?15:0)) {
            vg.phase=2;vg.timer=0;
            if(vg.action==70)play(105);
            fx_vanguard_event(vg.action==70?516:499,vg.pos);trace("arrival_fx");
        }
        else if(vg.phase==2 && vg.timer>(vg.action==69?30:50)) {
            vg.phase=3;vg.timer=0;vg.yaw=toward(px,pz);vg.hidden=0;vg.visible=1;trace("reveal");
            if(vg.action==69)play_from(102,202);
            else play(107);
            /* Do not apply the preceding motion's root delta to a new action. */
            memset(root,0,sizeof(root));
            trace("strike");
        } else if(vg.phase==3) {
            if(vg.action==69) {
                if(vg.done)action(73);
            } else if(vg.timer>3) {
                vg.phase=4;vg.vy=80;trace("jump");
            }
        } else if(vg.action==70 && (vg.phase==4 || vg.phase==5)) {
                /* 0047ef00 switches from -8 to -0.5 at the apex. */
                if(vg.phase==4 && vg.vy<0) {vg.phase=5;vg.vy=0;}
                vg.vy-=(vg.phase==4?8:.5f)*f;
                vg.pos[1]+=vg.vy*f;
                if(vg.pos[1]<=vg.ground && vg.vy<0) {
                    vg.pos[1]=vg.ground;vg.vy=0;if(vg.done)action(73);
                }
        }
    } else if(vg.action==71 || vg.action==73) {
        /* FUN_0047f5b0/0047fd10: disappear, relocate, face Dante, reveal after 30. */
        if(vg.phase==0 && vg.timer>30) {vg.visible=0;warp_to(px,py,pz,400);vg.phase=1;vg.timer=0;play(0);}
        else if(vg.phase==1 && vg.timer<=f) {fx_vanguard_event(499,vg.pos);trace("arrival_fx");}
        else if(vg.phase==1 && vg.timer>30) {
            vg.hidden=0;vg.visible=1;vg.yaw=toward(px,pz);vg.phase=2;
            if(vg.action==73) {play(103);trace("reveal");}else idle();
        } else if(vg.phase==2 && vg.done)idle();
    }
    if(!vg.hidden && vg.action!=70) {
        move_ground(root[0]*cosf(vg.yaw)+root[2]*sinf(vg.yaw),
                    root[2]*cosf(vg.yaw)-root[0]*sinf(vg.yaw));
    }
    pose();
    if(attacks)hit=attack_test(px,py,pz);
    if(vg.trace && vg.ticks%60==0)trace("sample");
    return hit;
}
int vanguard_update(float frames,float px,float py,float pz,int attacks,float pos[3],int *alive) {
    int hit=0;
    while(frames>0.0001f && vg.active) {float f=fminf(1,frames);hit|=step(f,px,py,pz,attacks);frames-=f;}
    memcpy(pos,vg.pos,sizeof(vg.pos));*alive=vg.active;return hit;
}
void vanguard_spawn(float x,float y,float z) {
    int trace_on=vg.trace,ids[80];memcpy(ids,vg.ids,sizeof(ids));memset(&vg,0,sizeof(vg));memcpy(vg.ids,ids,sizeof(ids));vg.trace=trace_on;
    vg.pos[0]=x;vg.pos[1]=y;vg.pos[2]=z;vg.ground=y;vg.hp=VANGUARD_HP;vg.active=1;vg.visible=1;vg.plan=14;vg.action=13;
    vg.strain=VANGUARD_STRAIN_MAX;
    vg.motion=vg.previous_motion=vg.ids[0];play(13);pose();trace("spawn");
}
int vanguard_hidden(void) {return vg.hidden;}
int vanguard_dying(void) {return vg.dying;}
float vanguard_health(void) {return vg.hp;}
float vanguard_max_health(void) {return VANGUARD_HP;}

int vanguard_init(void) {
    int i,k;
    for(i=0;i<80;++i) {char name[8];snprintf(name,sizeof(name),"%s-%02d",i<71?"vg":"vx",i<71?i:i-71);vg.ids[i]=motion_find(name);if(vg.ids[i]<0)return 0;}
    vg.motion=vg.ids[0];vg.frame=0;pose();memcpy(bind_body,body,sizeof(body));
    for(k=0;k<9;++k) {
        const VanguardMesh *m=&vanguard_parts[k];
        vertices[k]=memalign(16,sizeof(Vertex)*m->vertices);indices[k]=memalign(16,sizeof(unsigned short)*m->indices_count);
        if(!vertices[k] || !indices[k])return 0;
        memcpy(indices[k],m->indices,sizeof(unsigned short)*m->indices_count);
        for(i=0;i<m->vertices;++i) {vertices[k][i].u=m->uvs[i].u;vertices[k][i].v=m->uvs[i].v;}
    }
    for(i=0;i<2;++i) {
        const Dmc3Texture *t=&vanguard_textures[i];size_t size=(size_t)t->width*t->height;
        textures[i]=memalign(16,size);if(!textures[i])return 0;memcpy(textures[i],t->rgba,size);
        if(!swizzle_texture8(textures[i],t->width,t->height))return 0;
    }
    return 1;
}

static void skin_part(int part) {
    const VanguardMesh *m=&vanguard_parts[part];int i,j,k;
    /* 002b52f0: body bone table begins at +404. Robes 2/3 use +410
     * (bone 3), not bone 4; the hood uses +418 (bone 5). */
    static const int anchor[9]={0,5,3,3,8,12,14,14,9};
    Transform attachment=body[anchor[part]],local;
    Transform weapon_bones[2];
    if(part>0 && part<8) {
        int row,col;Transform bind_inverse;
        identity(&bind_inverse);
        for(row=0;row<3;++row)for(col=0;col<3;++col)bind_inverse.m[row][col]=bind_body[anchor[part]].m[col][row];
        multiply(&attachment,&attachment,&bind_inverse);
    }
    if(part==8) {
        static MotionSample weapon;
        int spinning=vg.action==69 && vg.phase==3 && vg.frame>=203;
        int returning=vg.action==73 && vg.phase==2;
        const Motion *motion=spinning?motion_get(vg.ids[76]):returning?motion_get(vg.ids[78]):0;
        if(!spinning && !returning) {
            euler(&local,0.207f,0,0);local.p[0]=-15;local.p[1]=-61.4f;local.p[2]=-18.93f;
            multiply(&attachment,&attachment,&local);
        }
        memset(&weapon,0,sizeof(weapon));
        if(motion)motion_sample(motion,spinning?vg.frame-203:vg.frame,&weapon);
        for(i=0;i<2;++i) {
            euler(&local,weapon.rot[i][0],weapon.rot[i][1],weapon.rot[i][2]);
            local.p[0]=weapon.has_trans[i][0]?weapon.trans[i][0]:m->local[i].x;
            local.p[1]=weapon.has_trans[i][1]?weapon.trans[i][1]:m->local[i].y;
            local.p[2]=weapon.has_trans[i][2]?weapon.trans[i][2]:m->local[i].z;
            multiply(&weapon_bones[i],m->parents[i]<0?&attachment:&weapon_bones[m->parents[i]],&local);
        }
    }
    for(i=0;i<m->vertices;++i) {
        Vertex *v=&vertices[part][i];float p[3]={0},n[3]={0};
        for(j=0;j<3;++j) {
            int b=m->bones[i][j],w=m->weights[i][j];const Transform *t;
            float src[3]={m->positions[i].x-m->rest[b].x,m->positions[i].y-m->rest[b].y,m->positions[i].z-m->rest[b].z};
            float ns[3]={m->normals[i].x,m->normals[i].y,m->normals[i].z};
            if(!w)continue;
            if(!part)t=&body[b];
            else if(part==8)t=&weapon_bones[b];
            else {
                /* Rest cloth rigs follow their native attachment. Sleeves are authored in body space. */
                identity(&local);local.p[0]=m->rest[b].x;local.p[1]=m->rest[b].y;local.p[2]=m->rest[b].z;
                if(part==4 || part==5) {
                    local.p[0]-=vanguard_parts[0].rest[anchor[part]].x;
                    local.p[1]-=vanguard_parts[0].rest[anchor[part]].y;
                    local.p[2]-=vanguard_parts[0].rest[anchor[part]].z;
                }
                multiply(&local,&attachment,&local);t=&local;
            }
            for(k=0;k<3;++k) {
                p[k]+=(t->p[k]+t->m[k][0]*src[0]+t->m[k][1]*src[1]+t->m[k][2]*src[2])*(w/31.0f);
                n[k]+=(t->m[k][0]*ns[0]+t->m[k][1]*ns[1]+t->m[k][2]*ns[2])*(w/31.0f);
            }
        }
        v->x=p[0];v->y=p[1];v->z=p[2];v->nx=n[0];v->ny=n[1];v->nz=n[2];
    }
    sceKernelDcacheWritebackRange(vertices[part],sizeof(Vertex)*m->vertices);
}
void vanguard_draw(void) {
    int part,i;
    if(!vg.active || !vg.visible)return;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    {ScePspFVector3 p={vg.pos[0],vg.pos[1],vg.pos[2]};sceGumTranslate(&p);}sceGumRotateY(vg.yaw);
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_LIGHTING);sceGuEnable(GU_ALPHA_TEST);
    if(no_texture)sceGuDisable(GU_TEXTURE_2D);
    if(no_lighting)sceGuDisable(GU_LIGHTING);
    sceGuColor(0xffffffffu);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuTexMode(GU_PSM_T8,0,0,1);sceGuClutMode(GU_PSM_8888,0,255,0);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    for(part=0;part<9;++part) {
        const VanguardMesh *m=&vanguard_parts[part];
        if(part==7)continue; /* alternate cloth model 15 is not drawn by CEm008's normal draw path */
        skin_part(part);
        for(i=0;i<m->objects_count;++i) {
            const Dmc3ModelObject *o=&m->objects[i];const Dmc3Texture *t=&vanguard_textures[o->texture];
            sceGuClutLoad(32,vanguard_palettes[o->texture]);sceGuTexImage(0,t->width,t->height,t->width,textures[o->texture]);sceGuTexFlush();
            sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_NORMAL_32BITF|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D,
                           o->index_count,indices[part]+o->index_start,vertices[part]);
        }
    }
    sceGuDisable(GU_ALPHA_TEST);
}
