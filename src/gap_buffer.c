#include "gap_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_buffer(GapBuffer *gb, int size) {
    gb->buffer = malloc(size);
    gb->total_size = size;
    gb->gap_start = 0;
    gb->gap_end = size;
}

static void resize_buffer(GapBuffer *gb) {
    int old_size = gb->total_size;
    int new_size = old_size * 2;
    char *new_buffer = malloc(new_size);

    memcpy(new_buffer, gb->buffer, gb->gap_start);
    int post_gap_size = old_size - gb->gap_end;
    int new_gap_end = new_size - post_gap_size;
    memcpy(new_buffer + new_gap_end, gb->buffer + gb->gap_end, post_gap_size);

    free(gb->buffer);
    gb->buffer = new_buffer;
    gb->total_size = new_size;
    gb->gap_end = new_gap_end;
}

void insert_char(GapBuffer *gb, char c) {
    if (gb->gap_start == gb->gap_end)
        resize_buffer(gb);
    gb->buffer[gb->gap_start++] = c;
}

void delete_char(GapBuffer *gb) {
    if (gb->gap_start > 0)
        gb->gap_start--;
}

void delete_range(GapBuffer *gb, int start, int end) {
    if (start >= end) return;
    move_cursor(gb, start);
    gb->gap_end += (end - start);
}

void move_cursor(GapBuffer *gb, int target_position) {
    while (gb->gap_start > target_position) {
        gb->gap_start--;
        gb->gap_end--;
        gb->buffer[gb->gap_end] = gb->buffer[gb->gap_start];
    }
    while (gb->gap_start < target_position) {
        gb->buffer[gb->gap_start] = gb->buffer[gb->gap_end];
        gb->gap_start++;
        gb->gap_end++;
    }
}

void render_buffer(GapBuffer *gb) {
    for (int i = 0; i < gb->gap_start; i++)
        putchar(gb->buffer[i]);
    for (int i = gb->gap_end; i < gb->total_size; i++)
        putchar(gb->buffer[i]);
}
