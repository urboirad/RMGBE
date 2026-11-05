#ifndef EDITOR_H
#define EDITOR_H

#include "gap_buffer.h"
#include "editor_state.h"
#include "syntax.h"
#include "undo.h"

#define EDITOR_MAX_LINES 4096
#define EDITOR_LINE_LEN  1024
#define EDITOR_MAX_LOAD  (256 * 1024 * 1024)

#define SEARCH_MAX_LEN 256
#define SEARCH_MAX_MATCHES 4096

/* Per-line syntax cache.  Token offsets refer to the expanded (tabs -> spaces)
 * display string in `text`; rendering therefore never has to re-tokenize a
 * line unless the buffer or the theme changed. */
typedef struct {
    int       start;
    int       len;
    TokenType type;
} CachedToken;

typedef struct {
    char         *text;
    int           text_len;
    int           text_cap;
    CachedToken  *tokens;
    int           token_count;
    int           token_cap;
    SyntaxState   state_in;
    SyntaxState   state_out;
    int           valid;
} LineSyntax;

typedef struct {
    GapBuffer      gb;
    EditorMode     mode;
    int            cursor_col;    // byte column within the line
    int            cursor_row;    // zero-based row
    int            visual_start;  // where visual selection started
    SmoothedCursor smooth;
    float          scroll_y;      // how far we've scrolled (in pixels)
    char           filepath[512];
    int            dirty;         // set when buffer has unsaved changes
    int            use_crlf;      // preserve CRLF line endings on save
    int            selection_start; // byte offset, start of selection
    int            selection_end;   // byte offset, end (exclusive)
    int            selecting;       // true while dragging a selection
    int            cursor_pos;      // buffer offset for the cursor
    int            key_handled;     // set to skip the next char callback
    int           *line_offsets;    // cached start offset of each line
    int            line_count;
    int            line_cap;
    int            lines_dirty;     // set when line_offsets needs rebuilding
    LineSyntax    *line_syntax;     // per-line highlight cache
    int            line_syntax_cap;
    int            line_syntax_count;
    float          viewport_h;      // last known viewport height
    int            needs_scroll_to_cursor;
    UndoStack      undo;
    UndoStack      redo;
    // Search state
    int   search_active;
    char  search_query[SEARCH_MAX_LEN];
    int   search_len;
    int   search_cursor;
    int   search_match_count;
    int   search_match_cur;
    int   search_matches[SEARCH_MAX_MATCHES];
    int   search_prev_len;
    // Hot reload
    double file_mtime;
    int    external_change;
    float  saved_flash;
    // Status line message (save/load errors, etc.)
    char   status_msg[256];
    float  status_flash;
} Editor;

void editor_init(Editor *e);
void editor_free(Editor *e);
int  editor_open_file(Editor *e, const char *path);
int  editor_save_file(Editor *e);
int  editor_save_file_as(Editor *e, const char *path);
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
int  editor_animation_active(Editor *e);
void editor_check_external_change(Editor *e);
void editor_reload_file(Editor *e);
void editor_dismiss_external_change(Editor *e);

#endif
