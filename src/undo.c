#include "undo.h"
#include <stdlib.h>
#include <string.h>

void undo_stack_init(UndoStack *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->memory_limit = UNDO_MEMORY_LIMIT;
}

void undo_entry_free(UndoEntry *entry) {
    if (!entry) return;
    free(entry->text);
    entry->text = NULL;
    entry->len = 0;
}

void undo_stack_free(UndoStack *s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++)
        free(s->entries[i].text);
    free(s->entries);
    memset(s, 0, sizeof(*s));
    s->memory_limit = UNDO_MEMORY_LIMIT;
}

static void drop_oldest(UndoStack *s) {
    if (!s || s->count <= 0) return;
    s->bytes -= (size_t)s->entries[0].len + 1;
    free(s->entries[0].text);
    if (s->count > 1)
        memmove(&s->entries[0], &s->entries[1],
                (size_t)(s->count - 1) * sizeof(UndoEntry));
    s->count--;
}

int undo_stack_push(UndoStack *s, UndoType type, int pos,
                    const char *text, int len, int cursor_row, int cursor_col) {
    if (!s || !text || len <= 0 || len > UNDO_MAX_ENTRY_BYTES) return 0;

    size_t limit = s->memory_limit ? s->memory_limit : UNDO_MEMORY_LIMIT;
    size_t entry_bytes = (size_t)len + 1;
    if (entry_bytes > limit) return 0;

    char *copy = malloc(entry_bytes);
    if (!copy) return 0;
    memcpy(copy, text, (size_t)len);
    copy[len] = '\0';

    while (s->count > 0 && s->bytes + entry_bytes > limit)
        drop_oldest(s);
    if (s->bytes + entry_bytes > limit) {
        free(copy);
        return 0;
    }

    if (s->count == s->cap) {
        int new_cap = s->cap ? s->cap * 2 : 64;
        if (new_cap < s->cap + 1) new_cap = s->cap + 1;
        UndoEntry *new_entries = realloc(s->entries,
                                         (size_t)new_cap * sizeof(UndoEntry));
        if (!new_entries) { free(copy); return 0; }
        s->entries = new_entries;
        s->cap = new_cap;
    }

    UndoEntry *e = &s->entries[s->count++];
    e->type = type;
    e->pos = pos;
    e->len = len;
    e->text = copy;
    e->cursor_row = cursor_row;
    e->cursor_col = cursor_col;
    s->bytes += entry_bytes;
    return 1;
}

int undo_stack_pop(UndoStack *s, UndoEntry *out) {
    if (!s || !out || s->count <= 0) return 0;
    *out = s->entries[--s->count];
    s->entries[s->count].text = NULL;
    s->bytes -= (size_t)out->len + 1;
    return 1;
}
