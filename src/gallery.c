// =========================================================================
// gallery.c — Gallery grid using D3D11 instanced rendering
// =========================================================================
#include "types.h"
#include "ui.h"
#include <math.h>

void gal_update_layout_scales(AppState *s)
{
    if (s->dpi_scale <= 0.0f) s->dpi_scale = 1.0f;

    s->layout.grid_gap = 16.0f * s->dpi_scale;
    s->layout.panel_padding = 16.0f * s->dpi_scale;
    s->layout.thumb_radius = 8.0f * s->dpi_scale;
    s->layout.card_radius = 12.0f * s->dpi_scale;
    s->layout.button_height = 32.0f * s->dpi_scale;
    s->layout.topbar_height = 50.0f * s->dpi_scale;
    s->layout.scrollbar_w = 6.0f * s->dpi_scale;
}

typedef struct {
    int cols;
    int pad;
    int grid_width;
    int left_margin;
    int scroll_int;
    int first_row;
    int last_row;
    int first_visible;
    int last_visible;
} GridLayout;

static void gal_calc_layout(AppState *s, GridLayout *out)
{
    float dpi = s->dpi_scale > 0.0f ? s->dpi_scale : 1.0f;
    float thumb_size = 160.0f * dpi;
    float thumb_padding = s->layout.grid_gap > 0.0f ? s->layout.grid_gap : 8.0f * dpi;
    float gallery_padding = s->layout.panel_padding > 0.0f ? s->layout.panel_padding : 16.0f * dpi;
    
    out->pad = (int)(thumb_size + thumb_padding);
    if (out->pad < 1) out->pad = 1;
    
    out->cols = (int)((s->window_width - gallery_padding) / out->pad);
    if (out->cols < 1) out->cols = 1;
    
    out->grid_width = (int)(out->cols * thumb_size + (out->cols - 1) * thumb_padding);
    out->left_margin = (s->window_width - out->grid_width) / 2;
    if (out->left_margin < (int)gallery_padding) out->left_margin = (int)gallery_padding;
    
    out->scroll_int = (int)s->scroll_current_y;
    float top_margin_h = s->layout.topbar_height > 0.0f ? s->layout.topbar_height : 0.0f;
    int top_margin = (int)(top_margin_h + gallery_padding);
    out->first_row = (out->scroll_int - top_margin) / out->pad;
    if (out->first_row < 0) out->first_row = 0;
    
    out->last_row = (out->scroll_int + s->window_height - top_margin) / out->pad + 1;
    
    out->first_visible = out->first_row * out->cols;
    out->last_visible = (out->last_row + 1) * out->cols;
    if (out->last_visible > s->count) out->last_visible = s->count;
}

static int gal_max_scroll(AppState *s)
{
    GridLayout lay;
    gal_calc_layout(s, &lay);
    if (lay.cols <= 0) return 0;
    
    int total_rows = ceil_div(s->count, lay.cols);
    float dpi = s->dpi_scale > 0.0f ? s->dpi_scale : 1.0f;
    float gallery_padding = s->layout.panel_padding > 0.0f ? s->layout.panel_padding : 16.0f * dpi;
    float top_margin_h = s->layout.topbar_height > 0.0f ? s->layout.topbar_height : 0.0f;
    int top_margin = (int)(top_margin_h + gallery_padding);
    int content_h = top_margin + total_rows * lay.pad + (int)gallery_padding;
    int max_s = content_h - s->window_height;
    return max_s < 0 ? 0 : max_s;
}

static int cmp_date_created(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->created_time < eb->created_time) return -1;
    if (ea->created_time > eb->created_time) return 1;
    return 0;
}

static int cmp_date_modified(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->last_modified < eb->last_modified) return -1;
    if (ea->last_modified > eb->last_modified) return 1;
    return 0;
}

static int cmp_size(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->file_size < eb->file_size) return -1;
    if (ea->file_size > eb->file_size) return 1;
    return 0;
}

void gal_apply_sort(AppState *s)
{
    if (s->count == 0) return;
    
    // Save currently selected path
    wchar_t selected_path[MAX_PATH_LEN] = {0};
    if (s->selected_index >= 0 && s->selected_index < s->count) {
        wcsncpy(selected_path, s->images[s->selected_index].path, MAX_PATH_LEN-1);
    }
    
    int (*cmp)(const void*, const void*) = cmp_date_created;
    if (s->sort_mode == SORT_DATE_MODIFIED) cmp = cmp_date_modified;
    if (s->sort_mode == SORT_SIZE) cmp = cmp_size;
    
    qsort(s->images, s->count, sizeof(ImageEntry), cmp);
    
    // Reverse if descending
    if (s->sort_descending) {
        for (int i = 0; i < s->count / 2; i++) {
            ImageEntry tmp = s->images[i];
            s->images[i] = s->images[s->count - 1 - i];
            s->images[s->count - 1 - i] = tmp;
        }
    }
    
    // Restore selection
    if (selected_path[0]) {
        for (int i = 0; i < s->count; i++) {
            if (_wcsicmp(s->images[i].path, selected_path) == 0) {
                s->selected_index = i;
                break;
            }
        }
    }
    s->needs_redraw = 1;
}

static void gal_apply_sort_option(AppState *s, int cmd)
{
    if (cmd >= 1 && cmd <= 3) {
        if (cmd == 1) s->sort_mode = SORT_DATE_CREATED;
        if (cmd == 2) s->sort_mode = SORT_DATE_MODIFIED;
        if (cmd == 3) s->sort_mode = SORT_SIZE;
        gal_apply_sort(s);
    } else if (cmd == 4 || cmd == 5) {
        s->sort_descending = (cmd == 5) ? 1 : 0;
        gal_apply_sort(s);
    }
}

int gal_handle_ui_click(AppState *s, int x, int y)
{
    if (s->view_mode != VIEW_GALLERY) return 0;
    
    int btn_w = 80;
    int btn_h = 30;
    int btn_x = s->window_width - btn_w - 20; // Top right
    int btn_y = 10;

    if (s->sort_menu_open) {
        int menu_w = 150;
        int menu_h = 5 * 30;
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + 5;
        
        if (x >= menu_x && x <= menu_x + menu_w && y >= menu_y && y <= menu_y + menu_h) {
            int row = (y - menu_y) / 30;
            gal_apply_sort_option(s, row + 1);
        }
        s->sort_menu_open = 0;
        s->needs_redraw = 1;
        return 1;
    }

    // Sort Button hit test
    if (x >= btn_x && x <= btn_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
        s->sort_menu_open = 1;
        s->needs_redraw = 1;
        return 1;
    }
    
    // Allow clicking thumbnails behind transparent area
    return 0;
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
    if (s->sort_menu_open) return 0; // Don't allow clicking thumbnails when menu is open
    GridLayout lay;
    gal_calc_layout(s, &lay);

    float dpi = s->dpi_scale > 0.0f ? s->dpi_scale : 1.0f;
    float thumb_size = 160.0f * dpi;
    float gallery_padding = s->layout.panel_padding > 0.0f ? s->layout.panel_padding : 16.0f * dpi;
    float top_margin_h = s->layout.topbar_height > 0.0f ? s->layout.topbar_height : 0.0f;

    for (int i = lay.first_visible; i < lay.last_visible; i++) {
        int row = i / lay.cols, col = i % lay.cols;
        int ix = lay.left_margin + col * lay.pad;
        int iy = (int)(top_margin_h + gallery_padding + row * lay.pad - lay.scroll_int);
        if (x >= ix && x < ix + thumb_size && y >= iy && y < iy + thumb_size) {
            *out_index = i; return 1;
        }
    }
    return 0;
}

void gal_select_full_image(AppState *s, int index)
{
    if (index < 0 || index >= s->count) return;

    s->selected_index = index;
    s->zoom_level = 1.0f;
    s->zoom_ui_timer = 0.0f;
    s->zoom_pan_x = 0.0f;
    s->zoom_pan_y = 0.0f;
    s->is_panning = 0;

    if (s->images) {
        ImageEntry *e = &s->images[index];
        if (e->full_width == 0) {
            int w = 0, h = 0;
            if (il_get_image_dimensions(e->path, &w, &h)) {
                e->full_width = (uint16_t)w;
                e->full_height = (uint16_t)h;
            }
        }

        if (e->file_size < 2 * 1024 * 1024) {
            s->full_load_timer = 0.0;
            r_load_full_image(s, e->path);
        } else {
            s->full_load_timer = 0.15; // 150ms debounce for files >= 2MB
        }
    } else {
        s->full_load_timer = 0.15;
    }
    s->needs_redraw = 1;
}

void gal_open_full(AppState *s, int index)
{
    if (index < 0 || index >= s->count) return;
    s->view_mode = VIEW_FULLIMAGE;
    gal_select_full_image(s, index);
}


void gal_render_gallery(HDC hdc, AppState *s)
{
    (void)hdc; // Unused parameter
    r_clear(s, 0.12f, 0.12f, 0.12f);

    if (s->count == 0) {
        r_present(s);
        return;
    }

    GridLayout lay;
    gal_calc_layout(s, &lay);

    static InstanceData instances[4096];
    int inst_count = 0;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(s->hwnd, &pt);

    float thumb_size = 160.0f * s->dpi_scale;

    for (int i = lay.first_visible; i < lay.last_visible; i++) {
        int row = i / lay.cols, col = i % lay.cols;
        float x = (float)(lay.left_margin + col * lay.pad);
        float y = (float)(s->layout.topbar_height + s->layout.panel_padding + row * lay.pad - lay.scroll_int);

        if (y + thumb_size < 0 || y > s->window_height) continue;

        if (s->images[i].texture_slot == -1 && !s->images[i].thumb_requested) {
            s->images[i].thumb_requested = 1;
            aw_request_thumbnail(s, s->images[i].path, THUMB_SIZE, s->hwnd);
        }

        int hovered = ui_is_hovered(x, y, thumb_size, thumb_size, (float)pt.x, (float)pt.y);

        // Selection Border: draw a glowing accent color border slightly larger
        if (s->selected_index == i) {
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = x - 4.0f * s->dpi_scale;
            instances[inst_count].y = y - 4.0f * s->dpi_scale;
            instances[inst_count].w = thumb_size + 8.0f * s->dpi_scale;
            instances[inst_count].h = thumb_size + 8.0f * s->dpi_scale;
            instances[inst_count].tex_index = -7; // Accent color token (-7)
            instances[inst_count].opacity = 1.0f;
            instances[inst_count].corner_radius = s->layout.thumb_radius + 4.0f * s->dpi_scale;
            inst_count++;
        }
        // Hover Border: draw a subtle translucent white border
        else if (hovered) {
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = x - 2.0f * s->dpi_scale;
            instances[inst_count].y = y - 2.0f * s->dpi_scale;
            instances[inst_count].w = thumb_size + 4.0f * s->dpi_scale;
            instances[inst_count].h = thumb_size + 4.0f * s->dpi_scale;
            instances[inst_count].tex_index = -2; // White border
            instances[inst_count].opacity = 0.35f;
            instances[inst_count].corner_radius = s->layout.thumb_radius + 2.0f * s->dpi_scale;
            inst_count++;
        }

        // Draw main thumbnail
        instances[inst_count] = (InstanceData){0};
        instances[inst_count].x = x;
        instances[inst_count].y = y;
        instances[inst_count].w = thumb_size;
        instances[inst_count].h = thumb_size;
        instances[inst_count].tex_index = (s->images[i].state == IMG_STATE_RESIDENT_GPU) ? s->images[i].texture_slot : -1;
        instances[inst_count].opacity = (hovered || s->selected_index == i) ? 1.0f : 0.85f;
        instances[inst_count].corner_radius = s->layout.thumb_radius;
        
        if (s->images[i].state == IMG_STATE_RESIDENT_GPU && s->images[i].texture_slot != -1) {
            s->tex_pool.last_used[s->images[i].texture_slot] = s->tex_pool.frame_counter;
        }

        inst_count++;
        if (inst_count >= 4090) break; // Leave room for scrollbar and buttons
    }

    // Scrollbar Update & Draw
    int ms = gal_max_scroll(s);
    if (ms > 0) {
        if (fabsf(s->scroll_target_y - s->scroll_current_y) > 0.5f) {
            s->scrollbar_fade_timer = 2.0f;
        }

        float hit_x = (float)s->window_width - 16.0f * s->dpi_scale;
        int scrollbar_hovered = ui_is_hovered(hit_x, 0.0f, 16.0f * s->dpi_scale, (float)s->window_height, (float)pt.x, (float)pt.y);
        if (scrollbar_hovered) {
            s->scrollbar_fade_timer = 2.0f;
        }

        if (s->scrollbar_fade_timer > 0.0f) {
            s->scrollbar_fade_timer -= (float)s->delta_time;
        }

        float target_opacity = (s->scrollbar_fade_timer > 0.0f) ? 1.0f : 0.0f;
        s->scrollbar_opacity += (target_opacity - s->scrollbar_opacity) * ease_out_factor(10.0f, (float)s->delta_time);

        float target_hover_t = scrollbar_hovered ? 1.0f : 0.0f;
        s->scrollbar_hover_t += (target_hover_t - s->scrollbar_hover_t) * ease_out_factor(12.0f, (float)s->delta_time);

        if (s->scrollbar_opacity > 0.01f) {
            float track_w = lerpf(6.0f, 10.0f, s->scrollbar_hover_t) * s->dpi_scale;
            float track_x = (float)s->window_width - track_w - 4.0f * s->dpi_scale;
            float track_y = 8.0f * s->dpi_scale;
            float track_h = (float)s->window_height - 16.0f * s->dpi_scale;

            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = track_x;
            instances[inst_count].y = track_y;
            instances[inst_count].w = track_w;
            instances[inst_count].h = track_h;
            instances[inst_count].tex_index = -3;
            instances[inst_count].opacity = 0.15f * s->scrollbar_opacity;
            instances[inst_count].corner_radius = track_w * 0.5f;
            inst_count++;

            float thumb_h = (s->window_height / (float)(ms + s->window_height)) * track_h;
            if (thumb_h < 24.0f * s->dpi_scale) thumb_h = 24.0f * s->dpi_scale;
            float thumb_y = track_y + (s->scroll_current_y / (float)ms) * (track_h - thumb_h);

            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = track_x;
            instances[inst_count].y = thumb_y;
            instances[inst_count].w = track_w;
            instances[inst_count].h = thumb_h;
            instances[inst_count].tex_index = -4;
            instances[inst_count].opacity = s->scrollbar_opacity * (scrollbar_hovered ? 1.0f : 0.7f);
            instances[inst_count].corner_radius = track_w * 0.5f;
            inst_count++;
        }
    }

    // Sort Button & Dropdown Layout
    int btn_w = (int)(90 * s->dpi_scale);
    int btn_h = (int)(s->layout.button_height);
    int btn_x = (int)(s->window_width - btn_w - 20 * s->dpi_scale);
    int btn_y = (int)(10 * s->dpi_scale);

    ui_button(instances, &inst_count, (float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h, 0.8f, (float)pt.x, (float)pt.y, 6.0f * s->dpi_scale);

    if (s->sort_menu_open) {
        int menu_w = (int)(160 * s->dpi_scale);
        int menu_h = 5 * (int)(30 * s->dpi_scale);
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + (int)(5 * s->dpi_scale);
        
        ui_panel(instances, &inst_count, (float)menu_x, (float)menu_y, (float)menu_w, (float)menu_h, 0.92f, 1, s->layout.card_radius);
    }

    r_draw_instances(s, instances, inst_count);

    ui_button_text(s, L"Sort \x25BC", (float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h);

    if (s->sort_menu_open) {
        int menu_w = (int)(160 * s->dpi_scale);
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + (int)(5 * s->dpi_scale);
        
        const wchar_t* opts[] = {
            s->sort_mode == SORT_DATE_CREATED ? L"\x2713 Date created" : L"  Date created",
            s->sort_mode == SORT_DATE_MODIFIED ? L"\x2713 Date modified" : L"  Date modified",
            s->sort_mode == SORT_SIZE ? L"\x2713 Size" : L"  Size",
            !s->sort_descending ? L"\x2713 Ascending" : L"  Ascending",
            s->sort_descending ? L"\x2713 Descending" : L"  Descending"
        };
        
        float option_h = 30.0f * s->dpi_scale;
        for (int i = 0; i < 5; i++) {
            r_draw_text_ext(s, opts[i], (float)(menu_x + 12 * s->dpi_scale), (float)(menu_y + 5 * s->dpi_scale + i * option_h), (float)menu_w, option_h, s->dwrite_format_regular, s->theme.text_main);
        }
    }

    r_present(s);
    s->needs_redraw = 0;
}

static void format_size(uint64_t bytes, wchar_t *buf, int len)
{
    if (bytes >= 1024ULL * 1024 * 1024) {
        swprintf(buf, len, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024) {
        swprintf(buf, len, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        swprintf(buf, len, L"%.2f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buf, len, L"%llu Bytes", bytes);
    }
}

static void format_filetime(uint64_t filetime, wchar_t *buf, int len)
{
    FILETIME ft;
    ft.dwHighDateTime = (DWORD)(filetime >> 32);
    ft.dwLowDateTime = (DWORD)(filetime & 0xFFFFFFFF);
    
    SYSTEMTIME stUTC, stLocal;
    if (FileTimeToSystemTime(&ft, &stUTC)) {
        if (SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal)) {
            swprintf(buf, len, L"%04d-%02d-%02d %02d:%02d:%02d",
                     stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                     stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
            return;
        }
    }
    wcscpy(buf, L"Unknown");
}

void gal_close_full(AppState *s)
{
    s->view_mode = VIEW_GALLERY;
    r_free_full_image(s);
    s->needs_redraw = 1;
}

void gal_render_fullimage(HDC hdc, AppState *s)
{
    (void)hdc; // Unused parameter
    r_clear(s, 0.08f, 0.08f, 0.08f); // Sleek darker premium background

    if (s->count == 0 || s->selected_index < 0 || s->selected_index >= s->count) {
        r_present(s);
        return;
    }

    // Try loading full-resolution image when debounce delay has expired
    if (s->images && s->full_load_timer <= 0.0) {
        if (r_load_full_image(s, s->images[s->selected_index].path)) {
            // Preload ALL visible images in the bottom strip in memory parallelly (staggered, 1 per frame)
            float main_w = (float)s->window_width - 40.0f * s->dpi_scale;
            float avail_w = main_w - 100.0f * s->dpi_scale;
            int thumb_w = (int)(80 * s->dpi_scale);
            int thumb_pad = (int)(10 * s->dpi_scale);
            int col_w = thumb_w + thumb_pad;

            int num_strip_thumbs = (int)(avail_w / col_w);
            if (num_strip_thumbs < 1) num_strip_thumbs = 1;
            if (num_strip_thumbs > s->count) num_strip_thumbs = s->count;

            int half_n = num_strip_thumbs / 2;
            int start_idx = s->selected_index - half_n;
            if (start_idx < 0) start_idx = 0;
            int end_idx = start_idx + num_strip_thumbs - 1;
            if (end_idx >= s->count) {
                end_idx = s->count - 1;
                start_idx = end_idx - num_strip_thumbs + 1;
                if (start_idx < 0) start_idx = 0;
            }

            for (int i = start_idx; i <= end_idx; i++) {
                if (i == s->selected_index) continue;
                if (!r_get_full_image_slot(s, s->images[i].path)) {
                    r_load_full_image(s, s->images[i].path);
                    s->needs_redraw = 1; // trigger another render on next frame to stagger load
                    break;
                }
            }

        }
    }

    static InstanceData instances[4096];
    int inst_count = 0;

    // --- 1. Main Image Area ---
    float main_x = 20.0f * s->dpi_scale;
    float main_y = s->layout.topbar_height + 20.0f * s->dpi_scale;
    float main_w = (float)s->window_width - 40.0f * s->dpi_scale;
    float main_h = (float)s->window_height - (s->layout.topbar_height + 160.0f * s->dpi_scale);

    if (main_w > 0 && main_h > 0) {
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, s->images[s->selected_index].path) : NULL;
        if (slot && slot->w > 0 && slot->h > 0) {
            // Render with true aspect ratio, zoom & panning!
            float img_w = (float)slot->w;
            float img_h = (float)slot->h;
            float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
            float display_w = img_w * scale * s->zoom_level;
            float display_h = img_h * scale * s->zoom_level;
            float display_x = main_x + (main_w - display_w) / 2.0f + s->zoom_pan_x;
            float display_y = main_y + (main_h - display_h) / 2.0f + s->zoom_pan_y;

            // Draw full high-resolution image!
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = display_x;
            instances[inst_count].y = display_y;
            instances[inst_count].w = display_w;
            instances[inst_count].h = display_h;
            instances[inst_count].tex_index = -5; // Samples from register(t1)
            instances[inst_count].opacity = 1.0f;
            instances[inst_count].corner_radius = 4.0f * s->dpi_scale;
            inst_count++;

            // Ensure active_full_srv matches the geometry we just submitted
            s->active_full_srv = slot->srv;
        }
    }

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(s->hwnd, &pt);

    // Solid top letterbox panel mask (masks zoomed overflow behind top controls)
    instances[inst_count] = (InstanceData){0};
    instances[inst_count].x = 0.0f;
    instances[inst_count].y = 0.0f;
    instances[inst_count].w = (float)s->window_width;
    instances[inst_count].h = s->layout.topbar_height + 20.0f * s->dpi_scale;
    instances[inst_count].tex_index = -3; // Solid dark gray backplate
    instances[inst_count].opacity = 1.0f;
    inst_count++;

    // --- 2. Back Button ---
    ui_button(instances, &inst_count, 20.0f * s->dpi_scale, 20.0f * s->dpi_scale, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, 0.8f, (float)pt.x, (float)pt.y, 6.0f * s->dpi_scale);

    // --- 3. Info Button ---
    float info_btn_x = (float)s->window_width - 100.0f * s->dpi_scale;
    ui_badge(instances, &inst_count, info_btn_x, 20.0f * s->dpi_scale, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, 0.8f, s->info_open, (float)pt.x, (float)pt.y, 6.0f * s->dpi_scale);

    // --- 3.5 Zoom Level Badge UI ---
    if (s->zoom_ui_timer > 0.0f && s->zoom_level > 1.0f) {
        float cx = (float)s->window_width / 2.0f;
        float bx = cx - 60.0f * s->dpi_scale;
        float by = 20.0f * s->dpi_scale;
        float bw = 120.0f * s->dpi_scale;
        float bh = 30.0f * s->dpi_scale;
        int hovered = (pt.x >= bx && pt.x <= bx + bw && pt.y >= by && pt.y <= by + bh);
        ui_badge(instances, &inst_count, bx, by, bw, bh, 0.8f, hovered, (float)pt.x, (float)pt.y, 6.0f * s->dpi_scale);
    }

    // --- 4. Bottom Strip ---
    float strip_y = (float)s->window_height - 130.0f * s->dpi_scale;

    // Solid bottom strip panel backplate (masks zoomed overflow)
    instances[inst_count] = (InstanceData){0};
    instances[inst_count].x = 0.0f;
    instances[inst_count].y = strip_y;
    instances[inst_count].w = (float)s->window_width;
    instances[inst_count].h = 130.0f * s->dpi_scale;
    instances[inst_count].tex_index = -3; // Gray backplate
    instances[inst_count].opacity = 1.0f;  // Fully solid
    inst_count++;

    // Previous Arrow <
    ui_button(instances, &inst_count, 20.0f * s->dpi_scale, strip_y + 35.0f * s->dpi_scale, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, 0.8f, (float)pt.x, (float)pt.y, 15.0f * s->dpi_scale);

    // Next Arrow >
    ui_button(instances, &inst_count, (float)s->window_width - 50.0f * s->dpi_scale, strip_y + 35.0f * s->dpi_scale, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, 0.8f, (float)pt.x, (float)pt.y, 15.0f * s->dpi_scale);

    // Bottom strip thumbnails window centered around s->selected_index
    float avail_w = (float)s->window_width - 140.0f * s->dpi_scale; // Width between arrows
    int thumb_w = (int)(80 * s->dpi_scale);
    int thumb_h = (int)(80 * s->dpi_scale);
    int thumb_pad = (int)(10 * s->dpi_scale);
    int col_w = thumb_w + thumb_pad;

    int num_strip_thumbs = (int)(avail_w / col_w);
    if (num_strip_thumbs < 1) num_strip_thumbs = 1;
    if (num_strip_thumbs > s->count) num_strip_thumbs = s->count;

    int half_n = num_strip_thumbs / 2;
    int start_idx = s->selected_index - half_n;
    if (start_idx < 0) start_idx = 0;
    int end_idx = start_idx + num_strip_thumbs - 1;
    if (end_idx >= s->count) {
        end_idx = s->count - 1;
        start_idx = end_idx - num_strip_thumbs + 1;
        if (start_idx < 0) start_idx = 0;
    }

    float total_thumbs_w = (float)(num_strip_thumbs * thumb_w + (num_strip_thumbs - 1) * thumb_pad);
    float thumbs_start_x = 55.0f * s->dpi_scale + (avail_w - total_thumbs_w) / 2.0f;

    for (int i = start_idx; i <= end_idx; i++) {
        float tx = thumbs_start_x + (float)((i - start_idx) * col_w);
        float ty = strip_y + 10.0f * s->dpi_scale;

        // Lazy load strip thumbnails
        if (s->images[i].texture_slot == -1 && !s->images[i].thumb_requested) {
            s->images[i].thumb_requested = 1;
            aw_request_thumbnail(s, s->images[i].path, THUMB_SIZE, s->hwnd);
        }

        // Draw selection border if active
        if (i == s->selected_index) {
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = tx - 4.0f * s->dpi_scale;
            instances[inst_count].y = ty - 4.0f * s->dpi_scale;
            instances[inst_count].w = (float)(thumb_w + 8.0f * s->dpi_scale);
            instances[inst_count].h = (float)(thumb_h + 8.0f * s->dpi_scale);
            instances[inst_count].tex_index = -7; // Accent border
            instances[inst_count].opacity = 1.0f;
            instances[inst_count].corner_radius = 8.0f * s->dpi_scale;
            inst_count++;
        }

        // Draw thumbnail
        instances[inst_count] = (InstanceData){0};
        instances[inst_count].x = tx;
        instances[inst_count].y = ty;
        instances[inst_count].w = (float)thumb_w;
        instances[inst_count].h = (float)thumb_h;
        instances[inst_count].tex_index = s->images[i].texture_slot;
        instances[inst_count].opacity = 1.0f;
        instances[inst_count].corner_radius = 6.0f * s->dpi_scale;

        if (s->images[i].texture_slot != -1) {
            s->tex_pool.last_used[s->images[i].texture_slot] = s->tex_pool.frame_counter;
        }
        inst_count++;
    }

    // --- 5. Info Overlay Box ---
    float info_x = (float)s->window_width - 320.0f * s->dpi_scale;
    float info_y = s->layout.topbar_height + 25.0f * s->dpi_scale;
    float info_w = 300.0f * s->dpi_scale;
    float info_h = 240.0f * s->dpi_scale;

    if (s->info_open) {
        ui_panel(instances, &inst_count, info_x, info_y, info_w, info_h, 0.92f, 1, s->layout.card_radius);

        // Circular close button in top right of info card
        float close_w = 20.0f * s->dpi_scale;
        float close_h = 20.0f * s->dpi_scale;
        float close_x = info_x + info_w - close_w - 10.0f * s->dpi_scale;
        float close_y = info_y + 10.0f * s->dpi_scale;
        ui_button(instances, &inst_count, close_x, close_y, close_w, close_h, 0.6f, (float)pt.x, (float)pt.y, close_w * 0.5f);
    }

    // Draw all D3D11 geometry
    r_draw_instances(s, instances, inst_count);

    // Draw Back and Info Button text
    float btn_icon_size = 14.0f * s->dpi_scale;
    r_draw_text_ext(s, L"\uE72B", 20.0f * s->dpi_scale + (80.0f * s->dpi_scale - btn_icon_size)/2.0f, 20.0f * s->dpi_scale + (30.0f * s->dpi_scale - btn_icon_size)/2.0f, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, s->dwrite_format_icons, s->theme.text_main);
    r_draw_text_ext(s, L"\uE946", info_btn_x + (80.0f * s->dpi_scale - btn_icon_size)/2.0f, 20.0f * s->dpi_scale + (30.0f * s->dpi_scale - btn_icon_size)/2.0f, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, s->dwrite_format_icons, s->theme.text_main);

    // Draw Zoom Level badge text if active
    if (s->zoom_ui_timer > 0.0f && s->zoom_level > 1.0f) {
        float cx = (float)s->window_width / 2.0f;
        float bx = cx - 60.0f * s->dpi_scale;
        float by = 20.0f * s->dpi_scale;
        float bw = 120.0f * s->dpi_scale;
        float bh = 30.0f * s->dpi_scale;
        POINT m_pt;
        GetCursorPos(&m_pt);
        ScreenToClient(s->hwnd, &m_pt);
        int hovered = (m_pt.x >= bx && m_pt.x <= bx + bw && m_pt.y >= by && m_pt.y <= by + bh);
        wchar_t zoom_text[64];
        if (hovered) {
            wcscpy(zoom_text, L"Reset Zoom");
        } else {
            swprintf(zoom_text, 64, L"Zoom: %.0f%%", s->zoom_level * 100.0f);
        }
        ui_badge_text(s, zoom_text, bx, by, bw, bh);
    }

    // Draw previous/next strip buttons
    float strip_icon_size = 12.0f * s->dpi_scale;
    r_draw_text_ext(s, L"\uE76B", 20.0f * s->dpi_scale + (30.0f * s->dpi_scale - strip_icon_size)/2.0f, strip_y + 35.0f * s->dpi_scale + (30.0f * s->dpi_scale - strip_icon_size)/2.0f, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, s->dwrite_format_icons, s->theme.text_main);
    r_draw_text_ext(s, L"\uE76C", (float)s->window_width - 50.0f * s->dpi_scale + (30.0f * s->dpi_scale - strip_icon_size)/2.0f, strip_y + 35.0f * s->dpi_scale + (30.0f * s->dpi_scale - strip_icon_size)/2.0f, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, s->dwrite_format_icons, s->theme.text_main);

    // Draw Metadata Card details
    if (s->info_open) {
        ImageEntry *e = &s->images[s->selected_index];
        if (e->full_width == 0) {
            int w = 0, h = 0;
            if (il_get_image_dimensions(e->path, &w, &h)) {
                e->full_width = (uint16_t)w;
                e->full_height = (uint16_t)h;
            }
        }

        // Fetch actual texture size if loaded in high-res D3D slot
        int actual_w = e->full_width;
        int actual_h = e->full_height;
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, e->path) : NULL;
        if (slot && slot->w > 0 && slot->h > 0) {
            actual_w = slot->w;
            actual_h = slot->h;
        }

        wchar_t sz_buf[64] = {0};
        format_size(e->file_size, sz_buf, 64);

        wchar_t tc_buf[64] = {0};
        format_filetime(e->created_time, tc_buf, 64);

        wchar_t tm_buf[64] = {0};
        format_filetime(e->last_modified, tm_buf, 64);

        wchar_t dim_buf[64] = {0};
        if (actual_w > 0 && actual_h > 0) {
            swprintf(dim_buf, 64, L"%d x %d", actual_w, actual_h);
        } else {
            wcscpy(dim_buf, L"Unknown");
        }

        // Limit path display to avoid overflow
        wchar_t path_trunc[128] = {0};
        if (wcslen(e->path) > 30) {
            swprintf(path_trunc, 128, L"...%ls", e->path + wcslen(e->path) - 27);
        } else {
            wcscpy(path_trunc, e->path);
        }

        wchar_t name_trunc[128] = {0};
        if (wcslen(e->filename) > 22) {
            swprintf(name_trunc, 128, L"%.19ls...", e->filename);
        } else {
            wcscpy(name_trunc, e->filename);
        }

        // Draw close button close icon text
        float close_w = 20.0f * s->dpi_scale;
        float close_h = 20.0f * s->dpi_scale;
        float close_x = info_x + info_w - close_w - 10.0f * s->dpi_scale;
        float close_y = info_y + 10.0f * s->dpi_scale;
        float close_icon_size = 10.0f * s->dpi_scale;
        r_draw_text_ext(s, L"\uE711", close_x + (close_w - close_icon_size)/2.0f, close_y + (close_h - close_icon_size)/2.0f, close_w, close_h, s->dwrite_format_icons, s->theme.text_main);

        float pad = 15.0f * s->dpi_scale;
        float item_h = 24.0f * s->dpi_scale;

        // Render line items beautiful Segoe UI Variable text
        r_draw_text_ext(s, L"IMAGE METADATA", info_x + pad, info_y + pad, info_w - pad * 2.0f, item_h, s->dwrite_format_semibold, s->theme.text_main);
        
        wchar_t line[256];
        swprintf(line, 256, L"Name:  %ls", name_trunc);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 1.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Path:  %ls", path_trunc);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 2.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Size:  %ls", sz_buf);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 3.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Dims:  %ls", dim_buf);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 4.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Created:  %ls", tc_buf);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 5.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Modified: %ls", tm_buf);
        r_draw_text_ext(s, line, info_x + pad, info_y + pad + item_h * 6.2f, info_w - pad * 2.0f, item_h, s->dwrite_format_regular, s->theme.text_main);
    }

    r_present(s);
    s->needs_redraw = 0;
}

int gal_handle_fullimage_click(AppState *s, int x, int y)
{
    if (s->view_mode != VIEW_FULLIMAGE) return 0;

    float dpi = s->dpi_scale > 0.0f ? s->dpi_scale : 1.0f;

    // --- 1. Hit test back button ---
    if (x >= 20 * dpi && x <= 100 * dpi && y >= 20 * dpi && y <= 50 * dpi) {
        gal_close_full(s);
        return 1;
    }

    // --- 2. Hit test info button ---
    if (x >= s->window_width - 100 * dpi && x <= s->window_width - 20 * dpi && y >= 20 * dpi && y <= 50 * dpi) {
        s->info_open = !s->info_open;
        s->needs_redraw = 1;
        return 1;
    }

    // --- 2.5 Zoom badge hit test ---
    if (s->zoom_ui_timer > 0.0f && s->zoom_level > 1.0f) {
        float cx = (float)s->window_width / 2.0f;
        float bx = cx - 60.0f * dpi;
        float by = 20.0f * dpi;
        float bw = 120.0f * dpi;
        float bh = 30.0f * dpi;
        if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
            s->zoom_level = 1.0f;
            s->zoom_ui_timer = 0.0f;
            gal_clamp_zoom_pan(s);
            s->needs_redraw = 1;
            return 1;
        }
    }

    float info_x = (float)s->window_width - 320.0f * dpi;
    float info_y = s->layout.topbar_height + 25.0f * dpi;
    float info_w = 300.0f * dpi;
    float info_h = 240.0f * dpi;

    // --- 2.6 Hit test metadata close button ---
    if (s->info_open) {
        float close_w = 20.0f * dpi;
        float close_h = 20.0f * dpi;
        float close_x = info_x + info_w - close_w - 10.0f * dpi;
        float close_y = info_y + 10.0f * dpi;
        if (x >= close_x && x <= close_x + close_w && y >= close_y && y <= close_y + close_h) {
            s->info_open = 0;
            s->needs_redraw = 1;
            return 1;
        }
    }

    // --- 3. Click inside Info Box & Click Outside handling ---
    int closed_info = 0;
    if (s->info_open) {
        if (x >= info_x && x <= info_x + info_w && y >= info_y && y <= info_y + info_h) {
            return 1; // Click inside info box -> consume click
        }
        // Click was outside info box -> close it
        s->info_open = 0;
        s->needs_redraw = 1;
        closed_info = 1;
    }

    // --- 4. Bottom Navigation Strip ---
    float strip_y = (float)s->window_height - 130.0f * dpi;
    float strip_h = 130.0f * dpi;

    if (y >= strip_y && y <= strip_y + strip_h) {
        // Prev Arrow circular button hit test
        if (x >= 20 * dpi && x <= 50 * dpi && y >= strip_y + 35.0f * dpi && y <= strip_y + 65.0f * dpi) {
            int new_idx = s->selected_index - 1;
            if (new_idx >= 0) {
                gal_select_full_image(s, new_idx);
            }
            return 1;
        }

        // Next Arrow circular button hit test
        if (x >= s->window_width - 50 * dpi && x <= s->window_width - 20 * dpi && y >= strip_y + 35.0f * dpi && y <= strip_y + 65.0f * dpi) {
            int new_idx = s->selected_index + 1;
            if (new_idx < s->count) {
                gal_select_full_image(s, new_idx);
            }
            return 1;
        }

        // Individual thumbnail hit testing in the strip
        float avail_w = (float)s->window_width - 140.0f * dpi;
        int thumb_w = (int)(80 * dpi);
        int thumb_pad = (int)(10 * dpi);
        int col_w = thumb_w + thumb_pad;

        int num_strip_thumbs = (int)(avail_w / col_w);
        if (num_strip_thumbs < 1) num_strip_thumbs = 1;
        if (num_strip_thumbs > s->count) num_strip_thumbs = s->count;

        int half_n = num_strip_thumbs / 2;
        int start_idx = s->selected_index - half_n;
        if (start_idx < 0) start_idx = 0;
        int end_idx = start_idx + num_strip_thumbs - 1;
        if (end_idx >= s->count) {
            end_idx = s->count - 1;
            start_idx = end_idx - num_strip_thumbs + 1;
            if (start_idx < 0) start_idx = 0;
        }

        float total_thumbs_w = (float)(num_strip_thumbs * thumb_w + (num_strip_thumbs - 1) * thumb_pad);
        float thumbs_start_x = 55.0f * dpi + (avail_w - total_thumbs_w) / 2.0f;

        for (int i = start_idx; i <= end_idx; i++) {
            float tx = thumbs_start_x + (float)((i - start_idx) * col_w);
            float ty = strip_y + 10.0f * dpi;

            if (x >= tx && x <= tx + thumb_w && y >= ty && y <= ty + thumb_w) {
                if (s->selected_index != i) {
                    gal_select_full_image(s, i);
                }
                return 1;
            }
        }
    }

    return closed_info;
}

void gal_clamp_zoom_pan(AppState *s)
{
    if (s->zoom_level <= 1.0f) {
        s->zoom_level = 1.0f;
        s->zoom_pan_x = 0.0f;
        s->zoom_pan_y = 0.0f;
        s->is_panning = 0;
        return;
    }
    if (s->zoom_level > 8.0f) {
        s->zoom_level = 8.0f;
    }

    float img_w = 0.0f, img_h = 0.0f;
    if (s->count > 0 && s->selected_index >= 0 && s->selected_index < s->count) {
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, s->images[s->selected_index].path) : NULL;
        if (slot && slot->texture) {
            img_w = (float)slot->w;
            img_h = (float)slot->h;
        } else {
            ImageEntry *e = s->images ? &s->images[s->selected_index] : NULL;
            if (e) {
                img_w = (float)e->full_width;
                img_h = (float)e->full_height;
            }
        }
    }
    if (img_w <= 0.0f || img_h <= 0.0f) {
        img_w = (float)s->window_width;
        img_h = (float)s->window_height;
    }

    float main_w = (float)s->window_width - 40.0f * s->dpi_scale;
    float main_h = (float)s->window_height - (s->layout.topbar_height + 160.0f * s->dpi_scale);
    float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
    float display_w = img_w * scale * s->zoom_level;
    float display_h = img_h * scale * s->zoom_level;

    float max_pan_x = (display_w > main_w) ? (display_w - main_w) / 2.0f : 0.0f;
    float max_pan_y = (display_h > main_h) ? (display_h - main_h) / 2.0f : 0.0f;

    if (s->zoom_pan_x < -max_pan_x) s->zoom_pan_x = -max_pan_x;
    if (s->zoom_pan_x > max_pan_x) s->zoom_pan_x = max_pan_x;
    if (s->zoom_pan_y < -max_pan_y) s->zoom_pan_y = -max_pan_y;
    if (s->zoom_pan_y > max_pan_y) s->zoom_pan_y = max_pan_y;
}
