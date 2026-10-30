/*
 * C renderer for the GLFW / Cyclone DDS client.
 * Uses legacy OpenGL (glBegin, glOrtho) to match the existing client.c.
 * For image loading, put stb_image.h in this directory.
 */
#include "Render.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Define this in exactly ONE .c file in the project. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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
}
