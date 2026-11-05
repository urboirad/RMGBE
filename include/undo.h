#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>

/* Delta-based undo: each entry stores only the text touched by one operation.
 * Storage is capped so a long editing session cannot grow without bound. */
#define UNDO_MAX_ENTRY_BYTES (1024 * 1024)
#define UNDO_MEMORY_LIMIT    (8 * 1024 * 1024)

typedef enum {
    UNDO_INSERT,
    UNDO_DELETE
} UndoType;

typedef struct {
    UndoType type;
    int      pos;        /* logical buffer offset before the operation */
    int      len;        /* byte length of `text` */
    char    *text;       /* owned, NUL terminated */
    int      cursor_row;
    int      cursor_col;
} UndoEntry;

typedef struct {
    UndoEntry *entries;
    int        count;
    int        cap;
    size_t     bytes;        /* sum of len + 1 for all entries */
    size_t     memory_limit; /* 0 means UNDO_MEMORY_LIMIT */
} UndoStack;

void undo_stack_init(UndoStack *s);
void undo_stack_free(UndoStack *s);

/* Push a copy of `text` (len bytes).  Oldest entries are discarded when the
 * memory limit is reached.  Returns 1 on success. */
int undo_stack_push(UndoStack *s, UndoType type, int pos,
                    const char *text, int len, int cursor_row, int cursor_col);

/* Pop the newest entry, transferring ownership of entry->text to the caller.
 * Returns 1 on success; free entry->text when done. */
int undo_stack_pop(UndoStack *s, UndoEntry *out);

void undo_entry_free(UndoEntry *entry);

#endif
