// =========================================================================
// gallery.c — Gallery grid and full-image viewer
//
// All rendering is done via the renderer abstraction.
// Smooth scrolling uses target/current lerp for inertial feel.
// UI virtualization: only items visible in the viewport are drawn.
// =========================================================================
#include "types.h"

int gal_get_columns(AppState *s)
{
    int avail = s->window_width - 2 * GALLERY_PADDING;
    int cols  = (avail + THUMB_PADDING) / (THUMB_SIZE + THUMB_PADDING);
    return (cols < 1) ? 1 : cols;
}

// Compute the maximum scroll offset in pixels (0 if content fits).
static int gal_max_scroll(AppState *s)
{
    int cols = gal_get_columns(s);
    if (cols == 0 || s->count == 0) return 0;
    int rows = ceil_div(s->count, cols);
    int total = rows * (THUMB_SIZE + THUMB_PADDING) + GALLERY_PADDING;
    int ms = total - s->window_height + GALLERY_PADDING;
    return (ms < 0) ? 0 : ms;
}

void gal_update_layout(AppState *s)
{
    int ms = gal_max_scroll(s);
    if (s->scroll_target_y > (float)ms) s->scroll_target_y = (float)ms;
    if (s->scroll_current_y > (float)ms) s->scroll_current_y = (float)ms;
    s->needs_redraw = 1;
}

void gal_tick_smooth_scroll(AppState *s)
{
    if (s->view_mode != VIEW_GALLERY) return;
    float diff = s->scroll_target_y - s->scroll_current_y;
    if (diff > -0.5f && diff < 0.5f) {
        s->scroll_current_y = s->scroll_target_y;
        return;
    }
    float factor = ease_out_factor(SMOOTH_SCROLL_SPEED, (float)s->delta_time);
    s->scroll_current_y += diff * factor;
    s->needs_redraw = 1;
}

void gal_scroll(AppState *s, float delta)
{
    s->scroll_target_y -= delta;
    int ms = gal_max_scroll(s);
    if (s->scroll_target_y < 0.0f) s->scroll_target_y = 0.0f;
    if (s->scroll_target_y > (float)ms) s->scroll_target_y = (float)ms;
    s->needs_redraw = 1;
}

int gal_hit_test(AppState *s, int x, int y, int *out_index)
{
    if (s->view_mode != VIEW_GALLERY || s->count == 0) return 0;
    int cols = gal_get_columns(s);
    int pad  = THUMB_SIZE + THUMB_PADDING;
    // Iterate only potentially visible items
    int scroll_int = (int)s->scroll_current_y;
    int first_row  = (scroll_int - GALLERY_PADDING) / pad;
    if (first_row < 0) first_row = 0;
    int last_row = (scroll_int + s->window_height - GALLERY_PADDING) / pad + 1;
    int first = first_row * cols;
    int last  = (last_row + 1) * cols;
    if (last > s->count) last = s->count;

    for (int i = first; i < last; i++) {
        int row = i / cols, col = i % cols;
        int ix = GALLERY_PADDING + col * pad;
        int iy = GALLERY_PADDING + row * pad - scroll_int;
        if (x >= ix && x < ix + THUMB_SIZE && y >= iy && y < iy + THUMB_SIZE) {
            *out_index = i; return 1;
        }
    }
    return 0;
}

void gal_open_full(AppState *s, int index)
{
    if (index < 0 || index >= s->count) return;
    s->selected_index = index;
    s->view_mode = VIEW_FULLIMAGE;
    s->needs_redraw = 1;
}

void gal_close_full(AppState *s)
{
    s->view_mode = VIEW_GALLERY;
    s->needs_redraw = 1;
}

void gal_render_gallery(HDC hdc, AppState *s)
{
    int w = s->window_width, h = s->window_height;
    r_clear(hdc, w, h, RGB(30, 30, 30));
    int scroll_int = (int)s->scroll_current_y;

    if (s->count == 0) {
        r_draw_text(hdc, L"No images found", 0, 0, w, h,
                    RGB(160, 160, 160), 24, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    }

    int cols = gal_get_columns(s);
    int pad  = THUMB_SIZE + THUMB_PADDING;

    // Determine visible range (UI virtualization)
    int first_row = (scroll_int - GALLERY_PADDING) / pad;
    if (first_row < 0) first_row = 0;
    int last_row = (scroll_int + h - GALLERY_PADDING) / pad + 1;
    int first_visible = first_row * cols;
    int last_visible = (last_row + 1) * cols;
    if (last_visible > s->count) last_visible = s->count;

    // Lazily load thumbnails for visible items (request workers)
    for (int i = first_visible; i < last_visible; i++) {
        if (!s->images[i].loaded_thumb) {
            s->images[i].thumbnail = il_load_thumbnail(s->images[i].path, THUMB_SIZE);
            s->images[i].loaded_thumb = 1;

            // If worker threads are available, use them instead:
            // aw_request_thumbnail(s, i, THUMB_SIZE, s->hwnd);
        }
    }

    // Double-buffer
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old_bmp = SelectObject(mem_dc, mem_bmp);
    BitBlt(mem_dc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

    // Draw visible thumbnails
    for (int i = 0; i < s->count; i++) {
        int row = i / cols, col = i % cols;
        int x = GALLERY_PADDING + col * pad;
        int y = GALLERY_PADDING + row * pad - scroll_int;
        if (y + THUMB_SIZE < 0 || y > h) continue;

        // Selection highlight
        if (i == s->selected_index)
            r_round_rect(mem_dc, x-3, y-3, THUMB_SIZE+6, THUMB_SIZE+6, 6, RGB(70, 130, 220));

        if (s->images[i].thumbnail) {
            HDC img_dc = CreateCompatibleDC(mem_dc);
            BITMAP bm;
            GetObject(s->images[i].thumbnail, sizeof(bm), &bm);
            int dx = x + (THUMB_SIZE - bm.bmWidth)  / 2;
            int dy = y + (THUMB_SIZE - bm.bmHeight) / 2;
            // Shadow
            r_fill_rect(mem_dc, dx+2, dy+2, bm.bmWidth, bm.bmHeight, RGB(0,0,0));
            SelectObject(img_dc, s->images[i].thumbnail);
            BitBlt(mem_dc, dx, dy, bm.bmWidth, bm.bmHeight, img_dc, 0, 0, SRCCOPY);
            DeleteDC(img_dc);
        } else {
            r_fill_rect(mem_dc, x, y, THUMB_SIZE, THUMB_SIZE, RGB(50, 50, 50));
        }
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    s->needs_redraw = 0;
}

void gal_render_fullimage(HDC hdc, AppState *s)
{
    int w = s->window_width, h = s->window_height;
    r_clear(hdc, w, h, RGB(0, 0, 0));

    if (s->selected_index < 0 || s->selected_index >= s->count) return;
    ImageEntry *e = &s->images[s->selected_index];

    if (!e->loaded_full) {
        e->full_image = il_load_full(e->path, &e->full_width, &e->full_height);
        e->loaded_full = 1;
    }

    if (!e->full_image) {
        r_draw_text(hdc, L"Failed to load image", 0, 0, w, h,
                    RGB(200, 80, 80), 24, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    }

    // Fit to window with aspect ratio, max 1:1 (no upscale)
    float sx = (float)(w - 40) / e->full_width;
    float sy = (float)(h - 80) / e->full_height;
    float scale = (sx < sy) ? sx : sy;
    if (scale > 1.0f) scale = 1.0f;

    int dw = (int)(e->full_width  * scale);
    int dh = (int)(e->full_height * scale);
    int dx = (w - dw) / 2, dy = (h - dh) / 2;

    HDC img_dc = CreateCompatibleDC(hdc);
    SelectObject(img_dc, e->full_image);
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, dx, dy, dw, dh, img_dc, 0, 0, e->full_width, e->full_height, SRCCOPY);
    DeleteDC(img_dc);

    // Info overlay
    wchar_t info[512];
    wsprintfW(info, L"%s  —  %d / %d", e->filename, s->selected_index + 1, s->count);
    r_draw_text(hdc, info, 0, h-50, w, 40, RGB(200,200,200), 16, DT_CENTER|DT_SINGLELINE);
    r_draw_text(hdc, L"\x2190  Left  |  Right  \x2192   |  Esc to return",
                0, 10, w, 30, RGB(120,120,120), 14, DT_CENTER|DT_SINGLELINE);
}
