#include "chess.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static float zero(void){return 0;}
int main(void) {
    ChessMaster m;ChessPiece p[6];int i;
    chess_reset(&m);memset(p,0,sizeof p);
    for(i=0;i<6;++i){p[i].est=35+i;p[i].active=1;p[i].waiting=1;p[i].waited=i+1;}
    chess_update(&m,p,6,699,zero);assert(m.capacity==1 && p[5].command==10 && !p[4].command);
    for(i=0;i<6;++i)p[i].command=0;
    p[5].busy=1;chess_update(&m,p,6,700,zero);assert(m.capacity==2 && p[4].command==10 && !p[3].command);
    for(i=0;i<6;++i)p[i].command=0;
    p[5].busy=0;p[5].hurt=1;p[3].swap_ready=1;
    p[5].pos[0]=200;p[3].pos[0]=900;
    chess_update(&m,p,6,1000,zero);
    assert(m.capacity==3 && p[3].command==30 && p[5].command==30 && p[4].command==20);
    assert(p[3].destination[0]==200 && p[5].destination[0]==900);
    p[5].active=0;chess_update(&m,p,6,0,zero);assert(p[0].kill);
    p[0].active=0;for(i=0;i<29;++i)chess_update(&m,p,6,0,zero);assert(!p[1].kill);
    chess_update(&m,p,6,0,zero);assert(p[1].kill);
    assert(chess_wait_step(500,0)==3 && chess_wait_step(2000,1)==3);
    assert(chess_wait(35,0,0,0)>=0);
    puts("Chess master: capacities, wait priority, Queen response, castling, King death cascade passed");
    return 0;
}
