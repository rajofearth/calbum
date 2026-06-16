// =========================================================================
// gallery_fullimage.c — Full-image viewer mode with zoom, pan, info overlay
// =========================================================================
#include "types.h"
#include "../lib/ui/ui.h"
#include <d2d1.h>
#include <math.h>

void gal_select_full_image(DataState *data, ViewState *view, GpuState *r, WorkerState *worker, int index, HWND hwnd)
{
    int limit = data->grid_items ? data->grid_item_count : data->count;
    if (index < 0 || index >= limit)
        return;
    view->selected_index = index;
    view->zoom_level = 1.0F;
    view->zoom_ui_timer = 0.0F;
    view->zoom_pan_x = 0.0F;
    view->zoom_pan_y = 0.0F;
    view->is_panning = 0;
    if (data->images)
    {
        int img_idx = (data->grid_items) ? data->grid_items[index].image_index : index;
        if (img_idx < 0 || img_idx >= data->count)
            return;
        ImageEntry *e = &data->images[img_idx];
        if (e->full_width == 0)
        {
            int w = 0;
            int h = 0;
            if (il_get_image_dimensions(e->path, &w, &h))
            {
                e->full_width = (uint16_t) w;
                e->full_height = (uint16_t) h;
            }
        }

        if (e->file_size < (uint64_t) 2 * 1024 * 1024)
        {
            data->full_load_timer = 0.0;
            r_load_full_image(r, e->path);
        }
        else
        {
            // Async load for large files (Section 2.1)
            data->full_load_pending = 1;
            aw_request_full_image(worker, e->path, hwnd);
        }
    }
    else
    {
        data->full_load_pending = 1;
    }
}

void gal_open_full(ViewState *view, int index)
{
    (void) index;
    view->view_mode = VIEW_FULLIMAGE;
    // gal_select_full_image is called by the parent with proper state
}

void gal_close_full(ViewState *view, GpuState *r, int *needs_redraw)
{
    view->view_mode = VIEW_GALLERY;
    r_free_full_image(r);
    *needs_redraw = 1;
}

// ── Strip bounds helpers ────────────────────────────────────────────────

// Computes which thumbnails in the bottom strip are visible
// based on the active image index and available width.
void fiv_strip_bounds(const UIState *ui, int window_width, int active_strip_idx, int total_images, int *out_start,
                      int *out_end, int *out_num_thumbs)
{
    float dpi = ui->dpi_scale > 0.0F ? ui->dpi_scale : 1.0F;
    float avail_w = (float) window_width - (140.0F * dpi);
    int thumb_w = (int) (80 * dpi);
    int thumb_pad = (int) (10 * dpi);
    int col_w = thumb_w + thumb_pad;

    int num_thumbs = (int) (avail_w / (float) col_w);
    if (num_thumbs < 1)
        num_thumbs = 1;
    if (num_thumbs > total_images)
        num_thumbs = total_images;

    int half_n = num_thumbs / 2;
    int start = active_strip_idx - half_n;
    if (start < 0)
        start = 0;
    int end = start + num_thumbs - 1;
    if (end >= total_images)
    {
        end = total_images - 1;
        start = end - num_thumbs + 1;
        if (start < 0)
            start = 0;
    }
    *out_start = start;
    *out_end = end;
    *out_num_thumbs = num_thumbs;
}

// Returns 1 if the given image path is visible in the bottom strip.
int fiv_is_in_strip(const DataState *data, const ViewState *view, int grid_item_count, const wchar_t *path)
{
    if (!data->images || !data->grid_items || data->strip_image_count <= 0)
        return 0;

    int active_img_idx_in_strip = -1;
    for (int i = 0; i < data->strip_image_count; i++)
    {
        if (data->strip_image_grid_indices[i] == view->selected_index)
        {
            active_img_idx_in_strip = i;
            break;
        }
    }
    if (active_img_idx_in_strip == -1)
        return 0;

    // Note: fiv_is_in_strip cannot determine visible bounds without UIState/window_width.
    // Check if the path is in the full strip list instead.
    for (int k = 0; k < data->strip_image_count; k++)
    {
        int grid_idx = data->strip_image_grid_indices[k];
        if (grid_idx >= 0 && grid_idx < grid_item_count)
        {
            int img_idx = data->grid_items[grid_idx].image_index;
            if (img_idx >= 0 && img_idx < data->count)
            {
                if (_wcsicmp(path, data->images[img_idx].path) == 0)
                    return 1;
            }
        }
    }
    return 0;
}

// ── Full-image render helpers ──────────────────────────────────────────

// Pre-loads adjacent strip images after debounce delay expires
static void fiv_update_preloading(DataState *data, ViewState *view, GpuState *r, UIState *ui, int window_width,
                                  int active_img_idx)
{
    if (data->images && data->full_load_timer <= 0.0 && !data->full_load_pending)
    {
        if (r_load_full_image(r, data->images[active_img_idx].path))
        {
            // Preload ALL visible images in the bottom strip (staggered, 1 per frame)
            if (data->grid_items && data->strip_image_count > 0)
            {
                int active_img_idx_in_strip = -1;
                for (int i = 0; i < data->strip_image_count; i++)
                {
                    if (data->strip_image_grid_indices[i] == view->selected_index)
                    {
                        active_img_idx_in_strip = i;
                        break;
                    }
                }

                if (active_img_idx_in_strip != -1)
                {
                    int start_strip_idx;
                    int end_strip_idx;
                    int num_strip_thumbs;
                    fiv_strip_bounds(ui, window_width, active_img_idx_in_strip, data->strip_image_count,
                                     &start_strip_idx, &end_strip_idx, &num_strip_thumbs);

                    for (int k = start_strip_idx; k <= end_strip_idx; k++)
                    {
                        int grid_idx = data->strip_image_grid_indices[k];
                        if (grid_idx == view->selected_index)
                            continue;
                        int img_idx = data->grid_items[grid_idx].image_index;
                        if (!r_get_full_image_slot(r, data->images[img_idx].path))
                        {
                            r_load_full_image(r, data->images[img_idx].path);
                            break;
                        }
                    }
                }
            }
            else
            {
                int start_idx;
                int end_idx;
                int num_thumbs;
                fiv_strip_bounds(ui, window_width, view->selected_index, data->count, &start_idx, &end_idx,
                                 &num_thumbs);

                for (int i = start_idx; i <= end_idx; i++)
                {
                    if (i == view->selected_index)
                        continue;
                    if (!r_get_full_image_slot(r, data->images[i].path))
                    {
                        r_load_full_image(r, data->images[i].path);
                        break;
                    }
                }
            }
        }
    }
}

// Renders the main high-resolution image area with zoom/pan
static void fiv_render_main_image(DataState *data, ViewState *view, GpuState *r, UIState *ui, InstanceData *instances,
                                  int *inst_count, int active_img_idx, float main_x, float main_y, float main_w,
                                  float main_h)
{
    if (main_w > 0 && main_h > 0)
    {
        FullImageSlot *slot = data->images ? r_get_full_image_slot(r, data->images[active_img_idx].path) : NULL;
        if (slot && slot->w > 0 && slot->h > 0)
        {
            float img_w = (float) slot->w;
            float img_h = (float) slot->h;
            float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
            float display_w = img_w * scale * view->zoom_level;
            float display_h = img_h * scale * view->zoom_level;
            float display_x = main_x + ((main_w - display_w) / 2.0F) + view->zoom_pan_x;
            float display_y = main_y + ((main_h - display_h) / 2.0F) + view->zoom_pan_y;

            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = display_x;
            instances[*inst_count].y = display_y;
            instances[*inst_count].w = display_w;
            instances[*inst_count].h = display_h;
            instances[*inst_count].tex_index = TOKEN_FULL_IMAGE;
            instances[*inst_count].opacity = 1.0F;
            instances[*inst_count].corner_radius = 4.0F * ui->dpi_scale;
            if (*inst_count >= MAX_INSTANCES - 16)
                return;
            (*inst_count)++;

            r->active_full_srv = slot->srv;
        }
    }
}

// Renders top letterbox panel mask and control buttons (back, info, zoom badge)
static void fiv_render_top_controls(UIState *ui, ViewState *view, int window_width, InstanceData *instances,
                                    int *inst_count, POINT pt, float info_btn_x)
{
    // Solid top letterbox panel mask
    instances[*inst_count] = (InstanceData){0};
    instances[*inst_count].x = 0.0F;
    instances[*inst_count].y = 0.0F;
    instances[*inst_count].w = (float) window_width;
    instances[*inst_count].h = ui->layout.topbar_height + (20.0F * ui->dpi_scale);
    instances[*inst_count].tex_index = TOKEN_PANEL;
    instances[*inst_count].opacity = 1.0F;
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    (*inst_count)++;

    // Back Button
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, inst_count, 20.0F * ui->dpi_scale, 20.0F * ui->dpi_scale, 80.0F * ui->dpi_scale,
              30.0F * ui->dpi_scale, 0.8F, (float) pt.x, (float) pt.y, 6.0F * ui->dpi_scale);

    // Info Button
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    ui_badge(instances, inst_count, info_btn_x, 20.0F * ui->dpi_scale, 80.0F * ui->dpi_scale, 30.0F * ui->dpi_scale,
             0.8F, ui->info_open, (float) pt.x, (float) pt.y, 6.0F * ui->dpi_scale);

    // Zoom Level Badge UI
    if (view->zoom_ui_timer > 0.0F && view->zoom_level > 1.0F)
    {
        float cx = (float) window_width / 2.0F;
        float bx = cx - (60.0F * ui->dpi_scale);
        float by = 20.0F * ui->dpi_scale;
        float bw = 120.0F * ui->dpi_scale;
        float bh = 30.0F * ui->dpi_scale;
        int hovered = ((float) pt.x >= bx && (float) pt.x <= bx + bw && (float) pt.y >= by && (float) pt.y <= by + bh);
        if (*inst_count >= MAX_INSTANCES - 16)
            return;
        ui_badge(instances, inst_count, bx, by, bw, bh, 0.8F, hovered, (float) pt.x, (float) pt.y,
                 6.0F * ui->dpi_scale);
    }
}

// Renders bottom strip panel, navigation arrows, and thumbnail strip
static void fiv_render_bottom_strip(DataState *data, ViewState *view, GpuState *r, UIState *ui, WorkerState *worker,
                                    int window_width, HWND hwnd, InstanceData *instances, int *inst_count,
                                    int active_img_idx, float strip_y, POINT pt)
{
    (void) active_img_idx;

    // Bottom strip panel backplate
    instances[*inst_count] = (InstanceData){0};
    instances[*inst_count].x = 0.0F;
    instances[*inst_count].y = strip_y;
    instances[*inst_count].w = (float) window_width;
    instances[*inst_count].h = 130.0F * ui->dpi_scale;
    instances[*inst_count].tex_index = TOKEN_PANEL;
    instances[*inst_count].opacity = 1.0F;
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    (*inst_count)++;

    // Previous Arrow <
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, inst_count, 20.0F * ui->dpi_scale, strip_y + (35.0F * ui->dpi_scale), 30.0F * ui->dpi_scale,
              30.0F * ui->dpi_scale, 0.8F, (float) pt.x, (float) pt.y, 15.0F * ui->dpi_scale);

    // Next Arrow >
    if (*inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, inst_count, (float) window_width - (50.0F * ui->dpi_scale), strip_y + (35.0F * ui->dpi_scale),
              30.0F * ui->dpi_scale, 30.0F * ui->dpi_scale, 0.8F, (float) pt.x, (float) pt.y, 15.0F * ui->dpi_scale);

    // Bottom strip thumbnails window centered around view->selected_index
    float avail_w = (float) window_width - (140.0F * ui->dpi_scale);
    int thumb_w = (int) (80 * ui->dpi_scale);
    int thumb_h = (int) (80 * ui->dpi_scale);
    int thumb_pad = (int) (10 * ui->dpi_scale);
    int col_w = thumb_w + thumb_pad;

    int start_idx = 0;
    int end_idx = 0;
    int active_img_idx_in_strip = -1;
    int total_images = 0;

    if (data->grid_items && data->strip_image_count > 0)
    {
        total_images = data->strip_image_count;
        for (int i = 0; i < data->strip_image_count; i++)
        {
            if (data->strip_image_grid_indices[i] == view->selected_index)
            {
                active_img_idx_in_strip = i;
                break;
            }
        }
    }
    else
    {
        total_images = data->count;
        active_img_idx_in_strip = view->selected_index;
    }

    if (total_images > 0 && active_img_idx_in_strip != -1)
    {
        int num_strip_thumbs;
        fiv_strip_bounds(ui, window_width, active_img_idx_in_strip, total_images, &start_idx, &end_idx,
                         &num_strip_thumbs);

        float total_thumbs_w = (float) ((num_strip_thumbs * thumb_w) + ((num_strip_thumbs - 1) * thumb_pad));
        float thumbs_start_x = (55.0F * ui->dpi_scale) + ((avail_w - total_thumbs_w) / 2.0F);

        for (int k = start_idx; k <= end_idx; k++)
        {
            int i = (data->grid_items && data->strip_image_count > 0) ? data->strip_image_grid_indices[k] : k;
            int img_idx = data->grid_items ? data->grid_items[i].image_index : i;
            if (img_idx < 0 || img_idx >= data->count)
                continue;

            float tx = thumbs_start_x + (float) ((k - start_idx) * col_w);
            float ty = strip_y + (10.0F * ui->dpi_scale);

            // Lazy load strip thumbnails
            if (data->images != NULL && data->images[img_idx].texture_slot == -1 &&
                !data->images[img_idx].thumb_requested)
            {
                data->images[img_idx].thumb_requested = 1;
                if (!aw_request_thumbnail(worker, data->images[img_idx].path, THUMB_SIZE, hwnd))
                {
                    data->images[img_idx].thumb_requested = 0; // allow retry next frame
                }
            }

            // Draw selection border if active
            if (i == view->selected_index)
            {
                instances[*inst_count] = (InstanceData){0};
                instances[*inst_count].x = tx - (4.0F * ui->dpi_scale);
                instances[*inst_count].y = ty - (4.0F * ui->dpi_scale);
                instances[*inst_count].w = (float) thumb_w + (8.0F * ui->dpi_scale);
                instances[*inst_count].h = (float) thumb_h + (8.0F * ui->dpi_scale);
                instances[*inst_count].tex_index = TOKEN_ACCENT;
                instances[*inst_count].opacity = 1.0F;
                instances[*inst_count].corner_radius = 8.0F * ui->dpi_scale;
                if (*inst_count >= MAX_INSTANCES - 16)
                    break;
                (*inst_count)++;
            }

            // Draw thumbnail
            int tex = data->images ? data->images[img_idx].texture_slot : -1;

            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = tx;
            instances[*inst_count].y = ty;
            instances[*inst_count].w = (float) thumb_w;
            instances[*inst_count].h = (float) thumb_h;
            instances[*inst_count].tex_index = tex;
            instances[*inst_count].opacity = 1.0F;
            instances[*inst_count].corner_radius = 6.0F * ui->dpi_scale;

            if (data->images && data->images[img_idx].texture_slot != -1)
            {
                r->tex_pool.last_used[data->images[img_idx].texture_slot] = r->tex_pool.frame_counter;
            }
            if (*inst_count >= MAX_INSTANCES - 16)
                break;
            (*inst_count)++;
        }
    }
}

// Renders info panel blur overlay + close button (D3D instances), then flushes
static void fiv_render_info_panel(GpuState *r, UIState *ui, InstanceData *instances, int *inst_count,
                                  int active_img_idx, float info_x, float info_y, float info_w, float info_h, POINT pt)
{
    (void) active_img_idx;

    // Draw all background elements first
    r_draw_instances(r, instances, *inst_count);
    *inst_count = 0;

    if (ui->info_open)
    {
        r_copy_backbuffer_for_blur(r);
        ui_blur_panel(instances, inst_count, info_x, info_y, info_w, info_h, 0.92F, 1, ui->layout.card_radius);

        // Circular close button in top right of info card
        float close_w = 20.0F * ui->dpi_scale;
        float close_h = 20.0F * ui->dpi_scale;
        float close_x = info_x + info_w - close_w - (10.0F * ui->dpi_scale);
        float close_y = info_y + (10.0F * ui->dpi_scale);
        ui_button(instances, inst_count, close_x, close_y, close_w, close_h, 0.6F, (float) pt.x, (float) pt.y,
                  close_w * 0.5F);

        // Draw the info panel overlay elements
        r_draw_instances(r, instances, *inst_count);
        *inst_count = 0;
    }

    // Draw all D3D11 geometry
    r_draw_instances(r, instances, *inst_count);
}

// Renders all D2D text: back/info icons, loading indicator, zoom badge, strip arrows, info metadata
static void fiv_render_d2d_text(DataState *data, ViewState *view, GpuState *r, TextState *txt, UIState *ui,
                                int window_width, int active_img_idx, float info_btn_x, float main_x, float main_y,
                                float main_w, float main_h, float strip_y, float info_x, float info_y, float info_w,
                                float info_h, POINT pt)
{
    (void) info_h;

    // Draw Back and Info Button text
    r_draw_text_aligned(txt, r, L"\uE72B", 20.0F * ui->dpi_scale, 20.0F * ui->dpi_scale, 80.0F * ui->dpi_scale,
                        30.0F * ui->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, txt->dwrite_format_icons,
                        ui->theme.text_main);
    r_draw_text_aligned(txt, r, L"\uE946", info_btn_x, 20.0F * ui->dpi_scale, 80.0F * ui->dpi_scale,
                        30.0F * ui->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, txt->dwrite_format_icons,
                        ui->theme.text_main);

    // Loading indicator
    if (data->full_load_pending)
    {
        FullImageSlot *ls = data->images ? r_get_full_image_slot(r, data->images[active_img_idx].path) : NULL;
        if (!ls || !ls->srv)
        {
            float muted[4] = {0.663F, 0.686F, 0.737F, 1.0F};
            r_draw_text_aligned(txt, r, L"Loading\u2026", main_x, main_y, main_w, main_h, ALIGN_X_CENTER,
                                ALIGN_Y_CENTER, txt->dwrite_format, muted);
        }
    }

    // Zoom Level badge text
    if (view->zoom_ui_timer > 0.0F && view->zoom_level > 1.0F)
    {
        float cx = (float) window_width / 2.0F;
        float bx = cx - (60.0F * ui->dpi_scale);
        float by = 20.0F * ui->dpi_scale;
        float bw = 120.0F * ui->dpi_scale;
        float bh = 30.0F * ui->dpi_scale;
        int hovered = ((float) pt.x >= bx && (float) pt.x <= bx + bw && (float) pt.y >= by && (float) pt.y <= by + bh);
        wchar_t zoom_text[64];
        if (hovered)
        {
            wcscpy(zoom_text, L"Reset Zoom");
        }
        else
        {
            swprintf(zoom_text, 64, L"Zoom: %.0f%%", view->zoom_level * 100.0F);
        }
        ui_badge_text(txt, r, ui, zoom_text, bx, by, bw, bh);
    }

    // Previous/next strip button icons
    r_draw_text_aligned(txt, r, L"\uE76B", 20.0F * ui->dpi_scale, strip_y + (35.0F * ui->dpi_scale),
                        30.0F * ui->dpi_scale, 30.0F * ui->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER,
                        txt->dwrite_format_icons, ui->theme.text_main);
    r_draw_text_aligned(txt, r, L"\uE76C", (float) window_width - (50.0F * ui->dpi_scale),
                        strip_y + (35.0F * ui->dpi_scale), 30.0F * ui->dpi_scale, 30.0F * ui->dpi_scale, ALIGN_X_CENTER,
                        ALIGN_Y_CENTER, txt->dwrite_format_icons, ui->theme.text_main);

    // Metadata Card details
    if (ui->info_open)
    {
        ImageEntry *e = &data->images[active_img_idx];
        if (e->full_width == 0)
        {
            int w = 0;
            int h = 0;
            if (il_get_image_dimensions(e->path, &w, &h))
            {
                e->full_width = (uint16_t) w;
                e->full_height = (uint16_t) h;
            }
        }

        int actual_w = e->full_width;
        int actual_h = e->full_height;
        FullImageSlot *slot = data->images ? r_get_full_image_slot(r, e->path) : NULL;
        if (slot && slot->w > 0 && slot->h > 0)
        {
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
        if (actual_w > 0 && actual_h > 0)
        {
            swprintf(dim_buf, 64, L"%d x %d", actual_w, actual_h);
        }
        else
        {
            wcscpy(dim_buf, L"Unknown");
        }

        // Limit path display to avoid overflow
        wchar_t path_trunc[128] = {0};
        if (wcslen(e->path) > 30)
        {
            swprintf(path_trunc, 128, L"...%ls", e->path + wcslen(e->path) - 27);
        }
        else
        {
            wcscpy(path_trunc, e->path);
        }

        wchar_t name_trunc[128] = {0};
        if (wcslen(e->filename) > 22)
        {
            swprintf(name_trunc, 128, L"%.19ls...", e->filename);
        }
        else
        {
            wcscpy(name_trunc, e->filename);
        }

        // Close button icon text
        float close_w = 20.0F * ui->dpi_scale;
        float close_h = 20.0F * ui->dpi_scale;
        float close_x = info_x + info_w - close_w - (10.0F * ui->dpi_scale);
        float close_y = info_y + (10.0F * ui->dpi_scale);
        r_draw_text_aligned(txt, r, L"\uE711", close_x, close_y, close_w, close_h, ALIGN_X_CENTER, ALIGN_Y_CENTER,
                            txt->dwrite_format_icons, ui->theme.text_main);

        float pad = 15.0F * ui->dpi_scale;
        float item_h = 24.0F * ui->dpi_scale;

        r_draw_text_aligned(txt, r, L"IMAGE METADATA", info_x + pad, info_y + pad, info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_semibold, ui->theme.text_main);

        wchar_t line[256];
        swprintf(line, 256, L"Name:  %ls", name_trunc);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 1.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);

        swprintf(line, 256, L"Path:  %ls", path_trunc);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 2.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);

        swprintf(line, 256, L"Size:  %ls", sz_buf);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 3.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);

        swprintf(line, 256, L"Dims:  %ls", dim_buf);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 4.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);

        swprintf(line, 256, L"Created:  %ls", tc_buf);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 5.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);

        swprintf(line, 256, L"Modified: %ls", tm_buf);
        r_draw_text_aligned(txt, r, line, info_x + pad, info_y + pad + (item_h * 6.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, txt->dwrite_format_mono, ui->theme.text_main);
    }
}

void gal_render_fullimage(HDC hdc, GpuState *r, TextState *txt, DataState *data, ViewState *view, UIState *ui,
                          WorkerState *worker, int window_width, int window_height, HWND hwnd)
{
    (void) hdc;
    r_clear(r, 0.118F, 0.133F, 0.153F);

    int total_items = data->grid_items ? data->grid_item_count : data->count;
    if (total_items == 0 || view->selected_index < 0 || view->selected_index >= total_items)
    {
        r_present(r);
        return;
    }

    int active_img_idx = data->grid_items ? data->grid_items[view->selected_index].image_index : view->selected_index;
    if (active_img_idx < 0 || active_img_idx >= data->count)
    {
        r_present(r);
        return;
    }

    fiv_update_preloading(data, view, r, ui, window_width, active_img_idx);

    static InstanceData instances[MAX_INSTANCES];
    int inst_count = 0;

    float main_x = 20.0F * ui->dpi_scale;
    float main_y = ui->layout.topbar_height + (20.0F * ui->dpi_scale);
    float main_w = (float) window_width - (40.0F * ui->dpi_scale);
    float main_h = (float) window_height - (ui->layout.topbar_height + (160.0F * ui->dpi_scale));

    fiv_render_main_image(data, view, r, ui, instances, &inst_count, active_img_idx, main_x, main_y, main_w, main_h);

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    float info_btn_x = (float) window_width - (100.0F * ui->dpi_scale);
    fiv_render_top_controls(ui, view, window_width, instances, &inst_count, pt, info_btn_x);

    float strip_y = (float) window_height - (130.0F * ui->dpi_scale);
    fiv_render_bottom_strip(data, view, r, ui, worker, window_width, hwnd, instances, &inst_count, active_img_idx,
                            strip_y, pt);

    float info_x = (float) window_width - (320.0F * ui->dpi_scale);
    float info_y = ui->layout.topbar_height + (25.0F * ui->dpi_scale);
    float info_w = 300.0F * ui->dpi_scale;
    float info_h = 240.0F * ui->dpi_scale;

    fiv_render_info_panel(r, ui, instances, &inst_count, active_img_idx, info_x, info_y, info_w, info_h, pt);

    txt->d2d_rtv->lpVtbl->BeginDraw(txt->d2d_rtv);
    fiv_render_d2d_text(data, view, r, txt, ui, window_width, active_img_idx, info_btn_x, main_x, main_y, main_w,
                        main_h, strip_y, info_x, info_y, info_w, info_h, pt);
    txt->d2d_rtv->lpVtbl->EndDraw(txt->d2d_rtv, NULL, NULL);

    r_present(r);
}

int gal_handle_fullimage_click(DataState *data, ViewState *view, UIState *ui, GpuState *r, WorkerState *worker, int x,
                               int y, int window_width, int window_height, int *needs_redraw, HWND hwnd)
{
    if (view->view_mode != VIEW_FULLIMAGE)
        return 0;

    float dpi = ui->dpi_scale > 0.0F ? ui->dpi_scale : 1.0F;

    // --- 1. Hit test back button ---
    if ((float) x >= 20.0F * dpi && (float) x <= 100.0F * dpi && (float) y >= 20.0F * dpi && (float) y <= 50.0F * dpi)
    {
        gal_close_full(view, r, needs_redraw);
        return 1;
    }

    // --- 2. Hit test info button ---
    if ((float) x >= (float) window_width - (100.0F * dpi) && (float) x <= (float) window_width - (20.0F * dpi) &&
        (float) y >= 20.0F * dpi && (float) y <= 50.0F * dpi)
    {
        ui->info_open = !ui->info_open;
        *needs_redraw = 1;
        return 1;
    }

    // --- 2.5 Zoom badge hit test ---
    if (view->zoom_ui_timer > 0.0F && view->zoom_level > 1.0F)
    {
        float cx = (float) window_width / 2.0F;
        float bx = cx - (60.0F * dpi);
        float by = 20.0F * dpi;
        float bw = 120.0F * dpi;
        float bh = 30.0F * dpi;
        if ((float) x >= bx && (float) x <= bx + bw && (float) y >= by && (float) y <= by + bh)
        {
            view->zoom_level = 1.0F;
            view->zoom_ui_timer = 0.0F;
            *needs_redraw = 1;
            return 1;
        }
    }

    float info_x = (float) window_width - (320.0F * dpi);
    float info_y = ui->layout.topbar_height + (25.0F * dpi);
    float info_w = 300.0F * dpi;
    float info_h = 240.0F * dpi;

    // --- 2.6 Hit test metadata close button ---
    if (ui->info_open)
    {
        float close_w = 20.0F * dpi;
        float close_h = 20.0F * dpi;
        float close_x = info_x + info_w - close_w - (10.0F * dpi);
        float close_y = info_y + (10.0F * dpi);
        if ((float) x >= close_x && (float) x <= close_x + close_w && (float) y >= close_y &&
            (float) y <= close_y + close_h)
        {
            ui->info_open = 0;
            *needs_redraw = 1;
            return 1;
        }
    }

    // --- 3. Click inside Info Box & Click Outside handling ---
    int closed_info = 0;
    if (ui->info_open)
    {
        if ((float) x >= info_x && (float) x <= info_x + info_w && (float) y >= info_y && (float) y <= info_y + info_h)
        {
            return 1; // Click inside info box -> consume click
        }
        // Click was outside info box -> close it
        ui->info_open = 0;
        *needs_redraw = 1;
        closed_info = 1;
    }

    // --- 4. Bottom Navigation Strip ---
    float strip_y = (float) window_height - (130.0F * dpi);
    float strip_h = 130.0F * dpi;

    if ((float) y >= strip_y && (float) y <= strip_y + strip_h)
    {
        int active_img_idx_in_strip = -1;
        int total_images = 0;

        if (data->grid_items && data->strip_image_count > 0)
        {
            total_images = data->strip_image_count;
            for (int i = 0; i < data->strip_image_count; i++)
            {
                if (data->strip_image_grid_indices[i] == view->selected_index)
                {
                    active_img_idx_in_strip = i;
                    break;
                }
            }
        }
        else
        {
            total_images = data->count;
            active_img_idx_in_strip = view->selected_index;
        }

        // Prev Arrow circular button hit test
        if ((float) x >= 20.0F * dpi && (float) x <= 50.0F * dpi && (float) y >= strip_y + (35.0F * dpi) &&
            (float) y <= strip_y + (65.0F * dpi))
        {
            if (active_img_idx_in_strip > 0)
            {
                int new_grid_idx = (data->grid_items && data->strip_image_count > 0) ?
                                       data->strip_image_grid_indices[active_img_idx_in_strip - 1] :
                                       active_img_idx_in_strip - 1;
                gal_select_full_image(data, view, r, worker, new_grid_idx, hwnd);
            }
            return 1;
        }

        // Next Arrow circular button hit test
        if ((float) x >= (float) window_width - (50.0F * dpi) && (float) x <= (float) window_width - (20.0F * dpi) &&
            (float) y >= strip_y + (35.0F * dpi) && (float) y <= strip_y + (65.0F * dpi))
        {
            if (active_img_idx_in_strip >= 0 && active_img_idx_in_strip < total_images - 1)
            {
                int new_grid_idx = (data->grid_items && data->strip_image_count > 0) ?
                                       data->strip_image_grid_indices[active_img_idx_in_strip + 1] :
                                       active_img_idx_in_strip + 1;
                gal_select_full_image(data, view, r, worker, new_grid_idx, hwnd);
            }
            return 1;
        }

        // Individual thumbnail hit testing in the strip
        float avail_w = (float) window_width - (140.0F * dpi);
        int thumb_w = (int) (80 * dpi);
        int thumb_pad = (int) (10 * dpi);
        int col_w = thumb_w + thumb_pad;

        if (total_images > 0 && active_img_idx_in_strip != -1)
        {
            int num_strip_thumbs;
            int start_idx;
            int end_idx;
            fiv_strip_bounds(ui, window_width, active_img_idx_in_strip, total_images, &start_idx, &end_idx,
                             &num_strip_thumbs);

            float total_thumbs_w = (float) ((num_strip_thumbs * thumb_w) + ((num_strip_thumbs - 1) * thumb_pad));
            float thumbs_start_x = (55.0F * dpi) + ((avail_w - total_thumbs_w) / 2.0F);

            for (int k = start_idx; k <= end_idx; k++)
            {
                int i = (data->grid_items && data->strip_image_count > 0) ? data->strip_image_grid_indices[k] : k;
                float tx = thumbs_start_x + (float) ((k - start_idx) * col_w);
                float ty = strip_y + (10.0F * dpi);

                if ((float) x >= tx && (float) x <= tx + (float) thumb_w && (float) y >= ty &&
                    (float) y <= ty + (float) thumb_w)
                {
                    if (view->selected_index != i)
                    {
                        gal_select_full_image(data, view, r, worker, i, hwnd);
                    }
                    return 1;
                }
            }
        }
    }

    return closed_info;
}
