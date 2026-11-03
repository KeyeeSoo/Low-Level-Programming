/*
 * C renderer for the GLFW / Cyclone DDS client.
 * Uses legacy OpenGL (glBegin, glOrtho) to match the existing client.c.
 * For image loading, put stb_image.h in this directory.
 */
#include "Render.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Third-party single-header libraries: their warnings are not ours to fix.
 * Define each implementation in exactly ONE .c file in the project. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"
#define STB_EASY_FONT_IMPLEMENTATION
#include "../stb_easy_font.h"
#pragma GCC diagnostic pop

/* GL_CLAMP_TO_EDGE is available in modern compatibility GL implementations. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

typedef struct ImageCacheEntry {
    char *filename;
    unsigned int texture_id;
    struct ImageCacheEntry *next;
} ImageCacheEntry;

/* Used by the main (OpenGL) thread only. */
static ImageCacheEntry *image_cache = NULL;

/* ---- Font atlas: baked once at start-up, so drawing text never rasterises. */
#define FONT_BAKE_PX 64.0f
#define FONT_ATLAS_SIZE 1024
#define FONT_FIRST_CHAR 32
#define FONT_CHAR_COUNT 95
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

/* This font's letters are small for their nominal height: scaling here means
 * callers can think of px as roughly the visible line height. */
#define FONT_VISUAL_SCALE 1.2f

static stbtt_packedchar font_chars[FONT_CHAR_COUNT];
static unsigned int font_texture = 0;
static float font_ascent = 0.8f; /* Baseline offset as a fraction of px. */

/* ---- Unit circle: computed once, so ellipses never call sin/cos per frame. */
#define CIRCLE_SEGMENTS 40
static float circle_x[CIRCLE_SEGMENTS + 1], circle_y[CIRCLE_SEGMENTS + 1];

static void load_font(const char *font_path)
{
    FILE *file = fopen(font_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Cannot open font '%s'; using the fallback font. "
                        "Run the client from the project folder.\n", font_path);
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *ttf = NULL;
    if (size > 0)
        ttf = (unsigned char *)malloc((size_t)size);
    unsigned char *atlas = (unsigned char *)calloc(FONT_ATLAS_SIZE, FONT_ATLAS_SIZE);
    int ok = ttf != NULL && atlas != NULL && fread(ttf, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (ok) {
        stbtt_pack_context pack;
        ok = stbtt_PackBegin(&pack, atlas, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0, 4, NULL);
        if (ok) {
            ok = stbtt_PackFontRange(&pack, ttf, 0, FONT_BAKE_PX, FONT_FIRST_CHAR,
                                     FONT_CHAR_COUNT, font_chars);
            stbtt_PackEnd(&pack);
        }
    }
    if (ok) {
        stbtt_fontinfo info;
        int ascent, descent, gap;
        if (stbtt_InitFont(&info, ttf, 0)) {
            stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
            font_ascent = (float)ascent * stbtt_ScaleForPixelHeight(&info, FONT_BAKE_PX) /
                          FONT_BAKE_PX;
        }
        glGenTextures(1, &font_texture);
        glBindTexture(GL_TEXTURE_2D, font_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        /* Mipmaps keep small UI text smooth when the 64px glyphs are minified. */
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, atlas);
    } else {
        fprintf(stderr, "Cannot read font '%s'; using the fallback font.\n", font_path);
    }
    free(ttf);
    free(atlas);
}

void render_init(const char *font_path)
{
    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        float angle = 6.28318531f * (float)i / CIRCLE_SEGMENTS;
        circle_x[i] = cosf(angle);
        circle_y[i] = sinf(angle);
    }
    /* Everything is alpha-blended, so set this once rather than per shape. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    load_font(font_path);
}

void render_color(unsigned rgb)
{
    render_color_a(rgb, 1.0f);
}

void render_color_a(unsigned rgb, float alpha)
{
    glColor4f((float)((rgb >> 16) & 255u) / 255.0f, (float)((rgb >> 8) & 255u) / 255.0f,
              (float)(rgb & 255u) / 255.0f, alpha);
}

void render_rect(float x, float y, float w, float h)
{
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y);
    glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void render_gradient(float x, float y, float w, float h, unsigned top, unsigned bottom)
{
    glBegin(GL_QUADS);
    render_color(top);
    glVertex2f(x, y); glVertex2f(x + w, y);
    render_color(bottom);
    glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void render_ellipse(float cx, float cy, float rx, float ry)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= CIRCLE_SEGMENTS; i++)
        glVertex2f(cx + rx * circle_x[i], cy + ry * circle_y[i]);
    glEnd();
}

void render_glow(float cx, float cy, float rx, float ry, unsigned rgb, float alpha)
{
    glBegin(GL_TRIANGLE_FAN);
    render_color_a(rgb, alpha);
    glVertex2f(cx, cy);
    render_color_a(rgb, 0.0f);
    for (int i = 0; i <= CIRCLE_SEGMENTS; i++)
        glVertex2f(cx + rx * circle_x[i], cy + ry * circle_y[i]);
    glEnd();
}

void render_round_rect(float x, float y, float w, float h, float radius)
{
    /* One convex fan: walk each corner's quarter-circle in turn. */
    const float corner_x[4] = {x + w - radius, x + radius, x + radius, x + w - radius};
    const float corner_y[4] = {y + h - radius, y + h - radius, y + radius, y + radius};
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + w * 0.5f, y + h * 0.5f);
    for (int corner = 0; corner < 4; corner++) {
        for (int i = 0; i <= CIRCLE_SEGMENTS / 4; i++) {
            int k = corner * CIRCLE_SEGMENTS / 4 + i;
            glVertex2f(corner_x[corner] + radius * circle_x[k],
                       corner_y[corner] + radius * circle_y[k]);
        }
    }
    glVertex2f(x + w - radius + radius * circle_x[0], y + h - radius + radius * circle_y[0]);
    glEnd();
}

void render_tri(float x0, float y0, float x1, float y1, float x2, float y2)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}

void render_quad(float x0, float y0, float x1, float y1,
                 float x2, float y2, float x3, float y3)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3);
    glEnd();
}

void render_line(float x0, float y0, float x1, float y1, float width)
{
    /* A quad, because glLineWidth above 1 is not guaranteed by every driver. */
    float dx = x1 - x0, dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length <= 0.0f)
        return;
    float nx = -dy / length * width * 0.5f, ny = dx / length * width * 0.5f;
    render_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
}

static void fallback_text(float x, float y, float px, const char *text)
{
    char buffer[16384];
    int quads = stb_easy_font_print(0, 0, (char *)text, NULL, buffer, sizeof(buffer));
    float scale = px / 11.0f;
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, quads * 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
}

void render_text(float x, float y, float px, const char *text)
{
    if (font_texture == 0) {
        fallback_text(x, y, px, text);
        return;
    }
    const float scale = px * FONT_VISUAL_SCALE / FONT_BAKE_PX;
    const float baseline = y + font_ascent * px;
    float pen_x = 0.0f, pen_y = 0.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glBegin(GL_QUADS);
    for (const char *c = text; *c != '\0'; c++) {
        int index = (int)(unsigned char)*c - FONT_FIRST_CHAR;
        if (index < 0 || index >= FONT_CHAR_COUNT)
            continue;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(font_chars, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, index,
                            &pen_x, &pen_y, &q, 0);
        glTexCoord2f(q.s0, q.t0); glVertex2f(x + q.x0 * scale, baseline + q.y0 * scale);
        glTexCoord2f(q.s1, q.t0); glVertex2f(x + q.x1 * scale, baseline + q.y0 * scale);
        glTexCoord2f(q.s1, q.t1); glVertex2f(x + q.x1 * scale, baseline + q.y1 * scale);
        glTexCoord2f(q.s0, q.t1); glVertex2f(x + q.x0 * scale, baseline + q.y1 * scale);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void render_text_bold(float x, float y, float px, const char *text)
{
    /* The font has one weight; a small offset double-strike thickens it. */
    const float offset = px * 0.018f;
    render_text(x - offset, y, px, text);
    render_text(x + offset, y, px, text);
}

float render_text_width(float px, const char *text)
{
    if (font_texture == 0)
        return (float)stb_easy_font_width((char *)text) * px / 11.0f;
    float width = 0.0f;
    for (const char *c = text; *c != '\0'; c++) {
        int index = (int)(unsigned char)*c - FONT_FIRST_CHAR;
        if (index >= 0 && index < FONT_CHAR_COUNT)
            width += font_chars[index].xadvance;
    }
    return width * px * FONT_VISUAL_SCALE / FONT_BAKE_PX;
}

void render_text_centered(float cx, float y, float px, const char *text)
{
    render_text(cx - render_text_width(px, text) * 0.5f, y, px, text);
}

void render_draw_square(float x, float y, float size,
                        float r, float g, float b, float a,
                        int filled)
{
    if (size <= 0.0f)
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);

    glBegin(filled ? GL_QUADS : GL_LINE_LOOP);
        glVertex2f(x,        y);
        glVertex2f(x,        y + size);
        glVertex2f(x + size, y + size);
        glVertex2f(x + size, y);
    glEnd();

    glPopAttrib();
}

unsigned int render_load_image(const char *filename)
{
    ImageCacheEntry *entry;
    unsigned char *pixels;
    unsigned int texture_id = 0;
    int width, height, channels;
    size_t filename_len;

    if (filename == NULL || filename[0] == '\0')
        return 0;

    /* Avoid allocating a new OpenGL texture when one is already loaded. */
    for (entry = image_cache; entry != NULL; entry = entry->next) {
        if (strcmp(entry->filename, filename) == 0)
            return entry->texture_id;
    }

    /* Keep the first image row at the top of the screen. */
    stbi_set_flip_vertically_on_load(0);
    pixels = stbi_load(filename, &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == NULL) {
        fprintf(stderr, "Cannot load image '%s': %s\n", filename,
                stbi_failure_reason());
        return 0;
    }

    glGenTextures(1, &texture_id);
    if (texture_id == 0) {
        fprintf(stderr, "OpenGL could not create a texture for '%s'\n", filename);
        stbi_image_free(pixels);
        return 0;
    }

    glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPopAttrib();
    stbi_image_free(pixels);

    entry = (ImageCacheEntry *)malloc(sizeof(*entry));
    filename_len = strlen(filename) + 1;
    if (entry != NULL) {
        entry->filename = (char *)malloc(filename_len);
        if (entry->filename != NULL) {
            memcpy(entry->filename, filename, filename_len);
            entry->texture_id = texture_id;
            entry->next = image_cache;
            image_cache = entry;
            return texture_id;
        }
        free(entry);
    }

    fprintf(stderr, "Out of memory caching image '%s'\n", filename);
    glDeleteTextures(1, &texture_id);
    return 0;
}

void render_draw_image(unsigned int texture_id,
                       float x, float y, float width, float height)
{
    if (texture_id == 0 || width <= 0.0f || height <= 0.0f)
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT |
                 GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
        /* The top row of the image appears at the top of the quad. */
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,         y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + width, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + width, y + height);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,         y + height);
    glEnd();

    glPopAttrib();
}

void render_cleanup(void)
{
    ImageCacheEntry *entry = image_cache;
    while (entry != NULL) {
        ImageCacheEntry *next = entry->next;
        glDeleteTextures(1, &entry->texture_id);
        free(entry->filename);
        free(entry);
        entry = next;
    }
    image_cache = NULL;
    if (font_texture != 0) {
        glDeleteTextures(1, &font_texture);
        font_texture = 0;
    }
}
