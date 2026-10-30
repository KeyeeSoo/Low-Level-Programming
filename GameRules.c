#include "GameRules.h"
#include <string.h>

const char *ingredient_names[4] = {"Moonflower", "Ginger Root", "Black Feather", "Sugar Glass"};
const int ingredient_x[4] = {315, 650, 315, 650};
const int ingredient_y[4] = {180, 180, 493, 493};
const int cauldron_x = 490, cauldron_y = 330;
static int sq(int n){return n*n;}
static int near(int x, int y, int ax, int ay, int radius){return sq(x-ax)+sq(y-ay)<=sq(radius);}

/* Original server collision logic, with strict overlap to permit adjacency. */
static int detect_collision(const Player players[], int id, int x, int y) {
    for(int i=0;i<MAX_PLAYERS;i++) {
        if(i==id-1 || !players[i].b_active) continue;
        if(x < players[i].x+SQ_WIDTH && players[i].x < x+SQ_WIDTH &&
           y < players[i].y+SQ_WIDTH && players[i].y < y+SQ_WIDTH) return 1;
    }
    return 0;
}
void game_init(GameRules *g){
    memset(g,0,sizeof(*g));
    g->held[0]=g->held[1]=-1;
    g->phase=PHASE_COLLECT;
}
int game_join(GameRules *g,int token){
    if(token<=0)return 0;
    /* Repeated join requests must never create extra players. */
    for(int i=0;i<MAX_PLAYERS;i++)
        if(g->players[i].b_active && g->players[i].token==token)return i+1;
    for(int i=0;i<MAX_PLAYERS;i++) if(!g->players[i].b_active){
        Player *p=&g->players[i];
        p->b_active=1;p->token=token;
        p->x=(i==0)?355:555;p->y=340;
        ++g->num_players;++g->revision;
        return i+1;
    }
    return 0;
}
int game_valid_player(const GameRules*g,int id,int token){
    return id>=1 && id<=MAX_PLAYERS && token>0 &&
           g->players[id-1].b_active && g->players[id-1].token==token;
}
void game_move(GameRules*g,int id,int token,char direction){
    if(!game_valid_player(g,id,token) || g->phase!=PHASE_COLLECT)return;
    Player *p=&g->players[id-1];
    int x=p->x,y=p->y;
    switch(direction){
        case 'u':y-=STEP;break;case 'd':y+=STEP;break;
        case 'l':x-=STEP;break;case 'r':x+=STEP;break;
        default:return;
    }
    /* The left recipe panel and title are UI, not part of the playfield. */
    if(x<211 || x>WIDTH-SQ_WIDTH-10 || y<143 || y>HEIGHT-SQ_WIDTH-9)return;
    if(!detect_collision(g->players,id,x,y)){p->x=x;p->y=y;}
}
void game_action(GameRules*g,int id,int token,int action,int session){
    if(!game_valid_player(g,id,token))return;
    int i=id-1;
    if(action==ACTION_DROP){
        if(g->phase==PHASE_COLLECT && g->held[i]>=0){g->held[i]=-1;g->revision++;}
        return;
    }
    if(action==ACTION_INTERACT){
        if(g->phase!=PHASE_COLLECT || g->num_players<2)return;
        int cx=g->players[i].x+SQ_WIDTH/2;
        int cy=g->players[i].y+SQ_WIDTH/2;
        if(g->held[i]>=0){
            if(!near(cx,cy,cauldron_x,cauldron_y,83))return;
            if(g->held[i]!=g->stage){g->phase=PHASE_LOSE;}
            else {
                g->held[i]=-1;g->phase=PHASE_TRACE;g->actor=id;g->session++;
            }
            g->revision++;
            return;
        }
        int closest=-1,best=sq(67);
        for(int item=g->stage;item<4;item++){
            if(g->held[1-i]==item)continue;
            int d=sq(cx-ingredient_x[item])+sq(cy-ingredient_y[item]);
            if(d<best){best=d;closest=item;}
        }
        if(closest>=0){g->held[i]=closest;g->revision++;}
        return;
    }
    if(id!=g->actor || session!=g->session)return;
    if(g->phase==PHASE_TRACE){
        if(action==ACTION_TRACE_BAD){
            if(++g->mistakes>=3)g->phase=PHASE_LOSE;
            g->revision++;
        }else if(action==ACTION_TRACE_OK){g->phase=PHASE_STIR;g->revision++;}
    } else if(g->phase==PHASE_STIR && action==ACTION_STIR_OK){
        g->stage++;g->actor=0;
        g->phase=(g->stage==4)?PHASE_WIN:PHASE_COLLECT;
        g->revision++;
    }
}
