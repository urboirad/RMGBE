#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

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
#include "update.h"

// ---- Globals ----------------------------------------------------------------
static Editor    g_editor;
static FilePanel g_fp;
static Terminal  g_term;

static int g_win_w = 1280, g_win_h = 720;
// Which panel has focus: 0 = editor, 1 = terminal
static int g_focus = 0;

static double g_last_time = 0.0;
static float g_editor_x, g_editor_y;

// Popups
static int g_about_open = 0;
static int g_theme_open = 0;
static int g_update_open = 0;
static UpdateState g_update;

static void get_exe_dir(char *buf, int len) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, buf, len);
    // Remove the exe filename, keep just the directory
    char *last = strrchr(buf, '\\');
    if (last) *last = '\0';
#elif defined(__APPLE__)
    uint32_t size = len;
    _NSGetExecutablePath(buf, &size);
    char *last = strrchr(buf, '/');
    if (last) *last = '\0';
#else
    ssize_t n = readlink("/proc/self/exe", buf, len - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *last = strrchr(buf, '/');
        if (last) *last = '\0';
    } else {
        buf[0] = '\0';
    }
#endif
}

#ifdef _WIN32
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
#else
static int open_folder_dialog(char *out, int out_len) {
    (void)out_len;
    // Linux/Mac don't have a native folder picker, so just use current directory
    if (getcwd(out, out_len)) return 1;
    return 0;
}

static int open_file_dialog(char *out, int out_len) {
    (void)out_len;
    out[0] = '\0';
    return 0;
}
#endif

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
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);

    float toolbar_h = 32.0f;
    float sidebar_w = PANEL_SIDEBAR_W;
    float term_h    = PANEL_TERMINAL_H;
    float editor_y  = toolbar_h;
    float editor_h  = g_win_h - toolbar_h - term_h;

    if (action == GLFW_RELEASE) {
        editor_mouse_release(&g_editor);
        return;
    }

    // Close popups when clicking outside them or on their close button
    if (g_about_open) {
        float pw = 340.0f, ph = 160.0f;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;
        if (mx >= px + pw - 70 && mx <= px + pw - 14 && my >= py + ph - 32 && my <= py + ph - 8) {
            g_about_open = 0; return;
        }
        if (mx < px || mx > px + pw || my < py || my > py + ph) {
            g_about_open = 0; return;
        }
        return;
    }
    if (g_theme_open) {
        float pw = 360.0f, ph = 240.0f;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;
        if (mx >= px + pw - 70 && mx <= px + pw - 14 && my >= py + ph - 32 && my <= py + ph - 8) {
            g_theme_open = 0; return;
        }
        if (mx < px || mx > px + pw || my < py || my > py + ph) {
            g_theme_open = 0; return;
        }
        return;
    }
    if (g_update_open) {
        float pw = 400.0f, ph = 200.0f;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;
        // Close button
        if (mx >= px + pw - 70 && mx <= px + pw - 14 && my >= py + ph - 32 && my <= py + ph - 8) {
            g_update_open = 0; return;
        }
        // Download button (only when update is available)
        if (g_update.status == UPDATE_AVAILABLE) {
            float bx = px + pw * 0.5f - 60.0f, by = py + ph - 68.0f;
            if (mx >= bx && mx <= bx + 120 && my >= by && my <= by + 28) {
                char exe_path[1024];
                get_exe_dir(exe_path, sizeof(exe_path));
                // get_exe_dir returns the exe dir, we need the exe path itself
                // For update, we use the directory to place the new exe
                update_download_start(&g_update, exe_path);
                return;
            }
        }
        if (mx < px || mx > px + pw || my < py || my > py + ph) {
            g_update_open = 0; return;
        }
        return;
    }

    // Toolbar area
    if (my < toolbar_h) {
        if (mx >= 8 && mx <= 120) {
            char path[512] = "";
            if (open_folder_dialog(path, sizeof(path)))
                fp_open_dir(&g_fp, path);
        }
        if (mx >= 98 && mx <= 224) {
            char path[512] = "";
            if (open_file_dialog(path, sizeof(path)))
                editor_open_file(&g_editor, path);
        }
        if (mx >= 188 && mx <= 292)
            editor_save_file(&g_editor);
        if (mx >= 300 && mx <= 400)
            g_theme_open = 1;
        if (mx >= 408 && mx <= 468)
            g_about_open = 1;
        if (mx >= 450 && mx <= 560) {
            g_update_open = 1;
            update_check_start(&g_update);
        }
        return;
    }

    // Terminal focus
    if (my > g_win_h - term_h) { g_focus = 1; return; }

    // Editor area click
    if (mx > sidebar_w) {
        g_focus = 0;
        editor_mouse_press(&g_editor, (float)mx, (float)my, g_editor_x, g_editor_y);
        return;
    }

    // Sidebar click
    const char *picked = fp_update(&g_fp, 0, editor_y, sidebar_w, editor_h,
                                   1, (float)mx, (float)my);
    if (picked) editor_open_file(&g_editor, picked);
}

static void cb_cursor_pos(GLFWwindow *win, double mx, double my) {
    (void)win;
    editor_mouse_move(&g_editor, (float)mx, (float)my, g_editor_x, g_editor_y);
}

static void cb_scroll(GLFWwindow *win, double xoff, double yoff) {
    (void)win; (void)xoff;
    if (g_focus == 0) editor_scroll(&g_editor, (float)yoff);
}

// ---- Toolbar ----------------------------------------------------------------
static void draw_toolbar(float w) {
    draw_rect(0, 0, w, 32.0f, COLOR_TOOLBAR);

    struct { float x; float bw; const char *label; } btns[] = {
        {8,   112.0f, "Open Folder"},
        {128, 96.0f,  "Open File"},
        {232, 60.0f,  "Save"},
        {300, 70.0f, "Theme"},
        {380, 60.0f,  "About"},
        {450, 110.0f, "Check for Updates"},
    };
    for (int i = 0; i < 6; i++) {
        draw_rect(btns[i].x, 4, btns[i].bw, 24.0f, COLOR_BUTTON, 0.5f);
        draw_text(btns[i].label, btns[i].x + 8, 20.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---- Modals ----------------------------------------------------------------
static void draw_about(void) {
    if (!g_about_open) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = 340.0f, ph = 160.0f;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, 30/255.0f, 30/255.0f, 30/255.0f, 1.0f);
    draw_rect(px, py, pw, 1.0f, 0.4f, 0.4f, 0.45f, 1.0f);

    draw_text("Rick's Minimal Gap Buffer Editor", px + 16, py + 28.0f, COLOR_TEXT);
    draw_text("Version 0.1.0", px + 16, py + 54.0f, 0.7f, 0.7f, 0.7f);
    draw_text("---", px + 16, py + 76.0f, 0.7f, 0.7f, 0.7f);
    draw_text("no-ad technologies", px + 16, py + 98.0f, 0.7f, 0.7f, 0.7f);

    draw_rect(px + pw - 70, py + ph - 32, 56.0f, 24.0f, COLOR_BUTTON, 0.5f);
    draw_text("Close", px + pw - 58, py + ph - 14.0f, 1.0f, 1.0f, 1.0f);
}

static void draw_theme_editor(void) {
    if (!g_theme_open) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = 360.0f, ph = 240.0f;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, 30/255.0f, 30/255.0f, 30/255.0f, 1.0f);
    draw_rect(px, py, pw, 1.0f, 0.4f, 0.4f, 0.45f, 1.0f);

    draw_text("Theme Editor", px + 16, py + 28.0f, COLOR_TEXT);
    draw_text("Coming soon...", px + 16, py + 60.0f, 0.6f, 0.6f, 0.6f);

    draw_rect(px + pw - 70, py + ph - 32, 56.0f, 24.0f, COLOR_BUTTON, 0.5f);
    draw_text("Close", px + pw - 58, py + ph - 14.0f, 1.0f, 1.0f, 1.0f);
}

static void draw_update_popup(void) {
    if (!g_update_open) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = 400.0f, ph = 200.0f;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, 30/255.0f, 30/255.0f, 30/255.0f, 1.0f);
    draw_rect(px, py, pw, 1.0f, 0.4f, 0.4f, 0.45f, 1.0f);

    draw_text("Check for Updates", px + 16, py + 28.0f, COLOR_TEXT);

    char msg[512];
    switch (g_update.status) {
        case UPDATE_CHECKING:
            draw_text("Checking for updates...", px + 16, py + 60.0f, 0.7f, 0.7f, 0.7f);
            break;
        case UPDATE_AVAILABLE:
            snprintf(msg, sizeof(msg), "New version available: %s", g_update.latest_version);
            draw_text(msg, px + 16, py + 60.0f, 1.0f, 0.8f, 0.3f);
            draw_text("Click Download to update.", px + 16, py + 84.0f, 0.7f, 0.7f, 0.7f);
            // Download button
            draw_rect(px + pw * 0.5f - 60.0f, py + ph - 68.0f, 120.0f, 28.0f, COLOR_BUTTON, 0.5f);
            draw_text("Download", px + pw * 0.5f - 40.0f, py + ph - 50.0f, 1.0f, 1.0f, 1.0f);
            break;
        case UPDATE_UPTODATE:
            snprintf(msg, sizeof(msg), "You're up to date! (v%s)", UPDATE_CURRENT_VERSION);
            draw_text(msg, px + 16, py + 60.0f, 0.4f, 0.9f, 0.5f);
            break;
        case UPDATE_DOWNLOADING:
            draw_text("Downloading update...", px + 16, py + 60.0f, 0.7f, 0.7f, 0.7f);
            // Progress bar
            draw_rect(px + 16, py + 90.0f, pw - 32.0f, 12.0f, 0.2f, 0.2f, 0.2f, 1.0f);
            draw_rect(px + 16, py + 90.0f, (pw - 32.0f) * g_update.progress, 12.0f, 0.3f, 0.7f, 0.4f, 1.0f);
            break;
        case UPDATE_READY:
            draw_text("Update downloaded!", px + 16, py + 60.0f, 0.4f, 0.9f, 0.5f);
            draw_text("Restart RMGBE to apply the update.", px + 16, py + 84.0f, 0.7f, 0.7f, 0.7f);
            break;
        case UPDATE_FAILED:
            snprintf(msg, sizeof(msg), "Update check failed: %s", g_update.error_msg);
            draw_text("Update failed:", px + 16, py + 60.0f, 0.9f, 0.3f, 0.3f);
            draw_text(g_update.error_msg, px + 16, py + 84.0f, 0.7f, 0.7f, 0.7f);
            break;
        default:
            draw_text("Click Check for Updates to start.", px + 16, py + 60.0f, 0.7f, 0.7f, 0.7f);
            break;
    }

    draw_rect(px + pw - 70, py + ph - 32, 56.0f, 24.0f, COLOR_BUTTON, 0.5f);
    draw_text("Close", px + pw - 58, py + ph - 14.0f, 1.0f, 1.0f, 1.0f);
}

// ---- Main -------------------------------------------------------------------
int main(void) {
    if (!glfwInit()) return -1;

    GLFWwindow *win = glfwCreateWindow(g_win_w, g_win_h, "RMGBE", NULL, NULL);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    char font_path[1024];
    char exe_dir[512];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    snprintf(font_path, sizeof(font_path), "%s/../assets/FiraCode-Regular.ttf", exe_dir);
    text_renderer_init(font_path, 16.0f);
    text_renderer_set_win_size(g_win_w, g_win_h);

    editor_init(&g_editor);
    fp_init(&g_fp);

    // Clean up previous update backup
    {
        char old_path[1024];
        snprintf(old_path, sizeof(old_path), "%s\\RMGBE_old.exe", exe_dir);
        remove(old_path);
    }

    char cwd[512];
#ifdef _WIN32
    if (GetCurrentDirectoryA(sizeof(cwd), cwd))
#else
    if (getcwd(cwd, sizeof(cwd)))
#endif
        fp_open_dir(&g_fp, cwd);

    term_init(&g_term);

    glfwSetKeyCallback(win, cb_key);
    glfwSetCharCallback(win, cb_char);
    glfwSetFramebufferSizeCallback(win, cb_resize);
    glfwSetMouseButtonCallback(win, cb_mouse_button);
    glfwSetCursorPosCallback(win, cb_cursor_pos);
    glfwSetScrollCallback(win, cb_scroll);

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

        // Draw the sidebar file list
        fp_update(&g_fp, 0, editor_y, sidebar_w, editor_h, 0, 0, 0);

        // Line between sidebar and editor
        draw_rect(sidebar_w, editor_y, 1.0f, editor_h, 0.3f, 0.3f, 0.35f, 1.0f);

        // Editor
        g_editor_x = sidebar_w + 1.0f;
        g_editor_y = editor_y;
        editor_update(&g_editor, dt);
        editor_render(&g_editor, g_editor_x, g_editor_y, editor_w, editor_h);

        // Terminal
        term_poll_output(&g_term);
        term_render(&g_term, 0, g_win_h - term_h, (float)g_win_w, term_h);

        // Green border on terminal when it has focus
        if (g_focus == 1)
            draw_rect(0, g_win_h - term_h, (float)g_win_w, 2.0f, 0.2f, 0.8f, 0.4f, 1.0f);

        draw_about();
        draw_theme_editor();
        draw_update_popup();

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    term_free(&g_term);
    editor_free(&g_editor);
    text_renderer_free();
    glfwTerminate();
    return 0;
}
