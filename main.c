#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#else
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#include "tinyfiledialogs.h"
#include "startup_audio.h"
#include "GLFW/glfw3.h"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "GLFW/glfw3native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "colors.h"
#include "editor_state.h"
#include "text_renderer.h"
#include "file_panel.h"
#include "terminal.h"
#include "editor.h"
#include "version.h"
#include "theme.h"

// ---- Globals ----------------------------------------------------------------
static Editor    g_editor;
static FilePanel g_fp;
static Terminal  g_term;

static int g_win_w = 1280, g_win_h = 720;
// Which panel has focus: 0 = editor, 1 = terminal
static int g_focus = 0;

static double g_last_time = 0.0;
static float g_editor_x, g_editor_y;

// FPS counter
static int   g_fps_frame_count = 0;
static double g_fps_last_update = 0.0;
static float  g_fps_value = 0.0f;

// Popups
static int g_about_open = 0;
static int g_theme_open = 0;
static int g_reload_prompt = 0;  // hot-reload prompt visible

// Theme editor state
static int    g_theme_sel      = -1;   // selected color entry index, -1 = none
static int    g_theme_input_on = 0;    // text input focused
static char   g_theme_input[64];       // edit buffer ("#RRGGBB" or "R,G,B")
static int    g_theme_cursor;          // caret position in buffer
static char   g_theme_msg[128] = "";   // status message (load/save errors etc.)
static int    g_theme_font_input_on = 0; // font size input focused
static char   g_theme_font_input[32];    // font size edit buffer
static int    g_theme_font_cursor;       // caret position in font size buffer
static float  g_theme_list_scroll = 0;   // scroll offset for color list

// Theme modal layout (shared between draw + hit-testing)
#define THEME_PW     580.0f
#define THEME_PH     720.0f
#define THEME_LIST_Y  48.0f   // list top, relative to modal top
#define THEME_ROW_H   24.0f   // compact rows to fit more entries
#define THEME_LIST_MAX_H 400.0f  // max visible height for color list

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
    const char *result = tinyfd_selectFolderDialog("Open Folder", NULL);
    if (!result) { out[0] = '\0'; return 0; }
    strncpy(out, result, out_len - 1);
    out[out_len - 1] = '\0';
    return 1;
}

static int open_file_dialog(char *out, int out_len, const char *filter) {
    (void)filter;
    const char *result = tinyfd_openFileDialog("Open File", NULL, 0, NULL, NULL, 0);
    if (!result) { out[0] = '\0'; return 0; }
    strncpy(out, result, out_len - 1);
    out[out_len - 1] = '\0';
    return 1;
}

static int save_file_dialog(char *out, int out_len, const char *filter, const char *def_ext) {
    (void)filter; (void)def_ext;
    const char *result = tinyfd_saveFileDialog("Save As", NULL, 0, NULL, NULL);
    if (!result) { out[0] = '\0'; return 0; }
    strncpy(out, result, out_len - 1);
    out[out_len - 1] = '\0';
    return 1;
}

#else

static int has_dialog_backend = -1; // -1 = unchecked

static int check_dialog_backend(void) {
    if (has_dialog_backend >= 0) return has_dialog_backend;
    has_dialog_backend = 0;
    FILE *p = popen("which zenity 2>/dev/null || which kdialog 2>/dev/null || which yad 2>/dev/null || which matedialog 2>/dev/null || which xdg-open 2>/dev/null", "r");
    if (p) {
        char buf[8];
        if (fgets(buf, sizeof(buf), p))
            has_dialog_backend = 1;
        pclose(p);
    }
    // Also check if XDG portal is available via dbus
    if (!has_dialog_backend) {
        p = popen("which dbus-send 2>/dev/null && dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>/dev/null | grep -q org.freedesktop.portal && echo yes", "r");
        if (p) {
            char buf[8];
            if (fgets(buf, sizeof(buf), p))
                has_dialog_backend = 1;
            pclose(p);
        }
    }
    return has_dialog_backend;
}

static int run_popen_dialog(const char *cmd, char *out, int out_len) {
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    out[0] = '\0';
    if (fgets(out, out_len, p)) {
        size_t len = strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
    }
    int ok = pclose(p) == 0 && out[0] != '\0';
    if (!ok) out[0] = '\0';
    return ok;
}

static int open_folder_dialog(char *out, int out_len) {
    if (!check_dialog_backend()) {
        // No GUI dialog backend — just use current directory
        if (getcwd(out, out_len)) return 1;
        out[0] = '\0';
        return 0;
    }
    // Try tinyfiledialogs first — it has better error handling
    const char *result = tinyfd_selectFolderDialog("Open Folder", NULL);
    if (result && result[0]) {
        strncpy(out, result, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }
    out[0] = '\0';
    return 0;
}

static int open_file_dialog(char *out, int out_len, const char *filter) {
    (void)filter;
    if (!check_dialog_backend()) {
        out[0] = '\0';
        return 0;
    }
    const char *result = tinyfd_openFileDialog("Open File", NULL, 0, NULL, NULL, 0);
    if (result && result[0]) {
        strncpy(out, result, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }
    out[0] = '\0';
    return 0;
}

static int save_file_dialog(char *out, int out_len, const char *filter, const char *def_ext) {
    (void)filter; (void)def_ext;
    if (!check_dialog_backend()) {
        out[0] = '\0';
        return 0;
    }
    const char *result = tinyfd_saveFileDialog("Save As", NULL, 0, NULL, NULL);
    if (result && result[0]) {
        strncpy(out, result, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }
    out[0] = '\0';
    return 0;
}

#endif

// ---- Callbacks --------------------------------------------------------------
static void cb_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win; (void)scancode;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // Theme editor input capture
        if (g_theme_open) {
            if (g_theme_input_on) {
                int len = (int)strlen(g_theme_input);
                if (key == GLFW_KEY_ESCAPE) { g_theme_input_on = 0; return; }
                if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                    // Apply color
                    if (g_theme_sel >= 0) {
                        ThemeColor c;
                        if (theme_parse_color(g_theme_input, &c)) {
                            *theme_entry_by_index(g_theme_sel).color = c;
                            g_theme_msg[0] = '\0';
                            theme_save_persisted();
                        } else {
                            snprintf(g_theme_msg, sizeof(g_theme_msg), "Bad color value");
                        }
                    }
                    g_theme_input_on = 0;
                    return;
                }
                if (key == GLFW_KEY_BACKSPACE && g_theme_cursor > 0) {
                    memmove(g_theme_input + g_theme_cursor - 1,
                            g_theme_input + g_theme_cursor,
                            len - g_theme_cursor + 1);
                    g_theme_cursor--;
                    return;
                }
                if (key == GLFW_KEY_DELETE && g_theme_cursor < len) {
                    memmove(g_theme_input + g_theme_cursor,
                            g_theme_input + g_theme_cursor + 1,
                            len - g_theme_cursor);
                    return;
                }
                if (key == GLFW_KEY_LEFT  && g_theme_cursor > 0)     { g_theme_cursor--; return; }
                if (key == GLFW_KEY_RIGHT && g_theme_cursor < len)   { g_theme_cursor++; return; }
                if (key == GLFW_KEY_HOME) { g_theme_cursor = 0;   return; }
                if (key == GLFW_KEY_END)  { g_theme_cursor = len; return; }
            }
            if (g_theme_font_input_on) {
                int len = (int)strlen(g_theme_font_input);
                if (key == GLFW_KEY_ESCAPE) { g_theme_font_input_on = 0; return; }
                if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                    float sz = 0;
                    if (sscanf(g_theme_font_input, "%f", &sz) == 1 && sz >= 6.0f && sz <= 72.0f) {
                        g_theme.font_size = sz;
                        theme_apply_font();
                        theme_save_persisted();
                        g_theme_msg[0] = '\0';
                    } else {
                        snprintf(g_theme_msg, sizeof(g_theme_msg), "Font size must be 6-72");
                    }
                    g_theme_font_input_on = 0;
                    return;
                }
                if (key == GLFW_KEY_BACKSPACE && g_theme_font_cursor > 0) {
                    memmove(g_theme_font_input + g_theme_font_cursor - 1,
                            g_theme_font_input + g_theme_font_cursor,
                            len - g_theme_font_cursor + 1);
                    g_theme_font_cursor--;
                    return;
                }
                if (key == GLFW_KEY_DELETE && g_theme_font_cursor < len) {
                    memmove(g_theme_font_input + g_theme_font_cursor,
                            g_theme_font_input + g_theme_font_cursor + 1,
                            len - g_theme_font_cursor);
                    return;
                }
                if (key == GLFW_KEY_LEFT  && g_theme_font_cursor > 0)     { g_theme_font_cursor--; return; }
                if (key == GLFW_KEY_RIGHT && g_theme_font_cursor < len)   { g_theme_font_cursor++; return; }
                if (key == GLFW_KEY_HOME) { g_theme_font_cursor = 0;   return; }
                if (key == GLFW_KEY_END)  { g_theme_font_cursor = len; return; }
            }
            return; // swallow all other keys while modal is open
        }
        if (g_reload_prompt) {
            if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER) {
                if (key == GLFW_KEY_ENTER)
                    editor_reload_file(&g_editor);
                else
                    editor_dismiss_external_change(&g_editor);
                g_reload_prompt = 0;
            }
            return;
        }

        // Tab switches focus between editor and terminal
        if (key == GLFW_KEY_TAB && (mods & GLFW_MOD_CONTROL)) {
            g_focus ^= 1; return;
        }
        if (g_focus == 0) {
            // Ctrl+S: Save As if untitled, otherwise normal save
            if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_S && !g_editor.filepath[0]) {
                char path[512] = "";
                if (save_file_dialog(path, sizeof(path), NULL, NULL))
                    editor_save_file_as(&g_editor, path);
            } else {
                editor_key(&g_editor, key, mods);
            }
        }
        else              term_key_input(&g_term, key);
    }
}

static void cb_char(GLFWwindow *win, unsigned int cp) {
    (void)win;
    if (g_theme_open) {
        if (g_theme_input_on && cp >= 32 && cp < 127) {
            int len = (int)strlen(g_theme_input);
            if (len < (int)sizeof(g_theme_input) - 1 && g_theme_cursor <= len) {
                memmove(g_theme_input + g_theme_cursor + 1,
                        g_theme_input + g_theme_cursor,
                        len - g_theme_cursor + 1);
                g_theme_input[g_theme_cursor++] = (char)cp;
                g_theme_input[len + 1] = '\0';
            }
        }
        if (g_theme_font_input_on && cp >= 32 && cp < 127) {
            // Only accept digits and one decimal point
            if ((cp >= '0' && cp <= '9') || (cp == '.' && !strchr(g_theme_font_input, '.'))) {
                int len = (int)strlen(g_theme_font_input);
                if (len < (int)sizeof(g_theme_font_input) - 1 && g_theme_font_cursor <= len) {
                    memmove(g_theme_font_input + g_theme_font_cursor + 1,
                            g_theme_font_input + g_theme_font_cursor,
                            len - g_theme_font_cursor + 1);
                    g_theme_font_input[g_theme_font_cursor++] = (char)cp;
                    g_theme_font_input[len + 1] = '\0';
                }
            }
        }
        return;
    }
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
    if (g_reload_prompt) {
        float pw = 420.0f, ph = 120.0f;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;
        if (mx >= px + 16 && mx <= px + 106 && my >= py + ph - 40 && my <= py + ph - 12) {
            editor_reload_file(&g_editor);
            g_reload_prompt = 0;
            return;
        }
        if (mx >= px + 120 && mx <= px + 210 && my >= py + ph - 40 && my <= py + ph - 12) {
            editor_dismiss_external_change(&g_editor);
            g_reload_prompt = 0;
            return;
        }
        if (mx < px || mx > px + pw || my < py || my > py + ph) {
            editor_dismiss_external_change(&g_editor);
            g_reload_prompt = 0;
            return;
        }
        return;
    }
    if (g_theme_open) {
        float pw = THEME_PW, ph = THEME_PH;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;

        // Close button
        if (mx >= px + pw - 74 && mx <= px + pw - 16 && my >= py + ph - 38 && my <= py + ph - 10) {
            g_theme_open = 0; return;
        }

        // Color list rows (scrollable)
        float list_y = py + THEME_LIST_Y;
        float row_h = THEME_ROW_H;
        float list_vis_h = THEME_LIST_MAX_H;
        int count = theme_entry_count();
        if (my >= list_y && my < list_y + list_vis_h && mx >= px + 8 && mx <= px + pw - 8) {
            float click_y = my - list_y + g_theme_list_scroll;
            int idx = (int)(click_y / row_h);
            if (idx >= 0 && idx < count) {
                g_theme_sel = idx;
                ThemeEntry e = theme_entry_by_index(idx);
                theme_color_to_hex(e.color, g_theme_input, sizeof(g_theme_input));
                g_theme_cursor = (int)strlen(g_theme_input);
                g_theme_input_on = 1;
                g_theme_font_input_on = 0;
                return;
            }
        }

        // Controls below list
        float ctrl_y = list_y + list_vis_h + 12.0f;
        float ib_h = 24.0f;

        // Input box (color)
        float ib_x = px + 100.0f, ib_w = 180.0f;
        if (mx >= ib_x && mx <= ib_x + ib_w && my >= ctrl_y && my <= ctrl_y + ib_h) {
            g_theme_input_on = 1;
            g_theme_font_input_on = 0;
            float cwch = text_char_width();
            int off = (int)((mx - ib_x - 6.0f) / cwch);
            int len = (int)strlen(g_theme_input);
            if (off < 0) off = 0;
            if (off > len) off = len;
            g_theme_cursor = off;
            return;
        }

        // Gradient cycle button
        if (mx >= px + 300.0f && mx <= px + 440.0f && my >= ctrl_y && my <= ctrl_y + ib_h) {
            g_theme.bg_gradient = (g_theme.bg_gradient + 1) % 3;
            theme_save_persisted();
            return;
        }

        ctrl_y += ib_h + 8.0f;

        // Font size input
        float fs_ib_x = px + 120.0f, fs_ib_w = 80.0f;
        if (mx >= fs_ib_x && mx <= fs_ib_x + fs_ib_w && my >= ctrl_y && my <= ctrl_y + ib_h) {
            g_theme_font_input_on = 1;
            g_theme_input_on = 0;
            snprintf(g_theme_font_input, sizeof(g_theme_font_input), "%.1f", g_theme.font_size);
            g_theme_font_cursor = (int)strlen(g_theme_font_input);
            return;
        }

        ctrl_y += ib_h + 8.0f;

        // Font path chooser button
        if (mx >= px + 390.0f && mx <= px + 480.0f && my >= ctrl_y && my <= ctrl_y + ib_h) {
            char path[1024] = "";
            if (open_file_dialog(path, sizeof(path),
                                  "Font Files (*.ttf)\0*.ttf\0All Files (*.*)\0*.*\0")) {
                strncpy(g_theme.font_path, path, sizeof(g_theme.font_path) - 1);
                g_theme.font_path[sizeof(g_theme.font_path) - 1] = '\0';
                theme_apply_font();
                theme_save_persisted();
            }
            return;
        }

        // Bottom-left buttons
        float btn_y = py + ph - 38.0f;
        struct { float x; float w; const char *label; } b[] = {
            { px + 20.0f,   72.0f, "Load..." },
            { px + 104.0f,  92.0f, "Save As..." },
            { px + 208.0f,  64.0f, "Reset" },
        };
        for (int i = 0; i < 3; i++) {
            if (mx >= b[i].x && mx <= b[i].x + b[i].w && my >= btn_y && my <= btn_y + 24.0f) {
                if (i == 0) {
                    char path[1024] = "";
                    if (open_file_dialog(path, sizeof(path),
                                          "RMGBE Themes (*.rmgtheme)\0*.rmgtheme\0All Files (*.*)\0*.*\0")) {
                        char err[256];
                        if (!theme_load_file(path, err, sizeof(err)))
                            snprintf(g_theme_msg, sizeof(g_theme_msg), "%s", err);
                        else {
                            snprintf(g_theme_msg, sizeof(g_theme_msg), "Loaded %s", g_theme.name);
                            g_theme_sel = -1; g_theme_input_on = 0; g_theme_font_input_on = 0;
                            theme_apply_font();
                            theme_save_persisted();
                        }
                    }
                } else if (i == 1) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%.60s.rmgtheme", g_theme.name);
                    if (save_file_dialog(path, sizeof(path),
                                         "RMGBE Themes (*.rmgtheme)\0*.rmgtheme\0All Files (*.*)\0*.*\0",
                                         "rmgtheme")) {
                        char err[256];
                        if (!theme_save_file(path, err, sizeof(err)))
                            snprintf(g_theme_msg, sizeof(g_theme_msg), "%s", err);
                        else {
                            snprintf(g_theme_msg, sizeof(g_theme_msg), "Saved");
                            theme_save_persisted();
                        }
                    }
                } else {
                    theme_reset();
                    snprintf(g_theme_msg, sizeof(g_theme_msg), "Defaults restored");
                    theme_apply_font();
                    theme_save_persisted();
                    g_theme_sel = -1; g_theme_input_on = 0; g_theme_font_input_on = 0;
                }
                return;
            }
        }

        if (mx < px || mx > px + pw || my < py || my > py + ph) {
            g_theme_open = 0; return;
        }
        // Click inside modal but not on a widget: unfocus the input
        g_theme_input_on = 0;
        g_theme_font_input_on = 0;
        return;
    }

    // Toolbar area
    if (my < toolbar_h) {
        if (mx >= 8 && mx <= 88) {
            editor_new_file(&g_editor);
        }
        if (mx >= 96 && mx <= 208) {
            char path[512] = "";
            if (open_folder_dialog(path, sizeof(path))) {
                fp_open_dir(&g_fp, path);
                term_set_cwd(&g_term, path);
            }
        }
        if (mx >= 216 && mx <= 312) {
            char path[512] = "";
            if (open_file_dialog(path, sizeof(path),
                                 "Source Files (*.c;*.h;*.cpp;*.txt)\0*.c;*.h;*.cpp;*.txt\0All Files (*.*)\0*.*\0")) {
                editor_open_file(&g_editor, path);
                term_set_cwd(&g_term, path);
            }
        }
        if (mx >= 320 && mx <= 380) {
            if (!g_editor.filepath[0]) {
                char path[512] = "";
                if (save_file_dialog(path, sizeof(path), NULL, NULL))
                    editor_save_file_as(&g_editor, path);
            } else {
                editor_save_file(&g_editor);
            }
        }
        if (mx >= 390 && mx <= 460) {
            g_theme_open = 1;
            g_theme_list_scroll = 0;
            g_theme_input_on = 0;
            g_theme_font_input_on = 0;
        }
        if (mx >= 468 && mx <= 528)
            g_about_open = 1;
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
    if (picked) {
        editor_open_file(&g_editor, picked);
        term_set_cwd(&g_term, picked);
    }
}

static void cb_cursor_pos(GLFWwindow *win, double mx, double my) {
    (void)win;
    editor_mouse_move(&g_editor, (float)mx, (float)my, g_editor_x, g_editor_y);
}

static void cb_scroll(GLFWwindow *win, double xoff, double yoff) {
    (void)win; (void)xoff;
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    float sidebar_w = (float)PANEL_SIDEBAR_W;
    float toolbar_h = 32.0f;

    // Theme editor scroll — scroll the color list
    if (g_theme_open) {
        float pw = THEME_PW, ph = THEME_PH;
        float px = ((float)g_win_w - pw) * 0.5f, py = ((float)g_win_h - ph) * 0.5f;
        float list_y = py + THEME_LIST_Y;
        if (mx >= px && mx <= px + pw && my >= list_y && my <= list_y + THEME_LIST_MAX_H) {
            g_theme_list_scroll -= (float)yoff * THEME_ROW_H * 3.0f;
            return;
        }
    }

    if (mx < sidebar_w && my > toolbar_h) {
        // Scroll file panel
        float ch = text_char_height();
        float row = ch + 4.0f;
        g_fp.scroll -= (float)yoff * row;
        if (g_fp.scroll < 0) g_fp.scroll = 0;
    } else if (g_focus == 0) {
        editor_scroll(&g_editor, (float)yoff);
    }
}

// ---- Toolbar ----------------------------------------------------------------
static void draw_toolbar(float w) {
    draw_rect(0, 0, w, 32.0f, COLOR_TOOLBAR, 1.0f);

    struct { float x; float bw; const char *label; } btns[] = {
        {8,   80.0f,  "New File"},
        {96,  112.0f, "Open Folder"},
        {216, 96.0f,  "Open File"},
        {320, 60.0f,  "Save"},
        {390, 70.0f,  "Theme"},
        {468, 60.0f,  "About"},
    };
    for (int i = 0; i < 6; i++) {
        draw_rect(btns[i].x, 4, btns[i].bw, 24.0f, COLOR_BUTTON, 0.5f);
        draw_text(btns[i].label, btns[i].x + 8, 20.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---- Modals ----------------------------------------------------------------
static void draw_reload_prompt(void) {
    if (!g_reload_prompt) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = 420.0f, ph = 120.0f;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, COLOR_MODAL_BACKGROUND, 1.0f);
    draw_rect(px, py, pw, 1.0f, COLOR_MODAL_BORDER, 1.0f);

    draw_text("File changed on disk", px + 16, py + 28.0f, COLOR_TEXT);
    const char *name = g_editor.filepath;
    const char *slash = strrchr(name, '\\');
    if (!slash) slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    draw_text(name, px + 16, py + 50.0f, 0.7f, 0.7f, 0.7f);

    // Reload button
    draw_rect(px + 16, py + ph - 40, 90.0f, 28.0f, COLOR_BUTTON, 0.5f);
    draw_text("Reload", px + 28, py + ph - 21.0f, COLOR_TEXT);
    // Dismiss button
    draw_rect(px + 120, py + ph - 40, 90.0f, 28.0f, COLOR_BUTTON, 0.5f);
    draw_text("Dismiss", px + 130, py + ph - 21.0f, COLOR_TEXT);
}

static void draw_about(void) {
    if (!g_about_open) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = 340.0f, ph = 160.0f;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, COLOR_MODAL_BACKGROUND, 1.0f);
    draw_rect(px, py, pw, 1.0f, COLOR_MODAL_BORDER, 1.0f);

    draw_text("Rick's Minimal Gap Buffer Editor", px + 16, py + 28.0f, COLOR_TEXT);
    draw_text("Version " RMGBE_VERSION, px + 16, py + 54.0f, 0.7f, 0.7f, 0.7f);
    draw_text("---", px + 16, py + 76.0f, 0.7f, 0.7f, 0.7f);
    draw_text("urboirad", px + 16, py + 98.0f, 0.7f, 0.7f, 0.7f);

    draw_rect(px + pw - 70, py + ph - 32, 56.0f, 24.0f, COLOR_BUTTON, 0.5f);
    draw_text("Close", px + pw - 58, py + ph - 14.0f, 1.0f, 1.0f, 1.0f);
}

static void draw_theme_editor(void) {
    if (!g_theme_open) return;
    float cw = (float)g_win_w, ch = (float)g_win_h;
    float pw = THEME_PW, ph = THEME_PH;
    float px = (cw - pw) * 0.5f, py = (ch - ph) * 0.5f;

    draw_rect(0, 0, cw, ch, 0, 0, 0, 0.5f);
    draw_rect(px, py, pw, ph, COLOR_MODAL_BACKGROUND, 1.0f);
    draw_rect(px, py, pw, 1.0f, COLOR_MODAL_BORDER, 1.0f);

    draw_text("Theme Editor", px + 16, py + 28.0f, COLOR_TEXT);

    // ---- Color list (scrollable) ----
    float list_y = py + THEME_LIST_Y;
    float row_h = THEME_ROW_H;
    int count = theme_entry_count();
    float chw = text_char_width();
    float list_total = count * row_h;
    float list_vis_h = THEME_LIST_MAX_H;

    // Clamp scroll
    float max_scroll = list_total - list_vis_h;
    if (max_scroll < 0) max_scroll = 0;
    if (g_theme_list_scroll < 0) g_theme_list_scroll = 0;
    if (g_theme_list_scroll > max_scroll) g_theme_list_scroll = max_scroll;

    // Scissor-clip the list region
    int sx = (int)px, sy = (int)list_y;
    int sw = (int)pw, sh = (int)list_vis_h;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx * cw / (float)g_win_w, (float)g_win_h - (sy + sh) * ch / (float)g_win_h,
              sw * cw / (float)g_win_w, sh * ch / (float)g_win_h);

    for (int i = 0; i < count; i++) {
        float ry = list_y + i * row_h - g_theme_list_scroll;
        if (ry + row_h < list_y || ry > list_y + list_vis_h) continue;

        // Row background: highlight selected
        if (i == g_theme_sel)
            draw_rect(px + 8, ry, pw - 16, row_h, COLOR_BUTTON, 0.25f);

        ThemeEntry e = theme_entry_by_index(i);

        // Swatch
        draw_rect(px + 14, ry + 4.0f, 28.0f, row_h - 8.0f,
                  e.color->r, e.color->g, e.color->b, 1.0f);
        draw_rect(px + 14, ry + 4.0f, 28.0f, 1.0f, 1, 1, 1, 0.35f);
        draw_rect(px + 14, ry + row_h - 5.0f, 28.0f, 1.0f, 1, 1, 1, 0.35f);

        // Label
        draw_text(e.label, px + 50.0f, ry + row_h * 0.75f, COLOR_TEXT);

        // Hex value (dimmed)
        char hex[16];
        theme_color_to_hex(e.color, hex, sizeof(hex));
        draw_text(hex, px + 280.0f, ry + row_h * 0.75f, 0.65f, 0.65f, 0.65f);
    }
    glDisable(GL_SCISSOR_TEST);

    // Scrollbar (thin track on right edge of list)
    if (max_scroll > 0) {
        float track_x = px + pw - 10.0f;
        float track_h = list_vis_h;
        float thumb_h = track_h * (list_vis_h / list_total);
        float thumb_y = list_y + (g_theme_list_scroll / max_scroll) * (track_h - thumb_h);
        draw_rect(track_x, list_y, 4.0f, track_h, 0.2f, 0.2f, 0.25f, 0.5f);
        draw_rect(track_x, thumb_y, 4.0f, thumb_h, 0.5f, 0.5f, 0.55f, 0.7f);
    }

    // ---- Controls below list ----
    float ctrl_y = list_y + list_vis_h + 12.0f;

    // Value input row
    draw_text("Value:", px + 20.0f, ctrl_y + 14.0f, COLOR_TEXT);
    float ib_x = px + 100.0f, ib_w = 180.0f, ib_h = 24.0f;
    draw_rect(ib_x, ctrl_y, ib_w, ib_h, 0, 0, 0, 0.6f);
    draw_rect(ib_x, ctrl_y, ib_w, 1.0f, g_theme_input_on ? 0.9f : 0.35f,
              g_theme_input_on ? 0.9f : 0.35f, g_theme_input_on ? 0.9f : 0.4f, 1.0f);
    draw_rect(ib_x, ctrl_y + ib_h - 1.0f, ib_w, 1.0f, COLOR_MODAL_BORDER, 1.0f);
    {
        float tx = ib_x + 6.0f;
        draw_text(g_theme_input, tx, ctrl_y + 16.0f, COLOR_TEXT);
        if (g_theme_input_on && ((int)(glfwGetTime() * 2.0) & 1)) {
            float cx = tx + g_theme_cursor * chw;
            draw_rect(cx, ctrl_y + 4.0f, 1.5f, ib_h - 8.0f, COLOR_CURSOR_HIGHLIGHT, 1.0f);
        }
    }

    // Gradient cycle button
    const char *gl = g_theme.bg_gradient == THEME_GRADIENT_VERTICAL   ? "Gradient: Vertical" :
                     g_theme.bg_gradient == THEME_GRADIENT_HORIZONTAL ? "Gradient: Horizontal" : "Gradient: None";
    draw_rect(px + 300.0f, ctrl_y, 140.0f, ib_h, COLOR_BUTTON, 0.5f);
    draw_text(gl, px + 308.0f, ctrl_y + 16.0f, COLOR_TEXT);

    ctrl_y += ib_h + 8.0f;

    // Font size input row
    draw_text("Font size:", px + 20.0f, ctrl_y + 14.0f, COLOR_TEXT);
    float fs_ib_x = px + 120.0f, fs_ib_w = 80.0f;
    draw_rect(fs_ib_x, ctrl_y, fs_ib_w, ib_h, 0, 0, 0, 0.6f);
    draw_rect(fs_ib_x, ctrl_y, fs_ib_w, 1.0f, g_theme_font_input_on ? 0.9f : 0.35f,
              g_theme_font_input_on ? 0.9f : 0.35f, g_theme_font_input_on ? 0.9f : 0.4f, 1.0f);
    draw_rect(fs_ib_x, ctrl_y + ib_h - 1.0f, fs_ib_w, 1.0f, COLOR_MODAL_BORDER, 1.0f);
    draw_text(g_theme_font_input, fs_ib_x + 6.0f, ctrl_y + 16.0f, COLOR_TEXT);
    if (g_theme_font_input_on && ((int)(glfwGetTime() * 2.0) & 1)) {
        float cx = fs_ib_x + 6.0f + g_theme_font_cursor * chw;
        draw_rect(cx, ctrl_y + 4.0f, 1.5f, ib_h - 8.0f, COLOR_CURSOR_HIGHLIGHT, 1.0f);
    }

    ctrl_y += ib_h + 8.0f;

    // Font path chooser row
    draw_text("Font:", px + 20.0f, ctrl_y + 14.0f, COLOR_TEXT);
    const char *fp_label = g_theme.font_path[0] ? g_theme.font_path : "(embedded)";
    draw_rect(px + 80.0f, ctrl_y, 300.0f, ib_h, COLOR_BUTTON, 0.5f);
    draw_text(fp_label, px + 88.0f, ctrl_y + 16.0f, COLOR_TEXT);
    draw_rect(px + 390.0f, ctrl_y, 90.0f, ib_h, COLOR_BUTTON, 0.5f);
    draw_text("Choose...", px + 398.0f, ctrl_y + 16.0f, COLOR_TEXT);

    ctrl_y += ib_h + 12.0f;

    // Hint / message line
    if (g_theme_msg[0])
        draw_text(g_theme_msg, px + 20.0f, ctrl_y + 14.0f, 0.8f, 0.8f, 0.5f);
    else if (g_theme_sel >= 0)
        draw_text("Type #RRGGBB or R,G,B then press Enter", px + 20.0f, ctrl_y + 14.0f, 0.55f, 0.55f, 0.55f);
    else
        draw_text("Click a color to edit", px + 20.0f, ctrl_y + 14.0f, 0.55f, 0.55f, 0.55f);

    // ---- Bottom buttons ----
    float btn_y = py + ph - 38.0f;
    struct { float x; float w; const char *label; } b[] = {
        { px + 20.0f,   72.0f, "Load..." },
        { px + 104.0f,  92.0f, "Save As..." },
        { px + 208.0f,  64.0f, "Reset" },
    };
    for (int i = 0; i < 3; i++) {
        draw_rect(b[i].x, btn_y, b[i].w, 28.0f, COLOR_BUTTON, 0.5f);
        draw_text(b[i].label, b[i].x + 8.0f, btn_y + 19.0f, COLOR_TEXT);
    }

    draw_rect(px + pw - 74, py + ph - 38, 58.0f, 28.0f, COLOR_BUTTON, 0.5f);
    draw_text("Close", px + pw - 61, py + ph - 19.0f, 1.0f, 1.0f, 1.0f);
}

// ---- Main -------------------------------------------------------------------
int main(void) {
    if (!glfwInit()) return -1;

    GLFWwindow *win = glfwCreateWindow(g_win_w, g_win_h, "RMGBE " RMGBE_VERSION, NULL, NULL);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

#ifdef _WIN32
// Icon
    SendMessage(glfwGetWin32Window(win), WM_SETICON, ICON_BIG,
                (LPARAM)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    SendMessage(glfwGetWin32Window(win), WM_SETICON, ICON_SMALL,
                (LPARAM)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
#endif

    text_renderer_set_win_size(g_win_w, g_win_h);

    editor_init(&g_editor);
    fp_init(&g_fp);
    theme_init();
    theme_load_persisted();
    theme_apply_font();

    char cwd[512];
#ifdef _WIN32
    if (GetCurrentDirectoryA(sizeof(cwd), cwd))
#else
    if (getcwd(cwd, sizeof(cwd)))
#endif
        fp_open_dir(&g_fp, cwd);

    term_init(&g_term);

    startup_audio_play(NULL);

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
        draw_rect(sidebar_w, editor_y, 1.0f, editor_h, COLOR_MODAL_BORDER, 1.0f);

        // Editor
        g_editor_x = sidebar_w + 1.0f;
        g_editor_y = editor_y;
        editor_check_external_change(&g_editor);
        if (g_editor.external_change && !g_reload_prompt)
            g_reload_prompt = 1;
        editor_update(&g_editor, dt);
        editor_render(&g_editor, g_editor_x, g_editor_y, editor_w, editor_h);

        // Terminal
        term_poll_output(&g_term);
        term_render(&g_term, 0, g_win_h - term_h, (float)g_win_w, term_h);

        // Green border on terminal when it has focus
        if (g_focus == 1)
            draw_rect(0, g_win_h - term_h, (float)g_win_w, 2.0f, COLOR_CURSOR_HIGHLIGHT, 1.0f);

        draw_about();
        draw_theme_editor();
        draw_reload_prompt();

        // FPS counter
        g_fps_frame_count++;
        if (now - g_fps_last_update >= 1.0) {
            g_fps_value = (float)g_fps_frame_count / (float)(now - g_fps_last_update);
            g_fps_frame_count = 0;
            g_fps_last_update = now;
        }
        char fps_str[32];
        snprintf(fps_str, sizeof(fps_str), "%.0f FPS", g_fps_value);
        draw_text(fps_str, (float)g_win_w - 60.0f, 10.0f, 0.6f, 0.8f, 0.6f);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    term_free(&g_term);
    editor_free(&g_editor);
    text_renderer_free();
    startup_audio_stop();
    glfwTerminate();
    return 0;
}
