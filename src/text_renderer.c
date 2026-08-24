#include "text_renderer.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font_data.h"

#define ATLAS_W    512
#define ATLAS_H    512
#define FIRST_CHAR  32
#define NUM_CHARS   96

static stbtt_bakedchar g_chars[NUM_CHARS];
static GLuint g_texture;
static int    g_win_w = 1280, g_win_h = 720;
static float  g_char_w = 10.0f, g_char_h = 18.0f;

void text_renderer_set_win_size(int w, int h) { g_win_w = w; g_win_h = h; }

void text_renderer_init(const char *font_path, float font_size) {
    FILE *f = fopen(font_path, "rb");
    if (!f) { fprintf(stderr, "text_renderer: cannot open font: %s\n", font_path); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    unsigned char *ttf = malloc(sz);
    fread(ttf, 1, sz, f); fclose(f);

    unsigned char *bitmap = calloc(ATLAS_W * ATLAS_H, 1);
    stbtt_BakeFontBitmap(ttf, 0, font_size, bitmap, ATLAS_W, ATLAS_H, FIRST_CHAR, NUM_CHARS, g_chars);
    free(ttf);

    // Measure 'M' for monospace cell size
    float cx = 0, cy = 0;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(g_chars, ATLAS_W, ATLAS_H, 'M' - FIRST_CHAR, &cx, &cy, &q, 1);
    g_char_w = cx;
    g_char_h = font_size;

    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(bitmap);
}

void text_renderer_init_embedded(float font_size) {
    unsigned char *bitmap = calloc(ATLAS_W * ATLAS_H, 1);
    stbtt_BakeFontBitmap(embedded_font, 0, font_size, bitmap, ATLAS_W, ATLAS_H, FIRST_CHAR, NUM_CHARS, g_chars);

    float cx = 0, cy = 0;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(g_chars, ATLAS_W, ATLAS_H, 'M' - FIRST_CHAR, &cx, &cy, &q, 1);
    g_char_w = cx;
    g_char_h = font_size;

    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(bitmap);
}

void text_renderer_free(void) { glDeleteTextures(1, &g_texture); }

float text_char_width(void)  { return g_char_w; }
float text_char_height(void) { return g_char_h; }

static void set_ortho(void) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, g_win_w, g_win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
}
static void pop_ortho(void) {
    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void rect_impl(float x, float y, float w, float h,
                      float r1, float g1, float b1, float a1,
                      float r2, float g2, float b2, float a2,
                      int vertical) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    set_ortho();
    glBegin(GL_QUADS);
        if (vertical) {
            glColor4f(r1, g1, b1, a1);
            glVertex2f(x,     y);
            glVertex2f(x + w, y);
            glColor4f(r2, g2, b2, a2);
            glVertex2f(x + w, y + h);
            glVertex2f(x,     y + h);
        } else {
            glColor4f(r1, g1, b1, a1);
            glVertex2f(x,     y);
            glColor4f(r2, g2, b2, a2);
            glVertex2f(x + w, y);
            glColor4f(r2, g2, b2, a2);
            glVertex2f(x + w, y + h);
            glColor4f(r1, g1, b1, a1);
            glVertex2f(x,     y + h);
        }
    glEnd();
    pop_ortho();
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    rect_impl(x, y, w, h, r, g, b, a, r, g, b, a, 0);
}

void draw_rect_gradient(float x, float y, float w, float h,
                        float r1, float g1, float b1,
                        float r2, float g2, float b2,
                        int vertical) {
    rect_impl(x, y, w, h, r1, g1, b1, 1.0f, r2, g2, b2, 1.0f, vertical);
}

void draw_text(const char *text, float x, float y, float r, float g, float b) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_texture);
    set_ortho();
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    while (*text) {
        if ((unsigned char)*text >= FIRST_CHAR && (unsigned char)*text < FIRST_CHAR + NUM_CHARS) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(g_chars, ATLAS_W, ATLAS_H, *text - FIRST_CHAR, &x, &y, &q, 1);
            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
        }
        ++text;
    }
    glEnd();
    pop_ortho();
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
}

void text_renderer_begin(void) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_texture);
    set_ortho();
    glBegin(GL_QUADS);
}

void batch_text(const char *text, float *x, float y, float r, float g, float b) {
    glColor3f(r, g, b);
    float cx = *x;
    while (*text) {
        if ((unsigned char)*text >= FIRST_CHAR && (unsigned char)*text < FIRST_CHAR + NUM_CHARS) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(g_chars, ATLAS_W, ATLAS_H, *text - FIRST_CHAR, &cx, &y, &q, 1);
            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
        }
        ++text;
    }
    *x = cx;
}

void batch_text_len(const char *text, int len, float *x, float y, float r, float g, float b) {
    glColor3f(r, g, b);
    float cx = *x;
    const char *end = text + len;
    while (text < end) {
        if ((unsigned char)*text >= FIRST_CHAR && (unsigned char)*text < FIRST_CHAR + NUM_CHARS) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(g_chars, ATLAS_W, ATLAS_H, *text - FIRST_CHAR, &cx, &y, &q, 1);
            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
        }
        ++text;
    }
    *x = cx;
}

void text_renderer_end(void) {
    glEnd();
    pop_ortho();
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
}
