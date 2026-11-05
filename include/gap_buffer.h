#ifndef GAP_BUFFER_H
#define GAP_BUFFER_H

#include <stddef.h>

/* Hard ceiling for the byte buffer (256 MiB).  Kept well below INT_MAX so
 * offset arithmetic remains safe on all platforms. */
#define GB_MAX_SIZE (256 * 1024 * 1024)
#define GB_MIN_CAPACITY 64

typedef struct {
    char *buffer;
    int total_size;   /* physical capacity including the gap */
    int gap_start;    /* logical offset of the gap */
    int gap_end;      /* physical offset of the gap */
} GapBuffer;

/* Initialize with an initial capacity.  Returns 1 on success, 0 on failure
 * (the struct is left in a safe empty state). */
int init_buffer(GapBuffer *gb, int size);

/* Logical length (bytes stored, excluding the gap). */
int gb_length(const GapBuffer *gb);

/* Read a logical byte.  Returns '\0' when out of range. */
char gb_char_at(const GapBuffer *gb, int pos);

/* Insert/delete.  All functions return 1 on success, 0 on failure. */
int insert_char(GapBuffer *gb, char c);
int gb_insert_text(GapBuffer *gb, const char *text, int len);
int delete_char(GapBuffer *gb);
int delete_range(GapBuffer *gb, int start, int end);

/* Move the gap so the insertion point is at target_position.  The position is
 * clamped to the valid range.  Returns the final position. */
int move_cursor(GapBuffer *gb, int target_position);

/* Copy [start,end) into out (NUL terminated when out is large enough).
 * Returns the number of bytes copied. */
int gb_copy_range(GapBuffer *gb, int start, int end, char *out, int out_size);

#endif
