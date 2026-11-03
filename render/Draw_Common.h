#ifndef DRAW_COMMON_H
#define DRAW_COMMON_H
#include "Render.h"
#include "Art_Style.h"

static inline float noise(int n)
{
    unsigned x = (unsigned)n * 2654435761u;
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;
    return (float)(x & 0xFFFFu) / 65535.0f;
}

static inline unsigned pick_color(int condition, unsigned if_true, unsigned if_false)
{
    if (condition)
        return if_true;
    return if_false;
}

static inline float pick_float(int condition, float if_true, float if_false)
{
    if (condition)
        return if_true;
    return if_false;
}

static inline void outlined_ellipse(float cx, float cy, float rx, float ry, unsigned fill)
{
    render_color(INK);
    render_ellipse(cx, cy, rx + 1.4f, ry + 1.4f);
    render_color(fill);
    render_ellipse(cx, cy, rx, ry);
}

static inline void outlined_box(float x, float y, float w, float h, float radius, unsigned fill)
{
    render_color(INK);
    render_round_rect(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f);
    render_color(fill);
    render_round_rect(x, y, w, h, radius);
}

static inline int inside(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
#endif
