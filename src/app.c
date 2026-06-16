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
    void *arena = VirtualAlloc(NULL, ARENA_CAPACITY, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!arena)
    {
        log_error(L"app_init: VirtualAlloc for arena failed (size=%llu)", ARENA_CAPACITY);
        return;
    }
    arena_init(&s->data.arena, arena, ARENA_CAPACITY);

    // nav_arena (2MB block)
    void *nav_arena_buf = VirtualAlloc(NULL, (SIZE_T) (2 * 1024 * 1024), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!nav_arena_buf)
    {
        log_error(L"app_init: VirtualAlloc for nav_arena failed (size=2097152)");
        return;
    }
    arena_init(&s->data.nav_arena, nav_arena_buf, (size_t) (2 * 1024 * 1024));

    // Frame timing
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    s->perf_counter_freq = freq.QuadPart;
    QueryPerformanceCounter(&now);
    s->last_tick = now.QuadPart;

    // Ring buffer fields
    rb_init(&s->worker.work_queue, (void *) s->worker.ring_slots, RING_CAPACITY);

    s->view.view_mode = VIEW_GALLERY;
    s->view.selected_index = -1;
    s->view.sort_mode = SORT_DATE_CREATED;
    s->view.sort_descending = 1; // Default to newest first
    s->needs_redraw = 1;
}

void app_shutdown(AppState *s, GpuState *r, TextState *txt)
{
    (void) txt;
    fm_stop_monitor(&s->worker);
    aw_stop_workers(&s->worker);
    r_free_full_image(r);

    if (s->data.images)
    {
        for (int i = 0; i < s->data.count; i++)
        {
            if (s->data.images[i].texture_slot != -1)
            {
                r_evict_texture(r, &s->data, s->data.images[i].texture_slot);
            }
        }
    }

    if (s->data.arena.buf)
        VirtualFree(s->data.arena.buf, 0, MEM_RELEASE);
    if (s->data.nav_arena.buf)
        VirtualFree(s->data.nav_arena.buf, 0, MEM_RELEASE);
    rb_destroy(&s->worker.work_queue);
}

void app_get_pictures_folder(wchar_t *buf, int len)
{
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, SHGFP_TYPE_CURRENT, buf)))
        return;
    DWORD ret = GetEnvironmentVariableW(L"USERPROFILE", buf, len);
    if (ret > 0 && ret < (DWORD) len - 20)
    {
        wcscat(buf, L"\\Pictures");
        return;
    }
    wchar_t drv[4] = L"C:";
    GetEnvironmentVariableW(L"HOMEDRIVE", drv, 4);
    wchar_t path[MAX_PATH_LEN];
    if (GetEnvironmentVariableW(L"HOMEPATH", path, MAX_PATH_LEN))
    {
        wsprintfW(buf, L"%s%s\\Pictures", drv, path);
    }
    else
    {
        wcsncpy(buf, L"C:\\Users\\Public\\Pictures", len - 1);
    }
    buf[len - 1] = L'\0';
}

void app_load_folder(AppState *s, GpuState *r, TextState *txt, const wchar_t *path)
{
    (void) txt;
    // ── 1. Stop background threads ────────────────────────────────────
    fm_stop_monitor(&s->worker);
    aw_stop_workers(&s->worker);

    // ── 2. Drain any in-flight messages (thumb, full, scan, file change) ─
    MSG msg;
    while (PeekMessageW(&msg, s->hwnd, WM_CALBUM_LOAD_COMPLETE, WM_CALBUM_SCAN_COMPLETE, PM_REMOVE))
    {
        if (msg.message == WM_CALBUM_LOAD_COMPLETE)
        {
            union
            {
                LPARAM l;
                LoadResult *p;
            } u = {.l = msg.lParam};
            LoadResult *result = u.p;
            if (result && result->bc1_data)
                il_free_bc1_data(result->bc1_data);
            free(result);
        }
        else if (msg.message == WM_CALBUM_FILE_CHANGE)
        {
            union
            {
                LPARAM l;
                FileChange *p;
            } u = {.l = msg.lParam};
            FileChange *fc = u.p;
            free(fc);
        }
        else if (msg.message == WM_CALBUM_FULL_LOAD_COMPLETE)
        {
            union
            {
                LPARAM l;
                FullLoadResult *p;
            } u = {.l = msg.lParam};
            FullLoadResult *result = u.p;
            if (result)
            {
                if (result->rgba_data)
                    free(result->rgba_data);
                free(result);
            }
        }
        else if (msg.message == WM_CALBUM_SCAN_PROGRESS)
        {
            union
            {
                LPARAM l;
                ScanBatch *p;
            } u = {.l = msg.lParam};
            ScanBatch *batch = u.p;
            free(batch);
        }
    }

    // ── 3. Free old image resources ──────────────────────────────────
    r_free_full_image(r);
    if (s->data.images)
    {
        for (int i = 0; i < s->data.count; i++)
        {
            if (s->data.images[i].texture_slot != -1)
            {
                r_evict_texture(r, &s->data, s->data.images[i].texture_slot);
            }
        }
    }

    // ── 4. Reset arena and image array ───────────────────────────────
    arena_reset(&s->data.arena);
    s->data.count = s->data.capacity = 0;
    s->data.images = NULL;
    s->worker.work_queue.head = s->worker.work_queue.tail = 0;
    arena_reset(&s->data.nav_arena);

    // ── 5. Reset view state ──────────────────────────────────────────
    s->view.view_mode = VIEW_GALLERY;
    s->view.selected_index = -1;
    s->view.scroll_target_y = s->view.scroll_current_y = 0.0F;
    s->data.full_load_timer = 0.0;
    s->data.full_load_pending = 0;
    s->needs_redraw = 1;

    // ── 6. Set directory and start async scan thread ────────────────
    wcsncpy(s->data.current_dir, path, MAX_PATH_LEN - 1);
    s->data.current_dir[MAX_PATH_LEN - 1] = L'\0';
    wcsncpy(s->data.viewing_dir, path, MAX_PATH_LEN - 1);
    s->data.viewing_dir[MAX_PATH_LEN - 1] = L'\0';

    s->worker.scanning = 1;
    s->worker.scan_progress = 0;
    ScanParam *sp = (ScanParam *) malloc(sizeof(ScanParam));
    if (sp)
    {
        wcsncpy(sp->directory, path, MAX_PATH_LEN - 1);
        sp->directory[MAX_PATH_LEN - 1] = L'\0';
        sp->hwnd = s->hwnd;
        s->worker.scan_thread = CreateThread(NULL, 0, fs_scan_thread, sp, 0, NULL);
        if (!s->worker.scan_thread)
        {
            s->worker.scanning = 0;
            free(sp);
        }
    }

    // gal_update_layout is called by the caller after load
}

void app_update_title(const wchar_t *viewing_dir, HWND hwnd)
{
    wchar_t title[MAX_PATH_LEN + 64];
    wchar_t *leaf = wcsrchr(viewing_dir, L'\\');
    const wchar_t *display_name = leaf ? leaf + 1 : viewing_dir;
    swprintf(title, sizeof(title) / sizeof(wchar_t), L"calbum " APP_VERSION_W L" \u2014 %s", display_name);
    SetWindowTextW(hwnd, title);
}

ImageEntry *app_append_image_entry(DataState *data, const wchar_t *path, const wchar_t *filename, uint64_t file_size,
                                   uint64_t last_modified, uint64_t created_time)
{
    if (data->count >= data->capacity)
    {
        int new_cap = data->capacity ? data->capacity * 2 : 256;
        size_t sz = new_cap * sizeof(ImageEntry);
        size_t align = 16;
        size_t mask = align - 1;
        size_t off = (data->arena.offset + mask) & ~mask;
        if (off + sz > data->arena.capacity)
            return NULL;

        ImageEntry *old_images = data->images;
        data->images = (ImageEntry *) (data->arena.buf + off);
        data->arena.offset = off + sz;
        // Copy surviving entries into the new block so nothing is lost
        if (old_images && data->count > 0)
            memcpy(data->images, old_images, data->count * sizeof(ImageEntry));
        data->capacity = new_cap;
    }

    size_t path_sz = (wcslen(path) + 1) * sizeof(wchar_t);
    size_t name_sz = (wcslen(filename) + 1) * sizeof(wchar_t);
    wchar_t *p_path = (wchar_t *) arena_alloc(&data->arena, path_sz);
    wchar_t *p_name = (wchar_t *) arena_alloc(&data->arena, name_sz);
    if (!p_path || !p_name)
        return NULL;

    wcscpy(p_path, path);
    wcscpy(p_name, filename);

    ImageEntry *e = &data->images[data->count];
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
    data->count++;
    return e;
}

void app_get_parent_dir(const wchar_t *path, wchar_t *out, int max_len)
{
    wcsncpy(out, path, max_len - 1)[max_len - 1] = L'\0';
    size_t len = wcslen(out);
    if (len == 0)
        return;
    if (out[len - 1] == L'\\')
    {
        out[len - 1] = L'\0';
        len--;
    }
    wchar_t *last_slash = wcsrchr(out, L'\\');
    if (last_slash)
    {
        *last_slash = L'\0';
    }
    else
    {
        out[0] = L'\0';
    }
}

static int cmp_grid_item_folder(const void *a, const void *b)
{
    GridItem *ga = (GridItem *) a;
    GridItem *gb = (GridItem *) b;
    return _wcsicmp(ga->folder_name, gb->folder_name);
}

static int cmp_raw_folders(const void *a, const void *b)
{
    return _wcsicmp(*(const wchar_t **) a, *(const wchar_t **) b);
}

static void app_calculate_folder_counts(DataState *data, const wchar_t *folder_path, int *out_images, int *out_folders)
{
    int img_cnt = 0;
    size_t folder_len = wcslen(folder_path);
    int has_trailing_slash = (folder_len > 0 && folder_path[folder_len - 1] == L'\\');

    // Collect direct subfolder names under folder_path using data->images
    const wchar_t **sub_folders = arena_alloc_array(&data->nav_arena, const wchar_t *, data->count);
    int sub_folder_count = 0;

    for (int i = 0; i < data->count; i++)
    {
        const wchar_t *img_path = data->images[i].path;
        if (_wcsnicmp(img_path, folder_path, folder_len) == 0)
        {
            const wchar_t *rel_path = img_path + folder_len;
            if (!has_trailing_slash && *rel_path == L'\\')
            {
                rel_path++;
            }
            else if (!has_trailing_slash && *rel_path != L'\0')
            {
                continue; // False prefix match
            }

            if (*rel_path == L'\0')
            {
                continue; // Exact folder match (unlikely for image file paths)
            }

            // Increment recursive image count
            img_cnt++;

            // Check if there is a subfolder in the relative path
            const wchar_t *slash = wcschr(rel_path, L'\\');
            if (slash != NULL)
            {
                size_t sub_name_len = slash - rel_path;
                if (sub_name_len > 0 && sub_name_len < MAX_PATH_LEN)
                {
                    int duplicate = 0;
                    for (int j = 0; j < sub_folder_count; j++)
                    {
                        if (_wcsnicmp(sub_folders[j], rel_path, sub_name_len) == 0 &&
                            sub_folders[j][sub_name_len] == L'\0')
                        {
                            duplicate = 1;
                            break;
                        }
                    }
                    if (!duplicate)
                    {
                        wchar_t *sub_name =
                            (wchar_t *) arena_alloc(&data->nav_arena, (sub_name_len + 1) * sizeof(wchar_t));
                        if (sub_name)
                        {
                            wcsncpy(sub_name, rel_path, sub_name_len);
                            sub_name[sub_name_len] = L'\0';
                            sub_folders[sub_folder_count++] = sub_name;
                        }
                    }
                }
            }
        }
    }

    *out_images = img_cnt;
    *out_folders = sub_folder_count;
}

void app_populate_grid_items(DataState *data)
{
    arena_reset(&data->nav_arena);
    data->grid_item_count = 0;
    data->strip_image_count = 0;

    if (!data->grid_items || data->grid_item_capacity <= 0)
        return;

    // 1. Add parent directory ".." if we are not at the root current_dir
    int is_sub_dir = (_wcsicmp(data->viewing_dir, data->current_dir) != 0);
    if (is_sub_dir)
    {
        wchar_t parent[MAX_PATH_LEN];
        app_get_parent_dir(data->viewing_dir, parent, MAX_PATH_LEN);

        GridItem *item = &data->grid_items[data->grid_item_count++];
        item->type = ITEM_FOLDER;

        size_t name_sz = sizeof(L"..");
        size_t path_sz = (wcslen(parent) + 1) * sizeof(wchar_t);

        wchar_t *p_name = (wchar_t *) arena_alloc(&data->nav_arena, name_sz);
        wchar_t *p_path = (wchar_t *) arena_alloc(&data->nav_arena, path_sz);
        if (p_name && p_path)
        {
            wcscpy(p_name, L"..");
            wcscpy(p_path, parent);
            item->folder_name = p_name;
            item->folder_path = p_path;
            item->image_index = -1;
            item->image_count = 0;
            item->folder_count = 0;
        }
    }

    int start_folder_idx = data->grid_item_count;

    // 2. Scan data->images to build unique direct subfolders list and direct images list.
    // To do O(N log N) subfolder deduplication, we collect subfolder names first, sort them,
    // and extract the unique ones.
    const wchar_t **raw_folders = arena_alloc_array(&data->nav_arena, const wchar_t *, data->count);
    int raw_folder_count = 0;

    // We also allocate a temp array for direct image items in nav_arena.
    GridItem *temp_images = arena_alloc_array(&data->nav_arena, GridItem, data->count);
    int temp_image_count = 0;

    size_t view_len = wcslen(data->viewing_dir);
    int has_trailing_slash = (view_len > 0 && data->viewing_dir[view_len - 1] == L'\\');

    for (int i = 0; i < data->count; i++)
    {
        const wchar_t *img_path = data->images[i].path;
        if (_wcsnicmp(img_path, data->viewing_dir, view_len) == 0)
        {
            const wchar_t *rel_path = img_path + view_len;
            if (!has_trailing_slash && *rel_path == L'\\')
            {
                rel_path++;
            }
            else if (!has_trailing_slash && *rel_path != L'\0')
            {
                continue; // False prefix match
            }

            const wchar_t *slash = wcschr(rel_path, L'\\');
            if (slash == NULL)
            {
                // Direct image in the current directory!
                if (temp_image_count < data->count)
                {
                    GridItem *item = &temp_images[temp_image_count++];
                    item->type = ITEM_IMAGE;
                    item->image_index = i;
                    item->folder_name = NULL;
                    item->folder_path = NULL;
                    item->image_count = 0;
                    item->folder_count = 0;
                }
            }
            else
            {
                // In a subfolder! Find the subfolder name prefix.
                size_t sub_name_len = slash - rel_path;
                if (sub_name_len > 0 && sub_name_len < MAX_PATH_LEN)
                {
                    wchar_t *sub_name = (wchar_t *) arena_alloc(&data->nav_arena, (sub_name_len + 1) * sizeof(wchar_t));
                    if (sub_name)
                    {
                        wcsncpy(sub_name, rel_path, sub_name_len);
                        sub_name[sub_name_len] = L'\0';
                        raw_folders[raw_folder_count++] = sub_name;
                    }
                }
            }
        }
    }

    // 3. Sort subfolder names and deduplicate.
    if (raw_folder_count > 0)
    {
        qsort((void *) raw_folders, raw_folder_count, sizeof(const wchar_t *), cmp_raw_folders);

        for (int i = 0; i < raw_folder_count; i++)
        {
            if (i == 0 || _wcsicmp(raw_folders[i], raw_folders[i - 1]) != 0)
            {
                if (data->grid_item_count < data->grid_item_capacity)
                {
                    GridItem *item = &data->grid_items[data->grid_item_count++];
                    item->type = ITEM_FOLDER;
                    item->folder_name = raw_folders[i];
                    item->image_index = -1;

                    // Reconstruct full folder path
                    wchar_t full_sub_path[MAX_PATH_LEN];
                    wcsncpy(full_sub_path, data->viewing_dir, MAX_PATH_LEN - 1);
                    size_t f_len = wcslen(full_sub_path);
                    if (f_len > 0 && full_sub_path[f_len - 1] != L'\\')
                    {
                        wcscat(full_sub_path, L"\\");
                    }
                    wcsncat(full_sub_path, raw_folders[i], MAX_PATH_LEN - f_len - 2);

                    size_t path_sz = (wcslen(full_sub_path) + 1) * sizeof(wchar_t);
                    wchar_t *p_path = (wchar_t *) arena_alloc(&data->nav_arena, path_sz);
                    if (p_path)
                    {
                        wcscpy(p_path, full_sub_path);
                        item->folder_path = p_path;
                        app_calculate_folder_counts(data, p_path, &item->image_count, &item->folder_count);
                    }
                    else
                    {
                        item->folder_path = NULL;
                        item->image_count = 0;
                        item->folder_count = 0;
                    }
                }
            }
        }
    }

    // 4. Sort subfolders alphabetically
    int folders_to_sort = data->grid_item_count - start_folder_idx;
    if (folders_to_sort > 1)
    {
        qsort(&data->grid_items[start_folder_idx], folders_to_sort, sizeof(GridItem), cmp_grid_item_folder);
    }

    // 5. Append direct images
    for (int i = 0; i < temp_image_count; i++)
    {
        if (data->grid_item_count < data->grid_item_capacity)
        {
            data->grid_items[data->grid_item_count++] = temp_images[i];
        }
    }

    // 6. Rebuild the cached strip_image_grid_indices
    for (int i = 0; i < data->grid_item_count; i++)
    {
        if (data->grid_items[i].type == ITEM_IMAGE)
        {
            data->strip_image_grid_indices[data->strip_image_count++] = i;
        }
    }
}
