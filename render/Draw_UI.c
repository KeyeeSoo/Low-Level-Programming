#include "Draw_UI.h"
#include "Draw_Common.h"
#include <GLFW/glfw3.h>
#include <math.h>



void small_star(float x, float y, float r)
{
    render_quad(x - r, y, x, y - r * 0.28f, x + r, y, x, y + r * 0.28f);
    render_quad(x, y - r, x + r * 0.28f, y, x, y + r, x - r * 0.28f, y);
}

void draw_parchment_banner(float x, float y, float w, float h)
{
    /* A polygon with a jittered edge reads as torn paper. */
    enum { TEARS = 26, SIDES = 6 };
    float px[2 * TEARS + 2 * SIDES][2];
    int n = 0;
    for (int i = 0; i < TEARS; i++) {
        float t = (float)i / (TEARS - 1);
        px[n][0] = x + w * t;
        px[n++][1] = y + (noise(i + 100) - 0.5f) * 5.0f;
    }
    for (int i = 1; i < SIDES; i++) {
        float t = (float)i / SIDES;
        px[n][0] = x + w + (noise(i + 200) - 0.5f) * 11.0f;
        px[n++][1] = y + h * t;
    }
    for (int i = TEARS - 1; i >= 0; i--) {
        float t = (float)i / (TEARS - 1);
        px[n][0] = x + w * t;
        px[n++][1] = y + h + (noise(i + 300) - 0.5f) * 5.0f;
    }
    for (int i = SIDES - 1; i >= 1; i--) {
        float t = (float)i / SIDES;
        px[n][0] = x + (noise(i + 400) - 0.5f) * 11.0f;
        px[n++][1] = y + h * t;
    }
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    for (int layer = 0; layer < 2; layer++) {
        const float grow = pick_float(layer == 0, 1.03f, 1.0f);
        render_color(pick_color(layer == 0, 0x8B6A42u, PARCHMENT));
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= n; i++) {
            float *p = px[i % n];
            glVertex2f(cx + (p[0] - cx) * grow, cy + (p[1] - cy) * pick_float(layer == 0, 1.08f, 1.0f));
        }
        glEnd();
    }
}

void tilted_text(float x, float y, float degrees, float px, const char *text)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glRotatef(degrees, 0, 0, 1);
    render_text(0, 0, px, text);
    glPopMatrix();
}

void card(float x, float y, float w, float h, unsigned fill)
{
    render_color_a(0x0A0814u, 0.45f);
    render_round_rect(x + 2, y + 3, w, h, 9);
    render_color(fill);
    render_round_rect(x, y, w, h, 9);
}

void card_title(float x, float y, const char *text)
{
    render_color(INK);
    render_text_bold(x, y, 25, text);
}

void draw_flask_icon(float x, float y)
{
    render_color(INK);
    render_ellipse(x, y + 8, 11, 11);
    render_rect(x - 3.5f, y - 10, 7, 12);
    render_rect(x - 5, y - 12, 10, 3.5f);
    render_color(0x6C7078u);
    render_ellipse(x - 4, y + 5, 2.4f, 2.4f);
}

void draw_gamepad_icon(float x, float y)
{
    render_color(INK);
    render_round_rect(x - 14, y - 7, 28, 14, 6);
    render_ellipse(x - 10, y + 5, 5, 5);
    render_ellipse(x + 10, y + 5, 5, 5);
    render_color(CARD_BLUE);
    render_rect(x - 10.5f, y - 3.5f, 6, 2);
    render_rect(x - 8.5f, y - 5.5f, 2, 6);
    render_ellipse(x + 6.5f, y - 1, 1.6f, 1.6f);
    render_ellipse(x + 10.5f, y - 3, 1.6f, 1.6f);
}

void draw_book_icon(float x, float y)
{
    render_color(INK);
    render_quad(x - 15, y - 9, x, y - 6, x, y + 10, x - 15, y + 8);
    render_quad(x + 15, y - 9, x, y - 6, x, y + 10, x + 15, y + 8);
    render_color(CARD_SAGE);
    render_quad(x - 13, y - 6.5f, x - 1, y - 4, x - 1, y + 7.5f, x - 13, y + 5.5f);
    render_quad(x + 13, y - 6.5f, x + 1, y - 4, x + 1, y + 7.5f, x + 13, y + 5.5f);
    render_color(INK);
    render_line(x - 11, y - 2, x - 3, y, 1);
    render_line(x - 11, y + 2, x - 3, y + 4, 1);
    render_line(x + 11, y - 2, x + 3, y, 1);
    render_line(x + 11, y + 2, x + 3, y + 4, 1);
}

void draw_skull_icon(float x, float y)
{
    render_color(INK);
    render_ellipse(x, y - 2, 11.5f, 10.5f);
    render_round_rect(x - 6.5f, y + 3, 13, 10, 2);
    render_color(CARD_PINK);
    render_ellipse(x - 4.5f, y - 2, 3, 3.4f);
    render_ellipse(x + 4.5f, y - 2, 3, 3.4f);
    render_tri(x, y + 1, x - 1.6f, y + 4.5f, x + 1.6f, y + 4.5f);
    render_rect(x - 3.2f, y + 7, 1.4f, 6);
    render_rect(x + 1.8f, y + 7, 1.4f, 6);
}

void draw_keycap(float x, float y, const char *letter)
{
    render_color(INK);
    render_round_rect(x, y, 21, 21, 4);
    render_color(0xF6F8FCu);
    render_round_rect(x + 1.6f, y + 1.6f, 17.8f, 17.8f, 3);
    render_color(INK);
    render_text_centered(x + 10.5f, y + 1.5f, 16.5f, letter);
}

void draw_mouse_icon(float x, float y, float scale, unsigned fill)
{
    const float w = 19 * scale, h = 27 * scale;
    outlined_box(x - w * 0.5f, y - h * 0.5f, w, h, w * 0.45f, fill);
    render_color(INK);
    render_rect(x - 0.7f, y - h * 0.5f, 1.4f, h * 0.38f);
    render_rect(x - w * 0.5f, y - h * 0.12f, w, 1.4f);
}

void draw_cursor(float x, float y)
{
    render_color(INK);
    render_tri(x - 1.2f, y - 2.5f, x - 1.2f, y + 15, x + 11.5f, y + 10);
    render_quad(x + 2, y + 9, x + 5, y + 8, x + 9, y + 17, x + 6, y + 18);
    render_color(0xFFFFFFu);
    render_tri(x, y, x, y + 12.5f, x + 9, y + 9.2f);
    render_quad(x + 3, y + 9, x + 4.5f, y + 8.5f, x + 8, y + 16, x + 6.5f, y + 16.5f);
}

void draw_witch_silhouette(float x, float y)
{
    render_color_a(0x8E9799u, 0.55f);
    render_tri(x - 34, y - 62, x + 26, y - 62, x - 2, y - 96); /* hat */
    render_ellipse(x - 4, y - 61, 32, 5);
    render_ellipse(x - 2, y - 48, 13, 14);
    render_tri(x + 10, y - 50, x + 21, y - 43, x + 9, y - 42); /* nose */
    render_quad(x - 13, y - 38, x + 9, y - 38, x + 26, y + 6, x - 30, y + 6);
    render_line(x + 6, y - 28, x + 28, y - 23, 5); /* arm */
}

void draw_exit_button(int hovered)
{
    render_color_a(0x08060Fu, 0.60f);
    render_round_rect(EXIT_BUTTON_X + 2, EXIT_BUTTON_Y + 3,
                      EXIT_BUTTON_W, EXIT_BUTTON_H, 7);
    render_color(INK);
    render_round_rect(EXIT_BUTTON_X - 1.5f, EXIT_BUTTON_Y - 1.5f,
                      EXIT_BUTTON_W + 3, EXIT_BUTTON_H + 3, 8);
    render_color(pick_color(hovered, 0xC0444Fu, 0x92333Fu));
    render_round_rect(EXIT_BUTTON_X, EXIT_BUTTON_Y,
                      EXIT_BUTTON_W, EXIT_BUTTON_H, 7);
    render_color(0xFFFFFFu);
    render_text_centered(EXIT_BUTTON_X + EXIT_BUTTON_W * 0.5f,
                         EXIT_BUTTON_Y + 4, 20, "Exit");
}

void draw_chat_card_base(void)
{
    card(CHAT_X, CHAT_Y, CHAT_W, CHAT_H, CARD_SAGE);
    /* A speech bubble. */
    render_color(INK);
    render_round_rect(CHAT_X + 9, CHAT_Y + 6, 25, 16, 5);
    render_tri(CHAT_X + 14, CHAT_Y + 20, CHAT_X + 22, CHAT_Y + 20, CHAT_X + 13, CHAT_Y + 27);
    render_color(INK);
    render_text_bold(CHAT_X + 40, CHAT_Y + 3, 19, "Chat");
    render_color(0x6E7472u);
    render_text(CHAT_X + 84, CHAT_Y + 8, 12.5f, "Enter to talk, Esc to stop");
}

void draw_fail_card(void)
{
    const float x = 638, y = 628, w = 149, h = 110;
    card(x, y, w, h, CARD_PINK);
    draw_skull_icon(x + 19, y + 17);
    render_color(INK);
    render_text_bold(x + 34, y + 6, 17.5f, "Fail Conditions");
    render_text(x + 10, y + 32, 13.5f, "Wrong ingredient =");
    render_text_bold(x + 10, y + 47, 14.5f, "GAME OVER");
    render_text(x + 10, y + 67, 12.5f, "3 bad runes/timeouts =");
    render_text_bold(x + 10, y + 82, 14.5f, "GAME OVER");
}

void draw_recipe_card_base(void)
{
    card(LEFT_COL_X, RECIPE_Y, LEFT_COL_W, RECIPE_H, CREAM);
    draw_flask_icon(LEFT_COL_X + 20, RECIPE_Y + 20);
    render_color(INK);
    render_text(LEFT_COL_X + 12, RECIPE_Y + 38, 14.5f, "Collect the correct");
    render_text(LEFT_COL_X + 12, RECIPE_Y + 54, 14.5f, "ingredients, in this order:");
}

void draw_minigame_card_base(void)
{
    card(LEFT_COL_X, MINI_Y, LEFT_COL_W, MINI_H, CARD_MINT);
    draw_flask_icon(LEFT_COL_X + 20, MINI_Y + 18);
    render_color(INK);
    render_text_bold(LEFT_COL_X + 36, MINI_Y + 5, 19, "Potion Minigame");
    draw_mouse_icon(LEFT_COL_X + 20, MINI_Y + 50, 0.72f, CARD_MINT);
    render_color(INK);
    render_text(LEFT_COL_X + 36, MINI_Y + 32, 14, "Trace the rune");
    render_text(LEFT_COL_X + 36, MINI_Y + 47, 14, "with your mouse");
    render_color(0x1E1B2Cu);
    render_round_rect(PREVIEW_CARD_X, RUNE_CARD_Y, PREVIEW_CARD_W, PREVIEW_CARD_H - 8, 8);
    draw_mouse_icon(LEFT_COL_X + 20, STIR_CARD_Y - 13, 0.75f, CARD_MINT);
    render_color(INK);
    render_text(LEFT_COL_X + 36, STIR_CARD_Y - 22, 15, "Stir the potion");
    render_color(0x1E1B2Cu);
    render_round_rect(PREVIEW_CARD_X, STIR_CARD_Y, PREVIEW_CARD_W, PREVIEW_CARD_H, 8);
}

void draw_stir_preview(void)
{
    /* The stir preview's pot is static; only its arrow and cursor move. */
    const float cx = PREVIEW_CARD_X + PREVIEW_CARD_W * 0.5f;
    const float cy = STIR_CARD_Y + 80;
    outlined_ellipse(cx, cy + 12, 50, 36, 0x33323Du);
    outlined_ellipse(cx, cy - 14, 47, 14, 0x484754u);
    render_color(0x3F8F36u);
    render_ellipse(cx, cy - 13, 40, 10);
    render_color(POTION);
    render_ellipse(cx - 6, cy - 14, 22, 5);
}

void draw_footer(void)
{
    render_color(0x8C86A8u);
    render_text_centered(493, 741, 14, "GOOD POTIONS   -   BRIGHTER TOMORROWS");
}

void draw_rune_shape(int rune, float cx, float cy, float size)
{
    render_color(LILAC);
    if (rune == 0) {
        for (int i = 0; i < 5; i++) {
            float a0 = -PI / 2 + 2 * PI * (float)i / 5;
            float a1 = -PI / 2 + 2 * PI * (float)(i + 2) / 5;
            render_line(cx + size * cosf(a0), cy + size * sinf(a0),
                        cx + size * cosf(a1), cy + size * sinf(a1), 2.0f);
        }
        return;
    }
    if (rune == 3) {
        const float w = size * 1.15f, h = size * 0.82f;
        render_line(cx - w, cy - h, cx + w, cy - h, 2.0f);
        render_line(cx + w, cy - h, cx + w, cy + h, 2.0f);
        render_line(cx + w, cy + h, cx - w, cy + h, 2.0f);
        render_line(cx - w, cy + h, cx - w, cy - h, 2.0f);
        return;
    }
    /* Circle or five-lobed flower: the same radii as trace_set_shape(). */
    float px = 0, py = 0;
    for (int i = 0; i <= 90; i++) {
        float a = -PI / 2 + 2 * PI * (float)i / 90;
        float r = size;
        if (rune != 1)
            r = size * (0.75f + 0.25f * cosf(5 * (a + PI / 2)));
        float x = cx + r * cosf(a), y = cy + r * sinf(a);
        if (i > 0)
            render_line(px, py, x, y, 2.0f);
        px = x;
        py = y;
    }
}

void rune_cursor_position(int rune, float t, float cx, float cy, float size, float *x, float *y)
{
    if (rune == 0) {
        float segment = t * 5.0f;
        int i = (int)segment % 5;
        float f = segment - floorf(segment);
        float a0 = -PI / 2 + 2 * PI * (float)(i * 2) / 5;
        float a1 = -PI / 2 + 2 * PI * (float)((i + 1) * 2) / 5;
        *x = cx + size * (cosf(a0) + (cosf(a1) - cosf(a0)) * f);
        *y = cy + size * (sinf(a0) + (sinf(a1) - sinf(a0)) * f);
    } else if (rune == 3) {
        const float w = size * 1.15f, h = size * 0.82f;
        const float corners[5][2] = {{-w, -h}, {w, -h}, {w, h}, {-w, h}, {-w, -h}};
        float segment = t * 4.0f;
        int i = (int)segment % 4;
        float f = segment - floorf(segment);
        *x = cx + corners[i][0] + (corners[i + 1][0] - corners[i][0]) * f;
        *y = cy + corners[i][1] + (corners[i + 1][1] - corners[i][1]) * f;
    } else {
        float a = -PI / 2 + 2 * PI * t;
        float r = size;
        if (rune != 1)
            r = size * (0.75f + 0.25f * cosf(5 * (a + PI / 2)));
        *x = cx + r * cosf(a);
        *y = cy + r * sinf(a);
    }
}
