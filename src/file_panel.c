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

static void scan_dir(FilePanel *fp, const char *path, int depth, int parent_idx) {
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
        FileEntry *e = &fp->entries[fp->count];
        int idx = fp->count;
        fp->count++;
        strncpy(e->name, fd.cFileName, FP_NAME_LEN - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s\\%s", path, fd.cFileName);
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->depth  = depth;
        e->expanded = 0;
        e->parent_idx = parent_idx;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (fp->count >= FP_MAX_ENTRIES) break;

        FileEntry *e = &fp->entries[fp->count];
        int idx = fp->count;
        fp->count++;
        strncpy(e->name, ent->d_name, FP_NAME_LEN - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s/%s", path, ent->d_name);
        e->depth = depth;
        e->expanded = 0;
        e->parent_idx = parent_idx;

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
    scan_dir(fp, path, 0, -1);
}

static int is_visible(FilePanel *fp, int idx) {
    int cur = fp->entries[idx].parent_idx;
    while (cur >= 0) {
        if (!fp->entries[cur].expanded) return 0;
        cur = fp->entries[cur].parent_idx;
    }
    return 1;
}

static int is_descendant_of(FileEntry *entries, int idx, int ancestor) {
    int cur = entries[idx].parent_idx;
    int depth = 0;
    while (cur >= 0 && depth < 256) {
        if (cur == ancestor) return 1;
        cur = entries[cur].parent_idx;
        depth++;
    }
    return 0;
}

static void collapse_dir(FilePanel *fp, int idx) {
    FileEntry *e = &fp->entries[idx];
    if (!e->is_dir || !e->expanded) return;

    // Remove ALL descendants (not just direct children)
    int remove_start = idx + 1;
    int remove_count = 0;
    for (int i = remove_start; i < fp->count; i++) {
        if (is_descendant_of(fp->entries, i, idx))
            remove_count++;
        else
            break;
    }
    if (remove_count > 0) {
        memmove(&fp->entries[remove_start],
                &fp->entries[remove_start + remove_count],
                (fp->count - remove_start - remove_count) * sizeof(FileEntry));
        fp->count -= remove_count;
        // Fix all parent_idx references for shifted entries
        for (int i = 0; i < fp->count; i++) {
            int p = fp->entries[i].parent_idx;
            if (p < 0) continue;
            if (p >= remove_start && p < remove_start + remove_count) {
                // Parent was removed — this shouldn't happen for properly-structured data
                // but handle gracefully
                fp->entries[i].parent_idx = -1;
                fp->entries[i].depth = 0;
            } else if (p >= remove_start + remove_count) {
                fp->entries[i].parent_idx = p - remove_count;
            }
        }
        if (fp->selected >= remove_start && fp->selected < remove_start + remove_count)
            fp->selected = -1;
        else if (fp->selected >= remove_start)
            fp->selected -= remove_count;
    }
    e->expanded = 0;
}

static void expand_dir(FilePanel *fp, int idx) {
    FileEntry *e = &fp->entries[idx];
    if (!e->is_dir || e->expanded) return;

    // Scan children into temp buffer
    FileEntry temp[256];
    int old_count = fp->count;
    scan_dir(fp, e->full_path, e->depth + 1, idx);
    int added = fp->count - old_count;
    if (added <= 0) { e->expanded = 1; return; }

    // Copy new entries to temp, restore count
    memcpy(temp, &fp->entries[old_count], added * sizeof(FileEntry));
    fp->count = old_count;

    // Make room at insert_at
    int insert_at = idx + 1;
    memmove(&fp->entries[insert_at + added],
            &fp->entries[insert_at],
            (fp->count - insert_at) * sizeof(FileEntry));
    fp->count += added;

    // Fix parent_idx for entries that shifted right (skip new entries)
    for (int i = 0; i < fp->count; i++) {
        // Skip the new entry slots [insert_at, insert_at+added)
        if (i >= insert_at && i < insert_at + added) continue;
        int p = fp->entries[i].parent_idx;
        if (p >= insert_at) {
            fp->entries[i].parent_idx = p + added;
        }
    }

    // Copy new entries into position (AFTER fixing shifted entries)
    memcpy(&fp->entries[insert_at], temp, added * sizeof(FileEntry));
    e->expanded = 1;
}

static void toggle_dir(FilePanel *fp, int idx) {
    FileEntry *e = &fp->entries[idx];
    if (!e->is_dir) return;
    if (e->expanded)
        collapse_dir(fp, idx);
    else
        expand_dir(fp, idx);
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
        if (!is_visible(fp, i)) continue;
        if (ty + row < y || ty > y + h) { ty += row; continue; }
        FileEntry *e = &fp->entries[i];

        if (i == fp->selected)
            draw_rect(x, ty, w, row, 0.2f, 0.4f, 0.6f, 0.5f);

        char label[FP_NAME_LEN * 2 + 8];
        char indent[32] = "";
        for (int d = 0; d < e->depth && d < 8; d++) strcat(indent, "  ");
        const char *arrow = e->is_dir ? (e->expanded ? "▾ " : "▸ ") : "  ";
        snprintf(label, sizeof(label), "%s%s%s", indent, arrow, e->name);

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
        if (!is_visible(fp, i)) continue;
        if (my >= ty && my < ty + row) {
            fp->selected = i;
            FileEntry *e = &fp->entries[i];
            if (e->is_dir) {
                toggle_dir(fp, i);
                return NULL;
            }
            return e->full_path;
        }
        ty += row;
    }
    return NULL;
}
