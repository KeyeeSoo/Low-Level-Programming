#ifndef ART_H
#define ART_H

#include "../GameRules.h"

/*
 * Client-facing art API. Art.c coordinates objects from the Draw_* modules.
 * Procedural sprites and menu/login images are cached using OpenGL display
 * lists and static-layer textures.
 * Call these only from the main (OpenGL) thread.
 */

/* The canvas holds the original 800x600 game coordinates untouched (the server
 * still works in those) and adds the story/controls panels below them. */
#define CANVAS_W 800
#define CANVAS_H 760

/* Requires a current OpenGL context and render_init(). */
void art_init(void);
void art_cleanup(void);

/* All static scenery and panels for one screen. Rendered once and then redrawn
 * as a single textured quad. */
typedef enum { ART_LAYER_GAME, ART_LAYER_MENU, ART_LAYER_LOGIN, ART_LAYER_SETTINGS,
               ART_LAYER_COUNT } ArtLayer;
void art_draw_static(ArtLayer layer);
/* Call when the viewport changes: the cached images must be re-rendered. */
void art_invalidate_static(void);
/* Candles, bubbles, twinkling stars, gingerbread men, minigame previews. */
void art_draw_ambient(float time_s, int preview_rune);

/* Ingredient sprite centred at (cx, cy); scale 1 is about 44 pixels across. */
void art_draw_ingredient(int item, float cx, float cy, float scale);

/* Dim the plate of a used-up ingredient and show its crate as empty. */
void art_draw_empty_crate(int item);

/* player is 0 or 1. (x, y) is the server's top-left of the 50x50 player box.
 * facing is -1 or +1; walk_phase is 0 when standing still. name may be empty. */
void art_draw_player(int player, float x, float y, int facing, float walk_phase,
                     int held_item, int is_local, const char *name);

/* Live contents of the recipe card; recipe holds `length` ingredient IDs. */
void art_draw_recipe(const int *recipe, int length, int stage, int round, int mistakes);

/* Prompt strip under the attic. time_fraction < 0 hides the countdown. */
void art_draw_status(const char *message, float time_fraction, int seconds_left);

/* The dark spell-page behind the rune and stir minigames. */
void art_draw_trace_panel(const char *title, const char *subtitle);

/* A big win or lose card over the attic. */
void art_draw_end_card(int won, int recipes_won, int play_again_hovered, float time_s);
int art_play_again_hit_test(float x, float y);

/* ---- Main menu ---- */
typedef enum { MENU_HIT_NONE, MENU_HIT_PLAY, MENU_HIT_SETTINGS, MENU_HIT_QUIT } MenuHit;

/* status is shown under the buttons, for example who is logged in. May be empty. */
void art_draw_menu(MenuHit hover, const char *status, int status_is_error, float time_s);
MenuHit art_menu_hit_test(float x, float y);

/* ---- Settings ---- */
#define WINDOW_SIZE_COUNT 3
typedef enum {
    SETTINGS_HIT_NONE, SETTINGS_HIT_SIZE_SMALL, SETTINGS_HIT_SIZE_NORMAL, SETTINGS_HIT_SIZE_LARGE,
    SETTINGS_HIT_ANIMATIONS_ON, SETTINGS_HIT_ANIMATIONS_OFF, SETTINGS_HIT_BACK
} SettingsHit;

void art_draw_settings(int window_size, int animations_on, SettingsHit hover);
SettingsHit art_settings_hit_test(float x, float y);

/* ---- Login page ---- */
typedef enum {
    LOGIN_HIT_NONE, LOGIN_HIT_USERNAME, LOGIN_HIT_PASSWORD, LOGIN_HIT_EYE,
    LOGIN_HIT_LOGIN, LOGIN_HIT_GUEST, LOGIN_HIT_SIGNUP
} LoginHit;

typedef struct {
    const char *username;
    const char *password; /* Drawn as stars unless show_password is set. */
    int show_password;
    LoginHit focus; /* LOGIN_HIT_USERNAME or LOGIN_HIT_PASSWORD. */
    LoginHit hover;
    const char *message; /* Feedback line; may be empty. */
    int message_is_error;
    int busy; /* A request is in flight: buttons are dimmed. */
} LoginView;

void art_draw_login(const LoginView *view, float time_s);
LoginHit art_login_hit_test(float x, float y);

/* ---- Chat card ---- */
#define CHAT_VISIBLE_LINES 3
#define CHAT_LINE_PX 13.0f
#define CHAT_LINE_WIDTH 217.0f

/* lines[i] is at most one card-width of text; player is who sent it (0 or 1). */
void art_draw_chat(const char (*lines)[CHAT_MAX + USERNAME_MAX + 4], const int *line_players,
                   int line_count, const char *input, int focused, float time_s);
int art_chat_hit_test(float x, float y);

/* Gameplay Exit control. Hit-test coordinates are logical canvas coordinates. */
void art_draw_exit_button(int hovered);
int art_exit_hit_test(float x, float y);

/* Card over a paused game. The client picks the wording: a saved seat waiting
 * for its player, a guest who can't come back, or this player's own disconnect. */
void art_draw_disconnect_card(const char *title, const char *line1, const char *line2,
                              int leaving, int hovered);
int art_disconnect_hit_test(float x, float y);

/* Small badge at the top-left of the main menu: who is logged in, with a
 * Log out button for accounts or a Log in button for guests. */
void art_draw_account_badge(const char *name, int guest, int hovered);
/* Tests the badge's button as it was last drawn. */
int art_account_hit_test(float x, float y);

#endif /* ART_H */
