#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

static void set_err(char *err, int err_size, const char *fmt, const char *a, const char *b) {
    if (!err || err_size <= 0) return;
    if (b) snprintf(err, (size_t)err_size, fmt, a, b);
    else   snprintf(err, (size_t)err_size, fmt, a);
}

int atomic_file_open(AtomicFile *af, const char *path, char *err, int err_size) {
    if (!af || !path || !path[0]) {
        set_err(err, err_size, "%s", "Invalid file path", NULL);
        return 0;
    }
    memset(af, 0, sizeof(*af));
    if (strlen(path) >= sizeof(af->target_path)) {
        set_err(err, err_size, "%s", "File path is too long", NULL);
        return 0;
    }
    snprintf(af->target_path, sizeof(af->target_path), "%s", path);

    /* Unique temp name in the destination directory.  Using a hidden prefix
     * keeps failed writes out of the way on POSIX. */
#ifdef _WIN32
    unsigned long pid = (unsigned long)_getpid();
    int temp_n = snprintf(af->temp_path, sizeof(af->temp_path),
                          "%s.tmp.%lu", path, pid);
#else
    int temp_n = snprintf(af->temp_path, sizeof(af->temp_path),
                          "%s.tmp.%ld", path, (long)getpid());
#endif
    if (temp_n < 0 || temp_n >= (int)sizeof(af->temp_path)) {
        set_err(err, err_size, "%s", "File path is too long", NULL);
        af->temp_path[0] = '\0';
        return 0;
    }

    af->fp = fopen(af->temp_path, "wb");
    if (!af->fp) {
        set_err(err, err_size, "Cannot write '%s': %s", path, strerror(errno));
        af->temp_path[0] = '\0';
        return 0;
    }
    af->open = 1;
    return 1;
}

int atomic_file_write(AtomicFile *af, const void *data, size_t len) {
    if (!af || !af->open || !af->fp || (!data && len > 0)) return 0;
    if (len == 0) return 1;
    return fwrite(data, 1, len, af->fp) == len;
}

int atomic_file_commit(AtomicFile *af, char *err, int err_size) {
    if (!af || !af->open || !af->fp) {
        set_err(err, err_size, "%s", "No file is open", NULL);
        return 0;
    }

    int ok = (fflush(af->fp) == 0);
#ifndef _WIN32
    if (ok) ok = (fsync(fileno(af->fp)) == 0);
#else
    if (ok) ok = FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(af->fp))) != 0;
#endif
    if (fclose(af->fp) != 0) ok = 0;
    af->fp = NULL;
    af->open = 0;

    if (!ok) {
        remove(af->temp_path);
        set_err(err, err_size, "Failed to finish writing '%s': %s",
                af->target_path, strerror(errno));
        af->temp_path[0] = '\0';
        return 0;
    }

#ifdef _WIN32
    if (!MoveFileExA(af->temp_path, af->target_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(af->temp_path);
        set_err(err, err_size, "Cannot replace '%s'", af->target_path, NULL);
        af->temp_path[0] = '\0';
        return 0;
    }
#else
    if (rename(af->temp_path, af->target_path) != 0) {
        remove(af->temp_path);
        set_err(err, err_size, "Cannot replace '%s': %s",
                af->target_path, strerror(errno));
        af->temp_path[0] = '\0';
        return 0;
    }
    /* Best-effort directory sync so the rename itself survives a crash. */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", af->target_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        if (slash == dir) slash[1] = '\0';
        else *slash = '\0';
        int dfd = open(dir, O_RDONLY);
        if (dfd >= 0) { fsync(dfd); close(dfd); }
    }
#endif

    af->temp_path[0] = '\0';
    if (err && err_size > 0) err[0] = '\0';
    return 1;
}

void atomic_file_abort(AtomicFile *af) {
    if (!af) return;
    if (af->fp) fclose(af->fp);
    if (af->open && af->temp_path[0]) remove(af->temp_path);
    af->fp = NULL;
    af->open = 0;
    af->temp_path[0] = '\0';
}

int atomic_write_all(const char *path, const void *data, size_t len,
                     char *err, int err_size) {
    AtomicFile af;
    if (!atomic_file_open(&af, path, err, err_size)) return 0;
    if (!atomic_file_write(&af, data, len)) {
        set_err(err, err_size, "Failed to write '%s': %s", path, strerror(errno));
        atomic_file_abort(&af);
        return 0;
    }
    return atomic_file_commit(&af, err, err_size);
}
