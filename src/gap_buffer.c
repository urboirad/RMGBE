#include "gap_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef NDEBUG
static void gb_check_invariants(const GapBuffer *gb) {
    assert(gb != NULL);
    if (!gb->buffer) {
        assert(gb->total_size == 0);
        assert(gb->gap_start == 0);
        assert(gb->gap_end == 0);
        return;
    }
    assert(gb->total_size > 0);
    assert(gb->gap_start >= 0 && gb->gap_start <= gb->total_size);
    assert(gb->gap_end >= 0 && gb->gap_end <= gb->total_size);
    assert(gb->gap_start <= gb->gap_end);
}
#else
#define gb_check_invariants(gb) ((void)0)
#endif

int gb_length(const GapBuffer *gb) {
    if (!gb || !gb->buffer) return 0;
    return gb->gap_start + (gb->total_size - gb->gap_end);
}

int init_buffer(GapBuffer *gb, int size) {
    if (!gb) return 0;
    gb->buffer = NULL;
    gb->total_size = 0;
    gb->gap_start = 0;
    gb->gap_end = 0;

    if (size < GB_MIN_CAPACITY) size = GB_MIN_CAPACITY;
    if (size > GB_MAX_SIZE) size = GB_MAX_SIZE;

    gb->buffer = malloc((size_t)size);
    if (!gb->buffer) return 0;
    gb->total_size = size;
    gb->gap_end = size;
    gb_check_invariants(gb);
    return 1;
}

/* Grow the buffer geometrically.  `needed` is the number of extra bytes that
 * must fit after the gap is filled.  Returns 1 on success. */
static int resize_buffer(GapBuffer *gb, int needed) {
    if (!gb || !gb->buffer || needed <= 0) return 0;

    /* Growing the physical buffer also grows the gap by exactly the number of
     * bytes added, so the gap after resizing is
     *     old_gap + (new_size - old_size).
     * Grow geometrically until that is at least `needed`. */
    int old_size = gb->total_size;
    int old_gap = gb->gap_end - gb->gap_start;
    int deficit = needed - old_gap;
    if (deficit <= 0) return 1;

    long long required = (long long)old_size + deficit;
    if (required > GB_MAX_SIZE) return 0;

    int new_size = old_size;
    while ((long long)new_size < required) {
        if (new_size > GB_MAX_SIZE / 2) {
            new_size = GB_MAX_SIZE;
        } else {
            new_size *= 2;
        }
        if (new_size <= old_size) return 0;
    }
    if (new_size == old_size) return 1;

    char *new_buffer = malloc((size_t)new_size);
    if (!new_buffer) return 0;

    memcpy(new_buffer, gb->buffer, (size_t)gb->gap_start);
    int post_gap_size = old_size - gb->gap_end;
    int new_gap_end = new_size - post_gap_size;
    memcpy(new_buffer + new_gap_end, gb->buffer + gb->gap_end, (size_t)post_gap_size);

    free(gb->buffer);
    gb->buffer = new_buffer;
    gb->total_size = new_size;
    gb->gap_end = new_gap_end;
    gb_check_invariants(gb);
    return 1;
}

int insert_char(GapBuffer *gb, char c) {
    return gb_insert_text(gb, &c, 1);
}

int gb_insert_text(GapBuffer *gb, const char *text, int len) {
    gb_check_invariants(gb);
    if (!gb || !gb->buffer || !text || len <= 0) return len == 0 ? 1 : 0;

    int gap = gb->gap_end - gb->gap_start;
    if (len > gap) {
        if (!resize_buffer(gb, len)) return 0;
    }
    memcpy(gb->buffer + gb->gap_start, text, (size_t)len);
    gb->gap_start += len;
    gb_check_invariants(gb);
    return 1;
}

int delete_char(GapBuffer *gb) {
    gb_check_invariants(gb);
    if (!gb || gb->gap_start <= 0) return 0;
    gb->gap_start--;
    gb_check_invariants(gb);
    return 1;
}

int delete_range(GapBuffer *gb, int start, int end) {
    gb_check_invariants(gb);
    if (!gb) return 0;
    int n = gb_length(gb);
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (start >= end) return 1;

    move_cursor(gb, start);
    gb->gap_end += (end - start);
    if (gb->gap_end > gb->total_size) gb->gap_end = gb->total_size;
    gb_check_invariants(gb);
    return 1;
}

int move_cursor(GapBuffer *gb, int target_position) {
    gb_check_invariants(gb);
    if (!gb || !gb->buffer) return 0;

    int n = gb_length(gb);
    if (target_position < 0) target_position = 0;
    if (target_position > n) target_position = n;

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
    return gb->gap_start;
}

char gb_char_at(const GapBuffer *gb, int pos) {
    if (!gb || !gb->buffer || pos < 0) return '\0';
    int n = gb_length(gb);
    if (pos >= n) return '\0';
    int phys = pos < gb->gap_start ? pos : pos + (gb->gap_end - gb->gap_start);
    if (phys < 0 || phys >= gb->total_size) return '\0';
    return gb->buffer[phys];
}

int gb_copy_range(GapBuffer *gb, int start, int end, char *out, int out_size) {
    if (!gb || !out || out_size <= 0) return 0;
    int n = gb_length(gb);
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (start >= end) { out[0] = '\0'; return 0; }

    int len = end - start;
    int copy = len < out_size - 1 ? len : out_size - 1;
    for (int i = 0; i < copy; i++) out[i] = gb_char_at(gb, start + i);
    out[copy] = '\0';
    return copy;
}
