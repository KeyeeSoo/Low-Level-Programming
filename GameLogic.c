#include "GameLogic.h"
#include <math.h>
#include <string.h>

const GamePoint potion_ingredients[POTION_INGREDIENTS] = {
    {480, 228}, /* Moonflower */
    {840, 228}, /* Ginger root */
    {480, 538}, /* Black feather */
    {840, 538}  /* Sugar glass */
};
const GamePoint potion_cauldron = {660, 385};
const char *potion_ingredient_names[POTION_INGREDIENTS] = {
    "Moonflower", "Ginger Root", "Black Feather", "Sugar Glass"
};
static float square(float x) {return x*x;}
static float d2(float x, float y, GamePoint p) {
    return square(x-p.x)+square(y-p.y);
}
static int clampdir(int value) {return value<0?-1:(value>0?1:0);}

void game_init(GameWorld *w) {
    memset(w, 0, sizeof(*w));
    w->x[0]=550; w->y[0]=397;
    w->x[1]=750; w->y[1]=397;
    w->held[0]=w->held[1]=-1;
    w->phase=POTION_COLLECT;
    w->session=0;
}

int game_join(GameWorld *w, int token) {
    if(token<=0) return 0;
    for(int i=0;i<2;i++) if(w->joined[i]&&w->token[i]==token) return i+1;
    for(int i=0;i<2;i++) if(!w->joined[i]) {
        w->token[i]=token; w->joined[i]=1; w->revision++;
        return i+1;
    }
    return 0;
}

void game_tick(GameWorld *w, float dt) {
    if(w->phase!=POTION_COLLECT || !w->joined[0] || !w->joined[1]) return;
    if(dt<0) dt=0;
    if(dt>0.05f) dt=0.05f;
    for(int i=0;i<2;i++) {
        float dx=(float)w->move_x[i], dy=(float)w->move_y[i];
        if(dx&&dy) {dx*=0.70710678f;dy*=0.70710678f;}
        float nx=w->x[i]+dx*175.0f*dt;
        float ny=w->y[i]+dy*175.0f*dt;
        if(nx<270) nx=270;
        if(nx>1005) nx=1005;
        if(ny<160) ny=160;
        if(ny>605) ny=605;
        w->x[i]=nx; w->y[i]=ny;
    }
}

void game_command(GameWorld *w, int player, int token,
                  int action, int dx, int dy, int session) {
    int i=player-1;
    if(i<0||i>=2||!w->joined[i]||w->token[i]!=token) return;
    if(action==POTION_MOVE) {
        /* Stop stale movement during a modal minigame. */
        w->move_x[i]=(w->phase==POTION_COLLECT)?clampdir(dx):0;
        w->move_y[i]=(w->phase==POTION_COLLECT)?clampdir(dy):0;
        return;
    }
    if(action==POTION_DROP) {
        if(w->phase==POTION_COLLECT && w->held[i]>=0) {
            w->held[i]=-1;
            w->revision++;
        }
        return;
    }
    if(action==POTION_INTERACT) {
        if(w->phase!=POTION_COLLECT||!w->joined[0]||!w->joined[1]) return;
        if(w->held[i]>=0) {
            if(d2(w->x[i],w->y[i],potion_cauldron)>78.0f*78.0f) return;
            if(w->held[i]!=w->next_ingredient) {
                w->phase=POTION_LOSE;
            } else {
                w->held[i]=-1;
                w->phase=POTION_TRACE;
                w->active_player=player;
                w->session++;
                w->move_x[0]=w->move_x[1]=0;
                w->move_y[0]=w->move_y[1]=0;
            }
            w->revision++;
            return;
        }
        int nearest=-1;
        float best=66.0f*66.0f;
        for(int item=w->next_ingredient;item<POTION_INGREDIENTS;item++) {
            if(w->held[1-i]==item) continue;
            float dist=d2(w->x[i],w->y[i],potion_ingredients[item]);
            if(dist<best) {best=dist;nearest=item;}
        }
        if(nearest>=0) {w->held[i]=nearest;w->revision++;}
        return;
    }
    if(player!=w->active_player || session!=w->session) return;
    if(w->phase==POTION_TRACE) {
        if(action==POTION_TRACE_BAD) {
            w->rune_mistakes++;
            if(w->rune_mistakes>=3) w->phase=POTION_LOSE;
            w->revision++;
        } else if(action==POTION_TRACE_OK) {
            w->phase=POTION_STIR;
            w->revision++;
        }
    } else if(w->phase==POTION_STIR&&action==POTION_STIR_OK) {
        w->next_ingredient++;
        w->active_player=0;
        w->phase=(w->next_ingredient==POTION_INGREDIENTS)?POTION_WIN:POTION_COLLECT;
        w->revision++;
    }
}
