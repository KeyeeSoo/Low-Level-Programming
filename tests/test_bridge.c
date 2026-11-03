#include "ServerBridge.h"
#include <assert.h>
#include <stdio.h>
#include <sched.h>
static ServerBridge b;
static int wakes;
static void wake(void *p){++*(int *)p;}
static void *produce(void *unused){
    (void)unused;
    for(int i=0;i<100000;i++){
        ServerEvent e={.type=SERVER_MOVE,.token=i};
        while(!server_bridge_push_event(&b,e))sched_yield();
    }
    return NULL;
}
int main(void){
    GameRules g={0};assert(server_bridge_init(&b,&g));
    ServerEvent out[128];
    assert(server_bridge_pop_batch(&b,out,128)==0);
    for(int i=0;i<SERVER_EVENT_CAP-1;i++)assert(server_bridge_push_event(&b,(ServerEvent){.token=i}));
    assert(!server_bridge_push_event(&b,(ServerEvent){0}));assert(b.dropped_events==1);
    int expected=0;size_t n;
    while((n=server_bridge_pop_batch(&b,out,128)))for(size_t k=0;k<n;k++)assert(out[k].token==expected++);
    assert(expected==SERVER_EVENT_CAP-1);
    server_bridge_set_waker(&b,wake,&wakes);
    server_bridge_publish(&b,&g);assert(wakes==0);
    g.players[0].x++;server_bridge_publish(&b,&g);assert(wakes==1);
    g.time_remaining_ms=9000;server_bridge_publish(&b,&g);assert(wakes==2);
    g.time_remaining_ms=8999;server_bridge_publish(&b,&g);assert(wakes==2);
    g.revision++;server_bridge_publish(&b,&g);assert(wakes==3);
    assert(server_bridge_push_reply(&b,(ServerJoinReply){0}));
    ServerAuthReply a={0};ServerChatReply c={0};
    assert(server_bridge_push_auth(&b,&a));assert(server_bridge_push_chat(&b,&c));assert(wakes==6);
    server_bridge_set_waker(&b,NULL,NULL);
    pthread_t thread;assert(!pthread_create(&thread,NULL,produce,NULL));
    expected=0;
    while(expected<100000){
        n=server_bridge_pop_batch(&b,out,128);
        for(size_t k=0;k<n;k++)assert(out[k].token==expected++);
        if(!n)sched_yield();
    }
    assert(!pthread_join(thread,NULL));
    server_bridge_set_waker(&b,wake,&wakes);
    server_bridge_set_stop(&b,1);assert(wakes==7);assert(server_bridge_should_stop(&b));
    server_bridge_destroy(&b);
    puts("PASS: FIFO, capacity, wraparound, batched transfer, 100000 concurrent events, publication/reply/stop notifications.");
}
