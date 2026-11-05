#ifndef TEXT_H
#define TEXT_H

/*
 * Small text utilities shared by the editor and the tests.
 *
 * The editor stores UTF-8 bytes in the gap buffer.  Cursor columns are byte
 * offsets inside a line; display columns are computed from these helpers so
 * that multibyte characters and tabs line up correctly on screen.
 */

#define TEXT_TAB_WIDTH 4

/* Decode one UTF-8 codepoint.  Returns the number of bytes consumed (>= 1).
 * Invalid sequences decode as U+FFFD and consume one byte. */
int text_utf8_decode(const char *s, int len, unsigned int *cp);

/* Encode a codepoint as UTF-8.  Returns the number of bytes written (1-4).
 * Invalid codepoints are encoded as '?'. */
int text_utf8_encode(unsigned int cp, char out[4]);

/* Start of the codepoint before `pos`, and the one after.  Both clamp to
 * [0, len] and never split a UTF-8 sequence. */
int text_utf8_prev(const char *s, int pos);
int text_utf8_next(const char *s, int pos, int len);

/* Number of terminal cells occupied by a codepoint at display column `col`.
 * Tabs advance to the next TEXT_TAB_WIDTH stop. */
int text_codepoint_cells(unsigned int cp, int col, int tab_width);

/* Display width / reverse mapping for a UTF-8 string. */
int text_display_width(const char *s, int len, int start_col, int tab_width);
int text_index_for_display_col(const char *s, int len, int target_col, int tab_width);

#endif
