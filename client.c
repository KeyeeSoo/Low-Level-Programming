#define _POSIX_C_SOURCE 200809L
/* Gingerbread Curse client, adapted from the original client.c:
   Same JoinRequest/JoinResponse/Input/Position DDS communication, new GameState,
   GameAction, C Render module and mouse rune detection. */
#include <GLFW/glfw3.h>
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
#include "Render.h"
#include "Trace.h"
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

#define PI 3.14159265358979323846f

/* Rendering and tracing belong exclusively to the GLFW/main thread. */
typedef struct {
    int token,id,known_state;
    int visible[MAX_PLAYERS],x[MAX_PLAYERS],y[MAX_PLAYERS];
    GameState state;
    TraceSession trace;
    int shown_phase,shown_stage,shown_session,shown_mistakes;
    int pending_result,prev_e,prev_q;
    double last_input_time;
    int network_failed;
    char network_error[256];
} Client;
static Client app;

/* DDS operates exclusively on the network thread.  Only this small bridge is
 * shared with the main thread. Never hold its mutex during DDS or OpenGL calls. */
#define ACTION_QUEUE_CAP 32
typedef struct { int code,session; } QueuedAction;
typedef struct {
    pthread_mutex_t mutex;
    pthread_t thread;
    int stop,failed;
    char error[256];
    int id,known_state;
    int visible[MAX_PLAYERS],x[MAX_PLAYERS],y[MAX_PLAYERS];
    GameState state;
    char next_direction;
    QueuedAction actions[ACTION_QUEUE_CAP];
    int action_read, action_write;
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

static void rect(float x,float y,float w,float h,float r,float g,float b,float a){
    glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r,g,b,a);glBegin(GL_QUADS);
    glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);
    glEnd();glPopAttrib();
}
static void frame(float x,float y,float w,float h,float r,float g,float b){
    glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_LINE_BIT);
    glDisable(GL_TEXTURE_2D);glColor3f(r,g,b);glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);
    glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();glPopAttrib();
}
static void draw_text(float x,float y,float scale,const char *msg,float r,float g,float b){
    char buf[32768];
    int n=stb_easy_font_print(0,0,(char*)msg,NULL,buf,sizeof(buf));
    if(n<=0)return;
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);
    glDisable(GL_TEXTURE_2D);glColor3f(r,g,b);
    glPushMatrix();glTranslatef(x,y,0);glScalef(scale,scale,1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2,GL_FLOAT,16,buf);
    glDrawArrays(GL_QUADS,0,n*4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();glPopAttrib();
}
static void label(float x,float y,float scale,const char *msg){
    draw_text(x,y,scale,msg,.18f,.14f,.19f);
}
/* Input is queued to the DDS worker; OpenGL callbacks never invoke DDS. */
static void send_input(char dir){
    pthread_mutex_lock(&net.mutex);
    net.next_direction=dir;
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
static void prepare_trace(void){
    if(!app.known_state)return;
    int phase=app.state.int_phase,stage=app.state.int_stage;
    if(phase==PHASE_STIR)make_stir(&app.trace);
    else if(phase==PHASE_TRACE){
        if(stage==0)make_pentagram(&app.trace);
        else if(stage==1)trace_set_shape(&app.trace,TRACE_CIRCLE,500,355);
        else if(stage==2)trace_set_shape(&app.trace,TRACE_FLOWER,500,355);
        else trace_set_shape(&app.trace,TRACE_RECTANGLE,500,355);
    }
    app.pending_result=0;
}
static void receive_state(const GameState *s){
    if(s->int_phase<PHASE_COLLECT || s->int_phase>PHASE_LOSE ||
       s->int_stage<0 || s->int_stage>4 ||
       s->int_actor<0 || s->int_actor>MAX_PLAYERS ||
       s->int_held1 < -1 || s->int_held1>3 ||
       s->int_held2 < -1 || s->int_held2>3 ||
       s->int_num_players<0 || s->int_num_players>MAX_PLAYERS)return;
    int changed=!app.known_state || app.shown_phase!=s->int_phase ||
        app.shown_stage!=s->int_stage || app.shown_session!=s->int_session ||
        app.shown_mistakes!=s->int_mistakes;
    app.state=*s;app.known_state=1;
    if(changed){
        app.shown_phase=s->int_phase;app.shown_stage=s->int_stage;
        app.shown_session=s->int_session;app.shown_mistakes=s->int_mistakes;
        prepare_trace();
    }
}
/* Copy the latest DDS snapshot to the main/GLFW thread under the mutex.
 * prepare_trace() is only called from this main thread. */
static void network_snapshot(void){
    GameState state={0};int have_state,failed;
    pthread_mutex_lock(&net.mutex);
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
    if(have_state)receive_state(&state);
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
    dds_entity_t participant=dds_create_participant(DDS_DOMAIN_DEFAULT,NULL,NULL);
    if(network_error(participant,"dds_create_participant"))return NULL;
    dds_entity_t jt,rt,it,pt,at,st;
    dds_entity_t jw,rr,iw,pr,aw,sr;
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
    uint64_t last_join=0;
    int local_id=0;
    int running=1;
    printf("DDS worker started (pid %ld, token %d)\n",(long)getpid(),app.token);
    while(running){
        int stop;
        pthread_mutex_lock(&net.mutex);
        stop=net.stop;
        pthread_mutex_unlock(&net.mutex);
        if(stop)break;

        dds_return_t n;
        dds_sample_info_t info[1];
        JoinResponse resp={0};void *rs[1]={&resp};
        for(int k=0;k<64;k++){
            n=dds_take(rr,rs,info,1,1);
            if(network_error(n,"take JoinResponse")){running=0;break;}
            if(n==0)break;
            if(info[0].valid_data && resp.int_client_token==app.token && !local_id){
                if(resp.int_player_id>=1 && resp.int_player_id<=MAX_PLAYERS){
                    local_id=resp.int_player_id;
                    pthread_mutex_lock(&net.mutex);
                    net.id=local_id;
                    pthread_mutex_unlock(&net.mutex);
                    printf("Joined as Player %d\n",local_id);
                } else fprintf(stderr,"Join rejected: server full or invalid ID\n");
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
            if(info[0].valid_data && index>=0 && index<MAX_PLAYERS &&
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
            if(info[0].valid_data){
                atomic_fetch_add(&client_state_rx,1);
                pthread_mutex_lock(&net.mutex);
                net.state=state;net.known_state=1;
                pthread_mutex_unlock(&net.mutex);
            }
        }
        if(!running)break;
        uint64_t now=dds_time();
        if(!local_id && (!last_join || now-last_join>=DDS_MSECS(750))){
            JoinRequest req={0};req.b_dummy=1;req.int_client_token=app.token;
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
        pthread_mutex_unlock(&net.mutex);
        if(local_id){
            if(direction!='n'){
                Input in={0};in.int_player_id=local_id;
                in.int_client_token=app.token;in.ch_direction=direction;
                if(network_error(dds_write(iw,&in),"write Input"))break;
                atomic_fetch_add(&client_move_tx,1);
            }
            for(int i=0;i<n_actions;i++){
                GameAction a={0};a.int_player_id=local_id;
                a.int_client_token=app.token;
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
        dds_sleepfor(DDS_MSECS(5));
    }
dds_cleanup:
    if(stream_qos)dds_delete_qos(stream_qos);
    if(rel_qos)dds_delete_qos(rel_qos);
    dds_delete(participant);
    fprintf(stderr,"DDS worker stopped\n");
    return NULL;
}
static int can_trace(void){
    return app.known_state && app.id>0 && app.state.int_actor==app.id &&
        (app.state.int_phase==PHASE_TRACE || app.state.int_phase==PHASE_STIR) &&
        !app.pending_result;
}
static void mouse_button(GLFWwindow *w,int button,int action,int mods){
    (void)mods;
    if(button!=GLFW_MOUSE_BUTTON_LEFT || !can_trace())return;
    double x,y;glfwGetCursorPos(w,&x,&y);
    if(action==GLFW_PRESS)trace_mouse_down(&app.trace,(float)x,(float)y);
    if(action==GLFW_RELEASE && app.trace.status==TRACE_DRAWING){
        trace_mouse_up(&app.trace,(float)x,(float)y);
        int success=app.trace.status==TRACE_SUCCESS;
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
    if(can_trace() && app.trace.status==TRACE_DRAWING)
        trace_mouse_move(&app.trace,(float)x,(float)y);
}
static void draw_scene(void){
    rect(0,0,WIDTH,HEIGHT,.15f,.12f,.22f,1);
    rect(8,8,784,83,.25f,.18f,.28f,1);
    draw_text(205,19,3.1f,"THE GINGERBREAD CURSE",.99f,.85f,.66f);
    draw_text(279,62,1.35f,"TWO-PLAYER POTION GAME",.86f,.79f,.90f);
    rect(9,101,179,488,.96f,.86f,.70f,1);frame(9,101,179,488,.45f,.29f,.21f);
    label(23,119,2.3f,"RECIPE");
    label(20,160,1.05f,"Add in this order:");
    const float colors[4][3]={{.35f,.67f,.37f},{.81f,.48f,.23f},{.52f,.35f,.70f},{.30f,.59f,.83f}};
    for(int i=0;i<4;i++){
        float y=194+i*56;
        render_draw_square(24,y,25,colors[i][0],colors[i][1],colors[i][2],1,1);
        char step[4];snprintf(step,sizeof(step),"%d",i+1);
        draw_text(33,y+6,1.45f,step,1,1,1);
        label(55,y+8,1.12f,ingredient_names[i]);
        if(app.known_state && i<app.state.int_stage)label(162,y+7,1.1f,"OK");
    }
    label(22,455,1.17f,"CONTROLS");
    label(22,481,1.10f,"WASD / Arrows: Move");
    label(22,504,1.10f,"E: Interact  Q: Drop");
    label(22,527,1.08f,"Mouse: Trace / Stir");
    label(22,559,1.05f,"Follow green start dot");
    rect(199,104,588,482,.35f,.27f,.28f,1);
    for(int y=138;y<580;y+=30){
        glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);glDisable(GL_TEXTURE_2D);
        glColor3f(.42f,.33f,.34f);glBegin(GL_LINES);glVertex2f(204,y);glVertex2f(785,y);glEnd();glPopAttrib();
    }
    frame(199,104,588,482,.11f,.08f,.12f);
    if(!app.known_state)return;
    for(int i=app.state.int_stage;i<4;i++){
        int held=(app.state.int_held1==i || app.state.int_held2==i);
        if(held)continue;
        render_draw_square(ingredient_x[i]-21,ingredient_y[i]-21,42,
            colors[i][0],colors[i][1],colors[i][2],1,1);
        label(ingredient_x[i]-33,ingredient_y[i]+27,1.05f,ingredient_names[i]);
    }
    render_draw_square(cauldron_x-34,cauldron_y-31,68,.15f,.17f,.17f,1,1);
    render_draw_square(cauldron_x-23,cauldron_y-20,46,.31f,.73f,.37f,1,1);
    label(cauldron_x-31,cauldron_y+43,1.17f,"CAULDRON");
    for(int i=0;i<MAX_PLAYERS;i++)if(app.visible[i]){
        float r=(i==0)?.82f:.40f,g=(i==0)?.40f:.48f,b=(i==0)?.44f:.85f;
        render_draw_square(app.x[i],app.y[i],SQ_WIDTH,r,g,b,1,1);
        char tag[24];snprintf(tag,sizeof(tag),"P%d",i+1);
        draw_text(app.x[i]+12,app.y[i]+14,1.7f,tag,1,1,1);
        int held=(i==0)?app.state.int_held1:app.state.int_held2;
        if(held>=0)render_draw_square(app.x[i]+35,app.y[i]-12,18,
            colors[held][0],colors[held][1],colors[held][2],1,1);
    }
    char line[120];
    if(app.id==0)snprintf(line,sizeof(line),"Waiting for server / player assignment...");
    else if(app.state.int_num_players<2)snprintf(line,sizeof(line),"Player %d  |  Waiting for Player 2...",app.id);
    else if(app.state.int_phase==PHASE_COLLECT)snprintf(line,sizeof(line),
        "Player %d  |  Next: %s  |  Bring to cauldron (E)",app.id,ingredient_names[app.state.int_stage]);
    else if(app.state.int_phase==PHASE_WIN)snprintf(line,sizeof(line),"YOU WIN! The children are saved.");
    else if(app.state.int_phase==PHASE_LOSE)snprintf(line,sizeof(line),"GAME OVER! Restart server to play again.");
    else snprintf(line,sizeof(line),"Potion stage %d  |  P%d traces",app.state.int_stage+1,app.state.int_actor);
    rect(200,550,585,33,.94f,.85f,.71f,1);
    label(213,558,1.25f,line);
}
static void draw_trace(void){
    if(!app.known_state || (app.state.int_phase!=PHASE_TRACE && app.state.int_phase!=PHASE_STIR))return;
    int mine=app.state.int_actor==app.id && app.id>0;
    rect(212,143,561,398,.17f,.15f,.26f,.97f);
    frame(212,143,561,398,.76f,.68f,.84f);
    char hdr[120];
    if(app.state.int_phase==PHASE_STIR)snprintf(hdr,sizeof(hdr),"STIR THE POTION - TRACE THE CIRCLE");
    else snprintf(hdr,sizeof(hdr),"TRACE RUNE: %s  (mistakes %d/3)",
                  trace_shape_name(app.trace.shape),app.state.int_mistakes);
    draw_text(246,162,1.52f,hdr,.98f,.85f,.69f);
    if(!mine){draw_text(290,205,1.5f,"WAITING FOR OTHER PLAYER",.93f,.85f,.92f);return;}
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT|GL_POINT_BIT|GL_LINE_BIT);
    glDisable(GL_TEXTURE_2D);glPointSize(3.5f);glColor3f(.80f,.77f,.86f);
    glBegin(GL_POINTS);
    for(int i=0;i<app.trace.target_count;i+=3)
        glVertex2f(app.trace.target[i].x,app.trace.target[i].y);
    glEnd();
    if(app.trace.stroke_count>1){
        glLineWidth(3.0f);
        glColor3f(.33f,.77f,1.0f);
        glBegin(GL_LINE_STRIP);
        for(int i=0;i<app.trace.stroke_count;i++)
            glVertex2f(app.trace.stroke[i].x,app.trace.stroke[i].y);
        glEnd();
    }
    if(app.trace.target_count){
        glPointSize(13.0f);glColor3f(.34f,.96f,.38f);glBegin(GL_POINTS);
        glVertex2f(app.trace.target[0].x,app.trace.target[0].y);glEnd();
    }
    glPopAttrib();
    char status[135];
    if(app.pending_result)snprintf(status,sizeof(status),"Checking result with server...");
    else snprintf(status,sizeof(status),"%s | Accuracy %.0f%% | Completion %.0f%%",
         trace_status_name(app.trace.status),100*trace_accuracy(&app.trace),100*trace_completion(&app.trace));
    draw_text(240,500,1.30f,status,.91f,.88f,.96f);
    draw_text(240,521,1.05f,"Click green dot, hold mouse, follow dots, release.",.91f,.88f,.96f);
}
static void handle_keys(GLFWwindow *w){
    int e=glfwGetKey(w,GLFW_KEY_E)==GLFW_PRESS;
    int q=glfwGetKey(w,GLFW_KEY_Q)==GLFW_PRESS;
    if(app.id>0 && app.known_state){
        if(e&&!app.prev_e)send_action(ACTION_INTERACT);
        if(q&&!app.prev_q)send_action(ACTION_DROP);
    }
    app.prev_e=e;app.prev_q=q;
    if(app.id==0 || !app.known_state || app.state.int_phase!=PHASE_COLLECT)return;
    double now=glfwGetTime();
    if(now-app.last_input_time < 0.028)return;
    char d='n';
    if(glfwGetKey(w,GLFW_KEY_W)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_UP)==GLFW_PRESS)d='u';
    else if(glfwGetKey(w,GLFW_KEY_S)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_DOWN)==GLFW_PRESS)d='d';
    else if(glfwGetKey(w,GLFW_KEY_A)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_LEFT)==GLFW_PRESS)d='l';
    else if(glfwGetKey(w,GLFW_KEY_D)==GLFW_PRESS || glfwGetKey(w,GLFW_KEY_RIGHT)==GLFW_PRESS)d='r';
    if(d!='n'){send_input(d);app.last_input_time=now;}
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    setvbuf(stderr,NULL,_IONBF,0);
    memset(&app,0,sizeof(app));
    app.token=(int)((((uint32_t)getpid()<<15) ^ (uint32_t)time(NULL))&0x7fffffff);
    if(app.token==0)app.token=1;
    app.shown_phase=-1;
    if(!glfwInit()){fprintf(stderr,"GLFW init failed\n");return 1;}
    GLFWwindow *w=glfwCreateWindow(WIDTH,HEIGHT,"The Gingerbread Curse - DDS",NULL,NULL);
    if(!w){glfwTerminate();return 1;}
    glfwMakeContextCurrent(w);glfwSwapInterval(0); /* avoid multi-window vsync stalls */
    glfwSetWindowSizeLimits(w,WIDTH,HEIGHT,WIDTH,HEIGHT);
    glfwSetMouseButtonCallback(w,mouse_button);
    glfwSetCursorPosCallback(w,cursor_move);
    int fbw,fbh;glfwGetFramebufferSize(w,&fbw,&fbh);glViewport(0,0,fbw,fbh);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,WIDTH,HEIGHT,0,-1,1);
    glMatrixMode(GL_MODELVIEW);
    if(pthread_create(&net.thread,NULL,network_worker,NULL)!=0){
        perror("pthread_create network_worker");glfwDestroyWindow(w);glfwTerminate();return 1;
    }
    uint64_t last_main_log=monotonic_ms();
    unsigned long main_frames=0;
    const struct timespec frame_pause={.tv_sec=0,.tv_nsec=16000000L};
    while(!glfwWindowShouldClose(w)){
        uint64_t frame_started=monotonic_ms();
        glfwPollEvents();network_snapshot();
        handle_keys(w);
        glClearColor(.15f,.12f,.22f,1);glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();draw_scene();draw_trace();
        if(app.network_failed){
            rect(12,558,775,38,.67f,.08f,.12f,1);
            draw_text(18,570,1.28f,app.network_error,1,1,1);
        }
        glfwSwapBuffers(w);
        main_frames++;
        uint64_t frame_finished=monotonic_ms();
        if(frame_finished-frame_started>200)
            fprintf(stderr,"[client %d MAIN] SLOW FRAME %llu ms (poll+draw+swap)\n",
                (int)getpid(),(unsigned long long)(frame_finished-frame_started));
        if(frame_finished-last_main_log>=2000){
            printf("[client %d MAIN] alive: frames=%lu id=%d known=%d phase=%d rev=%d posRX=%lu stateRX=%lu\n",
                (int)getpid(),main_frames,app.id,app.known_state,
                app.known_state?app.state.int_phase:-1,
                app.known_state?app.state.int_revision:-1,
                (unsigned long)atomic_load(&client_pos_rx),
                (unsigned long)atomic_load(&client_state_rx));
            last_main_log=frame_finished;
        }
        /* With vsync disabled, cap the GLFW main loop near 60 FPS. */
        nanosleep(&frame_pause,NULL);
    }
    /* Exit safely: network worker stops before GLFW/OpenGL teardown. */
    pthread_mutex_lock(&net.mutex);net.stop=1;pthread_mutex_unlock(&net.mutex);
    pthread_join(net.thread,NULL);
    render_cleanup();glfwDestroyWindow(w);glfwTerminate();
    return 0;
}
