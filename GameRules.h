#ifndef GAME_RULES_H
#define GAME_RULES_H

#include <stdint.h>

/* Original upper-left player-square coordinates and movement. */
#define WIDTH 800
#define HEIGHT 600
#define SQ_WIDTH 50
#define MAX_PLAYERS 2
#define STEP 7

#define INGREDIENT_COUNT 10
#define RECIPE_MIN_INGREDIENTS 2
#define RECIPE_MAX_INGREDIENTS 4
/* Number of complete variable-length recipes before the cooperative victory. */
#define RECIPES_TO_WIN 3

/* Server-authoritative per-phase time limits; change these for difficulty. */
#define COLLECT_TIME_MS 35000
#define TRACE_TIME_MS   25000
#define STIR_TIME_MS    25000

/* Login and chat limits, shared by client and server. They match messages.idl. */
#define USERNAME_MIN 3
#define USERNAME_MAX 16
#define PASSWORD_MIN 4
#define PASSWORD_MAX 32
#define CHAT_MAX 80
#define PLAYER_TIMEOUT_MS 5000
#define PLAYER_HEARTBEAT_MS 1000

enum { AUTH_MODE_LOGIN=1, AUTH_MODE_REGISTER=2, AUTH_MODE_GUEST=3 };
enum { AUTH_OK=0, AUTH_BAD_CREDENTIALS=1, AUTH_NAME_TAKEN=2, AUTH_INVALID_INPUT=3,
       AUTH_ALREADY_PLAYING=4, AUTH_GAME_FULL=5, AUTH_STORE_ERROR=6 };

enum { PHASE_COLLECT=0, PHASE_TRACE=1, PHASE_STIR=2, PHASE_WIN=3, PHASE_LOSE=4, PHASE_DISCONNECTED=5 };
enum { ACTION_INTERACT=1, ACTION_DROP=2, ACTION_TRACE_OK=3,
       ACTION_TRACE_BAD=4, ACTION_STIR_OK=5, ACTION_RESTART=6,
       ACTION_LEAVE=7, ACTION_HEARTBEAT=8 };

typedef struct {
    int b_active, x, y, token;
    uint64_t last_seen_ms; /* Monotonic heartbeat time, server-owned. */
    char name[USERNAME_MAX+1]; /* Verified account name, set by the server. */
} Player;
typedef struct {
    Player players[MAX_PLAYERS];
    int num_players, phase, stage, actor, mistakes, held[MAX_PLAYERS];
    int session, revision;
    /* Only the server's main thread modifies these. */
    int recipe[RECIPE_MAX_INGREDIENTS];  /* recipe[stage] = ingredient ID, unused slots -1 */
    int recipe_len;                /* 2, 3, or 4 ingredients */
    int used_mask;                 /* Ingredients already added this round */
    int round;                     /* Zero-based recipe index */
    int recipes_completed;
    uint32_t rng_state;
    /* Timing is owned by server main thread only; clients only display the snapshot. */
    uint64_t clock_ms;          /* Latest server monotonic time */
    uint64_t deadline_ms;       /* 0 while waiting or after win/loss */
    int time_limit_ms;          /* Duration of the current stage */
    int time_remaining_ms;      /* Published through GameState */
} GameRules;

extern const char *ingredient_names[INGREDIENT_COUNT];
extern const char *ingredient_short_names[INGREDIENT_COUNT];
/* 0=star, 1=circle, 2=flower, 3=rectangle */
extern const int ingredient_rune[INGREDIENT_COUNT];
extern const int ingredient_x[INGREDIENT_COUNT], ingredient_y[INGREDIENT_COUNT];
extern const int cauldron_x, cauldron_y;
void game_init(GameRules *g);
void game_init_seed(GameRules *g, uint32_t seed);
/* Call once every server main-loop iteration, BEFORE handling queued actions. */
void game_tick(GameRules *g, uint64_t now_ms);
void game_touch(GameRules *g, int id, int token);
int game_leave(GameRules *g, int token);
void game_expire_players(GameRules *g);
int game_join(GameRules *g, int token);
int game_valid_player(const GameRules *g, int id, int token);
void game_move(GameRules *g, int id, int token, char direction);
void game_action(GameRules *g, int id, int token, int action, int session);
/* Server-authoritative cooperative roles, both players participate every stage. */
int game_collector(const GameRules *g);
int game_tracer(const GameRules *g);
int game_stirrer(const GameRules *g);

#endif
