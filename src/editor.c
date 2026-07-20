#include "editor.h"
#include "text_renderer.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "colors.h"
#include <ctype.h>

#define LERP_SPEED 18.0f
#define GUTTER_W(cw) (4.0f + 4.0f * (cw) + 8.0f)

void editor_init(Editor *e) {
    memset(e, 0, sizeof(*e));
    init_buffer(&e->gb, 4096);
    e->mode = MODE_NORMAL;
    e->smooth.vis_x = 0;
    e->smooth.vis_y = 0;
}

void editor_free(Editor *e) {
    if (e->gb.buffer) free(e->gb.buffer);
}

void editor_open_file(Editor *e, const char *path) {
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    FILE *f = fopen(path, "rb");
    if (!f) return;

    // Reset gap buffer
    free(e->gb.buffer);
    init_buffer(&e->gb, 4096);
    e->cursor_col = e->cursor_row = 0;
    e->scroll_y = 0;

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

// Return buffer offset for (row, col)
static int offset_of(Editor *e, int row, int col) {
    GapBuffer *gb = &e->gb;
    int r = 0, c = 0, pos = 0;
    int total = gb->gap_start + (gb->total_size - gb->gap_end);
    // iterate over logical chars
    for (int i = 0; i < gb->gap_start + (gb->total_size - gb->gap_end); ) {
        // map logical index to buffer index
        int bi = (i < gb->gap_start) ? i : i + (gb->gap_end - gb->gap_start);
        if (bi >= gb->total_size) break;
        if (r == row && c == col) return i;
        char ch = gb->buffer[bi];
        if (ch == '\n') { r++; c = 0; } else { c++; }
        i++;
        pos = i;
        (void)total;
    }
    return pos;
}

static int logical_len_of_row(Editor *e, int row) {
    GapBuffer *gb = &e->gb;
    int r = 0, c = 0;
    int n = gb->gap_start + (gb->total_size - gb->gap_end);
    for (int i = 0; i < n; i++) {
        int bi = (i < gb->gap_start) ? i : i + (gb->gap_end - gb->gap_start);
        if (bi >= gb->total_size) break;
        char ch = gb->buffer[bi];
        if (r == row) { if (ch == '\n') return c; c++; }
        else if (ch == '\n') { r++; c = 0; }
    }
    return (r == row) ? c : 0;
}

static int count_rows(Editor *e) {
    GapBuffer *gb = &e->gb;
    int rows = 1;
    int n = gb->gap_start + (gb->total_size - gb->gap_end);
    for (int i = 0; i < n; i++) {
        int bi = (i < gb->gap_start) ? i : i + (gb->gap_end - gb->gap_start);
        if (bi < gb->total_size && gb->buffer[bi] == '\n') rows++;
    }
    return rows;
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
    // Delete selection if any
    if (e->selection_start != e->selection_end) {
        int start = e->selection_start < e->selection_end ? e->selection_start : e->selection_end;
        int end   = e->selection_start > e->selection_end ? e->selection_start : e->selection_end;
        delete_range(&e->gb, start, end);
        move_cursor(&e->gb, start);
        e->selection_start = e->selection_end = 0;
        // Recompute cursor_row/cursor_col from the buffer position
        int r = 0, c = 0;
        int n = e->gb.gap_start + (e->gb.total_size - e->gb.gap_end);
        for (int i = 0; i < start && i < n; i++) {
            int bi = (i < e->gb.gap_start) ? i : i + (e->gb.gap_end - e->gb.gap_start);
            if (bi < e->gb.total_size && e->gb.buffer[bi] == '\n') { r++; c = 0; } else { c++; }
        }
        e->cursor_row = r;
        e->cursor_col = c;
    }
    // Switch to insert mode if in normal/visual so paste always works
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
}

// Convert pixel position (px,py) relative to window into a logical buffer offset.
// Also updates e->cursor_row and e->cursor_col to match.
// ex,ey is the top-left of the editor panel.
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

void editor_key(Editor *e, int key, int mods) {
    int ctrl = (mods & GLFW_MOD_CONTROL);

    // Save
    if (ctrl && key == GLFW_KEY_S) { editor_save_file(e); return; }
    // Copy & Paste
    if (ctrl && key == GLFW_KEY_C) { editor_copy(e); return; }
    if (ctrl && key == GLFW_KEY_V) { editor_paste(e); return; }
    if (ctrl && key == GLFW_KEY_A) { e->cursor_col = 0; move_cursor(&e->gb, offset_of(e, e->cursor_row, 0)); return; }

    if (e->mode == MODE_INSERT) {
        int cur = e->gb.gap_start;
        if (key == GLFW_KEY_ESCAPE) { e->mode = MODE_NORMAL; return; }
        if (key == GLFW_KEY_BACKSPACE) {
            int prev_row_len = 0;
            if (e->cursor_col == 0 && e->cursor_row > 0)
                prev_row_len = logical_len_of_row(e, e->cursor_row - 1);
            delete_char(&e->gb); e->dirty = 1;
            if (e->cursor_col > 0) e->cursor_col--;
            else if (e->cursor_row > 0) {
                e->cursor_row--;
                e->cursor_col = prev_row_len;
            }
            return;
        }
        if (key == GLFW_KEY_ENTER) {
            insert_char(&e->gb, '\n'); e->dirty = 1;
            e->cursor_row++; e->cursor_col = 0; return;
        }
        if (key == GLFW_KEY_LEFT)  { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, cur-1); } return; }
        if (key == GLFW_KEY_RIGHT) { e->cursor_col++; move_cursor(&e->gb, cur+1); return; }
        if (key == GLFW_KEY_UP)    { if (e->cursor_row > 0) { e->cursor_row--; int maxc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > maxc) e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } return; }
        if (key == GLFW_KEY_DOWN)  { int rows = count_rows(e); if (e->cursor_row < rows-1) { e->cursor_row++; int maxc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > maxc) e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } return; }
        return;
    }

    // Visual mode keys
    if (e->mode == MODE_VISUAL) {
        int rows = count_rows(e);
        int maxc = logical_len_of_row(e, e->cursor_row);
        if (key == GLFW_KEY_ESCAPE) { e->mode = MODE_NORMAL; return; }
        if (key == GLFW_KEY_LEFT) {
            if (e->cursor_col > 0) {
                e->cursor_col--;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            return;
        }
        if (key == GLFW_KEY_RIGHT) {
            if (e->cursor_col < maxc) {
                e->cursor_col++;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            return;
        }
        if (key == GLFW_KEY_UP) {
            if (e->cursor_row > 0) {
                e->cursor_row--;
                int mc = logical_len_of_row(e, e->cursor_row);
                if (e->cursor_col > mc) e->cursor_col = mc;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            return;
        }
        if (key == GLFW_KEY_DOWN) {
            if (e->cursor_row < rows - 1) {
                e->cursor_row++;
                int mc = logical_len_of_row(e, e->cursor_row);
                if (e->cursor_col > mc) e->cursor_col = mc;
                move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col));
                e->selection_end = offset_of(e, e->cursor_row, e->cursor_col);
            }
            return;
        }
        return;
    }

    // Normal mode keys
    if (e->mode == MODE_NORMAL) {
        int rows = count_rows(e);
        int maxc = logical_len_of_row(e, e->cursor_row);
        if (key == GLFW_KEY_I || key == GLFW_KEY_A) { e->mode = MODE_INSERT; e->key_handled = 1; return; }
        if (key == GLFW_KEY_V) { 
            e->mode = MODE_VISUAL; 
            e->selection_start = offset_of(e, e->cursor_row, e->cursor_col);
            e->selection_end = e->selection_start;
            
            return; 
        }
        if (key == GLFW_KEY_H) { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, e->gb.gap_start - 1); } return; }
        if (key == GLFW_KEY_L) { if (e->cursor_col < maxc) { e->cursor_col++; move_cursor(&e->gb, e->gb.gap_start + 1); } return; }
        if (key == GLFW_KEY_K) { if (e->cursor_row > 0) { e->cursor_row--; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } return; }
        if (key == GLFW_KEY_J) { if (e->cursor_row < rows - 1) { e->cursor_row++; int mc = logical_len_of_row(e, e->cursor_row); if (e->cursor_col > mc) e->cursor_col = mc; move_cursor(&e->gb, offset_of(e, e->cursor_row, e->cursor_col)); } return; }
        if (key == GLFW_KEY_0) { e->cursor_col = 0; move_cursor(&e->gb, offset_of(e, e->cursor_row, 0)); return; }
        if (key == GLFW_KEY_4 && (mods & GLFW_MOD_SHIFT)) { e->cursor_col = maxc; move_cursor(&e->gb, offset_of(e, e->cursor_row, maxc)); return; }
        if (key == GLFW_KEY_X) {
            GapBuffer *gb = &e->gb;
            int n = gb->gap_start + (gb->total_size - gb->gap_end);
            if (gb->gap_start < n) {
                gb->gap_end++;
                e->dirty = 1;
            }
            return;
        }
        if (key == GLFW_KEY_O) {
            int eol = offset_of(e, e->cursor_row, logical_len_of_row(e, e->cursor_row));
            move_cursor(&e->gb, eol);
            insert_char(&e->gb, '\n'); e->dirty = 1;
            e->cursor_row++; e->cursor_col = 0;
            e->mode = MODE_INSERT;
            e->key_handled = 1;
            return;
        }
        if (key == GLFW_KEY_LEFT)  { if (e->cursor_col > 0) { e->cursor_col--; move_cursor(&e->gb, e->gb.gap_start - 1); } return; }
    }
}

void editor_char(Editor *e, unsigned int cp) {
    if (e->key_handled) { e->key_handled = 0; return; }
    if (e->mode != MODE_INSERT) return;
    if (cp < 32 || cp > 126) return;
    insert_char(&e->gb, (char)cp);
    e->cursor_col++;
    e->dirty = 1;
}

void editor_update(Editor *e, float dt) {
    float cw = text_char_width();
    float ch = text_char_height();
    float target_x = GUTTER_W(cw) + e->cursor_col * cw;
    float target_y = e->cursor_row * (ch + 2.0f);
    float t = 1.0f - expf(-LERP_SPEED * dt);
    e->smooth.vis_x += (target_x - e->smooth.vis_x) * t;
    e->smooth.vis_y += (target_y - e->smooth.vis_y) * t;
}

void editor_render(Editor *e, float x, float y, float w, float h) {
    draw_rect(x, y, w, h, COLOR_ED_BACKGROUND);

    float cw  = text_char_width();
    float ch  = text_char_height();
    float row = ch + 2.0f;

    // Scroll so cursor stays visible
    float cursor_screen_y = e->smooth.vis_y - e->scroll_y;
    if (cursor_screen_y < 0)         e->scroll_y += cursor_screen_y - row;
    if (cursor_screen_y > h - row*2) e->scroll_y += (cursor_screen_y - (h - row*2));
    if (e->scroll_y < 0) e->scroll_y = 0;

    // Draw cursor (block in normal, line in insert)
    // vis_x already includes gutter offset (set in editor_update)
    float cx = x + e->smooth.vis_x;
    float cy = y + e->smooth.vis_y - e->scroll_y + (row - ch) * 0.5f;
    if (e->mode == MODE_INSERT)
        draw_rect(cx, cy, 2.0f, ch, COLOR_CURSOR_HIGHLIGHT, 0.9f);
    else if (e->mode == MODE_NORMAL)
        draw_rect(cx, cy, cw, ch, COLOR_CURSOR_HIGHLIGHT, 0.45f);
    else // VISUAL
        draw_rect(cx, cy, cw, ch, COLOR_CURSOR_HIGHLIGHT, 0.45f);

    // Render text line by line
    GapBuffer *gb = &e->gb;
    int n = gb->gap_start + (gb->total_size - gb->gap_end);
    char line[EDITOR_LINE_LEN];
    int  llen = 0;
    int  lrow = 0;
    float gutter = GUTTER_W(cw);
    float ty  = y - e->scroll_y;

    int sel_start = e->selection_start < e->selection_end ? e->selection_start : e->selection_end;
    int sel_end   = e->selection_start > e->selection_end ? e->selection_start : e->selection_end;
    int line_start_idx = 0;

    for (int i = 0; i <= n; i++) {
        char c = '\n';
        if (i < n) {
            int bi = (i < gb->gap_start) ? i : i + (gb->gap_end - gb->gap_start);
            if (bi < gb->total_size) c = gb->buffer[bi]; else c = '\n';
        }
        if (c == '\n' || i == n) {
            if (ty + row >= y && ty < y + h) {
                line[llen] = '\0';
                // Draw selection highlight for this line
                if (sel_start != sel_end) {
                    int hl_s = sel_start - line_start_idx;
                    int hl_e = sel_end   - line_start_idx;
                    if (hl_s < llen && hl_e > 0) {
                        if (hl_s < 0) hl_s = 0;
                        if (hl_e > llen) hl_e = llen;
                        draw_rect(x + gutter + hl_s * cw, ty + (row - ch) * 0.5f,
                                  (hl_e - hl_s) * cw, ch,
                                  0, 188/255.0f, 212/255.0f, 0.3f);
                    }
                }
                char lnum[16]; snprintf(lnum, sizeof(lnum), "%4d", lrow + 1);
                draw_text(lnum, x + 4, ty + ch * 0.85f, COLOR_TEXT);
                draw_text(line, x + gutter, ty + ch * 0.85f, COLOR_TEXT);
            }
            line_start_idx = i + 1;
            llen = 0; lrow++; ty += row;
            if (ty > y + h) break;
        } else {
            if (llen < EDITOR_LINE_LEN - 1) line[llen++] = c;
        }
    }

    // Status bar
    const char *mode_str = e->mode == MODE_INSERT ? "INSERT" :
                           e->mode == MODE_VISUAL ? "VISUAL" : "NORMAL";
    char status[640];
    snprintf(status, sizeof(status), " %s  %s  %d:%d",
             mode_str, e->filepath[0] ? e->filepath : "[No File]",
             e->cursor_row + 1, e->cursor_col + 1);
    draw_rect(x, y + h - 20.0f, w, 20.0f, COLOR_TOOLBAR);
    draw_text(status, x + 4, y + h - 4.0f, COLOR_TEXT);
}