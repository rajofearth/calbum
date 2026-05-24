// =========================================================================
// app.c — Application lifecycle
// =========================================================================
#include "types.h"
#include <shellapi.h>
#include <shlobj.h>

void app_init(AppState *s)
{
    ZeroMemory(s, sizeof(*s));

    // Arena
    void *arena = VirtualAlloc(NULL, ARENA_CAPACITY, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    arena_init(&s->arena, arena, ARENA_CAPACITY);

    // Frame timing
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    s->perf_counter_freq = freq.QuadPart;
    QueryPerformanceCounter(&now);
    s->last_tick = now.QuadPart;

    // Ring buffer fields
    s->ring_head = s->ring_tail = 0;
    InitializeCriticalSection(&s->work_lock);
    s->ring_nonempty = CreateEventW(NULL, FALSE, FALSE, NULL);

    s->view_mode = VIEW_GALLERY;
    s->selected_index = -1;
    s->needs_redraw = 1;
}

void app_shutdown(AppState *s)
{
    fm_stop_monitor(s);
    aw_stop_workers(s);

    if (s->images)
        for (int i = 0; i < s->count; i++) {
            if (s->images[i].thumbnail)  DeleteObject(s->images[i].thumbnail);
            if (s->images[i].full_image) DeleteObject(s->images[i].full_image);
        }

    if (s->arena.buf) VirtualFree(s->arena.buf, 0, MEM_RELEASE);
    DeleteCriticalSection(&s->work_lock);
    if (s->ring_nonempty) CloseHandle(s->ring_nonempty);
}

void get_pictures_folder(wchar_t *buf, int len)
{
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, SHGFP_TYPE_CURRENT, buf)))
        return;
    DWORD ret = GetEnvironmentVariableW(L"USERPROFILE", buf, len);
    if (ret > 0 && ret < (DWORD)len - 20) { wcscat(buf, L"\\Pictures"); return; }
    wchar_t drv[4] = L"C:";
    GetEnvironmentVariableW(L"HOMEDRIVE", drv, 4);
    wchar_t path[MAX_PATH_LEN];
    if (GetEnvironmentVariableW(L"HOMEPATH", path, MAX_PATH_LEN))
        wsprintfW(buf, L"%s%s\\Pictures", drv, path);
    else
        wcsncpy(buf, L"C:\\Users\\Public\\Pictures", len-1);
    buf[len-1] = L'\0';
}

void app_load_folder(AppState *s, const wchar_t *path)
{
    fm_stop_monitor(s);

    if (s->images)
        for (int i = 0; i < s->count; i++) {
            if (s->images[i].thumbnail)  DeleteObject(s->images[i].thumbnail);
            if (s->images[i].full_image) DeleteObject(s->images[i].full_image);
        }
    arena_reset(&s->arena);
    s->count = s->capacity = 0;
    s->images = NULL;

    s->view_mode = VIEW_GALLERY;
    s->selected_index = -1;
    s->scroll_target_y = s->scroll_current_y = 0.0f;
    s->needs_redraw = 1;

    fs_scan_directory(path, s);
    if (s->count > 0) s->selected_index = 0;
    fm_start_monitor(s, path);
    gal_update_layout(s);
    app_update_title(s);
}

void app_update_title(AppState *s)
{
    wchar_t title[MAX_PATH_LEN + 32];
    wsprintfW(title, L"calbum — %s", s->current_dir);
    SetWindowTextW(s->hwnd, title);
}
