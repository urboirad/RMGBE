#include "update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Run a command and read its output into buf (max buf_size bytes).
// Returns the number of bytes read, or 0 on failure.
static int run_and_capture(const char *cmd, char *buf, int buf_size) {
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    int total = 0;
    while (total < buf_size - 1) {
        int n = (int)fread(buf + total, 1, buf_size - 1 - total, f);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    pclose(f);
    return total;
}

// Find a JSON string value by key. Writes the value (without quotes) into out.
// Returns 1 on success, 0 if not found.
static int json_get_string(const char *json, const char *key, char *out, int out_size) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++; // skip ':'
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++; // skip opening quote
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p+1)) { p++; } // skip escape
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

int update_version_newer(const char *a, const char *b) {
    int a1 = 0, a2 = 0, a3 = 0;
    int b1 = 0, b2 = 0, b3 = 0;
    // skip leading 'v' or 'V'
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
    sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
    if (a1 != b1) return a1 > b1;
    if (a2 != b2) return a2 > b2;
    return a3 > b3;
}

void update_check_start(UpdateState *s) {
    s->status = UPDATE_CHECKING;
    s->error_msg[0] = '\0';
    s->latest_version[0] = '\0';
    s->download_url[0] = '\0';

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 10 https://api.github.com/repos/%s/releases/latest",
        UPDATE_REPO);

    char buf[4096];
    int n = run_and_capture(cmd, buf, sizeof(buf));
    if (n == 0) {
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "Could not reach GitHub");
        return;
    }

    // Check for API rate limit or error
    if (strstr(buf, "\"message\"")) {
        char msg[256];
        if (json_get_string(buf, "message", msg, sizeof(msg))) {
            s->status = UPDATE_FAILED;
            snprintf(s->error_msg, sizeof(s->error_msg), "GitHub: %s", msg);
            return;
        }
    }

    if (!json_get_string(buf, "tag_name", s->latest_version, sizeof(s->latest_version))) {
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "Could not parse release info");
        return;
    }

    // Look for the .exe asset download URL
    // Search for "browser_download_url" that contains ".exe"
    const char *asset_start = strstr(buf, "\"assets\"");
    if (asset_start) {
        const char *p = asset_start;
        while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
            char url[512];
            if (json_get_string(p, "browser_download_url", url, sizeof(url))) {
                if (strstr(url, ".exe")) {
                    strncpy(s->download_url, url, sizeof(s->download_url) - 1);
                    break;
                }
            }
            p++;
        }
    }

    if (update_version_newer(s->latest_version, UPDATE_CURRENT_VERSION)) {
        s->status = UPDATE_AVAILABLE;
    } else {
        s->status = UPDATE_UPTODATE;
    }
}

void update_download_start(UpdateState *s, const char *exe_dir) {
    if (!s->download_url[0]) {
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "No download URL");
        return;
    }

    s->status = UPDATE_DOWNLOADING;
    s->progress = 0.0f;

    // Build paths: <dir>/RMGBE.exe (current), <dir>/RMGBE_update.tmp (download), <dir>/RMGBE_old.exe (backup)
    char dir[1024];
    strncpy(dir, exe_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char exe_path[1024], tmp_path[1024], old_path[1024];
    snprintf(exe_path, sizeof(exe_path), "%s\\RMGBE.exe", dir);
    snprintf(tmp_path, sizeof(tmp_path), "%s\\RMGBE_update.tmp", dir);
    snprintf(old_path, sizeof(old_path), "%s\\RMGBE_old.exe", dir);

    // Remove any leftover old backup
    remove(old_path);

    // Download to temp file
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -L -s -o \"%s\" \"%s\"", tmp_path, s->download_url);

    FILE *f = popen(cmd, "r");
    if (f) {
        pclose(f);
        s->progress = 1.0f;
    } else {
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "Download failed");
        return;
    }

    // Verify the download is valid
    FILE *check = fopen(tmp_path, "rb");
    if (!check) {
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "Downloaded file not found");
        return;
    }
    fseek(check, 0, SEEK_END);
    long sz = ftell(check);
    fclose(check);
    if (sz < 10000) {
        remove(tmp_path);
        s->status = UPDATE_FAILED;
        snprintf(s->error_msg, sizeof(s->error_msg), "Downloaded file too small (%ld bytes)", sz);
        return;
    }

    // Can't overwrite a running exe on Windows, so rename current -> old, new -> current
    rename(exe_path, old_path);
    rename(tmp_path, exe_path);
    s->status = UPDATE_READY;
}
