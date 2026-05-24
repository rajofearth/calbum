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
#include <shellapi.h>
#include <shlobj.h>

// Global state — single instance for the entire application
static AppState g_state;

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

static void image_cleanup_full(ImageEntry *e)
{
    if (e && e->full_image) {
        DeleteObject(e->full_image);
        e->full_image = NULL;
        e->loaded_full = 0;
    }
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
// Window procedure
// ─────────────────────────────────────────────────────────────────────────

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    g_state.window_width  = client.right - client.left;
    g_state.window_height = client.bottom - client.top;

    if (g_state.window_width > 0 && g_state.window_height > 0) {
        gal_tick_smooth_scroll(&g_state);
        if (g_state.view_mode == VIEW_GALLERY)
            gal_render_gallery(hdc, &g_state);
        else
            gal_render_fullimage(hdc, &g_state);
    }
    EndPaint(hwnd, &ps);
}

static void on_size(HWND hwnd)
{
    RECT client;
    GetClientRect(hwnd, &client);
    g_state.window_width  = client.right - client.left;
    g_state.window_height = client.bottom - client.top;
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
    int idx;
    if (gal_hit_test(&g_state, x, y, &idx)) {
        g_state.selected_index = idx;
        g_state.needs_redraw = 1;
        InvalidateRect(hwnd,NULL,TRUE);
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
    case WM_MOUSEWHEEL:      on_mousewheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
    case WM_DROPFILES:       on_drop_files(hwnd, (HDROP)wParam); return 0;

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

    ShowWindow(g_state.hwnd, nCmdShow);
    UpdateWindow(g_state.hwnd);

    // Start worker threads
    aw_start_workers(&g_state);

    // Load default folder
    wchar_t pictures_path[MAX_PATH_LEN];
    get_pictures_folder(pictures_path, MAX_PATH_LEN);
    app_load_folder(&g_state, pictures_path);
    InvalidateRect(g_state.hwnd, NULL, TRUE);

    // Message loop with delta time
    MSG msg = {0};
    for (;;) {
        BOOL has = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        if (has) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        tick_delta_time(&g_state);

        // If needed, yield CPU briefly to avoid 100% usage on idle
        if (!has && !g_state.needs_redraw) {
            WaitMessage();
        } else if (g_state.needs_redraw) {
            InvalidateRect(g_state.hwnd, NULL, TRUE);
        }
    }

    app_shutdown(&g_state);
    return (int)msg.wParam;
}
