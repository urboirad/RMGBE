#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#define PANEL_SIDEBAR_W   220
#define PANEL_TERMINAL_H  180
#define STATUS_BAR_H       22

typedef enum { MODE_NORMAL, MODE_INSERT, MODE_VISUAL } EditorMode;

typedef struct {
    float x, y;        // logical (char col/row)
    float vis_x, vis_y; // smoothed pixel position for rendering
} SmoothedCursor;

#endif
