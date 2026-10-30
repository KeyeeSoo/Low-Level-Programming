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

#endif /* RENDER_H */
