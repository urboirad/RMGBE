#include "gap_buffer.h"
#include "text.h"
#include "undo.h"
#include "file_io.h"
#include "editor.h"
#include "GLFW/glfw3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond) do {                                                     \
    if (!(cond)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        g_failures++;                                                        \
    }                                                                        \
} while (0)

static void check_bytes(GapBuffer *gb, const char *expected, int line) {
    int n = gb_length(gb);
    int want = (int)strlen(expected);
    if (n != want) {
        fprintf(stderr, "FAIL line %d: length %d, expected %d\n", line, n, want);
        g_failures++;
        return;
    }
    for (int i = 0; i < n; i++) {
        if (gb_char_at(gb, i) != expected[i]) {
            fprintf(stderr, "FAIL line %d: byte %d is 0x%02X, expected 0x%02X\n",
                    line, i, (unsigned char)gb_char_at(gb, i),
                    (unsigned char)expected[i]);
            g_failures++;
            return;
        }
    }
}

#define CHECK_BYTES(gb, expected) check_bytes((gb), (expected), __LINE__)

static void test_gap_buffer_basic(void) {
    GapBuffer gb;
    CHECK(init_buffer(&gb, 64));
    CHECK(gb_length(&gb) == 0);

    CHECK(gb_insert_text(&gb, "hello", 5));
    CHECK_BYTES(&gb, "hello");

    CHECK(move_cursor(&gb, 2) == 2);
    CHECK(gb_insert_text(&gb, "XY", 2));
    CHECK_BYTES(&gb, "heXYllo");

    CHECK(move_cursor(&gb, 0) == 0);
    CHECK(delete_range(&gb, 1, 3));
    CHECK_BYTES(&gb, "hYllo");

    CHECK(move_cursor(&gb, gb_length(&gb)) == 5);
    CHECK(delete_char(&gb));
    CHECK_BYTES(&gb, "hYll");

    CHECK(move_cursor(&gb, 100) == 4);
    CHECK(move_cursor(&gb, -5) == 0);

    CHECK(delete_range(&gb, 2, 99));
    CHECK_BYTES(&gb, "hY");
    CHECK(delete_range(&gb, 1, 1));

    free(gb.buffer);
}

static void test_gap_buffer_growth_and_movement(void) {
    GapBuffer gb;
    CHECK(init_buffer(&gb, GB_MIN_CAPACITY));

    /* Force several geometric growths from the minimum capacity. */
    char big[5000];
    memset(big, 'a', sizeof(big));
    for (int i = 0; i < (int)sizeof(big); i++) big[i] = (char)('a' + (i % 26));
    CHECK(gb_insert_text(&gb, big, (int)sizeof(big)));
    CHECK(gb_length(&gb) == (int)sizeof(big));
    for (int i = 0; i < (int)sizeof(big); i += 97)
        CHECK(gb_char_at(&gb, i) == big[i]);

    /* Moving the gap must never change the logical contents. */
    int len = gb_length(&gb);
    for (int pos = 0; pos <= len; pos += 211) {
        CHECK(move_cursor(&gb, pos) == pos);
        CHECK(gb_length(&gb) == len);
        CHECK(gb_char_at(&gb, 0) == big[0]);
        CHECK(gb_char_at(&gb, len - 1) == big[len - 1]);
    }

    CHECK(move_cursor(&gb, 10) == 10);
    CHECK(gb_insert_text(&gb, "XYZ", 3));
    CHECK(gb_char_at(&gb, 9) == big[9]);
    CHECK(gb_char_at(&gb, 10) == 'X');
    CHECK(gb_char_at(&gb, 11) == 'Y');
    CHECK(gb_char_at(&gb, 12) == 'Z');
    CHECK(gb_char_at(&gb, 13) == big[10]);
    CHECK(gb_length(&gb) == len + 3);

    free(gb.buffer);
}

static void test_text_helpers(void) {
    unsigned int cp = 0;
    char utf8[4];

    CHECK(text_utf8_encode('A', utf8) == 1);
    CHECK(text_utf8_decode(utf8, 1, &cp) == 1 && cp == 'A');

    int n = text_utf8_encode(0x20AC, utf8); /* euro sign */
    CHECK(n == 3);
    CHECK(text_utf8_decode(utf8, n, &cp) == 3 && cp == 0x20AC);

    char e_acute[4];
    n = text_utf8_encode(0x00E9, e_acute);
    CHECK(n == 2);
    CHECK(text_utf8_next(e_acute, 0, n) == n);
    CHECK(text_utf8_prev(e_acute, n) == 0);

    CHECK(text_codepoint_cells('\t', 0, TEXT_TAB_WIDTH) == TEXT_TAB_WIDTH);
    CHECK(text_codepoint_cells('\t', 1, TEXT_TAB_WIDTH) == TEXT_TAB_WIDTH - 1);
    CHECK(text_codepoint_cells('x', 0, TEXT_TAB_WIDTH) == 1);
    CHECK(text_display_width("a\tb", 3, 0, TEXT_TAB_WIDTH) == 5);

    CHECK(text_utf8_decode("\xFF", 1, &cp) == 1 && cp == 0xFFFDu);
}

static void test_undo_stack(void) {
    UndoStack s;
    undo_stack_init(&s);
    CHECK(s.count == 0);

    CHECK(undo_stack_push(&s, UNDO_INSERT, 0, "abc", 3, 0, 0));
    CHECK(undo_stack_push(&s, UNDO_DELETE, 3, "de", 2, 1, 2));
    CHECK(s.count == 2);

    UndoEntry e;
    CHECK(undo_stack_pop(&s, &e));
    CHECK(e.type == UNDO_DELETE);
    CHECK(e.pos == 3 && e.len == 2);
    CHECK(e.text && strcmp(e.text, "de") == 0);
    undo_entry_free(&e);

    CHECK(undo_stack_pop(&s, &e));
    CHECK(e.type == UNDO_INSERT);
    CHECK(e.text && strcmp(e.text, "abc") == 0);
    undo_entry_free(&e);

    CHECK(!undo_stack_pop(&s, &e));
    undo_stack_free(&s);

    /* Memory cap: old entries are discarded, oversized entries rejected. */
    undo_stack_init(&s);
    s.memory_limit = 16;
    CHECK(undo_stack_push(&s, UNDO_INSERT, 0, "123456", 6, 0, 0));
    CHECK(undo_stack_push(&s, UNDO_INSERT, 0, "abcdef", 6, 0, 0));
    CHECK(undo_stack_push(&s, UNDO_INSERT, 0, "ABCDEF", 6, 0, 0));
    CHECK(s.bytes <= s.memory_limit);
    CHECK(s.count <= 2);
    CHECK(!undo_stack_push(&s, UNDO_INSERT, 0, "012345678901234567890", 21, 0, 0));
    CHECK(s.bytes <= s.memory_limit);
    undo_stack_free(&s);
}

static void check_editor(Editor *e, const char *expected, int line) {
    int n = gb_length(&e->gb);
    int want = (int)strlen(expected);
    if (n != want) {
        fprintf(stderr, "FAIL line %d: editor length %d, expected %d\n",
                line, n, want);
        g_failures++;
        return;
    }
    for (int i = 0; i < n; i++) {
        if (gb_char_at(&e->gb, i) != expected[i]) {
            fprintf(stderr, "FAIL line %d: editor byte %d differs\n", line, i);
            g_failures++;
            return;
        }
    }
}

#define CHECK_EDITOR(e, expected) check_editor((e), (expected), __LINE__)

static void test_editor_editing_undo_redo(void) {
    Editor e;
    editor_init(&e);
    CHECK(e.mode == MODE_NORMAL);
    CHECK_EDITOR(&e, "");

    /* Insert ASCII and a multi-byte UTF-8 character. */
    editor_key(&e, GLFW_KEY_I, 0);
    CHECK(e.mode == MODE_INSERT);
    editor_char(&e, 'i'); /* the character belonging to the mode key is swallowed */
    CHECK_EDITOR(&e, "");
    editor_char(&e, 'a');
    editor_char(&e, 0x00E9);
    CHECK_EDITOR(&e, "a\xC3\xA9");

    /* Backspace removes the whole UTF-8 sequence. */
    editor_key(&e, GLFW_KEY_BACKSPACE, 0);
    CHECK_EDITOR(&e, "a");

    /* Newline support. */
    editor_key(&e, GLFW_KEY_ENTER, 0);
    editor_char(&e, 'b');
    CHECK_EDITOR(&e, "a\nb");
    editor_key(&e, GLFW_KEY_ESCAPE, 0);
    CHECK(e.mode == MODE_NORMAL);

    /* Undo / redo the inserts in reverse order. */
    editor_undo(&e);
    CHECK_EDITOR(&e, "a\n");
    editor_undo(&e);
    CHECK_EDITOR(&e, "a");
    editor_redo(&e);
    CHECK_EDITOR(&e, "a\n");
    editor_redo(&e);
    CHECK_EDITOR(&e, "a\nb");

    /* Normal-mode delete at the start of the second line. */
    editor_key(&e, GLFW_KEY_0, 0);
    editor_key(&e, GLFW_KEY_X, 0);
    CHECK_EDITOR(&e, "a\n");
    editor_undo(&e);
    CHECK_EDITOR(&e, "a\nb");

    /* Visual selection delete. */
    editor_key(&e, GLFW_KEY_0, 0);
    editor_key(&e, GLFW_KEY_V, 0);
    CHECK(e.mode == MODE_VISUAL);
    editor_key(&e, GLFW_KEY_L, 0);
    editor_key(&e, GLFW_KEY_D, 0);
    CHECK(e.mode == MODE_NORMAL);
    CHECK_EDITOR(&e, "a\n");
    editor_undo(&e);
    CHECK_EDITOR(&e, "a\nb");

    editor_free(&e);
}

static void test_editor_render_multiline(void) {
    Editor e;
    editor_init(&e);

    const char *src = "int main(void) {\n\treturn 0;\n\n}\n";
    CHECK(gb_insert_text(&e.gb, src, (int)strlen(src)));
    e.lines_dirty = 1;
    e.mode = MODE_NORMAL;
    CHECK_EDITOR(&e, src);

    /* Rendering an empty line used to hand a NULL string to the tokenizer. */
    editor_render(&e, 0.0f, 0.0f, 800.0f, 400.0f);
    CHECK(e.line_count == 5);
    for (int i = 0; i < e.line_count; i++) {
        CHECK(i < e.line_syntax_cap);
        CHECK(e.line_syntax[i].valid);
        CHECK(e.line_syntax[i].text != NULL);
    }

    editor_free(&e);
}

static void test_atomic_write(void) {
    const char *path = "rmgbe_test_atomic.txt";
    const char *data = "alpha\nbeta\n";
    char err[128] = "";

    CHECK(atomic_write_all(path, data, strlen(data), err, sizeof(err)));
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        char buf[64] = {0};
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        CHECK(got == strlen(data));
        CHECK(memcmp(buf, data, strlen(data)) == 0);
    }
    remove(path);

    CHECK(!atomic_write_all("", "x", 1, err, sizeof(err)));
    CHECK(err[0] != '\0');
}

int main(void) {
    test_gap_buffer_basic();
    test_gap_buffer_growth_and_movement();
    test_text_helpers();
    test_undo_stack();
    test_atomic_write();
    test_editor_editing_undo_redo();
    test_editor_render_multiline();

    if (g_failures) {
        fprintf(stderr, "%d test assertion(s) failed\n", g_failures);
        return 1;
    }
    printf("All RMGBE tests passed\n");
    return 0;
}
