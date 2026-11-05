#include "terminal.h"
#include "text_renderer.h"
#include "colors.h"
#include "GLFW/glfw3.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#endif

/* -------------------------------------------------------------------------
 * Small platform helpers.  All shell I/O goes through these so Windows and
 * POSIX share the same calling conventions and error handling.
 * ---------------------------------------------------------------------- */

static int term_write(Terminal *t, const char *buf, int len) {
    if (!t || !buf || len <= 0) return 0;
#ifdef _WIN32
    if (!t->stdin_write) return 0;
    DWORD written = 0;
    while (len > 0) {
        DWORD chunk = (DWORD)(len > 0x7FFFFFFF ? 0x7FFFFFFF : len);
        if (!WriteFile(t->stdin_write, buf, chunk, &written, NULL) || written == 0)
            return 0;
        buf += written;
        len -= (int)written;
    }
    return 1;
#else
    if (t->stdin_fd < 0) return 0;
    while (len > 0) {
        ssize_t n = write(t->stdin_fd, buf, (size_t)len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (n == 0) return 0;
        buf += n;
        len -= (int)n;
    }
    return 1;
#endif
}

/* Returns bytes read, 0 when nothing is available (or the pipe closed), and
 * -1 on a hard error. */
static int term_read(Terminal *t, char *buf, int cap) {
#ifdef _WIN32
    if (!t->stdout_read || cap <= 0) return 0;
    DWORD avail = 0;
    if (!PeekNamedPipe(t->stdout_read, NULL, 0, NULL, &avail, NULL)) return -1;
    if (avail == 0) return 0;
    DWORD want = (DWORD)cap;
    if (avail < want) want = avail;
    DWORD got = 0;
    if (!ReadFile(t->stdout_read, buf, want, &got, NULL)) return -1;
    return (int)got;
#else
    if (t->stdout_fd < 0 || cap <= 0) return 0;
    ssize_t n = read(t->stdout_fd, buf, (size_t)cap);
    if (n < 0) {
        if (errno == EINTR) return 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (int)n;
#endif
}

void term_init(Terminal *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));

#ifdef _WIN32
    t->proc = NULL;
    t->stdin_write = NULL;
    t->stdout_read = NULL;

    HANDLE stdin_read = NULL, stdout_write = NULL;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&stdin_read, &t->stdin_write, &sa, 0)) return;
    if (!CreatePipe(&t->stdout_read, &stdout_write, &sa, 0)) {
        CloseHandle(stdin_read);
        CloseHandle(t->stdin_write);
        t->stdin_write = NULL;
        return;
    }
    SetHandleInformation(t->stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(t->stdout_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError  = stdout_write;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessA(NULL, "cmd.exe", NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        t->proc = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (!t->proc) {
        CloseHandle(t->stdin_write);
        CloseHandle(t->stdout_read);
        t->stdin_write = NULL;
        t->stdout_read = NULL;
    }
#else
    t->pid = 0;
    t->stdin_fd = -1;
    t->stdout_fd = -1;

    int stdin_pipe[2] = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    if (pipe(stdin_pipe) != 0) return;
    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return;
    }
    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        if (dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
            dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(stdout_pipe[1], STDERR_FILENO) < 0)
            _exit(127);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    t->pid = pid;
    t->stdin_fd = stdin_pipe[1];
    t->stdout_fd = stdout_pipe[0];

    int flags = fcntl(t->stdout_fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(t->stdout_fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void term_free(Terminal *t) {
    if (!t) return;
#ifdef _WIN32
    if (t->proc) {
        TerminateProcess(t->proc, 0);
        CloseHandle(t->proc);
        t->proc = NULL;
    }
    if (t->stdin_write) { CloseHandle(t->stdin_write); t->stdin_write = NULL; }
    if (t->stdout_read) { CloseHandle(t->stdout_read); t->stdout_read = NULL; }
#else
    if (t->stdin_fd >= 0) { close(t->stdin_fd); t->stdin_fd = -1; }
    if (t->stdout_fd >= 0) { close(t->stdout_fd); t->stdout_fd = -1; }
    if (t->pid > 0) {
        kill(t->pid, SIGTERM);
        waitpid(t->pid, NULL, 0);
        t->pid = 0;
    }
#endif
}

static void term_push_text(Terminal *t, const char *buf, int n) {
    if (!t || !buf) return;
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (t->line_count < TERM_LINES - 1) {
                t->line_count++;
                t->lines[t->line_count][0] = '\0';
            } else {
                memmove(t->lines[0], t->lines[1],
                        (size_t)(TERM_LINES - 1) * TERM_LINE_LEN);
                t->lines[TERM_LINES - 1][0] = '\0';
            }
        } else {
            int row = t->line_count;
            if (row < 0) row = 0;
            if (row >= TERM_LINES) row = TERM_LINES - 1;
            int len = (int)strlen(t->lines[row]);
            if (len < TERM_LINE_LEN - 1) {
                t->lines[row][len]   = c;
                t->lines[row][len + 1] = '\0';
            }
        }
    }
}

int term_poll_output(Terminal *t) {
    if (!t) return 0;
    int changed = 0;
    char buf[1024];
    for (;;) {
        int n = term_read(t, buf, (int)sizeof(buf));
        if (n <= 0) break;
        term_push_text(t, buf, n);
        changed = 1;
        if (n < (int)sizeof(buf)) break;
    }
    return changed;
}

void term_send_input(Terminal *t) {
    if (!t || t->input_len <= 0) return;

    /* Handle "clear" / "cls" locally */
    if (strcmp(t->input_buf, "clear") == 0 || strcmp(t->input_buf, "cls") == 0) {
        t->line_count = 0;
        t->lines[0][0] = '\0';
        t->input_len = 0;
        t->input_buf[0] = '\0';
        return;
    }

    if (t->input_len >= TERM_LINE_LEN - 1) t->input_len = TERM_LINE_LEN - 2;
    t->input_buf[t->input_len++] = '\n';
    term_write(t, t->input_buf, t->input_len);
    t->input_len = 0;
    t->input_buf[0] = '\0';
}

void term_char_input(Terminal *t, unsigned int cp) {
    if (!t || cp < 32 || cp > 126) return;
    if (t->input_len < TERM_LINE_LEN - 2)
        t->input_buf[t->input_len++] = (char)cp;
    t->input_buf[t->input_len] = '\0';
}

void term_key_input(Terminal *t, int key) {
    if (!t) return;
    if (key == GLFW_KEY_BACKSPACE && t->input_len > 0)
        t->input_buf[--t->input_len] = '\0';
    else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
        term_send_input(t);
}

void term_render(Terminal *t, float x, float y, float w, float h) {
    if (!t) return;
    draw_rect(x, y, w, h, COLOR_TERM_BACKGROUND, 1.0f);
    draw_rect(x, y, w, 1.0f, COLOR_TERM_BORDER, 1.0f);

    float ch  = text_char_height();
    float row = ch + 2.0f;
    int   visible = (int)((h - row - 6.0f) / row);
    if (visible < 1) visible = 1;
    int   start   = t->line_count - visible + 1;
    if (start < 0) start = 0;

    float ty = y + 6.0f;
    for (int i = start; i <= t->line_count && i < TERM_LINES; i++) {
        if (ty + row > y + h - row) break;
        draw_text(t->lines[i], x + 6.0f, ty + ch * 0.85f, COLOR_TERM_TEXT);
        ty += row;
    }

    /* Input line */
    char prompt[TERM_LINE_LEN + 4];
    snprintf(prompt, sizeof(prompt), "> %s_", t->input_buf);
    draw_rect(x, y + h - row - 4.0f, w, row + 4.0f, COLOR_TERM_INPUT_BG, 1.0f);
    draw_text(prompt, x + 6.0f, y + h - 4.0f, 0.4f, 1.0f, 0.5f);
}

/* Append to a bounded command buffer, always leaving room for NUL. */
static void append_cmd(char *dst, size_t cap, size_t *used, const char *src) {
    size_t n = strlen(src);
    if (*used + n >= cap) n = cap - *used - 1;
    if (n > 0) {
        memcpy(dst + *used, src, n);
        *used += n;
    }
    dst[*used] = '\0';
}

void term_set_cwd(Terminal *t, const char *path) {
    if (!t || !path || !path[0]) return;

    /* Extract the directory from the path (which may be a file). */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *sep = strrchr(dir, '\\');
    if (!sep) sep = strrchr(dir, '/');
    if (!sep) return;
    *sep = '\0';
    if (!dir[0]) return;

    char cmd[2200];
    size_t used = 0;
    cmd[0] = '\0';
#ifdef _WIN32
    append_cmd(cmd, sizeof(cmd), &used, "cd /d \"");
    append_cmd(cmd, sizeof(cmd), &used, dir);
    append_cmd(cmd, sizeof(cmd), &used, "\"\n");
#else
    append_cmd(cmd, sizeof(cmd), &used, "cd '");
    for (const char *p = dir; *p; p++) {
        if (*p == '\'') append_cmd(cmd, sizeof(cmd), &used, "'\\''");
        else {
            char one[2] = { *p, '\0' };
            append_cmd(cmd, sizeof(cmd), &used, one);
        }
    }
    append_cmd(cmd, sizeof(cmd), &used, "'\n");
#endif
    term_write(t, cmd, (int)used);
}
