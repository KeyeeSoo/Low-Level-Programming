/* Threading model from slides: two concurrent DDS producers deliver EVENTS;
 * main consumes those events and is the ONLY GameRules writer. A third
 * thread continuously reads synchronized published snapshots. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "ServerBridge.h"

#define MOVES 10000
static ServerBridge bridge;
static GameRules authoritative;

typedef struct { int token, id; } ProducerArgs;
static void *simulate_network(void *arg){
    const ProducerArgs *p=arg;
    ServerEvent join={.type=SERVER_JOIN,.token=p->token};
    while(!server_bridge_push_event(&bridge,join))sched_yield();
    for(int i=0;i<MOVES;i++){
        ServerEvent e={.type=SERVER_MOVE,.token=p->token,.player_id=p->id,
                       .direction=(i%4==0)?'u':(i%4==1)?'d':(i%4==2)?'l':'r'};
        while(!server_bridge_push_event(&bridge,e))sched_yield();
    }
    return NULL;
}
static void *simulate_dds_publisher(void *unused){
    (void)unused;
    GameRules snap;
    while(!server_bridge_should_stop(&bridge)){
        server_bridge_snapshot(&bridge,&snap);
        assert(snap.num_players>=0 && snap.num_players<=2);
        for(int i=0;i<MAX_PLAYERS;i++)if(snap.players[i].b_active){
            assert(snap.players[i].x>=211);
            assert(snap.players[i].x<=WIDTH-SQ_WIDTH-10);
            assert(snap.players[i].y>=143);
            assert(snap.players[i].y<=HEIGHT-SQ_WIDTH-9);
        }
        sched_yield();
    }
    return NULL;
}
int main(void){
    game_init(&authoritative);
    assert(server_bridge_init(&bridge,&authoritative));
    pthread_t p1,p2,reader;
    ProducerArgs a={101,1},b={202,2};
    assert(pthread_create(&reader,NULL,simulate_dds_publisher,NULL)==0);
    assert(pthread_create(&p1,NULL,simulate_network,&a)==0);
    assert(pthread_create(&p2,NULL,simulate_network,&b)==0);
    int events=0;
    const int total=(MOVES+1)*2;
    while(events<total){
        ServerEvent ev;
        if(!server_bridge_pop_event(&bridge,&ev)){sched_yield();continue;}
        if(ev.type==SERVER_JOIN){
            int id=game_join(&authoritative,ev.token);
            assert(id==((ev.token==101)?1:2) || id==((ev.token==101)?2:1));
            assert(server_bridge_push_reply(&bridge,(ServerJoinReply){ev.token,id}));
        }else if(ev.type==SERVER_MOVE){
            game_move(&authoritative,ev.player_id,ev.token,ev.direction);
        }
        server_bridge_publish(&bridge,&authoritative);
        events++;
    }
    pthread_join(p1,NULL);pthread_join(p2,NULL);
    ServerJoinReply response;
    int responses=0;
    while(server_bridge_pop_reply(&bridge,&response)){
        assert(response.player_id>=1 && response.player_id<=2);
        responses++;
    }
    assert(responses==2);
    assert(authoritative.num_players==2);
    assert(game_join(&authoritative,101)==1 || game_join(&authoritative,101)==2);
    server_bridge_set_stop(&bridge,0);
    pthread_join(reader,NULL);
    server_bridge_destroy(&bridge);
    printf("Passed: slide model — 2 DDS producers, main-thread game owner, %d events, snapshot reader.\n",events);
    return 0;
}
