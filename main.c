#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "colors.h"
#include "editor_state.h"
#include "text_renderer.h"
#include "file_panel.h"
#include "terminal.h"
#include "editor.h"

// ---- Globals ----------------------------------------------------------------
static Editor    g_editor;
static FilePanel g_fp;
static Terminal  g_term;

static int g_win_w = 1280, g_win_h = 720;
static int g_focus = 0; // 0=editor 1=terminal

static double g_last_time = 0.0;

// Simple open-folder dialog via Windows SHBrowseForFolder
static int open_folder_dialog(char *out, int out_len) {
    (void)out_len;
    BROWSEINFOA bi = {0};
    bi.lpszTitle  = "Select project folder";
    bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return 0;
    SHGetPathFromIDListA(pidl, out);
    CoTaskMemFree(pidl);
    return 1;
}

static int open_file_dialog(char *out, int out_len) {
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = out_len;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    out[0] = '\0';
    return GetOpenFileNameA(&ofn);
}

// ---- Callbacks --------------------------------------------------------------
static void cb_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win; (void)scancode;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // Tab switches focus between editor and terminal
        if (key == GLFW_KEY_TAB && (mods & GLFW_MOD_CONTROL)) {
            g_focus ^= 1; return;
        }
        if (g_focus == 0) editor_key(&g_editor, key, mods);
        else              term_key_input(&g_term, key);
    }
}

static void cb_char(GLFWwindow *win, unsigned int cp) {
    (void)win;
    if (g_focus == 0) editor_char(&g_editor, cp);
    else              term_char_input(&g_term, cp);
}

static void cb_resize(GLFWwindow *win, int w, int h) {
    (void)win;
    g_win_w = w; g_win_h = h;
    glViewport(0, 0, w, h);
    text_renderer_set_win_size(w, h);
}

static void cb_mouse_button(GLFWwindow *win, int button, int action, int mods) {
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);

    float toolbar_h = 32.0f;
    float sidebar_w = PANEL_SIDEBAR_W;
    float term_h    = PANEL_TERMINAL_H;
    float editor_y  = toolbar_h;
    float editor_h  = g_win_h - toolbar_h - term_h;

    // Toolbar buttons
    if (my < toolbar_h) {
        // Open Folder button: x=8..90
        if (mx >= 8 && mx <= 120) {
            char path[512] = "";
            if (open_folder_dialog(path, sizeof(path)))
                fp_open_dir(&g_fp, path);
        }
        // Open File button: x=98..180
        if (mx >= 98 && mx <= 224) {
            char path[512] = "";
            if (open_file_dialog(path, sizeof(path)))
                editor_open_file(&g_editor, path);
        }
        // Save button: x=188..240
        if (mx >= 188 && mx <= 292)
            editor_save_file(&g_editor);
        return;
    }

    // Terminal focus
    if (my > g_win_h - term_h) { g_focus = 1; return; }

    // Editor focus
    if (mx > sidebar_w) { g_focus = 0; return; }

    // Sidebar click
    const char *picked = fp_update(&g_fp, 0, editor_y, sidebar_w, editor_h,
                                   1, (float)mx, (float)my);
    if (picked) editor_open_file(&g_editor, picked);
}

// ---- Toolbar ----------------------------------------------------------------
static void draw_toolbar(float w) {
    draw_rect(0, 0, w, 32.0f, COLOR_TOOLBAR);

    struct { float x; float bw; const char *label; } btns[] = {
        {8,   112.0f, "Open Folder"},
        {128, 96.0f,  "Open File"},
        {232, 60.0f,  "Save"},
    };
    for (int i = 0; i < 3; i++) {
        draw_rect(btns[i].x, 4, btns[i].bw, 24.0f, COLOR_BUTTON, 0.5f);
        draw_text(btns[i].label, btns[i].x + 8, 20.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---- Main -------------------------------------------------------------------
int main(void) {
    if (!glfwInit()) return -1;

    GLFWwindow *win = glfwCreateWindow(g_win_w, g_win_h, "Editor", NULL, NULL);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    text_renderer_init("C:/Windows/Fonts/consola.ttf", 16.0f);
    text_renderer_set_win_size(g_win_w, g_win_h);

    editor_init(&g_editor);
    fp_init(&g_fp);

    // Open current dir in sidebar by default
    char cwd[512];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd))
        fp_open_dir(&g_fp, cwd);

    term_init(&g_term);

    glfwSetKeyCallback(win, cb_key);
    glfwSetCharCallback(win, cb_char);
    glfwSetFramebufferSizeCallback(win, cb_resize);
    glfwSetMouseButtonCallback(win, cb_mouse_button);

    g_last_time = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float  dt  = (float)(now - g_last_time);
        g_last_time = now;

        glClearColor(0.14f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float toolbar_h = 32.0f;
        float sidebar_w = (float)PANEL_SIDEBAR_W;
        float term_h    = (float)PANEL_TERMINAL_H;
        float editor_y  = toolbar_h;
        float editor_h  = g_win_h - toolbar_h - term_h;
        float editor_w  = g_win_w - sidebar_w;

        draw_toolbar((float)g_win_w);

        // Sidebar divider
        fp_update(&g_fp, 0, editor_y, sidebar_w, editor_h, 0, 0, 0);

        // Divider line
        draw_rect(sidebar_w, editor_y, 1.0f, editor_h, 0.3f, 0.3f, 0.35f, 1.0f);

        // Editor
        editor_update(&g_editor, dt);
        editor_render(&g_editor, sidebar_w + 1.0f, editor_y, editor_w, editor_h);

        // Terminal
        term_poll_output(&g_term);
        term_render(&g_term, 0, g_win_h - term_h, (float)g_win_w, term_h);

        // Focus highlight on terminal border
        if (g_focus == 1)
            draw_rect(0, g_win_h - term_h, (float)g_win_w, 2.0f, 0.2f, 0.8f, 0.4f, 1.0f);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    term_free(&g_term);
    editor_free(&g_editor);
    text_renderer_free();
    glfwTerminate();
    return 0;
}
