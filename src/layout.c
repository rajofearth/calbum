#include "types.h"
#include <math.h>

void gal_calc_layout(const DataState *data, const ViewState *view, const UIState *ui, int window_width,
                     int window_height, GridLayout *out)
{
    float dpi = ui->dpi_scale > 0.0F ? ui->dpi_scale : 1.0F;
    float thumb_size = 160.0F * dpi;
    float thumb_padding = ui->layout.grid_gap > 0.0F ? ui->layout.grid_gap : 8.0F * dpi;
    float gallery_padding = ui->layout.panel_padding > 0.0F ? ui->layout.panel_padding : 16.0F * dpi;

    out->pad = (int) (thumb_size + thumb_padding);
    if (out->pad < 1)
        out->pad = 1;

    out->cols = (int) (((float) window_width - gallery_padding) / (float) out->pad);
    if (out->cols < 1)
        out->cols = 1;

    out->grid_width = (int) (((float) out->cols * thumb_size) + ((float) (out->cols - 1) * thumb_padding));
    out->left_margin = (window_width - out->grid_width) / 2;
    if (out->left_margin < (int) gallery_padding)
        out->left_margin = (int) gallery_padding;

    out->scroll_int = (int) view->scroll_current_y;
    float top_margin_h = ui->layout.topbar_height > 0.0F ? ui->layout.topbar_height : 0.0F;
    int top_margin = (int) (top_margin_h + gallery_padding);
    out->first_row = (out->scroll_int - top_margin) / out->pad;
    if (out->first_row < 0)
        out->first_row = 0;

    out->last_row = ((out->scroll_int + window_height - top_margin) / out->pad) + 1;

    out->first_visible = out->first_row * out->cols;
    out->last_visible = (out->last_row + 1) * out->cols;
    if (out->last_visible > data->grid_item_count)
        out->last_visible = data->grid_item_count;
}

int gal_max_scroll(const DataState *data, const ViewState *view, const UIState *ui, int window_width, int window_height)
{
    GridLayout lay;
    gal_calc_layout(data, view, ui, window_width, window_height, &lay);
    if (lay.cols <= 0)
        return 0;

    int total_rows = ceil_div(data->grid_item_count, lay.cols);
    float dpi = ui->dpi_scale > 0.0F ? ui->dpi_scale : 1.0F;
    float gallery_padding = ui->layout.panel_padding > 0.0F ? ui->layout.panel_padding : 16.0F * dpi;
    float top_margin_h = ui->layout.topbar_height > 0.0F ? ui->layout.topbar_height : 0.0F;
    int top_margin = (int) (top_margin_h + gallery_padding);
    int content_h = top_margin + (total_rows * lay.pad) + (int) gallery_padding;
    int max_s = content_h - window_height;
    return max_s < 0 ? 0 : max_s;
}

int gal_hit_test(const DataState *data, const ViewState *view, const UIState *ui, int window_width, int window_height,
                 int x, int y, int *out_index)
{
    if (view->view_mode != VIEW_GALLERY || data->grid_item_count == 0)
        return 0;
    if (ui->sort_menu_open)
        return 0;
    GridLayout lay;
    gal_calc_layout(data, view, ui, window_width, window_height, &lay);

    float dpi = ui->dpi_scale > 0.0F ? ui->dpi_scale : 1.0F;
    float thumb_size = 160.0F * dpi;
    float top_margin_h = ui->layout.topbar_height > 0.0F ? ui->layout.topbar_height : 0.0F;
    float gallery_padding = ui->layout.panel_padding > 0.0F ? ui->layout.panel_padding : 16.0F * dpi;

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

void gal_clamp_zoom_pan(ViewState *view, int window_width, int window_height, float dpi_scale, float topbar_height)
{
    (void) dpi_scale;
    if (view->zoom_level <= ZOOM_MIN)
    {
        view->zoom_level = ZOOM_MIN;
        view->zoom_pan_x = 0.0F;
        view->zoom_pan_y = 0.0F;
        view->is_panning = 0;
        return;
    }
    if (view->zoom_level > ZOOM_MAX)
    {
        view->zoom_level = ZOOM_MAX;
    }

    float img_w = 0.0F;
    float img_h = 0.0F;
    // Note: no image data access in this function anymore - caller provides image dimensions
    if (img_w <= 0.0F || img_h <= 0.0F)
    {
        img_w = (float) window_width;
        img_h = (float) window_height;
    }

    float main_w = (float) window_width - (40.0F * dpi_scale);
    float main_h = (float) window_height - (topbar_height + (160.0F * dpi_scale));
    if (main_w <= 0.0F)
        main_w = 1.0F;
    if (main_h <= 0.0F)
        main_h = 1.0F;
    float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
    float display_w = img_w * scale * view->zoom_level;
    float display_h = img_h * scale * view->zoom_level;

    float max_pan_x = (display_w > main_w) ? (display_w - main_w) / 2.0F : 0.0F;
    float max_pan_y = (display_h > main_h) ? (display_h - main_h) / 2.0F : 0.0F;

    if (view->zoom_pan_x < -max_pan_x)
        view->zoom_pan_x = -max_pan_x;
    if (view->zoom_pan_x > max_pan_x)
        view->zoom_pan_x = max_pan_x;
    if (view->zoom_pan_y < -max_pan_y)
        view->zoom_pan_y = -max_pan_y;
    if (view->zoom_pan_y > max_pan_y)
        view->zoom_pan_y = max_pan_y;
}
