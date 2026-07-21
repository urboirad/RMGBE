#ifndef TERMINAL_H
#define TERMINAL_H

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

#define TERM_LINES   256
#define TERM_LINE_LEN 512

typedef struct {
    char   lines[TERM_LINES][TERM_LINE_LEN];
    int    line_count;
    char   input_buf[TERM_LINE_LEN];
    int    input_len;
#ifdef _WIN32
    HANDLE proc;
    HANDLE stdin_write;
    HANDLE stdout_read;
#else
    pid_t  pid;
    int    stdin_fd;
    int    stdout_fd;
#endif
    float  scroll;
} Terminal;

void term_init(Terminal *t);
void term_free(Terminal *t);
void term_send_input(Terminal *t);
void term_poll_output(Terminal *t);
void term_render(Terminal *t, float x, float y, float w, float h);
void term_char_input(Terminal *t, unsigned int codepoint);
void term_key_input(Terminal *t, int key);

#endif
