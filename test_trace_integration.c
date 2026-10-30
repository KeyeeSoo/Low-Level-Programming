/* Headless acceptance test for actual potion-stage transition and reversed
   five-point pentagram & stirring, without DDS or OpenGL runtime. */
#include "GameRules.h"
#include "Trace.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846f
static TraceSession trace;
static void pentagram(void) {
    int order[5] = {0,2,4,1,3};
    TracePoint v[5];
    for(int i=0;i<5;i++){
        float a=-PI/2.0f+2.0f*PI*order[i]/5.0f;
        v[i]=(TracePoint){500+125*cosf(a),355+125*sinf(a)};
    }
    assert(trace_set_path(&trace,v,5,1));
    trace.shape=TRACE_STAR;
}
static void stir_circle(void) {
    TracePoint v[180];
    for(int i=0;i<180;i++){
        float a=-PI/2.0f+2.0f*PI*i/180.0f;
        v[i]=(TracePoint){500+104*cosf(a),355+104*sinf(a)};
    }
    assert(trace_set_path(&trace,v,180,1));
    trace.shape=TRACE_CIRCLE;
}
static void trace_reverse(void) {
    int n=trace.target_count;
    assert(n>2);
    TracePoint original[TRACE_MAX_TARGET_POINTS];
    for(int i=0;i<n;i++) original[i]=trace.target[i];
    trace_mouse_down(&trace,original[0].x,original[0].y);
    for(int i=n-2;i>=0;i--) trace_mouse_move(&trace,original[i].x,original[i].y);
    trace_mouse_up(&trace,original[0].x,original[0].y);
    assert(trace.direction==-1);
    assert(trace.status==TRACE_SUCCESS);
}
int main(void){
    GameRules g;
    game_init(&g);
    assert(game_join(&g,111)==1);
    assert(game_join(&g,222)==2);
    g.players[0].x=ingredient_x[0]-SQ_WIDTH/2;
    g.players[0].y=ingredient_y[0]-SQ_WIDTH/2;
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.held[0]==0);
    g.players[0].x=cauldron_x-SQ_WIDTH/2;
    g.players[0].y=cauldron_y-SQ_WIDTH/2;
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.phase==PHASE_TRACE && g.actor==1);
    int session=g.session;
    pentagram();trace_reverse();
    game_action(&g,1,111,ACTION_TRACE_OK,session);
    assert(g.phase==PHASE_STIR);
    stir_circle();trace_reverse();
    game_action(&g,1,111,ACTION_STIR_OK,session);
    assert(g.phase==PHASE_COLLECT && g.stage==1);
    puts("PASS: reverse rune -> DDS action equivalent -> stir -> next recipe stage");
}
