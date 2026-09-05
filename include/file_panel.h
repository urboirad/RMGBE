#ifndef FILE_PANEL_H
#define FILE_PANEL_H

#define FP_MAX_ENTRIES 512
#define FP_NAME_LEN    256

typedef struct {
    char  name[FP_NAME_LEN];
    char  full_path[FP_NAME_LEN * 2];
    int   is_dir;
    int   depth;
    int   expanded;   // for directories: 1 if expanded, 0 if collapsed
    int   parent_idx; // index of parent directory entry, -1 for root
} FileEntry;

typedef struct {
    char       root[FP_NAME_LEN * 2];
    FileEntry  entries[FP_MAX_ENTRIES];
    int        count;
    int        selected;
    float      scroll;
} FilePanel;

void fp_init(FilePanel *fp);
void fp_open_dir(FilePanel *fp, const char *path);
// Returns the clicked file path, or NULL if a directory was toggled open/closed.
const char *fp_handle_click(FilePanel *fp, float mouse_y, float panel_y, float row_h);
void fp_render(FilePanel *fp, float x, float y, float w, float h);
// Call every frame. Returns a file path if the user double-clicked a file.
const char *fp_update(FilePanel *fp, float x, float y, float w, float h,
                      int mouse_clicked, float mouse_px, float mouse_py);

#endif
