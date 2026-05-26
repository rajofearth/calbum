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
    rb_init(&s->work_queue, s->ring_slots, RING_CAPACITY);

    s->view_mode = VIEW_GALLERY;
    s->selected_index = -1;
    s->sort_mode = SORT_DATE_CREATED;
    s->sort_descending = 1; // Default to newest first
    s->needs_redraw = 1;
}

void app_shutdown(AppState *s)
{
    fm_stop_monitor(s);
    aw_stop_workers(s);
    r_free_full_image(s);

    if (s->images)
        for (int i = 0; i < s->count; i++) {
            if (s->images[i].texture_slot != -1) {
                r_evict_texture(s, s->images[i].texture_slot);
            }
        }

    if (s->arena.buf) VirtualFree(s->arena.buf, 0, MEM_RELEASE);
    rb_destroy(&s->work_queue);
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
    // ── 1. Stop background threads ────────────────────────────────────
    fm_stop_monitor(s);
    aw_stop_workers(s);

    // ── 2. Drain any in-flight LoadResults from the message queue ─────
    MSG msg;
    while (PeekMessageW(&msg, s->hwnd, WM_CALBUM_LOAD_COMPLETE,
                        WM_CALBUM_LOAD_COMPLETE, PM_REMOVE))
    {
        LoadResult *result = (LoadResult *)msg.lParam;
        if (result && result->bc1_data) {
            il_free_bc1_data(result->bc1_data);
        }
        free(result);
    }

    // ── 3. Free old image resources ──────────────────────────────────
    r_free_full_image(s);
    if (s->images)
        for (int i = 0; i < s->count; i++) {
            if (s->images[i].texture_slot != -1) {
                r_evict_texture(s, s->images[i].texture_slot);
            }
        }

    // ── 4. Reset arena and image array ───────────────────────────────
    arena_reset(&s->arena);
    s->count = s->capacity = 0;
    s->images = NULL;
    s->work_queue.head = s->work_queue.tail = 0;

    // ── 5. Reset view state ──────────────────────────────────────────
    s->view_mode = VIEW_GALLERY;
    s->selected_index = -1;
    s->scroll_target_y = s->scroll_current_y = 0.0f;
    s->needs_redraw = 1;

    // ── 6. Scan the new folder ───────────────────────────────────────
    fs_scan_directory(path, s);
    gal_apply_sort(s);
    if (s->count > 0) s->selected_index = 0;

    // ── 7. Restart background threads (clean state) ──────────────────
    fm_start_monitor(s, path);
    aw_start_workers(s);

    gal_update_layout(s);
    app_update_title(s);
}

void app_update_title(AppState *s)
{
    wchar_t title[MAX_PATH_LEN + 32];
    wsprintfW(title, L"calbum — %s", s->current_dir);
    SetWindowTextW(s->hwnd, title);
}

ImageEntry* app_append_image_entry(AppState *s, const wchar_t *path, const wchar_t *filename, uint64_t file_size, uint64_t last_modified, uint64_t created_time)
{
    if (s->count >= s->capacity) {
        int new_cap = s->capacity ? s->capacity * 2 : 256;
        size_t sz   = new_cap * sizeof(ImageEntry);
        size_t align = 16, mask = align - 1;
        size_t off = (s->arena.offset + mask) & ~mask;
        if (off + sz > s->arena.capacity) return NULL;

        ImageEntry *old_images = s->images;
        s->images = (ImageEntry *)(s->arena.buf + off);
        s->arena.offset = off + sz;
        // Copy surviving entries into the new block so nothing is lost
        if (old_images && s->count > 0)
            memcpy(s->images, old_images, s->count * sizeof(ImageEntry));
        s->capacity = new_cap;
    }

    size_t path_sz = (wcslen(path) + 1) * sizeof(wchar_t);
    size_t name_sz = (wcslen(filename) + 1) * sizeof(wchar_t);
    wchar_t *p_path = (wchar_t *)arena_alloc(&s->arena, path_sz);
    wchar_t *p_name = (wchar_t *)arena_alloc(&s->arena, name_sz);
    if (!p_path || !p_name) return NULL;

    wcscpy(p_path, path);
    wcscpy(p_name, filename);

    ImageEntry *e = &s->images[s->count];
    e->path = p_path;
    e->filename = p_name;
    e->file_size = file_size;
    e->last_modified = last_modified;
    e->created_time = created_time;
    e->texture_slot = -1;
    e->state = IMG_STATE_NEW;
    e->thumb_requested = 0;
    e->full_width = 0;
    e->full_height = 0;
    s->count++;
    return e;
}
