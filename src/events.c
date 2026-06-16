// =========================================================================
// events.c — Window message handlers and window procedure
//
// Responsibilities:
//   1. Handle all window messages (input, paint, custom notifications)
//   2. Dispatch to the appropriate subsystem
// =========================================================================
#include "types.h"
#include <windowsx.h>
#include <dwmapi.h>

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
                    gal_select_full_image(&s->data, &s->view, &s->gpu, &s->worker,
                                          s->data.strip_image_grid_indices[new_active_idx], s->hwnd);
                    return 1;
                }
            }
            return 0;
        }
        int new_idx = s->view.selected_index + delta;
        if (new_idx >= 0 && new_idx < s->data.count)
        {
            gal_select_full_image(&s->data, &s->view, &s->gpu, &s->worker, new_idx, s->hwnd);
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

        // ── Nudge scroll if cell not 100% visible ───────────────────────
        float dpi = s->ui.dpi_scale > 0.0F ? s->ui.dpi_scale : 1.0F;
        float thumb_size = 160.0F * dpi;
        float thumb_padding = s->ui.layout.grid_gap > 0.0F ? s->ui.layout.grid_gap : 8.0F * dpi;
        int pad = (int) (thumb_size + thumb_padding);
        if (pad < 1)
            pad = 1;
        int cols = (int) (((float) s->window_width - s->ui.layout.panel_padding) / (float) pad);
        if (cols < 1)
            cols = 1;

        int row = new_idx / cols;
        float topbar_h = s->ui.layout.topbar_height > 0.0F ? s->ui.layout.topbar_height : 0.0F;
        float gp = s->ui.layout.panel_padding > 0.0F ? s->ui.layout.panel_padding : 16.0F * dpi;
        float sc = s->view.scroll_current_y;

        float cell_screen_top = topbar_h + gp + (float) (row * pad) - sc;
        float cell_screen_bot = cell_screen_top + thumb_size;

        if (cell_screen_top < 0.0F || cell_screen_bot > (float) s->window_height)
        {
            float target_y = s->view.scroll_target_y + (float) (delta * 3 * pad);
            int max_s = gal_max_scroll(&s->data, &s->view, &s->ui, s->window_width, s->window_height);
            if (target_y < 0.0F)
                target_y = 0.0F;
            if ((int) target_y > max_s)
                target_y = (float) max_s;
            s->view.scroll_target_y = target_y;
        }
        return 1;
    }
    return 0;
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
                int slot = r_alloc_texture_slot(&g_state.gpu, &g_state.data, found_idx);
                if (slot != -1)
                {
                    r_upload_texture(&g_state.gpu, slot, result->bc1_data);
                    e->texture_slot = (int16_t) slot;
                    e->state = IMG_STATE_RESIDENT_GPU;
                }
            }
        }
        else
        {
            for (int i = 0; i < g_state.data.count; i++)
            {
                if (_wcsicmp(g_state.data.images[i].path, result->path) == 0)
                {
                    if (g_state.data.images[i].state != IMG_STATE_RESIDENT_GPU)
                    {
                        g_state.data.images[i].state = IMG_STATE_FAILED;
                        g_state.data.images[i].thumb_requested = 0;
                    }
                    break;
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
// Full-image async load completion
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
                int slot_idx =
                    r_alloc_full_image_slot(&g_state.gpu, &g_state.data, &g_state.view, g_state.data.grid_item_count);
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
        }
    }
    // Common cleanup (rgba_data is now a per-request allocation, so free it)
    g_state.data.full_load_pending = 0;
    g_state.needs_redraw = 1;
    InvalidateRect(hwnd, NULL, TRUE);
    if (result)
    {
        if (result->rgba_data)
            free(result->rgba_data);
        free(result);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Async directory scan progress
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
        if (!app_append_image_entry(&g_state.data, e->path, e->filename, e->file_size, e->last_modified,
                                    e->created_time))
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

    gal_apply_sort(&g_state.data, &g_state.view);
    if (g_state.data.count > 0)
        g_state.view.selected_index = 0;

    app_populate_grid_items(&g_state.data);
    app_update_title(g_state.data.viewing_dir, g_state.hwnd);

    // Restart background threads
    fm_start_monitor(&g_state, g_state.data.current_dir);
    aw_start_workers(&g_state.worker);

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

            ImageEntry *e =
                app_append_image_entry(&s->data, fc->path, fc->filename, file_size, last_modified, created_time);
            if (e)
            {
                gal_apply_sort(&s->data, &s->view);
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case CHANGE_REMOVED:
        {
            for (int i = 0; i < s->data.count; i++)
            {
                if (_wcsicmp(s->data.images[i].path, fc->path) == 0)
                {
                    if (s->data.images[i].texture_slot != -1)
                        r_evict_texture(&s->gpu, &s->data, s->data.images[i].texture_slot);
                    int remaining = s->data.count - i - 1;
                    if (remaining > 0)
                        memmove(&s->data.images[i], &s->data.images[i + 1], remaining * sizeof(ImageEntry));
                    s->data.count--;
                    if (s->data.grid_items)
                        app_populate_grid_items(&s->data);
                    int limit = s->data.grid_items ? s->data.grid_item_count : s->data.count;
                    if (s->view.selected_index >= limit)
                        s->view.selected_index = limit - 1;
                    if (limit == 0)
                        s->view.selected_index = -1;
                    else if (s->view.selected_index < 0)
                        s->view.selected_index = 0;
                    g_state.needs_redraw = 1;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                }
            }
            break;
        }
        case CHANGE_MODIFIED:
        {
            for (int i = 0; i < s->data.count; i++)
            {
                if (_wcsicmp(s->data.images[i].path, fc->path) == 0)
                {
                    if (s->data.images[i].texture_slot != -1)
                        r_evict_texture(&s->gpu, &s->data, s->data.images[i].texture_slot);
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
    if (g_state.window_width > 0 && g_state.window_height > 0 && g_state.gpu.d3d_context)
    {
        if (g_state.view.view_mode == VIEW_GALLERY)
            gal_render_gallery(NULL, &g_state.gpu, &g_state.txt, &g_state.data, &g_state.view, &g_state.ui,
                               &g_state.worker, g_state.window_width, g_state.window_height, g_state.hwnd,
                               g_state.delta_time);
        else
            gal_render_fullimage(NULL, &g_state.gpu, &g_state.txt, &g_state.data, &g_state.view, &g_state.ui,
                                 &g_state.worker, g_state.window_width, g_state.window_height, g_state.hwnd);
    }
    g_state.needs_redraw = 0;
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
    if (g_state.gpu.d3d_context)
        r_resize(&g_state.gpu, &g_state.txt, width, height, g_state.ui.dpi_scale);
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
                gal_close_full(&g_state.view, &g_state.gpu, &g_state.needs_redraw);
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
            case 0xBB:
            case VK_ADD:
            {
                g_state.view.zoom_level *= ZOOM_STEP;
                if (g_state.view.zoom_level > ZOOM_MAX)
                    g_state.view.zoom_level = ZOOM_MAX;
                gal_clamp_zoom_pan(&g_state.view, g_state.window_width, g_state.window_height, g_state.ui.dpi_scale,
                                   g_state.ui.layout.topbar_height);
                g_state.view.zoom_ui_timer = ZOOM_BADGE_TIMER;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
            case 0xBD:
            case VK_SUBTRACT:
            {
                g_state.view.zoom_level /= ZOOM_STEP;
                gal_clamp_zoom_pan(&g_state.view, g_state.window_width, g_state.window_height, g_state.ui.dpi_scale,
                                   g_state.ui.layout.topbar_height);
                g_state.view.zoom_ui_timer = ZOOM_BADGE_TIMER;
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
                    gal_activate_item(&g_state.data, &g_state.view, &g_state.ui, &g_state.gpu, &g_state.worker,
                                      g_state.view.selected_index, g_state.hwnd);
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
                    app_populate_grid_items(&g_state.data);
                    g_state.view.scroll_target_y = g_state.view.scroll_current_y = 0.0F;
                    g_state.view.selected_index = 0;
                    app_update_title(g_state.data.viewing_dir, g_state.hwnd);
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
            default:
                break;
        }
    }
}

static void on_lbutton_down(HWND hwnd, int x, int y)
{
    if (g_state.view.view_mode == VIEW_FULLIMAGE)
    {
        if (gal_handle_fullimage_click(&g_state.data, &g_state.view, &g_state.ui, &g_state.gpu, &g_state.worker, x, y,
                                       g_state.window_width, g_state.window_height, &g_state.needs_redraw,
                                       g_state.hwnd))
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

    if (gal_handle_ui_click(&g_state.data, &g_state.view, &g_state.ui, x, y, g_state.window_width,
                            &g_state.needs_redraw))
    {
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    int ms = gal_max_scroll(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height);
    if (ms > 0 && (float) x >= (float) g_state.window_width - (16.0F * g_state.ui.dpi_scale))
    {
        float render_track_y = 8.0F * g_state.ui.dpi_scale;
        float render_track_h = (float) g_state.window_height - (16.0F * g_state.ui.dpi_scale);
        float render_thumb_h = ((float) g_state.window_height / (float) (ms + g_state.window_height)) * render_track_h;
        if (render_thumb_h < 24.0F * g_state.ui.dpi_scale)
            render_thumb_h = 24.0F * g_state.ui.dpi_scale;
        float render_thumb_y =
            render_track_y + ((g_state.view.scroll_current_y / (float) ms) * (render_track_h - render_thumb_h));

        if ((float) y >= render_thumb_y && (float) y <= render_thumb_y + render_thumb_h)
        {
            g_state.ui.is_dragging_scrollbar = 1;
            g_state.ui.drag_start_y = (float) y;
            g_state.ui.drag_start_scroll_y = g_state.view.scroll_current_y;
            SetCapture(hwnd);
            return;
        }

        float scrollable_pixels = render_track_h - render_thumb_h;
        float ratio = (scrollable_pixels > 0.0F) ? ((float) y - render_track_y) / scrollable_pixels : 0.0F;
        if (ratio < 0.0F)
            ratio = 0.0F;
        if (ratio > 1.0F)
            ratio = 1.0F;
        float new_y = ratio * (float) ms;
        g_state.view.scroll_current_y = new_y;
        g_state.view.scroll_target_y = new_y;
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    int idx;
    if (gal_hit_test(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height, x, y,
                     &idx))
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
        int ms = gal_max_scroll(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height);
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

        gal_clamp_zoom_pan(&g_state.view, g_state.window_width, g_state.window_height, g_state.ui.dpi_scale,
                           g_state.ui.layout.topbar_height);

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
    if (gal_hit_test(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height, x, y,
                     &idx))
    {
        gal_activate_item(&g_state.data, &g_state.view, &g_state.ui, &g_state.gpu, &g_state.worker, idx, g_state.hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void on_mousewheel(HWND hwnd, int delta)
{
    if (g_state.view.view_mode == VIEW_GALLERY)
    {
        float scroll_amount = (float) delta * 1.5F;
        int ms = gal_max_scroll(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height);
        gal_scroll(&g_state.view, scroll_amount, &g_state.needs_redraw, ms);
        InvalidateRect(hwnd, NULL, TRUE);
    }
    else
    {
        float factor = (delta > 0) ? ZOOM_STEP : (1.0F / ZOOM_STEP);
        g_state.view.zoom_level *= factor;
        gal_clamp_zoom_pan(&g_state.view, g_state.window_width, g_state.window_height, g_state.ui.dpi_scale,
                           g_state.ui.layout.topbar_height);
        g_state.view.zoom_ui_timer = ZOOM_BADGE_TIMER;
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
        app_load_folder(&g_state, &g_state.gpu, &g_state.txt, path);
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
            gal_update_layout_scales(&g_state.ui);
            gal_update_layout(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height);
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
