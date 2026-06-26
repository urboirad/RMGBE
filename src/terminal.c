#include "terminal.h"
#include "text_renderer.h"
#include "GLFW/glfw3.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>

void term_init(Terminal *t) {
    memset(t, 0, sizeof(*t));

    HANDLE stdin_read,  stdout_write;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    CreatePipe(&stdin_read,   &t->stdin_write,  &sa, 0);
    CreatePipe(&t->stdout_read, &stdout_write, &sa, 0);
    SetHandleInformation(t->stdin_write,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(t->stdout_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError  = stdout_write;

    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, "cmd.exe", NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        t->proc = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
}

void term_free(Terminal *t) {
    if (t->proc)        { TerminateProcess(t->proc, 0); CloseHandle(t->proc); }
    if (t->stdin_write) CloseHandle(t->stdin_write);
    if (t->stdout_read) CloseHandle(t->stdout_read);
}

static void term_push_text(Terminal *t, const char *buf, DWORD n) {
    for (DWORD i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (t->line_count < TERM_LINES - 1) t->line_count++;
            else {
                memmove(t->lines[0], t->lines[1], (TERM_LINES - 1) * TERM_LINE_LEN);
                t->lines[TERM_LINES - 1][0] = '\0';
            }
        } else {
            int row = (t->line_count < TERM_LINES) ? t->line_count : TERM_LINES - 1;
            int len = (int)strlen(t->lines[row]);
            if (len < TERM_LINE_LEN - 1) {
                t->lines[row][len]   = c;
                t->lines[row][len+1] = '\0';
            }
        }
    }
}

void term_poll_output(Terminal *t) {
    if (!t->stdout_read) return;
    DWORD avail = 0;
    while (PeekNamedPipe(t->stdout_read, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char buf[1024];
        DWORD read = 0;
        if (ReadFile(t->stdout_read, buf, sizeof(buf) - 1, &read, NULL) && read > 0)
            term_push_text(t, buf, read);
    }
}

void term_send_input(Terminal *t) {
    if (!t->stdin_write || t->input_len == 0) return;
    t->input_buf[t->input_len++] = '\n';
    DWORD written;
    WriteFile(t->stdin_write, t->input_buf, t->input_len, &written, NULL);
    t->input_len = 0;
    t->input_buf[0] = '\0';
}

void term_char_input(Terminal *t, unsigned int cp) {
    if (cp < 32 || cp > 126) return;
    if (t->input_len < TERM_LINE_LEN - 2)
        t->input_buf[t->input_len++] = (char)cp;
    t->input_buf[t->input_len] = '\0';
}

void term_key_input(Terminal *t, int key) {
    if (key == GLFW_KEY_BACKSPACE && t->input_len > 0)
        t->input_buf[--t->input_len] = '\0';
    else if (key == GLFW_KEY_ENTER)
        term_send_input(t);
}

void term_render(Terminal *t, float x, float y, float w, float h) {
    draw_rect(x, y, w, h, 0.08f, 0.08f, 0.10f, 1.0f);
    draw_rect(x, y, w, 1.0f, 0.2f, 0.6f, 1.0f, 1.0f); // top border

    float ch  = text_char_height();
    float row = ch + 2.0f;
    int   visible = (int)((h - row - 6.0f) / row);
    int   start   = t->line_count - visible + 1;
    if (start < 0) start = 0;

    float ty = y + 6.0f;
    for (int i = start; i <= t->line_count && i < TERM_LINES; i++) {
        if (ty + row > y + h - row) break;
        draw_text(t->lines[i], x + 6.0f, ty + ch * 0.85f, 0.8f, 0.9f, 0.8f);
        ty += row;
    }

    // Input line
    char prompt[TERM_LINE_LEN + 4];
    snprintf(prompt, sizeof(prompt), "> %s_", t->input_buf);
    draw_rect(x, y + h - row - 4.0f, w, row + 4.0f, 0.05f, 0.05f, 0.07f, 1.0f);
    draw_text(prompt, x + 6.0f, y + h - 4.0f, 0.4f, 1.0f, 0.5f);
}
