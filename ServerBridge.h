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
#define SERVER_AUTH_REPLY_CAP 32
#define SERVER_CHAT_CAP 64

typedef enum {
    SERVER_JOIN = 1,
    SERVER_MOVE = 2,
    SERVER_ACTION = 3,
    SERVER_AUTHED = 4, /* From the auth thread: action holds the AuthResult. */
    SERVER_CHAT = 5
} ServerEventType;

typedef struct {
    ServerEventType type;
    int player_id;
    int token;
    int action;
    int session;
    char direction;
    int nonce;
    int guest; /* SERVER_AUTHED only: this login was a guest, not an account. */
    char name[USERNAME_MAX + 1];
    char text[CHAT_MAX + 1];
} ServerEvent;

typedef struct {
    int token;
    int player_id;
} ServerJoinReply;

typedef struct {
    int nonce, result, token;
    char name[USERNAME_MAX + 1];
} ServerAuthReply;

typedef struct {
    int sequence, player_id;
    char name[USERNAME_MAX + 1];
    char text[CHAT_MAX + 1];
} ServerChatReply;

typedef struct {
    pthread_mutex_t mutex;
    /* Notification only; callback must not call back into the bridge. */
    void (*wake_network)(void *);
    void *wake_context;
    ServerEvent events[SERVER_EVENT_CAP];
    unsigned event_read, event_write;
    ServerJoinReply replies[SERVER_REPLY_CAP];
    unsigned reply_read, reply_write;
    ServerAuthReply auth_replies[SERVER_AUTH_REPLY_CAP];
    unsigned auth_read, auth_write;
    ServerChatReply chat_replies[SERVER_CHAT_CAP];
    unsigned chat_read, chat_write;
    GameRules published_state;      /* A COPY, never authoritative state. */
    unsigned long dropped_events;
    int stop;
    int failed;
} ServerBridge;

int server_bridge_init(ServerBridge *bridge, const GameRules *initial);
void server_bridge_destroy(ServerBridge *bridge);
int server_bridge_push_event(ServerBridge *bridge, ServerEvent event);  /* DDS */
size_t server_bridge_pop_batch(ServerBridge *bridge, ServerEvent *out, size_t capacity);
void server_bridge_set_waker(ServerBridge *bridge, void (*wake)(void *), void *context);
int server_bridge_pop_event(ServerBridge *bridge, ServerEvent *out);   /* main */
int server_bridge_push_reply(ServerBridge *bridge, ServerJoinReply reply); /* main */
int server_bridge_pop_reply(ServerBridge *bridge, ServerJoinReply *out); /* DDS */
int server_bridge_push_auth(ServerBridge *bridge, const ServerAuthReply *reply); /* main */
int server_bridge_pop_auth(ServerBridge *bridge, ServerAuthReply *out); /* DDS */
int server_bridge_push_chat(ServerBridge *bridge, const ServerChatReply *reply); /* main */
int server_bridge_pop_chat(ServerBridge *bridge, ServerChatReply *out); /* DDS */
void server_bridge_publish(ServerBridge *bridge, const GameRules *authoritative); /* main */
void server_bridge_snapshot(ServerBridge *bridge, GameRules *out); /* DDS */
void server_bridge_set_stop(ServerBridge *bridge, int failed);
int server_bridge_should_stop(ServerBridge *bridge);

#endif
