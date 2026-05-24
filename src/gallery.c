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

static void gal_calc_layout(AppState *s, GridLayout *out)
{
    out->pad = THUMB_SIZE + THUMB_PADDING;
    out->cols = (s->window_width - GALLERY_PADDING) / out->pad;
    if (out->cols < 1) out->cols = 1;
    
    out->grid_width = out->cols * THUMB_SIZE + (out->cols - 1) * THUMB_PADDING;
    out->left_margin = (s->window_width - out->grid_width) / 2;
    if (out->left_margin < GALLERY_PADDING) out->left_margin = GALLERY_PADDING;
    
    out->scroll_int = (int)s->scroll_current_y;
    out->first_row = (out->scroll_int - GALLERY_PADDING) / out->pad;
    if (out->first_row < 0) out->first_row = 0;
    
    out->last_row = (out->scroll_int + s->window_height - GALLERY_PADDING) / out->pad + 1;
    
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
    int total = rows * lay.pad + GALLERY_PADDING;
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
    GridLayout lay;
    gal_calc_layout(s, &lay);

    for (int i = lay.first_visible; i < lay.last_visible; i++) {
        int row = i / lay.cols, col = i % lay.cols;
        int ix = lay.left_margin + col * lay.pad;
        int iy = GALLERY_PADDING + row * lay.pad - lay.scroll_int;
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
        float y = (float)(GALLERY_PADDING + row * lay.pad - lay.scroll_int);

        if (y + THUMB_SIZE < 0 || y > s->window_height) continue;

        if (s->images[i].state == IMG_STATE_NEW && !s->images[i].thumb_requested) {
            s->images[i].thumb_requested = 1;
            aw_request_thumbnail(s, s->images[i].path, THUMB_SIZE, s->hwnd);
        }

        instances[inst_count].x = x;
        instances[inst_count].y = y;
        instances[inst_count].size = (float)THUMB_SIZE;
        instances[inst_count].tex_index = (s->images[i].state == IMG_STATE_RESIDENT_GPU) ? s->images[i].texture_slot : -1;
        instances[inst_count].opacity = 1.0f;
        
        if (s->selected_index != -1 && s->selected_index != i) {
            instances[inst_count].opacity = 0.5f;
        }

        if (s->images[i].state == IMG_STATE_RESIDENT_GPU && s->images[i].texture_slot != -1) {
            s->tex_pool.last_used[s->images[i].texture_slot] = s->tex_pool.frame_counter;
        }

        inst_count++;
        if (inst_count >= 4096) break;
    }

    r_draw_instances(s, instances, inst_count);
    r_present(s);
    s->needs_redraw = 0;
}

void gal_render_fullimage(HDC hdc, AppState *s)
{
    // Out of scope for D3D11 Phase 1
    gal_render_gallery(hdc, s);
}
