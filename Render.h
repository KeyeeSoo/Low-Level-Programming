#ifndef RENDER_H
#define RENDER_H

/*
 * Rendering helpers for the existing GLFW / OpenGL client, written in C.
 * Call these functions only while a GLFW OpenGL context is current.
 * The client's glOrtho(0, WIDTH, HEIGHT, 0, -1, 1) makes (0, 0) top-left.
 */

/* filled = 0 for an outline; nonzero for a filled square. */
void render_draw_square(float x, float y, float size,
                        float r, float g, float b, float a,
                        int filled);

/*
 * Load a PNG, JPEG or other stb_image-supported file.
 * Returns 0 on failure, otherwise an OpenGL texture ID.
 * Repeated calls for the same filename return the cached texture.
 * Requires a current OpenGL context.
 */
unsigned int render_load_image(const char *filename);

/* Draw a previously loaded texture. Position and dimensions are in pixels. */
void render_draw_image(unsigned int texture_id,
                       float x, float y, float width, float height);

/* Free cached textures before destroying the GLFW window. */
void render_cleanup(void);

/* ---- Vector-style drawing primitives used by Art.c and the HUD ----------
 * Shapes use the current colour set by render_color(), so a run of same-coloured
 * shapes changes no GL state. All of these may be compiled into display lists. */

/* Enable blending and bake the TTF into a texture atlas. Falls back to
 * stb_easy_font (with a warning) when the font file cannot be read. */
void render_init(const char *font_path);

/* Colours are 0xRRGGBB. */
void render_color(unsigned rgb);
void render_color_a(unsigned rgb, float alpha);

void render_rect(float x, float y, float w, float h);
void render_gradient(float x, float y, float w, float h, unsigned top, unsigned bottom);
void render_round_rect(float x, float y, float w, float h, float radius);
void render_ellipse(float cx, float cy, float rx, float ry);
/* Radial falloff from alpha at the centre to fully transparent at the edge. */
void render_glow(float cx, float cy, float rx, float ry, unsigned rgb, float alpha);
void render_tri(float x0, float y0, float x1, float y1, float x2, float y2);
void render_quad(float x0, float y0, float x1, float y1,
                 float x2, float y2, float x3, float y3);
void render_line(float x0, float y0, float x1, float y1, float width);

/* (x, y) is the top-left of the line; px is the font size in canvas pixels. */
void render_text(float x, float y, float px, const char *text);
void render_text_bold(float x, float y, float px, const char *text);
void render_text_centered(float cx, float y, float px, const char *text);
float render_text_width(float px, const char *text);

#endif /* RENDER_H */
