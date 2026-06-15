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
    arena_init(&s->arena, arena, ARENA_CAPACITY);

    // nav_arena (2MB block)
    void *nav_arena_buf = VirtualAlloc(NULL, (SIZE_T) (2 * 1024 * 1024), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    arena_init(&s->nav_arena, nav_arena_buf, (size_t) (2 * 1024 * 1024));

    // Frame timing
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    s->perf_counter_freq = freq.QuadPart;
    QueryPerformanceCounter(&now);
    s->last_tick = now.QuadPart;

    // Ring buffer fields
    rb_init(&s->work_queue, (void *) s->ring_slots, RING_CAPACITY);

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
    {
        for (int i = 0; i < s->count; i++)
        {
            if (s->images[i].texture_slot != -1)
            {
                r_evict_texture(s, s->images[i].texture_slot);
            }
        }
    }

    if (s->arena.buf)
        VirtualFree(s->arena.buf, 0, MEM_RELEASE);
    if (s->nav_arena.buf)
        VirtualFree(s->nav_arena.buf, 0, MEM_RELEASE);
    rb_destroy(&s->work_queue);
}

void get_pictures_folder(wchar_t *buf, int len)
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

void app_load_folder(AppState *s, const wchar_t *path)
{
    // ── 1. Stop background threads ────────────────────────────────────
    fm_stop_monitor(s);
    aw_stop_workers(s);

    // ── 2. Drain any in-flight LoadResults from the message queue ─────
    MSG msg;
    while (PeekMessageW(&msg, s->hwnd, WM_CALBUM_LOAD_COMPLETE, WM_CALBUM_LOAD_COMPLETE, PM_REMOVE))
    {
        LoadResult *result = (LoadResult *) (uintptr_t) msg.lParam; // NOLINT(performance-no-int-to-ptr)
        if (result && result->bc1_data)
        {
            il_free_bc1_data(result->bc1_data);
        }
        free(result);
    }

    // ── 2b. Drain any in-flight FileChange messages ─────────────────────
    while (PeekMessageW(&msg, s->hwnd, WM_CALBUM_FILE_CHANGE, WM_CALBUM_FILE_CHANGE, PM_REMOVE))
    {
        FileChange *fc = (FileChange *) (uintptr_t) msg.lParam;
        free(fc);
    }

    // ── 3. Free old image resources ──────────────────────────────────
    r_free_full_image(s);
    if (s->images)
    {
        for (int i = 0; i < s->count; i++)
        {
            if (s->images[i].texture_slot != -1)
            {
                r_evict_texture(s, s->images[i].texture_slot);
            }
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
    s->scroll_target_y = s->scroll_current_y = 0.0F;
    s->needs_redraw = 1;

    // ── 6. Scan the new folder ───────────────────────────────────────
    fs_scan_directory(path, s);

    // Allocate s->grid_items and s->strip_image_grid_indices capacity
    s->grid_item_capacity = (s->count * 2) + 256;
    s->grid_items = arena_alloc_array(&s->arena, GridItem, s->grid_item_capacity);
    s->strip_image_grid_indices = arena_alloc_array(&s->arena, int, s->grid_item_capacity);
    s->grid_item_count = 0;
    s->strip_image_count = 0;

    gal_apply_sort(s);
    if (s->count > 0)
        s->selected_index = 0;

    // Initialize viewing directory to current directory
    wcsncpy(s->viewing_dir, s->current_dir, MAX_PATH_LEN - 1);
    s->viewing_dir[MAX_PATH_LEN - 1] = L'\0';
    app_populate_grid_items(s);

    // ── 7. Restart background threads (clean state) ──────────────────
    fm_start_monitor(s, path);
    aw_start_workers(s);

    gal_update_layout(s);
    app_update_title(s);
}

void app_update_title(AppState *s)
{
    wchar_t title[MAX_PATH_LEN + 64];
    wsprintfW(title, L"calbum " APP_VERSION_W L" — %s", s->viewing_dir);
    SetWindowTextW(s->hwnd, title);
}

ImageEntry *app_append_image_entry(AppState *s, const wchar_t *path, const wchar_t *filename, uint64_t file_size,
                                   uint64_t last_modified, uint64_t created_time)
{
    if (s->count >= s->capacity)
    {
        int new_cap = s->capacity ? s->capacity * 2 : 256;
        size_t sz = new_cap * sizeof(ImageEntry);
        size_t align = 16;
        size_t mask = align - 1;
        size_t off = (s->arena.offset + mask) & ~mask;
        if (off + sz > s->arena.capacity)
            return NULL;

        ImageEntry *old_images = s->images;
        s->images = (ImageEntry *) (s->arena.buf + off);
        s->arena.offset = off + sz;
        // Copy surviving entries into the new block so nothing is lost
        if (old_images && s->count > 0)
            memcpy(s->images, old_images, s->count * sizeof(ImageEntry));
        s->capacity = new_cap;
    }

    size_t path_sz = (wcslen(path) + 1) * sizeof(wchar_t);
    size_t name_sz = (wcslen(filename) + 1) * sizeof(wchar_t);
    wchar_t *p_path = (wchar_t *) arena_alloc(&s->arena, path_sz);
    wchar_t *p_name = (wchar_t *) arena_alloc(&s->arena, name_sz);
    if (!p_path || !p_name)
        return NULL;

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

void get_parent_dir(const wchar_t *path, wchar_t *out, int max_len)
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

static void app_calculate_folder_counts(AppState *s, const wchar_t *folder_path, int *out_images, int *out_folders)
{
    int img_cnt = 0;
    size_t folder_len = wcslen(folder_path);
    int has_trailing_slash = (folder_len > 0 && folder_path[folder_len - 1] == L'\\');

    // Collect direct subfolder names under folder_path using s->images
    const wchar_t **sub_folders = arena_alloc_array(&s->nav_arena, const wchar_t *, s->count);
    int sub_folder_count = 0;

    for (int i = 0; i < s->count; i++)
    {
        const wchar_t *img_path = s->images[i].path;
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
                            (wchar_t *) arena_alloc(&s->nav_arena, (sub_name_len + 1) * sizeof(wchar_t));
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

void app_populate_grid_items(AppState *s)
{
    arena_reset(&s->nav_arena);
    s->grid_item_count = 0;
    s->strip_image_count = 0;

    if (!s->grid_items || s->grid_item_capacity <= 0)
        return;

    // 1. Add parent directory ".." if we are not at the root current_dir
    int is_sub_dir = (_wcsicmp(s->viewing_dir, s->current_dir) != 0);
    if (is_sub_dir)
    {
        wchar_t parent[MAX_PATH_LEN];
        get_parent_dir(s->viewing_dir, parent, MAX_PATH_LEN);

        GridItem *item = &s->grid_items[s->grid_item_count++];
        item->type = ITEM_FOLDER;

        size_t name_sz = sizeof(L"..");
        size_t path_sz = (wcslen(parent) + 1) * sizeof(wchar_t);

        wchar_t *p_name = (wchar_t *) arena_alloc(&s->nav_arena, name_sz);
        wchar_t *p_path = (wchar_t *) arena_alloc(&s->nav_arena, path_sz);
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

    int start_folder_idx = s->grid_item_count;

    // 2. Scan s->images to build unique direct subfolders list and direct images list.
    // To do O(N log N) subfolder deduplication, we collect subfolder names first, sort them,
    // and extract the unique ones.
    const wchar_t **raw_folders = arena_alloc_array(&s->nav_arena, const wchar_t *, s->count);
    int raw_folder_count = 0;

    // We also allocate a temp array for direct image items in nav_arena.
    GridItem *temp_images = arena_alloc_array(&s->nav_arena, GridItem, s->count);
    int temp_image_count = 0;

    size_t view_len = wcslen(s->viewing_dir);
    int has_trailing_slash = (view_len > 0 && s->viewing_dir[view_len - 1] == L'\\');

    for (int i = 0; i < s->count; i++)
    {
        const wchar_t *img_path = s->images[i].path;
        if (_wcsnicmp(img_path, s->viewing_dir, view_len) == 0)
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
                if (temp_image_count < s->count)
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
                    wchar_t *sub_name = (wchar_t *) arena_alloc(&s->nav_arena, (sub_name_len + 1) * sizeof(wchar_t));
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
                if (s->grid_item_count < s->grid_item_capacity)
                {
                    GridItem *item = &s->grid_items[s->grid_item_count++];
                    item->type = ITEM_FOLDER;
                    item->folder_name = raw_folders[i];
                    item->image_index = -1;

                    // Reconstruct full folder path
                    wchar_t full_sub_path[MAX_PATH_LEN];
                    wcsncpy(full_sub_path, s->viewing_dir, MAX_PATH_LEN - 1);
                    size_t f_len = wcslen(full_sub_path);
                    if (f_len > 0 && full_sub_path[f_len - 1] != L'\\')
                    {
                        wcscat(full_sub_path, L"\\");
                    }
                    wcsncat(full_sub_path, raw_folders[i], MAX_PATH_LEN - f_len - 2);

                    size_t path_sz = (wcslen(full_sub_path) + 1) * sizeof(wchar_t);
                    wchar_t *p_path = (wchar_t *) arena_alloc(&s->nav_arena, path_sz);
                    if (p_path)
                    {
                        wcscpy(p_path, full_sub_path);
                        item->folder_path = p_path;
                        app_calculate_folder_counts(s, p_path, &item->image_count, &item->folder_count);
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
    int folders_to_sort = s->grid_item_count - start_folder_idx;
    if (folders_to_sort > 1)
    {
        qsort(&s->grid_items[start_folder_idx], folders_to_sort, sizeof(GridItem), cmp_grid_item_folder);
    }

    // 5. Append direct images
    for (int i = 0; i < temp_image_count; i++)
    {
        if (s->grid_item_count < s->grid_item_capacity)
        {
            s->grid_items[s->grid_item_count++] = temp_images[i];
        }
    }

    // 6. Rebuild the cached strip_image_grid_indices
    for (int i = 0; i < s->grid_item_count; i++)
    {
        if (s->grid_items[i].type == ITEM_IMAGE)
        {
            s->strip_image_grid_indices[s->strip_image_count++] = i;
        }
    }
}
