// =========================================================================
// file_monitor.c — Real-time directory change notification
//
// Uses ReadDirectoryChangesW with overlapped I/O on a background thread.
// File changes are posted to the main thread via custom window message.
// =========================================================================
#include "types.h"
#include <stdlib.h>

#define WM_CALBUM_FILE_CHANGE (WM_APP + 2)

// Map NT file notify action to our FileChange type enum
static int map_action(DWORD action)
{
    switch (action) {
    case FILE_ACTION_ADDED:            return CHANGE_ADDED;
    case FILE_ACTION_REMOVED:          return CHANGE_REMOVED;
    case FILE_ACTION_MODIFIED:         return CHANGE_MODIFIED;
    case FILE_ACTION_RENAMED_OLD_NAME: return CHANGE_REMOVED;
    case FILE_ACTION_RENAMED_NEW_NAME: return CHANGE_ADDED;
    default:                           return -1;
    }
}

static DWORD WINAPI fm_thread_proc(LPVOID param)
{
    AppState *s = (AppState *)param;
    HANDLE hDir = s->dir_handle;

    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_SIZE |
                   FILE_NOTIFY_CHANGE_LAST_WRITE;

    // Synchronous ReadDirectoryChangesW polling with stop-event check
    while (WaitForSingleObject(s->monitor_stop_event, 0) != WAIT_OBJECT_0) {
        char buf[4096];
        DWORD bytes = 0;
        if (ReadDirectoryChangesW(hDir, buf, sizeof(buf), TRUE, filter, &bytes, NULL, NULL)) {
            if (bytes > 0) {
                FILE_NOTIFY_INFORMATION *fni = (FILE_NOTIFY_INFORMATION *)buf;
                for (;;) {
                    wchar_t fname[MAX_PATH_LEN] = {0};
                    wcsncpy(fname, fni->FileName,
                            min(fni->FileNameLength / sizeof(wchar_t), MAX_PATH_LEN - 1));

                    wchar_t full[MAX_PATH_LEN];
                    wcsncpy(full, s->current_dir, MAX_PATH_LEN-1);
                    size_t len = wcslen(full);
                    if (len && full[len-1] != L'\\') wcscat(full, L"\\");
                    wcscat(full, fname);

                    // Post change to main thread non-blocking
                    if (s->hwnd) {
                        FileChange *fc = (FileChange *)malloc(sizeof(FileChange));
                        if (fc) {
                            int mapped = map_action(fni->Action);
                            if (mapped >= 0) {
                                fc->type = mapped;
                                wcsncpy(fc->path, full, MAX_PATH_LEN-1);
                                fc->path[MAX_PATH_LEN-1] = L'\0';
                                wcsncpy(fc->filename, fname, MAX_PATH_LEN-1);
                                fc->filename[MAX_PATH_LEN-1] = L'\0';
                                PostMessageW(s->hwnd, WM_CALBUM_FILE_CHANGE, 0, (LPARAM)fc);
                            } else {
                                free(fc);
                            }
                        }
                    }

                    if (!fni->NextEntryOffset) break;
                    fni = (FILE_NOTIFY_INFORMATION *)((char *)fni + fni->NextEntryOffset);
                }
            }
        } else {
            Sleep(1000); // directory unavailable; wait before retry
        }
    }
    return 0;
}

int fm_start_monitor(AppState *s, const wchar_t *directory)
{
    if (s->monitoring_active) return 1;

    s->dir_handle = CreateFileW(directory, FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                NULL);
    if (s->dir_handle == INVALID_HANDLE_VALUE) {
        s->dir_handle = NULL;
        return 0;
    }

    s->monitor_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->monitor_thread = CreateThread(NULL, 0, fm_thread_proc, s, 0, NULL);
    s->monitoring_active = 1;
    return 1;
}

void fm_stop_monitor(AppState *s)
{
    if (!s->monitoring_active) return;
    s->monitoring_active = 0;

    if (s->monitor_stop_event) {
        SetEvent(s->monitor_stop_event);
        WaitForSingleObject(s->monitor_thread, 2000);
        CloseHandle(s->monitor_stop_event);
        s->monitor_stop_event = NULL;
    }
    if (s->monitor_thread) { CloseHandle(s->monitor_thread); s->monitor_thread = NULL; }
    if (s->dir_handle) { CloseHandle(s->dir_handle); s->dir_handle = NULL; }
}
