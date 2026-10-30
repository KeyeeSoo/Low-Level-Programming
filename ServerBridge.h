#ifndef SERVER_BRIDGE_H
#define SERVER_BRIDGE_H

#include <pthread.h>
#include <stddef.h>
#include "GameRules.h"

/* Communication ONLY: the authoritative GameRules lives on server main thread.
 * This bridge carries requests from the DDS thread to main and copies the
 * main thread's state for publication by DDS. */
#define SERVER_EVENT_CAP 4096
#define SERVER_REPLY_CAP 128

typedef enum {
    SERVER_JOIN = 1,
    SERVER_MOVE = 2,
    SERVER_ACTION = 3
} ServerEventType;

typedef struct {
    ServerEventType type;
    int player_id;
    int token;
    int action;
    int session;
    char direction;
} ServerEvent;

typedef struct {
    int token;
    int player_id;
} ServerJoinReply;

typedef struct {
    pthread_mutex_t mutex;
    ServerEvent events[SERVER_EVENT_CAP];
    unsigned event_read, event_write;
    ServerJoinReply replies[SERVER_REPLY_CAP];
    unsigned reply_read, reply_write;
    GameRules published_state;      /* A COPY, never authoritative state. */
    unsigned long dropped_events;
    int stop;
    int failed;
} ServerBridge;

int server_bridge_init(ServerBridge *bridge, const GameRules *initial);
void server_bridge_destroy(ServerBridge *bridge);
int server_bridge_push_event(ServerBridge *bridge, ServerEvent event);  /* DDS */
int server_bridge_pop_event(ServerBridge *bridge, ServerEvent *out);   /* main */
int server_bridge_push_reply(ServerBridge *bridge, ServerJoinReply reply); /* main */
int server_bridge_pop_reply(ServerBridge *bridge, ServerJoinReply *out); /* DDS */
void server_bridge_publish(ServerBridge *bridge, const GameRules *authoritative); /* main */
void server_bridge_snapshot(ServerBridge *bridge, GameRules *out); /* DDS */
void server_bridge_set_stop(ServerBridge *bridge, int failed);
int server_bridge_should_stop(ServerBridge *bridge);

#endif
