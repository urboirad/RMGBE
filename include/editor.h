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
    int selection_start; // Start of the selection
    int selection_end;   // End of the selection
    int selecting;      // Flag to indicate if selection is active
    int cursor_pos;      // buffer offset for cursor
    int key_handled;     // suppress next char event after command key
} Editor;

void editor_init(Editor *e);
void editor_free(Editor *e);
void editor_open_file(Editor *e, const char *path);
void editor_save_file(Editor *e);
void editor_key(Editor *e, int key, int mods);
void editor_char(Editor *e, unsigned int codepoint);
void editor_update(Editor *e, float dt);
void editor_render(Editor *e, float x, float y, float w, float h);
void editor_mouse_press(Editor *e, float px, float py, float ex, float ey);
void editor_mouse_move(Editor *e, float px, float py, float ex, float ey);
void editor_mouse_release(Editor *e);
void editor_scroll(Editor *e, float yoffset);

#endif
