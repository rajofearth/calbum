// =========================================================================
// asset_worker.c — Background thread pool for thumbnail loading & caching
// =========================================================================
#include "types.h"
#include <shlobj.h>
#include <stdio.h>

static wchar_t g_cache_dir[MAX_PATH_LEN];

static uint64_t hash_path(const wchar_t *path)
{
    uint64_t hash = 14695981039346656037ULL;
    while (*path)
    {
        hash ^= (uint64_t) (*path++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void ensure_cache_dir()
{
    if (g_cache_dir[0] == 0)
    {
        PWSTR appdata;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &appdata)))
        {
            swprintf(g_cache_dir, MAX_PATH_LEN, L"%s\\calbum", appdata);
            CreateDirectoryW(g_cache_dir, NULL);
            swprintf(g_cache_dir, MAX_PATH_LEN, L"%s\\calbum\\Cache", appdata);
            CreateDirectoryW(g_cache_dir, NULL);
            CoTaskMemFree(appdata);
        }
    }
}

DWORD WINAPI aw_worker_thread(LPVOID param)
{
    WorkerState *worker = (WorkerState *) param;
    ensure_cache_dir();

    for (;;)
    {
        if (worker->worker_stop_event && WaitForSingleObject(worker->worker_stop_event, 0) == WAIT_OBJECT_0)
            return 0;

        LoadRequest *req = (LoadRequest *) rb_try_pop(&worker->work_queue);

        if (!req)
        {
            HANDLE events[2] = {worker->work_queue.nonempty, worker->worker_stop_event};
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1)
                return 0;
            continue;
        }

        void *bc1 = NULL;
        int bc1_size = 0;

        if (req->path[0] != L'\0' && !req->is_full_image)
        {
            const wchar_t *path = req->path;

            wchar_t cache_path[MAX_PATH_LEN];
            swprintf(cache_path, MAX_PATH_LEN, L"%s\\%llx_%d.bc1", g_cache_dir, hash_path(path), req->thumb_size);

            HANDLE hFile = CreateFileW(cache_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                bc1_size = (int) GetFileSize(hFile, NULL);
                if (bc1_size > 0)
                {
                    bc1 = malloc(bc1_size);
                    DWORD bytesRead;
                    ReadFile(hFile, bc1, bc1_size, &bytesRead, NULL);
                }
                CloseHandle(hFile);
            }

            if (!bc1)
            {
                bc1 = il_load_and_compress(path, req->thumb_size, &bc1_size);
                if (!bc1)
                {
                    log_error(L"aw_worker: il_load_and_compress failed for %s", req->path);
                }
                else
                {
                    hFile = CreateFileW(cache_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE)
                    {
                        DWORD bytesWritten;
                        WriteFile(hFile, bc1, bc1_size, &bytesWritten, NULL);
                        CloseHandle(hFile);
                    }
                }
            }
        }
        else if (req->is_full_image)
        {
            int w = 0;
            int h = 0;
            void *rgba = il_load_full_image(req->path, &w, &h);
            if (req->target_hwnd)
            {
                FullLoadResult *result = (FullLoadResult *) malloc(sizeof(FullLoadResult));
                if (result)
                {
                    wcsncpy(result->path, req->path, MAX_PATH_LEN - 1)[MAX_PATH_LEN - 1] = L'\0';
                    result->rgba_data = rgba;
                    result->w = w;
                    result->h = h;
                    result->succeeded = (rgba != NULL);
                    PostMessageW(req->target_hwnd, WM_CALBUM_FULL_LOAD_COMPLETE, 0, (LPARAM) result);
                }
            }
        }

        if (req->target_hwnd)
        {
            LoadResult *result = (LoadResult *) malloc(sizeof(LoadResult));
            if (result)
            {
                wcsncpy(result->path, req->path, MAX_PATH_LEN - 1)[MAX_PATH_LEN - 1] = L'\0';
                result->bc1_data = bc1;
                result->bc1_size = bc1_size;
                result->succeeded = (bc1 != NULL);
                PostMessageW(req->target_hwnd, WM_CALBUM_LOAD_COMPLETE, 0, (LPARAM) result);
            }
        }

        free(req);
    }
    return 0;
}

int aw_start_workers(WorkerState *worker)
{
    if (worker->worker_threads[0])
        return 1;

    worker->worker_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!worker->worker_stop_event)
        log_error(L"aw_start_workers: CreateEventW failed");

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        worker->worker_threads[i] = CreateThread(NULL, 0, aw_worker_thread, worker, 0, NULL);
        if (!worker->worker_threads[i])
            log_error(L"aw_start_workers: CreateThread failed for worker %d", i);
    }
    return 1;
}

void aw_stop_workers(WorkerState *worker)
{
    if (!worker->worker_threads[0])
        return;

    if (worker->worker_stop_event)
        SetEvent(worker->worker_stop_event);
    SetEvent(worker->work_queue.nonempty);

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        if (worker->worker_threads[i])
        {
            WaitForSingleObject(worker->worker_threads[i], INFINITE);
            CloseHandle(worker->worker_threads[i]);
            worker->worker_threads[i] = NULL;
        }
    }
    if (worker->worker_stop_event)
    {
        CloseHandle(worker->worker_stop_event);
        worker->worker_stop_event = NULL;
    }
}

int aw_request_thumbnail(WorkerState *worker, const wchar_t *path, int thumb_size, HWND hwnd)
{
    LoadRequest *req = (LoadRequest *) malloc(sizeof(LoadRequest));
    if (!req)
    {
        log_error(L"aw_request: malloc failed");
        return 0;
    }
    wcsncpy(req->path, path, MAX_PATH_LEN - 1)[MAX_PATH_LEN - 1] = L'\0';
    req->thumb_size = thumb_size;
    req->target_hwnd = hwnd;
    req->is_full_image = 0;

    if (rb_push(&worker->work_queue, req))
    {
        return 1;
    }
    free(req);
    return 0;
}

int aw_request_full_image(WorkerState *worker, const wchar_t *path, HWND hwnd)
{
    LoadRequest *req = (LoadRequest *) malloc(sizeof(LoadRequest));
    if (!req)
    {
        log_error(L"aw_request: malloc failed");
        return 0;
    }
    wcsncpy(req->path, path, MAX_PATH_LEN - 1)[MAX_PATH_LEN - 1] = L'\0';
    req->thumb_size = 0;
    req->target_hwnd = hwnd;
    req->is_full_image = 1;

    if (rb_push(&worker->work_queue, req))
    {
        return 1;
    }
    free(req);
    return 0;
}
