#ifndef DRAW_UI_H
#define DRAW_UI_H

/* Internal object helpers; clients should include Art.h. */
#include "Art.h"
#include "Art_Style.h"


void small_star(float x, float y, float r);
void draw_parchment_banner(float x, float y, float w, float h);
void tilted_text(float x, float y, float degrees, float px, const char *text);
void card(float x, float y, float w, float h, unsigned fill);
void card_title(float x, float y, const char *text);
void draw_flask_icon(float x, float y);
void draw_gamepad_icon(float x, float y);
void draw_book_icon(float x, float y);
void draw_skull_icon(float x, float y);
void draw_keycap(float x, float y, const char *letter);
void draw_mouse_icon(float x, float y, float scale, unsigned fill);
void draw_cursor(float x, float y);
void draw_witch_silhouette(float x, float y);
void draw_exit_button(int hovered);
void draw_chat_card_base(void);
void draw_fail_card(void);
void draw_recipe_card_base(void);
void draw_minigame_card_base(void);
void draw_stir_preview(void);
void draw_footer(void);
void draw_rune_shape(int rune, float cx, float cy, float size);
void rune_cursor_position(int rune, float t, float cx, float cy, float size, float *x, float *y);

#endif
