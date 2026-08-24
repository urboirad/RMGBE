#ifndef COLORS_H
#define COLORS_H

#include "theme.h"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Runtime theme lookups — expand to r, g, b (call sites append alpha as needed)
#define COLOR_ED_BACKGROUND     g_theme.background.r, g_theme.background.g, g_theme.background.b
#define COLOR_TEXT              g_theme.text.r, g_theme.text.g, g_theme.text.b
#define COLOR_TOOLBAR           g_theme.toolbar.r, g_theme.toolbar.g, g_theme.toolbar.b
#define COLOR_CURSOR_HIGHLIGHT  g_theme.cursor.r, g_theme.cursor.g, g_theme.cursor.b
#define COLOR_BUTTON            g_theme.button.r, g_theme.button.g, g_theme.button.b
#define COLOR_STATUSBAR         g_theme.statusbar.r, g_theme.statusbar.g, g_theme.statusbar.b

#endif