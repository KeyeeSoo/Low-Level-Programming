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
/* Copy in FIFO order under one lock; caller processes after unlocking. */
size_t server_bridge_pop_batch(ServerBridge *b,ServerEvent *out,size_t capacity){
    size_t n=0;
    pthread_mutex_lock(&b->mutex);
    while(n<capacity && b->event_read!=b->event_write){
        out[n++]=b->events[b->event_read];
        b->event_read=(b->event_read+1)%SERVER_EVENT_CAP;
    }
    pthread_mutex_unlock(&b->mutex);
    return n;
}
void server_bridge_set_waker(ServerBridge *b,void (*wake)(void *),void *context){
    pthread_mutex_lock(&b->mutex);
    b->wake_network=wake;b->wake_context=context;
    pthread_mutex_unlock(&b->mutex);
}
static void notify_network(ServerBridge *b){
    /* Called with mutex held: unregister cannot race a callback/delete. */
    if(b->wake_network)b->wake_network(b->wake_context);
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
        b->replies[b->reply_write]=reply;b->reply_write=next;ok=1;notify_network(b);
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
/* The auth and chat queues differ only in element type and capacity. */
#define DEFINE_REPLY_QUEUE(suffix,type,items,head,tail,cap) \
int server_bridge_push_##suffix(ServerBridge *b,const type *reply){ \
    int ok=0; \
    pthread_mutex_lock(&b->mutex); \
    unsigned next=(b->tail+1)%(cap); \
    if(next!=b->head){b->items[b->tail]=*reply;b->tail=next;ok=1;notify_network(b);} \
    pthread_mutex_unlock(&b->mutex); \
    return ok; \
} \
int server_bridge_pop_##suffix(ServerBridge *b,type *out){ \
    int ok=0; \
    pthread_mutex_lock(&b->mutex); \
    if(b->head!=b->tail){*out=b->items[b->head];b->head=(b->head+1)%(cap);ok=1;} \
    pthread_mutex_unlock(&b->mutex); \
    return ok; \
}
DEFINE_REPLY_QUEUE(auth,ServerAuthReply,auth_replies,auth_read,auth_write,SERVER_AUTH_REPLY_CAP)
DEFINE_REPLY_QUEUE(chat,ServerChatReply,chat_replies,chat_read,chat_write,SERVER_CHAT_CAP)
void server_bridge_publish(ServerBridge *b,const GameRules *g){
    pthread_mutex_lock(&b->mutex);
    int changed=b->published_state.revision!=g->revision ||
        (b->published_state.time_remaining_ms+999)/1000!=(g->time_remaining_ms+999)/1000;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(b->published_state.players[i].x!=g->players[i].x ||
           b->published_state.players[i].y!=g->players[i].y ||
           b->published_state.players[i].b_active!=g->players[i].b_active)changed=1;
    }
    b->published_state=*g;
    if(changed)notify_network(b);
    pthread_mutex_unlock(&b->mutex);
}
void server_bridge_snapshot(ServerBridge *b,GameRules *g){
    pthread_mutex_lock(&b->mutex);
    *g=b->published_state;
    pthread_mutex_unlock(&b->mutex);
}
void server_bridge_set_stop(ServerBridge *b,int failed){
    pthread_mutex_lock(&b->mutex);
    b->stop=1;if(failed)b->failed=1;notify_network(b);
    pthread_mutex_unlock(&b->mutex);
}
int server_bridge_should_stop(ServerBridge *b){
    pthread_mutex_lock(&b->mutex);
    int stop=b->stop;
    pthread_mutex_unlock(&b->mutex);
    return stop;
}
