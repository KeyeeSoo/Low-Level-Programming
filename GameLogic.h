#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

/* The game rules are independent of Cyclone DDS and GLFW, so we can unit-test
   them without network or rendering libraries. World coordinates are pixels. */
enum {
    POTION_COLLECT = 0,
    POTION_TRACE = 1,
    POTION_STIR = 2,
    POTION_WIN = 3,
    POTION_LOSE = 4
};
enum {
    POTION_MOVE = 0,
    POTION_INTERACT = 1,
    POTION_TRACE_OK = 2,
    POTION_TRACE_BAD = 3,
    POTION_STIR_OK = 4,
    POTION_DROP = 5
};
enum { POTION_INGREDIENTS = 4, POTION_MAX_PLAYERS = 2 };

typedef struct {float x, y;} GamePoint;
extern const GamePoint potion_ingredients[POTION_INGREDIENTS];
extern const GamePoint potion_cauldron;
extern const char *potion_ingredient_names[POTION_INGREDIENTS];

typedef struct {
    int token[2];
    int joined[2];
    float x[2], y[2];
    int move_x[2], move_y[2];
    int held[2];
    int next_ingredient;
    int phase;
    int active_player;
    int rune_mistakes;
    int session;
    int revision;
} GameWorld;

void game_init(GameWorld *world);
int game_join(GameWorld *world, int token);
void game_tick(GameWorld *world, float delta_seconds);
void game_command(GameWorld *world, int player, int token,
                  int action, int dx, int dy, int session);
#endif
