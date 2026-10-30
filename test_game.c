#include "GameRules.h"
#include "Trace.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static void trace_full(TraceSession *s){
    trace_mouse_down(s,s->target[0].x,s->target[0].y);
    assert(s->status==TRACE_DRAWING);
    for(int i=1;i<s->target_count;i++)trace_mouse_move(s,s->target[i].x,s->target[i].y);
    trace_mouse_up(s,s->target[s->target_count-1].x,s->target[s->target_count-1].y);
    assert(s->status==TRACE_SUCCESS);
}
static void shapes(void){
    static TraceSession t;
    for(int shape=TRACE_CIRCLE;shape<=TRACE_FLOWER;shape++){
        assert(trace_set_shape(&t,(TraceShape)shape,500,355));trace_full(&t);
    }
    TracePoint v[5];int order[5]={0,2,4,1,3};
    for(int i=0;i<5;i++){
        float angle=-3.14159265f/2+2*3.14159265f*order[i]/5;
        v[i]=(TracePoint){500+125*cosf(angle),355+125*sinf(angle)};
    }
    assert(trace_set_path(&t,v,5,1));trace_full(&t);
    trace_mouse_down(&t,20,20);assert(t.status==TRACE_SUCCESS);
}
int main(void){
    GameRules g;game_init(&g);
    assert(game_join(&g,123)==1);
    assert(game_join(&g,123)==1);assert(g.num_players==1);
    assert(game_join(&g,456)==2);assert(game_join(&g,789)==0);
    int x=g.players[0].x;
    game_move(&g,0,123,'r');game_move(&g,3,123,'r');game_move(&g,1,999,'r');
    assert(g.players[0].x==x);
    game_move(&g,1,123,'r');assert(g.players[0].x==x+STEP);
    /* Move to Moonflower, pick it up, move to cauldron and add. */
    g.players[0].x=ingredient_x[0]-SQ_WIDTH/2;
    g.players[0].y=ingredient_y[0]-SQ_WIDTH/2;
    game_action(&g,1,123,ACTION_INTERACT,0);assert(g.held[0]==0);
    g.players[0].x=cauldron_x-SQ_WIDTH/2;
    g.players[0].y=cauldron_y-SQ_WIDTH/2;
    game_action(&g,1,123,ACTION_INTERACT,0);
    assert(g.phase==PHASE_TRACE && g.actor==1);
    int session=g.session;
    game_action(&g,2,456,ACTION_TRACE_OK,session);assert(g.phase==PHASE_TRACE);
    game_action(&g,1,123,ACTION_TRACE_OK,session-1);assert(g.phase==PHASE_TRACE);
    game_action(&g,1,123,ACTION_TRACE_BAD,session);
    assert(g.mistakes==1);
    game_action(&g,1,123,ACTION_TRACE_OK,session);
    assert(g.phase==PHASE_STIR);
    game_action(&g,1,123,ACTION_STIR_OK,session);
    assert(g.phase==PHASE_COLLECT && g.stage==1);
    shapes();
    puts("Passed: join deduplication, invalid IDs, movement, potion rules and tracing.");
}
