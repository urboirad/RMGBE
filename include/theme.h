#ifndef THEME_H
#define THEME_H

// ---- Theme color ------------------------------------------------------------
typedef struct {
    float r, g, b;
} ThemeColor;

// Gradient direction for supported surfaces (currently editor background)
typedef enum {
    THEME_GRADIENT_NONE = 0,
    THEME_GRADIENT_VERTICAL,
    THEME_GRADIENT_HORIZONTAL
} ThemeGradient;

// ---- Runtime theme ----------------------------------------------------------
typedef struct {
    char name[64];

    // UI
    ThemeColor background;      // editor background (gradient start)
    ThemeColor background_end;  // gradient end (used when bg_gradient != NONE)
    ThemeGradient bg_gradient;

    ThemeColor text;
    ThemeColor toolbar;
    ThemeColor button;
    ThemeColor cursor;
    ThemeColor statusbar;

    // Syntax
    ThemeColor syntax_default;
    ThemeColor keyword;
    ThemeColor type;
    ThemeColor string;
    ThemeColor number;
    ThemeColor comment;
    ThemeColor preproc;
    ThemeColor operatorc;       // 'operator' is a C++-ish word, avoid

    // File panel
    ThemeColor fp_background;
    ThemeColor fp_selection;
    ThemeColor fp_text;
    ThemeColor fp_folder;

    // Terminal
    ThemeColor term_background;
    ThemeColor term_border;
    ThemeColor term_text;
    ThemeColor term_input_bg;

    // Modal (about / theme editor)
    ThemeColor modal_background;
    ThemeColor modal_border;

    // Editor extras
    ThemeColor search_bar_bg;
    ThemeColor search_match;
    ThemeColor search_match_cur;

    // Font (non-color)
    float font_size;
    char  font_path[512];
} Theme;

extern Theme g_theme;

void theme_init(void);   // apply built-in defaults
void theme_reset(void);  // same as init

// Load/save .rmgtheme files. Returns 1 on success, 0 on failure and fills err.
int theme_load_file(const char *path, char *err, int err_size);
int theme_save_file(const char *path, char *err, int err_size);

// Load/save persisted theme (auto-saved across sessions)
int  theme_load_persisted(void);
void theme_save_persisted(void);

// Parse a single color string: "#RRGGBB", "RRGGBB" or "r,g,b". Returns 1 on ok.
int theme_parse_color(const char *s, ThemeColor *out);

// Format a color as "#RRGGBB" into buf.
void theme_color_to_hex(const ThemeColor *c, char *buf, int buf_size);

// Apply the theme's font settings (re-initializes the text renderer).
// Falls back to embedded font if font_path is empty or fails to load.
void theme_apply_font(void);

// ---- Editor UI metadata ------------------------------------------------------
// Entries describe each editable color for the theme editor modal.
typedef struct {
    const char  *label;
    ThemeColor  *color;
} ThemeEntry;

int        theme_entry_count(void);
const char *theme_entry_name(int i);          // stable key used in files
ThemeEntry  theme_entry_by_index(int i);       // label + pointer
int         theme_entry_count_named(void);     // total named keys (incl. gradient extras)

#endif
