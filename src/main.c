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

// Global state — single instance for the entire application
static AppState g_state;

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

static void image_cleanup_full(ImageEntry *e)
{
    (void)e;
    // Removed full image for Phase 1
}

static int image_select_offset(AppState *s, int delta)
{
    int new_idx = s->selected_index + delta;
    if (new_idx < 0 || new_idx >= s->count) return 0;
    if (s->view_mode == VIEW_FULLIMAGE)
        image_cleanup_full(&s->images[s->selected_index]);
    s->selected_index = new_idx;
    s->needs_redraw = 1;
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────
// Frame timing
// ─────────────────────────────────────────────────────────────────────────
static void tick_delta_time(AppState *s)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int64_t ticks = now.QuadPart - s->last_tick;
    s->delta_time  = (double)ticks / s->perf_counter_freq;
    s->last_tick   = now.QuadPart;
    // Clamp delta to avoid spiral of death on pause/resume
    if (s->delta_time > 0.1) s->delta_time = 0.1;
}

// ─────────────────────────────────────────────────────────────────────────
// Custom message handlers (defined by asset_worker.c / file_monitor.c)
// ─────────────────────────────────────────────────────────────────────────

static void on_thumb_complete(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    LoadResult *result = (LoadResult *)lParam;
    if (result) {
        if (result->succeeded && result->bc1_data) {
            int found_idx = -1;
            for (int i = 0; i < g_state.count; i++) {
                if (_wcsicmp(g_state.images[i].path, result->path) == 0) {
                    found_idx = i;
                    break;
                }
            }
            if (found_idx != -1) {
                ImageEntry *e = &g_state.images[found_idx];
                int slot = r_alloc_texture_slot(&g_state, found_idx);
                if (slot != -1) {
                    r_upload_texture(&g_state, slot, result->bc1_data);
                    e->texture_slot = slot;
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

static void on_file_changed(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    FileChange *fc = (FileChange *)lParam;
    if (!fc) return;
    AppState *s = &g_state;

    if (!fs_has_image_extension(fc->path)) { free(fc); return; }

    switch (fc->type) {
    case CHANGE_ADDED: {
        // Append and let the next paint lazy-load the thumbnail
        if (s->count < s->capacity) {
            size_t full_sz = (wcslen(fc->path) + 1) * sizeof(wchar_t);
            size_t name_sz = (wcslen(fc->filename) + 1) * sizeof(wchar_t);
            wchar_t *p_full = (wchar_t *)arena_alloc(&s->arena, full_sz);
            wchar_t *p_name = (wchar_t *)arena_alloc(&s->arena, name_sz);
            if (p_full && p_name) {
                wcscpy(p_full, fc->path);
                wcscpy(p_name, fc->filename);
                ImageEntry *e = &s->images[s->count];
                e->path = p_full;
                e->filename = p_name;
                
                WIN32_FILE_ATTRIBUTE_DATA wfad;
                if (GetFileAttributesExW(p_full, GetFileExInfoStandard, &wfad)) {
                    e->file_size = ((uint64_t)wfad.nFileSizeHigh << 32) | wfad.nFileSizeLow;
                    e->last_modified = ((uint64_t)wfad.ftLastWriteTime.dwHighDateTime << 32) | wfad.ftLastWriteTime.dwLowDateTime;
                    e->created_time = ((uint64_t)wfad.ftCreationTime.dwHighDateTime << 32) | wfad.ftCreationTime.dwLowDateTime;
                } else {
                    e->file_size = 0; e->last_modified = 0; e->created_time = 0;
                }
                
                e->texture_slot = -1;
                e->state = IMG_STATE_NEW;
                e->thumb_requested = 0;
                e->full_width = 0;
                e->full_height = 0;
                s->count++;
                gal_apply_sort(s);
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        break;
    }
    case CHANGE_REMOVED: {
        // Linear scan to find and remove
        for (int i = 0; i < s->count; i++) {
            if (_wcsicmp(s->images[i].path, fc->path) == 0) {
                if (s->images[i].texture_slot != -1) {
                    r_evict_texture(s, s->images[i].texture_slot);
                }
                int remaining = s->count - i - 1;
                if (remaining > 0)
                    memmove(&s->images[i], &s->images[i+1], remaining * sizeof(ImageEntry));
                s->count--;
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
        }
        break;
    }
    case CHANGE_MODIFIED: {
        // Mark thumbnail as stale so it gets reloaded
        for (int i = 0; i < s->count; i++) {
            if (_wcsicmp(s->images[i].path, fc->path) == 0) {
                if (s->images[i].texture_slot != -1) {
                    r_evict_texture(s, s->images[i].texture_slot);
                }
                s->images[i].state = IMG_STATE_NEW;
                s->images[i].thumb_requested = 0;
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
    if (g_state.window_width > 0 && g_state.window_height > 0) {
        if (g_state.view_mode == VIEW_GALLERY)
            gal_render_gallery(NULL, &g_state);
        else
            gal_render_fullimage(NULL, &g_state);
    }
    EndPaint(hwnd, &ps);
}

static void on_size(HWND hwnd)
{
    RECT client;
    GetClientRect(hwnd, &client);
    g_state.window_width  = client.right - client.left;
    g_state.window_height = client.bottom - client.top;
    r_resize(&g_state);
    gal_update_layout(&g_state);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void on_keydown(HWND hwnd, int vk)
{
    if (g_state.view_mode == VIEW_FULLIMAGE) {
        switch (vk) {
        case VK_ESCAPE: gal_close_full(&g_state); InvalidateRect(hwnd,NULL,TRUE); break;
        case VK_LEFT:  case VK_UP:
            if (image_select_offset(&g_state,-1)) InvalidateRect(hwnd,NULL,TRUE);
            break;
        case VK_RIGHT: case VK_DOWN:
            if (image_select_offset(&g_state,1)) InvalidateRect(hwnd,NULL,TRUE);
            break;
        }
    } else {
        switch (vk) {
        case VK_RETURN: case VK_SPACE:
            if (g_state.selected_index >= 0) {
                gal_open_full(&g_state, g_state.selected_index);
                InvalidateRect(hwnd,NULL,TRUE);
            }
            break;
        case VK_LEFT:  case VK_UP:
            if (image_select_offset(&g_state,-1)) InvalidateRect(hwnd,NULL,TRUE);
            break;
        case VK_RIGHT: case VK_DOWN:
            if (image_select_offset(&g_state,1)) InvalidateRect(hwnd,NULL,TRUE);
            break;
        case VK_HOME:
            g_state.selected_index = 0;
            g_state.needs_redraw = 1;
            InvalidateRect(hwnd,NULL,TRUE);
            break;
        case VK_END:
            g_state.selected_index = g_state.count - 1;
            g_state.needs_redraw = 1;
            InvalidateRect(hwnd,NULL,TRUE);
            break;
        }
    }
}

static void on_lbutton_down(HWND hwnd, int x, int y)
{
    if (g_state.view_mode == VIEW_FULLIMAGE) {
        if (image_select_offset(&g_state,1)) InvalidateRect(hwnd,NULL,TRUE);
        return;
    }

    if (gal_handle_ui_click(&g_state, x, y)) {
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    // Check scrollbar
    int ms = gal_max_scroll(&g_state);
    if (ms > 0 && x >= g_state.window_width - 16) {
        g_state.is_dragging_scrollbar = 1;
        g_state.drag_start_y = (float)y;
        g_state.drag_start_scroll_y = g_state.scroll_current_y;
        SetCapture(hwnd);
        return;
    }

    int idx;
    if (gal_hit_test(&g_state, x, y, &idx)) {
        g_state.selected_index = idx;
        g_state.needs_redraw = 1;
        InvalidateRect(hwnd,NULL,TRUE);
    }
}

static void on_mouse_move(HWND hwnd, int x, int y)
{
    (void)x;
    if (g_state.is_dragging_scrollbar) {
        int ms = gal_max_scroll(&g_state);
        if (ms > 0) {
            float track_h = (float)g_state.window_height - 16.0f;
            float thumb_h = ((float)g_state.window_height / (float)(ms + g_state.window_height)) * track_h;
            if (thumb_h < 20.0f) thumb_h = 20.0f;
            
            float dy = (float)y - g_state.drag_start_y;
            float scrollable_track = track_h - thumb_h;
            
            if (scrollable_track > 0.0f) {
                float scroll_delta = dy * (float)ms / scrollable_track;
                g_state.scroll_current_y = g_state.drag_start_scroll_y + scroll_delta;
                
                if (g_state.scroll_current_y < 0.0f) g_state.scroll_current_y = 0.0f;
                if (g_state.scroll_current_y > (float)ms) g_state.scroll_current_y = (float)ms;
                g_state.scroll_target_y = g_state.scroll_current_y;
                
                g_state.needs_redraw = 1;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
    }
}

static void on_lbutton_up(HWND hwnd, int x, int y)
{
    (void)hwnd; (void)x; (void)y;
    if (g_state.is_dragging_scrollbar) {
        g_state.is_dragging_scrollbar = 0;
        ReleaseCapture();
    }
}

static void on_lbutton_dblclk(HWND hwnd, int x, int y)
{
    if (g_state.view_mode != VIEW_GALLERY) return;
    int idx;
    if (gal_hit_test(&g_state, x, y, &idx)) {
        gal_open_full(&g_state, idx);
        InvalidateRect(hwnd,NULL,TRUE);
    }
}

static void on_mousewheel(HWND hwnd, int delta)
{
    if (g_state.view_mode == VIEW_GALLERY) {
        gal_scroll(&g_state, (float)(delta / 120 * 60));
        InvalidateRect(hwnd,NULL,TRUE);
    }
}

static void on_drop_files(HWND hwnd, HDROP hDrop)
{
    wchar_t path[MAX_PATH_LEN];
    DragQueryFileW(hDrop, 0, path, MAX_PATH_LEN);
    DragFinish(hDrop);
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        app_load_folder(&g_state, path);
        InvalidateRect(hwnd,NULL,TRUE);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:           on_paint(hwnd); return 0;
    case WM_ERASEBKGND:      return 1;
    case WM_SIZE:            on_size(hwnd); return 0;
    case WM_KEYDOWN:         on_keydown(hwnd, (int)wParam); return 0;
    case WM_LBUTTONDOWN:     on_lbutton_down(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_LBUTTONDBLCLK:   on_lbutton_dblclk(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSEMOVE:       on_mouse_move(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_LBUTTONUP:       on_lbutton_up(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSEWHEEL:      on_mousewheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
    case WM_DROPFILES:       on_drop_files(hwnd, (HDROP)wParam); return 0;
    case WM_CALBUM_LOAD_COMPLETE: on_thumb_complete(hwnd, wParam, lParam); return 0;
    case WM_CALBUM_FILE_CHANGE:   on_file_changed(hwnd, wParam, lParam); return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
        mmi->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)lpCmdLine;

    app_init(&g_state);

    // Register window class
    const wchar_t CLASS_NAME[] = L"calbumWindow";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassW(&wc)) return 1;

    // Create window
    g_state.hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, CLASS_NAME, L"calbum",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 800, NULL, NULL, hInstance, NULL);
    if (!g_state.hwnd) return 1;

    // Apply immersive dark mode to title bar
    BOOL dark_mode = TRUE;
    if (FAILED(DwmSetWindowAttribute(g_state.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode)))) {
        DwmSetWindowAttribute(g_state.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_V2, &dark_mode, sizeof(dark_mode));
    }

    ShowWindow(g_state.hwnd, nCmdShow);
    UpdateWindow(g_state.hwnd);

    // Init WIC and D3D11
    il_init_wic();
    r_init(&g_state);

    // Start worker threads
    aw_start_workers(&g_state);

    // Load default folder
    wchar_t pictures_path[MAX_PATH_LEN];
    get_pictures_folder(pictures_path, MAX_PATH_LEN);
    app_load_folder(&g_state, pictures_path);
    InvalidateRect(g_state.hwnd, NULL, TRUE);

    // Message loop with delta time
    MSG msg = {0};
    int was_idle = 0;
    
    for (;;) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto exit_loop;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Waking up from idle: reset the tick to avoid a massive frame delta jump
        if (was_idle) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            g_state.last_tick = now.QuadPart;
            was_idle = 0;
        }

        tick_delta_time(&g_state);
        gal_tick_smooth_scroll(&g_state);

        if (g_state.needs_redraw) {
            InvalidateRect(g_state.hwnd, NULL, TRUE);
            UpdateWindow(g_state.hwnd); // Synchronous paint ensures drawing matches physics
        } else {
            // No messages and no drawing needed -> Yield CPU
            WaitMessage();
            was_idle = 1;
        }
    }
exit_loop:

    r_shutdown(&g_state);
    il_shutdown_wic();
    app_shutdown(&g_state);
    return (int)msg.wParam;
}
