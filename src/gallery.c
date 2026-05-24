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
    s->selected_index = index;
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

void gal_render_fullimage(HDC hdc, AppState *s)
{
    // Out of scope for D3D11 Phase 1
    gal_render_gallery(hdc, s);
}
