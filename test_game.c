/* Build: gcc -std=c11 test_game.c GameLogic.c -lm -o test_game && ./test_game */
#include "GameLogic.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
static void move_near(GameWorld*w,int id,GamePoint p) {
    w->x[id-1]=p.x; w->y[id-1]=p.y;
}
static void command(GameWorld*w,int id,int action) {
    game_command(w,id,100+id,action,0,0,w->session);
}
int main(void) {
    GameWorld w;game_init(&w);
    assert(game_join(&w,101)==1);
    assert(game_join(&w,101)==1);
    assert(game_join(&w,102)==2);
    assert(game_join(&w,103)==0);
    game_command(&w,1,999,POTION_INTERACT,0,0,0); /* Reject wrong token */
    assert(w.held[0]==-1);
    /* Wrong ingredient immediately ends game if added to cauldron. */
    move_near(&w,1,potion_ingredients[1]);command(&w,1,POTION_INTERACT);
    assert(w.held[0]==1);
    move_near(&w,1,potion_cauldron);command(&w,1,POTION_INTERACT);
    assert(w.phase==POTION_LOSE);

    game_init(&w);game_join(&w,101);game_join(&w,102);
    for(int item=0;item<4;item++) {
        int id=item%2+1;
        move_near(&w,id,potion_ingredients[item]);command(&w,id,POTION_INTERACT);
        assert(w.held[id-1]==item);
        move_near(&w,id,potion_cauldron);command(&w,id,POTION_INTERACT);
        assert(w.phase==POTION_TRACE&&w.active_player==id);
        /* Old session and wrong player cannot certify a rune. */
        game_command(&w,3-id,100+(3-id),POTION_TRACE_OK,0,0,w.session);
        game_command(&w,id,100+id,POTION_TRACE_OK,0,0,w.session-1);
        assert(w.phase==POTION_TRACE);
        command(&w,id,POTION_TRACE_OK);
        assert(w.phase==POTION_STIR);
        command(&w,id,POTION_STIR_OK);
        assert(w.next_ingredient==item+1);
    }
    assert(w.phase==POTION_WIN);

    game_init(&w);game_join(&w,101);game_join(&w,102);
    move_near(&w,1,potion_ingredients[0]);command(&w,1,POTION_INTERACT);
    move_near(&w,1,potion_cauldron);command(&w,1,POTION_INTERACT);
    for(int i=1;i<=3;i++) {
        command(&w,1,POTION_TRACE_BAD);
        assert(w.rune_mistakes==i);
    }
    assert(w.phase==POTION_LOSE);
    puts("All game logic tests passed.");
    return 0;
}
