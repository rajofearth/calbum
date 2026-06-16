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

// DWM attribute constants (guarded — not all SDK versions define them)
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

// Forward declaration — window_proc defined in events.c
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ─────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────

static void theme_init(AppState *s)
{
    s->ui.theme.accent[0] = ACCENT_R;
    s->ui.theme.accent[1] = ACCENT_G;
    s->ui.theme.accent[2] = ACCENT_B;
    s->ui.theme.accent[3] = ACCENT_A;

    s->ui.theme.bg[0] = BG_R;
    s->ui.theme.bg[1] = BG_G;
    s->ui.theme.bg[2] = BG_B;
    s->ui.theme.bg[3] = BG_A;

    s->ui.theme.panel[0] = PANEL_R;
    s->ui.theme.panel[1] = PANEL_G;
    s->ui.theme.panel[2] = PANEL_B;
    s->ui.theme.panel[3] = PANEL_A;

    s->ui.theme.border[0] = BORDER_R;
    s->ui.theme.border[1] = BORDER_G;
    s->ui.theme.border[2] = BORDER_B;
    s->ui.theme.border[3] = BORDER_A;

    s->ui.theme.text_main[0] = TEXT_MAIN_R;
    s->ui.theme.text_main[1] = TEXT_MAIN_G;
    s->ui.theme.text_main[2] = TEXT_MAIN_B;
    s->ui.theme.text_main[3] = TEXT_MAIN_A;

    s->ui.theme.text_muted[0] = TEXT_MUTED_R;
    s->ui.theme.text_muted[1] = TEXT_MUTED_G;
    s->ui.theme.text_muted[2] = TEXT_MUTED_B;
    s->ui.theme.text_muted[3] = TEXT_MUTED_A;

    s->ui.theme.scrollbar[0] = SCROLLBAR_R;
    s->ui.theme.scrollbar[1] = SCROLLBAR_G;
    s->ui.theme.scrollbar[2] = SCROLLBAR_B;
    s->ui.theme.scrollbar[3] = SCROLLBAR_A;

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
    if (s->delta_time > 0.1)
        s->delta_time = 0.1;
}

// ─────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────

#ifndef CALBUM_TEST_BUILD
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    (void) hPrevInstance;
    (void) lpCmdLine;

    app_init(&g_state);

    WNDCLASSW wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassW(&wc))
        return 1;

    g_state.hwnd =
        CreateWindowExW(WS_EX_ACCEPTFILES, WINDOW_CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                        CW_USEDEFAULT, WINDOW_DEFAULT_W, WINDOW_DEFAULT_H, NULL, NULL, hInstance, NULL);
    if (!g_state.hwnd)
        return 1;

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
    gal_update_layout_scales(&g_state.ui);

    ShowWindow(g_state.hwnd, nShowCmd);
    UpdateWindow(g_state.hwnd);

    il_init_wic();
    if (!r_init(&g_state.gpu, &g_state.txt, g_state.hwnd))
    {
        MessageBoxW(NULL,
                    L"Failed to initialize the D3D11 rendering engine. Please check your graphics driver or system "
                    L"compatibility.",
                    L"calbum — Rendering Init Failure", MB_OK | MB_ICONERROR);
        il_shutdown_wic();
        app_shutdown(&g_state, &g_state.gpu, &g_state.txt);
        return 1;
    }
    r_resize(&g_state.gpu, &g_state.txt, g_state.window_width, g_state.window_height, g_state.ui.dpi_scale);

    theme_init(&g_state);

    aw_start_workers(&g_state.worker);

    wchar_t pictures_path[MAX_PATH_LEN];
    app_get_pictures_folder(pictures_path, MAX_PATH_LEN);
    app_load_folder(&g_state, &g_state.gpu, &g_state.txt, pictures_path);
    InvalidateRect(g_state.hwnd, NULL, TRUE);

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

        gal_tick_smooth_scroll(&g_state.view, g_state.window_height, g_state.delta_time, &g_state.needs_redraw);
        gal_update_layout(&g_state.data, &g_state.view, &g_state.ui, g_state.window_width, g_state.window_height);

        if (g_state.view.view_mode == VIEW_FULLIMAGE && g_state.view.zoom_ui_timer > 0.0F)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(g_state.hwnd, &pt);
            float cx = (float) g_state.window_width / 2.0F;
            float bx = cx - (ZOOM_BADGE_W * g_state.ui.dpi_scale / 2.0F);
            float by = ZOOM_BADGE_Y * g_state.ui.dpi_scale;
            float bw = ZOOM_BADGE_W * g_state.ui.dpi_scale;
            float bh = ZOOM_BADGE_H * g_state.ui.dpi_scale;
            int hovered =
                ((float) pt.x >= bx && (float) pt.x <= bx + bw && (float) pt.y >= by && (float) pt.y <= by + bh);
            if (hovered)
                g_state.view.zoom_ui_timer = ZOOM_BADGE_TIMER;
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
            UpdateWindow(g_state.hwnd);
        }
        else
        {
            WaitMessage();
            was_idle = 1;
        }
    }
exit_loop:

    aw_stop_workers(&g_state.worker);
    fm_stop_monitor(&g_state.worker);

    r_shutdown(&g_state.gpu, &g_state.txt);
    il_shutdown_wic();
    app_shutdown(&g_state, &g_state.gpu, &g_state.txt);
    return (int) msg.wParam;
}
#endif /* CALBUM_TEST_BUILD */
