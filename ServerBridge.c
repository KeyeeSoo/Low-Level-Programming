#include "ServerBridge.h"
#include <string.h>

int server_bridge_init(ServerBridge *b,const GameRules *initial){
    memset(b,0,sizeof(*b));
    if(pthread_mutex_init(&b->mutex,NULL)!=0)return 0;
    b->published_state=*initial;
    return 1;
}
void server_bridge_destroy(ServerBridge *b){pthread_mutex_destroy(&b->mutex);}
int server_bridge_push_event(ServerBridge *b,ServerEvent e){
    int ok=0;
    pthread_mutex_lock(&b->mutex);
    unsigned next=(b->event_write+1)%SERVER_EVENT_CAP;
    if(next!=b->event_read){b->events[b->event_write]=e;b->event_write=next;ok=1;}
    else b->dropped_events++;
    pthread_mutex_unlock(&b->mutex);
    return ok;
}
int server_bridge_pop_event(ServerBridge *b,ServerEvent *e){
    int ok=0;
    pthread_mutex_lock(&b->mutex);
    if(b->event_read!=b->event_write){
        *e=b->events[b->event_read];b->event_read=(b->event_read+1)%SERVER_EVENT_CAP;ok=1;
    }
    pthread_mutex_unlock(&b->mutex);
    return ok;
}
int server_bridge_push_reply(ServerBridge *b,ServerJoinReply reply){
    int ok=0;
    pthread_mutex_lock(&b->mutex);
    unsigned next=(b->reply_write+1)%SERVER_REPLY_CAP;
    if(next!=b->reply_read){
        b->replies[b->reply_write]=reply;b->reply_write=next;ok=1;
    }
    pthread_mutex_unlock(&b->mutex);
    return ok;
}
int server_bridge_pop_reply(ServerBridge *b,ServerJoinReply *reply){
    int ok=0;
    pthread_mutex_lock(&b->mutex);
    if(b->reply_read!=b->reply_write){
        *reply=b->replies[b->reply_read];b->reply_read=(b->reply_read+1)%SERVER_REPLY_CAP;ok=1;
    }
    pthread_mutex_unlock(&b->mutex);
    return ok;
}
void server_bridge_publish(ServerBridge *b,const GameRules *g){
    pthread_mutex_lock(&b->mutex);
    b->published_state=*g;
    pthread_mutex_unlock(&b->mutex);
}
void server_bridge_snapshot(ServerBridge *b,GameRules *g){
    pthread_mutex_lock(&b->mutex);
    *g=b->published_state;
    pthread_mutex_unlock(&b->mutex);
}
void server_bridge_set_stop(ServerBridge *b,int failed){
    pthread_mutex_lock(&b->mutex);
    b->stop=1;if(failed)b->failed=1;
    pthread_mutex_unlock(&b->mutex);
}
int server_bridge_should_stop(ServerBridge *b){
    pthread_mutex_lock(&b->mutex);
    int stop=b->stop;
    pthread_mutex_unlock(&b->mutex);
    return stop;
}
