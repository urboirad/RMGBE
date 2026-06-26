#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

void  text_renderer_init(const char *font_path, float font_size);
void  text_renderer_free(void);
void  draw_text(const char *text, float x, float y, float r, float g, float b);
float text_char_width(void);
float text_char_height(void);
void  draw_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void  text_renderer_set_win_size(int w, int h);

#endif
