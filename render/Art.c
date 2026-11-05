/* High-level art composition and private OpenGL cache ownership. */
#include "Art.h"
#include "Render.h"
#include "Draw_Common.h"
#include "Draw_Background.h"
#include "Draw_Ingredients.h"
#include "Draw_Main.h"
#include "Draw_UI.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Display lists built once by art_init(). */
static GLuint list_static[ART_LAYER_COUNT];
static GLuint list_icons = 0; /* INGREDIENT_COUNT consecutive lists. */
static GLuint list_gingerbread = 0;
static GLuint list_players = 0; /* MAX_PLAYERS consecutive lists. */
static GLuint list_stir_card = 0;

/* The static layer, captured from the framebuffer after its first draw. */
static GLuint static_texture[ART_LAYER_COUNT];
static int static_texture_valid[ART_LAYER_COUNT];


static GLuint menu_texture = 0;
static GLuint login_texture = 0;

static void draw_menu_layer(void);
static void build_static_lists(void);
static void draw_attic(void);
static void draw_controls_card(void);
static void draw_story_card(float x, float y);
static void draw_settings_background(void);
static void draw_dimmed_menu(void);
static void draw_login_layer(void);

void art_draw_player(int player, float x, float y, int facing, float walk_phase,
                     int held_item, int is_local, const char *name)
{
    const unsigned tag_color = pick_color(player == 0, 0xD0434Bu, 0x4C74D6u);
    const float feet_x = x + SQ_WIDTH * 0.5f;
    const float feet_y = y + SQ_WIDTH - 3.0f;
    float stride = 0.0f;
    float bob = 0.0f;
    if (walk_phase != 0.0f) {
        stride = sinf(walk_phase);
        bob = -fabsf(stride) * 2.2f;
    }
    render_color_a(0x120C10u, 0.38f);
    render_ellipse(feet_x, feet_y, 17, 5.5f);
    glPushMatrix();
    glTranslatef(feet_x, feet_y + bob, 0);
    glScalef((float)facing, 1, 1);
    /* Legs swing opposite each other. */
    outlined_box(-7 + stride * 2.5f, -8, 5, 8.5f + stride * 0.8f, 2, 0x3A2A28u);
    outlined_box(2 - stride * 2.5f, -8, 5, 8.5f - stride * 0.8f, 2, 0x3A2A28u);
    glCallList(list_players + (GLuint)player);
    glPopMatrix();
    /* The tag, label and held ingredient must stay readable, so they are not flipped. */
    const float top = feet_y + bob - 72.0f;
    char tag[4] = {'P', (char)('1' + player), '\0', '\0'};
    const char *label = tag;
    if (name != NULL && name[0] != '\0')
        label = name;
    const float half_width = render_text_width(12, label) * 0.5f + 5.0f;
    render_color(INK);
    render_round_rect(feet_x - half_width - 1.5f, top - 1.5f, half_width * 2 + 3, 15, 4);
    render_color(tag_color);
    render_round_rect(feet_x - half_width, top, half_width * 2, 12, 3);
    render_color(0xFFFFFFu);
    render_text_centered(feet_x, top - 1.5f, 12, label);
    if (is_local) {
        render_color_a(0x120C10u, 0.55f);
        render_round_rect(feet_x - 14, feet_y + 5, 28, 12, 4);
        render_color(0xFBEBCFu);
        render_text_centered(feet_x, feet_y + 4.5f, 11, "YOU");
    }
    if (held_item >= 0) {
        float carry_x = feet_x + (float)facing * 24.0f;
        float carry_y = feet_y + bob - 38.0f;
        render_color_a(0xFBEBCFu, 0.9f);
        render_ellipse(carry_x, carry_y, 14, 14);
        art_draw_ingredient(held_item, carry_x, carry_y, 0.56f);
    }
}

static void draw_menu_layer(void)
{
    static const char *const fallback_labels[MENU_BUTTON_COUNT] = {"Play", "Settings", "Quit"};
    draw_menu_background(menu_texture);
    if (menu_texture != 0)
        return;
    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        const float *b = menu_buttons[i];
        outlined_box(MENU_X(b[0]), MENU_Y(b[1]), b[2] * MENU_SCALE, b[3] * MENU_SCALE, 6, PARCHMENT);
        render_color(INK);
        render_text_centered(MENU_X(b[0] + b[2] * 0.5f), MENU_Y(b[1]) + 22, 30, fallback_labels[i]);
    }
}

static void draw_dimmed_menu(void)
{
    draw_menu_background(menu_texture);
    render_color_a(0x0C0816u, 0.5f);
    render_rect(0, 0, CANVAS_W, CANVAS_H);
}

static void draw_login_layer(void)
{
    if (login_texture == 0) {
        draw_login_fallback();
        return;
    }
    const float top = LOGIN_CROP_TOP / LOGIN_IMAGE_H;
    const float bottom = (LOGIN_CROP_TOP + (float)CANVAS_H / LOGIN_SCALE) / LOGIN_IMAGE_H;
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, login_texture);
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

void art_init(void)
{
    menu_texture = render_load_image(MENU_IMAGE_FILE);
    login_texture = render_load_image(LOGIN_IMAGE_FILE);
    /* Sprites first: the static list calls some of these lists. */
    list_icons = glGenLists(INGREDIENT_COUNT);
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        glNewList(list_icons + (GLuint)i, GL_COMPILE);
        draw_ingredient_sprite(i);
        glEndList();
    }
    list_players = glGenLists(MAX_PLAYERS);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        glNewList(list_players + (GLuint)i, GL_COMPILE);
        draw_player_sprite(i);
        glEndList();
    }
    list_gingerbread = glGenLists(1);
    glNewList(list_gingerbread, GL_COMPILE);
    draw_gingerbread();
    glEndList();
    list_stir_card = glGenLists(1);
    glNewList(list_stir_card, GL_COMPILE);
    draw_stir_preview();
    glEndList();
    build_static_lists();
}

void art_cleanup(void)
{
    for (int i = 0; i < ART_LAYER_COUNT; i++) {
        glDeleteLists(list_static[i], 1);
        if (static_texture[i] != 0)
            glDeleteTextures(1, &static_texture[i]);
        static_texture[i] = 0;
        static_texture_valid[i] = 0;
    }
    glDeleteLists(list_icons, INGREDIENT_COUNT);
    glDeleteLists(list_players, MAX_PLAYERS);
    glDeleteLists(list_gingerbread, 1);
    glDeleteLists(list_stir_card, 1);
}

void art_invalidate_static(void)
{
    for (int i = 0; i < ART_LAYER_COUNT; i++)
        static_texture_valid[i] = 0;
}

void art_draw_static(ArtLayer layer)
{
    /* The scenery is thousands of blended vertices that never change. Rendering it
     * once and blitting one quad costs far less per frame, especially on the
     * software GL driver a WSL install often falls back to. */
    if (!static_texture_valid[layer]) {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        glCallList(list_static[layer]);
        if (static_texture[layer] == 0)
            glGenTextures(1, &static_texture[layer]);
        glBindTexture(GL_TEXTURE_2D, static_texture[layer]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, viewport[0], viewport[1],
                         viewport[2], viewport[3], 0);
        static_texture_valid[layer] = 1;
        return;
    }
    /* GL textures start at the bottom row; the canvas starts at the top. */
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, static_texture[layer]);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(0, 0);
    glTexCoord2f(1, 1); glVertex2f(CANVAS_W, 0);
    glTexCoord2f(1, 0); glVertex2f(CANVAS_W, CANVAS_H);
    glTexCoord2f(0, 0); glVertex2f(0, CANVAS_H);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
}

void art_draw_empty_crate(int item)
{
    /* Grey the label so a used ingredient reads as "gone", not "missing". */
    const float cx = (float)ingredient_x[item], cy = (float)ingredient_y[item];
    float w = render_text_width(12.5f, ingredient_names[item]) + 12.5f;
    render_color_a(0x1A1216u, 0.55f);
    render_round_rect(cx - w * 0.5f, cy + 25, w, 14.5f, 3);
}

void art_draw_ambient(float time_s, int preview_rune)
{
    /* Twinkling stars over the static sky. */
    const float twinkle[][2] = {{30, 80}, {110, 16}, {625, 80}, {770, 88}, {660, 12}};
    for (int i = 0; i < 5; i++) {
        render_color_a(0xFFF3C4u, 0.25f + 0.55f * fabsf(sinf(time_s * 0.9f + (float)i * 1.7f)));
        small_star(twinkle[i][0], twinkle[i][1], 2.6f);
    }
    /* Candle flames and their soft glow. */
    candle_flame((float)cauldron_x - 45, ATTIC_Y + 22, time_s, 0);
    candle_flame((float)cauldron_x + 45, ATTIC_Y + 22, time_s, 1);
    candle_flame(ATTIC_X + 24.5f, 318, time_s, 2);
    /* Cauldron: rising bubbles, staggered and fading out as they climb. */
    const float cx = (float)cauldron_x, cy = (float)cauldron_y;
    for (int i = 0; i < 7; i++) {
        float life = fmodf(time_s * (0.32f + noise(i) * 0.2f) + noise(i + 9), 1.0f);
        float bx = cx + (noise(i + 20) - 0.5f) * 44.0f + sinf(time_s * 2.0f + (float)i) * 3.0f;
        float by = cy - 22.0f - life * 46.0f;
        float size = 2.0f + noise(i + 30) * 3.5f;
        render_color_a(0x2C6A2Au, (1.0f - life) * 0.9f);
        render_ellipse(bx, by, size + 1, size + 1);
        render_color_a(0xA4EA6Fu, (1.0f - life));
        render_ellipse(bx, by, size, size);
    }
    /* The kidnapped children, bouncing hopefully in the corner. */
    const float gx[2] = {ATTIC_X + ATTIC_W - 58, ATTIC_X + ATTIC_W - 24};
    for (int i = 0; i < 2; i++) {
        float hop = fabsf(sinf(time_s * 1.6f + (float)i * 1.2f)) * 3.5f;
        glPushMatrix();
        glTranslatef(gx[i], 496 - hop, 0);
        glCallList(list_gingerbread);
        glPopMatrix();
    }
    render_color(0xE9E2F2u);
    render_text_centered(ATTIC_X + ATTIC_W - 41, 500, 12, "Kidnapped");
    render_text_centered(ATTIC_X + ATTIC_W - 41, 513, 12, "children...?");
    /* Rune preview: shows the rune that the current ingredient really needs. */
    const float rx = PREVIEW_CARD_X + PREVIEW_CARD_W * 0.5f;
    const float ry = RUNE_CARD_Y + (PREVIEW_CARD_H - 8) * 0.5f;
    draw_rune_shape(preview_rune, rx, ry + 3, 50);
    float mx, my;
    rune_cursor_position(preview_rune, fmodf(time_s * 0.14f, 1.0f), rx, ry + 3, 50, &mx, &my);
    draw_cursor(mx, my);
    /* Stir preview: pot from a display list, then a spoon-arrow going around. */
    glCallList(list_stir_card);
    const float sx = rx, sy = STIR_CARD_Y + 67;
    const float swirl = time_s * 2.4f;
    render_color(0xBDE7FFu);
    for (int i = 0; i < 12; i++) {
        float a0 = swirl - (float)i * 0.28f, a1 = a0 - 0.28f;
        render_color_a(0xBDE7FFu, 1.0f - (float)i / 12.0f);
        render_line(sx + 33 * cosf(a0), sy + 9 * sinf(a0), sx + 33 * cosf(a1), sy + 9 * sinf(a1), 2.6f);
    }
    draw_cursor(sx + 33 * cosf(swirl), sy + 9 * sinf(swirl));
}

void art_draw_recipe(const int *recipe, int length, int stage, int round, int mistakes)
{
    static const unsigned step_colors[RECIPE_MAX_INGREDIENTS] = {
        0x3F9B57u, 0xD98027u, 0x7E4FC0u, 0x3E8FD8u
    };
    char title[32];
    snprintf(title, sizeof(title), "Recipe %d/%d", round + 1, RECIPES_TO_WIN);
    card_title(LEFT_COL_X + 36, RECIPE_Y + 6, title);
    for (int i = 0; i < length; i++) {
        const float y = RECIPE_Y + 92 + (float)i * 28;
        const float cx = LEFT_COL_X + 24;
        char number[12];
        snprintf(number, sizeof(number), "%d", i + 1);
        if (i == stage) {
            render_color_a(0xE9C86Bu, 0.55f);
            render_round_rect(LEFT_COL_X + 6, y - 13, LEFT_COL_W - 12, 26, 6);
        }
        render_color(INK);
        render_ellipse(cx, y, 11, 11);
        render_color(pick_color(i < stage, 0xA9A192u, step_colors[i]));
        render_ellipse(cx, y, 9.5f, 9.5f);
        render_color(0xFFFFFFu);
        render_text_centered(cx, y - 9, 14.5f, number);
        render_color(pick_color(i < stage, 0x8C8474u, INK));
        render_text(cx + 15, y - 10, 15.5f, ingredient_names[recipe[i]]);
        art_draw_ingredient(recipe[i], LEFT_COL_X + LEFT_COL_W - 20, y, 0.42f);
        if (i < stage) {
            render_color(0x8C8474u);
            render_line(cx + 15, y + 1,
                        cx + 18 + render_text_width(15.5f, ingredient_names[recipe[i]]), y + 1, 1.3f);
        }
    }
    /* Three hearts: mistakes are forgiven at each new recipe. */
    render_color(INK);
    render_text(LEFT_COL_X + 12, RECIPE_Y + RECIPE_H - 26, 14.5f, "Mistakes left:");
    for (int i = 0; i < 3; i++) {
        const float hx = LEFT_COL_X + 122 + (float)i * 19, hy = RECIPE_Y + RECIPE_H - 17;
        const int alive = i < 3 - mistakes;
        render_color(pick_color(alive, 0xD0434Bu, 0xC8BDA8u));
        render_ellipse(hx - 3.5f, hy - 2, 4, 4);
        render_ellipse(hx + 3.5f, hy - 2, 4, 4);
        render_tri(hx - 7.5f, hy - 0.5f, hx + 7.5f, hy - 0.5f, hx, hy + 8);
    }
}

void art_draw_status(const char *message, float time_fraction, int seconds_left)
{
    const float x = 199, y = 590, w = 588, h = 33;
    const int counting = time_fraction >= 0.0f;
    const int low = counting && seconds_left <= 10;
    const float text_w = w - pick_float(counting, 112.0f, 16.0f);
    float px = 18.0f;
    /* Long prompts shrink a little rather than run under the timer. */
    const float needed = render_text_width(px, message);
    if (needed > text_w)
        px *= text_w / needed;
    render_color(0x8B6A42u);
    render_round_rect(x - 1.5f, y - 1.5f, w + 3, h + 3, 8);
    render_color(PARCHMENT);
    render_round_rect(x, y, w, h, 7);
    render_color(INK);
    render_text(x + 10, y + (h - px) * 0.5f - 1, px, message);
    if (counting) {
        char text[16];
        snprintf(text, sizeof(text), "%d s", seconds_left);
        const float tx = x + w - 104;
        render_color(pick_color(low, 0x8E2130u, 0x2D2A47u));
        render_round_rect(tx, y + 4, 99, h - 8, 5);
        render_color(pick_color(low, 0xFFD3C2u, 0xBFEFC9u));
        render_text_centered(tx + 49.5f, y + 2.5f, 17.5f, text);
        render_color_a(0x000000u, 0.35f);
        render_rect(tx + 5, y + h - 8, 89, 2.5f);
        render_color(pick_color(low, 0xFF6A5Au, 0x79D44Fu));
        render_rect(tx + 5, y + h - 8, 89 * time_fraction, 2.5f);
    }
}

void art_draw_trace_panel(const char *title, const char *subtitle)
{
    const float x = 212, y = 143, w = 561, h = 398;
    render_color_a(0x0A0814u, 0.6f);
    render_round_rect(x - 13, y - 39, w + 26, h + 80 - 3, 4);
    render_color(0xC9A65Au);
    render_round_rect(x - 2, y - 2, w + 4, h + 4, 12);
    render_color(0x211C35u);
    render_round_rect(x, y, w, h, 10);
    render_color(PARCHMENT);
    render_text_centered(x + w * 0.5f, y + 6, 27, title);
    if (subtitle != NULL) {
        render_color(0xC9C2DEu);
        render_text_centered(x + w * 0.5f, y + 37, 17, subtitle);
    }
}

int art_play_again_hit_test(float x, float y)
{
    return x >= PLAY_AGAIN_X && x < PLAY_AGAIN_X + PLAY_AGAIN_W &&
           y >= PLAY_AGAIN_Y && y < PLAY_AGAIN_Y + PLAY_AGAIN_H;
}

int art_quit_hit_test(float x, float y)
{
    return x >= QUIT_X && x < QUIT_X + PLAY_AGAIN_W &&
           y >= PLAY_AGAIN_Y && y < PLAY_AGAIN_Y + PLAY_AGAIN_H;
}

void art_draw_end_card(int won, int recipes_won, int play_again_hovered, int quit_hovered,
                       float time_s)
{
    const float cx = (float)ATTIC_X + ATTIC_W * 0.5f, cy = ATTIC_Y + ATTIC_H * 0.5f;
    render_color_a(0x08060Fu, 0.68f);
    render_rect(ATTIC_X, ATTIC_Y, ATTIC_W, ATTIC_H);
    render_color(0x8B6A42u);
    render_round_rect(cx - 208, cy - 88, 416, 176, 13);
    render_color(pick_color(won, PARCHMENT, CARD_PINK));
    render_round_rect(cx - 205, cy - 85, 410, 170, 11);
    render_color(INK);
    const char *title = "GAME OVER";
    if (won)
        title = "The Curse is Broken!";
    render_text_bold(cx - render_text_width(44, title) * 0.5f, cy - 78, 44, title);
    char line[80];
    if (won)
        snprintf(line, sizeof(line), "All %d potions brewed - the children are saved!", recipes_won);
    else
        snprintf(line, sizeof(line), "The witch wins this time. %d of %d potions brewed.",
                 recipes_won, RECIPES_TO_WIN);
    render_text_centered(cx, cy - 22, 16.5f, line);
    /* One gingerbread man either side of the buttons. */
    for (int i = 0; i < 2; i++) {
        float hop = 0.0f;
        if (won)
            hop = fabsf(sinf(time_s * 3 + (float)i)) * 8.0f;
        glPushMatrix();
        glTranslatef(pick_float(i == 0, cx - 182, cx + 182), cy + 72 - hop, 0);
        glCallList(list_gingerbread);
        glPopMatrix();
    }
    login_button(PLAY_AGAIN_X, PLAY_AGAIN_Y, PLAY_AGAIN_W, PLAY_AGAIN_H, 20, "Play Again",
                 0x3F9B57u, 0x4FB56Au, play_again_hovered, 0);
    login_button(QUIT_X, PLAY_AGAIN_Y, PLAY_AGAIN_W, PLAY_AGAIN_H, 20, "Quit",
                 0xA8423Bu, 0xC2544Bu, quit_hovered, 0);
}

void art_draw_login(const LoginView *view, float time_s)
{
    char stars[PASSWORD_MAX + 1];
    size_t length = strlen(view->password);
    if (length > PASSWORD_MAX)
        length = PASSWORD_MAX;
    memset(stars, '*', length);
    stars[length] = '\0';
    art_draw_static(ART_LAYER_LOGIN);
    login_field(login_username_box, 715, view->username, view->focus == LOGIN_HIT_USERNAME, time_s);
    const char *password_text = stars;
    if (view->show_password)
        password_text = view->password;
    login_field(login_password_box, 650, password_text, view->focus == LOGIN_HIT_PASSWORD, time_s);
    if (view->show_password) {
        /* Replace the painted crossed-out eye with an open one. */
        const float cx = LOGIN_X(685), cy = LOGIN_Y(569);
        render_color(pick_color(view->focus == LOGIN_HIT_PASSWORD, 0xEFD8BDu, LOGIN_PILL_COLOR));
        render_rect(cx - 21, cy - 16, 42, 32);
        render_color(0x5F4F59u);
        render_ellipse(cx, cy, 16, 9);
        render_color(LOGIN_PILL_COLOR);
        render_ellipse(cx, cy, 13.5f, 7);
        render_color(0x5F4F59u);
        render_ellipse(cx, cy, 5, 5);
    }
    /* Hover and busy feedback, painted over the artwork's buttons. */
    if (view->hover == LOGIN_HIT_LOGIN && !view->busy) {
        render_color_a(0xFFFFFFu, 0.14f);
        login_rect(login_button_box, 28);
    }
    if (view->hover == LOGIN_HIT_GUEST && !view->busy) {
        render_color_a(0xE9C86Bu, 0.3f);
        login_rect(login_guest_box, 26);
    }
    if (view->hover == LOGIN_HIT_SIGNUP && !view->busy) {
        render_color_a(0xE9C86Bu, 0.35f);
        login_rect(login_signup_box, 5);
    }
    if (view->hover == LOGIN_HIT_EYE) {
        render_color_a(0xE9C86Bu, 0.3f);
        login_rect(login_eye_box, 10);
    }
    if (view->busy) {
        render_color_a(LOGIN_PAPER_COLOR, 0.55f);
        login_rect(login_button_box, 28);
        login_rect(login_guest_box, 26);
        login_rect(login_signup_box, 5);
    }
    /* Messages take the place of the "or" divider. */
    if (view->message[0] != '\0') {
        const float max_width = LOGIN_X(690) - LOGIN_X(275);
        float px = 20.0f;
        const float needed = render_text_width(px, view->message);
        if (needed > max_width)
            px *= max_width / needed;
        render_color(LOGIN_PAPER_COLOR);
        render_rect(LOGIN_X(280), LOGIN_Y(733), LOGIN_X(690) - LOGIN_X(280), 35 * LOGIN_SCALE);
        render_color(pick_color(view->message_is_error, 0xB3262Fu, 0x3B6E46u));
        render_text_centered(LOGIN_X(480), LOGIN_Y(753) - px * 0.62f, px, view->message);
    }
}

MenuHit art_menu_hit_test(float x, float y)
{
    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        const float *b = menu_buttons[i];
        if (inside(x, y, MENU_X(b[0]), MENU_Y(b[1]), b[2] * MENU_SCALE, b[3] * MENU_SCALE))
            return (MenuHit)(MENU_HIT_PLAY + i);
    }
    return MENU_HIT_NONE;
}

void art_draw_menu(MenuHit hover, const char *status, int status_is_error, float time_s)
{
    art_draw_static(ART_LAYER_MENU);
    /* Lamplight on the cottage, flickering gently over the painting. */
    const float flicker = 0.8f + 0.2f * sinf(time_s * 7.0f) * sinf(time_s * 3.1f);
    render_glow(MENU_X(368), MENU_Y(630), 62, 62, 0xFFC964u, 0.42f * flicker);
    render_glow(MENU_X(217), MENU_Y(590), 80, 72, 0xFFC964u, 0.2f * flicker);
    /* A few stars twinkle in the painted sky. */
    const float stars[][2] = {{705, 124}, {443, 413}, {720, 417}, {920, 326}, {870, 150}};
    for (int i = 0; i < 5; i++) {
        render_color_a(0xFFF3C4u, 0.2f + 0.6f * fabsf(sinf(time_s * 0.9f + (float)i * 1.7f)));
        small_star(MENU_X(stars[i][0]), MENU_Y(stars[i][1]), 5);
    }
    if (hover != MENU_HIT_NONE) {
        const float *b = menu_buttons[hover - MENU_HIT_PLAY];
        render_color_a(0xFFE9A8u, 0.28f);
        render_round_rect(MENU_X(b[0]), MENU_Y(b[1]), b[2] * MENU_SCALE, b[3] * MENU_SCALE, 7);
    }
    if (status[0] != '\0') {
        const float cx = MENU_X(631), y = MENU_Y(912);
        const float half_width = render_text_width(16, status) * 0.5f + 12;
        render_color_a(pick_color(status_is_error, 0x6E1420u, 0x1E1B2Cu), 0.88f);
        render_round_rect(cx - half_width, y, half_width * 2, 25, 7);
        render_color(pick_color(status_is_error, 0xFFD3C2u, 0xFBEBCFu));
        render_text_centered(cx, y + 3, 16, status);
    }
}

SettingsHit art_settings_hit_test(float x, float y)
{
    for (int i = 0; i < WINDOW_SIZE_COUNT; i++)
        if (inside(x, y, settings_option_x(i), SETTINGS_SIZE_Y, SETTINGS_OPTION_W, SETTINGS_OPTION_H))
            return (SettingsHit)(SETTINGS_HIT_SIZE_SMALL + i);
    for (int i = 0; i < 2; i++)
        if (inside(x, y, settings_option_x(i), SETTINGS_ANIM_Y, SETTINGS_OPTION_W, SETTINGS_OPTION_H))
            return (SettingsHit)(SETTINGS_HIT_ANIMATIONS_ON + i);
    if (inside(x, y, SETTINGS_CARD_X + (SETTINGS_CARD_W - SETTINGS_BACK_W) * 0.5f, SETTINGS_BACK_Y,
               SETTINGS_BACK_W, SETTINGS_OPTION_H))
        return SETTINGS_HIT_BACK;
    return SETTINGS_HIT_NONE;
}

void art_draw_settings(int window_size, int animations_on, SettingsHit hover)
{
    static const char *const sizes[WINDOW_SIZE_COUNT] = {"Small", "Normal", "Large"};
    art_draw_static(ART_LAYER_SETTINGS);
    for (int i = 0; i < WINDOW_SIZE_COUNT; i++)
        settings_option(i, SETTINGS_SIZE_Y, sizes[i], window_size == i,
                        hover == (SettingsHit)(SETTINGS_HIT_SIZE_SMALL + i));
    settings_option(0, SETTINGS_ANIM_Y, "On", animations_on, hover == SETTINGS_HIT_ANIMATIONS_ON);
    settings_option(1, SETTINGS_ANIM_Y, "Off", !animations_on, hover == SETTINGS_HIT_ANIMATIONS_OFF);
    login_button(SETTINGS_CARD_X + (SETTINGS_CARD_W - SETTINGS_BACK_W) * 0.5f, SETTINGS_BACK_Y,
                 SETTINGS_BACK_W, SETTINGS_OPTION_H, 17, "Back", 0x7E4FC0u, 0x9465D6u,
                 hover == SETTINGS_HIT_BACK, 0);
}

int art_chat_hit_test(float x, float y)
{
    return inside(x, y, CHAT_X, CHAT_Y, CHAT_W, CHAT_H);
}

void art_draw_chat(const char (*lines)[CHAT_MAX + USERNAME_MAX + 4], const int *line_players,
                   int line_count, const char *input, int focused, float time_s)
{
    static const unsigned player_colors[MAX_PLAYERS] = {0xA82F3Au, 0x2F58B8u};
    int first = 0;
    if (line_count > CHAT_VISIBLE_LINES)
        first = line_count - CHAT_VISIBLE_LINES;
    for (int i = first; i < line_count; i++) {
        int sender = line_players[i];
        unsigned color = INK;
        if (sender >= 0 && sender < MAX_PLAYERS)
            color = player_colors[sender];
        render_color(color);
        render_text(CHAT_X + 10, CHAT_Y + 28 + (float)(i - first) * 18, CHAT_LINE_PX, lines[i]);
    }
    const float box_x = CHAT_X + 8, box_w = CHAT_W - 16, box_h = 20;
    render_color(pick_color(focused, 0xC9872Du, 0x8E9799u));
    render_round_rect(box_x - 1.5f, CHAT_INPUT_Y - 1.5f, box_w + 3, box_h + 3, 5);
    render_color(pick_color(focused, 0xFFFBF2u, 0xEEF1EDu));
    render_round_rect(box_x, CHAT_INPUT_Y, box_w, box_h, 4);
    if (!focused && input[0] == '\0') {
        render_color(0x8E9799u);
        render_text(box_x + 6, CHAT_INPUT_Y + 2, CHAT_LINE_PX, "Press Enter to chat...");
        return;
    }
    /* Long messages scroll so the end being typed stays visible. */
    const char *visible = input;
    while (visible[0] != '\0' && render_text_width(CHAT_LINE_PX, visible) > box_w - 14)
        visible++;
    render_color(INK);
    render_text(box_x + 6, CHAT_INPUT_Y + 2, CHAT_LINE_PX, visible);
    if (focused && fmodf(time_s, 1.0f) < 0.5f)
        render_rect(box_x + 7 + render_text_width(CHAT_LINE_PX, visible), CHAT_INPUT_Y + 3, 1.5f, box_h - 6);
}

void draw_sky(void)
{
    render_gradient(0, 0, CANVAS_W, 104, SKY_TOP, SKY_BOTTOM);
    render_color(SKY_BOTTOM);
    render_rect(0, 104, CANVAS_W, CANVAS_H - 104);
    /* Distant roofline along the banner's bottom edge. */
    render_color(0x1A1630u);
    for (int i = 0; i < 16; i++) {
        float x = (float)i * 52.0f;
        float top = 86.0f + noise(i + 50) * 10.0f;
        render_tri(x - 8, 104, x + 26, top, x + 60, 104);
    }
    const float stars[][3] = {{164, 22, 5}, {146, 74, 4}, {26, 14, 2}, {618, 58, 3},
                              {708, 20, 3}, {784, 14, 4}, {640, 18, 2}};
    render_color(0xE8D199u);
    for (int i = 0; i < 7; i++)
        small_star(stars[i][0], stars[i][1], stars[i][2]);
    /* Crescent moon: a cream disc overlapped by a sky-coloured one. */
    render_color(0xF4EBD0u);
    render_ellipse(644, 40, 21, 21);
    render_color(0x201C3Cu);
    render_ellipse(653, 35, 18, 18);
    /* The witch's house. */
    render_color(0x100D22u);
    render_quad(668, 104, 748, 104, 748, 72, 668, 72);
    render_tri(656, 76, 708, 28, 760, 76);
    render_rect(722, 30, 10, 28);
    render_color(0xF4B945u);
    render_rect(699, 62, 7, 8);
    render_rect(708, 62, 7, 8);
    render_rect(699, 72, 7, 8);
    render_rect(708, 72, 7, 8);
    draw_parchment_banner(170, 8, 442, 84);
    render_color(INK);
    render_text_bold(170 + (442 - render_text_width(44, "The Gingerbread Curse")) * 0.5f, 6, 44,
                     "The Gingerbread Curse");
    render_text_centered(391, 60, 20, "2-player co-op potion game");
    render_color(0xC9C2DEu);
    tilted_text(14, 18, -4, 15, "A cozy attic.");
    tilted_text(16, 39, -4, 15, "A dark secret.");
    tilted_text(18, 60, -4, 15, "A sweeter tomorrow?");
    tilted_text(752, 38, 6, 14, "Mix.");
    tilted_text(735, 55, 6, 14, "Cooperate.");
    tilted_text(735, 72, 6, 14, "Break it.");
}

static void draw_attic(void)
{
    render_color(0x151019u);
    render_rect(ATTIC_X - 3, ATTIC_Y - 3, ATTIC_W + 6, ATTIC_H + 6);
    draw_floor();
    draw_back_wall();
    draw_rafters();
    draw_cobweb(ATTIC_X, ATTIC_Y, 44, 0);
    draw_cobweb(ATTIC_X + ATTIC_W, ATTIC_Y, 44, 1);
    draw_clutter();
    draw_spellbook();
    for (int i = 0; i < INGREDIENT_COUNT; i++)
        draw_station(i);
    draw_cauldron();
    draw_vignette();
}

static void draw_controls_card(void)
{
    const float x = 199, y = 628, w = 186, h = 110;
    card(x, y, w, h, CARD_BLUE);
    draw_gamepad_icon(x + 22, y + 18);
    card_title(x + 42, y + 3, "Controls");
    /* WASD cluster. */
    const float kx = x + 12, ky = y + 34;
    draw_keycap(kx + 23, ky, "W");
    draw_keycap(kx, ky + 23, "A");
    draw_keycap(kx + 23, ky + 23, "S");
    draw_keycap(kx + 46, ky + 23, "D");
    render_color(INK);
    render_text(kx, ky + 49, 13.5f, "or arrows = Move");
    const float rx = x + 90;
    draw_keycap(rx, ky - 1, "E");
    render_color(INK);
    render_text(rx + 25, ky + 1, 14.5f, "Interact");
    draw_keycap(rx, ky + 22, "Q");
    render_color(INK);
    render_text(rx + 25, ky + 24, 14.5f, "Drop");
    draw_mouse_icon(rx + 10.5f, ky + 58, 0.72f, 0xF6F8FCu);
    render_color(INK);
    render_text(rx + 22, ky + 50, 13.5f, "Trace/Stir");
}

static void draw_story_card(float x, float y)
{
    const float w = 237, h = 110;
    card(x, y, w, h, CARD_SAGE);
    draw_witch_silhouette(x + 203, y + 104);
    draw_book_icon(x + 22, y + 18);
    card_title(x + 44, y + 3, "Story");
    render_color(INK);
    const float tx = x + 10;
    render_text(tx, y + 32, 13.5f, "Two girls find a spellbook in the attic.");
    render_text(tx, y + 50, 13.5f, "A witch turned kidnapped children");
    render_text(tx, y + 68, 13.5f, "into gingerbread men. Brew the");
    render_text(tx, y + 86, 13.5f, "right potions, save the children.");
    glPushMatrix();
    glTranslatef(x + 218, y + 106, 0);
    glScalef(0.48f, 0.48f, 1);
    glCallList(list_gingerbread);
    glPopMatrix();
}

static void draw_settings_background(void)
{
    draw_dimmed_menu();
    draw_story_card(281, 582);
    card(SETTINGS_CARD_X, SETTINGS_CARD_Y, SETTINGS_CARD_W, SETTINGS_CARD_H, CREAM);
    render_color(INK);
    render_text_bold(SETTINGS_ROW_X, SETTINGS_CARD_Y + 8, 32, "Settings");
    render_text(SETTINGS_ROW_X, SETTINGS_SIZE_Y - 24, 16, "Window size");
    render_text(SETTINGS_ROW_X, SETTINGS_ANIM_Y - 24, 16, "Animations (off saves power)");
}

static void build_static_lists(void)
{
    list_static[ART_LAYER_GAME] = glGenLists(1);
    glNewList(list_static[ART_LAYER_GAME], GL_COMPILE);
    draw_sky();
    draw_attic();
    draw_recipe_card_base();
    draw_minigame_card_base();
    draw_controls_card();
    draw_chat_card_base();
    draw_fail_card();
    draw_footer();
    glEndList();
    list_static[ART_LAYER_MENU] = glGenLists(1);
    glNewList(list_static[ART_LAYER_MENU], GL_COMPILE);
    draw_menu_layer();
    glEndList();
    list_static[ART_LAYER_SETTINGS] = glGenLists(1);
    glNewList(list_static[ART_LAYER_SETTINGS], GL_COMPILE);
    draw_settings_background();
    glEndList();
    list_static[ART_LAYER_LOGIN] = glGenLists(1);
    glNewList(list_static[ART_LAYER_LOGIN], GL_COMPILE);
    draw_login_layer();
    glEndList();
}

void art_draw_ingredient(int item, float cx, float cy, float scale)
{
    if (item < 0 || item >= INGREDIENT_COUNT)
        return;
    glPushMatrix();
    glTranslatef(cx, cy, 0);
    glScalef(scale, scale, 1);
    glCallList(list_icons + (GLuint)item);
    glPopMatrix();
}

LoginHit art_login_hit_test(float x, float y)
{
    /* The eye sits inside the password field, so it must be checked first. */
    if (inside_login_box(x, y, login_eye_box))
        return LOGIN_HIT_EYE;
    if (inside_login_box(x, y, login_username_box))
        return LOGIN_HIT_USERNAME;
    if (inside_login_box(x, y, login_password_box))
        return LOGIN_HIT_PASSWORD;
    if (inside_login_box(x, y, login_button_box))
        return LOGIN_HIT_LOGIN;
    if (inside_login_box(x, y, login_guest_box))
        return LOGIN_HIT_GUEST;
    if (inside_login_box(x, y, login_signup_box))
        return LOGIN_HIT_SIGNUP;
    return LOGIN_HIT_NONE;
}

/* Draw last, above the scene/minigame overlay. Call only during gameplay. */
void art_draw_exit_button(int hovered)
{
    draw_exit_button(hovered);
}

int art_exit_hit_test(float x, float y)
{
    return inside(x, y, EXIT_BUTTON_X, EXIT_BUTTON_Y,
                  EXIT_BUTTON_W, EXIT_BUTTON_H);
}

int art_disconnect_hit_test(float x,float y)
{
    return inside(x,y,294,370,212,40);
}

/* Centred text, shrunk if needed so a long player name still fits the card. */
static void card_text_fit(float cx, float y, float px, float max_width, const char *text)
{
    const float width = render_text_width(px, text);
    if (width > max_width)
        px *= max_width / width;
    render_text_centered(cx, y, px, text);
}

void art_draw_disconnect_card(const char *title,const char *line1,const char *line2,
                              int leaving,int hovered)
{
    render_color_a(0x08060Fu,0.82f);
    render_rect(199,104,588,482);
    card(220,240,360,196,PARCHMENT);
    render_color(INK);
    card_text_fit(400,256,27,330,title);
    card_text_fit(400,301,17,330,line1);
    card_text_fit(400,328,15,330,line2);
    login_button(294,370,212,40,19,leaving?"Leaving...":"Return to Menu",
                 0x3F9B57u,0x4FB56Au,hovered,leaving);
}

/* Where the badge's button was last drawn; its width depends on the name. */
static float account_button[4];

void art_draw_account_badge(const char *name, int guest, int hovered)
{
    char label[48];
    const char *action = "Log out";
    if (guest) {
        action = "Log in";
        snprintf(label, sizeof(label), "Playing as %s", name);
    } else {
        snprintf(label, sizeof(label), "Signed in as %s", name);
    }
    const float x = 10, y = 10, h = 30, px = 15;
    const float text_w = render_text_width(px, label);
    const float button_w = render_text_width(px, action) + 22;
    const float button_x = x + 12 + text_w + 10;
    render_color_a(0x1E1B2Cu, 0.88f);
    render_round_rect(x, y, button_x + button_w + 5 - x, h, 8);
    render_color(0xFBEBCFu);
    render_text(x + 12, y + (h - px) * 0.5f - 1, px, label);
    login_button(button_x, y + 4, button_w, h - 8, px - 1, action,
                 0x7E4FC0u, 0x9465D6u, hovered, 0);
    account_button[0] = button_x;
    account_button[1] = y + 4;
    account_button[2] = button_w;
    account_button[3] = h - 8;
}

int art_account_hit_test(float x, float y)
{
    return account_button[2] > 0 &&
           inside(x, y, account_button[0], account_button[1], account_button[2], account_button[3]);
}
