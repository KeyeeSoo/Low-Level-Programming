/* Deterministic server-authoritative countdown tests (no DDS/GLFW needed). */
#include "GameRules.h"
#include <assert.h>
#include <stdio.h>

static void place_at(Player *p,int x,int y){p->x=x-SQ_WIDTH/2;p->y=y-SQ_WIDTH/2;}
static void setup(GameRules *g){
    game_init_seed(g,12345);
    game_tick(g,100000);
    assert(game_join(g,111)==1);
    game_tick(g,200000);  /* Waiting for the second player must not expire. */
    assert(g->time_limit_ms==0 && g->mistakes==0);
    assert(game_join(g,222)==2);
    assert(g->phase==PHASE_COLLECT);
    assert(g->time_limit_ms==COLLECT_TIME_MS);
    assert(g->time_remaining_ms==COLLECT_TIME_MS);
    assert(g->deadline_ms==200000u+COLLECT_TIME_MS);
}
static void deliver(GameRules *g){
    int collector=game_collector(g),item=g->recipe[g->stage];
    int token=collector==1?111:222;
    place_at(&g->players[collector-1],ingredient_x[item],ingredient_y[item]);
    game_action(g,collector,token,ACTION_INTERACT,g->session);
    assert(g->held[collector-1]==item);
    place_at(&g->players[collector-1],cauldron_x,cauldron_y);
    game_action(g,collector,token,ACTION_INTERACT,g->session);
    assert(g->phase==PHASE_TRACE);
}
static void test_collect_timeout(void){
    GameRules g;setup(&g);
    game_tick(&g,200000u+COLLECT_TIME_MS-1);
    assert(g.phase==PHASE_COLLECT && g.time_remaining_ms==1);
    game_tick(&g,200000u+COLLECT_TIME_MS);
    assert(g.phase==PHASE_COLLECT && g.mistakes==1);
    assert(g.time_remaining_ms==COLLECT_TIME_MS);
    assert(g.session==1);
    assert(g.held[0]==-1 && g.held[1]==-1);
    game_tick(&g,g.deadline_ms);
    assert(g.phase==PHASE_COLLECT && g.mistakes==2);
    game_tick(&g,g.deadline_ms);
    assert(g.phase==PHASE_LOSE && g.mistakes==3);
    assert(g.time_limit_ms==0 && g.time_remaining_ms==0);
    game_tick(&g,999999);
    assert(g.mistakes==3);
}
static void test_trace_and_stir_timers(void){
    GameRules g;setup(&g);
    deliver(&g);
    assert(g.time_limit_ms==TRACE_TIME_MS);
    int trace_session=g.session;
    game_tick(&g,g.deadline_ms);
    assert(g.phase==PHASE_TRACE && g.mistakes==1);
    assert(g.session==trace_session+1 && g.time_remaining_ms==TRACE_TIME_MS);
    int tracer=game_tracer(&g);
    int tracer_token=tracer==1?111:222;
    game_action(&g,tracer,tracer_token,ACTION_TRACE_OK,trace_session);
    assert(g.phase==PHASE_TRACE); /* Late result from expired attempt rejected. */
    game_action(&g,tracer,tracer_token,ACTION_TRACE_OK,g.session);
    assert(g.phase==PHASE_STIR && g.time_limit_ms==STIR_TIME_MS);
    int stir_session=g.session;
    game_tick(&g,g.deadline_ms);
    assert(g.phase==PHASE_STIR && g.mistakes==2);
    int stirrer=game_stirrer(&g),token=stirrer==1?111:222;
    game_action(&g,stirrer,token,ACTION_STIR_OK,stir_session);
    assert(g.phase==PHASE_STIR); /* Late stir also rejected. */
    game_action(&g,stirrer,token,ACTION_STIR_OK,g.session);
    assert(g.phase==PHASE_COLLECT && g.stage==1);
    assert(g.time_limit_ms==COLLECT_TIME_MS);
    assert(g.mistakes==2);
}
static void test_trace_failure_timer_restarts(void){
    GameRules g;setup(&g);deliver(&g);
    game_tick(&g,g.clock_ms+12000);
    int old_session=g.session;
    int tracer=game_tracer(&g),token=tracer==1?111:222;
    game_action(&g,tracer,token,ACTION_TRACE_BAD,old_session);
    assert(g.phase==PHASE_TRACE && g.mistakes==1);
    assert(g.time_remaining_ms==TRACE_TIME_MS && g.session==old_session+1);
    game_action(&g,tracer,token,ACTION_TRACE_OK,old_session);
    assert(g.phase==PHASE_TRACE);
}
static void test_next_recipe_resets_timer_and_mistakes(void){
    GameRules g;setup(&g);
    /* Complete exactly one variable-length recipe. */
    int len=g.recipe_len;
    for(int stage=0;stage<len;stage++){
        deliver(&g);
        int tracer=game_tracer(&g),tracer_token=tracer==1?111:222;
        game_action(&g,tracer,tracer_token,ACTION_TRACE_OK,g.session);
        assert(g.phase==PHASE_STIR && g.time_limit_ms==STIR_TIME_MS);
        int stirrer=game_stirrer(&g),token=stirrer==1?111:222;
        game_action(&g,stirrer,token,ACTION_STIR_OK,g.session);
    }
    assert(g.round==1 && g.recipes_completed==1);
    assert(g.phase==PHASE_COLLECT && g.stage==0);
    assert(g.time_limit_ms==COLLECT_TIME_MS && g.mistakes==0);
}
int main(void){
    test_collect_timeout();
    test_trace_and_stir_timers();
    test_trace_failure_timer_restarts();
    test_next_recipe_resets_timer_and_mistakes();
    puts("PASS: collect/trace/stir deadlines, player waiting, retries, stale actions, next recipe");
    return 0;
}
