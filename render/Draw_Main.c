#include "Draw_Main.h"
#include "Draw_Common.h"
#include <GLFW/glfw3.h>
#include <math.h>

#include "Draw_UI.h"

const float menu_buttons[MENU_BUTTON_COUNT][4] = {
    {490, 580, 283, 98}, {490, 697, 283, 90}, {490, 808, 283, 92}
};
const float login_username_box[4] = {230, 425, 503, 78};
const float login_password_box[4] = {230, 530, 503, 78};
const float login_eye_box[4] = {655, 545, 62, 48};
const float login_button_box[4] = {245, 633, 475, 82};
const float login_guest_box[4] = {303, 785, 360, 68};
const float login_signup_box[4] = {578, 882, 96, 38};

void draw_player_sprite(int player)
{
    /* Drawn with feet at y=0 and centred on x=0; the head is the near-square
     * chibi shape from the concept art. Legs are animated separately. */
    const unsigned dress = pick_color(player == 0, 0xC2404Bu, 0x6C55C9u);
    const unsigned dress_dark = pick_color(player == 0, 0x962E3Au, 0x4F3DA0u);
    const unsigned hair = pick_color(player == 0, 0x2D2230u, 0xD9702Cu);
    const unsigned hair_light = pick_color(player == 0, 0x4A3A4Du, 0xF0914Au);
    /* Back hair sits behind the dress. */
    if (player == 0) {
        const float curls[][3] = {{-15, -36, 8}, {-16, -27, 7.5f}, {-13, -19, 6.5f},
                                  {14, -37, 7}, {15, -28, 7}, {12, -20, 6}};
        render_color(INK);
        for (int i = 0; i < 6; i++)
            render_ellipse(curls[i][0], curls[i][1], curls[i][2] + 1.3f, curls[i][2] + 1.3f);
        render_color(hair);
        for (int i = 0; i < 6; i++)
            render_ellipse(curls[i][0], curls[i][1], curls[i][2], curls[i][2]);
    } else {
        outlined_ellipse(-16, -26, 8, 13, hair);
        outlined_ellipse(-14, -14, 5.5f, 7, hair);
        outlined_ellipse(15, -30, 7, 11, hair);
    }
    /* Dress: an A-line trapezoid with a darker hem and a collar. */
    render_color(INK);
    render_quad(-7.5f, -26, 7.5f, -26, 14.5f, -5.5f, -14.5f, -5.5f);
    render_color(dress);
    render_quad(-6.5f, -25, 6.5f, -25, 13, -7, -13, -7);
    render_color(dress_dark);
    render_quad(-12.2f, -10, 12.2f, -10, 13, -7, -13, -7);
    render_color(0xF6E4C6u);
    render_tri(-4.5f, -25, 4.5f, -25, 0, -20);
    /* Arms. */
    outlined_ellipse(-9.5f, -18, 3, 6, 0xF4CFAEu);
    outlined_ellipse(9.5f, -18, 3, 6, 0xF4CFAEu);
    /* Head. */
    outlined_ellipse(0, -39, 14.5f, 13.5f, 0xF8D9BCu);
    /* Fringe, swept to one side, over the top of the face. */
    render_color(INK);
    render_ellipse(-1, -47.5f, 15, 8);
    render_color(hair);
    render_ellipse(-1, -47.5f, 14, 7);
    render_ellipse(-9, -43, 6.5f, 5.5f);
    render_ellipse(7, -44.5f, 7.5f, 4.5f);
    render_color(hair_light);
    render_ellipse(-5, -50, 5, 1.8f);
    /* Face looks toward +x, so flipping the matrix turns the whole sprite. */
    render_color(INK);
    render_ellipse(1, -37.5f, 2.1f, 2.9f);
    render_ellipse(9, -37.5f, 2.1f, 2.9f);
    render_color(0xFFFFFFu);
    render_ellipse(1.6f, -38.6f, 0.8f, 0.9f);
    render_ellipse(9.6f, -38.6f, 0.8f, 0.9f);
    render_color_a(0xE58A8Au, 0.65f);
    render_ellipse(-3.5f, -33.5f, 2.8f, 1.7f);
    render_ellipse(11.8f, -33.5f, 2.2f, 1.6f);
    render_color(INK);
    render_line(3.5f, -32.5f, 6.5f, -32.5f, 1.2f);
    if (player == 1) {
        /* Purple bow, as in the concept art. */
        render_color(INK);
        render_tri(9, -52, 1.5f, -58, 1.5f, -46);
        render_tri(9, -52, 16.5f, -58, 16.5f, -46);
        render_color(0x8A5BD6u);
        render_tri(8.5f, -52, 3, -56.5f, 3, -47.5f);
        render_tri(9.5f, -52, 15, -56.5f, 15, -47.5f);
        outlined_ellipse(9, -52, 2.0f, 2.0f, 0xA981EBu);
    }
}

void draw_gingerbread(void)
{
    const unsigned dough = 0xA8693Du;
    const float limbs[][4] = {{-14, -22, 6, 3.8f}, {14, -22, 6, 3.8f},
                              {-7, -4, 4, 6.5f}, {7, -4, 4, 6.5f}};
    render_color(INK);
    for (int i = 0; i < 4; i++)
        render_ellipse(limbs[i][0], limbs[i][1], limbs[i][2] + 1.3f, limbs[i][3] + 1.3f);
    render_ellipse(0, -18, 10.5f, 12);
    render_ellipse(0, -35, 10.5f, 10.5f);
    render_color(dough);
    for (int i = 0; i < 4; i++)
        render_ellipse(limbs[i][0], limbs[i][1], limbs[i][2], limbs[i][3]);
    render_ellipse(0, -18, 9.2f, 10.8f);
    render_ellipse(0, -35, 9.2f, 9.2f);
    /* Icing. */
    render_color(0xFFF3DFu);
    render_ellipse(-3.2f, -36.5f, 1.4f, 1.4f);
    render_ellipse(3.2f, -36.5f, 1.4f, 1.4f);
    render_line(-3, -31.5f, 3, -31.5f, 1.2f);
    render_ellipse(0, -22, 1.3f, 1.3f);
    render_ellipse(0, -17, 1.3f, 1.3f);
    render_line(-15, -24, -13, -20, 1.0f);
    render_line(15, -24, 13, -20, 1.0f);
}

void draw_menu_background(unsigned int menu_texture)
{
    if (menu_texture == 0) {
        /* Painting missing (run from another folder?): the menu must still work. */
        draw_sky();
        return;
    }
    const float bottom = (MENU_CROP_TOP + (float)CANVAS_H / MENU_SCALE) / MENU_IMAGE_H;
    const float top = MENU_CROP_TOP / MENU_IMAGE_H;
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, menu_texture);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, top); glVertex2f(0, 0);
    glTexCoord2f(1, top); glVertex2f(CANVAS_W, 0);
    glTexCoord2f(1, bottom); glVertex2f(CANVAS_W, CANVAS_H);
    glTexCoord2f(0, bottom); glVertex2f(0, CANVAS_H);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
}

void login_rect(const float box[4], float radius)
{
    render_round_rect(LOGIN_X(box[0]), LOGIN_Y(box[1]), box[2] * LOGIN_SCALE, box[3] * LOGIN_SCALE, radius);
}

void draw_login_fallback(void)
{
    /* Painting missing: draw a plain form in the same places, so it still works. */
    static const float paper[4] = {180, 225, 600, 730};
    draw_sky();
    render_color(LOGIN_PAPER_COLOR);
    login_rect(paper, 10);
    render_color(LOGIN_PILL_COLOR);
    login_rect(login_username_box, 16);
    login_rect(login_password_box, 16);
    render_color(0x5C4A64u);
    login_rect(login_button_box, 16);
    render_color(LOGIN_PILL_COLOR);
    login_rect(login_guest_box, 14);
    render_color(INK);
    render_text_centered(LOGIN_X(480), LOGIN_Y(280), 34, "Welcome Back!");
    render_text(LOGIN_X(318), LOGIN_Y(447), 19, "Username");
    render_text(LOGIN_X(318), LOGIN_Y(552), 19, "Password");
    render_text_centered(LOGIN_X(480), LOGIN_Y(805), 19, "Play as Guest");
    render_text_centered(LOGIN_X(480), LOGIN_Y(888), 16, "Don't have an account? Sign Up");
    render_color(0xFFFFFFu);
    render_text_centered(LOGIN_X(480), LOGIN_Y(655), 22, "Login");
}

float settings_option_x(int column)
{
    return SETTINGS_ROW_X + (float)column * (SETTINGS_OPTION_W + SETTINGS_OPTION_GAP);
}

void settings_option(int column, float y, const char *text, int selected, int hovered)
{
    login_button(settings_option_x(column), y, SETTINGS_OPTION_W, SETTINGS_OPTION_H, 17, text,
                 pick_color(selected, 0x3F9B57u, 0x8C8474u), pick_color(selected, 0x4FB56Au, 0xA49C8Cu), hovered, 0);
}

int inside_login_box(float x, float y, const float box[4])
{
    return inside(x, y, LOGIN_X(box[0]), LOGIN_Y(box[1]), box[2] * LOGIN_SCALE, box[3] * LOGIN_SCALE);
}

void login_field(const float box[4], float text_right, const char *text, int focused, float time_s)
{
    const float px = 25.0f;
    const float x = LOGIN_X(box[0] + 84);
    const float y = LOGIN_Y(box[1]);
    const float h = box[3] * LOGIN_SCALE;
    const float w = LOGIN_X(text_right) - x;
    if (focused) {
        render_color_a(0xE9C86Bu, 0.22f);
        login_rect(box, 16);
    }
    if (!focused && text[0] == '\0')
        return; /* Leave the placeholder showing. */
    render_color(pick_color(focused, 0xEFD8BDu, LOGIN_PILL_COLOR));
    render_rect(x - 3, y + 8, w + 6, h - 16);
    /* Long text scrolls, so the end being typed stays visible. */
    const char *visible = text;
    while (visible[0] != '\0' && render_text_width(px, visible) > w - 6)
        visible++;
    render_color(INK);
    render_text(x, y + (h - px) * 0.5f - 4, px, visible);
    if (focused && fmodf(time_s, 1.0f) < 0.5f)
        render_rect(x + 1 + render_text_width(px, visible), y + 13, 1.8f, h - 26);
}

void login_button(float x, float y, float w, float h, float px, const char *text,
                         unsigned fill, unsigned hover_fill, int hovered, int disabled)
{
    render_color(INK);
    render_round_rect(x - 1.5f, y - 1.5f, w + 3, h + 3, 8);
    unsigned color = fill;
    if (hovered)
        color = hover_fill;
    if (disabled)
        color = 0xB7AE9Eu;
    render_color(color);
    render_round_rect(x, y, w, h, 7);
    render_color(0xFFFFFFu);
    render_text_centered(x + w * 0.5f, y + (h - px) * 0.5f - 1, px, text);
}
