#ifndef EDITOR_H
#define EDITOR_H

#include "gap_buffer.h"
#include "editor_state.h"

#define EDITOR_MAX_LINES 4096
#define EDITOR_LINE_LEN  1024

typedef struct {
    GapBuffer      gb;
    EditorMode     mode;
    int            cursor_col;    // logical col
    int            cursor_row;    // logical row
    int            visual_start;  // buffer offset for visual mode
    SmoothedCursor smooth;
    float          scroll_y;      // pixel scroll
    char           filepath[512];
    int            dirty;
} Editor;

void editor_init(Editor *e);
void editor_free(Editor *e);
void editor_open_file(Editor *e, const char *path);
void editor_save_file(Editor *e);
void editor_key(Editor *e, int key, int mods);
void editor_char(Editor *e, unsigned int codepoint);
void editor_update(Editor *e, float dt);  // lerp cursor
void editor_render(Editor *e, float x, float y, float w, float h);

#endif
