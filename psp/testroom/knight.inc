/* CEm017 EST36:4349e0, hop434de0, close strike434fc0, plunge435170,
 * recovery435370. Native scripts/caps/records exported by prepare_chess.py. */
static int knight_fx[ENEMY_MAX];
static void knight_state(int state) {
    ChessPiece *p=enemy_chess_actor();
    e->action=state;e->step=0;e->timer=0;p->waiting=0;p->busy=state!=0;
    if(!state) {
        p->command=0;p->waited=0;e->wake=chess_wait(36,0,0,ai_unit());
    }
    if(state!=3)script_play(state);
    if(state==2 || state==4) {
        e->vel[0]=cn_hop[0];e->vel[2]=cn_hop[1];
        if(state==4)e->shoot_set=3+(ai_random()&3);else e->shoot_set=0;
    }
    if(state==8){e->vel[0]=-5;e->vel[2]=0;
        knight_fx[e-enemies]=nfx_spawn(NFX_CHESS,0x396,e->pos,e->yaw,1);
    }
    trace_row(0);
}
static void knight_spawn(void) {
    int i;char name[8];ChessPiece *p=enemy_chess_actor();
    for(i=0;i<4;++i){snprintf(name,sizeof name,"cn%02d",i);cn_motion_map[i]=motion_find(name);}
    p->est=36;p->alternate_percent=chess_alternate_chance(36,0);
    e->plan=P_WATCH;e->shoot_timer[0]=0;e->shoot_timer[1]=0;knight_state(0);
}
static int knight_close(float px,float pz) {
    return hypotf(px-e->pos[0],pz-e->pos[2])<300 && fabsf(angle_to(px,pz))<ANGLE(80*65536.0f/360.0f);
}
static void knight_death(void) {
    float at[3]={e->pos[0],e->pos[1]+250,e->pos[2]};int i;
    e->plan=P_DEATH;e->timer=0;enemy_chess_actor()->busy=0;
    e->hit_left[0]=e->hit_left[1]=0;
    /* Native406d50: immediate shatter,60-frame actor lifetime. */
    nfx_kill(knight_fx[e-enemies]);knight_fx[e-enemies]=0;
    nfx_spawn(NFX_CHESS,0x1fb,e->pos,e->yaw,0);
    nfx_spawn(NFX_CHESS,0x20c,at,e->yaw,0);
    (void)i;
    died();trace_row(0);
}
static void knight_update(float f,float px,float py,float pz) {
    ChessPiece *p=enemy_chess_actor();float root[3],g;
    if(floor_under(e->pos[0],e->pos[1]+100,e->pos[2],&g))e->floor_y=g;
    if(p->kill && e->plan!=P_DEATH){p->kill=0;knight_death();}
    if(e->plan==P_DEATH){e->timer+=f;if(e->timer>=60)e->active=0;return;}
    e->shoot_timer[0]-=f;e->shoot_timer[1]-=f;p->alternate_ready=e->shoot_timer[1]<=0;
    if(knight_fx[e-enemies])nfx_move(knight_fx[e-enemies],e->pos,e->yaw);
    if(e->action==0 && e->shoot_timer[0]<=0 && knight_close(px,pz)) {
        knight_state(3);e->shoot_timer[0]=120;
    }
    if(e->action!=3 || e->step)script_tick(f,root);
    e->timer+=f;
    switch(e->action) {
    case 0:
        e->timer+=f*(chess_wait_step(hypotf(px-e->pos[0],pz-e->pos[2]),0)-1);
        if(e->timer>e->wake){p->waiting=1;p->waited+=f;
            if(p->command>=10){int action=p->command==15?4:2;if(action==4)e->shoot_timer[1]=300+300*ai_unit();p->command=0;knight_state(action);}}
        break;
    case 2:case 4:
        if(!e->step){
            turn(px,pz,cn_turn[e->action]*65536.0f/360.0f,f);
            e->vel[0]+=cn_hop[2]*f;e->vel[2]+=cn_hop[3]*f;
            move_local(0,e->vel[0]*f,e->vel[2]*f);
            if(e->vel[0]<=0){e->step=1;e->timer=0;}
        } else if(e->timer>=30)knight_state(8);
        break;
    case 8:
        e->vel[0]-=3.2f*f;move_local(0,e->vel[0]*f,0);
        if(e->pos[1]<=e->floor_y){float at[3]={e->pos[0],e->floor_y,e->pos[2]};
            e->pos[1]=e->floor_y;fx_hit_spark(at);
            nfx_kill(knight_fx[e-enemies]);knight_fx[e-enemies]=0;
            if(e->shoot_set>0 && --e->shoot_set>0){int repeats=e->shoot_set;knight_state(4);e->shoot_set=repeats;}
            else knight_state(9);}
        break;
    case 9:
        if(e->frame>28 && e->frame<58)turn(px,pz,cn_turn[9]*65536.0f/360.0f,f);
        move_local(root[0],root[1],root[2]);
        if(e->done){e->pos[1]=e->floor_y;knight_state(knight_close(px,pz)?3:0);}
        break;
    case 3:
        if(!e->step && e->timer>=30){e->step=1;script_play(3);}
        else if(e->step && e->done)knight_state(0);
        break;
    }
    if(e->action==3 && e->step && e->frame>=53 && !knight_fx[e-enemies])
        knight_fx[e-enemies]=nfx_spawn(NFX_CHESS,0x241,e->pos,e->yaw,0);
    if(e->action!=3 && e->action!=8 && knight_fx[e-enemies]){nfx_kill(knight_fx[e-enemies]);knight_fx[e-enemies]=0;}
    if(attacks_on)test_attack(px,py,pz);
}
static void knight_hit(HitKind kind) {
    float at[3]={e->pos[0],e->pos[1]+200,e->pos[2]};
    if(e->plan==P_DEATH)return;
    ++hits;fx_hit_spark(at);
    /* Health is native1000; damage uses the existing ordinary-enemy hit units. */
    if(mortal)e->health-=kind==HIT_PISTOL?(ai_unit()<.2f):(kind==HIT_LIGHT || kind==HIT_AIR)?1:2;
    if(e->health<=0)knight_death();
}
