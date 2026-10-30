/* Headless acceptance test: randomized ingredient decides rune, reverse tracing
   and stirring, two-player roles, and automatic next recipe. */
#include "GameRules.h"
#include "Trace.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846f
static TraceSession trace;
static void prepare(int item){
    if(ingredient_rune[item]==0){
        int order[5]={0,2,4,1,3};TracePoint v[5];
        for(int i=0;i<5;i++){
            float a=-PI/2+2*PI*order[i]/5;
            v[i]=(TracePoint){500+125*cosf(a),355+125*sinf(a)};
        }
        assert(trace_set_path(&trace,v,5,1));trace.shape=TRACE_STAR;
    }else{
        TraceShape shape=(ingredient_rune[item]==1)?TRACE_CIRCLE:
            (ingredient_rune[item]==2)?TRACE_FLOWER:TRACE_RECTANGLE;
        assert(trace_set_shape(&trace,shape,500,355));
    }
}
static void stir(void){
    TracePoint v[180];
    for(int i=0;i<180;i++){
        float a=-PI/2+2*PI*i/180;
        v[i]=(TracePoint){500+104*cosf(a),355+104*sinf(a)};
    }
    assert(trace_set_path(&trace,v,180,1));trace.shape=TRACE_CIRCLE;
}
static void reverse_trace(void){
    int n=trace.target_count;assert(n>2);
    TracePoint original[TRACE_MAX_TARGET_POINTS];
    for(int i=0;i<n;i++)original[i]=trace.target[i];
    trace_mouse_down(&trace,original[0].x,original[0].y);
    for(int i=n-2;i>=0;i--)trace_mouse_move(&trace,original[i].x,original[i].y);
    trace_mouse_up(&trace,original[0].x,original[0].y);
    assert(trace.direction==-1 && trace.status==TRACE_SUCCESS);
}
static void place_at(Player *p,int x,int y){p->x=x-SQ_WIDTH/2;p->y=y-SQ_WIDTH/2;}
int main(void){
    GameRules g;game_init_seed(&g,149);
    game_join(&g,111);game_join(&g,222);
    int first_len=g.recipe_len;
    for(int i=0;i<first_len+1;i++){
        int collector=game_collector(&g),tracer=game_tracer(&g);
        int item=g.recipe[g.stage];int token[2]={111,222};
        place_at(&g.players[collector-1],ingredient_x[item],ingredient_y[item]);
        game_action(&g,collector,token[collector-1],ACTION_INTERACT,0);
        place_at(&g.players[collector-1],cauldron_x,cauldron_y);
        game_action(&g,collector,token[collector-1],ACTION_INTERACT,0);
        assert(g.phase==PHASE_TRACE && g.actor==tracer);
        int session=g.session;
        prepare(item);reverse_trace();
        game_action(&g,tracer,token[tracer-1],ACTION_TRACE_OK,session);
        assert(g.phase==PHASE_STIR && g.actor==collector);
        stir();reverse_trace();
        game_action(&g,collector,token[collector-1],ACTION_STIR_OK,session);
        assert(g.phase==PHASE_COLLECT);
    }
    assert(g.round==1 && g.stage==1 && g.recipes_completed==1);
    puts("PASS: ingredient-specific reverse tracing + co-op stirring + next recipe");
}
