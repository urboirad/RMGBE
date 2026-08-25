#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Theme g_theme;

// ---- Defaults -----------------------------------------------------------------
static void set_color(ThemeColor *c, int r, int g, int b) {
    c->r = r / 255.0f;
    c->g = g / 255.0f;
    c->b = b / 255.0f;
}

void theme_init(void) {
    memset(&g_theme, 0, sizeof(g_theme));
    snprintf(g_theme.name, sizeof(g_theme.name), "Default");

    set_color(&g_theme.background,     0,   0,   0);
    set_color(&g_theme.background_end, 24, 24,  46);
    g_theme.bg_gradient = THEME_GRADIENT_NONE;

    set_color(&g_theme.text,         224, 224, 224);
    set_color(&g_theme.toolbar,       18,  18,  18);
    set_color(&g_theme.button,       111,  44, 249);
    set_color(&g_theme.cursor,         0, 188, 212);
    set_color(&g_theme.statusbar,     18,  18,  18);

    set_color(&g_theme.syntax_default, 224, 224, 224);
    set_color(&g_theme.keyword,         86, 156, 214);
    set_color(&g_theme.type,            78, 201, 176);
    set_color(&g_theme.string,         209, 154, 102);
    set_color(&g_theme.number,         209, 154, 102);
    set_color(&g_theme.comment,        106, 106, 106);
    set_color(&g_theme.preproc,        198, 120, 221);
    set_color(&g_theme.operatorc,       86, 156, 214);
}

void theme_reset(void) { theme_init(); }

// ---- Color parsing -------------------------------------------------------------
static int hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int theme_parse_color(const char *s, ThemeColor *out) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;

    // Try hex: exactly 6 hex digits
    int ok = 1;
    for (int i = 0; i < 6; i++) {
        if (hexval(s[i]) < 0) { ok = 0; break; }
    }
    if (ok && !isxdigit((unsigned char)s[6])) {
        int r = hexval(s[0]) * 16 + hexval(s[1]);
        int g = hexval(s[2]) * 16 + hexval(s[3]);
        int b = hexval(s[4]) * 16 + hexval(s[5]);
        out->r = r / 255.0f;
        out->g = g / 255.0f;
        out->b = b / 255.0f;
        return 1;
    }

    // Fallback: decimal triple "r,g,b"
    int r, g, b;
    char extra;
    if (sscanf(s, "%d ,%d ,%d %c", &r, &g, &b, &extra) >= 3) {
        out->r = r / 255.0f;
        out->g = g / 255.0f;
        out->b = b / 255.0f;
        return 1;
    }
    return 0;
}

void theme_color_to_hex(const ThemeColor *c, char *buf, int buf_size) {
    snprintf(buf, buf_size, "#%02X%02X%02X",
             (int)(c->r * 255.0f + 0.5f),
             (int)(c->g * 255.0f + 0.5f),
             (int)(c->b * 255.0f + 0.5f));
}

// ---- Named entries (file keys + UI labels) --------------------------------------
typedef struct {
    const char *key;
    const char *label;
    ThemeColor *color;
} EntryDef;

static EntryDef s_entries[] = {
    { "background",     "Background",      &g_theme.background },
    { "background_end", "Background End",  &g_theme.background_end },
    { "text",           "Text",            &g_theme.text },
    { "toolbar",        "Toolbar",         &g_theme.toolbar },
    { "button",         "Button",          &g_theme.button },
    { "cursor",         "Cursor",          &g_theme.cursor },
    { "statusbar",      "Status Bar",      &g_theme.statusbar },
    { "syntax_default", "Text (Syntax)",   &g_theme.syntax_default },
    { "keyword",        "Keywords",        &g_theme.keyword },
    { "type",           "Types",           &g_theme.type },
    { "string",         "Strings",         &g_theme.string },
    { "number",         "Numbers",         &g_theme.number },
    { "comment",        "Comments",        &g_theme.comment },
    { "preprocessor",   "Preprocessor",    &g_theme.preproc },
    { "operator",       "Operators",       &g_theme.operatorc },
};

#define ENTRY_COUNT ((int)(sizeof(s_entries) / sizeof(s_entries[0])))

int theme_entry_count(void) { return ENTRY_COUNT; }

const char *theme_entry_name(int i) {
    if (i < 0 || i >= ENTRY_COUNT) return NULL;
    return s_entries[i].key;
}

ThemeEntry theme_entry_by_index(int i) {
    ThemeEntry e = { NULL, NULL };
    if (i < 0 || i >= ENTRY_COUNT) return e;
    e.label = s_entries[i].label;
    e.color = s_entries[i].color;
    return e;
}

int theme_entry_count_named(void) { return ENTRY_COUNT; }

// ---- File I/O ----------------------------------------------------------------------
// Trim whitespace in place
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return s;
}

int theme_load_file(const char *path, char *err, int err_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_size, "Cannot open '%s'", path);
        return 0;
    }

    Theme saved = g_theme;
    theme_init(); // reset to defaults first

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (!strcmp(key, "name")) {
            snprintf(g_theme.name, sizeof(g_theme.name), "%s", val);
        } else if (!strcmp(key, "background_gradient")) {
            if (!strcmp(val, "vertical"))      g_theme.bg_gradient = THEME_GRADIENT_VERTICAL;
            else if (!strcmp(val, "horizontal")) g_theme.bg_gradient = THEME_GRADIENT_HORIZONTAL;
            else                                 g_theme.bg_gradient = THEME_GRADIENT_NONE;
        } else {
            int found = 0;
            for (int i = 0; i < ENTRY_COUNT; i++) {
                if (!strcmp(key, s_entries[i].key)) {
                    if (!theme_parse_color(val, s_entries[i].color)) {
                        g_theme = saved;
                        fclose(f);
                        snprintf(err, err_size, "Line %d: bad color '%s'", lineno, val);
                        return 0;
                    }
                    found = 1;
                    break;
                }
            }
            if (!found) continue;
        }
    }
    fclose(f);

    err[0] = '\0';
    return 1;
}

int theme_save_file(const char *path, char *err, int err_size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(err, err_size, "Cannot write '%s'", path);
        return 0;
    }

    fprintf(f, "# RMGBE Theme File\n");
    fprintf(f, "# Colors accept #RRGGBB or R,G,B decimal\n");
    fprintf(f, "name = %s\n\n", g_theme.name);
    fprintf(f, "background_gradient = %s\n",
            g_theme.bg_gradient == THEME_GRADIENT_VERTICAL   ? "vertical" :
            g_theme.bg_gradient == THEME_GRADIENT_HORIZONTAL ? "horizontal" : "none");

    for (int i = 0; i < ENTRY_COUNT; i++) {
        char hex[16];
        theme_color_to_hex(s_entries[i].color, hex, sizeof(hex));
        fprintf(f, "%s = %s\n", s_entries[i].key, hex);
    }
    fclose(f);

    err[0] = '\0';
    return 1;
}
