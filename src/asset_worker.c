// =========================================================================
// asset_worker.c — Background thread pool for thumbnail loading
//
// Workers pull LoadRequest items from a ring buffer, decode the image,
// create an HBITMAP, and post a LoadResult back to the main thread
// via a custom window message (WM_APP + 1).
// =========================================================================
#include "types.h"

#define WM_CALBUM_LOAD_COMPLETE (WM_APP + 1)

DWORD WINAPI aw_worker_thread(LPVOID param)
{
    AppState *s = (AppState *)param;

    for (;;) {
        // Try to pop a work item (non-blocking first, then wait)
        LoadRequest *req = NULL;
        EnterCriticalSection(&s->work_lock);
        if (s->ring_head != s->ring_tail) {
            req = (LoadRequest *)s->ring_slots[s->ring_head];
            s->ring_head = (s->ring_head + 1) % RING_CAPACITY;
        }
        LeaveCriticalSection(&s->work_lock);

        if (!req) {
            // Wait for signal or shutdown
            HANDLE events[2] = { s->ring_nonempty, s->worker_stop_event };
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) return 0; // stop event
            continue;
        }

        // Load the thumbnail
        HBITMAP bmp = NULL;
        if (req->image_index >= 0 && req->image_index < s->count) {
            bmp = il_load_thumbnail(s->images[req->image_index].path, req->thumb_size);
        }

        // Post result back to main thread
        if (req->target_hwnd) {
            LoadResult *result = (LoadResult *)malloc(sizeof(LoadResult));
            if (result) {
                result->image_index = req->image_index;
                result->bitmap      = bmp;
                result->succeeded   = (bmp != NULL);
                PostMessageW(req->target_hwnd, WM_CALBUM_LOAD_COMPLETE, 0, (LPARAM)result);
            }
        }

        // Free the request
        free(req);
    }
    return 0;
}

int aw_start_workers(AppState *s)
{
    if (s->worker_threads[0]) return 1; // already running

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
    SetEvent(s->ring_nonempty); // wake up workers

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (s->worker_threads[i]) {
            WaitForSingleObject(s->worker_threads[i], 2000);
            CloseHandle(s->worker_threads[i]);
            s->worker_threads[i] = NULL;
        }
    }
    if (s->worker_stop_event) { CloseHandle(s->worker_stop_event); s->worker_stop_event = NULL; }
}

int aw_request_thumbnail(AppState *s, int image_index, int thumb_size, HWND hwnd)
{
    // Allocate request
    LoadRequest *req = (LoadRequest *)malloc(sizeof(LoadRequest));
    if (!req) return 0;
    req->image_index = image_index;
    req->thumb_size  = thumb_size;
    req->target_hwnd = hwnd;

    // Push to ring buffer
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
