// =========================================================================
// gallery.c — Gallery grid using D3D11 instanced rendering
// =========================================================================
#include "types.h"

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

#define TOP_BAR_HEIGHT 0

static void gal_calc_layout(AppState *s, GridLayout *out)
{
    out->pad = THUMB_SIZE + THUMB_PADDING;
    out->cols = (s->window_width - GALLERY_PADDING) / out->pad;
    if (out->cols < 1) out->cols = 1;
    
    out->grid_width = out->cols * THUMB_SIZE + (out->cols - 1) * THUMB_PADDING;
    out->left_margin = (s->window_width - out->grid_width) / 2;
    if (out->left_margin < GALLERY_PADDING) out->left_margin = GALLERY_PADDING;
    
    out->scroll_int = (int)s->scroll_current_y;
    int top_margin = TOP_BAR_HEIGHT + GALLERY_PADDING;
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
    if (lay.cols == 0 || s->count == 0) return 0;
    int rows = ceil_div(s->count, lay.cols);
    int top_margin = TOP_BAR_HEIGHT + GALLERY_PADDING;
    int total = rows * lay.pad + top_margin;
    int ms = total - s->window_height + GALLERY_PADDING;
    return (ms < 0) ? 0 : ms;
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

    for (int i = lay.first_visible; i < lay.last_visible; i++) {
        int row = i / lay.cols, col = i % lay.cols;
        int ix = lay.left_margin + col * lay.pad;
        int iy = TOP_BAR_HEIGHT + GALLERY_PADDING + row * lay.pad - lay.scroll_int;
        if (x >= ix && x < ix + THUMB_SIZE && y >= iy && y < iy + THUMB_SIZE) {
            *out_index = i; return 1;
        }
    }
    return 0;
}

void gal_open_full(AppState *s, int index)
{
    if (index < 0 || index >= s->count) return;
    r_free_full_image(s);
    s->selected_index = index;
    s->view_mode = VIEW_FULLIMAGE;
    s->full_load_timer = 0.0;
    s->needs_redraw = 1;
}

void gal_close_full(AppState *s)
{
    s->view_mode = VIEW_GALLERY;
    s->needs_redraw = 1;
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

    for (int i = lay.first_visible; i < lay.last_visible; i++) {
        int row = i / lay.cols, col = i % lay.cols;
        float x = (float)(lay.left_margin + col * lay.pad);
        float y = (float)(TOP_BAR_HEIGHT + GALLERY_PADDING + row * lay.pad - lay.scroll_int);

        if (y + THUMB_SIZE < 0 || y > s->window_height) continue;

        if (s->images[i].state == IMG_STATE_NEW && !s->images[i].thumb_requested) {
            s->images[i].thumb_requested = 1;
            aw_request_thumbnail(s, s->images[i].path, THUMB_SIZE, s->hwnd);
        }

        if (s->selected_index == i) {
            instances[inst_count].x = x - 4.0f;
            instances[inst_count].y = y - 4.0f;
            instances[inst_count].w = (float)(THUMB_SIZE + 8);
            instances[inst_count].h = (float)(THUMB_SIZE + 8);
            instances[inst_count].tex_index = -2; // White border
            instances[inst_count].opacity = 1.0f;
            inst_count++;
        }

        instances[inst_count].x = x;
        instances[inst_count].y = y;
        instances[inst_count].w = (float)THUMB_SIZE;
        instances[inst_count].h = (float)THUMB_SIZE;
        instances[inst_count].tex_index = (s->images[i].state == IMG_STATE_RESIDENT_GPU) ? s->images[i].texture_slot : -1;
        instances[inst_count].opacity = 1.0f;
        
        if (s->images[i].state == IMG_STATE_RESIDENT_GPU && s->images[i].texture_slot != -1) {
            s->tex_pool.last_used[s->images[i].texture_slot] = s->tex_pool.frame_counter;
        }

        inst_count++;
        if (inst_count >= 4092) break; // Leave room for scrollbar
    }

    // Draw Scrollbar
    int ms = gal_max_scroll(s);
    if (ms > 0) {
        float track_x = (float)s->window_width - 12.0f;
        float track_y = 8.0f;
        float track_w = 6.0f;
        float track_h = (float)s->window_height - 16.0f;

        // Track
        instances[inst_count].x = track_x;
        instances[inst_count].y = track_y;
        instances[inst_count].w = track_w;
        instances[inst_count].h = track_h;
        instances[inst_count].tex_index = -3;
        instances[inst_count].opacity = 1.0f;
        inst_count++;

        // Thumb
        float thumb_h = (s->window_height / (float)(ms + s->window_height)) * track_h;
        if (thumb_h < 20.0f) thumb_h = 20.0f;
        float thumb_y = track_y + (s->scroll_current_y / (float)ms) * (track_h - thumb_h);

        instances[inst_count].x = track_x;
        instances[inst_count].y = thumb_y;
        instances[inst_count].w = track_w;
        instances[inst_count].h = thumb_h;
        instances[inst_count].tex_index = -4;
        instances[inst_count].opacity = 1.0f;
        inst_count++;
    }

    int btn_w = 80;
    int btn_h = 30;
    int btn_x = s->window_width - btn_w - 20;
    int btn_y = 10;

    // Draw Sort Button BG (translucent)
    instances[inst_count].x = (float)btn_x;
    instances[inst_count].y = (float)btn_y;
    instances[inst_count].w = (float)btn_w;
    instances[inst_count].h = (float)btn_h;
    instances[inst_count].tex_index = -3; // 0.2 gray
    instances[inst_count].opacity = 0.8f;
    inst_count++;

    if (s->sort_menu_open) {
        int menu_w = 150;
        int menu_h = 5 * 30;
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + 5;
        
        // Menu BG
        instances[inst_count].x = (float)menu_x;
        instances[inst_count].y = (float)menu_y;
        instances[inst_count].w = (float)menu_w;
        instances[inst_count].h = (float)menu_h;
        instances[inst_count].tex_index = -3; 
        instances[inst_count].opacity = 0.95f;
        inst_count++;
    }

    r_draw_instances(s, instances, inst_count);

    r_draw_text(s, L"Sort \x25BC", (float)(btn_x + 15), (float)(btn_y + 5), (float)btn_w, (float)btn_h);

    if (s->sort_menu_open) {
        int menu_w = 150;
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + 5;
        
        const wchar_t* opts[] = {
            s->sort_mode == SORT_DATE_CREATED ? L"\x2713 Date created" : L"  Date created",
            s->sort_mode == SORT_DATE_MODIFIED ? L"\x2713 Date modified" : L"  Date modified",
            s->sort_mode == SORT_SIZE ? L"\x2713 Size" : L"  Size",
            !s->sort_descending ? L"\x2713 Ascending" : L"  Ascending",
            s->sort_descending ? L"\x2713 Descending" : L"  Descending"
        };
        
        for (int i = 0; i < 5; i++) {
            r_draw_text(s, opts[i], (float)(menu_x + 10), (float)(menu_y + 5 + i * 30), (float)menu_w, 30.0f);
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

void gal_render_fullimage(HDC hdc, AppState *s)
{
    (void)hdc; // Unused parameter
    r_clear(s, 0.08f, 0.08f, 0.08f); // Sleek darker premium background

    if (s->count == 0 || s->selected_index < 0 || s->selected_index >= s->count) {
        r_present(s);
        return;
    }

    // Try loading full-resolution image when debounce delay has expired
    if (s->full_load_timer <= 0.0) {
        r_load_full_image(s, s->images[s->selected_index].path);
    }

    static InstanceData instances[4096];
    int inst_count = 0;

    // --- 1. Main Image Area ---
    float main_x = 20.0f;
    float main_y = 70.0f;
    float main_w = (float)s->window_width - 40.0f;
    float main_h = (float)s->window_height - 230.0f; // Height minus top area and bottom strip

    if (main_w > 0 && main_h > 0) {
        if (s->full_srv && s->full_texture_w > 0 && s->full_texture_h > 0) {
            // Render with true aspect ratio!
            float img_w = (float)s->full_texture_w;
            float img_h = (float)s->full_texture_h;
            float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
            float display_w = img_w * scale;
            float display_h = img_h * scale;
            float display_x = main_x + (main_w - display_w) / 2.0f;
            float display_y = main_y + (main_h - display_h) / 2.0f;

            // Draw full high-resolution image!
            instances[inst_count].x = display_x;
            instances[inst_count].y = display_y;
            instances[inst_count].w = display_w;
            instances[inst_count].h = display_h;
            instances[inst_count].tex_index = -5; // Samples from register(t1)
            instances[inst_count].opacity = 1.0f;
            inst_count++;
        } else {
            // Fallback to square thumbnail if loading or failed
            float sq_size = main_w < main_h ? main_w : main_h;
            float display_x = main_x + (main_w - sq_size) / 2.0f;
            float display_y = main_y + (main_h - sq_size) / 2.0f;

            int active_idx = s->selected_index;
            if (s->images[active_idx].state == IMG_STATE_NEW && !s->images[active_idx].thumb_requested) {
                s->images[active_idx].thumb_requested = 1;
                aw_request_thumbnail(s, s->images[active_idx].path, THUMB_SIZE, s->hwnd);
            }

            instances[inst_count].x = display_x;
            instances[inst_count].y = display_y;
            instances[inst_count].w = sq_size;
            instances[inst_count].h = sq_size;
            instances[inst_count].tex_index = (s->images[active_idx].state == IMG_STATE_RESIDENT_GPU) ? s->images[active_idx].texture_slot : -1;
            instances[inst_count].opacity = 1.0f;

            if (s->images[active_idx].state == IMG_STATE_RESIDENT_GPU && s->images[active_idx].texture_slot != -1) {
                s->tex_pool.last_used[s->images[active_idx].texture_slot] = s->tex_pool.frame_counter;
            }
            inst_count++;
        }
    }

    // --- 2. Back Button ---
    instances[inst_count].x = 20.0f;
    instances[inst_count].y = 20.0f;
    instances[inst_count].w = 80.0f;
    instances[inst_count].h = 30.0f;
    instances[inst_count].tex_index = -3; // Translucent dark gray
    instances[inst_count].opacity = 0.8f;
    inst_count++;

    // --- 3. Info Button ---
    float info_btn_x = (float)s->window_width - 100.0f;
    if (s->info_open) {
        // Thin white border for active info button
        instances[inst_count].x = info_btn_x - 2.0f;
        instances[inst_count].y = 18.0f;
        instances[inst_count].w = 84.0f;
        instances[inst_count].h = 34.0f;
        instances[inst_count].tex_index = -2; // White border
        instances[inst_count].opacity = 0.9f;
        inst_count++;
    }
    instances[inst_count].x = info_btn_x;
    instances[inst_count].y = 20.0f;
    instances[inst_count].w = 80.0f;
    instances[inst_count].h = 30.0f;
    instances[inst_count].tex_index = -3;
    instances[inst_count].opacity = 0.8f;
    inst_count++;

    // --- 4. Bottom Strip ---
    float strip_y = (float)s->window_height - 130.0f;

    // Previous Arrow <
    instances[inst_count].x = 20.0f;
    instances[inst_count].y = strip_y + 35.0f;
    instances[inst_count].w = 30.0f;
    instances[inst_count].h = 30.0f;
    instances[inst_count].tex_index = -3;
    instances[inst_count].opacity = 0.8f;
    inst_count++;

    // Next Arrow >
    instances[inst_count].x = (float)s->window_width - 50.0f;
    instances[inst_count].y = strip_y + 35.0f;
    instances[inst_count].w = 30.0f;
    instances[inst_count].h = 30.0f;
    instances[inst_count].tex_index = -3;
    instances[inst_count].opacity = 0.8f;
    inst_count++;

    // Bottom strip thumbnails window centered around s->selected_index
    float avail_w = (float)s->window_width - 140.0f; // Width between arrows
    int thumb_w = 80;
    int thumb_h = 80;
    int thumb_pad = 10;
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
    float thumbs_start_x = 55.0f + (avail_w - total_thumbs_w) / 2.0f;

    for (int i = start_idx; i <= end_idx; i++) {
        float tx = thumbs_start_x + (float)((i - start_idx) * col_w);
        float ty = strip_y + 10.0f;

        // Lazy load strip thumbnails
        if (s->images[i].state == IMG_STATE_NEW && !s->images[i].thumb_requested) {
            s->images[i].thumb_requested = 1;
            aw_request_thumbnail(s, s->images[i].path, THUMB_SIZE, s->hwnd);
        }

        // Draw selection border if active
        if (i == s->selected_index) {
            instances[inst_count].x = tx - 4.0f;
            instances[inst_count].y = ty - 4.0f;
            instances[inst_count].w = (float)(thumb_w + 8);
            instances[inst_count].h = (float)(thumb_h + 8);
            instances[inst_count].tex_index = -2; // White border
            instances[inst_count].opacity = 1.0f;
            inst_count++;
        }

        // Draw thumbnail
        instances[inst_count].x = tx;
        instances[inst_count].y = ty;
        instances[inst_count].w = (float)thumb_w;
        instances[inst_count].h = (float)thumb_h;
        instances[inst_count].tex_index = (s->images[i].state == IMG_STATE_RESIDENT_GPU) ? s->images[i].texture_slot : -1;
        instances[inst_count].opacity = 1.0f;

        if (s->images[i].state == IMG_STATE_RESIDENT_GPU && s->images[i].texture_slot != -1) {
            s->tex_pool.last_used[s->images[i].texture_slot] = s->tex_pool.frame_counter;
        }
        inst_count++;
    }

    // --- 5. Info Overlay Box ---
    float info_x = (float)s->window_width - 320.0f;
    float info_y = 60.0f;
    float info_w = 300.0f;
    float info_h = 220.0f;

    if (s->info_open) {
        // Underlay white border
        instances[inst_count].x = info_x - 1.0f;
        instances[inst_count].y = info_y - 1.0f;
        instances[inst_count].w = info_w + 2.0f;
        instances[inst_count].h = info_h + 2.0f;
        instances[inst_count].tex_index = -2; // White border
        instances[inst_count].opacity = 0.5f; // Premium semi-translucent border
        inst_count++;

        // Solid background
        instances[inst_count].x = info_x;
        instances[inst_count].y = info_y;
        instances[inst_count].w = info_w;
        instances[inst_count].h = info_h;
        instances[inst_count].tex_index = -3; // Dark gray
        instances[inst_count].opacity = 0.95f;
        inst_count++;
    }

    // Draw all D3D11 geometry
    r_draw_instances(s, instances, inst_count);

    // Draw Back and Info Button text
    r_draw_text(s, L"< back", 32.0f, 25.0f, 60.0f, 25.0f);
    r_draw_text(s, L"\x24D8 info", info_btn_x + 15.0f, 25.0f, 60.0f, 25.0f);

    // Draw previous/next strip buttons
    r_draw_text(s, L"<", 30.0f, strip_y + 40.0f, 20.0f, 20.0f);
    r_draw_text(s, L">", (float)s->window_width - 40.0f, strip_y + 40.0f, 20.0f, 20.0f);

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
        if (s->full_texture_w > 0 && s->full_texture_h > 0 && _wcsicmp(s->full_loaded_path, e->path) == 0) {
            actual_w = s->full_texture_w;
            actual_h = s->full_texture_h;
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

        // Render line items beautiful Segoe UI text
        r_draw_text(s, L"IMAGE METADATA", info_x + 15.0f, info_y + 15.0f, info_w - 30.0f, 25.0f);
        
        wchar_t line[256];
        swprintf(line, 256, L"Name:  %ls", name_trunc);
        r_draw_text(s, line, info_x + 15.0f, info_y + 45.0f, info_w - 30.0f, 20.0f);

        swprintf(line, 256, L"Path:  %ls", path_trunc);
        r_draw_text(s, line, info_x + 15.0f, info_y + 75.0f, info_w - 30.0f, 20.0f);

        swprintf(line, 256, L"Size:  %ls", sz_buf);
        r_draw_text(s, line, info_x + 15.0f, info_y + 105.0f, info_w - 30.0f, 20.0f);

        swprintf(line, 256, L"Dims:  %ls", dim_buf);
        r_draw_text(s, line, info_x + 15.0f, info_y + 135.0f, info_w - 30.0f, 20.0f);

        swprintf(line, 256, L"Created:  %ls", tc_buf);
        r_draw_text(s, line, info_x + 15.0f, info_y + 165.0f, info_w - 30.0f, 20.0f);

        swprintf(line, 256, L"Modified: %ls", tm_buf);
        r_draw_text(s, line, info_x + 15.0f, info_y + 190.0f, info_w - 30.0f, 20.0f);
    }

    r_present(s);
    s->needs_redraw = 0;
}

int gal_handle_fullimage_click(AppState *s, int x, int y)
{
    if (s->view_mode != VIEW_FULLIMAGE) return 0;

    // --- 1. Hit test back button ---
    if (x >= 20 && x <= 100 && y >= 20 && y <= 50) {
        gal_close_full(s);
        return 1;
    }

    // --- 2. Hit test info button ---
    if (x >= s->window_width - 100 && x <= s->window_width - 20 && y >= 20 && y <= 50) {
        s->info_open = !s->info_open;
        s->needs_redraw = 1;
        return 1;
    }

    // --- 3. Click inside Info Box & Click Outside handling ---
    int closed_info = 0;
    if (s->info_open) {
        float info_x = (float)s->window_width - 320.0f;
        float info_y = 60.0f;
        float info_w = 300.0f;
        float info_h = 220.0f;
        if (x >= info_x && x <= info_x + info_w && y >= info_y && y <= info_y + info_h) {
            return 1; // Click inside info box -> consume click
        }
        // Click was outside info box -> close it
        s->info_open = 0;
        s->needs_redraw = 1;
        closed_info = 1;
    }

    // --- 4. Bottom Navigation Strip ---
    float strip_y = (float)s->window_height - 130.0f;
    float strip_h = 100.0f;

    if (y >= strip_y && y <= strip_y + strip_h) {
        // Prev Arrow <
        if (x >= 20 && x <= 50) {
            int new_idx = s->selected_index - 1;
            if (new_idx >= 0) {
                r_free_full_image(s);
                s->full_load_timer = 0.15;
                s->selected_index = new_idx;
                s->needs_redraw = 1;
            }
            return 1;
        }

        // Next Arrow >
        if (x >= s->window_width - 50 && x <= s->window_width - 20) {
            int new_idx = s->selected_index + 1;
            if (new_idx < s->count) {
                r_free_full_image(s);
                s->full_load_timer = 0.15;
                s->selected_index = new_idx;
                s->needs_redraw = 1;
            }
            return 1;
        }

        // Individual thumbnail hit testing in the strip
        float avail_w = (float)s->window_width - 140.0f;
        int thumb_w = 80;
        int thumb_pad = 10;
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
        float thumbs_start_x = 55.0f + (avail_w - total_thumbs_w) / 2.0f;

        for (int i = start_idx; i <= end_idx; i++) {
            float tx = thumbs_start_x + (float)((i - start_idx) * col_w);
            float ty = strip_y + 10.0f;

            if (x >= tx && x <= tx + thumb_w && y >= ty && y <= ty + thumb_w) {
                if (s->selected_index != i) {
                    r_free_full_image(s);
                    s->full_load_timer = 0.15;
                    s->selected_index = i;
                    s->needs_redraw = 1;
                }
                return 1;
            }
        }
    }

    return closed_info;
}
