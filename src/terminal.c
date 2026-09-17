#include "terminal.h"
#include "text_renderer.h"
#include "colors.h"
#include "GLFW/glfw3.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#endif

void term_init(Terminal *t) {
    memset(t, 0, sizeof(*t));

#ifdef _WIN32
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
#else
    int stdin_pipe[2], stdout_pipe[2];
    pipe(stdin_pipe);
    pipe(stdout_pipe);

    pid_t pid = fork();
    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        execl("/bin/sh", "/bin/sh", NULL);
        _exit(1);
    }
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    t->pid = pid;
    t->stdin_fd = stdin_pipe[1];
    t->stdout_fd = stdout_pipe[0];

    // Set stdout_fd non-blocking
    int flags = fcntl(t->stdout_fd, F_GETFL, 0);
    fcntl(t->stdout_fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void term_free(Terminal *t) {
#ifdef _WIN32
    if (t->proc)        { TerminateProcess(t->proc, 0); CloseHandle(t->proc); }
    if (t->stdin_write) CloseHandle(t->stdin_write);
    if (t->stdout_read) CloseHandle(t->stdout_read);
#else
    if (t->pid > 0) {
        kill(t->pid, SIGTERM);
        waitpid(t->pid, NULL, 0);
    }
    if (t->stdin_fd > 0)  close(t->stdin_fd);
    if (t->stdout_fd > 0) close(t->stdout_fd);
#endif
}

static void term_push_text(Terminal *t, const char *buf, int n) {
    for (int i = 0; i < n; i++) {
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
#ifdef _WIN32
    if (!t->stdout_read) return;
    DWORD avail = 0;
    while (PeekNamedPipe(t->stdout_read, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char buf[1024];
        DWORD read = 0;
        if (ReadFile(t->stdout_read, buf, sizeof(buf) - 1, &read, NULL) && read > 0)
            term_push_text(t, buf, (int)read);
    }
#else
    if (t->stdout_fd <= 0) return;
    char buf[1024];
    ssize_t n;
    while ((n = read(t->stdout_fd, buf, sizeof(buf) - 1)) > 0)
        term_push_text(t, buf, (int)n);
#endif
}

void term_send_input(Terminal *t) {
    if (t->input_len == 0) return;

    // Handle "clear" / "cls" locally
    if ((strcmp(t->input_buf, "clear") == 0 || strcmp(t->input_buf, "cls") == 0)) {
        t->line_count = 0;
        t->lines[0][0] = '\0';
        t->input_len = 0;
        t->input_buf[0] = '\0';
        return;
    }

    t->input_buf[t->input_len++] = '\n';
#ifdef _WIN32
    if (!t->stdin_write) return;
    DWORD written;
    WriteFile(t->stdin_write, t->input_buf, t->input_len, &written, NULL);
#else
    if (t->stdin_fd <= 0) return;
    write(t->stdin_fd, t->input_buf, t->input_len);
#endif
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
    draw_rect(x, y, w, h, COLOR_TERM_BACKGROUND, 1.0f);
    draw_rect(x, y, w, 1.0f, COLOR_TERM_BORDER, 1.0f);

    float ch  = text_char_height();
    float row = ch + 2.0f;
    int   visible = (int)((h - row - 6.0f) / row);
    int   start   = t->line_count - visible + 1;
    if (start < 0) start = 0;

    float ty = y + 6.0f;
    for (int i = start; i <= t->line_count && i < TERM_LINES; i++) {
        if (ty + row > y + h - row) break;
        draw_text(t->lines[i], x + 6.0f, ty + ch * 0.85f, COLOR_TERM_TEXT);
        ty += row;
    }

    // Input line
    char prompt[TERM_LINE_LEN + 4];
    snprintf(prompt, sizeof(prompt), "> %s_", t->input_buf);
    draw_rect(x, y + h - row - 4.0f, w, row + 4.0f, COLOR_TERM_INPUT_BG, 1.0f);
    draw_text(prompt, x + 6.0f, y + h - 4.0f, 0.4f, 1.0f, 0.5f);
}

void term_set_cwd(Terminal *t, const char *path) {
    if (!path || !path[0]) return;
    // Extract directory from file path
    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *sep = strrchr(dir, '\\');
    if (!sep) sep = strrchr(dir, '/');
    if (sep) *sep = '\0';
    else return;

    // Send cd command to the shell
#ifdef _WIN32
    if (!t->stdin_write) return;
    char cmd[1100];
    snprintf(cmd, sizeof(cmd), "cd /d \"%s\"\n", dir);
    DWORD written;
    WriteFile(t->stdin_write, cmd, (DWORD)strlen(cmd), &written, NULL);
#else
    if (t->stdin_fd <= 0) return;
    char cmd[1100];
    snprintf(cmd, sizeof(cmd), "cd '%s'\n", dir);
    write(t->stdin_fd, cmd, strlen(cmd));
#endif
}
