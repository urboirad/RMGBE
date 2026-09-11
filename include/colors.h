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

#define COLOR_FP_BACKGROUND     g_theme.fp_background.r, g_theme.fp_background.g, g_theme.fp_background.b
#define COLOR_FP_SELECTION      g_theme.fp_selection.r, g_theme.fp_selection.g, g_theme.fp_selection.b
#define COLOR_FP_TEXT           g_theme.fp_text.r, g_theme.fp_text.g, g_theme.fp_text.b
#define COLOR_FP_FOLDER         g_theme.fp_folder.r, g_theme.fp_folder.g, g_theme.fp_folder.b

#define COLOR_TERM_BACKGROUND   g_theme.term_background.r, g_theme.term_background.g, g_theme.term_background.b
#define COLOR_TERM_BORDER       g_theme.term_border.r, g_theme.term_border.g, g_theme.term_border.b
#define COLOR_TERM_TEXT         g_theme.term_text.r, g_theme.term_text.g, g_theme.term_text.b
#define COLOR_TERM_INPUT_BG     g_theme.term_input_bg.r, g_theme.term_input_bg.g, g_theme.term_input_bg.b

#define COLOR_MODAL_BACKGROUND  g_theme.modal_background.r, g_theme.modal_background.g, g_theme.modal_background.b
#define COLOR_MODAL_BORDER      g_theme.modal_border.r, g_theme.modal_border.g, g_theme.modal_border.b

#define COLOR_SEARCH_BAR_BG     g_theme.search_bar_bg.r, g_theme.search_bar_bg.g, g_theme.search_bar_bg.b
#define COLOR_SEARCH_MATCH      g_theme.search_match.r, g_theme.search_match.g, g_theme.search_match.b
#define COLOR_SEARCH_MATCH_CUR  g_theme.search_match_cur.r, g_theme.search_match_cur.g, g_theme.search_match_cur.b

#endif