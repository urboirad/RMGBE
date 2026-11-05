#include "theme.h"
#include "text_renderer.h"

/* Tests exercise editor logic without creating a window, so the renderer is
 * replaced by harmless stubs here. */
Theme g_theme;

void draw_rect(float x, float y, float w, float h,
               float r, float g, float b, float a) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a;
}

void draw_rect_gradient(float x, float y, float w, float h,
                        float r1, float g1, float b1,
                        float r2, float g2, float b2, int vertical) {
    (void)x; (void)y; (void)w; (void)h;
    (void)r1; (void)g1; (void)b1; (void)r2; (void)g2; (void)b2;
    (void)vertical;
}

void draw_text(const char *text, float x, float y,
               float r, float g, float b) {
    (void)text; (void)x; (void)y; (void)r; (void)g; (void)b;
}

void text_renderer_begin(void) {}
void text_renderer_end(void) {}

void batch_text(const char *text, float *x, float y,
                float r, float g, float b) {
    (void)text; (void)x; (void)y; (void)r; (void)g; (void)b;
}

void batch_text_len(const char *text, int len, float *x, float y,
                    float r, float g, float b) {
    (void)text; (void)len; (void)x; (void)y; (void)r; (void)g; (void)b;
}

float text_char_width(void)  { return 8.0f; }
float text_char_height(void) { return 16.0f; }

float text_measure_len(const char *text, int len) {
    (void)text; (void)len;
    return 0.0f;
}
