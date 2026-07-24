#include "file_panel.h"
#include "text_renderer.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

void fp_init(FilePanel *fp) {
    fp->count    = 0;
    fp->selected = -1;
    fp->scroll   = 0.0f;
    fp->root[0]  = '\0';
}

static void scan_dir(FilePanel *fp, const char *path, int depth) {
    if (fp->count >= FP_MAX_ENTRIES) return;

#ifdef _WIN32
    char pattern[FP_NAME_LEN * 2 + 4];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (fp->count >= FP_MAX_ENTRIES) break;
        FileEntry *e = &fp->entries[fp->count++];
        strncpy(e->name, fd.cFileName, FP_NAME_LEN - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s\\%s", path, fd.cFileName);
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->depth  = depth;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (fp->count >= FP_MAX_ENTRIES) break;

        FileEntry *e = &fp->entries[fp->count++];
        strncpy(e->name, ent->d_name, FP_NAME_LEN - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s/%s", path, ent->d_name);
        e->depth = depth;

        struct stat st;
        if (stat(e->full_path, &st) == 0)
            e->is_dir = S_ISDIR(st.st_mode);
        else
            e->is_dir = 0;
    }
    closedir(dir);
#endif
}

void fp_open_dir(FilePanel *fp, const char *path) {
    strncpy(fp->root, path, sizeof(fp->root) - 1);
    fp->count    = 0;
    fp->selected = -1;
    scan_dir(fp, path, 0);
}

void fp_render(FilePanel *fp, float x, float y, float w, float h) {
    // Background
    draw_rect(x, y, w, h, 0.12f, 0.12f, 0.15f, 1.0f);

    float ch = text_char_height();
    float row = ch + 4.0f;
    float ty  = y + 4.0f - fp->scroll;

    // Title
    draw_text("FILES", x + 6, ty + ch * 0.85f, 0.5f, 0.8f, 1.0f);
    ty += row + 4.0f;

    for (int i = 0; i < fp->count; i++) {
        if (ty + row < y || ty > y + h) { ty += row; continue; }
        FileEntry *e = &fp->entries[i];

        if (i == fp->selected)
            draw_rect(x, ty, w, row, 0.2f, 0.4f, 0.6f, 0.5f);

        char label[FP_NAME_LEN * 2 + 8];
        char indent[32] = "";
        for (int d = 0; d < e->depth && d < 8; d++) strcat(indent, "  ");
        snprintf(label, sizeof(label), "%s%s%s", indent, e->is_dir ? "▸ " : "  ", e->name);

        float fr = e->is_dir ? 0.9f : 0.85f;
        float fg = e->is_dir ? 0.75f : 0.85f;
        float fb = e->is_dir ? 0.3f  : 0.85f;
        draw_text(label, x + 6, ty + ch * 0.85f, fr, fg, fb);
        ty += row;
    }
}

const char *fp_update(FilePanel *fp, float x, float y, float w, float h,
                      int mouse_clicked, float mx, float my) {
    fp_render(fp, x, y, w, h);

    if (!mouse_clicked || mx < x || mx > x + w || my < y || my > y + h)
        return NULL;

    float ch  = text_char_height();
    float row = ch + 4.0f;
    float ty  = y + 4.0f - fp->scroll + row + 4.0f; // start below the "FILES" title

    for (int i = 0; i < fp->count; i++) {
        if (my >= ty && my < ty + row) {
            fp->selected = i;
            FileEntry *e = &fp->entries[i];
            if (!e->is_dir) return e->full_path;
            return NULL;
        }
        ty += row;
    }
    return NULL;
}
