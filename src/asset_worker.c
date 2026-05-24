// =========================================================================
// asset_worker.c — Background thread pool for thumbnail loading & caching
// =========================================================================
#include "types.h"
#include <shlobj.h>
#include <stdio.h>

#define WM_CALBUM_LOAD_COMPLETE (WM_APP + 1)

static wchar_t g_cache_dir[MAX_PATH_LEN];

static uint64_t hash_path(const wchar_t *path) {
    uint64_t hash = 14695981039346656037ULL;
    while (*path) {
        hash ^= (uint64_t)(*path++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void ensure_cache_dir() {
    if (g_cache_dir[0] == 0) {
        PWSTR appdata;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &appdata))) {
            swprintf(g_cache_dir, MAX_PATH_LEN, L"%s\\TaskSlinger", appdata);
            CreateDirectoryW(g_cache_dir, NULL);
            swprintf(g_cache_dir, MAX_PATH_LEN, L"%s\\TaskSlinger\\Cache", appdata);
            CreateDirectoryW(g_cache_dir, NULL);
            CoTaskMemFree(appdata);
        }
    }
}

DWORD WINAPI aw_worker_thread(LPVOID param)
{
    AppState *s = (AppState *)param;
    ensure_cache_dir();

    for (;;) {
        LoadRequest *req = NULL;
        EnterCriticalSection(&s->work_lock);
        if (s->ring_head != s->ring_tail) {
            req = (LoadRequest *)s->ring_slots[s->ring_head];
            s->ring_head = (s->ring_head + 1) % RING_CAPACITY;
        }
        LeaveCriticalSection(&s->work_lock);

        if (!req) {
            HANDLE events[2] = { s->ring_nonempty, s->worker_stop_event };
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) return 0; 
            continue;
        }

        void *bc1 = NULL;
        int bc1_size = 0;

        if (req->path[0] != L'\0') {
            const wchar_t *path = req->path;
            
            wchar_t cache_path[MAX_PATH_LEN];
            swprintf(cache_path, MAX_PATH_LEN, L"%s\\%llx_%d.bc1", g_cache_dir, hash_path(path), req->thumb_size);

            HANDLE hFile = CreateFileW(cache_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                bc1_size = GetFileSize(hFile, NULL);
                if (bc1_size > 0) {
                    bc1 = malloc(bc1_size);
                    DWORD bytesRead;
                    ReadFile(hFile, bc1, bc1_size, &bytesRead, NULL);
                }
                CloseHandle(hFile);
            }

            if (!bc1) {
                bc1 = il_load_and_compress(path, req->thumb_size, &bc1_size);
                if (bc1) {
                    hFile = CreateFileW(cache_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD bytesWritten;
                        WriteFile(hFile, bc1, bc1_size, &bytesWritten, NULL);
                        CloseHandle(hFile);
                    }
                }
            }
        }

        if (req->target_hwnd) {
            LoadResult *result = (LoadResult *)malloc(sizeof(LoadResult));
            if (result) {
                wcsncpy(result->path, req->path, MAX_PATH_LEN-1)[MAX_PATH_LEN-1] = L'\0';
                result->bc1_data    = bc1;
                result->bc1_size    = bc1_size;
                result->succeeded   = (bc1 != NULL);
                PostMessageW(req->target_hwnd, WM_CALBUM_LOAD_COMPLETE, 0, (LPARAM)result);
            }
        }

        free(req);
    }
    return 0;
}

int aw_start_workers(AppState *s)
{
    if (s->worker_threads[0]) return 1;

    s->worker_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->ring_head = s->ring_tail = 0;

    for (int i = 0; i < NUM_WORKERS; i++) {
        s->worker_threads[i] = CreateThread(NULL, 0, aw_worker_thread, s, 0, NULL);
    }
    return 1;
}

void aw_stop_workers(AppState *s)
{
    if (!s->worker_threads[0]) return;

    if (s->worker_stop_event) SetEvent(s->worker_stop_event);
    SetEvent(s->ring_nonempty);

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (s->worker_threads[i]) {
            WaitForSingleObject(s->worker_threads[i], 2000);
            CloseHandle(s->worker_threads[i]);
            s->worker_threads[i] = NULL;
        }
    }
    if (s->worker_stop_event) { CloseHandle(s->worker_stop_event); s->worker_stop_event = NULL; }
}

int aw_request_thumbnail(AppState *s, const wchar_t *path, int thumb_size, HWND hwnd)
{
    LoadRequest *req = (LoadRequest *)malloc(sizeof(LoadRequest));
    if (!req) return 0;
    wcsncpy(req->path, path, MAX_PATH_LEN-1)[MAX_PATH_LEN-1] = L'\0';
    req->thumb_size  = thumb_size;
    req->target_hwnd = hwnd;

    int ok = 0;
    EnterCriticalSection(&s->work_lock);
    int next = (s->ring_tail + 1) % RING_CAPACITY;
    if (next != s->ring_head) {
        s->ring_slots[s->ring_tail] = req;
        s->ring_tail = next;
        ok = 1;
    }
    LeaveCriticalSection(&s->work_lock);

    if (ok) {
        SetEvent(s->ring_nonempty);
    } else {
        free(req);
    }
    return ok;
}
