/* Thin adapter for the native Rook brain, posed beam origins and shared hit bridge. */
#include "rook.h"
static RookBrain rook_brain[ENEMY_MAX];
static int rook_motions[ROOK_M_COUNT]={-2};
static int rook_beam_fx[ENEMY_MAX][4],rook_castle_fx[ENEMY_MAX];
static void rook_play(int restart) {
 RookBrain *b=&rook_brain[e-enemies];int i,id;
 if(rook_motions[0]==-2)for(i=0;i<ROOK_M_COUNT;++i)rook_motions[i]=motion_find(rook_motion_names[i]);
 id=rook_motions[b->want];if(id<0)return;
 if(id!=e->motion || restart){e->prev_motion=e->motion;e->prev_frame=e->frame;
  e->fade=e->motion>=0?1:0;e->fade_rate=1.0f/8;e->motion=id;e->frame=0;e->done=0;}
}
static void rook_spawn_glue(void) {
 RookBrain *b=&rook_brain[e-enemies];ChessPiece *p=enemy_chess_actor();int i;
 for(i=0;i<4;++i){nfx_kill(rook_beam_fx[e-enemies][i]);rook_beam_fx[e-enemies][i]=0;}
 nfx_kill(rook_castle_fx[e-enemies]);rook_castle_fx[e-enemies]=0;
 memcpy(b->pos,e->pos,sizeof b->pos);b->yaw=e->yaw;b->floor_y=e->floor_y;
 rook_spawn(b,e->health,chess_room==401 || chess_room==441,ai_unit);
 p->est=38;p->alternate_percent=chess_alternate_chance(38,0);
 e->plan=P_WATCH;e->action=0;e->motion=-1;rook_play(1);b->restart=0;e->fade=0;
}
static void rook_spawn_param(Enemy *a,int index,int value) {
 if(index==0){RookBrain *b=&rook_brain[a-enemies];b->flags=b->board?3:value;}
}
static int rook_test_state(int state) {
 if(!e->active || e->kind!=ENEMY_ROOK || (state!=0 && state!=2 && state!=3 && state!=4 && state!=7))return 0;
 RookBrain *b=&rook_brain[e-enemies];rook_force(b,enemy_chess_actor(),state,ai_unit);
 rook_play(1);b->restart=0;return 1;
}
static void rook_shatter(void) {
 RookBrain *b=&rook_brain[e-enemies];int i;float at[3]={e->pos[0],e->pos[1]+50,e->pos[2]};
 e->plan=P_DEATH;b->timer=0;enemy_chess_actor()->busy=0;
 for(i=0;i<4;++i){b->beam_age[i]=-1;nfx_kill(rook_beam_fx[e-enemies][i]);rook_beam_fx[e-enemies][i]=0;}
 nfx_kill(rook_castle_fx[e-enemies]);rook_castle_fx[e-enemies]=0;
 nfx_spawn(NFX_CHESS,0x1fb,e->pos,e->yaw,0);nfx_spawn(NFX_CHESS,0x36a,at,e->yaw,0);
 died();trace_row(0);
}
static void rook_summon(int est) {
 int i;Enemy *owner=e;
 for(i=0;i<ENEMY_MAX && enemies[i].active;++i){}
 if(i>=ENEMY_MAX)return;
 if(enemy_add_class(est,e->pos[0],e->pos[2])) {
  Enemy *a=&enemies[i];a->pos[1]=a->floor_y=owner->floor_y;a->yaw=owner->yaw;
  a->health=owner->health; /* 437040 passes currentHP as the new spawn's override. */
  if(a->kind==ENEMY_BISHOP)bishop_brain[i].health=a->health;
  if(a->kind==ENEMY_ROOK)rook_brain[i].health=a->health;
  nfx_spawn(NFX_CHESS,0x1ee,owner->pos,owner->yaw,0);
 }
}
static void rook_update_glue(float f,float px,float py,float pz) {
 RookBrain *b=&rook_brain[e-enemies];ChessPiece *p=enemy_chess_actor();
 const Motion *m=motion_get(e->motion);float g,c,s;int i,k,was=e->action;
 static const int bones[4]={7,9,3,5};
 if(e->fade>0){e->fade-=e->fade_rate*f;if(e->fade<0)e->fade=0;}
 if(m){float end=b->want==ROOK_M_IDLE?1:m->end_frame;e->frame+=f;if(e->frame>=end){e->done=1;e->frame=end;}}
 memcpy(b->pos,e->pos,sizeof b->pos);b->yaw=e->yaw;b->health=e->health;
 if(floor_under(e->pos[0],e->pos[1]+100,e->pos[2],&g))e->floor_y=g;
 b->floor_y=e->floor_y;b->frame=e->frame;b->done=e->done;b->count=b->board?enemy_alive_count():chess_master.count;b->blocked=0;
 for(i=0;i<ENEMY_MAX;++i)if(i!=e-enemies && chess_actors[i].active &&
    ((enemies[i].kind==ENEMY_KNIGHT && (enemies[i].action==3 || enemies[i].action==8)) ||
     ((enemies[i].kind==ENEMY_BISHOP || enemies[i].kind==ENEMY_ROOK) && enemies[i].action>=3)))b->blocked=1;
 evaluate_pose();c=cosf(e->yaw);s=sinf(e->yaw);
 for(i=0;i<4;++i){float *v=world_pos[bones[i]];
  b->beam_pos[i][0]=e->pos[0]+c*v[0]+s*v[2];b->beam_pos[i][1]=e->pos[1]+v[1];b->beam_pos[i][2]=e->pos[2]-s*v[0]+c*v[2];}
 rook_think(b,p,f,px,py,pz,ai_unit,attacks_on && e->plan!=P_DEATH);
 memcpy(e->pos,b->pos,sizeof e->pos);e->yaw=b->yaw;e->action=b->action;e->health=(short)b->health;
 if(b->health<=0 && e->plan!=P_DEATH)rook_shatter();
 if(b->restart || rook_motions[b->want]!=e->motion){rook_play(b->restart);b->restart=0;}
 for(i=0;i<4;++i){int *fx=&rook_beam_fx[e-enemies][i];float a=e->yaw+(-180+90*i)*PI/180;
  if(b->beam_event){nfx_kill(*fx);*fx=nfx_spawn(NFX_CHESS,0x1e3,b->beam_pos[i],a,1);}
  if(b->beam_age[i]<0){nfx_kill(*fx);*fx=0;}else nfx_move(*fx,b->beam_pos[i],a);}
 if(b->castle==1)rook_castle_fx[e-enemies]=nfx_spawn(NFX_CHESS,0x390,e->pos,e->yaw,1);
 if(b->castle==2){nfx_kill(rook_castle_fx[e-enemies]);rook_castle_fx[e-enemies]=0;nfx_spawn(NFX_CHESS,0x1fa,e->pos,e->yaw,0);e->placed=1;}
 if(rook_castle_fx[e-enemies])nfx_move(rook_castle_fx[e-enemies],e->pos,e->yaw);
 if(b->summon)rook_summon(b->summon);
 if(b->land){land_attack(chess_rook_attack[1]>=500);landed_damage=chess_rook_attack[0];landed_kind=(int)chess_rook_attack[2];}
 if(b->remove)e->active=0;
 if(was!=e->action)trace_row(0);
 /* Tests: the Rook's action changes, beams, hits, summons and castles in the FOCUS_DONE report (enemy_giga_log). */
 if((was!=e->action || b->beam_event || b->land || b->summon || b->castle) && giga_log_len<(int)sizeof(giga_log_text)-120)
  giga_log_len+=snprintf(giga_log_text+giga_log_len,sizeof(giga_log_text)-giga_log_len,"rook%d action=%d beam=%d land=%d summon=%d castle=%d at %.0f %.0f hp=%d\n",
   (int)(e-enemies),e->action,b->beam_event,b->land,b->summon,b->castle,e->pos[0],e->pos[2],e->health);
 (void)k;
}
static void rook_hit_glue(HitKind kind) {
 RookBrain *b=&rook_brain[e-enemies];float at[3]={e->pos[0],e->pos[1]+150,e->pos[2]};
 if(e->plan==P_DEATH || b->action==11)return;
 ++hits;fx_hit_spark(at);
 if(mortal)e->health-=kind==HIT_PISTOL?(ai_unit()<.2f):(kind==HIT_LIGHT || kind==HIT_AIR)?1:2;
 b->health=e->health;if(e->health<=0)rook_shatter();
}
