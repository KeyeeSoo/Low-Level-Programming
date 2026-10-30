#include "GameRules.h"
#include <string.h>
#include <time.h>

const char *ingredient_names[INGREDIENT_COUNT] = {
    "Moonflower", "Ginger Root", "Black Feather", "Sugar Glass", "Witch Moss",
    "Night Berry", "Toadstool", "Phoenix Ash", "Silver Dew", "Thornvine"
};
const char *ingredient_short_names[INGREDIENT_COUNT] = {
    "Moon", "Ginger", "Feather", "Glass", "Moss",
    "Berry", "Toad", "Ash", "Dew", "Thorn"
};
/* Each ingredient determines its rune independently of recipe order. */
const int ingredient_rune[INGREDIENT_COUNT] = {0,1,2,3,2,0,3,1,1,2};
const int ingredient_x[INGREDIENT_COUNT] = {260,363,466,569,672,260,363,466,569,672};
const int ingredient_y[INGREDIENT_COUNT] = {195,195,195,195,195,485,485,485,485,485};
const int cauldron_x = 490, cauldron_y = 330;
static int sq(int n){return n*n;}
static int near(int x,int y,int ax,int ay,int radius){
    return sq(x-ax)+sq(y-ay)<=sq(radius);
}
static uint32_t random_next(GameRules *g){
    /* Small deterministic per-game PRNG for testable server-side shuffling.
       No rand() global state shared between threads. */
    uint32_t x=g->rng_state;
    x^=x<<13; x^=x>>17; x^=x<<5;
    g->rng_state=x;
    return x;
}
static void shuffle_recipe(GameRules *g, const int *previous, int previous_len){
    int pool[INGREDIENT_COUNT];
    for(int i=0;i<INGREDIENT_COUNT;i++)pool[i]=i;
    /* Fisher-Yates: choose distinct ingredients from the ten-item pool. */
    for(int i=INGREDIENT_COUNT-1;i>0;i--){
        int j=(int)(random_next(g)%(uint32_t)(i+1));
        int t=pool[i];pool[i]=pool[j];pool[j]=t;
    }
    g->recipe_len=RECIPE_MIN_INGREDIENTS+
        (int)(random_next(g)%(RECIPE_MAX_INGREDIENTS-RECIPE_MIN_INGREDIENTS+1));
    for(int i=0;i<RECIPE_MAX_INGREDIENTS;i++)
        g->recipe[i]=(i<g->recipe_len)?pool[i]:-1;
    /* Adjacent recipes must use different ingredient SETS, not merely
     * the same ingredients in a different order. */
    if(previous && previous_len==g->recipe_len){
        unsigned old_mask=0,new_mask=0;
        for(int i=0;i<g->recipe_len;i++){
            old_mask |= 1u << previous[i];
            new_mask |= 1u << g->recipe[i];
        }
        if(old_mask==new_mask){
            /* pool[recipe_len] is guaranteed not to be in this recipe. */
            g->recipe[g->recipe_len-1]=pool[g->recipe_len];
        }
    }
}
/* The server is the sole time authority. These functions never call clock_gettime
 * directly, allowing deterministic unit tests and preserving thread ownership. */
static int phase_duration_ms(const GameRules *g){
    if(g->num_players<2)return 0;
    switch(g->phase){
        case PHASE_COLLECT:return COLLECT_TIME_MS;
        case PHASE_TRACE:return TRACE_TIME_MS;
        case PHASE_STIR:return STIR_TIME_MS;
        default:return 0;
    }
}
static void begin_phase_timer(GameRules *g){
    g->time_limit_ms=phase_duration_ms(g);
    g->time_remaining_ms=g->time_limit_ms;
    g->deadline_ms=g->time_limit_ms ?
        g->clock_ms+(uint64_t)g->time_limit_ms : 0;
}
void game_tick(GameRules *g,uint64_t now_ms){
    /* Prevent a clock going backwards from extending a timer. */
    if(now_ms<g->clock_ms)now_ms=g->clock_ms;
    g->clock_ms=now_ms;
    if(!g->deadline_ms || !g->time_limit_ms)return;
    if(now_ms<g->deadline_ms){
        uint64_t remaining=g->deadline_ms-now_ms;
        g->time_remaining_ms=(int)(remaining>(uint64_t)g->time_limit_ms ?
                                  (uint64_t)g->time_limit_ms : remaining);
        return;
    }
    /* A single timeout costs one mistake; never accumulate multiple penalties
       just because one frame took longer than expected. */
    g->mistakes++;
    ++g->session; /* Reject any late TRACE/STIR results from the old attempt. */
    if(g->phase==PHASE_COLLECT){
        for(int i=0;i<MAX_PLAYERS;i++)g->held[i]=-1;
    }
    if(g->mistakes>=3){
        g->phase=PHASE_LOSE;
        g->actor=0;
    }
    begin_phase_timer(g);
    ++g->revision;
}
static void start_next_recipe(GameRules *g){
    int previous[RECIPE_MAX_INGREDIENTS];
    int previous_len=g->recipe_len;
    memcpy(previous,g->recipe,sizeof(previous));
    ++g->round;
    g->stage=0;
    g->actor=0;
    g->mistakes=0;
    g->used_mask=0;
    for(int i=0;i<MAX_PLAYERS;i++)g->held[i]=-1;
    shuffle_recipe(g,previous,previous_len);
    g->phase=PHASE_COLLECT;
    begin_phase_timer(g);
}
static int detect_collision(const Player players[],int id,int x,int y){
    for(int i=0;i<MAX_PLAYERS;i++){
        if(i==id-1 || !players[i].b_active)continue;
        if(x < players[i].x+SQ_WIDTH && players[i].x < x+SQ_WIDTH &&
           y < players[i].y+SQ_WIDTH && players[i].y < y+SQ_WIDTH)return 1;
    }
    return 0;
}
void game_init_seed(GameRules *g,uint32_t seed){
    memset(g,0,sizeof(*g));
    for(int i=0;i<MAX_PLAYERS;i++)g->held[i]=-1;
    g->rng_state=seed?seed:0xA341316Cu;
    g->phase=PHASE_COLLECT;
    shuffle_recipe(g,NULL,0);
}
void game_init(GameRules *g){
    /* Nanosecond + PID seed: consecutive game launches need not reuse a recipe. */
    struct timespec ts;
    timespec_get(&ts,TIME_UTC);
    uint32_t seed=(uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec ^
                  (uint32_t)(uintptr_t)g;
    game_init_seed(g,seed);
}
int game_join(GameRules *g,int token){
    if(token<=0)return 0;
    for(int i=0;i<MAX_PLAYERS;i++)
        if(g->players[i].b_active && g->players[i].token==token)return i+1;
    for(int i=0;i<MAX_PLAYERS;i++)if(!g->players[i].b_active){
        Player *p=&g->players[i];
        p->b_active=1;p->token=token;
        p->x=(i==0)?355:555;p->y=340;
        ++g->num_players;++g->revision;
        /* Countdown begins only after both players have joined. */
        if(g->num_players==MAX_PLAYERS)begin_phase_timer(g);
        return i+1;
    }
    return 0;
}
int game_valid_player(const GameRules *g,int id,int token){
    return id>=1 && id<=MAX_PLAYERS && token>0 &&
           g->players[id-1].b_active && g->players[id-1].token==token;
}
/* Alternate the collector each ingredient. Their teammate traces the rune,
   while the collector then stirs; every stage requires both players. */
int game_collector(const GameRules *g){return ((g->round+g->stage)%2)+1;}
int game_tracer(const GameRules *g){return 3-game_collector(g);}
int game_stirrer(const GameRules *g){return game_collector(g);}
void game_move(GameRules *g,int id,int token,char direction){
    if(!game_valid_player(g,id,token) || g->phase!=PHASE_COLLECT)return;
    Player *p=&g->players[id-1];
    int x=p->x,y=p->y;
    switch(direction){
        case 'u':y-=STEP;break;case 'd':y+=STEP;break;
        case 'l':x-=STEP;break;case 'r':x+=STEP;break;
        default:return;
    }
    if(x<211 || x>WIDTH-SQ_WIDTH-10 || y<143 || y>HEIGHT-SQ_WIDTH-9)return;
    if(!detect_collision(g->players,id,x,y)){p->x=x;p->y=y;}
}
void game_action(GameRules *g,int id,int token,int action,int session){
    if(!game_valid_player(g,id,token) ||
       g->phase==PHASE_WIN || g->phase==PHASE_LOSE)return;
    int i=id-1;
    if(action==ACTION_DROP){
        if(g->phase==PHASE_COLLECT && g->held[i]>=0){
            g->held[i]=-1;++g->revision;
        }
        return;
    }
    if(action==ACTION_INTERACT){
        /* Strict cooperative play: only the designated gatherer can interact
           with ingredients / cauldron; cannot play with only one client. */
        if(g->phase!=PHASE_COLLECT || g->num_players<2 ||
           id!=game_collector(g))return;
        int cx=g->players[i].x+SQ_WIDTH/2;
        int cy=g->players[i].y+SQ_WIDTH/2;
        if(g->held[i]>=0){
            if(!near(cx,cy,cauldron_x,cauldron_y,83))return;
            if(g->held[i]!=g->recipe[g->stage]){
                g->phase=PHASE_LOSE;
            }else{
                g->held[i]=-1;
                g->phase=PHASE_TRACE;
                g->actor=game_tracer(g);
                ++g->session;
            }
            begin_phase_timer(g);
            ++g->revision;
            return;
        }
        int closest=-1,best=sq(67);
        for(int item=0;item<INGREDIENT_COUNT;item++){
            if((g->used_mask & (1<<item)) || g->held[1-i]==item)continue;
            int d=sq(cx-ingredient_x[item])+sq(cy-ingredient_y[item]);
            if(d<best){best=d;closest=item;}
        }
        if(closest>=0){g->held[i]=closest;++g->revision;}
        return;
    }
    if(id!=g->actor || session!=g->session || g->num_players<2)return;
    if(g->phase==PHASE_TRACE){
        if(action==ACTION_TRACE_BAD){
            if(++g->mistakes>=3){g->phase=PHASE_LOSE;g->actor=0;}
            ++g->session; /* A failed attempt must use a new session. */
            begin_phase_timer(g);
            ++g->revision;
        }else if(action==ACTION_TRACE_OK){
            g->phase=PHASE_STIR;
            g->actor=game_stirrer(g);
            begin_phase_timer(g);
            ++g->revision;
        }
    }else if(g->phase==PHASE_STIR && action==ACTION_STIR_OK){
        g->used_mask |= 1 << g->recipe[g->stage];
        ++g->stage;
        g->actor=0;
        if(g->stage==g->recipe_len){
            ++g->recipes_completed;
            if(g->recipes_completed>=RECIPES_TO_WIN){
                g->phase=PHASE_WIN;
            }else start_next_recipe(g);
        }else{
            g->phase=PHASE_COLLECT;
            begin_phase_timer(g);
        }
        if(g->phase==PHASE_WIN)begin_phase_timer(g);
        ++g->revision;
    }
}
