#ifndef UPDATE_H
#define UPDATE_H

#define UPDATE_CURRENT_VERSION "0.1.0"
#define UPDATE_REPO "urboirad/RMGBE"

typedef enum {
    UPDATE_IDLE,
    UPDATE_CHECKING,
    UPDATE_AVAILABLE,
    UPDATE_UPTODATE,
    UPDATE_DOWNLOADING,
    UPDATE_READY,
    UPDATE_FAILED
} UpdateStatus;

typedef struct {
    UpdateStatus status;
    char latest_version[32];
    char download_url[512];
    char error_msg[256];
    float progress;     // 0.0 to 1.0 during download
} UpdateState;

// Start a background check for updates.
void update_check_start(UpdateState *s);

// Start downloading the update in the background.
void update_download_start(UpdateState *s, const char *exe_dir);

// Returns 1 if the version strings differ (a > b in semver).
int update_version_newer(const char *a, const char *b);

#endif
