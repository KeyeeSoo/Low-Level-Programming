/* Extended from the user's original Cyclone DDS server.c.
   Retains JoinRequest/JoinResponse/Input/Position topics, player squares,
   collision logic and server-authoritative movement. Adds GameAction/GameState.
   The original crash-prone player array indexing is validated in GameRules.c. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <dds/dds.h>
#include "messages.h"
#include "GameRules.h"

#define CHECK(x) do {dds_return_t rc_=(x); if(rc_<0){ \
    fprintf(stderr,"DDS failure %s: %s\n",#x,dds_strretcode(-rc_));exit(1);}}while(0)
static dds_entity_t topic(dds_entity_t participant,const dds_topic_descriptor_t *desc,const char *name){
    dds_entity_t t=dds_create_topic(participant,desc,name,NULL,NULL);
    if(t<0){fprintf(stderr,"Topic %s failed: %s\n",name,dds_strretcode(-t));exit(1);}return t;
}
static dds_entity_t reader(dds_entity_t p,dds_entity_t t){dds_entity_t e=dds_create_reader(p,t,NULL,NULL);CHECK(e);return e;}
static dds_entity_t writer(dds_entity_t p,dds_entity_t t){dds_entity_t e=dds_create_writer(p,t,NULL,NULL);CHECK(e);return e;}
static void send_positions(GameRules *g,dds_entity_t w){
    for(int i=0;i<MAX_PLAYERS;i++)if(g->players[i].b_active){
        Position pos={0};pos.int_player_id=i+1;
        pos.int_x=g->players[i].x;pos.int_y=g->players[i].y;
        CHECK(dds_write(w,&pos));
    }
}
static void send_game(GameRules *g,dds_entity_t w){
    GameState s={0};
    s.int_phase=g->phase;s.int_stage=g->stage;s.int_actor=g->actor;
    s.int_mistakes=g->mistakes;s.int_held1=g->held[0];s.int_held2=g->held[1];
    s.int_session=g->session;s.int_revision=g->revision;s.int_num_players=g->num_players;
    CHECK(dds_write(w,&s));
}
static void *worker(void *unused){
    (void)unused;
    GameRules game;game_init(&game);
    dds_entity_t p=dds_create_participant(DDS_DOMAIN_DEFAULT,NULL,NULL);CHECK(p);
    dds_entity_t jt=topic(p,&JoinRequest_desc,"JoinRequest");
    dds_entity_t rt=topic(p,&JoinResponse_desc,"JoinResponse");
    dds_entity_t it=topic(p,&Input_desc,"Input");
    dds_entity_t pt=topic(p,&Position_desc,"Position");
    dds_entity_t at=topic(p,&GameAction_desc,"GameAction");
    dds_entity_t st=topic(p,&GameState_desc,"GameState");
    dds_entity_t jr=reader(p,jt),ir=reader(p,it),ar=reader(p,at);
    dds_entity_t rw=writer(p,rt),pw=writer(p,pt),sw=writer(p,st);
    uint64_t last_update=dds_time();int last_revision=-1;
    printf("Original DDS server + Gingerbread game started. Waiting for two clients...\n");
    for(;;){
        dds_return_t rc;
        JoinRequest join={0};void *js[1]={&join};dds_sample_info_t ji[1];
        for(int n=0;n<32;n++){
            rc=dds_take(jr,js,ji,1,1);CHECK(rc);if(rc==0)break;
            if(!ji[0].valid_data)continue;
            int id=game_join(&game,join.int_client_token);
            JoinResponse response={0};response.int_player_id=id;
            response.int_client_token=join.int_client_token;
            CHECK(dds_write(rw,&response));
            if(id)printf("Join token %d -> player %d\n",(int)join.int_client_token,id);
        }
        Input in={0};void *is[1]={&in};dds_sample_info_t ii[1];
        for(int n=0;n<128;n++){
            rc=dds_take(ir,is,ii,1,1);CHECK(rc);if(rc==0)break;
            if(ii[0].valid_data)game_move(&game,in.int_player_id,in.int_client_token,in.ch_direction);
        }
        GameAction action={0};void *as[1]={&action};dds_sample_info_t ai[1];
        for(int n=0;n<32;n++){
            rc=dds_take(ar,as,ai,1,1);CHECK(rc);if(rc==0)break;
            if(ai[0].valid_data)
                game_action(&game,action.int_player_id,action.int_client_token,
                            action.int_action,action.int_session);
        }
        uint64_t now=dds_time();
        if(now-last_update>=DDS_MSECS(60)){
            last_update=now;
            send_positions(&game,pw);
            send_game(&game,sw);
            last_revision=game.revision;
        }else if(game.revision!=last_revision){
            send_game(&game,sw);last_revision=game.revision;
        }
        dds_sleepfor(DDS_MSECS(5));
    }
    return NULL;
}
int main(void){
    pthread_t thread;
    if(pthread_create(&thread,NULL,worker,NULL)!=0){perror("pthread_create");return 1;}
    pthread_join(thread,NULL);
    return 0;
}
