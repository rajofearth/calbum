#include "types.h"
#include <math.h>

void gal_calc_layout(const AppState *s, GridLayout *out)
{
    float dpi = s->dpi_scale > 0.0F ? s->dpi_scale : 1.0F;
    float thumb_size = 160.0F * dpi;
    float thumb_padding = s->layout.grid_gap > 0.0F ? s->layout.grid_gap : 8.0F * dpi;
    float gallery_padding = s->layout.panel_padding > 0.0F ? s->layout.panel_padding : 16.0F * dpi;

    out->pad = (int) (thumb_size + thumb_padding);
    if (out->pad < 1)
        out->pad = 1;

    out->cols = (int) (((float) s->window_width - gallery_padding) / (float) out->pad);
    if (out->cols < 1)
        out->cols = 1;

    out->grid_width = (int) (((float) out->cols * thumb_size) + ((float) (out->cols - 1) * thumb_padding));
    out->left_margin = (s->window_width - out->grid_width) / 2;
    if (out->left_margin < (int) gallery_padding)
        out->left_margin = (int) gallery_padding;

    out->scroll_int = (int) s->scroll_current_y;
    float top_margin_h = s->layout.topbar_height > 0.0F ? s->layout.topbar_height : 0.0F;
    int top_margin = (int) (top_margin_h + gallery_padding);
    out->first_row = (out->scroll_int - top_margin) / out->pad;
    if (out->first_row < 0)
        out->first_row = 0;

    out->last_row = ((out->scroll_int + s->window_height - top_margin) / out->pad) + 1;

    out->first_visible = out->first_row * out->cols;
    out->last_visible = (out->last_row + 1) * out->cols;
    if (out->last_visible > s->grid_item_count)
        out->last_visible = s->grid_item_count;
}

int gal_max_scroll(const AppState *s)
{
    GridLayout lay;
    gal_calc_layout(s, &lay);
    if (lay.cols <= 0)
        return 0;

    int total_rows = ceil_div(s->grid_item_count, lay.cols);
    float dpi = s->dpi_scale > 0.0F ? s->dpi_scale : 1.0F;
    float gallery_padding = s->layout.panel_padding > 0.0F ? s->layout.panel_padding : 16.0F * dpi;
    float top_margin_h = s->layout.topbar_height > 0.0F ? s->layout.topbar_height : 0.0F;
    int top_margin = (int) (top_margin_h + gallery_padding);
    int content_h = top_margin + (total_rows * lay.pad) + (int) gallery_padding;
    int max_s = content_h - s->window_height;
    return max_s < 0 ? 0 : max_s;
}

int gal_hit_test(const AppState *s, int x, int y, int *out_index)
{
    if (s->view_mode != VIEW_GALLERY || s->grid_item_count == 0)
        return 0;
    if (s->sort_menu_open)
        return 0;
    GridLayout lay;
    gal_calc_layout(s, &lay);

    float dpi = s->dpi_scale > 0.0F ? s->dpi_scale : 1.0F;
    float thumb_size = 160.0F * dpi;
    float top_margin_h = s->layout.topbar_height > 0.0F ? s->layout.topbar_height : 0.0F;
    float gallery_padding = s->layout.panel_padding > 0.0F ? s->layout.panel_padding : 16.0F * dpi;

    for (int i = lay.first_visible; i < lay.last_visible; i++)
    {
        int row = i / lay.cols;
        int col = i % lay.cols;
        int ix = lay.left_margin + (col * lay.pad);
        int iy = (int) (top_margin_h + gallery_padding + (float) (row * lay.pad) - (float) lay.scroll_int);
        if (x >= ix && (float) x < (float) ix + thumb_size && y >= iy && (float) y < (float) iy + thumb_size)
        {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

void gal_clamp_zoom_pan(AppState *s)
{
    if (s->zoom_level <= 1.0F)
    {
        s->zoom_level = 1.0F;
        s->zoom_pan_x = 0.0F;
        s->zoom_pan_y = 0.0F;
        s->is_panning = 0;
        return;
    }
    if (s->zoom_level > 8.0F)
    {
        s->zoom_level = 8.0F;
    }

    float img_w = 0.0F;
    float img_h = 0.0F;
    if (s->count > 0 && s->selected_index >= 0 && s->selected_index < s->count)
    {
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, s->images[s->selected_index].path) : NULL;
        if (slot && slot->texture)
        {
            img_w = (float) slot->w;
            img_h = (float) slot->h;
        }
        else
        {
            ImageEntry *e = s->images ? &s->images[s->selected_index] : NULL;
            if (e)
            {
                img_w = (float) e->full_width;
                img_h = (float) e->full_height;
            }
        }
    }
    if (img_w <= 0.0F || img_h <= 0.0F)
    {
        img_w = (float) s->window_width;
        img_h = (float) s->window_height;
    }

    float main_w = (float) s->window_width - (40.0F * s->dpi_scale);
    float main_h = (float) s->window_height - (s->layout.topbar_height + (160.0F * s->dpi_scale));
    if (main_w <= 0.0F)
        main_w = 1.0F;
    if (main_h <= 0.0F)
        main_h = 1.0F;
    float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
    float display_w = img_w * scale * s->zoom_level;
    float display_h = img_h * scale * s->zoom_level;

    float max_pan_x = (display_w > main_w) ? (display_w - main_w) / 2.0F : 0.0F;
    float max_pan_y = (display_h > main_h) ? (display_h - main_h) / 2.0F : 0.0F;

    if (s->zoom_pan_x < -max_pan_x)
        s->zoom_pan_x = -max_pan_x;
    if (s->zoom_pan_x > max_pan_x)
        s->zoom_pan_x = max_pan_x;
    if (s->zoom_pan_y < -max_pan_y)
        s->zoom_pan_y = -max_pan_y;
    if (s->zoom_pan_y > max_pan_y)
        s->zoom_pan_y = max_pan_y;
}
