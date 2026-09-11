#ifndef EDITOR_H
#define EDITOR_H

#include "gap_buffer.h"
#include "editor_state.h"

#define EDITOR_MAX_LINES 4096
#define EDITOR_LINE_LEN  1024
#define SYNTAX_CACHE_INTERVAL 64

#define UNDO_MAX_TEXT 4096
#define SEARCH_MAX_LEN 256
#define SEARCH_MAX_MATCHES 4096

typedef enum {
    UNDO_INSERT,
    UNDO_DELETE,
} UndoType;

typedef struct {
    UndoType type;
    int      pos;       // gap_start before the operation (buffer offset)
    int      len;       // length of text involved
    char     text[UNDO_MAX_TEXT];
    int      cursor_row;
    int      cursor_col;
} UndoEntry;

typedef struct {
    UndoEntry *entries;
    int        count;
    int        cap;
} UndoStack;

typedef struct {
    GapBuffer      gb;
    EditorMode     mode;
    int            cursor_col;    // column the cursor is on
    int            cursor_row;    // row the cursor is on
    int            visual_start;  // where visual selection started
    SmoothedCursor smooth;
    float          scroll_y;      // how far we've scrolled (in pixels)
    char           filepath[512];
    int            dirty;         // set when buffer has unsaved changes
    int selection_start; // start of the current selection
    int selection_end;   // end of the current selection
    int selecting;       // true while the user is dragging a selection
    int cursor_pos;      // buffer offset for the cursor
    int key_handled;     // set to skip the next char callback (for command keys)
    int *line_offsets;   // cached start offset of each line
    int  line_count;
    int  line_cap;
    int  lines_dirty;    // set when line_offsets needs rebuilding
    struct SyntaxState *syntax_cache;  // cached syntax states at intervals
    int  syntax_cache_count;
    int  syntax_cache_dirty;
    float viewport_h;  // last known viewport height (for cursor auto-scroll)
    int needs_scroll_to_cursor;  // set by key/char/click, cleared after scroll
    UndoStack undo;
    UndoStack redo;
    // Search state
    int   search_active;
    char  search_query[SEARCH_MAX_LEN];
    int   search_len;
    int   search_cursor;     // cursor position within search_query
    int   search_match_count;
    int   search_match_cur;  // index of currently focused match
    int   search_matches[SEARCH_MAX_MATCHES]; // buffer offsets of matches
    int   search_prev_len;   // tracks query length to avoid redundant re-search
} Editor;

void editor_init(Editor *e);
void editor_free(Editor *e);
void editor_open_file(Editor *e, const char *path);
void editor_save_file(Editor *e);
void editor_save_file_as(Editor *e, const char *path);
void editor_new_file(Editor *e);
void editor_key(Editor *e, int key, int mods);
void editor_char(Editor *e, unsigned int codepoint);
void editor_update(Editor *e, float dt);
void editor_render(Editor *e, float x, float y, float w, float h);
void editor_mouse_press(Editor *e, float px, float py, float ex, float ey);
void editor_mouse_move(Editor *e, float px, float py, float ex, float ey);
void editor_mouse_release(Editor *e);
void editor_scroll(Editor *e, float yoffset);
void editor_undo(Editor *e);
void editor_redo(Editor *e);

#endif
