#include "gap_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef NDEBUG
static void gb_check_invariants(const GapBuffer *gb) {
    assert(gb->buffer != NULL);
    assert(gb->total_size > 0);
    assert(gb->gap_start >= 0 && gb->gap_start <= gb->total_size);
    assert(gb->gap_end >= 0 && gb->gap_end <= gb->total_size);
    assert(gb->gap_start <= gb->gap_end);
}
#else
#define gb_check_invariants(gb) ((void)0)
#endif

void init_buffer(GapBuffer *gb, int size) {
    gb->buffer = malloc(size);
    if (!gb->buffer) { gb->total_size = 0; gb->gap_start = gb->gap_end = 0; return; }
    gb->total_size = size;
    gb->gap_start = 0;
    gb->gap_end = size;
    gb_check_invariants(gb);
}

static int resize_buffer(GapBuffer *gb) {
    gb_check_invariants(gb);
    int old_size = gb->total_size;
    int new_size = old_size * 2;
    char *new_buffer = malloc(new_size);
    if (!new_buffer) return 0;

    memcpy(new_buffer, gb->buffer, gb->gap_start);
    int post_gap_size = old_size - gb->gap_end;
    int new_gap_end = new_size - post_gap_size;
    memcpy(new_buffer + new_gap_end, gb->buffer + gb->gap_end, post_gap_size);

    free(gb->buffer);
    gb->buffer = new_buffer;
    gb->total_size = new_size;
    gb->gap_end = new_gap_end;
    gb_check_invariants(gb);
    return 1;
}

void insert_char(GapBuffer *gb, char c) {
    gb_check_invariants(gb);
    if (gb->gap_start == gb->gap_end)
        if (!resize_buffer(gb)) return;
    gb->buffer[gb->gap_start++] = c;
    gb_check_invariants(gb);
}

void delete_char(GapBuffer *gb) {
    gb_check_invariants(gb);
    if (gb->gap_start > 0)
        gb->gap_start--;
}

void delete_range(GapBuffer *gb, int start, int end) {
    gb_check_invariants(gb);
    if (start >= end) return;
    assert(start >= 0 && end <= gb->total_size - (gb->gap_end - gb->gap_start));
    move_cursor(gb, start);
    gb->gap_end += (end - start);
    gb_check_invariants(gb);
}

void move_cursor(GapBuffer *gb, int target_position) {
    gb_check_invariants(gb);
    assert(target_position >= 0);
    assert(target_position <= gb->total_size - (gb->gap_end - gb->gap_start));
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
    gb_check_invariants(gb);
}

