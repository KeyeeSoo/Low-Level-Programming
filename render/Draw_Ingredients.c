#include "Draw_Ingredients.h"
#include "Draw_Common.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include "Art.h"
#include "Art_Style.h"


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

void draw_ingredient_sprite(int item)
{
    if (item >= 0 && item < INGREDIENT_COUNT)
        ingredient_painters[item]();
}
