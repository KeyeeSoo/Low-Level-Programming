/* The Gingerbread Curse — ORIGINAL DDS architecture, as specified by slides.
 * Server MAIN: sole writer of authoritative GameRules (joins/movement/rules).
 * Server DDS WORKER: owns DDS entities, receives requests and publishes copies.
 * Mutex-protected request/response queues + state snapshot join the threads.
 * Server AUTH THREAD (AuthWorker.c): logins, which are slow by design.
 * DDS reads/writes occur outside the bridge mutex. Only the short waitset
 * trigger notification runs under the lock to protect its lifetime.
 */
#define _POSIX_C_SOURCE 200809L
#include <dds/dds.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "messages.h"
#include "GameRules.h"
#include "ServerBridge.h"
#include "DdsWake.h"
#include "AuthWorker.h"
#include "SecureDds.h"

/* Logged-in sessions. Owned by the main thread, like the rest of the game state. */
#define MAX_SESSIONS 32
#define CHAT_MIN_INTERVAL_MS 250
typedef struct {
    int token;
    int guest;
    char name[USERNAME_MAX+1];
} Session;
static Session sessions[MAX_SESSIONS];
static unsigned next_session=0;
static uint64_t last_chat_ms[MAX_PLAYERS];
static int chat_sequence=0;

static ServerBridge bridge;
static volatile sig_atomic_t quitting=0;
static atomic_ulong server_join_rx=0, server_input_rx=0, server_action_rx=0;
static atomic_ulong server_pos_tx=0, server_state_tx=0;
static uint64_t dds_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
static dds_qos_t *qos_best_effort(void) {
    dds_qos_t *q=dds_create_qos();
    if(!q)return NULL;
    dds_qset_reliability(q,DDS_RELIABILITY_BEST_EFFORT,DDS_MSECS(20));
    dds_qset_history(q,DDS_HISTORY_KEEP_LAST,8);
    return q;
}
static dds_qos_t *qos_reliable(void) {
    dds_qos_t *q=dds_create_qos();
    if(!q)return NULL;
    dds_qset_reliability(q,DDS_RELIABILITY_RELIABLE,DDS_MSECS(100));
    dds_qset_history(q,DDS_HISTORY_KEEP_LAST,16);
    return q;
}

static void on_signal(int sig){(void)sig;quitting=1;}

/* Only called by the DDS worker.  A DDS error exits that worker gracefully. */
static int dds_ok(dds_return_t rc,const char *label){
    if(rc>=0)return 1;
    fprintf(stderr,"[server DDS] %s failed: %s (%d)\n",
            label,dds_strretcode(-rc),(int)rc);
    server_bridge_set_stop(&bridge,1);
    return 0;
}
#define CREATE_DDS(v, expr, label) do { \
    (v)=(expr);if(!dds_ok((v),(label)))goto cleanup; \
}while(0)

static void *dds_worker(void *arg){
    (void)arg;
    dds_entity_t p=secure_create_participant("server");
    if(!dds_ok(p,"create secure participant"))return NULL;
    printf("[server DDS worker] DDS Security on: traffic is authenticated and encrypted\n");
    dds_entity_t wake=0;
    dds_entity_t jt,rt,it,pt,at,st,jr,ir,ar,rw,pw,sw;
    dds_entity_t aqt,art,cst,cmt,aqr,arw,csr,cmw;
    dds_qos_t *stream_qos=qos_best_effort(),*reliable_qos=qos_reliable();
    if(!stream_qos || !reliable_qos){
        fprintf(stderr,"[server DDS] QoS allocation failed\n");
        server_bridge_set_stop(&bridge,1);goto cleanup;
    }
    CREATE_DDS(jt,dds_create_topic(p,&JoinRequest_desc,"JoinRequest",NULL,NULL),"JoinRequest topic");
    CREATE_DDS(rt,dds_create_topic(p,&JoinResponse_desc,"JoinResponse",NULL,NULL),"JoinResponse topic");
    CREATE_DDS(it,dds_create_topic(p,&Input_desc,"Input",NULL,NULL),"Input topic");
    CREATE_DDS(pt,dds_create_topic(p,&Position_desc,"Position",NULL,NULL),"Position topic");
    CREATE_DDS(at,dds_create_topic(p,&GameAction_desc,"GameAction",NULL,NULL),"GameAction topic");
    CREATE_DDS(st,dds_create_topic(p,&GameState_desc,"GameState",NULL,NULL),"GameState topic");
    CREATE_DDS(aqt,dds_create_topic(p,&AuthRequest_desc,"AuthRequest",NULL,NULL),"AuthRequest topic");
    CREATE_DDS(art,dds_create_topic(p,&AuthResponse_desc,"AuthResponse",NULL,NULL),"AuthResponse topic");
    CREATE_DDS(cst,dds_create_topic(p,&ChatSend_desc,"ChatSend",NULL,NULL),"ChatSend topic");
    CREATE_DDS(cmt,dds_create_topic(p,&ChatMessage_desc,"ChatMessage",NULL,NULL),"ChatMessage topic");
    CREATE_DDS(aqr,dds_create_reader(p,aqt,reliable_qos,NULL),"AuthRequest reader");
    CREATE_DDS(arw,dds_create_writer(p,art,reliable_qos,NULL),"AuthResponse writer");
    CREATE_DDS(csr,dds_create_reader(p,cst,reliable_qos,NULL),"ChatSend reader");
    CREATE_DDS(cmw,dds_create_writer(p,cmt,reliable_qos,NULL),"ChatMessage writer");
    CREATE_DDS(jr,dds_create_reader(p,jt,reliable_qos,NULL),"JoinRequest reader");
    CREATE_DDS(ir,dds_create_reader(p,it,stream_qos,NULL),"Input reader");
    CREATE_DDS(ar,dds_create_reader(p,at,reliable_qos,NULL),"GameAction reader");
    CREATE_DDS(rw,dds_create_writer(p,rt,reliable_qos,NULL),"JoinResponse writer");
    CREATE_DDS(pw,dds_create_writer(p,pt,stream_qos,NULL),"Position writer");
    CREATE_DDS(sw,dds_create_writer(p,st,stream_qos,NULL),"GameState writer");

    dds_delete_qos(stream_qos);stream_qos=NULL;
    dds_delete_qos(reliable_qos);reliable_qos=NULL;
    dds_entity_t readers[]={aqr,csr,jr,ir,ar};
    wake=wake_create(p,readers,sizeof(readers)/sizeof(readers[0]));
    if(!dds_ok(wake,"create network waitset"))goto cleanup;
    server_bridge_set_waker(&bridge,wake_notify,&wake);
    uint64_t last_diagnostic=dds_monotonic_ms();
    uint64_t last_state_publish=0;
    uint64_t last_position_publish[MAX_PLAYERS]={0};
    int last_px[MAX_PLAYERS]={-1,-1},last_py[MAX_PLAYERS]={-1,-1};
    int last_revision=-1,last_timer_second=-1;
    printf("[server DDS worker] running\n");
    while(!server_bridge_should_stop(&bridge)){
        /* Reset BEFORE inspecting queues, so a later producer wake is retained. */
        if(!dds_ok(dds_waitset_set_trigger(wake,false),"reset waitset"))goto cleanup;
        dds_return_t n;
        dds_sample_info_t info[1];
        AuthRequest auth={0};void *auths[1]={&auth};
        for(int k=0;k<16;k++){
            n=dds_take(aqr,auths,info,1,1);
            if(!dds_ok(n,"read AuthRequest"))goto cleanup;
            if(n==0)break;
            if(!info[0].valid_data)continue;
            AuthJob job={.nonce=auth.int_nonce,.mode=auth.int_mode};
            snprintf(job.username,sizeof(job.username),"%s",auth.str_username);
            snprintf(job.password,sizeof(job.password),"%s",auth.str_password);
            /* If the queue is full the client simply retries. */
            if(!auth_worker_submit(&job))fprintf(stderr,"[server DDS] auth queue full\n");
            memset(&job,0,sizeof(job));memset(&auth,0,sizeof(auth));
        }
        ChatSend chat={0};void *chats[1]={&chat};
        for(int k=0;k<16;k++){
            n=dds_take(csr,chats,info,1,1);
            if(!dds_ok(n,"read ChatSend"))goto cleanup;
            if(n==0)break;
            if(!info[0].valid_data)continue;
            ServerEvent e={.type=SERVER_CHAT,.player_id=chat.int_player_id,
                           .token=chat.int_client_token};
            snprintf(e.text,sizeof(e.text),"%s",chat.str_text);
            if(!server_bridge_push_event(&bridge,e))
                fprintf(stderr,"[server DDS] chat queue full\n");
        }
        ServerAuthReply auth_reply;
        for(int k=0;k<16 && server_bridge_pop_auth(&bridge,&auth_reply);k++){
            AuthResponse r={0};
            r.int_nonce=auth_reply.nonce;r.int_result=auth_reply.result;
            r.int_session_token=auth_reply.token;
            snprintf(r.str_username,sizeof(r.str_username),"%s",auth_reply.name);
            if(!dds_ok(dds_write(arw,&r),"write AuthResponse"))goto cleanup;
        }
        ServerChatReply chat_reply;
        for(int k=0;k<16 && server_bridge_pop_chat(&bridge,&chat_reply);k++){
            ChatMessage m={0};
            m.int_sequence=chat_reply.sequence;m.int_player_id=chat_reply.player_id;
            snprintf(m.str_username,sizeof(m.str_username),"%s",chat_reply.name);
            snprintf(m.str_text,sizeof(m.str_text),"%s",chat_reply.text);
            if(!dds_ok(dds_write(cmw,&m),"write ChatMessage"))goto cleanup;
        }
        JoinRequest j={0};void *js[1]={&j};
        for(int k=0;k<32;k++){
            n=dds_take(jr,js,info,1,1);
            if(!dds_ok(n,"read JoinRequest"))goto cleanup;
            if(n==0)break;
            if(info[0].valid_data && j.int_client_token>0 &&
               (atomic_fetch_add(&server_join_rx,1),1) &&
               !server_bridge_push_event(&bridge,(ServerEvent){
                   .type=SERVER_JOIN,.token=j.int_client_token}))
                fprintf(stderr,"[server DDS] join queue full\n");
        }
        Input in={0};void *is[1]={&in};
        for(int k=0;k<128;k++){
            n=dds_take(ir,is,info,1,1);
            if(!dds_ok(n,"read Input"))goto cleanup;
            if(n==0)break;
            if(info[0].valid_data && (atomic_fetch_add(&server_input_rx,1),1) &&
               !server_bridge_push_event(&bridge,(ServerEvent){
                .type=SERVER_MOVE,.player_id=in.int_player_id,
                .token=in.int_client_token,.direction=in.ch_direction}))
                fprintf(stderr,"[server DDS] movement queue full\n");
        }
        GameAction a={0};void *as[1]={&a};
        for(int k=0;k<32;k++){
            n=dds_take(ar,as,info,1,1);
            if(!dds_ok(n,"read GameAction"))goto cleanup;
            if(n==0)break;
            if(info[0].valid_data && (atomic_fetch_add(&server_action_rx,1),1) &&
               !server_bridge_push_event(&bridge,(ServerEvent){
                .type=SERVER_ACTION,.player_id=a.int_player_id,
                .token=a.int_client_token,.action=a.int_action,.session=a.int_session}))
                fprintf(stderr,"[server DDS] action queue full\n");
        }
        ServerJoinReply reply;
        for(int k=0;k<64 && server_bridge_pop_reply(&bridge,&reply);k++){
            JoinResponse r={0};
            r.int_client_token=reply.token;r.int_player_id=reply.player_id;
            if(!dds_ok(dds_write(rw,&r),"write JoinResponse"))goto cleanup;
        }
        GameRules snapshot;
        server_bridge_snapshot(&bridge,&snapshot); /* lock only for copy */
        uint64_t now=dds_monotonic_ms();
        /* Position snapshots: at most 10 Hz while moving, 2 Hz when idle.
         * This prevents a constant 25 Hz per-player stream while stationary. */
        for(int i=0;i<MAX_PLAYERS;i++)if(snapshot.players[i].b_active){
            int changed=snapshot.players[i].x!=last_px[i] ||
                        snapshot.players[i].y!=last_py[i];
            uint64_t elapsed=now-last_position_publish[i];
            if((changed && elapsed>=100) || elapsed>=500 || !last_position_publish[i]){
                Position pos={0};pos.int_player_id=i+1;
                pos.int_x=snapshot.players[i].x;
                pos.int_y=snapshot.players[i].y;
                if(!dds_ok(dds_write(pw,&pos),"write Position"))goto cleanup;
                atomic_fetch_add(&server_pos_tx,1);
                last_px[i]=pos.int_x;last_py[i]=pos.int_y;
                last_position_publish[i]=now;
            }
        }
        /* Send game-state changes immediately, timer seconds at ~1 Hz,
         * plus a recovery refresh; no need to stream every 40 ms. */
        int timer_second=(snapshot.time_remaining_ms+999)/1000;
        if(last_revision!=snapshot.revision ||
           last_timer_second!=timer_second ||
           !last_state_publish || now-last_state_publish>=1500){
            GameState s={0};
            s.int_phase=snapshot.phase;s.int_stage=snapshot.stage;
            s.int_actor=snapshot.actor;s.int_mistakes=snapshot.mistakes;
            s.int_held1=snapshot.held[0];s.int_held2=snapshot.held[1];
            s.int_session=snapshot.session;s.int_revision=snapshot.revision;
            s.int_num_players=snapshot.num_players;
            s.int_round=snapshot.round;
            s.int_recipes_completed=snapshot.recipes_completed;
            s.int_used_mask=snapshot.used_mask;
            s.int_recipe_length=snapshot.recipe_len;
            s.int_recipe0=snapshot.recipe[0];
            s.int_recipe1=snapshot.recipe[1];
            s.int_recipe2=snapshot.recipe[2];
            s.int_recipe3=snapshot.recipe[3];
            s.int_timer_remaining_ms=snapshot.time_remaining_ms;
            s.int_timer_limit_ms=snapshot.time_limit_ms;
            snprintf(s.str_name1,sizeof(s.str_name1),"%s",snapshot.players[0].name);
            snprintf(s.str_name2,sizeof(s.str_name2),"%s",snapshot.players[1].name);
            s.int_away_player=game_away_player(&snapshot);
            if(!dds_ok(dds_write(sw,&s),"write GameState"))goto cleanup;
            atomic_fetch_add(&server_state_tx,1);
            last_state_publish=now;
            last_revision=snapshot.revision;
            last_timer_second=timer_second;
        }
        if(dds_monotonic_ms()-last_diagnostic>=2000){
            printf("[server DDS] alive: joins=%lu inputs=%lu actions=%lu posTx=%lu stateTx=%lu\n",
                (unsigned long)atomic_load(&server_join_rx),
                (unsigned long)atomic_load(&server_input_rx),
                (unsigned long)atomic_load(&server_action_rx),
                (unsigned long)atomic_load(&server_pos_tx),
                (unsigned long)atomic_load(&server_state_tx));
            last_diagnostic=dds_monotonic_ms();
        }
        /* Wake for DDS input, bridge output, or the next periodic deadline. */
        uint64_t wait_now=dds_monotonic_ms(),deadline=last_state_publish+1500;
        for(int i=0;i<MAX_PLAYERS;i++)if(snapshot.players[i].b_active){
            int changed=snapshot.players[i].x!=last_px[i] || snapshot.players[i].y!=last_py[i];
            uint64_t due=last_position_publish[i]+(changed?100:500);
            if(due<deadline)deadline=due;
        }
        uint64_t delay=deadline>wait_now?deadline-wait_now:0;
        pthread_mutex_lock(&bridge.mutex);
        int pending=bridge.reply_read!=bridge.reply_write ||
            bridge.auth_read!=bridge.auth_write || bridge.chat_read!=bridge.chat_write || bridge.stop;
        pthread_mutex_unlock(&bridge.mutex);
        if(pending)delay=0; /* Bounded drains must not leave queued replies asleep. */
        if(delay>100)delay=100; /* bounded signal/shutdown checking */
        dds_attach_t triggered[6];
        if(!dds_ok(dds_waitset_wait(wake,triggered,6,DDS_MSECS(delay)),"wait network"))goto cleanup;
    }
    
cleanup:
    server_bridge_set_waker(&bridge,NULL,NULL);
    if(stream_qos)dds_delete_qos(stream_qos);
    if(reliable_qos)dds_delete_qos(reliable_qos);
    dds_delete(p); /* all child entities deleted with their participant */
    printf("[server DDS worker] stopped\n");
    return NULL;
}

static const Session *find_session(int token){
    if(token<=0)return NULL;
    for(int i=0;i<MAX_SESSIONS;i++)
        if(sessions[i].token==token)return &sessions[i];
    return NULL;
}

/* Game-specific login rules: the auth thread has already checked the password. */
static void process_login(GameRules *g,const ServerEvent *e){
    ServerAuthReply reply={.nonce=e->nonce,.result=e->action,.token=e->token};
    snprintf(reply.name,sizeof(reply.name),"%s",e->name);
    if(reply.result==AUTH_OK && !find_session(reply.token)){
        int seated=0;
        for(int i=0;i<MAX_PLAYERS;i++)if(g->players[i].b_active){
            seated++;
            if(strcasecmp(g->players[i].name,reply.name)==0)reply.result=AUTH_ALREADY_PLAYING;
        }
        if(reply.result==AUTH_OK && seated>=MAX_PLAYERS)reply.result=AUTH_GAME_FULL;
        if(reply.result==AUTH_OK){
            Session *slot=&sessions[next_session];
            next_session=(next_session+1)%MAX_SESSIONS;
            slot->token=reply.token;
            slot->guest=e->guest;
            snprintf(slot->name,sizeof(slot->name),"%s",reply.name);
        }
    }
    if(reply.result!=AUTH_OK)reply.token=0;
    if(!server_bridge_push_auth(&bridge,&reply))
        fprintf(stderr,"[server main] auth reply queue full\n");
}
/* Relay only verified, rate-limited, printable text. Name and ID come from the
 * session, never from the message, so a client cannot speak as someone else. */
static void process_chat(const GameRules *g,const ServerEvent *e){
    if(!game_valid_player(g,e->player_id,e->token))return;
    uint64_t now=dds_monotonic_ms();
    uint64_t *last=&last_chat_ms[e->player_id-1];
    if(*last && now-*last<CHAT_MIN_INTERVAL_MS)return;
    ServerChatReply reply={.sequence=++chat_sequence,.player_id=e->player_id};
    size_t length=0;
    for(const char *c=e->text;*c!='\0' && length<CHAT_MAX;c++){
        if(*c>=32 && *c<127)reply.text[length++]=*c;
    }
    if(length==0)return;
    *last=now;
    snprintf(reply.name,sizeof(reply.name),"%s",g->players[e->player_id-1].name);
    if(!server_bridge_push_chat(&bridge,&reply))
        fprintf(stderr,"[server main] chat queue full\n");
}

/* Retire a session token on logout, so it can never be used again. Leaving a
 * game keeps the session: the player stays logged in at the menu. */
static void retire_session(int token){
    for(int i=0;i<MAX_SESSIONS;i++)if(sessions[i].token==token)
        memset(&sessions[i],0,sizeof(sessions[i]));
}

static void process_event(GameRules *g,const ServerEvent *e){
    if(e->type==SERVER_AUTHED){
        process_login(g,e);
    }else if(e->type==SERVER_CHAT){
        game_touch(g,e->player_id,e->token);
        process_chat(g,e);
    }else if(e->type==SERVER_JOIN){
        /* Only a token issued at login can take a seat in the game. */
        const Session *session=find_session(e->token);
        /* One account, one seat: two logged-in windows can't both play as one name. */
        int taken=0;
        for(int i=0;session && i<MAX_PLAYERS;i++)
            if(g->players[i].b_active && g->players[i].token!=e->token &&
               strcasecmp(g->players[i].name,session->name)==0)taken=1;
        int id=0,was_away=game_away_player(g);
        if(session && !taken)id=game_join_named(g,e->token,session->name,session->guest);
        if(!server_bridge_push_reply(&bridge,(ServerJoinReply){
            .token=e->token,.player_id=id}))
            fprintf(stderr,"[server main] join reply queue full\n");
        if(id && id==was_away)printf("[server main] %s rejoined as player %d\n",session->name,id);
        else if(id)printf("[server main] %s -> player %d\n",session->name,id);
    }else if(e->type==SERVER_MOVE){
        game_touch(g,e->player_id,e->token);
        game_move(g,e->player_id,e->token,e->direction);
    }else if(e->type==SERVER_ACTION){
        if(e->action==ACTION_LEAVE || e->action==ACTION_LOGOUT){
            /* Token also handles shutdown during a pending JoinResponse. */
            game_leave(g,e->token);
            if(e->action==ACTION_LOGOUT)retire_session(e->token);
            if(!server_bridge_push_reply(&bridge,(ServerJoinReply){
                .token=e->token,.player_id=-1}))
                fprintf(stderr,"[server main] leave acknowledgement queue full\n");
        }else{
            game_touch(g,e->player_id,e->token);
            if(e->action!=ACTION_HEARTBEAT)
                game_action(g,e->player_id,e->token,e->action,e->session);
        }
    }
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    setvbuf(stderr,NULL,_IONBF,0);
    signal(SIGINT,on_signal);
    signal(SIGTERM,on_signal);
    GameRules game;
    game_init(&game);
    if(!server_bridge_init(&bridge,&game)){
        fprintf(stderr,"Could not initialize server mutex\n");return EXIT_FAILURE;
    }
    if(!auth_worker_start(&bridge)){
        fprintf(stderr,"Could not start the auth thread\n");
        server_bridge_destroy(&bridge);return EXIT_FAILURE;
    }
    pthread_t network_thread;
    if(pthread_create(&network_thread,NULL,dds_worker,NULL)!=0){
        perror("pthread_create(dds_worker)");
        auth_worker_stop();
        server_bridge_destroy(&bridge);return EXIT_FAILURE;
    }
    printf("[server main] Game state loop running (authoritative owner)\n");
    printf("[server main] Recipe 1 (%d ingredients):",game.recipe_len);
    for(int i=0;i<game.recipe_len;i++)printf(" %s",ingredient_names[game.recipe[i]]);
    printf("\n");
    /* ~60 game ticks per second. Main thread performs ALL game state writes.
     * DDS worker NEVER calls game_join/move/action, only queues events. */
    const struct timespec step={.tv_sec=0,.tv_nsec=16000000L};
    uint64_t last_main_log=dds_monotonic_ms();
    unsigned long main_ticks=0;
    while(!quitting && !server_bridge_should_stop(&bridge)){
        /* Check the authoritative deadline before accepting player actions.
           CLOCK_MONOTONIC is immune to wall-clock adjustments. */
        int old_phase=game.phase,old_session=game.session;
        game_tick(&game,dds_monotonic_ms());
        if(game.session!=old_session && game.mistakes>0){
            printf("[server main] stage timeout: oldPhase=%d newPhase=%d "
                   "round=%d stage=%d mistakes=%d\n",
                   old_phase,game.phase,game.round+1,game.stage+1,game.mistakes);
        }
        ServerEvent events[128];
        int previous_round=game.round;
        size_t event_count=server_bridge_pop_batch(&bridge,events,128);
        for(size_t k=0;k<event_count;k++)process_event(&game,&events[k]);
        /* Check after queued heartbeats, so a freshly received heartbeat wins.
         * A timed-out player keeps their login, so after a network blip they can
         * return to the menu and press Play to reclaim their seat. */
        game_expire_players(&game);
        if(game.round!=previous_round){
            printf("[server main] Recipe %d (%d ingredients):",game.round+1,game.recipe_len);
            for(int i=0;i<game.recipe_len;i++)printf(" %s",ingredient_names[game.recipe[i]]);
            printf("\n");
        }
        server_bridge_publish(&bridge,&game);
        main_ticks++;
        uint64_t now_ms=dds_monotonic_ms();
        if(now_ms-last_main_log>=2000){
            pthread_mutex_lock(&bridge.mutex);
            unsigned long dropped=bridge.dropped_events;
            unsigned queue=(bridge.event_write+SERVER_EVENT_CAP-bridge.event_read)%SERVER_EVENT_CAP;
            pthread_mutex_unlock(&bridge.mutex);
            printf("[server MAIN] alive: ticks=%lu players=%d round=%d stage=%d phase=%d rev=%d queued=%u dropped=%lu\n",
                main_ticks,game.num_players,game.round+1,game.stage,game.phase,game.revision,queue,dropped);
            last_main_log=now_ms;
        }
        nanosleep(&step,NULL);
    }
    int failed;
    pthread_mutex_lock(&bridge.mutex);
    failed=bridge.failed;
    pthread_mutex_unlock(&bridge.mutex);
    server_bridge_set_stop(&bridge,0);
    pthread_join(network_thread,NULL);
    auth_worker_stop();
    server_bridge_destroy(&bridge);
    printf("[server main] stopped\n");
    return failed?EXIT_FAILURE:EXIT_SUCCESS;
}
