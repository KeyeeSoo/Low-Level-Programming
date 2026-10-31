/* Standalone recipe progression/permissions tests, no DDS or GLFW. */
#include "GameRules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void place_at(Player *p,int x,int y){p->x=x-SQ_WIDTH/2;p->y=y-SQ_WIDTH/2;}
static void assert_permutation(const GameRules *g){
    int mask=0;
    assert(g->recipe_len>=RECIPE_MIN_INGREDIENTS && g->recipe_len<=RECIPE_MAX_INGREDIENTS);
    for(int i=0;i<g->recipe_len;i++){
        assert(g->recipe[i]>=0 && g->recipe[i]<INGREDIENT_COUNT);
        assert(!(mask&(1<<g->recipe[i])));
        mask|=1<<g->recipe[i];
    }
    assert(mask!=0);
    for(int i=g->recipe_len;i<RECIPE_MAX_INGREDIENTS;i++)assert(g->recipe[i]==-1);
}
static void full_stage(GameRules *g){
    const int ids[2]={111,222};
    int collector=game_collector(g),tracer=game_tracer(g);
    int item=g->recipe[g->stage];
    int stage=g->stage,round=g->round,len=g->recipe_len;
    assert_permutation(g);
    assert(collector!=tracer);
    /* Other player must not be able to pick up ingredients. */
    place_at(&g->players[tracer-1],ingredient_x[item],ingredient_y[item]);
    game_action(g,tracer,ids[tracer-1],ACTION_INTERACT,0);
    assert(g->held[tracer-1]==-1);
    /* Collector gets current ingredient and adds it to cauldron. */
    place_at(&g->players[collector-1],ingredient_x[item],ingredient_y[item]);
    game_action(g,collector,ids[collector-1],ACTION_INTERACT,0);
    assert(g->held[collector-1]==item);
    place_at(&g->players[collector-1],cauldron_x,cauldron_y);
    game_action(g,collector,ids[collector-1],ACTION_INTERACT,0);
    assert(g->phase==PHASE_TRACE && g->actor==tracer);
    int session=g->session;
    game_action(g,collector,ids[collector-1],ACTION_TRACE_OK,session);
    game_action(g,tracer,ids[tracer-1],ACTION_TRACE_OK,session-1);
    assert(g->phase==PHASE_TRACE);
    game_action(g,tracer,ids[tracer-1],ACTION_TRACE_OK,session);
    assert(g->phase==PHASE_STIR && g->actor==collector);
    game_action(g,tracer,ids[tracer-1],ACTION_STIR_OK,session);
    assert(g->phase==PHASE_STIR);
    game_action(g,collector,ids[collector-1],ACTION_STIR_OK,session);
    assert(g->used_mask&(1<<item) || g->phase==PHASE_COLLECT || g->phase==PHASE_WIN);
    if(stage<len-1){
        assert(g->phase==PHASE_COLLECT && g->stage==stage+1 && g->round==round);
        assert(g->used_mask&(1<<item));
    }else if(round<RECIPES_TO_WIN-1){
        assert(g->phase==PHASE_COLLECT && g->stage==0 && g->round==round+1);
        assert(g->used_mask==0 && g->held[0]==-1 && g->held[1]==-1);
        assert(g->recipes_completed==round+1);
    }else{
        assert(g->phase==PHASE_WIN && g->recipes_completed==RECIPES_TO_WIN);
    }
}
static void test_all_rounds(void){
    GameRules a,b;
    game_init_seed(&a,42);game_init_seed(&b,42);
    assert(memcmp(a.recipe,b.recipe,sizeof(a.recipe))==0);
    assert_permutation(&a);
    assert(game_join(&a,111)==1);
    assert(game_join(&a,111)==1 && a.num_players==1);
    assert(game_join(&a,222)==2);
    assert(game_join(&a,333)==0);
    int x=a.players[0].x;
    game_move(&a,0,111,'r');game_move(&a,3,111,'r');game_move(&a,1,999,'r');
    assert(a.players[0].x==x);
    game_move(&a,1,111,'r');assert(a.players[0].x==x+STEP);
    int previous[RECIPE_MAX_INGREDIENTS];
    int previous_len=0;unsigned previous_mask=0;
    for(int round=0;round<RECIPES_TO_WIN;round++){
        assert(a.round==round);
        assert_permutation(&a);
        unsigned mask=0;
        for(int i=0;i<a.recipe_len;i++)mask |= 1u<<a.recipe[i];
        if(round>0 && previous_len==a.recipe_len)
            assert(mask!=previous_mask);
        memcpy(previous,a.recipe,sizeof(previous));previous_len=a.recipe_len;
        previous_mask=mask;
        printf("Round %d (%d):",round+1,a.recipe_len);
        for(int i=0;i<a.recipe_len;i++)printf(" %s",ingredient_names[a.recipe[i]]);
        printf("\n");
        int len=a.recipe_len;
        for(int i=0;i<len;i++)full_stage(&a);
    }
    assert(a.phase==PHASE_WIN);
    game_action(&a,1,111,ACTION_INTERACT,0);
    assert(a.phase==PHASE_WIN);
}
static void test_missing_player(void){
    GameRules g;game_init_seed(&g,7);
    assert(game_join(&g,111)==1);
    int item=g.recipe[0];
    place_at(&g.players[0],ingredient_x[item],ingredient_y[item]);
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.held[0]==-1 && g.phase==PHASE_COLLECT);
}
static void test_wrong_ingredient(void){
    GameRules g;game_init_seed(&g,8);
    game_join(&g,111);game_join(&g,222);
    int wrong=(g.recipe[0]+1)%INGREDIENT_COUNT;
    place_at(&g.players[0],ingredient_x[wrong],ingredient_y[wrong]);
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.held[0]==wrong);
    place_at(&g.players[0],cauldron_x,cauldron_y);
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.phase==PHASE_LOSE);
}
static void test_restart(void){
    GameRules g;game_init_seed(&g,8);
    game_join(&g,111);game_join(&g,222);
    /* A game in progress can't be restarted, and neither can a stranger restart one. */
    int before=g.revision;
    game_action(&g,1,111,ACTION_RESTART,0);
    assert(g.revision==before && g.phase==PHASE_COLLECT);
    int wrong=(g.recipe[0]+1)%INGREDIENT_COUNT;
    place_at(&g.players[0],ingredient_x[wrong],ingredient_y[wrong]);
    game_action(&g,1,111,ACTION_INTERACT,0);
    place_at(&g.players[0],cauldron_x,cauldron_y);
    game_action(&g,1,111,ACTION_INTERACT,0);
    assert(g.phase==PHASE_LOSE);
    game_action(&g,1,999,ACTION_RESTART,0);
    assert(g.phase==PHASE_LOSE);
    /* Either player can restart. Everything resets, but both keep their seats. */
    int session=g.session;
    game_action(&g,2,222,ACTION_RESTART,0);
    assert(g.phase==PHASE_COLLECT && g.round==0 && g.stage==0 && g.mistakes==0);
    assert(g.used_mask==0 && g.recipes_completed==0 && g.held[0]==-1 && g.held[1]==-1);
    assert(g.session!=session && g.time_limit_ms==COLLECT_TIME_MS);
    assert(g.num_players==2 && game_valid_player(&g,1,111) && game_valid_player(&g,2,222));
    assert(g.players[0].x==355 && g.players[1].x==555);
    /* A second click arriving late must not restart the new game too. */
    int revision=g.revision;
    game_action(&g,1,111,ACTION_RESTART,0);
    assert(g.revision==revision);
}
int main(void){
    test_missing_player();test_wrong_ingredient();test_all_rounds();test_restart();
    puts("PASS: random recipes, 3 consecutive rounds, two-player roles, and safety checks");
    return 0;
}
