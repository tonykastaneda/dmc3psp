#include "chess.h"
#include <string.h>
#include "chess_runtime.h"

float chess_wait(int est,int state,int rank,float unit) {
    if(est<35 || est>40 || state<0 || state>=14 || rank<0 || rank>3)return 0;
    return chess_waits[est-35][state][rank][0]+unit*chess_waits[est-35][state][rank][1];
}
float chess_alternate_chance(int est,int rank) {
    return est>=35 && est<=40 && rank>=0 && rank<4?chess_alternate_odds[est-35][rank]:0;
}

void chess_reset(ChessMaster *m) { memset(m,0,sizeof *m); }
float chess_wait_step(float distance,int accelerated) {
    if(distance<1000)distance=1000;
    return 3000/distance*(accelerated?2:1); /* 408320 */
}
static void command(ChessPiece *p,int value) { if(p->command<value)p->command=value; }
void chess_update(ChessMaster *m,ChessPiece *p,int count,float difficulty,float (*unit)(void)) {
    int i,j,k,king=0,severity=0,highest=0,damaged=-1,selected[4]={-1,-1,-1,-1};
    float age[4]={0,0,0,0};
    static const int weight[6]={1,8,9,10,11,12};
    m->count=m->busy=0;m->capacity=difficulty<700?1:difficulty<1000?2:3;
    for(i=0;i<count;++i) {
        if(!p[i].active || p[i].est<35 || p[i].est>40)continue;
        ++m->count; m->busy+=!!p[i].busy;
        if(p[i].est==40){king=1;m->king_seen=1;}
        if(p[i].hurt) {
            int w=weight[p[i].est-35];severity+=w;m->recent_hurt=10;
            if(w>highest && p[i].est!=38){highest=w;damaged=i;}
        }
    }
    if(m->recent_hurt)--m->recent_hurt;
    if(m->king_seen && !king) {
        if(--m->death_clock<=0) {
            m->death_clock=30;
            for(i=0;i<count;++i)if(p[i].active && p[i].est>=35 && p[i].est<=40){p[i].kill=1;break;}
        }
        return;
    }
    for(i=0;i<count;++i) {
        if(!p[i].active || p[i].est<35 || p[i].est>40 || !p[i].waiting || p[i].busy)continue;
        for(j=0;j<4;++j)if(age[j]<p[i].waited) {
            /* Native43f5e8 repeats the displaced slot into following slots. */
            for(k=j;k<3;++k){age[k+1]=age[j];selected[k+1]=selected[j];}
            age[j]=p[i].waited;selected[j]=i;break;
        }
    }
    for(i=0;i<m->capacity-m->busy && i<4;++i)if(selected[i]>=0) {
        ChessPiece *q=&p[selected[i]];command(q,10);
        /* At most two alternate commands per update. */
        if(m->recent_hurt && q->alternate_ready && unit && unit()*100<q->alternate_percent) {
            int alternates=0;
            for(j=0;j<i;++j)if(selected[j]>=0 && p[selected[j]].command==15)++alternates;
            if(alternates<2){command(q,15);q->alternate_ready=0;}
        }
    }
    if(severity>7 && damaged>=0 && p[damaged].est==40 && !p[damaged].swap_blocked) {
        for(i=0;i<count;++i)if(p[i].active && p[i].est==38 && !p[i].busy && p[i].swap_ready) {
            memcpy(p[i].destination,p[damaged].pos,sizeof p[i].destination);
            memcpy(p[damaged].destination,p[i].pos,sizeof p[i].destination);
            command(&p[i],30);command(&p[damaged],30);p[i].busy=p[damaged].busy=1;break;
        }
    }
    if(severity>7)for(i=0;i<count;++i)if(p[i].active && p[i].est==39)command(&p[i],20);
}
