#include "editor.h"
#include "text_renderer.h"
#include "text.h"
#include "syntax.h"
#include "file_io.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "colors.h"
#include <ctype.h>
#include <sys/stat.h>

#define LERP_SPEED 18.0f
#define GUTTER_W(cw) (4.0f + 4.0f * (cw) + 8.0f)
#define LINE_MAX_TOKENS 4096
#define EDITOR_CLIPBOARD_LEN (1024 * 1024)

static char gb_char_logical(GapBuffer *gb, int pos);
static int gb_total_logical(GapBuffer *gb);
static void editor_rebuild_lines(Editor *e);
static int offset_of(Editor *e, int row, int col);
static int row_for_offset(Editor *e, int pos);
static int logical_len_of_row(Editor *e, int row);
static double get_file_mtime(const char *path);
static void editor_invalidate_syntax(Editor *e, int from_row);

// ---- Small helpers -----------------------------------------------------------

static void editor_set_status(Editor *e, const char *msg) {
    if (!e) return;
    snprintf(e->status_msg, sizeof(e->status_msg), "%s", msg ? msg : "");
    e->status_flash = msg && msg[0] ? 4.0f : 0.0f;
}

static int gb_decode_cp(GapBuffer *gb, int pos, int limit, unsigned int *cp) {
    if (pos < 0 || pos >= limit) { if (cp) *cp = 0xFFFDu; return 0; }
    char buf[4];
    int avail = 0;
    for (int i = 0; i < 4 && pos + i < limit; i++) {
        buf[i] = gb_char_logical(gb, pos + i);
        avail++;
    }
    int n = text_utf8_decode(buf, avail, cp);
    return n > 0 ? n : 1;
}

static int char_len_at(Editor *e, int pos) {
    unsigned int cp;
    int n = gb_decode_cp(&e->gb, pos, gb_total_logical(&e->gb), &cp);
    return n > 0 ? n : 1;
}

static int editor_selection_bounds(Editor *e, int *start, int *end) {
    if (!e || e->selection_start == e->selection_end) return 0;
    if (e->selection_start < e->selection_end) {
        *start = e->selection_start;
        *end   = e->selection_end;
    } else {
        *start = e->selection_end;
        *end   = e->selection_start;
    }
    int n = gb_total_logical(&e->gb);
    if (*start < 0) *start = 0;
    if (*end > n) *end = n;
    return *start < *end;
}

static void editor_cursor_from_offset(Editor *e, int pos) {
    int n = gb_total_logical(&e->gb);
    if (pos < 0) pos = 0;
    if (pos > n) pos = n;
    int row = row_for_offset(e, pos);
    e->cursor_row = row;
    int col = pos - e->line_offsets[row];
    int maxc = logical_len_of_row(e, row);
    if (col > maxc) col = maxc;
    if (col < 0) col = 0;
    e->cursor_col = col;
}

// Display column (tabs and multibyte characters accounted for) for a byte
// column within `row`.
static int display_col_for_byte_col(Editor *e, int row, int byte_col) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (row < 0 || row >= e->line_count) return 0;
    int lstart = e->line_offsets[row];
    int lend = (row + 1 < e->line_count) ? e->line_offsets[row + 1]
                                         : gb_total_logical(&e->gb);
    int content_end = lstart;
    while (content_end < lend && gb_char_logical(&e->gb, content_end) != '\n')
        content_end++;
    int target = lstart + (byte_col < 0 ? 0 : byte_col);
    if (target > content_end) target = content_end;

    int col = 0, pos = lstart;
    while (pos < target) {
        unsigned int cp;
        int n = gb_decode_cp(&e->gb, pos, content_end, &cp);
        if (n <= 0) n = 1;
        col += text_codepoint_cells(cp, col, TEXT_TAB_WIDTH);
        pos += n;
    }
    return col;
}

static int byte_col_for_display_col(Editor *e, int row, int target_col) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (row < 0 || row >= e->line_count) return 0;
    int lstart = e->line_offsets[row];
    int lend = (row + 1 < e->line_count) ? e->line_offsets[row + 1]
                                         : gb_total_logical(&e->gb);
    int content_end = lstart;
    while (content_end < lend && gb_char_logical(&e->gb, content_end) != '\n')
        content_end++;

    int col = 0, pos = lstart;
    while (pos < content_end) {
        unsigned int cp;
        int n = gb_decode_cp(&e->gb, pos, content_end, &cp);
        if (n <= 0) n = 1;
        int cells = text_codepoint_cells(cp, col, TEXT_TAB_WIDTH);
        if (col + cells > target_col) break;
        col += cells;
        pos += n;
        if (col >= target_col) break;
    }
    return pos - lstart;
}

static int cursor_display_col(Editor *e) {
    return display_col_for_byte_col(e, e->cursor_row, e->cursor_col);
}

// ---- Search ------------------------------------------------------------------

static void search_find_all(Editor *e) {
    e->search_match_count = 0;
    e->search_match_cur = -1;
    if (e->search_len <= 0) return;
    int n = gb_total_logical(&e->gb);
    if (e->search_len > n) return;

    for (int i = 0; i <= n - e->search_len; i++) {
        int match = 1;
        for (int j = 0; j < e->search_len; j++) {
            if (gb_char_logical(&e->gb, i + j) != e->search_query[j]) { match = 0; break; }
        }
        if (match) {
            if (e->search_match_count < SEARCH_MAX_MATCHES)
                e->search_matches[e->search_match_count++] = i;
        }
    }
    int cur_pos = offset_of(e, e->cursor_row, e->cursor_col);
    for (int i = 0; i < e->search_match_count; i++) {
        if (e->search_matches[i] >= cur_pos) { e->search_match_cur = i; return; }
    }
    if (e->search_match_count > 0) e->search_match_cur = 0;
}

static void search_goto(Editor *e, int match_idx) {
    if (match_idx < 0 || match_idx >= e->search_match_count) return;
    e->search_match_cur = match_idx;
    int pos = e->search_matches[match_idx];
    e->lines_dirty = 1;
    editor_cursor_from_offset(e, pos);
    move_cursor(&e->gb, pos);
    e->needs_scroll_to_cursor = 1;
}

static void search_next(Editor *e) {
    if (e->search_match_count == 0) return;
    e->search_match_cur = (e->search_match_cur + 1) % e->search_match_count;
    search_goto(e, e->search_match_cur);
}

static void search_prev(Editor *e) {
    if (e->search_match_count == 0) return;
    e->search_match_cur = (e->search_match_cur - 1 + e->search_match_count) %
                          e->search_match_count;
    search_goto(e, e->search_match_cur);
}

static void search_open(Editor *e) {
    e->search_active = 1;
    e->search_len = 0;
    e->search_query[0] = '\0';
    e->search_cursor = 0;
    e->search_match_count = 0;
    e->search_match_cur = -1;
    e->search_prev_len = 0;
}

static void search_close(Editor *e) {
    e->search_active = 0;
    e->search_match_count = 0;
    e->search_match_cur = -1;
}

// ---- Per-line syntax cache ---------------------------------------------------

static void line_syntax_free(LineSyntax *ls) {
    if (!ls) return;
    free(ls->text);
    free(ls->tokens);
    memset(ls, 0, sizeof(*ls));
}

static void ensure_line_syntax_capacity(Editor *e, int count) {
    if (count <= e->line_syntax_cap) return;
    int new_cap = e->line_syntax_cap ? e->line_syntax_cap : 256;
    while (new_cap < count) new_cap *= 2;
    LineSyntax *p = realloc(e->line_syntax, (size_t)new_cap * sizeof(LineSyntax));
    if (!p) return;
    memset(p + e->line_syntax_cap, 0,
           (size_t)(new_cap - e->line_syntax_cap) * sizeof(LineSyntax));
    e->line_syntax = p;
    e->line_syntax_cap = new_cap;
}

static void editor_invalidate_syntax(Editor *e, int from_row) {
    if (!e) return;
    if (from_row < 0) from_row = 0;
    for (int i = from_row; i < e->line_syntax_cap; i++)
        e->line_syntax[i].valid = 0;
}

static int ls_append(LineSyntax *ls, const char *bytes, int n) {
    if (n < 0) return 0;
    if (ls->text_len + n + 1 > ls->text_cap) {
        int new_cap = ls->text_cap ? ls->text_cap : 256;
        while (new_cap < ls->text_len + n + 1) {
            if (new_cap > (1 << 30)) return 0;
            new_cap *= 2;
        }
        char *p = realloc(ls->text, (size_t)new_cap);
        if (!p) return 0;
        ls->text = p;
        ls->text_cap = new_cap;
    }
    if (n > 0) memcpy(ls->text + ls->text_len, bytes, (size_t)n);
    ls->text_len += n;
    ls->text[ls->text_len] = '\0';
    return 1;
}

static int editor_build_line_syntax(Editor *e, int row, SyntaxState state) {
    if (!e || row < 0 || row >= e->line_count || row >= e->line_syntax_cap)
        return 0;
    LineSyntax *ls = &e->line_syntax[row];
    ls->text_len = 0;
    ls->valid = 0;
    if (!ls_append(ls, "", 0)) return 0;

    int lstart = e->line_offsets[row];
    int lend = (row + 1 < e->line_count) ? e->line_offsets[row + 1]
                                         : gb_total_logical(&e->gb);
    int display_col = 0;
    int pos = lstart;
    while (pos < lend) {
        unsigned int cp;
        int n = gb_decode_cp(&e->gb, pos, lend, &cp);
        if (n <= 0) n = 1;
        if (cp == '\n') break;
        if (cp == '\t') {
            int cells = text_codepoint_cells('\t', display_col, TEXT_TAB_WIDTH);
            for (int k = 0; k < cells; k++)
                if (!ls_append(ls, " ", 1)) return 0;
            display_col += cells;
        } else {
            char bytes[4];
            int out = text_utf8_encode(cp, bytes);
            if (!ls_append(ls, bytes, out)) return 0;
            display_col += text_codepoint_cells(cp, display_col, TEXT_TAB_WIDTH);
        }
        pos += n;
    }

    int max_tokens = ls->text_len + 1;
    if (max_tokens > LINE_MAX_TOKENS) max_tokens = LINE_MAX_TOKENS;
    if (max_tokens < 1) max_tokens = 1;
    Token *tmp = malloc((size_t)max_tokens * sizeof(Token));
    if (!tmp) return 0;
    Token *out = tmp;
    SyntaxState state_in = state;
    int nt = syntax_tokenize(ls->text, out, max_tokens, &state);
    if (nt < 0) nt = 0;
    if (nt > max_tokens) nt = max_tokens;

    if (nt > ls->token_cap) {
        CachedToken *p = realloc(ls->tokens, (size_t)nt * sizeof(CachedToken));
        if (p) { ls->tokens = p; ls->token_cap = nt; }
        else nt = 0;
    }
    ls->token_count = nt;
    for (int i = 0; i < nt; i++) {
        ls->tokens[i].start = (int)(out[i].start - ls->text);
        ls->tokens[i].len = out[i].len;
        ls->tokens[i].type = out[i].type;
    }
    free(tmp);
    ls->state_in = state_in;
    ls->state_out = state;
    ls->valid = 1;
    return 1;
}

static void ensure_syntax_to_visible(Editor *e, int first_vis, int last_vis) {
    if (!e || e->line_count <= 0) return;
    if (first_vis < 0) first_vis = 0;
    if (first_vis >= e->line_count) first_vis = e->line_count - 1;
    if (last_vis >= e->line_count) last_vis = e->line_count - 1;
    if (last_vis < first_vis) last_vis = first_vis;

    int base = first_vis - 1;
    while (base >= 0 && !(base < e->line_syntax_cap && e->line_syntax[base].valid))
        base--;

    SyntaxState state;
    int start;
    if (base >= 0) {
        state = e->line_syntax[base].state_out;
        start = base + 1;
    } else {
        state.in_block_comment = 0;
        start = 0;
    }
    for (int li = start; li <= last_vis; li++) {
        if (li < e->line_syntax_cap && e->line_syntax[li].valid) {
            /* If an earlier edit changed the parser state entering this line,
             * the cached result (and everything after it) is stale. */
            if (e->line_syntax[li].state_in.in_block_comment !=
                state.in_block_comment) {
                editor_invalidate_syntax(e, li);
                editor_build_line_syntax(e, li, state);
            } else {
                state = e->line_syntax[li].state_out;
                continue;
            }
        } else {
            editor_build_line_syntax(e, li, state);
        }
        if (li < e->line_syntax_cap && e->line_syntax[li].valid)
            state = e->line_syntax[li].state_out;
    }
}

static void editor_rebuild_lines(Editor *e) {
    if (!e) return;
    int n = gb_total_logical(&e->gb);
    int old_count = e->line_count;
    e->line_count = 0;
    int start = 0;
    while (start <= n) {
        if (e->line_count >= e->line_cap) {
            int new_cap = e->line_cap ? e->line_cap * 2 : 256;
            int *new_offsets = realloc(e->line_offsets,
                                       (size_t)new_cap * sizeof(int));
            if (!new_offsets) { e->lines_dirty = 0; return; }
            e->line_offsets = new_offsets;
            e->line_cap = new_cap;
        }
        e->line_offsets[e->line_count++] = start;
        if (start == n) break;
        while (start < n && gb_char_logical(&e->gb, start) != '\n')
            start++;
        start++;
        if (e->line_count >= EDITOR_MAX_LINES) break;
    }
    ensure_line_syntax_capacity(e, e->line_count);
    if (e->line_count != old_count) {
        editor_invalidate_syntax(e, 0);
    }
    e->lines_dirty = 0;
}

// ---- Lifecycle ---------------------------------------------------------------

void editor_init(Editor *e) {
    memset(e, 0, sizeof(*e));
    init_buffer(&e->gb, 4096);
    e->mode = MODE_NORMAL;
    e->line_cap = 256;
    e->line_offsets = malloc((size_t)e->line_cap * sizeof(int));
    if (e->line_offsets) {
        e->line_offsets[0] = 0;
        e->line_count = 1;
    }
    ensure_line_syntax_capacity(e, e->line_cap);
    e->lines_dirty = 1;
    undo_stack_init(&e->undo);
    undo_stack_init(&e->redo);
}

void editor_free(Editor *e) {
    if (!e) return;
    free(e->gb.buffer);
    e->gb.buffer = NULL;
    free(e->line_offsets);
    e->line_offsets = NULL;
    for (int i = 0; i < e->line_syntax_cap; i++)
        line_syntax_free(&e->line_syntax[i]);
    free(e->line_syntax);
    e->line_syntax = NULL;
    undo_stack_free(&e->undo);
    undo_stack_free(&e->redo);
}

// ---- File loading / saving ---------------------------------------------------

static int editor_commit_loaded_buffer(Editor *e, GapBuffer *loaded,
                                       const char *path, int use_crlf) {
    free(e->gb.buffer);
    e->gb = *loaded;
    loaded->buffer = NULL;
    loaded->total_size = loaded->gap_start = loaded->gap_end = 0;

    snprintf(e->filepath, sizeof(e->filepath), "%s", path);
    e->use_crlf = use_crlf;
    e->cursor_col = e->cursor_row = 0;
    e->scroll_y = 0;
    e->smooth.vis_x = GUTTER_W(text_char_width());
    e->smooth.vis_y = 0;
    e->smooth.x = e->smooth.y = 0;
    e->dirty = 0;
    e->external_change = 0;
    e->selection_start = e->selection_end = 0;
    e->selecting = 0;
    e->file_mtime = get_file_mtime(path);
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
    undo_stack_free(&e->undo);
    undo_stack_free(&e->redo);
    undo_stack_init(&e->undo);
    undo_stack_init(&e->redo);
    editor_invalidate_syntax(e, 0);
    move_cursor(&e->gb, 0);
    return 1;
}

int editor_open_file(Editor *e, const char *path) {
    if (!e || !path || !path[0]) {
        editor_set_status(e, "Cannot open file: invalid path");
        return 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        char msg[320];
        snprintf(msg, sizeof(msg), "Cannot open '%s'", path);
        editor_set_status(e, msg);
        return 0;
    }

    GapBuffer loaded;
    if (!init_buffer(&loaded, 4096)) {
        fclose(f);
        editor_set_status(e, "Out of memory while opening file");
        return 0;
    }

    int use_crlf = 0;
    int pending_cr = 0;
    int load_ok = 1;
    size_t total = 0;
    char out[8192];
    int out_len = 0;

    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < got; i++) {
            unsigned char c = buf[i];
            if (pending_cr) {
                if (c == '\n') { use_crlf = 1; c = '\n'; pending_cr = 0; }
                else {
                    pending_cr = 0;
                    if ((size_t)out_len == sizeof(out)) {
                        if (!gb_insert_text(&loaded, out, out_len)) { load_ok = 0; break; }
                        total += (size_t)out_len;
                        out_len = 0;
                    }
                    out[out_len++] = '\r';
                }
            }
            if (load_ok && c == '\r') { pending_cr = 1; continue; }
            if (!load_ok) break;
            if (total + (size_t)out_len >= EDITOR_MAX_LOAD) { load_ok = 0; break; }
            if ((size_t)out_len == sizeof(out)) {
                if (!gb_insert_text(&loaded, out, out_len)) { load_ok = 0; break; }
                total += (size_t)out_len;
                out_len = 0;
            }
            out[out_len++] = (char)c;
        }
        if (!load_ok) break;
    }
    if (ferror(f)) load_ok = 0;
    if (load_ok && pending_cr) {
        if (total + (size_t)out_len >= EDITOR_MAX_LOAD) load_ok = 0;
        else if ((size_t)out_len == sizeof(out)) {
            if (!gb_insert_text(&loaded, out, out_len)) load_ok = 0;
            else { total += (size_t)out_len; out_len = 0; }
        }
        if (load_ok) out[out_len++] = '\r';
    }
    if (load_ok && out_len > 0) {
        if (!gb_insert_text(&loaded, out, out_len)) load_ok = 0;
        else total += (size_t)out_len;
    }
    fclose(f);

    if (!load_ok) {
        free(loaded.buffer);
        if (total >= EDITOR_MAX_LOAD)
            editor_set_status(e, "File is too large (limit 256 MiB)");
        else
            editor_set_status(e, "Failed to read file");
        return 0;
    }

    return editor_commit_loaded_buffer(e, &loaded, path, use_crlf);
}

static int editor_write_buffer(Editor *e, AtomicFile *af) {
    char out[8192];
    int olen = 0;
    int n = gb_total_logical(&e->gb);
    for (int i = 0; i < n; i++) {
        char c = gb_char_logical(&e->gb, i);
        if (e->use_crlf && c == '\n') {
            if (olen + 2 > (int)sizeof(out)) {
                if (!atomic_file_write(af, out, (size_t)olen)) return 0;
                olen = 0;
            }
            out[olen++] = '\r';
            out[olen++] = '\n';
        } else {
            if (olen + 1 > (int)sizeof(out)) {
                if (!atomic_file_write(af, out, (size_t)olen)) return 0;
                olen = 0;
            }
            out[olen++] = c;
        }
    }
    if (olen > 0 && !atomic_file_write(af, out, (size_t)olen)) return 0;
    return 1;
}

int editor_save_file(Editor *e) {
    if (!e || !e->filepath[0]) {
        editor_set_status(e, "No file name; use Save As");
        return 0;
    }
    AtomicFile af;
    char err[256];
    if (!atomic_file_open(&af, e->filepath, err, sizeof(err))) {
        editor_set_status(e, err);
        return 0;
    }
    if (!editor_write_buffer(e, &af)) {
        atomic_file_abort(&af);
        editor_set_status(e, "Failed to write file");
        return 0;
    }
    if (!atomic_file_commit(&af, err, sizeof(err))) {
        editor_set_status(e, err);
        return 0;
    }
    e->dirty = 0;
    e->file_mtime = get_file_mtime(e->filepath);
    e->saved_flash = 1.5f;
    editor_set_status(e, "Saved");
    return 1;
}

void editor_new_file(Editor *e) {
    if (!e) return;
    GapBuffer fresh;
    if (!init_buffer(&fresh, 4096)) {
        editor_set_status(e, "Out of memory while creating a new file");
        return;
    }
    free(e->gb.buffer);
    e->gb = fresh;
    e->filepath[0] = '\0';
    e->use_crlf = 0;
    e->cursor_col = 0;
    e->cursor_row = 0;
    e->scroll_y = 0;
    e->smooth.vis_x = 0;
    e->smooth.vis_y = 0;
    e->dirty = 0;
    e->file_mtime = 0;
    e->external_change = 0;
    e->selecting = 0;
    e->selection_start = 0;
    e->selection_end = 0;
    e->lines_dirty = 1;
    e->needs_scroll_to_cursor = 1;
    e->status_msg[0] = '\0';
    e->status_flash = 0;
    undo_stack_free(&e->undo);
    undo_stack_free(&e->redo);
    undo_stack_init(&e->undo);
    undo_stack_init(&e->redo);
    editor_invalidate_syntax(e, 0);
}

int editor_save_file_as(Editor *e, const char *path) {
    if (!e || !path || !path[0]) {
        editor_set_status(e, "Invalid save path");
        return 0;
    }
    if (strlen(path) >= sizeof(e->filepath)) {
        editor_set_status(e, "Path is too long");
        return 0;
    }
    char old_path[sizeof(e->filepath)];
    snprintf(old_path, sizeof(old_path), "%s", e->filepath);
    snprintf(e->filepath, sizeof(e->filepath), "%s", path);
    if (!editor_save_file(e)) {
        snprintf(e->filepath, sizeof(e->filepath), "%s", old_path);
        return 0;
    }
    return 1;
}

// ---- Undo / redo -------------------------------------------------------------

static void editor_mark_changed(Editor *e, int edit_row) {
    if (!e) return;
    e->dirty = 1;
    e->lines_dirty = 1;
    editor_invalidate_syntax(e, edit_row);
    e->needs_scroll_to_cursor = 1;
}

static void editor_clamp_cursor(Editor *e) {
    if (!e) return;
    if (e->line_count <= 0) {
        e->cursor_row = 0;
        e->cursor_col = 0;
        return;
    }
    if (e->cursor_row < 0) e->cursor_row = 0;
    if (e->cursor_row >= e->line_count) e->cursor_row = e->line_count - 1;
    int maxc = logical_len_of_row(e, e->cursor_row);
    if (e->cursor_col < 0) e->cursor_col = 0;
    if (e->cursor_col > maxc) e->cursor_col = maxc;
}

void editor_undo(Editor *e) {
    if (!e) return;
    UndoEntry ue;
    if (!undo_stack_pop(&e->undo, &ue)) return;

    int n = gb_total_logical(&e->gb);
    if (ue.type == UNDO_INSERT) {
        int start = ue.pos < 0 ? 0 : ue.pos;
        int end = start + ue.len;
        if (end > n) end = n;
        move_cursor(&e->gb, start);
        delete_range(&e->gb, start, end);
    } else {
        int pos = ue.pos < 0 ? 0 : ue.pos;
        if (pos > n) pos = n;
        move_cursor(&e->gb, pos);
        gb_insert_text(&e->gb, ue.text, ue.len);
    }

    int pos = ue.pos < 0 ? 0 : ue.pos;
    int cr = ue.cursor_row, cc = ue.cursor_col;
    undo_stack_push(&e->redo, ue.type, ue.pos, ue.text, ue.len, cr, cc);
    undo_entry_free(&ue);

    e->lines_dirty = 1;
    int edit_row = row_for_offset(e, pos);
    e->cursor_row = cr;
    e->cursor_col = cc;
    editor_clamp_cursor(e);
    move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
    editor_mark_changed(e, edit_row);
}

void editor_redo(Editor *e) {
    if (!e) return;
    UndoEntry re;
    if (!undo_stack_pop(&e->redo, &re)) return;

    int n = gb_total_logical(&e->gb);
    int pos = re.pos < 0 ? 0 : re.pos;
    if (re.type == UNDO_INSERT) {
        if (pos > n) pos = n;
        move_cursor(&e->gb, pos);
        gb_insert_text(&e->gb, re.text, re.len);
    } else {
        int start = pos;
        int end = start + re.len;
        if (end > n) end = n;
        move_cursor(&e->gb, start);
        delete_range(&e->gb, start, end);
    }

    undo_stack_push(&e->undo, re.type, re.pos, re.text, re.len,
                    re.cursor_row, re.cursor_col);
    int cr = re.cursor_row, cc = re.cursor_col;
    undo_entry_free(&re);

    e->lines_dirty = 1;
    int edit_row = row_for_offset(e, pos);
    e->cursor_row = cr;
    e->cursor_col = cc;
    editor_clamp_cursor(e);
    move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
    editor_mark_changed(e, edit_row);
}

// ---- Line access -------------------------------------------------------------

static int offset_of(Editor *e, int row, int col) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (e->line_count <= 0) return 0;
    if (row < 0) row = 0;
    if (row >= e->line_count) row = e->line_count - 1;
    int maxc = e->line_offsets[row + 1 < e->line_count ? row + 1 : row] -
               e->line_offsets[row];
    if (row + 1 < e->line_count && maxc > 0 &&
        gb_char_logical(&e->gb, e->line_offsets[row] + maxc - 1) == '\n')
        maxc--;
    if (row + 1 >= e->line_count)
        maxc = gb_total_logical(&e->gb) - e->line_offsets[row];
    if (col < 0) col = 0;
    if (col > maxc) col = maxc;
    int pos = e->line_offsets[row] + col;
    int n = gb_total_logical(&e->gb);
    if (pos > n) pos = n;
    return pos;
}

static int logical_len_of_row(Editor *e, int row) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (row < 0 || row >= e->line_count) return 0;
    int start = e->line_offsets[row];
    int end = (row + 1 < e->line_count) ? e->line_offsets[row + 1]
                                        : gb_total_logical(&e->gb);
    int len = end - start;
    if (len > 0 && gb_char_logical(&e->gb, end - 1) == '\n') len--;
    return len;
}

static int count_rows(Editor *e) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    return e->line_count;
}

static int row_for_offset(Editor *e, int pos) {
    if (e->lines_dirty) editor_rebuild_lines(e);
    if (e->line_count <= 0) return 0;
    int n = gb_total_logical(&e->gb);
    if (pos < 0) pos = 0;
    if (pos > n) pos = n;
    int lo = 0, hi = e->line_count - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (e->line_offsets[mid] <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

// ---- Clipboard / selection ---------------------------------------------------

static char clipboard[EDITOR_CLIPBOARD_LEN];

static char gb_char_logical(GapBuffer *gb, int pos) {
    return gb_char_at(gb, pos);
}

static int gb_total_logical(GapBuffer *gb) {
    return gb_length(gb);
}

static void editor_copy(Editor *e) {
    int start, end;
    if (!editor_selection_bounds(e, &start, &end)) return;
    int len = end - start;
    if (len <= 0) return;
    if (len >= EDITOR_CLIPBOARD_LEN) len = EDITOR_CLIPBOARD_LEN - 1;
    gb_copy_range(&e->gb, start, start + len, clipboard, EDITOR_CLIPBOARD_LEN);
}

static int editor_delete_selection(Editor *e) {
    int start, end;
    if (!editor_selection_bounds(e, &start, &end)) return 0;
    int len = end - start;
    char *sel = malloc((size_t)len + 1);
    if (sel) {
        gb_copy_range(&e->gb, start, end, sel, len + 1);
        int row = row_for_offset(e, start);
        undo_stack_push(&e->undo, UNDO_DELETE, start, sel, len,
                        e->cursor_row, e->cursor_col);
        free(sel);
        delete_range(&e->gb, start, end);
        move_cursor(&e->gb, start);
        e->lines_dirty = 1;
        editor_cursor_from_offset(e, start);
        editor_mark_changed(e, row);
    } else {
        /* Even if the selection is too large to record for undo, deleting it
         * is preferable to leaving the editor wedged. */
        delete_range(&e->gb, start, end);
        move_cursor(&e->gb, start);
        e->lines_dirty = 1;
        editor_cursor_from_offset(e, start);
        editor_mark_changed(e, row_for_offset(e, start));
    }
    e->selection_start = e->selection_end = 0;
    return 1;
}

void editor_paste(Editor *e) {
    if (!e || !clipboard[0]) return;
    int clip_len = (int)strlen(clipboard);
    int start, end;
    int had_selection = editor_selection_bounds(e, &start, &end);

    if (had_selection) {
        int sel_len = end - start;
        char *sel = malloc((size_t)sel_len + 1);
        if (sel) {
            gb_copy_range(&e->gb, start, end, sel, sel_len + 1);
            undo_stack_push(&e->undo, UNDO_DELETE, start, sel, sel_len,
                            e->cursor_row, e->cursor_col);
            free(sel);
            delete_range(&e->gb, start, end);
            move_cursor(&e->gb, start);
            e->lines_dirty = 1;
        }
    } else {
        start = e->gb.gap_start;
    }

    int insert_row = row_for_offset(e, start);
    int inserted = 0;
    if (undo_stack_push(&e->undo, UNDO_INSERT, start, clipboard, clip_len,
                        e->cursor_row, e->cursor_col)) {
        inserted = gb_insert_text(&e->gb, clipboard, clip_len);
    } else {
        inserted = gb_insert_text(&e->gb, clipboard, clip_len);
    }
    if (!inserted) {
        editor_set_status(e, "Paste failed (out of memory or file too large)");
        return;
    }

    if (e->mode == MODE_VISUAL) e->mode = MODE_NORMAL;
    if (e->mode == MODE_NORMAL) e->mode = MODE_INSERT;
    e->lines_dirty = 1;
    editor_cursor_from_offset(e, start + clip_len);
    e->selection_start = e->selection_end = 0;
    editor_mark_changed(e, insert_row);
    e->smooth.vis_x = GUTTER_W(text_char_width()) + cursor_display_col(e) * text_char_width();
    e->smooth.vis_y = e->cursor_row * (text_char_height() + 2.0f);
}

// ---- Movement / editing helpers ---------------------------------------------

static int next_byte_in_line(Editor *e, int row, int byte_col) {
    int len = logical_len_of_row(e, row);
    if (byte_col >= len) return len;
    int pos = e->line_offsets[row] + byte_col;
    int n = char_len_at(e, pos);
    int next = byte_col + (n > 0 ? n : 1);
    return next > len ? len : next;
}

static int prev_byte_in_line(Editor *e, int row, int byte_col) {
    if (byte_col <= 0) return 0;
    int pos = e->line_offsets[row] + byte_col - 1;
    while (pos > e->line_offsets[row] &&
           ((unsigned char)gb_char_logical(&e->gb, pos) & 0xC0) == 0x80)
        pos--;
    int col = pos - e->line_offsets[row];
    return col < 0 ? 0 : col;
}

static void editor_move_horizontal(Editor *e, int direction, int allow_cross) {
    int rows = count_rows(e);
    int len = logical_len_of_row(e, e->cursor_row);
    if (direction < 0) {
        if (e->cursor_col > 0) {
            e->cursor_col = prev_byte_in_line(e, e->cursor_row, e->cursor_col);
        } else if (allow_cross && e->cursor_row > 0) {
            e->cursor_row--;
            e->cursor_col = logical_len_of_row(e, e->cursor_row);
        }
    } else {
        if (e->cursor_col < len) {
            e->cursor_col = next_byte_in_line(e, e->cursor_row, e->cursor_col);
        } else if (allow_cross && e->cursor_row + 1 < rows) {
            e->cursor_row++;
            e->cursor_col = 0;
        }
    }
    move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
}

static void editor_move_vertical(Editor *e, int direction) {
    int rows = count_rows(e);
    if (direction < 0 && e->cursor_row > 0) e->cursor_row--;
    else if (direction > 0 && e->cursor_row + 1 < rows) e->cursor_row++;
    else return;
    int maxc = logical_len_of_row(e, e->cursor_row);
    if (e->cursor_col > maxc) e->cursor_col = maxc;
    move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
}

static void editor_insert_text_undo(Editor *e, const char *text, int len,
                                    int cursor_row, int cursor_col, int edit_row) {
    if (!text || len <= 0) return;
    int pos = offset_of(e, cursor_row, cursor_col);
    undo_stack_push(&e->undo, UNDO_INSERT, pos, text, len,
                    cursor_row, cursor_col);
    move_cursor(&e->gb, pos);
    gb_insert_text(&e->gb, text, len);
    e->lines_dirty = 1;
    editor_cursor_from_offset(e, pos + len);
    editor_mark_changed(e, edit_row);
    editor_invalidate_syntax(e, edit_row);
}

static void editor_delete_char_under_cursor(Editor *e) {
    int pos = offset_of(e, e->cursor_row, e->cursor_col);
    int n = gb_total_logical(&e->gb);
    if (pos >= n) return;
    int len = char_len_at(e, pos);
    char bytes[8];
    int got = gb_copy_range(&e->gb, pos, pos + len, bytes, sizeof(bytes));
    if (got <= 0) return;
    int row = row_for_offset(e, pos);
    undo_stack_push(&e->undo, UNDO_DELETE, pos, bytes, got,
                    e->cursor_row, e->cursor_col);
    delete_range(&e->gb, pos, pos + got);
    e->lines_dirty = 1;
    editor_cursor_from_offset(e, pos);
    editor_mark_changed(e, row);
    editor_invalidate_syntax(e, row);
}

// ---- Mouse / scroll ----------------------------------------------------------

static void editor_cursor_from_pixel(Editor *e, float px, float py,
                                     float ex, float ey) {
    float cw = text_char_width();
    float ch = text_char_height();
    float row = ch + 2.0f;
    float gutter = GUTTER_W(cw);
    int clicked_row = (int)((py - ey + e->scroll_y) / row);
    int clicked_display = (int)((px - ex - gutter) / cw);
    if (clicked_row < 0) clicked_row = 0;
    if (clicked_display < 0) clicked_display = 0;
    int rows = count_rows(e);
    if (rows <= 0) { e->cursor_row = 0; e->cursor_col = 0; return; }
    if (clicked_row >= rows) clicked_row = rows - 1;
    e->cursor_row = clicked_row;
    e->cursor_col = byte_col_for_display_col(e, clicked_row, clicked_display);
    int maxc = logical_len_of_row(e, clicked_row);
    if (e->cursor_col > maxc) e->cursor_col = maxc;
    e->smooth.vis_x = gutter + display_col_for_byte_col(e, clicked_row, e->cursor_col) * cw;
    e->smooth.vis_y = clicked_row * row;
    e->needs_scroll_to_cursor = 1;
}

static int pixel_to_offset(Editor *e, float px, float py, float ex, float ey) {
    editor_cursor_from_pixel(e, px, py, ex, ey);
    return offset_of(e, e->cursor_row, e->cursor_col);
}

void editor_mouse_press(Editor *e, float px, float py, float ex, float ey) {
    int pos = pixel_to_offset(e, px, py, ex, ey);
    move_cursor(&e->gb, pos);
    e->selection_start = pos;
    e->selection_end = pos;
    e->selecting = 1;
}

void editor_mouse_move(Editor *e, float px, float py, float ex, float ey) {
    if (!e->selecting) return;
    e->selection_end = pixel_to_offset(e, px, py, ex, ey);
}

void editor_mouse_release(Editor *e) {
    if (e->selecting) move_cursor(&e->gb, e->selection_end);
    e->selecting = 0;
}

void editor_scroll(Editor *e, float yoffset) {
    float ch = text_char_height();
    float row = ch + 2.0f;
    e->scroll_y -= yoffset * row;
    if (e->scroll_y < 0) e->scroll_y = 0;
    e->smooth.vis_x = GUTTER_W(text_char_width()) + cursor_display_col(e) * text_char_width();
    e->smooth.vis_y = e->cursor_row * row;
}

static void ensure_cursor_visible(Editor *e) {
    float ch = text_char_height();
    float row = ch + 2.0f;
    float cy = e->smooth.vis_y - e->scroll_y;
    float vh = e->viewport_h > 0 ? e->viewport_h : 400.0f;
    if (cy < 0) e->scroll_y -= (row - cy);
    if (cy > vh - row * 2) e->scroll_y += (cy - (vh - row * 2));
    if (e->scroll_y < 0) e->scroll_y = 0;
}

// ---- Key handling ------------------------------------------------------------

static void editor_visual_set_end(Editor *e) {
    int pos = offset_of(e, e->cursor_row, e->cursor_col);
    int n = gb_total_logical(&e->gb);
    int len = pos < n ? char_len_at(e, pos) : 0;
    e->selection_end = pos + len;
}

void editor_key(Editor *e, int key, int mods) {
    if (!e) return;
    int ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    int shift = (mods & GLFW_MOD_SHIFT) != 0;
    int prev_row = e->cursor_row;
    int prev_col = e->cursor_col;

    // Search mode — intercept all keys
    if (e->search_active) {
        if (key == GLFW_KEY_ESCAPE) { search_close(e); goto key_done; }
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER ||
            key == GLFW_KEY_F3) {
            if (shift) search_prev(e); else search_next(e);
            goto key_done;
        }
        if (key == GLFW_KEY_BACKSPACE && e->search_cursor > 0 &&
            e->search_cursor <= e->search_len) {
            int start = text_utf8_prev(e->search_query, e->search_cursor);
            int count = e->search_cursor - start;
            memmove(e->search_query + start,
                    e->search_query + e->search_cursor,
                    (size_t)(e->search_len - e->search_cursor + 1));
            e->search_cursor = start;
            e->search_len -= count;
            if (e->search_len < 0) e->search_len = 0;
            search_find_all(e);
            goto key_done;
        }
        if (key == GLFW_KEY_DELETE && e->search_cursor >= 0 &&
            e->search_cursor < e->search_len) {
            int next = text_utf8_next(e->search_query, e->search_cursor, e->search_len);
            int count = next - e->search_cursor;
            memmove(e->search_query + e->search_cursor,
                    e->search_query + next,
                    (size_t)(e->search_len - next + 1));
            e->search_len -= count;
            if (e->search_len < 0) e->search_len = 0;
            search_find_all(e);
            goto key_done;
        }
        if (key == GLFW_KEY_LEFT && e->search_cursor > 0) {
            e->search_cursor = text_utf8_prev(e->search_query, e->search_cursor);
            goto key_done;
        }
        if (key == GLFW_KEY_RIGHT && e->search_cursor < e->search_len) {
            e->search_cursor = text_utf8_next(e->search_query, e->search_cursor,
                                             e->search_len);
            goto key_done;
        }
        if (key == GLFW_KEY_HOME) { e->search_cursor = 0; goto key_done; }
        if (key == GLFW_KEY_END) { e->search_cursor = e->search_len; goto key_done; }
        goto key_done;
    }

    if (ctrl && key == GLFW_KEY_F) { search_open(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_N) { editor_new_file(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_S) { editor_save_file(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_C) { editor_copy(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_V) { editor_paste(e); goto key_done; }
    if (ctrl && key == GLFW_KEY_A) {
        e->cursor_col = 0;
        move_cursor(&e->gb, offset_of(e, e->cursor_row, 0));
        goto key_done;
    }
    if (ctrl && (key == GLFW_KEY_Z || key == GLFW_KEY_U)) { editor_undo(e); goto key_done; }
    if (ctrl && (key == GLFW_KEY_Y || key == GLFW_KEY_R)) { editor_redo(e); goto key_done; }

    if (e->mode == MODE_INSERT) {
        if (key == GLFW_KEY_ESCAPE) { e->mode = MODE_NORMAL; goto key_done; }
        if (key == GLFW_KEY_BACKSPACE) {
            int pos = offset_of(e, e->cursor_row, e->cursor_col);
            if (pos > 0) {
                int start = pos - 1;
                while (start > 0 &&
                       ((unsigned char)gb_char_logical(&e->gb, start) & 0xC0) == 0x80)
                    start--;
                char bytes[8];
                int got = gb_copy_range(&e->gb, start, pos, bytes, sizeof(bytes));
                if (got > 0) {
                    int row = row_for_offset(e, start);
                    undo_stack_push(&e->undo, UNDO_DELETE, start, bytes, got,
                                    e->cursor_row, e->cursor_col);
                    delete_range(&e->gb, start, pos);
                    e->lines_dirty = 1;
                    editor_cursor_from_offset(e, start);
                    editor_mark_changed(e, row);
                }
            }
            goto key_done;
        }
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            editor_insert_text_undo(e, "\n", 1, e->cursor_row, e->cursor_col,
                                    e->cursor_row);
            goto key_done;
        }
        if (key == GLFW_KEY_TAB) {
            editor_insert_text_undo(e, "\t", 1, e->cursor_row, e->cursor_col,
                                    e->cursor_row);
            goto key_done;
        }
        if (key == GLFW_KEY_LEFT)  { editor_move_horizontal(e, -1, 1); goto key_done; }
        if (key == GLFW_KEY_RIGHT) { editor_move_horizontal(e,  1, 1); goto key_done; }
        if (key == GLFW_KEY_UP)    { editor_move_vertical(e, -1); goto key_done; }
        if (key == GLFW_KEY_DOWN)  { editor_move_vertical(e,  1); goto key_done; }
        if (key == GLFW_KEY_HOME)  { e->cursor_col = 0; move_cursor(&e->gb, offset_of(e, e->cursor_row, 0)); goto key_done; }
        if (key == GLFW_KEY_END)   { e->cursor_col = logical_len_of_row(e, e->cursor_row); move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); goto key_done; }
        goto key_done;
    }

    // Visual mode keys
    if (e->mode == MODE_VISUAL) {
        int rows = count_rows(e);
        if (key == GLFW_KEY_ESCAPE) {
            e->mode = MODE_NORMAL;
            e->selection_start = e->selection_end = 0;
            goto key_done;
        }
        if (key == GLFW_KEY_H || key == GLFW_KEY_LEFT) {
            editor_move_horizontal(e, -1, 0);
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_L || key == GLFW_KEY_RIGHT) {
            editor_move_horizontal(e, 1, 0);
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_K || key == GLFW_KEY_UP) {
            editor_move_vertical(e, -1);
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_J || key == GLFW_KEY_DOWN) {
            editor_move_vertical(e, 1);
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_0 || key == GLFW_KEY_HOME) {
            e->cursor_col = 0;
            move_cursor(&e->gb, offset_of(e, e->cursor_row, 0));
            editor_visual_set_end(e);
            goto key_done;
        }
        if ((key == GLFW_KEY_4 && shift) || key == GLFW_KEY_END) {
            e->cursor_col = logical_len_of_row(e, e->cursor_row);
            move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_Y) {
            editor_copy(e);
            e->mode = MODE_NORMAL;
            e->selection_start = e->selection_end = 0;
            goto key_done;
        }
        if (key == GLFW_KEY_D || key == GLFW_KEY_X) {
            editor_delete_selection(e);
            e->mode = MODE_NORMAL;
            goto key_done;
        }
        if (key == GLFW_KEY_P) {
            editor_paste(e);
            e->mode = MODE_NORMAL;
            goto key_done;
        }
        (void)rows;
        goto key_done;
    }

    // Normal mode keys
    if (e->mode == MODE_NORMAL) {
        int maxc = logical_len_of_row(e, e->cursor_row);
        if (key == GLFW_KEY_I) {
            e->mode = MODE_INSERT;
            e->key_handled = 1;
            goto key_done;
        }
        if (key == GLFW_KEY_A) {
            if (e->cursor_col < maxc) {
                e->cursor_col = next_byte_in_line(e, e->cursor_row, e->cursor_col);
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
            }
            e->mode = MODE_INSERT;
            e->key_handled = 1;
            goto key_done;
        }
        if (key == GLFW_KEY_V) {
            e->mode = MODE_VISUAL;
            e->selection_start = offset_of(e, e->cursor_row, e->cursor_col);
            e->selection_end = e->selection_start;
            editor_visual_set_end(e);
            goto key_done;
        }
        if (key == GLFW_KEY_H) { editor_move_horizontal(e, -1, 0); goto key_done; }
        if (key == GLFW_KEY_L) { editor_move_horizontal(e,  1, 0); goto key_done; }
        if (key == GLFW_KEY_K) { editor_move_vertical(e, -1); goto key_done; }
        if (key == GLFW_KEY_J) { editor_move_vertical(e,  1); goto key_done; }
        if (key == GLFW_KEY_0 || key == GLFW_KEY_HOME) {
            e->cursor_col = 0;
            move_cursor(&e->gb, offset_of(e, e->cursor_row, 0));
            goto key_done;
        }
        if (key == GLFW_KEY_6 && shift) {
            int col = 0;
            while (col < maxc &&
                   (gb_char_logical(&e->gb, e->line_offsets[e->cursor_row] + col) == ' ' ||
                    gb_char_logical(&e->gb, e->line_offsets[e->cursor_row] + col) == '\t'))
                col++;
            e->cursor_col = col;
            move_cursor(&e->gb, offset_of(e, e->cursor_row, col));
            goto key_done;
        }
        if ((key == GLFW_KEY_4 && shift) || key == GLFW_KEY_END) {
            e->cursor_col = maxc;
            move_cursor(&e->gb, offset_of(e, e->cursor_row, maxc));
            goto key_done;
        }
        if (key == GLFW_KEY_X) {
            editor_delete_char_under_cursor(e);
            goto key_done;
        }
        if (key == GLFW_KEY_U) { editor_undo(e); goto key_done; }
        if (key == GLFW_KEY_P) { editor_paste(e); goto key_done; }
        if (key == GLFW_KEY_O) {
            int eol = offset_of(e, e->cursor_row, logical_len_of_row(e, e->cursor_row));
            undo_stack_push(&e->undo, UNDO_INSERT, eol, "\n", 1,
                            e->cursor_row, e->cursor_col);
            move_cursor(&e->gb, eol);
            gb_insert_text(&e->gb, "\n", 1);
            e->lines_dirty = 1;
            editor_cursor_from_offset(e, eol + 1);
            editor_mark_changed(e, e->cursor_row > 0 ? e->cursor_row - 1 : 0);
            e->mode = MODE_INSERT;
            e->key_handled = 1;
            goto key_done;
        }
        if (key == GLFW_KEY_LEFT)  { editor_move_horizontal(e, -1, 0); goto key_done; }
        if (key == GLFW_KEY_RIGHT) { editor_move_horizontal(e,  1, 0); goto key_done; }
        if (key == GLFW_KEY_UP)    { editor_move_vertical(e, -1); goto key_done; }
        if (key == GLFW_KEY_DOWN)  { editor_move_vertical(e,  1); goto key_done; }
    }

key_done:
    if (e->cursor_row != prev_row || e->cursor_col != prev_col)
        e->needs_scroll_to_cursor = 1;
}

void editor_char(Editor *e, unsigned int cp) {
    if (!e) return;
    if (e->key_handled) { e->key_handled = 0; return; }

    // Search mode — typed chars go into search query
    if (e->search_active) {
        if (cp < 32 || cp == 127) return;
        char bytes[4];
        int n = text_utf8_encode(cp, bytes);
        if (n <= 0 || e->search_len + n >= SEARCH_MAX_LEN) return;
        if (e->search_cursor < 0) e->search_cursor = 0;
        if (e->search_cursor > e->search_len) e->search_cursor = e->search_len;
        memmove(e->search_query + e->search_cursor + n,
                e->search_query + e->search_cursor,
                (size_t)(e->search_len - e->search_cursor + 1));
        memcpy(e->search_query + e->search_cursor, bytes, (size_t)n);
        e->search_cursor += n;
        e->search_len += n;
        search_find_all(e);
        return;
    }

    if (e->mode != MODE_INSERT) return;
    if (cp < 32 || cp == 127) return;

    char bytes[4];
    int n = text_utf8_encode(cp, bytes);
    if (n <= 0) return;

    char close = 0;
    if (cp == '"') close = '"';
    else if (cp == '(') close = ')';
    else if (cp == '[') close = ']';
    else if (cp == '{') close = '}';
    else if (cp == '\'') close = '\'';

    int row = e->cursor_row;
    int pos = offset_of(e, e->cursor_row, e->cursor_col);
    if (close) {
        char pair[2] = { (char)cp, close };
        undo_stack_push(&e->undo, UNDO_INSERT, pos, pair, 2, row, e->cursor_col);
        move_cursor(&e->gb, pos);
        gb_insert_text(&e->gb, pair, 2);
        move_cursor(&e->gb, pos + 1);
        e->cursor_col += 1;
    } else {
        undo_stack_push(&e->undo, UNDO_INSERT, pos, bytes, n, row, e->cursor_col);
        move_cursor(&e->gb, pos);
        gb_insert_text(&e->gb, bytes, n);
        e->cursor_col += n;
    }
    e->lines_dirty = 1;
    editor_mark_changed(e, row);
    editor_invalidate_syntax(e, row);
}

// ---- Update / render ---------------------------------------------------------

void editor_update(Editor *e, float dt) {
    if (!e) return;
    float cw = text_char_width();
    float ch = text_char_height();
    float target_x = GUTTER_W(cw) + cursor_display_col(e) * cw;
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
    if (e->saved_flash > 0) e->saved_flash -= dt;
    if (e->status_flash > 0) e->status_flash -= dt;
}

int editor_animation_active(Editor *e) {
    if (!e) return 0;
    float cw = text_char_width();
    float ch = text_char_height();
    float tx = GUTTER_W(cw) + display_col_for_byte_col(e, e->cursor_row, e->cursor_col) * cw;
    float ty = e->cursor_row * (ch + 2.0f);
    if (fabsf(e->smooth.vis_x - tx) > 0.05f || fabsf(e->smooth.vis_y - ty) > 0.05f)
        return 1;
    if (e->saved_flash > 0 || e->status_flash > 0) return 1;
    if (e->search_active) return 1; // blinking search caret
    return 0;
}

static int line_content_end(Editor *e, int row) {
    int lstart = e->line_offsets[row];
    int lend = (row + 1 < e->line_count) ? e->line_offsets[row + 1]
                                         : gb_total_logical(&e->gb);
    int end = lstart;
    while (end < lend && gb_char_logical(&e->gb, end) != '\n') end++;
    return end;
}

void editor_render(Editor *e, float x, float y, float w, float h) {
    if (!e) return;
    if (g_theme.bg_gradient == THEME_GRADIENT_VERTICAL)
        draw_rect_gradient(x, y, w, h, COLOR_ED_BACKGROUND,
                           g_theme.background_end.r, g_theme.background_end.g,
                           g_theme.background_end.b, 1);
    else if (g_theme.bg_gradient == THEME_GRADIENT_HORIZONTAL)
        draw_rect_gradient(x, y, w, h, COLOR_ED_BACKGROUND,
                           g_theme.background_end.r, g_theme.background_end.g,
                           g_theme.background_end.b, 0);
    else
        draw_rect(x, y, w, h, COLOR_ED_BACKGROUND, 1.0f);

    float cw = text_char_width();
    float ch = text_char_height();
    float row = ch + 2.0f;
    e->viewport_h = h;

    float search_bar_h = 0;
    if (e->search_active) {
        search_bar_h = row + 8.0f;
        float sbx = x + w - 320.0f;
        float sby = y;
        if (sbx < x) sbx = x;
        draw_rect(sbx, sby, 320.0f, search_bar_h, COLOR_SEARCH_BAR_BG, 0.95f);
        draw_rect(sbx, sby, 320.0f, 1.0f, COLOR_MODAL_BORDER, 1.0f);

        char label[SEARCH_MAX_LEN + 32];
        snprintf(label, sizeof(label), "Find: %s", e->search_query);
        draw_text(label, sbx + 8, sby + row * 0.85f, 0.9f, 0.9f, 0.9f);

        float cursor_col = (float)text_display_width(e->search_query,
                                                      e->search_cursor,
                                                      0, TEXT_TAB_WIDTH);
        float ctext_x = sbx + 8 + 6 * cw + cursor_col * cw;
        draw_rect(ctext_x, sby + 4, 1.5f, ch, COLOR_TEXT, 0.7f);

        if (e->search_len > 0) {
            char cnt[32];
            if (e->search_match_count > 0)
                snprintf(cnt, sizeof(cnt), "%d/%d", e->search_match_cur + 1,
                         e->search_match_count);
            else
                snprintf(cnt, sizeof(cnt), "no match");
            draw_text(cnt, sbx + 260, sby + row * 0.85f, 0.6f, 0.6f, 0.6f);
        }
        y += search_bar_h;
        h -= search_bar_h;
    }

    if (e->lines_dirty) editor_rebuild_lines(e);
    float gutter = GUTTER_W(cw);
    float cy = y + e->smooth.vis_y - e->scroll_y + (row - ch) * 0.5f;

    int cursor_x_display = cursor_display_col(e);
    float cursor_x = x + gutter + cursor_x_display * cw;
    if (e->mode == MODE_INSERT)
        draw_rect(cursor_x, cy, 2.0f, ch, COLOR_CURSOR_HIGHLIGHT, 0.9f);
    else
        draw_rect(cursor_x, cy, cw, ch, COLOR_CURSOR_HIGHLIGHT, 0.45f);

    int first_vis = 0;
    if (row > 0) {
        first_vis = (int)(e->scroll_y / row);
        if (first_vis < 0) first_vis = 0;
        if (first_vis >= e->line_count) first_vis = e->line_count - 1;
    }
    int last_vis = first_vis + (int)(h / row) + 2;
    if (last_vis >= e->line_count) last_vis = e->line_count - 1;
    if (last_vis < first_vis) last_vis = first_vis;
    ensure_syntax_to_visible(e, first_vis, last_vis);

    int sel_start, sel_end;
    int has_sel = editor_selection_bounds(e, &sel_start, &sel_end);

    float ty = y - e->scroll_y + first_vis * row;
    text_renderer_begin();
    for (int li = first_vis; li < e->line_count; li++) {
        if (ty >= y + h) break;
        if (li >= e->line_syntax_cap) break;
        LineSyntax *ls = &e->line_syntax[li];
        if (!ls->valid) continue;

        int lstart = e->line_offsets[li];
        int content_end = line_content_end(e, li);
        int llen = content_end - lstart;

        if (ty + row >= y) {
            if (has_sel) {
                int hs = sel_start < lstart ? 0 : sel_start - lstart;
                int he = sel_end > content_end ? llen : sel_end - lstart;
                if (hs < 0) hs = 0;
                if (he < 0) he = 0;
                if (hs < llen && he > 0) {
                    if (hs > llen) hs = llen;
                    if (he > llen) he = llen;
                    if (hs < he) {
                        int dc0 = display_col_for_byte_col(e, li, hs);
                        int dc1 = display_col_for_byte_col(e, li, he);
                        text_renderer_end();
                        draw_rect(x + gutter + dc0 * cw, ty + (row - ch) * 0.5f,
                                  (dc1 - dc0) * cw, ch,
                                  COLOR_CURSOR_HIGHLIGHT, 0.3f);
                        text_renderer_begin();
                    }
                }
            }
            if (e->search_match_count > 0 && e->search_len > 0) {
                for (int mi = 0; mi < e->search_match_count; mi++) {
                    int mpos = e->search_matches[mi];
                    if (mpos < lstart || mpos + e->search_len > content_end) continue;
                    int mcol = mpos - lstart;
                    int mcol_end = mcol + e->search_len;
                    int dc0 = display_col_for_byte_col(e, li, mcol);
                    int dc1 = display_col_for_byte_col(e, li, mcol_end);
                    int is_cur = (mi == e->search_match_cur);
                    text_renderer_end();
                    if (is_cur)
                        draw_rect(x + gutter + dc0 * cw, ty + (row - ch) * 0.5f,
                                  (dc1 - dc0) * cw, ch,
                                  COLOR_SEARCH_MATCH_CUR, 0.45f);
                    else
                        draw_rect(x + gutter + dc0 * cw, ty + (row - ch) * 0.5f,
                                  (dc1 - dc0) * cw, ch,
                                  COLOR_SEARCH_MATCH, 0.2f);
                    text_renderer_begin();
                }
            }

            float tx = x + gutter;
            for (int ti = 0; ti < ls->token_count; ti++) {
                CachedToken *t = &ls->tokens[ti];
                if (t->start < 0 || t->len < 0 ||
                    t->start + t->len > ls->text_len) continue;
                float cr, cg, cb;
                token_color(t->type, &cr, &cg, &cb);
                batch_text_len(ls->text + t->start, t->len, &tx,
                               ty + ch * 0.85f, cr, cg, cb);
            }
            char lnum[16];
            snprintf(lnum, sizeof(lnum), "%4d", li + 1);
            float lnx = x + 4;
            batch_text(lnum, &lnx, ty + ch * 0.85f, COLOR_TEXT);
        }
        ty += row;
    }
    text_renderer_end();

    const char *mode_str = e->mode == MODE_INSERT ? "INSERT" :
                           e->mode == MODE_VISUAL ? "VISUAL" : "NORMAL";
    char status[1024];
    const char *display_path = e->filepath[0] ? e->filepath : "[No File]";
    if (e->status_flash > 0 && e->status_msg[0]) {
        snprintf(status, sizeof(status), " %s  %s  %d:%d  %s",
                 mode_str, display_path, e->cursor_row + 1, e->cursor_col + 1,
                 e->status_msg);
    } else if (e->saved_flash > 0) {
        snprintf(status, sizeof(status), " %s  %s  %d:%d  Saved",
                 mode_str, display_path, e->cursor_row + 1, e->cursor_col + 1);
    } else {
        snprintf(status, sizeof(status), " %s  %s  %d:%d",
                 mode_str, display_path, e->cursor_row + 1, e->cursor_col + 1);
    }
    draw_rect(x, y + h - 20.0f, w, 20.0f, COLOR_TOOLBAR, 1.0f);
    draw_text(status, x + 4, y + h - 4.0f, COLOR_TEXT);
}

// ---- Hot reload --------------------------------------------------------------

static double get_file_mtime(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#ifdef _WIN32
    return (double)st.st_mtime;
#else
    return (double)st.st_mtime + (double)st.st_mtim.tv_nsec * 1e-9;
#endif
}

void editor_check_external_change(Editor *e) {
    if (!e->filepath[0]) { e->external_change = 0; return; }
    double mt = get_file_mtime(e->filepath);
    if (e->file_mtime == 0) { e->file_mtime = mt; return; }
    if (mt > e->file_mtime) e->external_change = 1;
}

void editor_reload_file(Editor *e) {
    if (!e || !e->filepath[0]) return;
    char path[sizeof(e->filepath)];
    snprintf(path, sizeof(path), "%s", e->filepath);
    editor_open_file(e, path);
    e->external_change = 0;
}

void editor_dismiss_external_change(Editor *e) {
    e->external_change = 0;
    e->file_mtime = get_file_mtime(e->filepath);
}
