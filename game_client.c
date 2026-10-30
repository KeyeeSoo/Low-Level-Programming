/* The Gingerbread Curse: two-player client, pure C / GLFW / OpenGL / Cyclone DDS.
   Players are coloured squares. The rune and stirring minigames use Trace.c. */
#include <GLFW/glfw3.h>
#include <dds/dds.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "GameNet.h"
#include "GameLogic.h"
#include "Render.h"
#include "Trace.h"

/* Must be BEFORE the header; include the implementation in one .c file only. */
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

#define SCREEN_W 1040
#define SCREEN_H 740
#define PI 3.14159265358979323846f

typedef struct {
    dds_entity_t participant;
    dds_entity_t join_writer, welcome_reader, command_writer, snapshot_reader;
    int token, player, have_snapshot;
    int previous_interact, previous_drop;
    int pending_result;
    int shown_phase, shown_session, shown_mistakes, shown_ingredient;
    int local_failure;
    uint64_t last_join, last_motion;
    PotionSnapshot state;
    TraceSession trace;
} GameClient;

static GameClient app;

static void solid(float x,float y,float w,float h,float r,float g,float b,float a) {
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r,g,b,a);
    glBegin(GL_QUADS);
    glVertex2f(x,y);glVertex2f(x+w,y);
    glVertex2f(x+w,y+h);glVertex2f(x,y+h);
    glEnd();glPopAttrib();
}
static void border(float x,float y,float w,float h,float r,float g,float b) {
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT|GL_LINE_BIT);
    glDisable(GL_TEXTURE_2D);glColor3f(r,g,b);glLineWidth(2);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);
    glEnd();glPopAttrib();
}
static void text_at(float x,float y,float scale,const char*str,float r,float g,float b) {
    char buffer[65536];
    int quads=stb_easy_font_print(0,0,(char*)str,NULL,buffer,sizeof(buffer));
    if(quads<=0)return;
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);
    glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);
    glColor3f(r,g,b);
    glPushMatrix();glTranslatef(x,y,0);glScalef(scale,scale,1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2,GL_FLOAT,16,buffer);
    glDrawArrays(GL_QUADS,0,quads*4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();glPopAttrib();
}
static void txt(float x,float y,float scale,const char*str) {
    text_at(x,y,scale,str,0.20f,0.15f,0.18f);
}
static dds_entity_t topic(dds_entity_t participant,const dds_topic_descriptor_t*desc,
                          const char*name) {
    dds_entity_t t=dds_create_topic(participant,desc,name,NULL,NULL);
    if(t<0) {fprintf(stderr,"Cannot create %s DDS topic\n",name);exit(1);}
    return t;
}
static void send_command(int action,int dx,int dy) {
    if(!app.player)return;
    PotionCommand cmd={0};
    cmd.token=app.token;cmd.player=app.player;
    cmd.action=action;cmd.dx=dx;cmd.dy=dy;cmd.session=app.state.session;
    dds_write(app.command_writer,&cmd);
}

/* Unlike the ten-corner concave outline, this is a continuous pentagram
   with crossing lines, matching the rune on the user's concept image. */
static void make_pentagram(TraceSession*t) {
    TracePoint points[5];
    const int order[5]={0,2,4,1,3};
    for(int i=0;i<5;i++) {
        float a=-PI/2+(2*PI*order[i])/5;
        points[i]=(TracePoint){650+132*cosf(a),370+132*sinf(a)};
    }
    trace_set_path(t,points,5,1);
    t->shape=TRACE_STAR;
}
static void make_stir(TraceSession*t) {
    TracePoint points[200];
    for(int i=0;i<200;i++) {
        float a=-PI/2+2*PI*i/200;
        points[i]=(TracePoint){650+102*cosf(a),370+102*sinf(a)};
    }
    trace_set_path(t,points,200,1);
    t->shape=TRACE_CIRCLE;
}
static void prepare_trace(void) {
    int phase=app.state.phase;
    if(phase!=POTION_TRACE && phase!=POTION_STIR) return;
    if(phase==POTION_STIR)make_stir(&app.trace);
    else {
        int stage=app.state.next_ingredient;
        switch(stage) {
            case 0: make_pentagram(&app.trace);break;
            case 1: trace_set_shape(&app.trace,TRACE_CIRCLE,650,370);break;
            case 2: trace_set_shape(&app.trace,TRACE_FLOWER,650,370);break;
            default:trace_set_shape(&app.trace,TRACE_RECTANGLE,650,370);break;
        }
    }
    app.pending_result=0;
    app.local_failure=0;
}
static void on_snapshot(const PotionSnapshot*next) {
    int changed=!app.have_snapshot ||
        app.shown_phase!=next->phase ||
        app.shown_session!=next->session ||
        app.shown_mistakes!=next->rune_mistakes ||
        app.shown_ingredient!=next->next_ingredient;
    app.state=*next;
    app.have_snapshot=1;
    if(changed) {
        app.shown_phase=next->phase;
        app.shown_session=next->session;
        app.shown_mistakes=next->rune_mistakes;
        app.shown_ingredient=next->next_ingredient;
        prepare_trace();
    }
}
static void network_poll(void) {
    PotionWelcome welcome={0};
    void *w[1]={&welcome};dds_sample_info_t wi[1];dds_return_t n;
    while((n=dds_take(app.welcome_reader,w,wi,1,1))>0) {
        if(wi[0].valid_data && welcome.token==app.token && !app.player) {
            app.player=welcome.player;
            printf("Joined game as Player %d\n",app.player);
        }
    }
    PotionSnapshot snapshot={0};
    void *s[1]={&snapshot};dds_sample_info_t si[1];
    while((n=dds_take(app.snapshot_reader,s,si,1,1))>0)
        if(si[0].valid_data)on_snapshot(&snapshot);
}
static void send_join(void) {
    PotionJoin j={0};j.token=app.token;
    dds_write(app.join_writer,&j);
}
static int trace_active(void) {
    return app.player>0 && app.have_snapshot &&
        app.state.active_player==app.player &&
        (app.state.phase==POTION_TRACE||app.state.phase==POTION_STIR) &&
        !app.pending_result;
}
static void mouse_button(GLFWwindow *window,int button,int action,int mods) {
    (void)mods;
    if(button!=GLFW_MOUSE_BUTTON_LEFT||!trace_active())return;
    double x,y;glfwGetCursorPos(window,&x,&y);
    if(action==GLFW_PRESS) {
        trace_mouse_down(&app.trace,(float)x,(float)y);
        if(app.trace.status==TRACE_DRAWING)app.local_failure=0;
    } else if(action==GLFW_RELEASE && app.trace.status==TRACE_DRAWING) {
        trace_mouse_up(&app.trace,(float)x,(float)y);
        int success=app.trace.status==TRACE_SUCCESS;
        int action_code=app.state.phase==POTION_TRACE ?
             (success?POTION_TRACE_OK:POTION_TRACE_BAD):
             (success?POTION_STIR_OK:-1);
        /* Stirring may be retried indefinitely; only wrong runes count as fails. */
        if(action_code>=0) {
            send_command(action_code,0,0);
            app.pending_result=1;
        }
        if(!success) {
            app.local_failure=1;
            if(app.state.phase==POTION_STIR)trace_reset(&app.trace);
        }
    }
}
static void cursor_move(GLFWwindow*window,double x,double y) {
    (void)window;
    if(trace_active()&&app.trace.status==TRACE_DRAWING)
        trace_mouse_move(&app.trace,(float)x,(float)y);
}

static void draw_map(void) {
    solid(0,0,SCREEN_W,SCREEN_H,.13f,.11f,.21f,1);
    solid(10,12,1020,86,.21f,.16f,.28f,1);
    text_at(275,26,3.0f,"THE GINGERBREAD CURSE",.98f,.90f,.72f);
    text_at(325,68,1.5f,"TWO PLAYER CO-OP POTION GAME",.82f,.74f,.87f);
    solid(10,110,220,620,.97f,.88f,.75f,1);
    border(10,110,220,620,.44f,.33f,.29f);
    txt(28,133,2.8f,"RECIPE");
    txt(26,180,1.35f,"Add ingredients in order:");
    const float colors[4][3]={
        {.34f,.69f,.35f},{.89f,.52f,.23f},{.58f,.37f,.72f},{.38f,.63f,.89f}
    };
    for(int i=0;i<4;i++){
        float y=220+i*61;
        render_draw_square(26,y,22,colors[i][0],colors[i][1],colors[i][2],1,1);
        char label[64];
        snprintf(label,sizeof(label),"%d %s",i+1,potion_ingredient_names[i]);
        text_at(59,y+7,1.4f,label,.22f,.15f,.16f);
        if(app.have_snapshot&&i<app.state.next_ingredient)
            txt(28,y+27,1.15f,"DONE");
        else if(app.have_snapshot&&i==app.state.next_ingredient)
            txt(28,y+27,1.15f,"NEXT");
    }
    solid(25,500,188,208,.73f,.86f,.78f,1);
    txt(39,521,2.1f,"POTION MINIGAME");
    txt(39,564,1.3f,"Trace the dotted rune");
    txt(39,588,1.3f,"Then stir the potion");
    txt(39,643,1.3f,"3 wrong runes = LOSE");

    solid(250,110,780,515,.34f,.27f,.25f,1);
    border(250,110,780,515,.12f,.09f,.13f);
    /* Subtle floor boards */
    for(int i=0;i<13;i++)solid(253,148+i*37,774,1,.23f,.19f,.19f,.4f);
    /* Attic window and shelves */
    solid(612,122,95,84,.17f,.17f,.29f,1);
    border(612,122,95,84,.66f,.54f,.43f);
    solid(659,123,3,83,.69f,.55f,.41f,1);
    solid(613,164,93,3,.69f,.55f,.41f,1);
    /* Ingredients */
    for(int i=0;i<4;i++) {
        if(app.have_snapshot&&i<app.state.next_ingredient)continue;
        if(app.have_snapshot&&(app.state.held1==i||app.state.held2==i))continue;
        GamePoint p=potion_ingredients[i];
        solid(p.x-47,p.y-35,94,72,.25f,.18f,.17f,1);
        border(p.x-47,p.y-35,94,72,.52f,.41f,.32f);
        render_draw_square(p.x-15,p.y-18,30,colors[i][0],colors[i][1],colors[i][2],1,1);
        char name[25];snprintf(name,sizeof(name),"%s",potion_ingredient_names[i]);
        text_at(p.x-42,p.y+19,1.05f,name,.98f,.88f,.68f);
    }
    /* Cauldron */
    solid(604,325,112,112,.16f,.15f,.17f,1);
    solid(617,337,86,76,.31f,.59f,.35f,1);
    border(604,325,112,112,.08f,.07f,.11f);
    text_at(618,441,1.55f,"CAULDRON",.99f,.86f,.64f);
    solid(633,519,56,48,.29f,.20f,.43f,1);
    txt(616,580,1.4f,"SPELLBOOK");
    /* Player squares */
    for(int p=0;p<2;p++){
        if(!app.have_snapshot||!(p==0?app.state.joined1:app.state.joined2))continue;
        float x=(float)(p==0?app.state.p1x:app.state.p2x);
        float y=(float)(p==0?app.state.p1y:app.state.p2y);
        float cr=p==0?.88f:.43f,cg=p==0?.43f:.45f,cb=p==0?.54f:.93f;
        render_draw_square(x-18,y-18,36,cr,cg,cb,1,1);
        border(x-18,y-18,36,36,.14f,.10f,.16f);
        text_at(x-11,y-49,1.6f,p==0?"P1":"P2",.99f,.92f,.77f);
        int carried=p==0?app.state.held1:app.state.held2;
        if(carried>=0&&carried<4)
            render_draw_square(x+9,y-30,17,colors[carried][0],
                               colors[carried][1],colors[carried][2],1,1);
    }
    solid(250,639,780,91,.86f,.89f,.94f,1);
    txt(271,656,1.55f,"WASD: Move   E: Pick up / Add   Q: Drop");
    txt(271,689,1.45f,"Mouse: Hold, trace dotted rune, then stir");
    if(app.player) {
        char id[30];snprintf(id,sizeof(id),"YOU ARE P%d",app.player);
        txt(830,695,1.5f,id);
    }
}
static void draw_modal(void) {
    if(!app.have_snapshot)return;
    int phase=app.state.phase;
    if(phase==POTION_COLLECT) {
        if(!app.state.joined1||!app.state.joined2) {
            solid(355,278,575,112,.12f,.11f,.19f,.92f);
            text_at(388,306,2.0f,"WAITING FOR TWO PLAYERS",.98f,.89f,.71f);
            text_at(388,345,1.55f,"Both clients must connect before starting",1,1,1);
        }
        return;
    }
    solid(255,112,770,510,.10f,.09f,.16f,.91f);
    solid(340,155,625,435,.95f,.88f,.75f,1);
    border(340,155,625,435,.60f,.44f,.35f);
    if(phase==POTION_WIN||phase==POTION_LOSE){
        text_at(455,280,3.6f,phase==POTION_WIN?"CURSE BROKEN!":"GAME OVER",.27f,.17f,.22f);
        txt(411,363,1.8f,phase==POTION_WIN?
            "The gingerbread children are saved!":
            (app.state.rune_mistakes>=3?
             "Three wrong rune attempts.":
             "Wrong ingredient was added."));
        txt(423,432,1.55f,"Restart the server for a new game.");
        return;
    }
    int mine=app.player&&app.state.active_player==app.player;
    char heading[128];
    if(phase==POTION_TRACE)
        snprintf(heading,sizeof(heading),"TRACE THE %s RUNE",
            (app.state.next_ingredient==0?"STAR":
             app.state.next_ingredient==1?"CIRCLE":
             app.state.next_ingredient==2?"FLOWER":"RECTANGLE"));
    else snprintf(heading,sizeof(heading),"STIR THE POTION");
    text_at(395,178,2.2f,heading,.27f,.17f,.22f);
    char message[160];
    if(!mine)snprintf(message,sizeof(message),"Player %d is drawing. Please wait.",
                      app.state.active_player);
    else if(app.pending_result)snprintf(message,sizeof(message),"Checking your drawing...");
    else if(app.local_failure)snprintf(message,sizeof(message),
                                     "Missed the line. Try again from the green dot.");
    else snprintf(message,sizeof(message),"Hold mouse at GREEN dot, trace, then release.");
    text_at(392,222,1.35f,message,.24f,.20f,.25f);
    if(mine) {
        render_draw_trace(&app.trace);
        char line[144];
        snprintf(line,sizeof(line),"Completion: %d%%    Accuracy: %d%%    Wrong runes: %d / 3",
            (int)(trace_completion(&app.trace)*100),
            (int)(trace_accuracy(&app.trace)*100),app.state.rune_mistakes);
        txt(393,546,1.4f,line);
    } else {
        text_at(408,358,1.8f,"CO-OP PARTNER IS DRAWING...",.37f,.25f,.38f);
    }
}
static void render_scene(void){
    glClearColor(.11f,.1f,.16f,1);glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    draw_map();draw_modal();
    if(!app.player){
        solid(390,285,440,94,.12f,.11f,.19f,.93f);
        text_at(421,320,1.8f,"CONNECTING TO DDS SERVER...",1,.92f,.73f);
    }
}
static int net_init(void) {
    app.participant=dds_create_participant(DDS_DOMAIN_DEFAULT,NULL,NULL);
    if(app.participant<0)return 0;
    dds_entity_t tj=topic(app.participant,&PotionJoin_desc,"PotionJoin");
    dds_entity_t tw=topic(app.participant,&PotionWelcome_desc,"PotionWelcome");
    dds_entity_t tc=topic(app.participant,&PotionCommand_desc,"PotionCommand");
    dds_entity_t ts=topic(app.participant,&PotionSnapshot_desc,"PotionSnapshot");
    app.join_writer=dds_create_writer(app.participant,tj,NULL,NULL);
    app.welcome_reader=dds_create_reader(app.participant,tw,NULL,NULL);
    app.command_writer=dds_create_writer(app.participant,tc,NULL,NULL);
    app.snapshot_reader=dds_create_reader(app.participant,ts,NULL,NULL);
    return app.join_writer>=0&&app.welcome_reader>=0&&
           app.command_writer>=0&&app.snapshot_reader>=0;
}
int main(void) {
    memset(&app,0,sizeof(app));
    app.shown_phase=app.shown_session=app.shown_mistakes=app.shown_ingredient=-1;
    /* Local unique-ish client token. For a classroom prototype, not security. */
    app.token=(int)(((uint32_t)dds_time()^(uint32_t)getpid()^(uint32_t)(uintptr_t)&app)&0x7FFFFFFFu);
    if(app.token==0)app.token=1;
    if(!net_init()){fprintf(stderr,"DDS initialization failed\n");return 1;}
    if(!glfwInit()){fprintf(stderr,"GLFW initialization failed\n");return 1;}
    glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
    GLFWwindow *window=glfwCreateWindow(SCREEN_W,SCREEN_H,
        "The Gingerbread Curse | DDS Co-op",NULL,NULL);
    if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);glfwSwapInterval(1);
    glfwSetMouseButtonCallback(window,mouse_button);
    glfwSetCursorPosCallback(window,cursor_move);
    int fw,fh;glfwGetFramebufferSize(window,&fw,&fh);
    glViewport(0,0,fw,fh);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,SCREEN_W,SCREEN_H,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();network_poll();
        uint64_t now=dds_time();
        if(!app.player&&now-app.last_join>=DDS_MSECS(600)){
            send_join();app.last_join=now;
        }
        if(app.player&&now-app.last_motion>=DDS_MSECS(45)){
            int x=0,y=0;
            if(app.have_snapshot&&app.state.phase==POTION_COLLECT){
                x=(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS)-
                  (glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS);
                y=(glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS)-
                  (glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS);
            }
            send_command(POTION_MOVE,x,y);app.last_motion=now;
        }
        int e=glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS;
        if(e&&!app.previous_interact&&app.have_snapshot&&
           app.state.phase==POTION_COLLECT)send_command(POTION_INTERACT,0,0);
        app.previous_interact=e;
        int q=glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS;
        if(q&&!app.previous_drop&&app.have_snapshot&&
           app.state.phase==POTION_COLLECT)send_command(POTION_DROP,0,0);
        app.previous_drop=q;
        render_scene();glfwSwapBuffers(window);
    }
    render_cleanup();glfwDestroyWindow(window);glfwTerminate();
    dds_delete(app.participant);
    return 0;
}
