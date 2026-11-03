#include "Draw_Background.h"
#include "Draw_Common.h"
#include <GLFW/glfw3.h>
#include <math.h>



void draw_floor(void)
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

void draw_back_wall(void)
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

void draw_rafters(void)
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

void draw_cobweb(float cx, float cy, float reach, int flip)
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

void wooden_crate(float x, float y, float w, float h)
{
    outlined_box(x, y, w, h, 2, WOOD);
    render_color(WOOD_LIGHT);
    render_rect(x + 1, y + 1, w - 2, 4);
    render_color(WOOD_DARK);
    render_line(x + 3, y + 6, x + w - 3, y + h - 3, 2.2f);
    render_line(x + w - 3, y + 6, x + 3, y + h - 3, 2.2f);
    render_rect(x, y + h * 0.5f, w, 1.2f);
}

void draw_clutter(void)
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

void draw_vignette(void)
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

void name_plate(float cx, float y, float px, const char *text, unsigned ink)
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

void draw_station(int item)
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

void draw_cauldron(void)
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

void draw_spellbook(void)
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

void candle_flame(float x, float y, float time_s, int seed)
{
    const float flicker = 0.75f + 0.25f * sinf(time_s * 9.0f + (float)seed * 2.1f) *
                                         sinf(time_s * 4.3f + (float)seed);
    render_glow(x, y, 52, 42, 0xFFC964u, 0.34f * flicker);
    render_color(0xF59A2Fu);
    render_ellipse(x, y, 2.8f, 4.2f * flicker + 0.8f);
    render_color(0xFFE28Au);
    render_ellipse(x, y + 0.8f, 1.5f, 2.6f * flicker);
}
