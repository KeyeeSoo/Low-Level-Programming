#define _DEFAULT_SOURCE /* getentropy() */
#define _POSIX_C_SOURCE 200809L
/* Gingerbread Curse client, adapted from the original client.c:
   Same JoinRequest/JoinResponse/Input/Position DDS communication, new GameState,
   GameAction, C Render module and mouse rune detection. */
#include <GLFW/glfw3.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <dds/dds.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <math.h>
#include "messages.h"
#include "GameRules.h"
#include "render/Render.h"
#include "render/Art.h"
#include "Trace.h"
#include "SecureDds.h"
#include "DdsWake.h"

#define PI 3.14159265358979323846f

#define CHAT_LINE_LENGTH (CHAT_MAX+USERNAME_MAX+4)
#define CHAT_HISTORY 32
/* How long to wait for the server before telling the player it isn't there. */
#define LOGIN_TIMEOUT_S 12.0
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 256
#define AUDIO_CHANNEL_COUNT 8
#define HOVER_SFX_CHANNEL 0

typedef enum { SCREEN_MENU, SCREEN_LOGIN, SCREEN_SETTINGS, SCREEN_GAME } Screen;
enum {
    HOVER_TARGET_NONE = 0,
    HOVER_TARGET_MENU_BASE = 100,
    HOVER_TARGET_SETTINGS_BASE = 200,
    HOVER_TARGET_LOGIN_BASE = 300,
    HOVER_TARGET_EXIT = 401,
    HOVER_TARGET_DISCONNECT = 402,
    HOVER_TARGET_PLAY_AGAIN = 403,
    HOVER_TARGET_CHAT = 404,
    HOVER_TARGET_ACCOUNT = 405,
    HOVER_TARGET_QUIT = 406
};

/* Window sizes offered in Settings, as a fraction of the full canvas. */
static const float window_size_scales[WINDOW_SIZE_COUNT]={0.7f,1.0f,1.25f};
#define WINDOW_SIZE_NORMAL 1

/* Rendering and tracing belong exclusively to the GLFW/main thread. */
typedef struct {
    Screen screen;
    /* Main menu and settings. Settings last for this run of the game. */
    GLFWwindow *window;
    MenuHit menu_hover;
    char menu_status[96];
    int menu_status_is_error;
    char player_name[USERNAME_MAX+1];
    SettingsHit settings_hover;
    int window_size,animations_on;
    /* Login form. */
    char username[USERNAME_MAX+1],password[PASSWORD_MAX+1];
    int show_password;
    LoginHit login_focus,login_hover;
    int play_again_hover,quit_hover;
    char login_message[96];
    int login_message_is_error,login_busy;
    double login_started;
    /* Chat: messages are wrapped once on arrival, not on every frame. */
    char chat_lines[CHAT_HISTORY][CHAT_LINE_LENGTH];
    int chat_line_players[CHAT_HISTORY],chat_line_count;
    char chat_input[CHAT_MAX+1];
    int chat_focused;
    int token,id,known_state;
    int visible[MAX_PLAYERS],x[MAX_PLAYERS],y[MAX_PLAYERS];
    GameState state;
    TraceSession trace;
    int shown_phase,shown_stage,shown_session,shown_mistakes,shown_round;
    int pending_result,prev_e,prev_q;
    int exit_hover,disconnect_hover,returning_to_menu;
    /* Login state shown on the menu. You stay logged in until Log out or closing the app. */
    int is_guest,login_is_guest,account_hover;
    /* Set when leaving a game whose seat the server is saving for this player. */
    int seat_saved;
    /* Last known partner name, still needed after the server clears a guest's seat. */
    char partner_name[USERNAME_MAX+1];
    double last_input_time;
    int last_hover_target;
    /* Purely visual walk state, derived from the server's position updates. */
    int drawn_x[MAX_PLAYERS],drawn_y[MAX_PLAYERS],facing[MAX_PLAYERS];
    double last_step_time[MAX_PLAYERS];
    int network_failed;
    char network_error[256];
} Client;
static Client app;
static Mix_Chunk *hover_sfx = NULL;
static Mix_Music *background_music = NULL;
static int audio_open = 0;
static int sdl_audio_started = 0;

/* DDS operates exclusively on the network thread.  Only this small bridge is
 * shared with the main thread. Never hold its mutex during DDS or OpenGL calls. */
#define ACTION_QUEUE_CAP 32
#define CHAT_QUEUE_CAP 16
typedef struct { int code,session; } QueuedAction;
typedef struct {
    int player_id;
    char name[USERNAME_MAX+1];
    char text[CHAT_MAX+1];
} QueuedChat;
typedef struct {
    pthread_mutex_t mutex;
    pthread_t thread;
    dds_entity_t wake; /* Protected by mutex, cleared before participant deletion. */
    int stop,failed;
    /* leave_requested leaves the game. With logout_requested set too, the
     * server also ends the login session. Closing the app always logs out. */
    int leave_requested,logout_requested;
    char error[256];
    int id,known_state;
    int visible[MAX_PLAYERS],x[MAX_PLAYERS],y[MAX_PLAYERS];
    GameState state;
    char next_direction;
    QueuedAction actions[ACTION_QUEUE_CAP];
    int action_read, action_write;
    /* Login: main fills these in; the worker sends until answered. */
    int auth_pending,auth_mode,auth_nonce;
    char auth_username[USERNAME_MAX+1],auth_password[PASSWORD_MAX+1];
    int auth_done,auth_result,session_token;
    char auth_name[USERNAME_MAX+1];
    /* Play sets join_requested; the worker reports back if the seat is refused. */
    int join_requested,join_refused;
    /* Chat in both directions. */
    char chat_out[CHAT_QUEUE_CAP][CHAT_MAX+1];
    int chat_out_read,chat_out_write;
    QueuedChat chat_in[CHAT_QUEUE_CAP];
    int chat_in_read,chat_in_write;
} NetworkBridge;
static NetworkBridge net = {.mutex=PTHREAD_MUTEX_INITIALIZER};
static atomic_ulong client_pos_rx=0, client_state_rx=0;
static atomic_ulong client_joins_tx=0, client_move_tx=0;
static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
static dds_qos_t *best_effort_qos(void){
    dds_qos_t *q=dds_create_qos();
    if(!q)return NULL;
    dds_qset_reliability(q,DDS_RELIABILITY_BEST_EFFORT,DDS_MSECS(20));
    dds_qset_history(q,DDS_HISTORY_KEEP_LAST,8);
    return q;
}
static dds_qos_t *reliable_qos(void){
    dds_qos_t *q=dds_create_qos();
    if(!q)return NULL;
    dds_qset_reliability(q,DDS_RELIABILITY_RELIABLE,DDS_MSECS(100));
    dds_qset_history(q,DDS_HISTORY_KEEP_LAST,16);
    return q;
}

/* Only the thread-safe waitset notification crosses threads; readers/writers
 * remain owned by the network worker. Caller holds net.mutex. */
static void notify_network_locked(void){
    if(net.wake>0)(void)dds_waitset_set_trigger(net.wake,true);
}
/* Callbacks queue input and signal the waitset; DDS reads/writes stay on worker. */
static void send_input(char dir){
    pthread_mutex_lock(&net.mutex);
    net.next_direction=dir;
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
}
static void send_action(int action){
    if(app.id<1 || app.id>MAX_PLAYERS || !app.known_state)return;
    pthread_mutex_lock(&net.mutex);
    int next=(net.action_write+1)%ACTION_QUEUE_CAP;
    if(next!=net.action_read){
        net.actions[net.action_write]=(QueuedAction){action,app.state.int_session};
        net.action_write=next;
    }else fprintf(stderr,"GameAction queue full; dropped action %d\n",action);
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
}
/* The star is a five-stroke continuous pentagram, not a concave star outline. */
static void make_pentagram(TraceSession *t){
    TracePoint v[5]; const int order[5]={0,2,4,1,3};
    for(int i=0;i<5;i++){
        float a=-PI/2.0f+2.0f*PI*order[i]/5.0f;
        v[i]=(TracePoint){500.0f+125.0f*cosf(a),355.0f+125.0f*sinf(a)};
    }
    trace_set_path(t,v,5,1);t->shape=TRACE_STAR;
}
static void make_stir(TraceSession *t){
    TracePoint v[180];
    for(int i=0;i<180;i++){
        float a=-PI/2.0f+2.0f*PI*i/180.0f;
        v[i]=(TracePoint){500.0f+104.0f*cosf(a),355.0f+104.0f*sinf(a)};
    }
    trace_set_path(t,v,180,1);t->shape=TRACE_CIRCLE;
}
/* Shape belongs to the INGREDIENT, not its random position in the recipe. */
static int current_recipe_ingredient(void){
    const int ids[RECIPE_MAX_INGREDIENTS]={app.state.int_recipe0,app.state.int_recipe1,
                                     app.state.int_recipe2,app.state.int_recipe3};
    int index=app.state.int_stage;
    return index>=0 && index<app.state.int_recipe_length ? ids[index] : -1;
}
static void prepare_trace(void){
    if(!app.known_state)return;
    int phase=app.state.int_phase,item=current_recipe_ingredient();
    if(phase==PHASE_STIR)make_stir(&app.trace);
    else if(phase==PHASE_TRACE){
        if(item<0 || item>=INGREDIENT_COUNT)return;
        switch(ingredient_rune[item]){
            case 0: make_pentagram(&app.trace);break;
            case 1: trace_set_shape(&app.trace,TRACE_CIRCLE,500,355);break;
            case 2: trace_set_shape(&app.trace,TRACE_FLOWER,500,355);break;
            case 3: trace_set_shape(&app.trace,TRACE_RECTANGLE,500,355);break;
        }
    }
    app.pending_result=0;
}
static void receive_state(const GameState *s){
    if(s->int_phase<PHASE_COLLECT || s->int_phase>PHASE_DISCONNECTED ||
       s->int_recipe_length<RECIPE_MIN_INGREDIENTS ||
       s->int_recipe_length>RECIPE_MAX_INGREDIENTS ||
       s->int_stage<0 || s->int_stage>s->int_recipe_length ||
       s->int_actor<0 || s->int_actor>MAX_PLAYERS ||
       s->int_held1 < -1 || s->int_held1>=INGREDIENT_COUNT ||
       s->int_held2 < -1 || s->int_held2>=INGREDIENT_COUNT ||
       s->int_num_players<0 || s->int_num_players>MAX_PLAYERS ||
       s->int_round<0 || s->int_round>=RECIPES_TO_WIN ||
       s->int_recipes_completed<0 || s->int_recipes_completed>RECIPES_TO_WIN ||
       s->int_used_mask<0 || s->int_used_mask>=(1<<INGREDIENT_COUNT) ||
       s->int_timer_limit_ms<0 || s->int_timer_limit_ms>3600000 ||
       s->int_timer_remaining_ms<0 ||
       s->int_timer_remaining_ms>s->int_timer_limit_ms ||
       s->int_away_player<0 || s->int_away_player>MAX_PLAYERS)return;
    const int recipe[RECIPE_MAX_INGREDIENTS]={s->int_recipe0,s->int_recipe1,
                                         s->int_recipe2,s->int_recipe3};
    int seen=0;
    for(int i=0;i<RECIPE_MAX_INGREDIENTS;i++){
        if(i>=s->int_recipe_length){if(recipe[i]!=-1)return;continue;}
        if(recipe[i]<0 || recipe[i]>=INGREDIENT_COUNT || (seen&(1<<recipe[i])))return;
        seen|=1<<recipe[i];
    }
    int changed=!app.known_state || app.shown_phase!=s->int_phase ||
        app.shown_stage!=s->int_stage || app.shown_session!=s->int_session ||
        app.shown_mistakes!=s->int_mistakes || app.shown_round!=s->int_round;
    app.state=*s;app.known_state=1;
    if(changed){
        app.shown_phase=s->int_phase;app.shown_stage=s->int_stage;
        app.shown_session=s->int_session;app.shown_mistakes=s->int_mistakes;
        app.shown_round=s->int_round;
        prepare_trace();
    }
}
/* Every frame travels through WSLg's Remote Desktop link to reach the screen, and
 * flooding it makes that link stop responding. So input is checked at about 60 Hz,
 * but a frame is only drawn when something changed or an animation is due. */
#define MIN_RENDER_INTERVAL_S (1.0/30.0)
static int redraw_needed=1;
static void set_login_message(const char *message,int is_error){
    redraw_needed=1;
    snprintf(app.login_message,sizeof(app.login_message),"%s",message);
    app.login_message_is_error=is_error;
}
static void set_menu_status(const char *message,int is_error){
    redraw_needed=1;
    snprintf(app.menu_status,sizeof(app.menu_status),"%s",message);
    app.menu_status_is_error=is_error;
}
static void show_logged_in_status(void){
    char status[64];
    snprintf(status,sizeof(status),"Logged in as %s. Press Play!",app.player_name);
    set_menu_status(status,0);
}
static void finish_login(int result,int session_token,const char *name){
    app.login_busy=0;
    switch(result){
        case AUTH_OK:
            app.token=session_token;
            app.is_guest=app.login_is_guest;
            snprintf(app.player_name,sizeof(app.player_name),"%s",name);
            app.screen=SCREEN_MENU;
            memset(app.password,0,sizeof(app.password));
            app.show_password=0;
            set_login_message("",0);
            show_logged_in_status();
            break;
        case AUTH_BAD_CREDENTIALS:
            set_login_message("Wrong username or password.",1);break;
        case AUTH_NAME_TAKEN:
            set_login_message("That username is taken. Try another, or log in.",1);break;
        case AUTH_INVALID_INPUT:
            set_login_message("Name: 3-16 letters, digits, _ or -. Password: 4-32 characters.",1);break;
        case AUTH_ALREADY_PLAYING:
            set_login_message("That player is already in the game.",1);break;
        case AUTH_GAME_FULL:
            set_login_message("The game already has two players.",1);break;
        default:
            set_login_message("The server could not check that. Try again.",1);break;
    }
}
/* Runs once per message, so the word-wrap is never repeated while drawing. */
static void append_chat_line(const char *text,int player){
    if(app.chat_line_count==CHAT_HISTORY){
        memmove(app.chat_lines[0],app.chat_lines[1],sizeof(app.chat_lines[0])*(CHAT_HISTORY-1));
        memmove(&app.chat_line_players[0],&app.chat_line_players[1],
                sizeof(app.chat_line_players[0])*(CHAT_HISTORY-1));
        app.chat_line_count--;
    }
    snprintf(app.chat_lines[app.chat_line_count],CHAT_LINE_LENGTH,"%s",text);
    app.chat_line_players[app.chat_line_count++]=player;
}
static void add_chat_message(const QueuedChat *message){
    char full[CHAT_LINE_LENGTH],line[CHAT_LINE_LENGTH];
    snprintf(full,sizeof(full),"%s: %s",message->name,message->text);
    const char *rest=full;
    while(*rest!='\0'){
        /* Take the longest prefix that fits, preferring to break at a space. */
        size_t fit=0,last_space=0,total=strlen(rest);
        while(fit<total){
            memcpy(line,rest,fit+1);line[fit+1]='\0';
            if(render_text_width(CHAT_LINE_PX,line)>CHAT_LINE_WIDTH)break;
            if(rest[fit]==' ')last_space=fit;
            fit++;
        }
        size_t take=fit;
        if(fit<total && last_space>0)take=last_space;
        if(take==0)take=1;
        memcpy(line,rest,take);line[take]='\0';
        append_chat_line(line,message->player_id-1);
        rest+=take;
        while(*rest==' ')rest++;
    }
}
/* Copy the latest DDS snapshot to the main/GLFW thread under the mutex.
 * prepare_trace() is only called from this main thread. */
static void network_snapshot(void){
    GameState state={0};int have_state,failed,leaving;
    int auth_done=0,auth_result=0,session_token=0,join_refused=0;
    char auth_name[USERNAME_MAX+1]="";
    QueuedChat incoming[CHAT_QUEUE_CAP];int incoming_count=0;
    pthread_mutex_lock(&net.mutex);
    if(net.auth_done){
        auth_done=1;auth_result=net.auth_result;session_token=net.session_token;
        snprintf(auth_name,sizeof(auth_name),"%s",net.auth_name);
        net.auth_done=0;
    }
    if(net.join_refused){join_refused=1;net.join_refused=0;net.join_requested=0;}
    while(net.chat_in_read!=net.chat_in_write){
        incoming[incoming_count++]=net.chat_in[net.chat_in_read];
        net.chat_in_read=(net.chat_in_read+1)%CHAT_QUEUE_CAP;
    }
    leaving=net.leave_requested;
    app.id=net.id;
    memcpy(app.visible,net.visible,sizeof(app.visible));
    memcpy(app.x,net.x,sizeof(app.x));
    memcpy(app.y,net.y,sizeof(app.y));
    have_state=net.known_state;
    if(have_state)state=net.state;
    failed=net.failed;
    if(failed)snprintf(app.network_error,sizeof(app.network_error),"%s",net.error);
    pthread_mutex_unlock(&net.mutex);
    app.network_failed=failed;
    if(have_state){
        int changed=!app.known_state || app.state.int_revision!=state.int_revision ||
            app.state.int_timer_remaining_ms!=state.int_timer_remaining_ms;
        receive_state(&state);
        if(changed)redraw_needed=1;
        /* Empty verified names identify slots removed by the server. */
        if(!state.str_name1[0])app.visible[0]=0;
        if(!state.str_name2[0])app.visible[1]=0;
        /* Remember the partner's name: a guest's is cleared as soon as they leave. */
        if(app.id==1 && state.str_name2[0])
            snprintf(app.partner_name,sizeof(app.partner_name),"%s",state.str_name2);
        else if(app.id==2 && state.str_name1[0])
            snprintf(app.partner_name,sizeof(app.partner_name),"%s",state.str_name1);
    }
    /* Leaving a game keeps the login, so Play works again straight away. */
    if(app.returning_to_menu && !leaving){
        app.returning_to_menu=0;app.screen=SCREEN_MENU;
        app.id=0;app.known_state=0;app.pending_result=0;
        app.chat_focused=0;app.chat_line_count=0;
        memset(app.visible,0,sizeof(app.visible));
        if(app.seat_saved)set_menu_status("Your seat is saved. Press Play to rejoin!",0);
        else show_logged_in_status();
        app.seat_saved=0;
    }
    if(auth_done)finish_login(auth_result,session_token,auth_name);
    if(join_refused){
        app.screen=SCREEN_MENU;
        set_menu_status("Can't join: the game is full or waiting for a player to rejoin.",1);
    }
    for(int i=0;i<incoming_count;i++)add_chat_message(&incoming[i]);
}

static int network_error(dds_return_t rc,const char *operation){
    if(rc>=0)return 0;
    fprintf(stderr,"DDS worker error in %s: %s (%d)\n",
            operation,dds_strretcode(-rc),(int)rc);
    pthread_mutex_lock(&net.mutex);
    net.failed=1;
    snprintf(net.error,sizeof(net.error),"%s: %s",operation,dds_strretcode(-rc));
    pthread_mutex_unlock(&net.mutex);
    return 1;
}

/* This entire function runs off the main thread.  The participant, topics,
 * readers and writers are created, used and deleted on this single thread. */
static void *network_worker(void *unused){
    (void)unused;
    dds_entity_t participant=secure_create_participant("client");
    if(network_error(participant,"secure participant (run.bat rebuilds its certificates)"))return NULL;
    dds_entity_t wake=0;
    dds_entity_t jt,rt,it,pt,at,st;
    dds_entity_t jw,rr,iw,pr,aw,sr;
    dds_entity_t aqt,art,cst,cmt,aqw,arr,csw,cmr;
    dds_qos_t *stream_qos=best_effort_qos(),*rel_qos=reliable_qos();
    if(!stream_qos || !rel_qos){
        fprintf(stderr,"DDS worker: unable to allocate QoS\n");
        pthread_mutex_lock(&net.mutex);net.failed=1;
        snprintf(net.error,sizeof(net.error),"QoS allocation failed");
        pthread_mutex_unlock(&net.mutex);
        goto dds_cleanup;
    }
    /* Do not exit the whole process if DDS startup fails on the worker. */
#define CREATE_DDS(var, expression, label) do { \
    (var)=(expression); \
    if(network_error((var),(label)))goto dds_cleanup; \
} while(0)
    CREATE_DDS(jt,dds_create_topic(participant,&JoinRequest_desc,"JoinRequest",NULL,NULL),"create JoinRequest topic");
    CREATE_DDS(rt,dds_create_topic(participant,&JoinResponse_desc,"JoinResponse",NULL,NULL),"create JoinResponse topic");
    CREATE_DDS(it,dds_create_topic(participant,&Input_desc,"Input",NULL,NULL),"create Input topic");
    CREATE_DDS(pt,dds_create_topic(participant,&Position_desc,"Position",NULL,NULL),"create Position topic");
    CREATE_DDS(at,dds_create_topic(participant,&GameAction_desc,"GameAction",NULL,NULL),"create GameAction topic");
    CREATE_DDS(st,dds_create_topic(participant,&GameState_desc,"GameState",NULL,NULL),"create GameState topic");
    CREATE_DDS(aqt,dds_create_topic(participant,&AuthRequest_desc,"AuthRequest",NULL,NULL),"create AuthRequest topic");
    CREATE_DDS(art,dds_create_topic(participant,&AuthResponse_desc,"AuthResponse",NULL,NULL),"create AuthResponse topic");
    CREATE_DDS(cst,dds_create_topic(participant,&ChatSend_desc,"ChatSend",NULL,NULL),"create ChatSend topic");
    CREATE_DDS(cmt,dds_create_topic(participant,&ChatMessage_desc,"ChatMessage",NULL,NULL),"create ChatMessage topic");
    CREATE_DDS(aqw,dds_create_writer(participant,aqt,rel_qos,NULL),"create AuthRequest writer");
    CREATE_DDS(arr,dds_create_reader(participant,art,rel_qos,NULL),"create AuthResponse reader");
    CREATE_DDS(csw,dds_create_writer(participant,cst,rel_qos,NULL),"create ChatSend writer");
    CREATE_DDS(cmr,dds_create_reader(participant,cmt,rel_qos,NULL),"create ChatMessage reader");
    CREATE_DDS(jw,dds_create_writer(participant,jt,rel_qos,NULL),"create JoinRequest writer");
    CREATE_DDS(rr,dds_create_reader(participant,rt,rel_qos,NULL),"create JoinResponse reader");
    CREATE_DDS(iw,dds_create_writer(participant,it,stream_qos,NULL),"create Input writer");
    CREATE_DDS(pr,dds_create_reader(participant,pt,stream_qos,NULL),"create Position reader");
    CREATE_DDS(aw,dds_create_writer(participant,at,rel_qos,NULL),"create GameAction writer");
    CREATE_DDS(sr,dds_create_reader(participant,st,stream_qos,NULL),"create GameState reader");
#undef CREATE_DDS
    dds_delete_qos(stream_qos);stream_qos=NULL;
    dds_delete_qos(rel_qos);rel_qos=NULL;
    uint64_t last_network_log=monotonic_ms();
    uint64_t last_join=0,last_heartbeat=0,last_leave=0,shutdown_started=0,leave_started=0;
    uint64_t last_auth=0;
    int last_auth_nonce=0;
    int local_id=0,local_token=0;
    int running=1;
    printf("DDS worker started with DDS Security (pid %ld)\n",(long)getpid());
    dds_entity_t readers[]={arr,cmr,rr,pr,sr};
    wake=wake_create(participant,readers,sizeof(readers)/sizeof(readers[0]));
    if(network_error(wake,"create network waitset"))goto dds_cleanup;
    pthread_mutex_lock(&net.mutex);net.wake=wake;pthread_mutex_unlock(&net.mutex);
    while(running){
        if(network_error(dds_waitset_set_trigger(wake,false),"reset waitset"))break;

        int stop,leaving,logging_out;
        pthread_mutex_lock(&net.mutex);
        stop=net.stop;leaving=net.leave_requested || stop;
        logging_out=net.logout_requested || stop;
        if(leaving)net.join_requested=0;
        pthread_mutex_unlock(&net.mutex);
        uint64_t control_now=monotonic_ms();
        if(stop && !shutdown_started)shutdown_started=control_now;
        if(stop && (!local_token || control_now-shutdown_started>=2000))break;
        /* Not logged in: there is nothing to tell the server, so finish at once.
         * A logout also gives up after 2 s, so a missing server can't block it. */
        if(!stop && leaving && (!local_token ||
           (logging_out && leave_started && control_now-leave_started>=2000))){
            if(logging_out)local_token=0;
            local_id=0;last_join=0;last_leave=0;last_heartbeat=0;leave_started=0;
            pthread_mutex_lock(&net.mutex);
            net.id=0;net.known_state=0;net.leave_requested=0;net.logout_requested=0;
            if(logging_out)net.session_token=0;
            memset(net.visible,0,sizeof(net.visible));
            pthread_mutex_unlock(&net.mutex);
            leaving=0;logging_out=0;
        }
        if(leaving && local_token && (!last_leave || control_now-last_leave>=250)){
            GameAction leave={0};
            leave.int_player_id=local_id;leave.int_client_token=local_token;
            leave.int_action=logging_out?ACTION_LOGOUT:ACTION_LEAVE;
            if(network_error(dds_write(aw,&leave),"write Leave"))break;
            last_leave=control_now;
            if(!leave_started)leave_started=control_now;
        }

        dds_return_t n;
        dds_sample_info_t info[1];
        AuthResponse auth={0};void *auths[1]={&auth};
        for(int k=0;k<16;k++){
            n=dds_take(arr,auths,info,1,1);
            if(network_error(n,"take AuthResponse")){running=0;break;}
            if(n==0)break;
            if(!info[0].valid_data)continue;
            pthread_mutex_lock(&net.mutex);
            /* The nonce ties the answer to this client's request. */
            if(net.auth_pending && auth.int_nonce==net.auth_nonce){
                net.auth_pending=0;net.auth_done=1;
                net.auth_result=auth.int_result;net.session_token=auth.int_session_token;
                memset(net.auth_password,0,sizeof(net.auth_password));
                snprintf(net.auth_name,sizeof(net.auth_name),"%s",auth.str_username);
                if(auth.int_result==AUTH_OK)local_token=auth.int_session_token;
            }
            pthread_mutex_unlock(&net.mutex);
        }
        if(!running)break;
        /* Discovery can take a moment, so keep sending until the server answers. */
        {
            AuthRequest request={0};int send_auth=0;
            uint64_t now_ns=dds_time();
            pthread_mutex_lock(&net.mutex);
            if(net.auth_pending && (net.auth_nonce!=last_auth_nonce ||
                                    now_ns-last_auth>=DDS_MSECS(750))){
                send_auth=1;
                request.int_nonce=net.auth_nonce;request.int_mode=net.auth_mode;
                snprintf(request.str_username,sizeof(request.str_username),"%s",net.auth_username);
                snprintf(request.str_password,sizeof(request.str_password),"%s",net.auth_password);
                last_auth_nonce=net.auth_nonce;last_auth=now_ns;
            }
            pthread_mutex_unlock(&net.mutex);
            if(send_auth){
                dds_return_t rc=dds_write(aqw,&request);
                memset(&request,0,sizeof(request));
                if(network_error(rc,"write AuthRequest"))break;
            }
        }
        ChatMessage chat={0};void *chats[1]={&chat};
        for(int k=0;k<16;k++){
            n=dds_take(cmr,chats,info,1,1);
            if(network_error(n,"take ChatMessage")){running=0;break;}
            if(n==0)break;
            if(!info[0].valid_data || chat.int_player_id<1 || chat.int_player_id>MAX_PLAYERS)continue;
            pthread_mutex_lock(&net.mutex);
            int next=(net.chat_in_write+1)%CHAT_QUEUE_CAP;
            if(next!=net.chat_in_read){
                QueuedChat *slot=&net.chat_in[net.chat_in_write];
                slot->player_id=chat.int_player_id;
                snprintf(slot->name,sizeof(slot->name),"%s",chat.str_username);
                snprintf(slot->text,sizeof(slot->text),"%s",chat.str_text);
                net.chat_in_write=next;
            }
            pthread_mutex_unlock(&net.mutex);
        }
        if(!running)break;
        JoinResponse resp={0};void *rs[1]={&resp};
        for(int k=0;k<64;k++){
            n=dds_take(rr,rs,info,1,1);
            if(network_error(n,"take JoinResponse")){running=0;break;}
            if(n==0)break;
            if(info[0].valid_data && local_token && resp.int_client_token==local_token &&
               resp.int_player_id==-1 && leaving){
                /* Only a logout ends the login; leaving keeps the token for Play. */
                if(logging_out)local_token=0;
                local_id=0;last_join=0;last_leave=0;last_heartbeat=0;leave_started=0;
                pthread_mutex_lock(&net.mutex);
                net.id=0;net.known_state=0;net.leave_requested=0;net.logout_requested=0;
                if(logging_out)net.session_token=0;
                net.join_requested=0;net.next_direction='n';
                net.action_read=net.action_write;net.chat_out_read=net.chat_out_write;
                memset(net.visible,0,sizeof(net.visible));
                pthread_mutex_unlock(&net.mutex);
                continue;
            }
            if(info[0].valid_data && local_token && resp.int_client_token==local_token && !local_id && !leaving){
                if(resp.int_player_id>=1 && resp.int_player_id<=MAX_PLAYERS){
                    local_id=resp.int_player_id;
                    pthread_mutex_lock(&net.mutex);
                    net.id=local_id;
                    pthread_mutex_unlock(&net.mutex);
                    printf("Joined as Player %d\n",local_id);
                } else {
                    fprintf(stderr,"Join rejected: server full or invalid ID\n");
                    pthread_mutex_lock(&net.mutex);
                    if(net.join_requested)net.join_refused=1;
                    pthread_mutex_unlock(&net.mutex);
                }
            }
        }
        if(!running)break;
        Position pos={0};void *ps[1]={&pos};
        for(int k=0;k<128;k++){
            n=dds_take(pr,ps,info,1,1);
            if(network_error(n,"take Position")){running=0;break;}
            if(n==0)break;
            int index=pos.int_player_id-1;
            if(info[0].valid_data)atomic_fetch_add(&client_pos_rx,1);
            if(local_id && info[0].valid_data && index>=0 && index<MAX_PLAYERS &&
               pos.int_x>=0 && pos.int_x<WIDTH && pos.int_y>=0 && pos.int_y<HEIGHT){
                pthread_mutex_lock(&net.mutex);
                net.visible[index]=1;net.x[index]=pos.int_x;net.y[index]=pos.int_y;
                pthread_mutex_unlock(&net.mutex);
            }
        }
        if(!running)break;
        GameState state={0};void *ss[1]={&state};
        for(int k=0;k<64;k++){
            n=dds_take(sr,ss,info,1,1);
            if(network_error(n,"take GameState")){running=0;break;}
            if(n==0)break;
            if(local_id && info[0].valid_data){
                atomic_fetch_add(&client_state_rx,1);
                pthread_mutex_lock(&net.mutex);
                net.state=state;net.known_state=1;
                pthread_mutex_unlock(&net.mutex);
            }
        }
        if(!running)break;
        uint64_t now=dds_time();
        /* Joining needs the session token, so it can only happen after login. */
        pthread_mutex_lock(&net.mutex);
        int join_requested=net.join_requested;
        pthread_mutex_unlock(&net.mutex);
        if(!leaving && join_requested && local_token && !local_id && (!last_join || now-last_join>=DDS_MSECS(750))){
            JoinRequest req={0};req.b_dummy=1;req.int_client_token=local_token;
            if(network_error(dds_write(jw,&req),"write JoinRequest"))break;
            atomic_fetch_add(&client_joins_tx,1);
            last_join=now;
        }
        /* Drain queued input without holding mutex while calling DDS. */
        char direction='n';
        QueuedAction actions[ACTION_QUEUE_CAP];int n_actions=0;
        pthread_mutex_lock(&net.mutex);
        direction=net.next_direction;net.next_direction='n';
        while(net.action_read!=net.action_write){
            actions[n_actions++]=net.actions[net.action_read];
            net.action_read=(net.action_read+1)%ACTION_QUEUE_CAP;
        }
        char chat_out[CHAT_QUEUE_CAP][CHAT_MAX+1];int n_chat=0;
        while(net.chat_out_read!=net.chat_out_write){
            memcpy(chat_out[n_chat++],net.chat_out[net.chat_out_read],CHAT_MAX+1);
            net.chat_out_read=(net.chat_out_read+1)%CHAT_QUEUE_CAP;
        }
        pthread_mutex_unlock(&net.mutex);
        if(local_id && !leaving){
            uint64_t heartbeat_now=monotonic_ms();
            if(!last_heartbeat || heartbeat_now-last_heartbeat>=PLAYER_HEARTBEAT_MS){
                GameAction heartbeat={0};heartbeat.int_player_id=local_id;
                heartbeat.int_client_token=local_token;heartbeat.int_action=ACTION_HEARTBEAT;
                if(network_error(dds_write(aw,&heartbeat),"write Heartbeat"))break;
                last_heartbeat=heartbeat_now;
            }
            for(int i=0;i<n_chat;i++){
                ChatSend message={0};
                message.int_player_id=local_id;message.int_client_token=local_token;
                memcpy(message.str_text,chat_out[i],sizeof(message.str_text));
                if(network_error(dds_write(csw,&message),"write ChatSend")){running=0;break;}
            }
            if(!running)break;
            if(direction!='n'){
                Input in={0};in.int_player_id=local_id;
                in.int_client_token=local_token;in.ch_direction=direction;
                if(network_error(dds_write(iw,&in),"write Input"))break;
                atomic_fetch_add(&client_move_tx,1);
            }
            for(int i=0;i<n_actions;i++){
                GameAction a={0};a.int_player_id=local_id;
                a.int_client_token=local_token;
                a.int_action=actions[i].code;
                a.int_session=actions[i].session;
                if(network_error(dds_write(aw,&a),"write GameAction")){running=0;break;}
            }
        }
        if(!running)break;
        if(monotonic_ms()-last_network_log>=2000){
            printf("[client %d DDS] alive: joinsSent=%lu movesSent=%lu posRX=%lu stateRX=%lu\n",
                (int)getpid(),(unsigned long)atomic_load(&client_joins_tx),
                (unsigned long)atomic_load(&client_move_tx),
                (unsigned long)atomic_load(&client_pos_rx),
                (unsigned long)atomic_load(&client_state_rx));
            last_network_log=monotonic_ms();
        }
        if(stop && !local_token)break;
        /* Retry clocks use a bounded timeout; gameplay work wakes immediately. */
        dds_attach_t triggered[6];
        if(network_error(dds_waitset_wait(wake,triggered,6,DDS_MSECS(50)),"wait network"))break;
    }
dds_cleanup:
    pthread_mutex_lock(&net.mutex);net.wake=0;pthread_mutex_unlock(&net.mutex);
    if(stream_qos)dds_delete_qos(stream_qos);
    if(rel_qos)dds_delete_qos(rel_qos);
    dds_delete(participant);
    fprintf(stderr,"DDS worker stopped\n");
    return NULL;
}
static int can_trace(void){
    return !app.returning_to_menu && app.known_state && app.id>0 && app.state.int_actor==app.id &&
        (app.state.int_phase==PHASE_TRACE || app.state.int_phase==PHASE_STIR) &&
        !app.pending_result;
}
static void audio_play_hover_sfx(void){
    if(!audio_open || hover_sfx==NULL)return;
    (void)Mix_PlayChannel(HOVER_SFX_CHANNEL,hover_sfx,0);
}
static void audio_track_hover_target(int target){
    if(target==app.last_hover_target)return;
    app.last_hover_target=target;
    if(target!=HOVER_TARGET_NONE)audio_play_hover_sfx();
}
static void audio_init(void){
    if(SDL_Init(SDL_INIT_AUDIO)!=0){
        fprintf(stderr,"Audio init failed: %s\n",SDL_GetError());
        return;
    }
    sdl_audio_started=1;
    int ready=Mix_Init(MIX_INIT_MP3);
    if((ready&MIX_INIT_MP3)!=MIX_INIT_MP3)
        fprintf(stderr,"MP3 decoder unavailable: %s\n",Mix_GetError());
    if(Mix_OpenAudio(AUDIO_SAMPLE_RATE,MIX_DEFAULT_FORMAT,2,AUDIO_BUFFER_SIZE)!=0){
        fprintf(stderr,"Audio device open failed: %s\n",Mix_GetError());
        return;
    }
    audio_open=1;
    Mix_AllocateChannels(AUDIO_CHANNEL_COUNT);
    Mix_ReserveChannels(1);
    Mix_Volume(HOVER_SFX_CHANNEL,MIX_MAX_VOLUME);
    hover_sfx=Mix_LoadWAV("assets/llp sfx.mp3");
    if(hover_sfx==NULL)
        fprintf(stderr,"Could not load hover SFX: %s\n",Mix_GetError());
    background_music=Mix_LoadMUS("assets/llp music.mp3");
    if(background_music==NULL){
        fprintf(stderr,"Could not load background music: %s\n",Mix_GetError());
    }else if(Mix_PlayMusic(background_music,-1)!=0){
        fprintf(stderr,"Could not start background music: %s\n",Mix_GetError());
    }
}
static void audio_cleanup(void){
    if(audio_open){
        Mix_HaltMusic();
        if(background_music!=NULL){
            Mix_FreeMusic(background_music);
            background_music=NULL;
        }
        if(hover_sfx!=NULL){
            Mix_FreeChunk(hover_sfx);
            hover_sfx=NULL;
        }
        Mix_CloseAudio();
        audio_open=0;
    }
    Mix_Quit();
    if(sdl_audio_started){
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_Quit();
        sdl_audio_started=0;
    }
}
static int menu_hover_target(MenuHit hit){
    if(hit==MENU_HIT_NONE)return HOVER_TARGET_NONE;
    return HOVER_TARGET_MENU_BASE+(int)hit;
}
static int settings_hover_target(SettingsHit hit){
    if(hit==SETTINGS_HIT_NONE)return HOVER_TARGET_NONE;
    return HOVER_TARGET_SETTINGS_BASE+(int)hit;
}
static int login_hover_target(LoginHit hit){
    if(hit==LOGIN_HIT_NONE)return HOVER_TARGET_NONE;
    return HOVER_TARGET_LOGIN_BASE+(int)hit;
}
/* The canvas is letterboxed into the window so it can be resized to fit small
 * screens. Tracing always works in canvas coordinates, whatever the window size. */
static float view_scale=1.0f,view_x=0.0f,view_y=0.0f;
static void framebuffer_resized(GLFWwindow *w,int fb_w,int fb_h){
    int win_w,win_h;glfwGetWindowSize(w,&win_w,&win_h);
    if(fb_w<=0 || fb_h<=0 || win_w<=0 || win_h<=0)return; /* Minimised. */
    float fit_x=(float)fb_w/CANVAS_W,fit_y=(float)fb_h/CANVAS_H;
    float fit=fit_x;
    if(fit_y<fit)fit=fit_y;
    int vw=(int)(CANVAS_W*fit),vh=(int)(CANVAS_H*fit);
    int vx=(fb_w-vw)/2,vy=(fb_h-vh)/2;
    glViewport(vx,vy,vw,vh);
    /* Cursor positions arrive in window units, not framebuffer pixels. */
    float pixels_per_unit=(float)fb_w/(float)win_w;
    view_scale=fit/pixels_per_unit;
    view_x=(float)vx/pixels_per_unit;
    view_y=(float)vy/pixels_per_unit;
    art_invalidate_static();
    redraw_needed=1;
}
static TracePoint window_to_canvas(double x,double y){
    return (TracePoint){((float)x-view_x)/view_scale,((float)y-view_y)/view_scale};
}
static void submit_login(int mode){
    if(app.login_busy)return;
    if(mode!=AUTH_MODE_GUEST && (app.username[0]=='\0' || app.password[0]=='\0')){
        if(mode==AUTH_MODE_REGISTER)set_login_message("Pick a username and password, then click Sign Up.",1);
        else set_login_message("Type your username and password.",1);
        return;
    }
    if(mode!=AUTH_MODE_GUEST && (strlen(app.username)<USERNAME_MIN || strlen(app.password)<PASSWORD_MIN)){
        set_login_message("Name: 3-16 letters, digits, _ or -. Password: 4-32 characters.",1);
        return;
    }
    int nonce=0;
    /* A fresh random nonce per attempt, so a retry can never reuse an old answer. */
    if(getentropy(&nonce,sizeof(nonce))!=0)nonce=(int)time(NULL);
    nonce&=0x7fffffff;
    if(nonce==0)nonce=1;
    pthread_mutex_lock(&net.mutex);
    /* The old session's logout must reach the server before a new login starts. */
    if(net.logout_requested){
        pthread_mutex_unlock(&net.mutex);
        set_login_message("Still logging out. Try again in a moment.",1);
        return;
    }
    net.auth_nonce=nonce;net.auth_mode=mode;net.auth_pending=1;
    /* A guest has no credentials to send, even if something was typed in the form. */
    net.auth_username[0]='\0';
    net.auth_password[0]='\0';
    if(mode!=AUTH_MODE_GUEST){
        snprintf(net.auth_username,sizeof(net.auth_username),"%s",app.username);
        snprintf(net.auth_password,sizeof(net.auth_password),"%s",app.password);
    }
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
    app.login_is_guest=mode==AUTH_MODE_GUEST;
    app.login_busy=1;
    app.login_started=glfwGetTime();
    set_login_message("Checking with the server...",0);
}
static void check_login_timeout(void){
    if(!app.login_busy || glfwGetTime()-app.login_started<LOGIN_TIMEOUT_S)return;
    pthread_mutex_lock(&net.mutex);
    net.auth_pending=0;
    memset(net.auth_password,0,sizeof(net.auth_password));
    pthread_mutex_unlock(&net.mutex);
    app.login_busy=0;
    set_login_message("Can't reach the server. Is it running?",1);
}
static void press_play(void){
    if(!app.token){app.screen=SCREEN_LOGIN;redraw_needed=1;return;}
    pthread_mutex_lock(&net.mutex);
    net.join_requested=1;
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
    app.partner_name[0]='\0';
    app.screen=SCREEN_GAME;
}
/* Only request departure here; the DDS worker sends and waits for its ACK. */
static void return_to_menu(void){
    if(app.returning_to_menu)return;
    app.returning_to_menu=1;app.chat_focused=0;app.pending_result=0;
    /* This player timed out and the server is holding their seat. */
    app.seat_saved=app.known_state && app.id>0 && app.state.int_away_player==app.id;
    pthread_mutex_lock(&net.mutex);
    net.leave_requested=1;net.join_requested=0;net.next_direction='n';
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
    redraw_needed=1;
}
/* Menu badge button: Log out for accounts, Log in for guests. Either way the
 * server ends this session and the player goes to the login page. */
static void log_out(void){
    int was_guest=app.is_guest;
    pthread_mutex_lock(&net.mutex);
    net.leave_requested=1;net.logout_requested=1;net.join_requested=0;
    notify_network_locked();
    pthread_mutex_unlock(&net.mutex);
    app.token=0;app.is_guest=0;app.player_name[0]='\0';
    app.account_hover=0;app.menu_status[0]='\0';
    memset(app.password,0,sizeof(app.password));
    app.login_focus=LOGIN_HIT_USERNAME;
    app.screen=SCREEN_LOGIN;
    if(was_guest)set_login_message("",0);
    else set_login_message("You have been logged out.",0);
}
static void apply_window_size(int size){
    float scale=window_size_scales[size];
    int area_x,area_y,area_w=0,area_h=0;
    GLFWmonitor *monitor=glfwGetPrimaryMonitor();
    if(monitor)glfwGetMonitorWorkarea(monitor,&area_x,&area_y,&area_w,&area_h);
    /* Never open larger than the screen can show. */
    if(area_h>0 && CANVAS_H*scale>area_h-60)scale=(float)(area_h-60)/CANVAS_H;
    app.window_size=size;
    glfwSetWindowSize(app.window,(int)(CANVAS_W*scale),(int)(CANVAS_H*scale));
}
static void settings_clicked(SettingsHit hit){
    switch(hit){
        case SETTINGS_HIT_SIZE_SMALL:
        case SETTINGS_HIT_SIZE_NORMAL:
        case SETTINGS_HIT_SIZE_LARGE:
            apply_window_size((int)hit-(int)SETTINGS_HIT_SIZE_SMALL);break;
        case SETTINGS_HIT_ANIMATIONS_ON:app.animations_on=1;break;
        case SETTINGS_HIT_ANIMATIONS_OFF:app.animations_on=0;break;
        case SETTINGS_HIT_BACK:app.screen=SCREEN_MENU;break;
        default:break;
    }
}
static void send_chat(void){
    if(app.chat_input[0]!='\0'){
        pthread_mutex_lock(&net.mutex);
        int next=(net.chat_out_write+1)%CHAT_QUEUE_CAP;
        if(next!=net.chat_out_read){
            memcpy(net.chat_out[net.chat_out_write],app.chat_input,sizeof(app.chat_input));
            net.chat_out_write=next;
        }
        notify_network_locked();
        pthread_mutex_unlock(&net.mutex);
    }
    app.chat_input[0]='\0';
}
/* Appends a printable character, respecting the buffer's size. */
static void append_char(char *buffer,size_t size,unsigned codepoint){
    size_t length=strlen(buffer);
    if(codepoint<32 || codepoint>126 || length+1>=size)return;
    buffer[length]=(char)codepoint;buffer[length+1]='\0';
}
static void remove_char(char *buffer){
    size_t length=strlen(buffer);
    if(length>0)buffer[length-1]='\0';
}
/* Diagnostics for client logs. Which keys were pressed is never logged: the player
 * may be typing a password. */
static void log_input(const char *what,float x,float y,int hit){
    fprintf(stderr,"[input] %s at %.0f,%.0f screen=%d hit=%d\n",what,x,y,(int)app.screen,hit);
}
static void window_focus_changed(GLFWwindow *w,int focused){
    (void)w;
    if(focused)fprintf(stderr,"[input] window gained focus\n");
    else fprintf(stderr,"[input] window lost focus\n");
    redraw_needed=1;
}
static void window_needs_refresh(GLFWwindow *w){
    (void)w;
    redraw_needed=1;
}
static void character_typed(GLFWwindow *w,unsigned codepoint){
    (void)w;
    redraw_needed=1;
    if(app.screen==SCREEN_LOGIN){
        if(app.login_busy)return;
        if(app.login_focus==LOGIN_HIT_USERNAME)append_char(app.username,sizeof(app.username),codepoint);
        else if(app.login_focus==LOGIN_HIT_PASSWORD)append_char(app.password,sizeof(app.password),codepoint);
    }else if(app.chat_focused){
        append_char(app.chat_input,sizeof(app.chat_input),codepoint);
    }
}
static void key_event(GLFWwindow *w,int key,int scancode,int action,int mods){
    (void)w;(void)scancode;(void)mods;
    if(action==GLFW_RELEASE)return;
    redraw_needed=1;
    static int keys_logged=0;
    if(keys_logged<5){keys_logged++;log_input("key press",0,0,0);}
    if(app.screen==SCREEN_MENU)return;
    if(app.screen==SCREEN_SETTINGS){
        if(key==GLFW_KEY_ESCAPE)app.screen=SCREEN_MENU;
        return;
    }
    if(app.screen==SCREEN_LOGIN){
        if(key==GLFW_KEY_TAB){
            if(app.login_focus==LOGIN_HIT_USERNAME)app.login_focus=LOGIN_HIT_PASSWORD;
            else app.login_focus=LOGIN_HIT_USERNAME;
        }
        else if(key==GLFW_KEY_ENTER || key==GLFW_KEY_KP_ENTER)submit_login(AUTH_MODE_LOGIN);
        else if(key==GLFW_KEY_BACKSPACE && !app.login_busy){
            if(app.login_focus==LOGIN_HIT_USERNAME)remove_char(app.username);
            else remove_char(app.password);
        }
        return;
    }
    if(app.chat_focused){
        if(key==GLFW_KEY_ENTER || key==GLFW_KEY_KP_ENTER){send_chat();app.chat_focused=0;}
        else if(key==GLFW_KEY_ESCAPE)app.chat_focused=0;
        else if(key==GLFW_KEY_BACKSPACE)remove_char(app.chat_input);
    }else if(action==GLFW_PRESS && (key==GLFW_KEY_ENTER || key==GLFW_KEY_KP_ENTER)){
        app.chat_focused=1;
    }
}
static int game_finished(void){
    return app.known_state && (app.state.int_phase==PHASE_WIN || app.state.int_phase==PHASE_LOSE);
}
static void mouse_button(GLFWwindow *w,int button,int action,int mods){
    (void)mods;
    if(button!=GLFW_MOUSE_BUTTON_LEFT)return;
    redraw_needed=1;
    double cursor_x,cursor_y;glfwGetCursorPos(w,&cursor_x,&cursor_y);
    TracePoint p=window_to_canvas(cursor_x,cursor_y);
    if(app.screen==SCREEN_GAME && art_exit_hit_test(p.x,p.y)){
        if(action==GLFW_PRESS)glfwSetWindowShouldClose(w,GLFW_TRUE);
        return;
    }
    if(app.screen==SCREEN_GAME && app.known_state &&
       (app.state.int_phase==PHASE_DISCONNECTED || app.returning_to_menu)){
        if(action==GLFW_PRESS && art_disconnect_hit_test(p.x,p.y))return_to_menu();
        return;
    }
    if(action==GLFW_PRESS){
        int hit=0;
        if(app.screen==SCREEN_MENU)hit=(int)art_menu_hit_test(p.x,p.y);
        else if(app.screen==SCREEN_LOGIN)hit=(int)art_login_hit_test(p.x,p.y);
        else if(app.screen==SCREEN_SETTINGS)hit=(int)art_settings_hit_test(p.x,p.y);
        log_input("click",p.x,p.y,hit);
    }
    if(app.screen==SCREEN_MENU){
        if(action!=GLFW_PRESS)return;
        if(app.token && art_account_hit_test(p.x,p.y)){log_out();return;}
        switch(art_menu_hit_test(p.x,p.y)){
            case MENU_HIT_PLAY:press_play();break;
            case MENU_HIT_SETTINGS:app.screen=SCREEN_SETTINGS;break;
            case MENU_HIT_QUIT:glfwSetWindowShouldClose(w,GLFW_TRUE);break;
            default:break;
        }
        return;
    }
    if(app.screen==SCREEN_SETTINGS){
        if(action==GLFW_PRESS)settings_clicked(art_settings_hit_test(p.x,p.y));
        return;
    }
    if(app.screen==SCREEN_LOGIN){
        if(action!=GLFW_PRESS)return;
        LoginHit hit=art_login_hit_test(p.x,p.y);
        if(hit==LOGIN_HIT_USERNAME || hit==LOGIN_HIT_PASSWORD)app.login_focus=hit;
        else if(hit==LOGIN_HIT_LOGIN)submit_login(AUTH_MODE_LOGIN);
        else if(hit==LOGIN_HIT_SIGNUP)submit_login(AUTH_MODE_REGISTER);
        else if(hit==LOGIN_HIT_GUEST)submit_login(AUTH_MODE_GUEST);
        else if(hit==LOGIN_HIT_EYE)app.show_password=!app.show_password;
        return;
    }
    if(action==GLFW_PRESS)app.chat_focused=art_chat_hit_test(p.x,p.y);
    if(action==GLFW_PRESS && game_finished() && art_play_again_hit_test(p.x,p.y)){
        send_action(ACTION_RESTART);
        return;
    }
    if(action==GLFW_PRESS && game_finished() && art_quit_hit_test(p.x,p.y)){
        glfwSetWindowShouldClose(w,GLFW_TRUE); /* Same as the Exit button. */
        return;
    }
    if(!can_trace())return;
    if(action==GLFW_PRESS)trace_mouse_down(&app.trace,p.x,p.y);
    if(action==GLFW_RELEASE && app.trace.status==TRACE_DRAWING){
        trace_mouse_up(&app.trace,p.x,p.y);
        int success=app.trace.status==TRACE_SUCCESS;
        fprintf(stderr,"[trace] %s shape=%s direction=%d accuracy=%.1f%% completion=%.1f%% session=%d\n",
            success?"SUCCESS":"FAILED",trace_shape_name(app.trace.shape),
            app.trace.direction,100.0f*trace_accuracy(&app.trace),
            100.0f*trace_completion(&app.trace),app.state.int_session);
        if(app.state.int_phase==PHASE_TRACE){
            send_action(success?ACTION_TRACE_OK:ACTION_TRACE_BAD);
            app.pending_result=1; /* Wait for server's new state. */
        }else if(success){
            send_action(ACTION_STIR_OK);app.pending_result=1;
        }else trace_reset(&app.trace); /* Stirring is freely retryable. */
    }
}
static void cursor_move(GLFWwindow *w,double x,double y){
    (void)w;
    if(app.screen!=SCREEN_GAME){
        TracePoint p=window_to_canvas(x,y);
        int hover_target=HOVER_TARGET_NONE;
        /* Moving the mouse only needs a new frame if the highlight actually changed. */
        if(app.screen==SCREEN_MENU){
            MenuHit hit=art_menu_hit_test(p.x,p.y);
            int account_hover=app.token && art_account_hit_test(p.x,p.y);
            if(hit!=app.menu_hover || account_hover!=app.account_hover)redraw_needed=1;
            app.menu_hover=hit;
            app.account_hover=account_hover;
            if(account_hover)hover_target=HOVER_TARGET_ACCOUNT;
            else hover_target=menu_hover_target(hit);
        }else if(app.screen==SCREEN_SETTINGS){
            SettingsHit hit=art_settings_hit_test(p.x,p.y);
            if(hit!=app.settings_hover)redraw_needed=1;
            app.settings_hover=hit;
            hover_target=settings_hover_target(hit);
        }else{
            LoginHit hit=art_login_hit_test(p.x,p.y);
            if(hit!=app.login_hover)redraw_needed=1;
            app.login_hover=hit;
            hover_target=login_hover_target(hit);
        }
        audio_track_hover_target(hover_target);
        return;
    }
    TracePoint hover_point=window_to_canvas(x,y);
    int exit_hover=art_exit_hit_test(hover_point.x,hover_point.y);
    int showing_disconnect=app.known_state &&
        (app.state.int_phase==PHASE_DISCONNECTED || app.returning_to_menu);
    int disconnect_hover=showing_disconnect && art_disconnect_hit_test(hover_point.x,hover_point.y);
    if(exit_hover!=app.exit_hover || disconnect_hover!=app.disconnect_hover)redraw_needed=1;
    app.exit_hover=exit_hover;app.disconnect_hover=disconnect_hover;
    int hover_target=HOVER_TARGET_NONE;
    if(exit_hover)hover_target=HOVER_TARGET_EXIT;
    else if(disconnect_hover)hover_target=HOVER_TARGET_DISCONNECT;
    if(game_finished()){
        TracePoint p=window_to_canvas(x,y);
        app.play_again_hover=art_play_again_hit_test(p.x,p.y);
        app.quit_hover=art_quit_hit_test(p.x,p.y);
        if(hover_target==HOVER_TARGET_NONE && app.play_again_hover)
            hover_target=HOVER_TARGET_PLAY_AGAIN;
        if(hover_target==HOVER_TARGET_NONE && app.quit_hover)
            hover_target=HOVER_TARGET_QUIT;
    }
    if(hover_target==HOVER_TARGET_NONE && !showing_disconnect &&
       art_chat_hit_test(hover_point.x,hover_point.y))
        hover_target=HOVER_TARGET_CHAT;
    audio_track_hover_target(hover_target);
    if(can_trace() && app.trace.status==TRACE_DRAWING){
        TracePoint p=window_to_canvas(x,y);
        trace_mouse_move(&app.trace,p.x,p.y);
        redraw_needed=1;
    }
}
/* Compare against the last drawn position, so walking animates from the same
 * snapshots on every client without any extra network traffic. */
static void update_walk_state(double now){
    for(int i=0;i<MAX_PLAYERS;i++){
        if(!app.visible[i])continue;
        if(app.x[i]!=app.drawn_x[i] || app.y[i]!=app.drawn_y[i]){
            if(app.x[i]>app.drawn_x[i])app.facing[i]=1;
            else if(app.x[i]<app.drawn_x[i])app.facing[i]=-1;
            app.drawn_x[i]=app.x[i];app.drawn_y[i]=app.y[i];
            app.last_step_time[i]=now;
        }
    }
}
static void draw_players(double now){
    /* Painter's order: whoever is lower on screen stands in front. */
    int order[MAX_PLAYERS];
    for(int i=0;i<MAX_PLAYERS;i++)order[i]=i;
    if(MAX_PLAYERS==2 && app.y[0]>app.y[1]){order[0]=1;order[1]=0;}
    for(int k=0;k<MAX_PLAYERS;k++){
        int i=order[k];
        if(!app.visible[i])continue;
        /* Movement arrives at ~18 Hz, so "still walking" must outlast one gap. */
        float walk=0.0f;
        if(now-app.last_step_time[i]<0.14)walk=(float)(now*14.0);
        int held=-1;
        const char *name="";
        if(app.known_state && i==0){held=app.state.int_held1;name=app.state.str_name1;}
        else if(app.known_state){held=app.state.int_held2;name=app.state.str_name2;}
        /* Until they first move, the players face each other. */
        int facing=app.facing[i];
        if(facing==0){
            if(i==0)facing=1;
            else facing=-1;
        }
        art_draw_player(i,app.x[i],app.y[i],facing,walk,held,i+1==app.id,name);
    }
}
/* The player's account name, or "Player N" before their name has arrived. */
static const char *player_label(int id,char *fallback,size_t size){
    const char *name="";
    if(id==1)name=app.state.str_name1;
    else if(id==2)name=app.state.str_name2;
    if(name[0]!='\0')return name;
    snprintf(fallback,size,"Player %d",id);
    return fallback;
}
static void status_message(char *line,size_t size,const int *recipe){
    int stage=app.state.int_stage;
    char fallback[16];
    if(app.id==0)snprintf(line,size,"Waiting for server / player assignment...");
    else if(app.state.int_phase==PHASE_WIN)snprintf(line,size,
        "You did it! All %d potions brewed together. Play again?",RECIPES_TO_WIN);
    else if(app.state.int_phase==PHASE_LOSE)snprintf(line,size,
        "Game over! Click Play Again to try again.");
    else if(app.state.int_num_players<2)snprintf(line,size,
        "You are Player %d. Waiting for your partner to log in...",app.id);
    else if(app.state.int_phase==PHASE_COLLECT && stage<app.state.int_recipe_length){
        int collector=((app.state.int_round+stage)%2)+1;
        if(collector==app.id)snprintf(line,size,"Your turn: bring %s to the cauldron (E).",
            ingredient_names[recipe[stage]]);
        else snprintf(line,size,"%s is fetching %s. Get ready to trace!",
            player_label(collector,fallback,sizeof(fallback)),ingredient_names[recipe[stage]]);
    }
    else if(app.state.int_actor==app.id && app.state.int_phase==PHASE_TRACE)
        snprintf(line,size,"Your turn: trace the rune!");
    else if(app.state.int_actor==app.id)snprintf(line,size,"Your turn: stir the potion!");
    else if(app.state.int_phase==PHASE_TRACE)snprintf(line,size,
        "%s is tracing the rune. You stir next.",
        player_label(app.state.int_actor,fallback,sizeof(fallback)));
    else snprintf(line,size,"%s is stirring the potion...",
        player_label(app.state.int_actor,fallback,sizeof(fallback)));
}
static void draw_scene(double now){
    /* With animations off, scenery time stands still. Typing carets keep blinking. */
    double scenery_time=0.0;
    if(app.animations_on)scenery_time=now;
    if(app.screen==SCREEN_MENU){
        art_draw_menu(app.menu_hover,app.menu_status,app.menu_status_is_error,(float)scenery_time);
        if(app.token)art_draw_account_badge(app.player_name,app.is_guest,app.account_hover);
        return;
    }
    if(app.screen==SCREEN_SETTINGS){
        art_draw_settings(app.window_size,app.animations_on,app.settings_hover);
        return;
    }
    if(app.screen==SCREEN_LOGIN){
        check_login_timeout();
        LoginView view={.username=app.username,.password=app.password,.show_password=app.show_password,
            .focus=app.login_focus,.hover=app.login_hover,.message=app.login_message,
            .message_is_error=app.login_message_is_error,.busy=app.login_busy};
        art_draw_login(&view,(float)now);
        return;
    }
    art_draw_static(ART_LAYER_GAME);
    art_draw_chat((const char (*)[CHAT_LINE_LENGTH])app.chat_lines,app.chat_line_players,
                  app.chat_line_count,app.chat_input,app.chat_focused,(float)now);
    /* Before the first snapshot there is no recipe yet: the preview just shows a star. */
    int preview_rune=0;
    if(app.known_state){
        int item=current_recipe_ingredient();
        if(item>=0)preview_rune=ingredient_rune[item];
    }
    art_draw_ambient((float)scenery_time,preview_rune);
    if(!app.known_state){
        art_draw_status("Waiting for the server...",-1.0f,0);
        return;
    }
    const int recipe[RECIPE_MAX_INGREDIENTS]={app.state.int_recipe0,app.state.int_recipe1,
                                        app.state.int_recipe2,app.state.int_recipe3};
    art_draw_recipe(recipe,app.state.int_recipe_length,app.state.int_stage,
                    app.state.int_round,app.state.int_mistakes);
    for(int i=0;i<INGREDIENT_COUNT;i++){
        if(app.state.int_used_mask&(1<<i))art_draw_empty_crate(i);
        else if(app.state.int_held1!=i && app.state.int_held2!=i)
            art_draw_ingredient(i,ingredient_x[i],ingredient_y[i]-5,1.0f);
    }
    update_walk_state(now);
    draw_players(now);
    char line[180];
    status_message(line,sizeof(line),recipe);
    int counting=app.state.int_num_players>=2 && app.state.int_timer_limit_ms>0;
    float time_fraction=-1.0f;
    if(counting)time_fraction=(float)app.state.int_timer_remaining_ms/(float)app.state.int_timer_limit_ms;
    art_draw_status(line,time_fraction,(app.state.int_timer_remaining_ms+999)/1000);
    if(app.state.int_phase==PHASE_WIN || app.state.int_phase==PHASE_LOSE)
        art_draw_end_card(app.state.int_phase==PHASE_WIN,app.state.int_recipes_completed,
                          app.play_again_hover,app.quit_hover,(float)scenery_time);
}
static void draw_trace(void){
    if(app.screen!=SCREEN_GAME || !app.known_state || (app.state.int_phase!=PHASE_TRACE && app.state.int_phase!=PHASE_STIR))return;
    int mine=app.state.int_actor==app.id && app.id>0;
    char title[120],subtitle[120];
    if(app.state.int_phase==PHASE_STIR){
        snprintf(title,sizeof(title),"Stir the potion");
        snprintf(subtitle,sizeof(subtitle),"Trace the circle, either direction");
    }else{
        snprintf(title,sizeof(title),"Trace the %s rune",trace_shape_name(app.trace.shape));
        snprintf(subtitle,sizeof(subtitle),"Mistakes %d/3 - start at the green dot",app.state.int_mistakes);
    }
    art_draw_trace_panel(title,subtitle);
    /* The rune dashes are shown to both players, so the partner can watch. */
    float rune_alpha=0.35f;
    if(mine)rune_alpha=0.9f;
    for(int i=0;i+1<app.trace.target_count;i++){
        if(i%6>=4)continue;
        render_color_a(0xC9B7EEu,rune_alpha);
        render_line(app.trace.target[i].x,app.trace.target[i].y,
                    app.trace.target[i+1].x,app.trace.target[i+1].y,2.4f);
    }
    if(!mine){
        render_color(0xFBEBCFu);
        render_text_centered(492,340,22,"Waiting for your partner...");
        return;
    }
    render_color(0x6FD6FFu);
    for(int i=1;i<app.trace.stroke_count;i++)
        render_line(app.trace.stroke[i-1].x,app.trace.stroke[i-1].y,
                    app.trace.stroke[i].x,app.trace.stroke[i].y,3.4f);
    if(app.trace.target_count){
        render_color(0x1B4D22u);
        render_ellipse(app.trace.target[0].x,app.trace.target[0].y,9.0f,9.0f);
        render_color(0x57F06Au);
        render_ellipse(app.trace.target[0].x,app.trace.target[0].y,7.0f,7.0f);
    }
    char status[135];
    if(app.pending_result)snprintf(status,sizeof(status),"Checking with the server...");
    else snprintf(status,sizeof(status),"%s  -  accuracy %.0f%%  -  completed %.0f%%",
         trace_status_name(app.trace.status),100*trace_accuracy(&app.trace),100*trace_completion(&app.trace));
    render_color(0xE5E0F2u);
    render_text_centered(492,512,14.5f,status);
}
/* Card over a paused game. GameState.int_away_player says whose seat is saved;
 * 0 means the player who left was a guest, and guests can't rejoin. */
static void draw_disconnect_card(void){
    char line1[96],line2[96];
    const char *title="Player disconnected";
    const char *mine="";
    if(app.id==1)mine=app.state.str_name1;
    else if(app.id==2)mine=app.state.str_name2;
    const char *partner="Your partner";
    if(app.partner_name[0])partner=app.partner_name;
    int away=app.state.int_away_player;
    if(away && away==app.id){
        title="You were disconnected";
        snprintf(line1,sizeof(line1),"Your seat is saved while your partner waits.");
        snprintf(line2,sizeof(line2),"Return to the menu, then press Play to rejoin.");
    }else if(away){
        snprintf(line1,sizeof(line1),"The game and timer are paused.");
        snprintf(line2,sizeof(line2),"Waiting for %s to log back in and rejoin...",partner);
    }else if(app.id && !mine[0]){
        title="You were disconnected";
        snprintf(line1,sizeof(line1),"Guests can't rejoin a game.");
        snprintf(line2,sizeof(line2),"Return to the menu to start a new one.");
    }else{
        snprintf(line1,sizeof(line1),"%s was a guest and can't rejoin.",partner);
        snprintf(line2,sizeof(line2),"Please return to the menu to start a new game.");
    }
    art_draw_disconnect_card(title,line1,line2,app.returning_to_menu,app.disconnect_hover);
}
/* The longest a screen may go without a new frame. */
static double render_interval(void){
    switch(app.screen){
        case SCREEN_GAME:
            if(app.animations_on)return 1.0/15.0;
            return 0.1;
        case SCREEN_MENU:
            if(app.animations_on)return 1.0/8.0;
            return 1.0;
        case SCREEN_LOGIN:return 0.5; /* Just the blinking caret. */
        default:return 1.0;
    }
}
static void handle_keys(GLFWwindow *w){
    /* Letters typed into the login form or chat must not also move the player. */
    if(app.screen!=SCREEN_GAME || app.chat_focused){
        app.prev_e=app.prev_q=1;
        return;
    }
    int e=glfwGetKey(w,GLFW_KEY_E)==GLFW_PRESS;
    int q=glfwGetKey(w,GLFW_KEY_Q)==GLFW_PRESS;
    if(app.returning_to_menu || (app.known_state && app.state.int_phase==PHASE_DISCONNECTED))return;
    if(app.id>0 && app.known_state){
        if(e&&!app.prev_e)send_action(ACTION_INTERACT);
        if(q&&!app.prev_q)send_action(ACTION_DROP);
    }
    app.prev_e=e;app.prev_q=q;
    if(app.id==0 || !app.known_state || app.state.int_phase!=PHASE_COLLECT)return;
    double now=glfwGetTime();
    if(now-app.last_input_time < 0.055)return; /* ~18 Hz instead of ~36 Hz */
    char d='n';
    if(glfwGetKey(w,GLFW_KEY_W)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_UP)==GLFW_PRESS)d='u';
    else if(glfwGetKey(w,GLFW_KEY_S)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_DOWN)==GLFW_PRESS)d='d';
    else if(glfwGetKey(w,GLFW_KEY_A)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_LEFT)==GLFW_PRESS)d='l';
    else if(glfwGetKey(w,GLFW_KEY_D)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_RIGHT)==GLFW_PRESS)d='r';
    if(d!='n'){send_input(d);app.last_input_time=now;}
}
/* slot 1 opens at the left of the screen and slot 2 at the right, so run.bat's two
 * players don't open exactly on top of each other. */
static void place_window(GLFWwindow *w,int slot,int area_x,int area_y,int area_w){
    int win_w,win_h;
    if(area_w<=0)return;
    glfwGetWindowSize(w,&win_w,&win_h);
    int x=area_x;
    if(slot!=1)x=area_x+area_w-win_w;
    if(x<area_x)x=area_x;
    glfwSetWindowPos(w,x,area_y+30);
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);
    setvbuf(stderr,NULL,_IONBF,0);
    memset(&app,0,sizeof(app));
    app.screen=SCREEN_LOGIN;
    app.login_focus=LOGIN_HIT_USERNAME;
    app.window_size=WINDOW_SIZE_NORMAL;
    app.animations_on=1;
    app.shown_phase=-1;
    app.shown_round=-1;
    if(!glfwInit()){fprintf(stderr,"GLFW init failed\n");return 1;}
    /* No multisampling: WSL often has no GPU here, and on the software driver it
     * costs about five times the CPU per client. */
    glfwWindowHint(GLFW_SAMPLES,0);
    /* Open at full size, unless that would run off a small laptop screen. */
    int area_x=0,area_y=0,area_w=0,area_h=0;
    GLFWmonitor *monitor=glfwGetPrimaryMonitor();
    if(monitor)glfwGetMonitorWorkarea(monitor,&area_x,&area_y,&area_w,&area_h);
    float open_scale=1.0f;
    if(area_h>0 && area_h-60<CANVAS_H)open_scale=(float)(area_h-60)/CANVAS_H;
    GLFWwindow *w=glfwCreateWindow((int)(CANVAS_W*open_scale),(int)(CANVAS_H*open_scale),
                                   "The Gingerbread Curse - DDS",NULL,NULL);
    if(!w){glfwTerminate();return 1;}
    if(argc>1)place_window(w,atoi(argv[1]),area_x,area_y,area_w);
    glfwFocusWindow(w);
    glfwSetWindowFocusCallback(w,window_focus_changed);
    glfwSetWindowRefreshCallback(w,window_needs_refresh);
    glfwMakeContextCurrent(w);glfwSwapInterval(0); /* avoid multi-window vsync stalls */
    app.window=w;
    glfwSetWindowAspectRatio(w,CANVAS_W,CANVAS_H);
    glfwSetMouseButtonCallback(w,mouse_button);
    glfwSetCursorPosCallback(w,cursor_move);
    glfwSetCharCallback(w,character_typed);
    glfwSetKeyCallback(w,key_event);
    glfwSetFramebufferSizeCallback(w,framebuffer_resized);
    int fbw,fbh;glfwGetFramebufferSize(w,&fbw,&fbh);framebuffer_resized(w,fbw,fbh);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,CANVAS_W,CANVAS_H,0,-1,1);
    glMatrixMode(GL_MODELVIEW);
    audio_init();
    render_init("assets/PatrickHand-Regular.ttf");
    art_init();
    if(pthread_create(&net.thread,NULL,network_worker,NULL)!=0){
        perror("pthread_create network_worker");
        audio_cleanup();
        glfwDestroyWindow(w);
        glfwTerminate();
        return 1;
    }
    uint64_t last_main_log=monotonic_ms();
    unsigned long main_frames=0;
    double last_render=-1.0;
    while(!glfwWindowShouldClose(w)){
        /* Sleeps until input arrives, or until the next network and key check is due. */
        glfwWaitEventsTimeout(0.016);
        if(glfwWindowShouldClose(w))break;
        network_snapshot();
        handle_keys(w);
        double now=glfwGetTime();
        double since_render=now-last_render;
        if((redraw_needed && since_render>=MIN_RENDER_INTERVAL_S) || since_render>=render_interval()){
            uint64_t frame_started=monotonic_ms();
            redraw_needed=0;
            last_render=now;
            glClearColor(.09f,.08f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);
            glLoadIdentity();draw_scene(now);draw_trace();
            if(app.network_failed){
                render_color(0xAB141Fu);render_rect(199,590,588,33);
                render_color(0xFFFFFFu);render_text(208,597,17,app.network_error);
            }
            if(app.screen==SCREEN_GAME){
                if(app.known_state && (app.state.int_phase==PHASE_DISCONNECTED || app.returning_to_menu))
                    draw_disconnect_card();
                art_draw_exit_button(app.exit_hover);
            }
            glfwSwapBuffers(w);
            main_frames++;
            uint64_t frame_finished=monotonic_ms();
            if(frame_finished-frame_started>200)
                fprintf(stderr,"[client %d MAIN] SLOW FRAME %llu ms (draw+swap)\n",
                    (int)getpid(),(unsigned long long)(frame_finished-frame_started));
        }
        uint64_t log_time=monotonic_ms();
        if(log_time-last_main_log>=2000){
            printf("[client %d MAIN] alive: frames=%lu id=%d known=%d phase=%d rev=%d posRX=%lu stateRX=%lu\n",
                (int)getpid(),main_frames,app.id,app.known_state,
                app.known_state?app.state.int_phase:-1,
                app.known_state?app.state.int_revision:-1,
                (unsigned long)atomic_load(&client_pos_rx),
                (unsigned long)atomic_load(&client_state_rx));
            last_main_log=log_time;
        }
    }
    /* Exit safely: network worker stops before GLFW/OpenGL teardown. */
    pthread_mutex_lock(&net.mutex);net.stop=1;notify_network_locked();pthread_mutex_unlock(&net.mutex);
    pthread_join(net.thread,NULL);
    audio_cleanup();
    art_cleanup();render_cleanup();glfwDestroyWindow(w);glfwTerminate();
    return 0;
}
