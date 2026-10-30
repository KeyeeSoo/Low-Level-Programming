#ifndef GAME_RULES_H
#define GAME_RULES_H

/* Preserve the original pixel-coordinate, upper-left-corner player squares. */
#define WIDTH 800
#define HEIGHT 600
#define SQ_WIDTH 50
#define MAX_PLAYERS 2
#define STEP 4

enum { PHASE_COLLECT=0, PHASE_TRACE=1, PHASE_STIR=2, PHASE_WIN=3, PHASE_LOSE=4 };
enum { ACTION_INTERACT=1, ACTION_DROP=2, ACTION_TRACE_OK=3,
       ACTION_TRACE_BAD=4, ACTION_STIR_OK=5 };

typedef struct {int b_active, x, y, token;} Player;
typedef struct {
    Player players[MAX_PLAYERS];
    int num_players, phase, stage, actor, mistakes, held[MAX_PLAYERS];
    int session, revision;
} GameRules;

extern const char *ingredient_names[4];
extern const int ingredient_x[4], ingredient_y[4];
extern const int cauldron_x, cauldron_y;
void game_init(GameRules *g);
int game_join(GameRules *g, int token);
int game_valid_player(const GameRules *g, int id, int token);
void game_move(GameRules *g, int id, int token, char direction);
void game_action(GameRules *g, int id, int token, int action, int session);

#endif
