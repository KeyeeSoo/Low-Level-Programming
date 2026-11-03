#ifndef DRAW_MAIN_H
#define DRAW_MAIN_H

void draw_sky(void); /* Composed by Art.c; used for missing-image fallback. */

/* Internal object helpers; clients should include Art.h. */
#include "Art.h"
#include "Art_Style.h"

extern const float menu_buttons[MENU_BUTTON_COUNT][4];
extern const float login_username_box[4];
extern const float login_password_box[4];
extern const float login_eye_box[4];
extern const float login_button_box[4];
extern const float login_guest_box[4];
extern const float login_signup_box[4];
void draw_player_sprite(int player);
void draw_gingerbread(void);
void draw_menu_background(unsigned int menu_texture);
void login_rect(const float box[4], float radius);
void draw_login_fallback(void);
float settings_option_x(int column);
void settings_option(int column, float y, const char *text, int selected, int hovered);
int inside_login_box(float x, float y, const float box[4]);
void login_field(const float box[4], float text_right, const char *text, int focused, float time_s);
void login_button(float x, float y, float w, float h, float px, const char *text,
                         unsigned fill, unsigned hover_fill, int hovered, int disabled);

#endif
