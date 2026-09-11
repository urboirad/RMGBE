#include "editor.h"
#include "text_renderer.h"
#include "syntax.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "colors.h"
#include <ctype.h>

#define LERP_SPEED 18.0f
#define GUTTER_W(cw) (4.0f + 4.0f * (cw) + 8.0f)

// ---- Undo/Redo ---------------------------------------------------------------

static void undo_free_stack(UndoStack *s) {
    for (int i = 0; i < s->count; i++)
        free(s->entries[i].text);
    free(s->entries);
    s->entries = NULL;
    s->count = s->cap = 0;
}

static void undo_push(UndoStack *stack, UndoType type, int pos, const char *text, int len, Editor *e) {
    if (len <= 0 || len >= UNDO_MAX_TEXT) return;
    if (stack->count == stack->cap) {
        int new_cap = stack->cap ? stack->cap * 2 : 64;
        UndoEntry *new_entries = realloc(stack->entries, new_cap * sizeof(UndoEntry));
        if (!new_entries) return;
        stack->entries = new_entries;
        stack->cap = new_cap;
    }
    UndoEntry *ue = &stack->entries[stack->count++];
    ue->type = type;
    ue->pos = pos;
    ue->len = len;
    memcpy(ue->text, text, len);
    ue->text[len] = '\0';
    ue->cursor_row = e->cursor_row;
    ue->cursor_col = e->cursor_col;
}

static void undo_clear(UndoStack *s) {
    undo_free_stack(s);
}

static void undo_clear_redo(Editor *e) {
    undo_clear(&e->redo);
}

void editor_init(Editor *e) {
    memset(e, 0, sizeof(*e));
    init_buffer(&e->gb, 4096);
    e->mode = MODE_NORMAL;
    e->smooth.vis_x = 0;
    e->smooth.vis_y = 0;
    e->line_cap = 256;
    e->line_offsets = malloc(e->line_cap * sizeof(int));
    if (e->line_offsets) e->line_offsets[0] = 0;
    e->line_count = e->line_offsets ? 1 : 0;
    e->lines_dirty = 1;
    e->undo.entries = NULL;
    e->undo.count = e->undo.cap = 0;
    e->redo.entries = NULL;
    e->redo.count = e->redo.cap = 0;
}

void editor_free(Editor *e) {
    if (e->gb.buffer) free(e->gb.buffer);
    free(e->line_offsets);
    free(e->syntax_cache);
    undo_free_stack(&e->undo);
    undo_free_stack(&e->redo);
}

static char gb_char_logical(GapBuffer *gb, int pos) {
    int phys = pos < gb->gap_start ? pos : pos + (gb->gap_end - gb->gap_start);
    if (phys < gb->total_size) return gb->buffer[phys];
    return '\n';
}

static int gb_total_logical(GapBuffer *gb) {
    return gb->gap_start + (gb->total_size - gb->gap_end);
}

static void editor_rebuild_lines(Editor *e) {
    int n = gb_total_logical(&e->gb);
    e->line_count = 0;
    int start = 0;
    while (start <= n) {
        if (e->line_count >= e->line_cap) {
            int new_cap = e->line_cap * 2;
            int *new_offsets = realloc(e->line_offsets, new_cap * sizeof(int));
            if (!new_offsets) { e->lines_dirty = 0; return; }
            e->line_offsets = new_offsets;
            e->line_cap = new_cap;
        }
        e->line_offsets[e->line_count++] = start;
        if (start == n) break;
        while (start < n && gb_char_logical(&e->gb, start) != '\n')
            start++;
        start++; // move past the newline
    }
    e->lines_dirty = 0;
    e->syntax_cache_dirty = 1;
}

static void editor_rebuild_syntax_cache(Editor *e) {
    int count = (e->line_count + SYNTAX_CACHE_INTERVAL - 1) / SYNTAX_CACHE_INTERVAL;
    if (count < 1) count = 1;
    if (count != e->syntax_cache_count) {
        SyntaxState *new_cache = realloc(e->syntax_cache, count * sizeof(SyntaxState));
        if (!new_cache) return;
        e->syntax_cache = new_cache;
        e->syntax_cache_count = count;
    }
    SyntaxState state = {0};
    GapBuffer *gb = &e->gb;
    int n = gb_total_logical(gb);
    char line[EDITOR_LINE_LEN];
    for (int li = 0; li < e->line_count; li++) {
        if (li % SYNTAX_CACHE_INTERVAL == 0) {
            int idx = li / SYNTAX_CACHE_INTERVAL;
            e->syntax_cache[idx] = state;
        }
        int lstart = e->line_offsets[li];
        int lend = (li + 1 < e->line_count) ? e->line_offsets[li + 1] : n;
        int llen = 0;
        for (int k = lstart; k < lend && k < n; k++) {
            char c = gb_char_logical(gb, k);
            if (c == '\n') break;
            if (llen < EDITOR_LINE_LEN - 1) line[llen++] = c;
        }
        line[llen] = '\0';
        Token dummy[1];
        syntax_tokenize(line, dummy, 0, &state);
    }
    e->syntax_cache_dirty = 0;
}

void editor_open_file(Editor *e, const char *path) {
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    FILE *f = fopen(path, "rb");
    if (!f) return;

    // Clear old content before loading new file
    free(e->gb.buffer);
    init_buffer(&e->gb, 4096);
    e->cursor_col = e->cursor_row = 0;
    e->scroll_y = 0;
    e->lines_dirty = 1;

    int c;
    while ((c = fgetc(f)) != EOF)
        insert_char(&e->gb, (char)c);
    fclose(f);

    // Move cursor to start
    move_cursor(&e->gb, 0);
    e->smooth.vis_x = GUTTER_W(text_char_width());
    e->smooth.vis_y = 0;
    e->smooth.x = e->smooth.y = 0;
    e->dirty = 0;
    undo_clear(&e->undo);
    undo_clear(&e->redo);
}

void editor_save_file(Editor *e) {
    if (!e->filepath[0]) return;
    FILE *f = fopen(e->filepath, "wb");
    if (!f) return;
    GapBuffer *gb = &e->gb;
    fwrite(gb->buffer, 1, gb->gap_start, f);
    fwrite(gb->buffer + gb->gap_end, 1, gb->total_size - gb->gap_end, f);
    fclose(f);
    e->dirty = 0;
}

void editor_new_file(Editor *e) {
    if (e->gb.buffer) free(e->gb.buffer);
    init_buffer(&e->gb, 4096);
    e->filepath[0] = '\0';
    e->cursor_col = 0;
    e->cursor_row = 0;
    e->scroll_y = 0;
    e->smooth.vis_x = 0;
    e->smooth.vis_y = 0;
    e->dirty = 0;
    e->selecting = 0;
    e->selection_start = 0;
    e->selection_end = 0;
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
    undo_clear(&e->undo);
    undo_clear(&e->redo);
}

void editor_save_file_as(Editor *e, const char *path) {
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    e->filepath[sizeof(e->filepath) - 1] = '\0';
    editor_save_file(e);
}

void editor_undo(Editor *e) {
    if (e->undo.count == 0) return;
    UndoEntry ue = e->undo.entries[e->undo.count - 1];
    e->undo.count--;

    if (ue.type == UNDO_INSERT) {
        // Undo insert = delete the inserted text
        move_cursor(&e->gb, ue.pos);
        for (int i = 0; i < ue.len; i++)
            delete_char(&e->gb);
    } else {
        // Undo delete = re-insert the deleted text
        move_cursor(&e->gb, ue.pos);
        for (int i = 0; i < ue.len; i++)
            insert_char(&e->gb, ue.text[i]);
    }

    // Push to redo (save current state first for redo)
    char *text_copy = malloc(ue.len + 1);
    if (!text_copy) return;
    memcpy(text_copy, ue.text, ue.len);
    text_copy[ue.len] = '\0';

    if (e->redo.count == e->redo.cap) {
        int new_cap = e->redo.cap ? e->redo.cap * 2 : 64;
        UndoEntry *new_entries = realloc(e->redo.entries, new_cap * sizeof(UndoEntry));
        if (!new_entries) { free(text_copy); return; }
        e->redo.entries = new_entries;
        e->redo.cap = new_cap;
    }
    UndoEntry *re = &e->redo.entries[e->redo.count++];
    re->type = ue.type;
    re->pos = ue.pos;
    re->len = ue.len;
    re->text[0] = '\0';
    // The text stored in redo is what undo just did (the reverse operation text)
    // For redo of an INSERT undo (which deleted text), the redo text = original insert text
    // For redo of a DELETE undo (which inserted text), the redo text = original delete text
    // In both cases, ue.text is the original operation text, which is what redo needs
    memcpy(re->text, text_copy, ue.len);
    re->text[ue.len] = '\0';
    re->cursor_row = ue.cursor_row;
    re->cursor_col = ue.cursor_col;
    free(text_copy);

    // Restore cursor position
    e->cursor_row = ue.cursor_row;
    e->cursor_col = ue.cursor_col;
    if (e->lines_dirty) editor_rebuild_lines(e);
    int undo_pos = e->line_offsets[e->cursor_row] + e->cursor_col;
    int undo_n = gb_total_logical(&e->gb);
    if (undo_pos > undo_n) undo_pos = undo_n;
    move_cursor(&e->gb, undo_pos);
    e->dirty = 1;
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
}

void editor_redo(Editor *e) {
    if (e->redo.count == 0) return;
    UndoEntry re = e->redo.entries[e->redo.count - 1];
    e->redo.count--;

    // Save current state for undo
    char *text_copy = malloc(re.len + 1);
    if (!text_copy) return;
    memcpy(text_copy, re.text, re.len);
    text_copy[re.len] = '\0';
    undo_push(&e->undo, re.type, re.pos, text_copy, re.len, e);
    free(text_copy);

    if (re.type == UNDO_INSERT) {
        // Redo insert = re-insert the text
        move_cursor(&e->gb, re.pos);
        for (int i = 0; i < re.len; i++)
            insert_char(&e->gb, re.text[i]);
    } else {
        // Redo delete = delete the text again
        move_cursor(&e->gb, re.pos);
        for (int i = 0; i < re.len; i++)
            delete_char(&e->gb);
    }

    // Restore cursor position
    e->cursor_row = re.cursor_row;
    e->cursor_col = re.cursor_col;
    if (e->lines_dirty) editor_rebuild_lines(e);
    int redo_pos = e->line_offsets[e->cursor_row] + e->cursor_col;
    int redo_n = gb_total_logical(&e->gb);
    if (redo_pos > redo_n) redo_pos = redo_n;
    move_cursor(&e->gb, redo_pos);
    e->dirty = 1;
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
}

static void editor_rebuild_lines(Editor *e);

// Return buffer offset for (row, col)
static int offset_of(Editor *e, int row, int col) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (row < 0) row = 0;
    if (row >= e->line_count) row = e->line_count - 1;
    int pos = e->line_offsets[row] + col;
    int n = gb_total_logical(&e->gb);
    if (pos > n) pos = n;
    return pos;
}

static int logical_len_of_row(Editor *e, int row) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (row < 0 || row >= e->line_count) return 0;
    int start = e->line_offsets[row];
    int end = (row + 1 < e->line_count) ? e->line_offsets[row + 1] : gb_total_logical(&e->gb);
    // Length is (end - start), minus 1 if the line ends with '\n'
    int len = end - start;
    if (len > 0 && gb_char_logical(&e->gb, end - 1) == '\n') len--;
    return len;
}

static int count_rows(Editor *e) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    return e->line_count;
}

#define EDITOR_CLIPBOARD_LEN 1024
char clipboard[EDITOR_CLIPBOARD_LEN];

// Read logical char at index i (skipping the gap)
static char logical_char_at(GapBuffer *gb, int i) {
    int bi = (i < gb->gap_start) ? i : i + (gb->gap_end - gb->gap_start);
    return gb->buffer[bi];
}

void editor_copy(Editor *e) {
    if (e->selection_start == e->selection_end) return;
    int start = e->selection_start < e->selection_end ? e->selection_start : e->selection_end;
    int end   = e->selection_start > e->selection_end ? e->selection_start : e->selection_end;
    int len = end - start;
    if (len <= 0 || len >= EDITOR_CLIPBOARD_LEN) return;
    for (int i = 0; i < len; i++)
        clipboard[i] = logical_char_at(&e->gb, start + i);
    clipboard[len] = '\0';
}

void editor_paste(Editor *e) {
    if (!clipboard[0]) return;
    undo_clear_redo(e);
    int clip_len = (int)strlen(clipboard);
    // Delete selection if any — record it as part of the undo
    if (e->selection_start != e->selection_end) {
        int start = e->selection_start < e->selection_end ? e->selection_start : e->selection_end;
        int end   = e->selection_start > e->selection_end ? e->selection_start : e->selection_end;
        int sel_len = end - start;
        // Capture selected text before deleting
        char sel_text[UNDO_MAX_TEXT];
        if (sel_len >= UNDO_MAX_TEXT) sel_len = UNDO_MAX_TEXT - 1;
        for (int i = 0; i < sel_len; i++)
            sel_text[i] = gb_char_logical(&e->gb, start + i);
        sel_text[sel_len] = '\0';
        // Build compound undo: recorded text = selected_text + clipboard
        int total = sel_len + clip_len;
        if (total >= UNDO_MAX_TEXT) total = UNDO_MAX_TEXT - 1;
        char compound[UNDO_MAX_TEXT];
        memcpy(compound, sel_text, sel_len);
        memcpy(compound + sel_len, clipboard, clip_len);
        compound[total] = '\0';
        undo_push(&e->undo, UNDO_DELETE, start, compound, total, e);
        delete_range(&e->gb, start, end);
        move_cursor(&e->gb, start);
        e->selection_start = e->selection_end = 0;
    // Recalculate row/col after deleting the selection
        int r = 0, c = 0;
        int n = e->gb.gap_start + (e->gb.total_size - e->gb.gap_end);
        for (int i = 0; i < start && i < n; i++) {
            int bi = (i < e->gb.gap_start) ? i : i + (e->gb.gap_end - e->gb.gap_start);
            if (bi < e->gb.total_size && e->gb.buffer[bi] == '\n') { r++; c = 0; } else { c++; }
        }
        e->cursor_row = r;
        e->cursor_col = c;
    } else {
        // No selection — simple insert
        undo_push(&e->undo, UNDO_INSERT, e->gb.gap_start, clipboard, clip_len, e);
    }
    // Make sure we're in insert mode so the paste actually works
    if (e->mode == MODE_VISUAL) { e->mode = MODE_NORMAL; }
    if (e->mode == MODE_NORMAL) { e->mode = MODE_INSERT; }
    for (int i = 0; clipboard[i]; i++) {
        insert_char(&e->gb, clipboard[i]);
        if (clipboard[i] == '\n') { e->cursor_row++; e->cursor_col = 0; }
        else e->cursor_col++;
    }
    float cw = text_char_width();
    float ch = text_char_height();
    e->smooth.vis_x = GUTTER_W(cw) + e->cursor_col * cw;
    e->smooth.vis_y = e->cursor_row * (ch + 2.0f);
    e->dirty = 1;
    e->lines_dirty = 1;
}

// Convert a screen pixel position to a buffer offset.
// Updates cursor_row/cursor_col to match. ex,ey is the top-left of the editor panel.

static int pixel_to_offset(Editor *e, float px, float py, float ex, float ey) {
    float cw  = text_char_width();
    float ch  = text_char_height();
    float row = ch + 2.0f;
    float gutter = GUTTER_W(cw);
    int clicked_row = (int)((py - ey + e->scroll_y) / row);
    int clicked_col = (int)((px - ex - gutter) / cw);
    if (clicked_row < 0) clicked_row = 0;
    if (clicked_col < 0) clicked_col = 0;
    int rows = count_rows(e);
    if (clicked_row >= rows) clicked_row = rows - 1;
    int maxc = logical_len_of_row(e, clicked_row);
    if (clicked_col > maxc) clicked_col = maxc;
    e->cursor_row = clicked_row;
    e->cursor_col = clicked_col;
    // Snap smooth position so ensure_cursor_visible uses correct coords
    e->smooth.vis_x = GUTTER_W(cw) + clicked_col * cw;
    e->smooth.vis_y = clicked_row * row;
    e->needs_scroll_to_cursor = 1;
    return offset_of(e, clicked_row, clicked_col);
}

void editor_mouse_press(Editor *e, float px, float py, float ex, float ey) {
    int pos = pixel_to_offset(e, px, py, ex, ey);
    move_cursor(&e->gb, pos);
    e->selection_start = pos;
    e->selection_end   = pos;
    e->selecting = 1;
}

void editor_mouse_move(Editor *e, float px, float py, float ex, float ey) {
    if (!e->selecting) return;
    e->selection_end = pixel_to_offset(e, px, py, ex, ey);
}

void editor_mouse_release(Editor *e) {
    if (e->selecting) {
        move_cursor(&e->gb, e->selection_end);
    }
    e->selecting = 0;
}

void editor_scroll(Editor *e, float yoffset) {
    float ch = text_char_height();
    float row = ch + 2.0f;
    e->scroll_y -= yoffset * 1.0f * row;
    if (e->scroll_y < 0) e->scroll_y = 0;
    e->smooth.vis_x = GUTTER_W(text_char_width()) + e->cursor_col * text_char_width();
    e->smooth.vis_y = e->cursor_row * row;
}

static void ensure_cursor_visible(Editor *e) {
    float ch = text_char_height();
    float row = ch + 2.0f;
    float cy = e->smooth.vis_y - e->scroll_y;
    float vh = e->viewport_h > 0 ? e->viewport_h : 400.0f;
    if (cy < 0) e->scroll_y -= (row - cy);
    if (cy > vh - row*2) e->scroll_y += (cy - (vh - row*2));
    if (e->scroll_y < 0) e->scroll_y = 0;
}

void editor_key(Editor *e, int key, int mods) {
    int ctrl = (mods & GLFW_MOD_CONTROL);
    int prev_row = e->cursor_row;
    int prev_col = e->cursor_col;

    // New file
    if (ctrl && key == GLFW_KEY_N) { editor_new_file(e); goto key_done; }
    // Save
    if (ctrl && key == GLFW_KEY_S) { editor_save_file(e); goto key_done; }
    // Copy & Paste
    if (ctrl && key == GLFW_KEY_C) { editor_copy(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_V) { editor_paste(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_A) { e->cursor_col = 0; move_cursor(&e->gb, offset_of(e, e->cursor_row, 0)); goto key_done; }
    // Undo & Redo
    if (ctrl && key == GLFW_KEY_Z) { editor_undo(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_Y) { editor_redo(e); goto key_done; }

    if (e->mode == MODE_INSERT) {
        int cur = e->gb.gap_start;
        if (key == GLFW_KEY_ESCAPE) { e->mode = MODE_NORMAL; goto key_done; }
        if (key == GLFW_KEY_BACKSPACE) {
            int prev_row_len = 0;
            if (e->cursor_col == 0 && e->cursor_row > 0)
                prev_row_len = logical_len_of_row(e, e->cursor_row - 1);
            // Record what's being deleted for undo
            if (cur > 0) {
                undo_clear_redo(e);
                char del = gb_char_logical(&e->gb, cur - 1);
                undo_push(&e->undo, UNDO_DELETE, cur - 1, &del, 1, e);
            }
            delete_char(&e->gb); e->dirty = 1; e->lines_dirty = 1;
            if (e->cursor_col > 0) e->cursor_col--;
            else if (e->cursor_row > 0) {
                e->cursor_row--;
                e->cursor_col = prev_row_len;
            }
            goto key_done;
        }
        if (key == GLFW_KEY_ENTER) {
            undo_clear_redo(e);
            undo_push(&e->undo, UNDO_INSERT, cur, "\n", 1, e);
            insert_char(&e->gb, '\n'); e->dirty = 1; e->lines_dirty = 1;
            e->cursor_row++; e->cursor_col = 0; goto key_done;
        }
        if (key == GLFW_KEY_LEFT)  { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, cur-1); } goto key_done; }
        if (key == GLFW_KEY_RIGHT) { e->cursor_col++; move_cursor(&e->gb, cur+1); goto key_done; }
        if (key == GLFW_KEY_UP)    { if (e->cursor_row > 0) { e->cursor_row--; int maxc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > maxc) e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
        if (key == GLFW_KEY_DOWN)  { int rows = count_rows(e); if (e->cursor_row < rows-1) { e->cursor_row++; int maxc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > maxc) e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
        goto key_done;
    }

    // Visual mode keys
    if (e->mode == MODE_VISUAL) {
        int rows = count_rows(e);
        int maxc = logical_len_of_row(e, e->cursor_row);
        if (key == GLFW_KEY_ESCAPE) { e->mode = MODE_NORMAL; goto key_done; }
        if (key == GLFW_KEY_LEFT) {
            if (e->cursor_col > 0) {
                e->cursor_col--;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            goto key_done;
        }
        if (key == GLFW_KEY_RIGHT) {
            if (e->cursor_col < maxc) {
                e->cursor_col++;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            goto key_done;
        }
        if (key == GLFW_KEY_UP) {
            if (e->cursor_row > 0) {
                e->cursor_row--;
                int mc = logical_len_of_row(e, e->cursor_row);
                if (e->cursor_col > mc) e->cursor_col = mc;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            goto key_done;
        }
        if (key == GLFW_KEY_DOWN) {
            if (e->cursor_row < rows - 1) {
                e->cursor_row++;
                int mc = logical_len_of_row(e, e->cursor_row);
                if (e->cursor_col > mc) e->cursor_col = mc;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            goto key_done;
        }
        goto key_done;
    }

    // Normal mode keys
    if (e->mode == MODE_NORMAL) {
        int rows = count_rows(e);
        int maxc = logical_len_of_row(e, e->cursor_row);
        if (key == GLFW_KEY_I || key == GLFW_KEY_A) { e->mode = MODE_INSERT; e->key_handled = 1; goto key_done; }
        if (key == GLFW_KEY_V) { 
            e->mode = MODE_VISUAL; 
            e->selection_start = offset_of(e, e->cursor_row, e->cursor_col);
            e->selection_end = e->selection_start;
            
            goto key_done; 
        }
        if (key == GLFW_KEY_H) { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, e->gb.gap_start - 1); } goto key_done; }
        if (key == GLFW_KEY_L) { if (e->cursor_col < maxc) { e->cursor_col++; move_cursor(&e->gb, e->gb.gap_start + 1); } goto key_done; }
        if (key == GLFW_KEY_K) { if (e->cursor_row > 0) { e->cursor_row--; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
        if (key == GLFW_KEY_J) { if (e->cursor_row < rows - 1) { e->cursor_row++; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
        if (key == GLFW_KEY_0) { e->cursor_col = 0; move_cursor(&e->gb, offset_of(e, e->cursor_row, 0)); goto key_done; }
        if (key == GLFW_KEY_4 && (mods & GLFW_MOD_SHIFT)) { e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, maxc)); goto key_done; }
        if (key == GLFW_KEY_X) {
            GapBuffer *gb = &e->gb;
            int n = gb->gap_start + (gb->total_size - gb->gap_end);
            if (gb->gap_start < n) {
                undo_clear_redo(e);
                char del = gb_char_logical(gb, gb->gap_start);
                undo_push(&e->undo, UNDO_DELETE, gb->gap_start, &del, 1, e);
                gb->gap_end++;
                e->dirty = 1;
                e->lines_dirty = 1;
            }
            goto key_done;
        }
        if (key == GLFW_KEY_O) {
            int eol = offset_of(e, e->cursor_row, logical_len_of_row(e, e->cursor_row));
            undo_clear_redo(e);
            undo_push(&e->undo, UNDO_INSERT, eol, "\n", 1, e);
            move_cursor(&e->gb, eol);
            insert_char(&e->gb, '\n'); e->dirty = 1; e->lines_dirty = 1;
            e->cursor_row++; e->cursor_col = 0;
            e->mode = MODE_INSERT;
            e->key_handled = 1;
            goto key_done;
        }
        if (key == GLFW_KEY_LEFT)  { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, e->gb.gap_start - 1); } goto key_done; }
        if (key == GLFW_KEY_RIGHT) { if (e->cursor_col < maxc) { e->cursor_col++; move_cursor(&e->gb, e->gb.gap_start + 1); } goto key_done; }
        if (key == GLFW_KEY_UP)    { if (e->cursor_row > 0) { e->cursor_row--; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
        if (key == GLFW_KEY_DOWN)  { if (e->cursor_row < rows - 1) { e->cursor_row++; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } goto key_done; }
    }

key_done:
    if (e->cursor_row != prev_row || e->cursor_col != prev_col)
        e->needs_scroll_to_cursor = 1;
}

void editor_char(Editor *e, unsigned int cp) {
    if (e->key_handled) { e->key_handled = 0; return; }
    if (e->mode != MODE_INSERT) return;
    if (cp < 32 || cp > 126) return;
    undo_clear_redo(e);
    char ch = (char)cp;
    undo_push(&e->undo, UNDO_INSERT, e->gb.gap_start, &ch, 1, e);
    insert_char(&e->gb, ch);
    e->cursor_col++;
    e->dirty = 1;
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
}

void editor_update(Editor *e, float dt) {
    float cw = text_char_width();
    float ch = text_char_height();
    float target_x = GUTTER_W(cw) + e->cursor_col * cw;
    float target_y = e->cursor_row * (ch + 2.0f);
    float t = 1.0f - expf(-LERP_SPEED * dt);
    e->smooth.vis_x += (target_x - e->smooth.vis_x) * t;
    e->smooth.vis_y += (target_y - e->smooth.vis_y) * t;

    if (e->needs_scroll_to_cursor) {
        e->smooth.vis_x = target_x;
        e->smooth.vis_y = target_y;
        ensure_cursor_visible(e);
        e->needs_scroll_to_cursor = 0;
    }
}

void editor_render(Editor *e, float x, float y, float w, float h) {
    if (g_theme.bg_gradient == THEME_GRADIENT_VERTICAL)
        draw_rect_gradient(x, y, w, h,
                           COLOR_ED_BACKGROUND,
                           g_theme.background_end.r, g_theme.background_end.g, g_theme.background_end.b, 1);
    else if (g_theme.bg_gradient == THEME_GRADIENT_HORIZONTAL)
        draw_rect_gradient(x, y, w, h,
                           COLOR_ED_BACKGROUND,
                           g_theme.background_end.r, g_theme.background_end.g, g_theme.background_end.b, 0);
    else
        draw_rect(x, y, w, h, COLOR_ED_BACKGROUND, 1.0f);

    float cw  = text_char_width();
    float ch  = text_char_height();
    float row = ch + 2.0f;

    // Cursor auto-scroll is handled in editor_update, not here
    e->viewport_h = h;

    float gutter = GUTTER_W(cw);
    float cy = y + e->cursor_row * row - e->scroll_y + (row - ch) * 0.5f;

    // Render text line by line
    GapBuffer *gb = &e->gb;
    int n = gb_total_logical(gb);
    char line[EDITOR_LINE_LEN];

    int sel_start = e->selection_start < e->selection_end ? e->selection_start : e->selection_end;
    int sel_end   = e->selection_start > e->selection_end ? e->selection_start : e->selection_end;

    if (e->lines_dirty) editor_rebuild_lines(e);
    if (e->syntax_cache_dirty) editor_rebuild_syntax_cache(e);

    // Build cursor line and draw cursor before text batch
    float cursor_x = x + gutter + e->cursor_col * cw;
    if (e->mode == MODE_INSERT)
        draw_rect(cursor_x, cy, 2.0f, ch, COLOR_CURSOR_HIGHLIGHT, 0.9f);
    else
        draw_rect(cursor_x, cy, cw, ch, COLOR_CURSOR_HIGHLIGHT, 0.45f);

    // Find the first visible line
    int first_vis = 0;
    if (row > 0) {
        first_vis = (int)(e->scroll_y / row);
        if (first_vis < 0) first_vis = 0;
        if (first_vis >= e->line_count) first_vis = e->line_count - 1;
    }

    float ty  = y - e->scroll_y + first_vis * row;

    // Restore syntax state from nearest cache point
    SyntaxState syntax_state = {0};
    int cache_idx = first_vis / SYNTAX_CACHE_INTERVAL;
    if (cache_idx < e->syntax_cache_count && e->syntax_cache)
        syntax_state = e->syntax_cache[cache_idx];
    int cache_line = cache_idx * SYNTAX_CACHE_INTERVAL;

    // Advance state from cache point to first visible line
    for (int li = cache_line; li < first_vis && li < e->line_count; li++) {
        int lstart = e->line_offsets[li];
        int lend = (li + 1 < e->line_count) ? e->line_offsets[li + 1] : n;
        int llen = 0;
        for (int k = lstart; k < lend && k < n; k++) {
            char c = gb_char_logical(gb, k);
            if (c == '\n') break;
            if (llen < EDITOR_LINE_LEN - 1) line[llen++] = c;
        }
        line[llen] = '\0';
        Token dummy[1];
        syntax_tokenize(line, dummy, 0, &syntax_state);
    }

    // Batch all text rendering in a single GL pass
    text_renderer_begin();
    for (int li = first_vis; li < e->line_count; li++) {
        if (ty >= y + h) break;

        int lstart = e->line_offsets[li];
        int lend = (li + 1 < e->line_count) ? e->line_offsets[li + 1] : n;
        int llen = 0;
        for (int k = lstart; k < lend && k < n; k++) {
            char c = gb_char_logical(gb, k);
            if (c == '\n') break;
            if (llen < EDITOR_LINE_LEN - 1) line[llen++] = c;
        }
        line[llen] = '\0';

        if (ty + row >= y) {
            // Highlight selected text on this line
            if (sel_start != sel_end) {
                int hl_s = sel_start - lstart;
                int hl_e = sel_end   - lstart;
                if (hl_s < llen && hl_e > 0) {
                    if (hl_s < 0) hl_s = 0;
                    if (hl_e > llen) hl_e = llen;
                    text_renderer_end();
                    draw_rect(x + gutter + hl_s * cw, ty + (row - ch) * 0.5f,
                              (hl_e - hl_s) * cw, ch,
                              0, 188/255.0f, 212/255.0f, 0.3f);
                    text_renderer_begin();
                }
            }
            // Draw syntax-highlighted text inline in the batch
            Token tokens[512];
            int nt = syntax_tokenize(line, tokens, 512, &syntax_state);
            float tx = x + gutter;
            for (int ti = 0; ti < nt; ti++) {
                Token *t = &tokens[ti];
                float cr, cg, cb;
                token_color(t->type, &cr, &cg, &cb);
                batch_text_len(t->start, t->len, &tx, ty + ch * 0.85f, cr, cg, cb);
            }
            // Line number
            char lnum[16]; snprintf(lnum, sizeof(lnum), "%4d", li + 1);
            float lnx = x + 4;
            batch_text(lnum, &lnx, ty + ch * 0.85f, COLOR_TEXT);
        }
        ty += row;
    }
    text_renderer_end();

    // Status bar
    const char *mode_str = e->mode == MODE_INSERT ? "INSERT" :
                           e->mode == MODE_VISUAL ? "VISUAL" : "NORMAL";
    char status[640];
    snprintf(status, sizeof(status), " %s  %s  %d:%d",
             mode_str, e->filepath[0] ? e->filepath : "[No File]",
             e->cursor_row + 1, e->cursor_col + 1);
    draw_rect(x, y + h - 20.0f, w, 20.0f, COLOR_TOOLBAR, 1.0f);
    draw_text(status, x + 4, y + h - 4.0f, COLOR_TEXT);
}