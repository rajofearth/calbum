// =========================================================================
// main.c — Entry point, window procedure, and message loop
//
// Responsibilities:
//   1. Register and create the application window
//   2. Handle all window messages (input, paint, custom notifications)
//   3. Run the high-precision message loop with delta time tracking
//   4. Orchestrate app lifecycle (init, load folder, shutdown)
// =========================================================================
#include "types.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <stdlib.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_V2
#define DWMWA_USE_IMMERSIVE_DARK_MODE_V2 19
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

// Global state — single instance for the entire application
static AppState g_state;

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

static int image_select_offset(AppState *s, int delta)
{
    if (s->view.view_mode == VIEW_FULLIMAGE)
    {
        if (s->data.grid_items && s->data.strip_image_count > 0)
        {
            int active_idx = -1;
            for (int i = 0; i < s->data.strip_image_count; i++)
            {
                if (s->data.strip_image_grid_indices[i] == s->view.selected_index)
                {
                    active_idx = i;
                    break;
                }
            }
            if (active_idx != -1)
            {
                int new_active_idx = active_idx + delta;
                if (new_active_idx >= 0 && new_active_idx < s->data.strip_image_count)
                {
                    gal_select_full_image(s, s->data.strip_image_grid_indices[new_active_idx]);
                    return 1;
                }
            }
            return 0;
        }

        // Fallback for tests
        int new_idx = s->view.selected_index + delta;
        if (new_idx >= 0 && new_idx < s->data.count)
        {
            gal_select_full_image(s, new_idx);
            return 1;
        }
        return 0;
    }

    int limit = s->data.grid_items ? s->data.grid_item_count : s->data.count;
    int new_idx = s->view.selected_index + delta;
    if (new_idx >= 0 && new_idx < limit)
    {
        s->view.selected_index = new_idx;
        s->needs_redraw = 1;
        return 1;
    }
    return 0;
}

static void theme_init(AppState *s)
{
    // Deep Charcoal / Obsidian base theme with Amber/Warm Orange accent
    s->ui.theme.accent[0] = 0.961F; // #f59e0b
    s->ui.theme.accent[1] = 0.620F;
    s->ui.theme.accent[2] = 0.043F;
    s->ui.theme.accent[3] = 1.0F;

    s->ui.theme.bg[0] = 0.039F; // #0a0b0d (Obsidian background)
    s->ui.theme.bg[1] = 0.043F;
    s->ui.theme.bg[2] = 0.051F;
    s->ui.theme.bg[3] = 1.0F;

    s->ui.theme.panel[0] = 0.078F; // #14161b (Dark panel background)
    s->ui.theme.panel[1] = 0.086F;
    s->ui.theme.panel[2] = 0.106F;
    s->ui.theme.panel[3] = 1.0F;

    s->ui.theme.border[0] = 0.133F; // #22252c (Dark border variant)
    s->ui.theme.border[1] = 0.145F;
    s->ui.theme.border[2] = 0.173F;
    s->ui.theme.border[3] = 1.0F;

    s->ui.theme.text_main[0] = 0.863F; // #dce0e5
    s->ui.theme.text_main[1] = 0.878F;
    s->ui.theme.text_main[2] = 0.898F;
    s->ui.theme.text_main[3] = 1.0F;

    s->ui.theme.text_muted[0] = 0.663F; // #a9afbc
    s->ui.theme.text_muted[1] = 0.686F;
    s->ui.theme.text_muted[2] = 0.737F;
    s->ui.theme.text_muted[3] = 1.0F;

    s->ui.theme.scrollbar[0] = 0.784F; // #c8ccd44c
    s->ui.theme.scrollbar[1] = 0.800F;
    s->ui.theme.scrollbar[2] = 0.831F;
    s->ui.theme.scrollbar[3] = 0.30F;

    if (s->gpu.d3d_context && s->gpu.theme_buffer)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(s->gpu.d3d_context->lpVtbl->Map(s->gpu.d3d_context, (ID3D11Resource *) s->gpu.theme_buffer, 0,
                                                      D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &s->ui.theme, sizeof(Theme));
            s->gpu.d3d_context->lpVtbl->Unmap(s->gpu.d3d_context, (ID3D11Resource *) s->gpu.theme_buffer, 0);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Frame timing
// ─────────────────────────────────────────────────────────────────────────
static void tick_delta_time(AppState *s)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int64_t ticks = now.QuadPart - s->last_tick;
    s->delta_time = (double) ticks / (double) s->perf_counter_freq;
    s->last_tick = now.QuadPart;
    // Clamp delta to avoid spiral of death on pause/resume
    if (s->delta_time > 0.1)
        s->delta_time = 0.1;
}

// ─────────────────────────────────────────────────────────────────────────
// Custom message handlers (defined by asset_worker.c / file_monitor.c)
// ─────────────────────────────────────────────────────────────────────────

static void on_thumb_complete(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void) wParam;
    union
    {
        LPARAM l;
        LoadResult *p;
    } u = {.l = lParam};
    LoadResult *result = u.p;
    if (result)
    {
        if (result->succeeded && result->bc1_data)
        {
            int found_idx = -1;
            for (int i = 0; i < g_state.data.count; i++)
            {
                if (_wcsicmp(g_state.data.images[i].path, result->path) == 0)
                {
                    found_idx = i;
                    break;
                }
            }
            if (found_idx != -1)
            {
                ImageEntry *e = &g_state.data.images[found_idx];
                int slot = r_alloc_texture_slot(&g_state, found_idx);
                if (slot != -1)
                {
                    r_upload_texture(&g_state, slot, result->bc1_data);
                    e->texture_slot = (int16_t) slot;
                    e->state = IMG_STATE_RESIDENT_GPU;
                }
            }
        }
        il_free_bc1_data(result->bc1_data);
        g_state.needs_redraw = 1;
        free(result);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Full-image async load completion (Section 2.1)
// ─────────────────────────────────────────────────────────────────────────
static void on_full_load_complete(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void) wParam;
    union
    {
        LPARAM l;
        FullLoadResult *p;
    } u = {.l = lParam};
    FullLoadResult *result = u.p;
    if (result && result->succeeded && result->rgba_data)
    {
        // Find which image this is for
        int found_idx = -1;
        for (int i = 0; i < g_state.data.count; i++)
        {
            if (_wcsicmp(g_state.data.images[i].path, result->path) == 0)
            {
                found_idx = i;
                break;
            }
        }
        if (found_idx >= 0)
        {
            int active_img_idx = g_state.data.grid_items ?
                                     g_state.data.grid_items[g_state.view.selected_index].image_index :
                                     g_state.view.selected_index;
            if (found_idx == active_img_idx)
            {
                // Create GPU texture from RGBA data
                int slot_idx = r_alloc_full_image_slot(&g_state);
                if (slot_idx >= 0)
                {
                    FullImageSlot *new_slot = &g_state.gpu.full_slots[slot_idx];
                    D3D11_TEXTURE2D_DESC desc = {0};
                    desc.Width = (UINT) result->w;
                    desc.Height = (UINT) result->h;
                    desc.MipLevels = 1;
                    desc.ArraySize = 1;
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    desc.SampleDesc.Count = 1;
                    desc.Usage = D3D11_USAGE_IMMUTABLE;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA init_data = {0};
                    init_data.pSysMem = result->rgba_data;
                    init_data.SysMemPitch = (UINT) (result->w * 4);
                    HRESULT hr = g_state.gpu.d3d_device->lpVtbl->CreateTexture2D(g_state.gpu.d3d_device, &desc,
                                                                                 &init_data, &new_slot->texture);
                    if (SUCCEEDED(hr))
                    {
                        hr = g_state.gpu.d3d_device->lpVtbl->CreateShaderResourceView(
                            g_state.gpu.d3d_device, (ID3D11Resource *) new_slot->texture, NULL, &new_slot->srv);
                        if (SUCCEEDED(hr))
                        {
                            wcsncpy(new_slot->path, result->path, MAX_PATH_LEN - 1);
                            new_slot->path[MAX_PATH_LEN - 1] = L'\0';
                            new_slot->w = result->w;
                            new_slot->h = result->h;
                            g_state.gpu.active_full_srv = new_slot->srv;
                        }
                    }
                }
            }
            // NOTE: rgba_data points to the global g_decode_buffer (static),
            // which is freed at shutdown. Do NOT free it here.
            result->rgba_data = NULL;
            g_state.data.full_load_pending = 0;
            g_state.needs_redraw = 1;
            InvalidateRect(hwnd, NULL, TRUE);
            free(result);
        }
        else
        {
            g_state.data.full_load_pending = 0;
            g_state.needs_redraw = 1;
            InvalidateRect(hwnd, NULL, TRUE);
            if (result)
            {
                result->rgba_data = NULL;
                free(result);
            }
        }
    }
    else
    {
        g_state.data.full_load_pending = 0;
        g_state.needs_redraw = 1;
        InvalidateRect(hwnd, NULL, TRUE);
        if (result)
        {
            result->rgba_data = NULL;
            free(result);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Async directory scan progress (Section 2.4)
// ─────────────────────────────────────────────────────────────────────────
static void on_scan_progress(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void) wParam;
    union
    {
        LPARAM l;
        ScanBatch *p;
    } u = {.l = lParam};
    ScanBatch *batch = u.p;
    if (!batch)
        return;
    for (int i = 0; i < batch->count; i++)
    {
        ScanEntry *e = &batch->entries[i];
        if (!app_append_image_entry(&g_state, e->path, e->filename, e->file_size, e->last_modified, e->created_time))
            break;
    }
    g_state.worker.scan_progress = g_state.data.count;
    g_state.needs_redraw = 1;
    free(batch);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void on_scan_complete(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void) lParam;
    g_state.worker.scanning = 0;
    g_state.worker.scan_progress = (int) wParam;

    if (g_state.worker.scan_thread)
    {
        WaitForSingleObject(g_state.worker.scan_thread, INFINITE);
        CloseHandle(g_state.worker.scan_thread);
        g_state.worker.scan_thread = NULL;
    }

    // Allocate grid_items and strip images
    g_state.data.grid_item_capacity = (g_state.data.count * 2) + 256;
    g_state.data.grid_items = arena_alloc_array(&g_state.data.arena, GridItem, g_state.data.grid_item_capacity);
    g_state.data.strip_image_grid_indices =
        arena_alloc_array(&g_state.data.arena, int, g_state.data.grid_item_capacity);
    g_state.data.grid_item_count = 0;
    g_state.data.strip_image_count = 0;

    gal_apply_sort(&g_state);
    if (g_state.data.count > 0)
        g_state.view.selected_index = 0;

    app_populate_grid_items(&g_state);
    gal_update_layout(&g_state);
    app_update_title(&g_state);

    // Restart background threads
    fm_start_monitor(&g_state, g_state.data.current_dir);
    aw_start_workers(&g_state);

    g_state.needs_redraw = 1;
    InvalidateRect(hwnd, NULL, TRUE);
}

static void on_file_changed(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void) wParam;
    union
    {
        LPARAM l;
        FileChange *p;
    } u = {.l = lParam};
    FileChange *fc = u.p;
    if (!fc)
        return;
    AppState *s = &g_state;

    if (!fs_has_image_extension(fc->path))
    {
        free(fc);
        return;
    }

    switch (fc->type)
    {
        case CHANGE_ADDED:
        {
            // Append and let the next paint lazy-load the thumbnail
            uint64_t file_size = 0;
            uint64_t last_modified = 0;
            uint64_t created_time = 0;
            WIN32_FILE_ATTRIBUTE_DATA wfad;
            if (GetFileAttributesExW(fc->path, GetFileExInfoStandard, &wfad))
            {
                file_size = ((uint64_t) wfad.nFileSizeHigh << 32) | wfad.nFileSizeLow;
                last_modified =
                    ((uint64_t) wfad.ftLastWriteTime.dwHighDateTime << 32) | wfad.ftLastWriteTime.dwLowDateTime;
                created_time =
                    ((uint64_t) wfad.ftCreationTime.dwHighDateTime << 32) | wfad.ftCreationTime.dwLowDateTime;
            }

            ImageEntry *e = app_append_image_entry(s, fc->path, fc->filename, file_size, last_modified, created_time);
            if (e)
            {
                gal_apply_sort(
                    s); // Note: gal_apply_sort internally calls app_populate_grid_items(s) if grid_items is active
                gal_update_layout(s);
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case CHANGE_REMOVED:
        {
            // Linear scan to find and remove
            for (int i = 0; i < s->data.count; i++)
            {
                if (_wcsicmp(s->data.images[i].path, fc->path) == 0)
                {
                    if (s->data.images[i].texture_slot != -1)
                    {
                        r_evict_texture(s, s->data.images[i].texture_slot);
                    }
                    int remaining = s->data.count - i - 1;
                    if (remaining > 0)
                        memmove(&s->data.images[i], &s->data.images[i + 1], remaining * sizeof(ImageEntry));
                    s->data.count--;
                    if (s->data.grid_items)
                    {
                        app_populate_grid_items(s);
                    }
                    int limit = s->data.grid_items ? s->data.grid_item_count : s->data.count;
                    if (s->view.selected_index >= limit)
                    {
                        s->view.selected_index = limit - 1;
                    }
                    if (limit == 0)
                    {
                        s->view.selected_index = -1;
                    }
                    else if (s->view.selected_index < 0)
                    {
                        s->view.selected_index = 0;
                    }
                    gal_update_layout(s);
                    g_state.needs_redraw = 1;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                }
            }
            break;
        }
        case CHANGE_MODIFIED:
        {
            // Mark thumbnail as stale so it gets reloaded
            for (int i = 0; i < s->data.count; i++)
            {
                if (_wcsicmp(s->data.images[i].path, fc->path) == 0)
                {
                    if (s->data.images[i].texture_slot != -1)
                    {
                        r_evict_texture(s, s->data.images[i].texture_slot);
                    }
                    s->data.images[i].state = IMG_STATE_NEW;
                    s->data.images[i].thumb_requested = 0;
                    g_state.needs_redraw = 1;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                }
            }
            break;
        }
    }
    free(fc);
}

// ─────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (g_state.window_width > 0 && g_state.window_height > 0)
    {
        if (g_state.view.view_mode == VIEW_GALLERY)
        {
            gal_render_gallery(NULL, &g_state);
        }
        else
        {
            gal_render_fullimage(NULL, &g_state);
        }
    }
    EndPaint(hwnd, &ps);
}

static void on_size(HWND hwnd)
{
    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
        return;

    g_state.window_width = width;
    g_state.window_height = height;
    r_resize(&g_state);
    gal_update_layout(&g_state);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void on_keydown(HWND hwnd, int vk)
{
    if (g_state.view.view_mode == VIEW_FULLIMAGE)
    {
        switch (vk)
        {
            case VK_ESCAPE:
            case VK_BACK:
                gal_close_full(&g_state);
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            case VK_LEFT:
            case VK_UP:
                if (image_select_offset(&g_state, -1))
                    InvalidateRect(hwnd, NULL, TRUE);
                break;
            case VK_RIGHT:
            case VK_DOWN:
                if (image_select_offset(&g_state, 1))
                    InvalidateRect(hwnd, NULL, TRUE);
                break;
            case 0xBB: // '+' or '='
            case VK_ADD:
            {
                g_state.view.zoom_level *= 1.1F;
                if (g_state.view.zoom_level > 8.0F)
                    g_state.view.zoom_level = 8.0F;
                gal_clamp_zoom_pan(&g_state);
                g_state.view.zoom_ui_timer = 2.0F;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
            case 0xBD: // '-'
            case VK_SUBTRACT:
            {
                g_state.view.zoom_level /= 1.1F;
                gal_clamp_zoom_pan(&g_state);
                g_state.view.zoom_ui_timer = 2.0F;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
            default:
                break;
        }
    }
    else
    {
        switch (vk)
        {
            case VK_RETURN:
            case VK_SPACE:
                if (g_state.view.selected_index >= 0)
                {
                    gal_activate_item(&g_state, g_state.view.selected_index);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                break;
            case VK_BACK:
            {
                if (_wcsicmp(g_state.data.viewing_dir, g_state.data.current_dir) != 0)
                {
                    wchar_t parent[MAX_PATH_LEN];
                    app_get_parent_dir(g_state.data.viewing_dir, parent, MAX_PATH_LEN);
                    wcsncpy(g_state.data.viewing_dir, parent, MAX_PATH_LEN - 1);
                    g_state.data.viewing_dir[MAX_PATH_LEN - 1] = L'\0';
                    app_populate_grid_items(&g_state);
                    g_state.view.scroll_target_y = g_state.view.scroll_current_y = 0.0F;
                    g_state.view.selected_index = 0;
                    gal_update_layout(&g_state);
                    app_update_title(&g_state);
                    g_state.needs_redraw = 1;
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                break;
            }
            case VK_LEFT:
            case VK_UP:
                if (image_select_offset(&g_state, -1))
                    InvalidateRect(hwnd, NULL, TRUE);
                break;
            case VK_RIGHT:
            case VK_DOWN:
                if (image_select_offset(&g_state, 1))
                    InvalidateRect(hwnd, NULL, TRUE);
                break;
            case VK_HOME:
                g_state.view.selected_index = 0;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            case VK_END:
            {
                int limit = g_state.data.grid_items ? g_state.data.grid_item_count : g_state.data.count;
                g_state.view.selected_index = limit - 1;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
            default:
                break;
        }
    }
}

static void on_lbutton_down(HWND hwnd, int x, int y)
{
    if (g_state.view.view_mode == VIEW_FULLIMAGE)
    {
        if (gal_handle_fullimage_click(&g_state, x, y))
        {
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (g_state.view.zoom_level > 1.0F)
        {
            g_state.view.is_panning = 1;
            g_state.view.pan_start_x = (float) x;
            g_state.view.pan_start_y = (float) y;
            g_state.view.pan_orig_x = g_state.view.zoom_pan_x;
            g_state.view.pan_orig_y = g_state.view.zoom_pan_y;
            SetCapture(hwnd);
        }
        return;
    }

    if (gal_handle_ui_click(&g_state, x, y))
    {
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    // Check scrollbar
    int ms = gal_max_scroll(&g_state);
    if (ms > 0 && (float) x >= (float) g_state.window_width - (16.0F * g_state.ui.dpi_scale))
    {
        g_state.ui.is_dragging_scrollbar = 1;
        g_state.ui.drag_start_y = (float) y;
        g_state.ui.drag_start_scroll_y = g_state.view.scroll_current_y;
        SetCapture(hwnd);
        return;
    }

    int idx;
    if (gal_hit_test(&g_state, x, y, &idx))
    {
        g_state.view.selected_index = idx;
        g_state.needs_redraw = 1;
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_mouse_move(HWND hwnd, int x, int y)
{
    if (g_state.ui.is_dragging_scrollbar)
    {
        (void) x;
        int ms = gal_max_scroll(&g_state);
        if (ms > 0)
        {
            float track_h = (float) g_state.window_height - (16.0F * g_state.ui.dpi_scale);
            float thumb_h = ((float) g_state.window_height / (float) (ms + g_state.window_height)) * track_h;
            if (thumb_h < 24.0F * g_state.ui.dpi_scale)
                thumb_h = 24.0F * g_state.ui.dpi_scale;

            float dy = (float) y - g_state.ui.drag_start_y;
            float scrollable_track = track_h - thumb_h;

            if (scrollable_track > 0.0F)
            {
                float scroll_delta = dy * (float) ms / scrollable_track;
                g_state.view.scroll_current_y = g_state.ui.drag_start_scroll_y + scroll_delta;

                if (g_state.view.scroll_current_y < 0.0F)
                    g_state.view.scroll_current_y = 0.0F;
                if (g_state.view.scroll_current_y > (float) ms)
                    g_state.view.scroll_current_y = (float) ms;
                g_state.view.scroll_target_y = g_state.view.scroll_current_y;

                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
    }
    else if (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.view.is_panning)
    {
        float dx = (float) x - g_state.view.pan_start_x;
        float dy = (float) y - g_state.view.pan_start_y;
        g_state.view.zoom_pan_x = g_state.view.pan_orig_x + dx;
        g_state.view.zoom_pan_y = g_state.view.pan_orig_y + dy;

        gal_clamp_zoom_pan(&g_state);

        g_state.needs_redraw = 1;
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_lbutton_up(HWND hwnd, int x, int y)
{
    (void) hwnd;
    (void) x;
    (void) y;
    if (g_state.ui.is_dragging_scrollbar)
    {
        g_state.ui.is_dragging_scrollbar = 0;
        ReleaseCapture();
    }
    if (g_state.view.is_panning)
    {
        g_state.view.is_panning = 0;
        ReleaseCapture();
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_lbutton_dblclk(HWND hwnd, int x, int y)
{
    if (g_state.view.view_mode != VIEW_GALLERY)
        return;
    int idx;
    if (gal_hit_test(&g_state, x, y, &idx))
    {
        gal_activate_item(&g_state, idx);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_mousewheel(HWND hwnd, int delta)
{
    if (g_state.view.view_mode == VIEW_GALLERY)
    {
        int snap = delta / 120 * 60;
        gal_scroll(&g_state, (float) snap);
        InvalidateRect(hwnd, NULL, TRUE);
    }
    else
    {
        float factor = (delta > 0) ? 1.1F : 0.9F;
        g_state.view.zoom_level *= factor;
        gal_clamp_zoom_pan(&g_state);
        g_state.view.zoom_ui_timer = 2.0F;
        g_state.needs_redraw = 1;
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_drop_files(HWND hwnd, HDROP hDrop)
{
    wchar_t path[MAX_PATH_LEN];
    DragQueryFileW(hDrop, 0, path, MAX_PATH_LEN);
    DragFinish(hDrop);
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        app_load_folder(&g_state, path);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
            on_paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            on_size(hwnd);
            return 0;
        case WM_KEYDOWN:
            on_keydown(hwnd, (int) wParam);
            return 0;
        case WM_LBUTTONDOWN:
            on_lbutton_down(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDBLCLK:
            on_lbutton_dblclk(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            on_mouse_move(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            on_lbutton_up(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL:
            on_mousewheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_DROPFILES:
        {
            union
            {
                WPARAM w;
                HDROP p;
            } ud = {.w = wParam};
            on_drop_files(hwnd, ud.p);
            return 0;
        }
        case WM_CALBUM_LOAD_COMPLETE:
            on_thumb_complete(hwnd, wParam, lParam);
            return 0;
        case WM_CALBUM_FILE_CHANGE:
            on_file_changed(hwnd, wParam, lParam);
            return 0;
        case WM_CALBUM_FULL_LOAD_COMPLETE:
            on_full_load_complete(hwnd, wParam, lParam);
            return 0;
        case WM_CALBUM_SCAN_PROGRESS:
            on_scan_progress(hwnd, wParam, lParam);
            return 0;
        case WM_CALBUM_SCAN_COMPLETE:
            on_scan_complete(hwnd, wParam, lParam);
            return 0;

        case WM_DWMCOLORIZATIONCOLORCHANGED:
            theme_init(&g_state);
            g_state.needs_redraw = 1;
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_DPICHANGED:
        {
            g_state.ui.dpi_scale = HIWORD(wParam) / 96.0F;
            gal_update_layout_scales(&g_state);
            gal_update_layout(&g_state);
            union
            {
                LPARAM l;
                RECT *p;
            } ur = {.l = lParam};
            RECT *r = ur.p;
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_GETMINMAXINFO:
        {
            union
            {
                LPARAM l;
                MINMAXINFO *p;
            } um = {.l = lParam};
            MINMAXINFO *mmi = um.p;
            mmi->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
            mmi->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, // NOLINT(readability-non-const-parameter)
                   int nShowCmd)
{
    (void) hPrevInstance;
    (void) lpCmdLine;

    app_init(&g_state);

    // Register window class
    const wchar_t CLASS_NAME[] = L"calbumWindow";
    WNDCLASSW wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassW(&wc))
        return 1;

    // Create window
    g_state.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, CLASS_NAME, L"calbum", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                   CW_USEDEFAULT, 1200, 800, NULL, NULL, hInstance, NULL);
    if (!g_state.hwnd)
        return 1;

    // Apply immersive dark mode to title bar and Mica backdrop
    BOOL dark_mode = TRUE;
    if (FAILED(DwmSetWindowAttribute(g_state.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode))))
    {
        DwmSetWindowAttribute(g_state.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_V2, &dark_mode, sizeof(dark_mode));
    }
    int backdrop_type = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(g_state.hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type, sizeof(backdrop_type));

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_state.hwnd, &margins);

    UINT dpi = GetDpiForWindow(g_state.hwnd);
    g_state.ui.dpi_scale = (float) dpi / 96.0F;
    gal_update_layout_scales(&g_state);

    ShowWindow(g_state.hwnd, nShowCmd);
    UpdateWindow(g_state.hwnd);

    // Init WIC and D3D11
    il_init_wic();
    if (!r_init(&g_state))
    {
        MessageBoxW(NULL,
                    L"Failed to initialize the D3D11 rendering engine. Please check your graphics driver or system "
                    L"compatibility.",
                    L"calbum — Rendering Init Failure", MB_OK | MB_ICONERROR);
        il_shutdown_wic();
        app_shutdown(&g_state);
        return 1;
    }
    theme_init(&g_state);

    // Start worker threads
    aw_start_workers(&g_state);

    // Load default folder
    wchar_t pictures_path[MAX_PATH_LEN];
    app_get_pictures_folder(pictures_path, MAX_PATH_LEN);
    app_load_folder(&g_state, pictures_path);
    InvalidateRect(g_state.hwnd, NULL, TRUE);

    // Message loop with delta time
    MSG msg = {0};
    int was_idle = 0;

    for (;;)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto exit_loop;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Waking up from idle: reset the tick to avoid a massive frame delta jump
        if (was_idle)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            g_state.last_tick = now.QuadPart;
            was_idle = 0;
        }

        tick_delta_time(&g_state);

        if (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.data.full_load_timer > 0.0)
        {
            g_state.data.full_load_timer -= g_state.delta_time;
            if (g_state.data.full_load_timer <= 0.0)
            {
                g_state.data.full_load_timer = 0.0;
                g_state.needs_redraw = 1;
            }
        }

        gal_tick_smooth_scroll(&g_state);

        if (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.view.zoom_ui_timer > 0.0F)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(g_state.hwnd, &pt);
            float cx = (float) g_state.window_width / 2.0F;
            float bx = cx - (60.0F * g_state.ui.dpi_scale);
            float by = 20.0F * g_state.ui.dpi_scale;
            float bw = 120.0F * g_state.ui.dpi_scale;
            float bh = 30.0F * g_state.ui.dpi_scale;
            int hovered =
                ((float) pt.x >= bx && (float) pt.x <= bx + bw && (float) pt.y >= by && (float) pt.y <= by + bh);
            if (hovered)
            {
                g_state.view.zoom_ui_timer = 2.0F;
            }
            else
            {
                g_state.view.zoom_ui_timer -= (float) g_state.delta_time;
                if (g_state.view.zoom_ui_timer < 0.0F)
                    g_state.view.zoom_ui_timer = 0.0F;
            }
            g_state.needs_redraw = 1;
        }

        if (g_state.needs_redraw || (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.data.full_load_timer > 0.0) ||
            (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.view.zoom_ui_timer > 0.0F))
        {
            InvalidateRect(g_state.hwnd, NULL, TRUE);
            UpdateWindow(g_state.hwnd); // Synchronous paint ensures drawing matches physics
        }
        else
        {
            // No messages and no drawing needed -> Yield CPU
            WaitMessage();
            was_idle = 1;
        }
    }
exit_loop:

    // Stop background threads first — they may still be using WIC / D3D resources
    aw_stop_workers(&g_state);
    fm_stop_monitor(&g_state);

    // Now safe to tear down subsystems
    r_shutdown(&g_state);
    il_shutdown_wic();
    app_shutdown(&g_state);
    return (int) msg.wParam;
}
