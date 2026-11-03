/*
 * Procedural art for The Gingerbread Curse. See Art.h for the overview.
 *
 * Layout on the 800x760 canvas:
 *   y   0-100   night sky with the title banner
 *   y 104-586   attic (the server's original play area), x 199-787
 *   y 590-622   prompt strip with the countdown
 *   y 628-738   controls / story / fail-condition cards
 *   x   8-190   recipe and minigame cards down the left side
 */
#include "Art.h"
#include "GameRules.h"
#include "Render.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265f

/* ---- Palette ------------------------------------------------------------ */
#define INK 0x2B2233u
#define SKY_TOP 0x17142Eu
#define SKY_BOTTOM 0x2A2547u
#define PARCHMENT 0xF7DFB4u
#define CREAM 0xFBEBCFu
#define WOOD_DARK 0x2E2019u
#define WOOD 0x5A3F2Fu
#define WOOD_LIGHT 0x7B5A41u
#define FLOOR 0x4C372Bu
#define PLATE 0xF3E2C2u
#define CARD_MINT 0xBFE3CFu
#define CARD_BLUE 0xDCE6F5u
#define CARD_SAGE 0xDDE3DAu
#define CARD_PINK 0xF9C6CDu
#define LILAC 0xC9B7EEu
#define POTION 0x79D44Fu

/* Attic bounds. These match the walls enforced by game_move() on the server. */
#define ATTIC_X 199.0f
#define ATTIC_Y 104.0f
#define ATTIC_W 588.0f
#define ATTIC_H 482.0f
#define WALL_H 48.0f
#define LEFT_COL_X 8.0f
#define LEFT_COL_W 182.0f
#define RECIPE_Y 104.0f
#define RECIPE_H 238.0f
#define MINI_Y 350.0f
#define MINI_H 388.0f
#define RUNE_CARD_Y 416.0f
#define STIR_CARD_Y 586.0f
#define PREVIEW_CARD_H 138.0f
#define PREVIEW_CARD_X (LEFT_COL_X + 12.0f)
#define PREVIEW_CARD_W (LEFT_COL_W - 24.0f)

/* Display lists built once by art_init(). */
static GLuint list_static[ART_LAYER_COUNT];
static GLuint list_icons = 0; /* INGREDIENT_COUNT consecutive lists. */
static GLuint list_gingerbread = 0;
static GLuint list_players = 0; /* MAX_PLAYERS consecutive lists. */
static GLuint list_stir_card = 0;

/* The static layer, captured from the framebuffer after its first draw. */
static GLuint static_texture[ART_LAYER_COUNT];
static int static_texture_valid[ART_LAYER_COUNT];

/* Deterministic pseudo-random values, so scenery looks hand-placed but is
 * identical on both clients and across display-list rebuilds. */
static float noise(int n)
{
    unsigned x = (unsigned)n * 2654435761u;
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;
    return (float)(x & 0xFFFFu) / 65535.0f;
}

static unsigned pick_color(int condition, unsigned if_true, unsigned if_false)
{
    if (condition)
        return if_true;
    return if_false;
}

static float pick_float(int condition, float if_true, float if_false)
{
    if (condition)
        return if_true;
    return if_false;
}

/* Filled ellipse with a dark cartoon outline. */
static void outlined_ellipse(float cx, float cy, float rx, float ry, unsigned fill)
{
    render_color(INK);
    render_ellipse(cx, cy, rx + 1.4f, ry + 1.4f);
    render_color(fill);
    render_ellipse(cx, cy, rx, ry);
}

static void outlined_box(float x, float y, float w, float h, float radius, unsigned fill)
{
    render_color(INK);
    render_round_rect(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f);
    render_color(fill);
    render_round_rect(x, y, w, h, radius);
}

/* ---- Ingredients: each is drawn around (0,0) in about +/-22 units ------- */
static void petal_ring(int petals, float distance, float rx, float ry, unsigned fill)
{
    for (int i = 0; i < petals; i++) {
        glPushMatrix();
        glRotatef(360.0f * (float)i / (float)petals - 90.0f, 0, 0, 1);
        render_color(INK);
        render_ellipse(distance, 0, rx + 1.2f, ry + 1.2f);
        render_color(fill);
        render_ellipse(distance, 0, rx, ry);
        glPopMatrix();
    }
}

static void leaf(float x, float y, float degrees, float length, unsigned fill)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glRotatef(degrees, 0, 0, 1);
    outlined_ellipse(length * 0.5f, 0, length * 0.5f, length * 0.22f, fill);
    render_color(0x2C6A36u);
    render_line(2, 0, length - 2, 0, 1.0f);
    glPopMatrix();
}

static void draw_moonflower(void)
{
    leaf(-6, 7, 160, 17, 0x4FA35Au);
    leaf(6, 7, 20, 17, 0x4FA35Au);
    leaf(0, 9, 90, 12, 0x3E8C4Du);
    petal_ring(5, 8.5f, 7.5f, 5.5f, 0xFBF7FFu);
    outlined_ellipse(0, 0, 3.6f, 3.6f, 0xF3C93Eu);
}

static void draw_ginger(void)
{
    const float lobes[][4] = {{-11, 6, 8, 6}, {-3, -4, 6.5f, 8}, {6, 3, 10, 7},
                              {14, -6, 5, 7}, {-13, -5, 4.5f, 5}, {2, 12, 5, 5}};
    render_color(INK);
    for (int i = 0; i < 6; i++)
        render_ellipse(lobes[i][0], lobes[i][1], lobes[i][2] + 1.4f, lobes[i][3] + 1.4f);
    render_color(0xE2B56Du);
    for (int i = 0; i < 6; i++)
        render_ellipse(lobes[i][0], lobes[i][1], lobes[i][2], lobes[i][3]);
    render_color(0xBC8C47u);
    render_line(-9, 8, -3, 6, 1.1f);
    render_line(4, 5, 11, 3, 1.1f);
    render_line(-5, -6, -1, -8, 1.1f);
}

static void draw_feather(void)
{
    /* A curved blade built from overlapping barbs along a quill. */
    glPushMatrix();
    glRotatef(-38, 0, 0, 1);
    render_color(0x14121Cu);
    render_ellipse(0, -3, 7.5f, 18);
    for (int i = 0; i < 6; i++) {
        float y = -14.0f + (float)i * 5.0f;
        float h = 8.0f - fabsf((float)i - 2.5f);
        render_tri(0, y, -9.5f, y + 4, 0, y + 4);
        render_tri(0, y, h + 1.5f, y + 4, 0, y + 4);
    }
    render_color(0x4A4560u);
    render_line(0, -19, 0, 22, 1.4f);
    render_line(0, -10, -5, -5, 0.8f);
    render_line(0, -2, 5, 3, 0.8f);
    glPopMatrix();
}

static void crystal(float x, float base, float half_width, float height, unsigned light, unsigned dark)
{
    render_color(INK);
    render_quad(x - half_width - 1.5f, base + 1, x + half_width + 1.5f, base + 1,
                x + half_width + 1.5f, base - height * 0.7f, x - half_width - 1.5f, base - height * 0.7f);
    render_tri(x - half_width - 1.5f, base - height * 0.7f, x + half_width + 1.5f,
               base - height * 0.7f, x, base - height - 2);
    render_color(light);
    render_quad(x - half_width, base, x, base, x, base - height, x - half_width, base - height * 0.7f);
    render_color(dark);
    render_quad(x, base, x + half_width, base, x + half_width, base - height * 0.7f, x, base - height);
}

static void draw_sugar_glass(void)
{
    crystal(-11, 14, 5.5f, 19, 0xCFE6FFu, 0x9EC4F2u);
    crystal(11, 14, 5.5f, 21, 0xCFE6FFu, 0x8FB8EDu);
    crystal(0, 16, 7.5f, 33, 0xE6F3FFu, 0xABD0F7u);
}

static void draw_moss(void)
{
    outlined_ellipse(0, 8, 19, 10, 0x77727Eu);
    const float tufts[][3] = {{-12, 2, 8}, {-3, -3, 9}, {8, 0, 9}, {14, 6, 6}, {-14, 8, 6}, {2, 6, 8}};
    render_color(INK);
    for (int i = 0; i < 6; i++)
        render_ellipse(tufts[i][0], tufts[i][1], tufts[i][2] + 1.3f, tufts[i][2] * 0.85f + 1.3f);
    for (int i = 0; i < 6; i++) {
        render_color(pick_color(i % 2, 0x5FA347u, 0x79BB57u));
        render_ellipse(tufts[i][0], tufts[i][1], tufts[i][2], tufts[i][2] * 0.85f);
    }
    render_color(0xA7DB80u);
    render_ellipse(-4, -5, 2.4f, 1.6f);
    render_ellipse(8, -2, 2.0f, 1.4f);
}

static void draw_berries(void)
{
    leaf(-1, -10, -150, 15, 0x4A9450u);
    leaf(1, -10, -30, 15, 0x3E8C4Du);
    const float berries[][2] = {{-9, 2}, {8, 1}, {0, -3}, {-5, 11}, {6, 11}};
    for (int i = 0; i < 5; i++) {
        outlined_ellipse(berries[i][0], berries[i][1], 7.2f, 7.2f, 0x6A3AA2u);
        render_color(0xB997E8u);
        render_ellipse(berries[i][0] - 2.4f, berries[i][1] - 2.6f, 1.7f, 1.7f);
    }
}

static void draw_toadstool(void)
{
    outlined_box(-5, 0, 10, 18, 4, 0xF5ECD8u);
    render_color(INK);
    render_ellipse(0, -2, 19.5f, 15.5f);
    render_rect(-20, -2, 40, 5);
    render_color(0xD94A3Fu);
    render_ellipse(0, -2, 18, 14);
    render_rect(-18, -2, 36, 3.6f);
    render_color(0xFFF4E2u);
    render_ellipse(-8, -5, 3.6f, 3.2f);
    render_ellipse(6, -9, 3.2f, 2.8f);
    render_ellipse(10, -1, 2.4f, 2.2f);
    render_ellipse(-1, -1, 2.0f, 1.8f);
}

static void draw_phoenix_ash(void)
{
    outlined_ellipse(0, 11, 19, 8, 0x8D8690u);
    render_color(0xB5AEB6u);
    render_ellipse(-4, 8, 8, 3.5f);
    render_color(INK);
    render_ellipse(0, -3, 11, 15);
    render_tri(-11, -3, 11, -3, 2, -22);
    render_color(0xF08A2Cu);
    render_ellipse(0, -2, 9.5f, 13);
    render_tri(-9.5f, -2, 9.5f, -2, 2, -20);
    render_color(0xFFD45Au);
    render_ellipse(0, 2, 5, 8);
    render_tri(-5, 2, 5, 2, 1, -9);
}

static void droplet(float x, float y, float size, unsigned fill)
{
    render_color(INK);
    render_tri(x - size - 1.2f, y, x + size + 1.2f, y, x, y - size * 2.3f);
    render_ellipse(x, y, size + 1.2f, size + 1.2f);
    render_color(fill);
    render_tri(x - size, y, x + size, y, x, y - size * 2.0f);
    render_ellipse(x, y, size, size);
    render_color(0xFFFFFFu);
    render_ellipse(x - size * 0.35f, y - size * 0.15f, size * 0.24f, size * 0.32f);
}

static void draw_silver_dew(void)
{
    droplet(0, 5, 11, 0xCBD7E4u);
    droplet(-14, 14, 5, 0xB6C5D6u);
    droplet(14, 13, 6, 0xB6C5D6u);
}

static void draw_thornvine(void)
{
    /* A vine sampled along a sine curve, with thorns on alternating sides. */
    float px = -18, py = 9;
    for (int i = 1; i <= 12; i++) {
        float x = -18.0f + (float)i * 3.0f;
        float y = 9.0f * cosf((float)i * 0.62f);
        render_color(INK);
        render_line(px, py, x, y, 6.5f);
        px = x;
        py = y;
    }
    px = -18;
    py = 9;
    for (int i = 1; i <= 12; i++) {
        float x = -18.0f + (float)i * 3.0f;
        float y = 9.0f * cosf((float)i * 0.62f);
        render_color(0x4E8A3Du);
        render_line(px, py, x, y, 4.0f);
        if (i % 2 == 0) {
            float side = pick_float(i % 4 == 0, -1.0f, 1.0f);
            render_color(0xE4E1B8u);
            render_tri(x - 2.2f, y + side * 1.8f, x + 2.2f, y + side * 1.8f, x, y + side * 7.8f);
        }
        px = x;
        py = y;
    }
    leaf(-16, 8, -120, 11, 0x5FA347u);
    leaf(16, 8, -60, 11, 0x5FA347u);
}

static void (*const ingredient_painters[INGREDIENT_COUNT])(void) = {
    draw_moonflower, draw_ginger, draw_feather, draw_sugar_glass, draw_moss,
    draw_berries, draw_toadstool, draw_phoenix_ash, draw_silver_dew, draw_thornvine
};

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

static void draw_exit_button(){
    render_rect(0,0,100,100);
}

/* ---- Characters ---------------------------------------------------------- */
static void draw_player_sprite(int player)
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

static void draw_gingerbread(void)
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

/* ---- Attic scenery ------------------------------------------------------- */
static void draw_floor(void)
{
    const float floor_top = ATTIC_Y + WALL_H;
    const float floor_h = ATTIC_H - WALL_H;
    render_color(FLOOR);
    render_rect(ATTIC_X, floor_top, ATTIC_W, floor_h);
    /* Planks with staggered seams and slight colour variation. */
    int row = 0;
    for (float y = floor_top; y < ATTIC_Y + ATTIC_H; y += 26.0f, row++) {
        float x = ATTIC_X - noise(row) * 70.0f;
        int seam = 0;
        while (x < ATTIC_X + ATTIC_W) {
            float length = 70.0f + noise(row * 31 + seam) * 70.0f;
            float left = x;
            float right = x + length;
            if (left < ATTIC_X)
                left = ATTIC_X;
            if (right > ATTIC_X + ATTIC_W)
                right = ATTIC_X + ATTIC_W;
            float shade = noise(row * 97 + seam * 13);
            render_color_a(pick_color(shade > 0.5f, 0x6A4B39u, 0x33241Cu), 0.10f + 0.20f * fabsf(shade - 0.5f));
            render_rect(left, y, right - left, 26);
            render_color_a(0x1F1510u, 0.65f);
            if (x >= ATTIC_X)
                render_rect(x, y, 1.4f, 26);
            x += length;
            seam++;
        }
        render_color_a(0x1F1510u, 0.7f);
        render_rect(ATTIC_X, y, ATTIC_W, 1.6f);
    }
}

static void draw_back_wall(void)
{
    render_gradient(ATTIC_X, ATTIC_Y, ATTIC_W, WALL_H, 0x30221Eu, 0x4A3528u);
    for (float x = ATTIC_X + 8; x < ATTIC_X + ATTIC_W; x += 30.0f) {
        render_color_a(0x1D1411u, 0.6f);
        render_rect(x, ATTIC_Y, 1.5f, WALL_H);
    }
    /* Skirting board. */
    render_color(WOOD_DARK);
    render_rect(ATTIC_X, ATTIC_Y + WALL_H - 3, ATTIC_W, 5);
    /* Vertical posts. */
    const float posts[] = {255, 400, 590, 690};
    for (int i = 0; i < 4; i++) {
        render_color(WOOD_DARK);
        render_rect(posts[i] - 1.5f, ATTIC_Y, 11, WALL_H);
        render_color(WOOD);
        render_rect(posts[i], ATTIC_Y, 8, WALL_H);
    }
    /* Arched window, centred over the cauldron. */
    const float cx = (float)cauldron_x;
    render_color(WOOD_DARK);
    render_rect(cx - 25, ATTIC_Y + 22, 50, 26);
    render_ellipse(cx, ATTIC_Y + 22, 25, 18);
    render_gradient(cx - 20, ATTIC_Y + 22, 40, 22, 0x2A3A87u, 0x3E4FA3u);
    render_color(0x2A3A87u);
    render_ellipse(cx, ATTIC_Y + 22, 20, 14);
    render_color(0xFFF3C4u);
    render_ellipse(cx - 11, ATTIC_Y + 20, 1, 1);
    render_ellipse(cx + 8, ATTIC_Y + 14, 1, 1);
    render_ellipse(cx + 12, ATTIC_Y + 30, 1, 1);
    render_color(0x1F2A5Eu);
    render_tri(cx - 20, ATTIC_Y + 44, cx - 8, ATTIC_Y + 44, cx - 16, ATTIC_Y + 30);
    render_tri(cx + 20, ATTIC_Y + 44, cx + 7, ATTIC_Y + 44, cx + 15, ATTIC_Y + 32);
    render_color(WOOD_DARK);
    render_rect(cx - 1.5f, ATTIC_Y + 8, 3, 36);
    render_rect(cx - 20, ATTIC_Y + 26, 40, 2.5f);
    /* Candle stubs either side of the window; the flames are animated. */
    const float candles[] = {cx - 45, cx + 45};
    for (int i = 0; i < 2; i++) {
        render_color(WOOD_DARK);
        render_rect(candles[i] - 7, ATTIC_Y + 37, 14, 3);
        outlined_box(candles[i] - 3, ATTIC_Y + 26, 6, 11, 1, 0xF4E7C8u);
    }
}

static void draw_rafters(void)
{
    /* Sloped roof beams cutting across the top corners. */
    const float l = ATTIC_X, r = ATTIC_X + ATTIC_W, t = ATTIC_Y;
    render_color(WOOD_DARK);
    render_quad(l, t + 52, l + 62, t, l + 76, t, l, t + 66);
    render_quad(r, t + 52, r - 62, t, r - 76, t, r, t + 66);
    render_color(WOOD);
    render_quad(l, t + 55, l + 65, t, l + 73, t, l, t + 62);
    render_quad(r, t + 55, r - 65, t, r - 73, t, r, t + 62);
}

static void draw_cobweb(float cx, float cy, float reach, int flip)
{
    const float sign = pick_float(flip, -1.0f, 1.0f);
    render_color_a(0xE9E4F0u, 0.5f);
    for (int spoke = 0; spoke < 5; spoke++) {
        float a = (float)spoke * PI * 0.125f;
        render_line(cx, cy, cx + sign * reach * cosf(a), cy + reach * sinf(a), 0.8f);
    }
    for (float ring = 0.35f; ring < 1.0f; ring += 0.3f) {
        for (int spoke = 0; spoke < 4; spoke++) {
            float a0 = (float)spoke * PI * 0.125f, a1 = a0 + PI * 0.125f;
            render_line(cx + sign * reach * ring * cosf(a0), cy + reach * ring * sinf(a0),
                        cx + sign * reach * ring * cosf(a1), cy + reach * ring * sinf(a1), 0.7f);
        }
    }
}

static void wooden_crate(float x, float y, float w, float h)
{
    outlined_box(x, y, w, h, 2, WOOD);
    render_color(WOOD_LIGHT);
    render_rect(x + 1, y + 1, w - 2, 4);
    render_color(WOOD_DARK);
    render_line(x + 3, y + 6, x + w - 3, y + h - 3, 2.2f);
    render_line(x + w - 3, y + 6, x + 3, y + h - 3, 2.2f);
    render_rect(x, y + h * 0.5f, w, 1.2f);
}

static void draw_clutter(void)
{
    /* Everything sits in gaps the server leaves between the crates. */
    const float right = ATTIC_X + ATTIC_W;
    /* Left wall: draped sheet, side table with a candle, stacked boxes. */
    render_color(INK);
    render_ellipse(ATTIC_X + 18, 268, 18, 22);
    render_rect(ATTIC_X, 268, 37, 26);
    render_color(0x5B4E73u);
    render_ellipse(ATTIC_X + 17, 268, 16.5f, 20.5f);
    render_rect(ATTIC_X, 268, 33.5f, 24.5f);
    render_color(0x463B5Eu);
    render_line(ATTIC_X + 9, 262, ATTIC_X + 7, 292, 2);
    render_line(ATTIC_X + 24, 258, ATTIC_X + 26, 292, 2);
    outlined_box(ATTIC_X + 4, 340, 42, 8, 2, WOOD_LIGHT);
    render_color(WOOD_DARK);
    render_rect(ATTIC_X + 9, 348, 5, 24);
    render_rect(ATTIC_X + 36, 348, 5, 24);
    outlined_box(ATTIC_X + 20, 322, 9, 18, 2, 0xF4E7C8u);
    wooden_crate(ATTIC_X + 3, 390, 34, 30);
    wooden_crate(ATTIC_X + 9, 366 + 54, 26, 24);
    /* Right wall: ladder, draped sheet, stacked boxes, a hatch of crates. */
    render_color(WOOD_DARK);
    render_rect(right - 44, 118, 5, 158);
    render_rect(right - 12, 118, 5, 158);
    for (float y = 135; y < 276; y += 22) {
        render_color(WOOD_DARK);
        render_rect(right - 41, y, 31, 6);
        render_color(WOOD);
        render_rect(right - 41, y + 1, 31, 3.5f);
    }
    render_color(INK);
    render_round_rect(right - 50, 300, 49, 54, 7);
    render_color(0x4A3D70u);
    render_round_rect(right - 48.5f, 301.5f, 46, 51, 6);
    render_color(0x392F58u);
    render_line(right - 38, 306, right - 40, 352, 2);
    render_line(right - 20, 306, right - 16, 352, 2);
    wooden_crate(right - 40, 372, 38, 34);
    wooden_crate(right - 50, 536, 48, 44);
    /* A fallen box, bottom-left corner. */
    wooden_crate(ATTIC_X + 5, 532, 16, 46);
}

/* Soft darkening towards the walls: cheap, and it does most of the "mood". */
static void draw_vignette(void)
{
    const float l = ATTIC_X, t = ATTIC_Y, w = ATTIC_W, h = ATTIC_H;
    const float band = 58.0f;
    const float edges[4][8] = {
        {l, t, l + w, t, l + w, t + band, l, t + band},
        {l, t + h, l + w, t + h, l + w, t + h - band, l, t + h - band},
        {l, t, l, t + h, l + band, t + h, l + band, t},
        {l + w, t, l + w, t + h, l + w - band, t + h, l + w - band, t}
    };
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
        render_color_a(0x0C0810u, 0.55f);
        glVertex2f(edges[i][0], edges[i][1]);
        glVertex2f(edges[i][2], edges[i][3]);
        render_color_a(0x0C0810u, 0.0f);
        glVertex2f(edges[i][4], edges[i][5]);
        glVertex2f(edges[i][6], edges[i][7]);
    }
    glEnd();
}

static void name_plate(float cx, float y, float px, const char *text, unsigned ink)
{
    const float text_px = px - 2.5f;
    float w = render_text_width(text_px, text) + 10.0f;
    render_color(INK);
    render_round_rect(cx - w * 0.5f - 1.3f, y - 1.3f, w + 2.6f, px + 2.6f, 3.5f);
    render_color(PLATE);
    render_round_rect(cx - w * 0.5f, y, w, px, 3);
    render_color(ink);
    render_text_centered(cx, y - px * 0.1f, text_px, text);
}

static void draw_station(int item)
{
    /* Crate tops match the 42-unit footprint the old squares had, so the
     * server's pick-up radius still feels centred on the item. */
    const float cx = (float)ingredient_x[item], cy = (float)ingredient_y[item];
    render_color_a(0x0C0810u, 0.4f);
    render_ellipse(cx, cy + 34, 37, 7);
    outlined_box(cx - 34, cy - 31, 68, 62, 3, 0x5E4332u);
    render_color(0x3B291Fu);
    render_rect(cx - 30, cy - 27, 60, 54);
    render_color(0x4A3528u);
    render_rect(cx - 27, cy - 24, 54, 48);
    render_color(WOOD_LIGHT);
    render_rect(cx - 34, cy - 31, 68, 3);
    name_plate(cx, cy + 25, 15, ingredient_names[item], INK);
}

static void draw_cauldron(void)
{
    const float cx = (float)cauldron_x, cy = (float)cauldron_y;
    /* Stone hearth under the pot. */
    render_color_a(0x0C0810u, 0.45f);
    render_ellipse(cx, cy + 42, 58, 12);
    render_color(INK);
    render_quad(cx - 40, cy + 18, cx + 40, cy + 18, cx + 54, cy + 46, cx - 54, cy + 46);
    render_color(0x6A4B39u);
    render_quad(cx - 38.5f, cy + 19.5f, cx + 38.5f, cy + 19.5f, cx + 52, cy + 44.5f, cx - 52, cy + 44.5f);
    /* Feet. */
    outlined_box(cx - 31, cy + 24, 9, 15, 3, 0x262530u);
    outlined_box(cx + 22, cy + 24, 9, 15, 3, 0x262530u);
    /* Body. */
    outlined_ellipse(cx, cy + 4, 45, 33, 0x33323Du);
    render_color(0x4A4956u);
    render_ellipse(cx - 18, cy + 4, 11, 18);
    render_color(0x33323Du);
    render_ellipse(cx - 11, cy + 6, 11, 21);
    /* Rim and potion surface. */
    outlined_ellipse(cx, cy - 20, 41, 11, 0x484754u);
    render_color(0x1E1D27u);
    render_ellipse(cx, cy - 20, 35, 8);
    render_color(POTION);
    render_ellipse(cx, cy - 19, 33, 6.8f);
    render_color(0xB3F07Du);
    render_ellipse(cx - 11, cy - 21, 10, 2.1f);
    name_plate(cx, cy + 49, 15.5f, "Cauldron", INK);
}

static void draw_spellbook(void)
{
    /* A small purple rug and the open tome, in the gap above the lower crates. */
    const float cx = (float)cauldron_x + 86, cy = 415;
    render_color(0x3D2F5Cu);
    render_round_rect(cx - 30, cy - 18, 60, 36, 3);
    render_color_a(0xB9A4E2u, 0.45f);
    render_rect(cx - 27, cy - 15, 54, 1);
    render_rect(cx - 27, cy + 14, 54, 1);
    outlined_box(cx - 16, cy - 12, 32, 24, 2, 0x5A3C94u);
    render_color(0xE9C86Bu);
    render_ellipse(cx, cy, 8, 8);
    render_color(0x5A3C94u);
    render_ellipse(cx, cy, 7, 7);
    render_color(0xE9C86Bu);
    for (int i = 0; i < 5; i++) {
        float a0 = -PI / 2 + 2 * PI * (float)i / 5, a1 = -PI / 2 + 2 * PI * (float)(i + 2) / 5;
        render_line(cx + 6.5f * cosf(a0), cy + 6.5f * sinf(a0),
                    cx + 6.5f * cosf(a1), cy + 6.5f * sinf(a1), 0.9f);
    }
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

/* ---- Night sky and the title banner -------------------------------------- */
static void small_star(float x, float y, float r)
{
    render_quad(x - r, y, x, y - r * 0.28f, x + r, y, x, y + r * 0.28f);
    render_quad(x, y - r, x + r * 0.28f, y, x, y + r, x - r * 0.28f, y);
}

static void draw_parchment_banner(float x, float y, float w, float h)
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

static void tilted_text(float x, float y, float degrees, float px, const char *text)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glRotatef(degrees, 0, 0, 1);
    render_text(0, 0, px, text);
    glPopMatrix();
}

static void draw_sky(void)
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

/* ---- HUD cards ------------------------------------------------------------ */
static void card(float x, float y, float w, float h, unsigned fill)
{
    render_color_a(0x0A0814u, 0.45f);
    render_round_rect(x + 2, y + 3, w, h, 9);
    render_color(fill);
    render_round_rect(x, y, w, h, 9);
}

static void card_title(float x, float y, const char *text)
{
    render_color(INK);
    render_text_bold(x, y, 25, text);
}

static void draw_flask_icon(float x, float y)
{
    render_color(INK);
    render_ellipse(x, y + 8, 11, 11);
    render_rect(x - 3.5f, y - 10, 7, 12);
    render_rect(x - 5, y - 12, 10, 3.5f);
    render_color(0x6C7078u);
    render_ellipse(x - 4, y + 5, 2.4f, 2.4f);
}

static void draw_gamepad_icon(float x, float y)
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

static void draw_book_icon(float x, float y)
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

static void draw_skull_icon(float x, float y)
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

static void draw_keycap(float x, float y, const char *letter)
{
    render_color(INK);
    render_round_rect(x, y, 21, 21, 4);
    render_color(0xF6F8FCu);
    render_round_rect(x + 1.6f, y + 1.6f, 17.8f, 17.8f, 3);
    render_color(INK);
    render_text_centered(x + 10.5f, y + 1.5f, 16.5f, letter);
}

static void draw_mouse_icon(float x, float y, float scale, unsigned fill)
{
    const float w = 19 * scale, h = 27 * scale;
    outlined_box(x - w * 0.5f, y - h * 0.5f, w, h, w * 0.45f, fill);
    render_color(INK);
    render_rect(x - 0.7f, y - h * 0.5f, 1.4f, h * 0.38f);
    render_rect(x - w * 0.5f, y - h * 0.12f, w, 1.4f);
}

/* The little arrow cursor from the concept's minigame previews. */
static void draw_cursor(float x, float y)
{
    render_color(INK);
    render_tri(x - 1.2f, y - 2.5f, x - 1.2f, y + 15, x + 11.5f, y + 10);
    render_quad(x + 2, y + 9, x + 5, y + 8, x + 9, y + 17, x + 6, y + 18);
    render_color(0xFFFFFFu);
    render_tri(x, y, x, y + 12.5f, x + 9, y + 9.2f);
    render_quad(x + 3, y + 9, x + 4.5f, y + 8.5f, x + 8, y + 16, x + 6.5f, y + 16.5f);
}

static void draw_witch_silhouette(float x, float y)
{
    render_color_a(0x8E9799u, 0.55f);
    render_tri(x - 34, y - 62, x + 26, y - 62, x - 2, y - 96); /* hat */
    render_ellipse(x - 4, y - 61, 32, 5);
    render_ellipse(x - 2, y - 48, 13, 14);
    render_tri(x + 10, y - 50, x + 21, y - 43, x + 9, y - 42); /* nose */
    render_quad(x - 13, y - 38, x + 9, y - 38, x + 26, y + 6, x - 30, y + 6);
    render_line(x + 6, y - 28, x + 28, y - 23, 5); /* arm */
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

#define CHAT_X 393.0f
#define CHAT_Y 628.0f
#define CHAT_W 237.0f
#define CHAT_H 110.0f
#define CHAT_INPUT_Y (CHAT_Y + CHAT_H - 26.0f)

static void draw_chat_card_base(void)
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

static void draw_fail_card(void)
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

static void draw_recipe_card_base(void)
{
    card(LEFT_COL_X, RECIPE_Y, LEFT_COL_W, RECIPE_H, CREAM);
    draw_flask_icon(LEFT_COL_X + 20, RECIPE_Y + 20);
    render_color(INK);
    render_text(LEFT_COL_X + 12, RECIPE_Y + 38, 14.5f, "Collect the correct");
    render_text(LEFT_COL_X + 12, RECIPE_Y + 54, 14.5f, "ingredients, in this order:");
}

static void draw_minigame_card_base(void)
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

static void draw_stir_preview(void)
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

static void draw_footer(void)
{
    render_color(0x8C86A8u);
    render_text_centered(493, 741, 14, "GOOD POTIONS   -   BRIGHTER TOMORROWS");
}

/* The menu painting is taller than the canvas, so it is cropped to a band that
 * keeps the title and the gingerbread men. These convert painting pixels to canvas. */
#define MENU_IMAGE_FILE "assets/main_menu.png"
#define MENU_IMAGE_W 972.0f
#define MENU_IMAGE_H 1090.0f
#define MENU_CROP_TOP 110.0f
#define MENU_SCALE ((float)CANVAS_W / MENU_IMAGE_W)
#define MENU_X(px) ((px) * MENU_SCALE)
#define MENU_Y(py) (((py) - MENU_CROP_TOP) * MENU_SCALE)

/* Where the wooden signs are painted: x, y, width, height in painting pixels. */
#define MENU_BUTTON_COUNT 3
static const float menu_buttons[MENU_BUTTON_COUNT][4] = {
    {490, 580, 283, 98}, {490, 697, 283, 90}, {490, 808, 283, 92}
};
static GLuint menu_texture = 0;

static void draw_menu_background(void)
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

static void draw_menu_layer(void)
{
    static const char *const fallback_labels[MENU_BUTTON_COUNT] = {"Play", "Settings", "Quit"};
    draw_menu_background();
    if (menu_texture != 0)
        return;
    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        const float *b = menu_buttons[i];
        outlined_box(MENU_X(b[0]), MENU_Y(b[1]), b[2] * MENU_SCALE, b[3] * MENU_SCALE, 6, PARCHMENT);
        render_color(INK);
        render_text_centered(MENU_X(b[0] + b[2] * 0.5f), MENU_Y(b[1]) + 22, 30, fallback_labels[i]);
    }
}

/* Dimmed painting behind the login and settings panels. */
static void draw_dimmed_menu(void)
{
    draw_menu_background();
    render_color_a(0x0C0816u, 0.5f);
    render_rect(0, 0, CANVAS_W, CANVAS_H);
}

/* The login painting, cropped the same way. LOGIN_X/Y convert painting pixels. */
#define LOGIN_IMAGE_FILE "assets/login.png"
#define LOGIN_IMAGE_W 970.0f
#define LOGIN_IMAGE_H 1092.0f
#define LOGIN_CROP_TOP 90.0f
#define LOGIN_SCALE ((float)CANVAS_W / LOGIN_IMAGE_W)
#define LOGIN_X(px) ((px) * LOGIN_SCALE)
#define LOGIN_Y(py) (((py) - LOGIN_CROP_TOP) * LOGIN_SCALE)
#define LOGIN_PILL_COLOR 0xF0DED2u
#define LOGIN_PAPER_COLOR 0xF9E0CCu

/* Everything clickable on the form: x, y, width, height in painting pixels. */
static const float login_username_box[4] = {230, 425, 503, 78};
static const float login_password_box[4] = {230, 530, 503, 78};
static const float login_eye_box[4] = {655, 545, 62, 48};
static const float login_button_box[4] = {245, 633, 475, 82};
static const float login_guest_box[4] = {303, 785, 360, 68};
static const float login_signup_box[4] = {578, 882, 96, 38};
static GLuint login_texture = 0;

static void login_rect(const float box[4], float radius)
{
    render_round_rect(LOGIN_X(box[0]), LOGIN_Y(box[1]), box[2] * LOGIN_SCALE, box[3] * LOGIN_SCALE, radius);
}

static void draw_login_fallback(void)
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

#define SETTINGS_CARD_X 200.0f
#define SETTINGS_CARD_Y 262.0f
#define SETTINGS_CARD_W 400.0f
#define SETTINGS_CARD_H 310.0f
#define SETTINGS_ROW_X (SETTINGS_CARD_X + 24.0f)
#define SETTINGS_OPTION_W 112.0f
#define SETTINGS_OPTION_H 36.0f
#define SETTINGS_OPTION_GAP 8.0f
#define SETTINGS_SIZE_Y (SETTINGS_CARD_Y + 94.0f)
#define SETTINGS_ANIM_Y (SETTINGS_CARD_Y + 178.0f)
#define SETTINGS_BACK_W 120.0f
#define SETTINGS_BACK_Y (SETTINGS_CARD_Y + 244.0f)

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
    draw_exit_button();
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

void art_init(void)
{
    menu_texture = render_load_image(MENU_IMAGE_FILE);
    login_texture = render_load_image(LOGIN_IMAGE_FILE);
    /* Sprites first: the static list calls some of these lists. */
    list_icons = glGenLists(INGREDIENT_COUNT);
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        glNewList(list_icons + (GLuint)i, GL_COMPILE);
        ingredient_painters[i]();
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

/* ---- Animated layer ------------------------------------------------------ */
static void candle_flame(float x, float y, float time_s, int seed)
{
    const float flicker = 0.75f + 0.25f * sinf(time_s * 9.0f + (float)seed * 2.1f) *
                                         sinf(time_s * 4.3f + (float)seed);
    render_glow(x, y, 52, 42, 0xFFC964u, 0.34f * flicker);
    render_color(0xF59A2Fu);
    render_ellipse(x, y, 2.8f, 4.2f * flicker + 0.8f);
    render_color(0xFFE28Au);
    render_ellipse(x, y + 0.8f, 1.5f, 2.6f * flicker);
}

static void draw_rune_shape(int rune, float cx, float cy, float size)
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

/* Where the preview's cursor sits at progress t in 0..1, tracing the rune. */
static void rune_cursor_position(int rune, float t, float cx, float cy, float size, float *x, float *y)
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

/* ---- Live HUD contents --------------------------------------------------- */
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

static void login_button(float x, float y, float w, float h, float px, const char *text,
                         unsigned fill, unsigned hover_fill, int hovered, int disabled);

#define PLAY_AGAIN_W 170.0f
#define PLAY_AGAIN_H 40.0f
#define PLAY_AGAIN_X (ATTIC_X + ATTIC_W * 0.5f - PLAY_AGAIN_W * 0.5f)
#define PLAY_AGAIN_Y (ATTIC_Y + ATTIC_H * 0.5f + 28.0f)

int art_play_again_hit_test(float x, float y)
{
    return x >= PLAY_AGAIN_X && x < PLAY_AGAIN_X + PLAY_AGAIN_W &&
           y >= PLAY_AGAIN_Y && y < PLAY_AGAIN_Y + PLAY_AGAIN_H;
}

void art_draw_end_card(int won, int recipes_won, int play_again_hovered, float time_s)
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
    /* One gingerbread man either side of the button. */
    for (int i = 0; i < 2; i++) {
        float hop = 0.0f;
        if (won)
            hop = fabsf(sinf(time_s * 3 + (float)i)) * 8.0f;
        glPushMatrix();
        glTranslatef(pick_float(i == 0, cx - 140, cx + 140), cy + 72 - hop, 0);
        glCallList(list_gingerbread);
        glPopMatrix();
    }
    login_button(PLAY_AGAIN_X, PLAY_AGAIN_Y, PLAY_AGAIN_W, PLAY_AGAIN_H, 20, "Play Again",
                 0x3F9B57u, 0x4FB56Au, play_again_hovered, 0);
}

/* ---- Login screen -------------------------------------------------------- */
static int inside(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int inside_login_box(float x, float y, const float box[4])
{
    return inside(x, y, LOGIN_X(box[0]), LOGIN_Y(box[1]), box[2] * LOGIN_SCALE, box[3] * LOGIN_SCALE);
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

/* Typed text replaces the painted placeholder word. */
static void login_field(const float box[4], float text_right, const char *text, int focused, float time_s)
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

static void login_button(float x, float y, float w, float h, float px, const char *text,
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

/* ---- Main menu ----------------------------------------------------------- */
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

/* ---- Settings ------------------------------------------------------------ */
static float settings_option_x(int column)
{
    return SETTINGS_ROW_X + (float)column * (SETTINGS_OPTION_W + SETTINGS_OPTION_GAP);
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

static void settings_option(int column, float y, const char *text, int selected, int hovered)
{
    login_button(settings_option_x(column), y, SETTINGS_OPTION_W, SETTINGS_OPTION_H, 17, text,
                 pick_color(selected, 0x3F9B57u, 0x8C8474u), pick_color(selected, 0x4FB56Au, 0xA49C8Cu), hovered, 0);
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

/* ---- Chat card ----------------------------------------------------------- */
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
