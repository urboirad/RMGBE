#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>

/* Atomic file replacement: write to a temporary file in the same directory,
 * fsync/flush it, then rename it over the destination.  A crash leaves either
 * the old file or the complete new file, never a truncated one. */
typedef struct {
    FILE       *fp;
    char        temp_path[1024];
    char        target_path[1024];
    int         open;
} AtomicFile;

int  atomic_file_open(AtomicFile *af, const char *path, char *err, int err_size);
int  atomic_file_write(AtomicFile *af, const void *data, size_t len);
int  atomic_file_commit(AtomicFile *af, char *err, int err_size);
void atomic_file_abort(AtomicFile *af);

/* Convenience wrapper for small complete buffers. */
int atomic_write_all(const char *path, const void *data, size_t len,
                     char *err, int err_size);

#endif
