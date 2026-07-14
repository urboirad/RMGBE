#ifndef GAP_BUFFER_H
#define GAP_BUFFER_H

typedef struct {
    char *buffer;
    int total_size;
    int gap_start;
    int gap_end;
} GapBuffer;

void init_buffer(GapBuffer *gb, int size);
void insert_char(GapBuffer *gb, char c);
void delete_char(GapBuffer *gb);
void delete_range(GapBuffer *gb, int start, int end);
void move_cursor(GapBuffer *gb, int target_position);
void render_buffer(GapBuffer *gb);

#endif
