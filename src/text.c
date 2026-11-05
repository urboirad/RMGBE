#include "text.h"

#define REPLACEMENT_CHAR 0xFFFDu

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

int text_utf8_decode(const char *s, int len, unsigned int *cp) {
    if (!s || len <= 0) { if (cp) *cp = REPLACEMENT_CHAR; return 0; }
    unsigned char c0 = (unsigned char)s[0];

    if (c0 < 0x80) {
        if (cp) *cp = c0;
        return 1;
    }

    int need = 0;
    unsigned int value = 0;
    if ((c0 & 0xE0) == 0xC0)      { need = 1; value = c0 & 0x1F; }
    else if ((c0 & 0xF0) == 0xE0) { need = 2; value = c0 & 0x0F; }
    else if ((c0 & 0xF8) == 0xF0) { need = 3; value = c0 & 0x07; }
    else {
        if (cp) *cp = REPLACEMENT_CHAR;
        return 1;
    }

    if (need + 1 > len) {
        if (cp) *cp = REPLACEMENT_CHAR;
        return 1;
    }
    for (int i = 1; i <= need; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!is_cont(c)) {
            if (cp) *cp = REPLACEMENT_CHAR;
            return 1;
        }
        value = (value << 6) | (c & 0x3F);
    }

    /* Reject overlong forms, surrogates and out-of-range values. */
    if ((need == 1 && value < 0x80) ||
        (need == 2 && value < 0x800) ||
        (need == 3 && value < 0x10000) ||
        value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        if (cp) *cp = REPLACEMENT_CHAR;
        return 1;
    }

    if (cp) *cp = value;
    return need + 1;
}

int text_utf8_encode(unsigned int cp, char out[4]) {
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        cp = '?';

    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

int text_utf8_prev(const char *s, int pos) {
    if (!s || pos <= 0) return 0;
    pos--;
    while (pos > 0 && is_cont((unsigned char)s[pos])) pos--;
    return pos;
}

int text_utf8_next(const char *s, int pos, int len) {
    if (!s || pos < 0) return 0;
    if (pos >= len) return len;
    unsigned int cp;
    int n = text_utf8_decode(s + pos, len - pos, &cp);
    if (n <= 0) n = 1;
    if (pos + n > len) n = len - pos;
    return pos + n;
}

int text_codepoint_cells(unsigned int cp, int col, int tab_width) {
    if (tab_width < 1) tab_width = TEXT_TAB_WIDTH;
    if (cp == '\t') {
        int cells = tab_width - (col % tab_width);
        return cells > 0 ? cells : tab_width;
    }
    if (cp == '\n' || cp == '\r') return 0;
    if (cp == 0) return 0;
    /* CJK / emoji are commonly displayed double-width.  The bundled bitmap
     * font only contains Latin glyphs, but treating these as two cells keeps
     * cursor geometry sensible when they are present in a file. */
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF))
        return 2;
    return 1;
}

int text_display_width(const char *s, int len, int start_col, int tab_width) {
    if (!s || len <= 0) return 0;
    int col = start_col;
    int i = 0;
    while (i < len) {
        unsigned int cp;
        int n = text_utf8_decode(s + i, len - i, &cp);
        if (n <= 0) n = 1;
        col += text_codepoint_cells(cp, col, tab_width);
        i += n;
    }
    return col - start_col;
}

int text_index_for_display_col(const char *s, int len, int target_col, int tab_width) {
    if (!s || len <= 0 || target_col <= 0) return 0;
    int col = 0;
    int i = 0;
    while (i < len) {
        unsigned int cp;
        int n = text_utf8_decode(s + i, len - i, &cp);
        if (n <= 0) n = 1;
        int cells = text_codepoint_cells(cp, col, tab_width);
        if (col + cells > target_col) return i;
        col += cells;
        i += n;
        if (col >= target_col) return i;
    }
    return len;
}
