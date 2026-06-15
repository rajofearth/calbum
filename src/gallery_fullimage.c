// =========================================================================
// gallery_fullimage.c — Full-image viewer mode with zoom, pan, info overlay
// =========================================================================
#include "types.h"
#include "ui.h"
#include <math.h>

void gal_select_full_image(AppState *s, int index)
{
    int limit = s->grid_items ? s->grid_item_count : s->count;
    if (index < 0 || index >= limit)
        return;
    s->selected_index = index;
    s->zoom_level = 1.0F;
    s->zoom_ui_timer = 0.0F;
    s->zoom_pan_x = 0.0F;
    s->zoom_pan_y = 0.0F;
    s->is_panning = 0;
    if (s->images)
    {
        int img_idx = (s->grid_items) ? s->grid_items[index].image_index : index;
        if (img_idx < 0 || img_idx >= s->count)
            return;
        ImageEntry *e = &s->images[img_idx];
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
            s->full_load_timer = 0.0;
            r_load_full_image(s, e->path);
        }
        else
        {
            s->full_load_timer = 0.15; // 150ms debounce for files >= 2MB
        }
    }
    else
    {
        s->full_load_timer = 0.15;
    }
    s->needs_redraw = 1;
}

void gal_open_full(AppState *s, int index)
{
    int limit = s->grid_items ? s->grid_item_count : s->count;
    if (index < 0 || index >= limit)
        return;
    s->view_mode = VIEW_FULLIMAGE;
    gal_select_full_image(s, index);
}

void gal_close_full(AppState *s)
{
    s->view_mode = VIEW_GALLERY;
    r_free_full_image(s);
    s->needs_redraw = 1;
}

void gal_render_fullimage(HDC hdc, AppState *s)
{
    (void) hdc;                         // Unused parameter
    r_clear(s, 0.118F, 0.133F, 0.153F); // Solid dark One Dark variant background

    int total_items = s->grid_items ? s->grid_item_count : s->count;
    if (total_items == 0 || s->selected_index < 0 || s->selected_index >= total_items)
    {
        r_present(s);
        return;
    }

    int active_img_idx = s->grid_items ? s->grid_items[s->selected_index].image_index : s->selected_index;
    if (active_img_idx < 0 || active_img_idx >= s->count)
    {
        r_present(s);
        return;
    }

    // Try loading full-resolution image when debounce delay has expired
    if (s->images && s->full_load_timer <= 0.0)
    {
        if (r_load_full_image(s, s->images[active_img_idx].path))
        {
            // Preload ALL visible images in the bottom strip in memory parallelly (staggered, 1 per frame)
            float main_w = (float) s->window_width - (40.0F * s->dpi_scale);
            float avail_w = main_w - (100.0F * s->dpi_scale);
            int thumb_w = (int) (80 * s->dpi_scale);
            int thumb_pad = (int) (10 * s->dpi_scale);
            int col_w = thumb_w + thumb_pad;

            int num_strip_thumbs = (int) (avail_w / (float) col_w);
            if (num_strip_thumbs < 1)
                num_strip_thumbs = 1;

            if (s->grid_items && s->strip_image_count > 0)
            {
                int active_img_idx_in_strip = -1;
                for (int i = 0; i < s->strip_image_count; i++)
                {
                    if (s->strip_image_grid_indices[i] == s->selected_index)
                    {
                        active_img_idx_in_strip = i;
                        break;
                    }
                }

                if (active_img_idx_in_strip != -1)
                {
                    int half_n = num_strip_thumbs / 2;
                    int start_strip_idx = active_img_idx_in_strip - half_n;
                    if (start_strip_idx < 0)
                        start_strip_idx = 0;
                    int end_strip_idx = start_strip_idx + num_strip_thumbs - 1;
                    if (end_strip_idx >= s->strip_image_count)
                    {
                        end_strip_idx = s->strip_image_count - 1;
                        start_strip_idx = end_strip_idx - num_strip_thumbs + 1;
                        if (start_strip_idx < 0)
                            start_strip_idx = 0;
                    }

                    for (int k = start_strip_idx; k <= end_strip_idx; k++)
                    {
                        int grid_idx = s->strip_image_grid_indices[k];
                        if (grid_idx == s->selected_index)
                            continue;
                        int img_idx = s->grid_items[grid_idx].image_index;
                        if (!r_get_full_image_slot(s, s->images[img_idx].path))
                        {
                            r_load_full_image(s, s->images[img_idx].path);
                            s->needs_redraw = 1; // trigger another render on next frame to stagger load
                            break;
                        }
                    }
                }
            }
            else
            {
                // Fallback for tests
                int num_strip_thumbs_fallback = num_strip_thumbs > s->count ? s->count : num_strip_thumbs;
                int half_n = num_strip_thumbs_fallback / 2;
                int start_idx = s->selected_index - half_n;
                if (start_idx < 0)
                    start_idx = 0;
                int end_idx = start_idx + num_strip_thumbs_fallback - 1;
                if (end_idx >= s->count)
                {
                    end_idx = s->count - 1;
                    start_idx = end_idx - num_strip_thumbs_fallback + 1;
                    if (start_idx < 0)
                        start_idx = 0;
                }

                for (int i = start_idx; i <= end_idx; i++)
                {
                    if (i == s->selected_index)
                        continue;
                    if (!r_get_full_image_slot(s, s->images[i].path))
                    {
                        r_load_full_image(s, s->images[i].path);
                        s->needs_redraw = 1; // trigger another render on next frame to stagger load
                        break;
                    }
                }
            }
        }
    }

    static InstanceData instances[MAX_INSTANCES];
    int inst_count = 0;

    // --- 1. Main Image Area ---
    float main_x = 20.0F * s->dpi_scale;
    float main_y = s->layout.topbar_height + (20.0F * s->dpi_scale);
    float main_w = (float) s->window_width - (40.0F * s->dpi_scale);
    float main_h = (float) s->window_height - (s->layout.topbar_height + (160.0F * s->dpi_scale));

    if (main_w > 0 && main_h > 0)
    {
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, s->images[active_img_idx].path) : NULL;
        if (slot && slot->w > 0 && slot->h > 0)
        {
            // Render with true aspect ratio, zoom & panning!
            float img_w = (float) slot->w;
            float img_h = (float) slot->h;
            float scale = main_w / img_w < main_h / img_h ? main_w / img_w : main_h / img_h;
            float display_w = img_w * scale * s->zoom_level;
            float display_h = img_h * scale * s->zoom_level;
            float display_x = main_x + ((main_w - display_w) / 2.0F) + s->zoom_pan_x;
            float display_y = main_y + ((main_h - display_h) / 2.0F) + s->zoom_pan_y;

            // Draw full high-resolution image!
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = display_x;
            instances[inst_count].y = display_y;
            instances[inst_count].w = display_w;
            instances[inst_count].h = display_h;
            instances[inst_count].tex_index = TOKEN_FULL_IMAGE; // Samples from register(t1)
            instances[inst_count].opacity = 1.0F;
            instances[inst_count].corner_radius = 4.0F * s->dpi_scale;
            if (inst_count >= MAX_INSTANCES - 16)
                return;
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
    instances[inst_count].x = 0.0F;
    instances[inst_count].y = 0.0F;
    instances[inst_count].w = (float) s->window_width;
    instances[inst_count].h = s->layout.topbar_height + (20.0F * s->dpi_scale);
    instances[inst_count].tex_index = TOKEN_PANEL; // Solid dark gray backplate
    instances[inst_count].opacity = 1.0F;
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    inst_count++;

    // --- 2. Back Button ---
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, &inst_count, 20.0F * s->dpi_scale, 20.0F * s->dpi_scale, 80.0F * s->dpi_scale,
              30.0F * s->dpi_scale, 0.8F, (float) pt.x, (float) pt.y, 6.0F * s->dpi_scale);

    // --- 3. Info Button ---
    float info_btn_x = (float) s->window_width - (100.0F * s->dpi_scale);
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    ui_badge(instances, &inst_count, info_btn_x, 20.0F * s->dpi_scale, 80.0F * s->dpi_scale, 30.0F * s->dpi_scale, 0.8F,
             s->info_open, (float) pt.x, (float) pt.y, 6.0F * s->dpi_scale);

    // --- 3.5 Zoom Level Badge UI ---
    if (s->zoom_ui_timer > 0.0F && s->zoom_level > 1.0F)
    {
        float cx = (float) s->window_width / 2.0F;
        float bx = cx - (60.0F * s->dpi_scale);
        float by = 20.0F * s->dpi_scale;
        float bw = 120.0F * s->dpi_scale;
        float bh = 30.0F * s->dpi_scale;
        int hovered = ((float) pt.x >= bx && (float) pt.x <= bx + bw && (float) pt.y >= by && (float) pt.y <= by + bh);
        if (inst_count >= MAX_INSTANCES - 16)
            return;
        ui_badge(instances, &inst_count, bx, by, bw, bh, 0.8F, hovered, (float) pt.x, (float) pt.y,
                 6.0F * s->dpi_scale);
    }

    // --- 4. Bottom Strip ---
    float strip_y = (float) s->window_height - (130.0F * s->dpi_scale);

    // Solid bottom strip panel backplate (masks zoomed overflow)
    instances[inst_count] = (InstanceData){0};
    instances[inst_count].x = 0.0F;
    instances[inst_count].y = strip_y;
    instances[inst_count].w = (float) s->window_width;
    instances[inst_count].h = 130.0F * s->dpi_scale;
    instances[inst_count].tex_index = TOKEN_PANEL; // Gray backplate
    instances[inst_count].opacity = 1.0F;          // Fully solid
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    inst_count++;

    // Previous Arrow <
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, &inst_count, 20.0F * s->dpi_scale, strip_y + (35.0F * s->dpi_scale), 30.0F * s->dpi_scale,
              30.0F * s->dpi_scale, 0.8F, (float) pt.x, (float) pt.y, 15.0F * s->dpi_scale);

    // Next Arrow >
    if (inst_count >= MAX_INSTANCES - 16)
        return;
    ui_button(instances, &inst_count, (float) s->window_width - (50.0F * s->dpi_scale),
              strip_y + (35.0F * s->dpi_scale), 30.0F * s->dpi_scale, 30.0F * s->dpi_scale, 0.8F, (float) pt.x,
              (float) pt.y, 15.0F * s->dpi_scale);

    // Bottom strip thumbnails window centered around s->selected_index
    float avail_w = (float) s->window_width - (140.0F * s->dpi_scale); // Width between arrows
    int thumb_w = (int) (80 * s->dpi_scale);
    int thumb_h = (int) (80 * s->dpi_scale);
    int thumb_pad = (int) (10 * s->dpi_scale);
    int col_w = thumb_w + thumb_pad;

    int start_idx = 0;
    int end_idx = 0;
    int active_img_idx_in_strip = -1;
    int total_images = 0;

    if (s->grid_items && s->strip_image_count > 0)
    {
        total_images = s->strip_image_count;
        for (int i = 0; i < s->strip_image_count; i++)
        {
            if (s->strip_image_grid_indices[i] == s->selected_index)
            {
                active_img_idx_in_strip = i;
                break;
            }
        }
    }
    else
    {
        total_images = s->count;
        active_img_idx_in_strip = s->selected_index;
    }

    if (total_images > 0 && active_img_idx_in_strip != -1)
    {
        int num_strip_thumbs = (int) (avail_w / (float) col_w);
        if (num_strip_thumbs < 1)
            num_strip_thumbs = 1;
        if (num_strip_thumbs > total_images)
            num_strip_thumbs = total_images;

        int half_n = num_strip_thumbs / 2;
        start_idx = active_img_idx_in_strip - half_n;
        if (start_idx < 0)
            start_idx = 0;
        end_idx = start_idx + num_strip_thumbs - 1;
        if (end_idx >= total_images)
        {
            end_idx = total_images - 1;
            start_idx = end_idx - num_strip_thumbs + 1;
            if (start_idx < 0)
                start_idx = 0;
        }

        float total_thumbs_w = (float) ((num_strip_thumbs * thumb_w) + ((num_strip_thumbs - 1) * thumb_pad));
        float thumbs_start_x = (55.0F * s->dpi_scale) + ((avail_w - total_thumbs_w) / 2.0F);

        for (int k = start_idx; k <= end_idx; k++)
        {
            int i = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[k] : k;
            int img_idx = s->grid_items ? s->grid_items[i].image_index : i;
            if (img_idx < 0 || img_idx >= s->count)
                continue;

            float tx = thumbs_start_x + (float) ((k - start_idx) * col_w);
            float ty = strip_y + (10.0F * s->dpi_scale);

            // Lazy load strip thumbnails
            if (s->images != NULL && s->images[img_idx].texture_slot == -1 && !s->images[img_idx].thumb_requested)
            {
                s->images[img_idx].thumb_requested = 1;
                aw_request_thumbnail(s, s->images[img_idx].path, THUMB_SIZE, s->hwnd);
            }

            // Draw selection border if active
            if (i == s->selected_index)
            {
                instances[inst_count] = (InstanceData){0};
                instances[inst_count].x = tx - (4.0F * s->dpi_scale);
                instances[inst_count].y = ty - (4.0F * s->dpi_scale);
                instances[inst_count].w = (float) thumb_w + (8.0F * s->dpi_scale);
                instances[inst_count].h = (float) thumb_h + (8.0F * s->dpi_scale);
                instances[inst_count].tex_index = TOKEN_ACCENT; // Accent border
                instances[inst_count].opacity = 1.0F;
                instances[inst_count].corner_radius = 8.0F * s->dpi_scale;
                if (inst_count >= MAX_INSTANCES - 16)
                    break;
                inst_count++;
            }

            // Draw thumbnail
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = tx;
            instances[inst_count].y = ty;
            instances[inst_count].w = (float) thumb_w;
            instances[inst_count].h = (float) thumb_h;
            instances[inst_count].tex_index = s->images ? s->images[img_idx].texture_slot : -1;
            instances[inst_count].opacity = 1.0F;
            instances[inst_count].corner_radius = 6.0F * s->dpi_scale;

            if (s->images && s->images[img_idx].texture_slot != -1)
            {
                s->tex_pool.last_used[s->images[img_idx].texture_slot] = s->tex_pool.frame_counter;
            }
            if (inst_count >= MAX_INSTANCES - 16)
                break;
            inst_count++;
        }
    }

    // --- 5. Info Overlay Box ---
    float info_x = (float) s->window_width - (320.0F * s->dpi_scale);
    float info_y = s->layout.topbar_height + (25.0F * s->dpi_scale);
    float info_w = 300.0F * s->dpi_scale;
    float info_h = 240.0F * s->dpi_scale;

    // Draw all background elements first
    r_draw_instances(s, instances, inst_count);
    inst_count = 0;

    if (s->info_open)
    {
        r_copy_backbuffer_for_blur(s);
        ui_blur_panel(instances, &inst_count, info_x, info_y, info_w, info_h, 0.92F, 1, s->layout.card_radius);

        // Circular close button in top right of info card
        float close_w = 20.0F * s->dpi_scale;
        float close_h = 20.0F * s->dpi_scale;
        float close_x = info_x + info_w - close_w - (10.0F * s->dpi_scale);
        float close_y = info_y + (10.0F * s->dpi_scale);
        ui_button(instances, &inst_count, close_x, close_y, close_w, close_h, 0.6F, (float) pt.x, (float) pt.y,
                  close_w * 0.5F);

        // Draw the info panel overlay elements
        r_draw_instances(s, instances, inst_count);
        inst_count = 0;
    }

    // Draw all D3D11 geometry
    r_draw_instances(s, instances, inst_count);

    // Draw Back and Info Button text
    r_draw_text_aligned(s, L"\uE72B", 20.0F * s->dpi_scale, 20.0F * s->dpi_scale, 80.0F * s->dpi_scale,
                        30.0F * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons,
                        s->theme.text_main);
    r_draw_text_aligned(s, L"\uE946", info_btn_x, 20.0F * s->dpi_scale, 80.0F * s->dpi_scale, 30.0F * s->dpi_scale,
                        ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);

    // Draw Zoom Level badge text if active
    if (s->zoom_ui_timer > 0.0F && s->zoom_level > 1.0F)
    {
        float cx = (float) s->window_width / 2.0F;
        float bx = cx - (60.0F * s->dpi_scale);
        float by = 20.0F * s->dpi_scale;
        float bw = 120.0F * s->dpi_scale;
        float bh = 30.0F * s->dpi_scale;
        POINT m_pt;
        GetCursorPos(&m_pt);
        ScreenToClient(s->hwnd, &m_pt);
        int hovered =
            ((float) m_pt.x >= bx && (float) m_pt.x <= bx + bw && (float) m_pt.y >= by && (float) m_pt.y <= by + bh);
        wchar_t zoom_text[64];
        if (hovered)
        {
            wcscpy(zoom_text, L"Reset Zoom");
        }
        else
        {
            swprintf(zoom_text, 64, L"Zoom: %.0f%%", s->zoom_level * 100.0F);
        }
        ui_badge_text(s, zoom_text, bx, by, bw, bh);
    }

    // Draw previous/next strip buttons
    r_draw_text_aligned(s, L"\uE76B", 20.0F * s->dpi_scale, strip_y + (35.0F * s->dpi_scale), 30.0F * s->dpi_scale,
                        30.0F * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons,
                        s->theme.text_main);
    r_draw_text_aligned(s, L"\uE76C", (float) s->window_width - (50.0F * s->dpi_scale),
                        strip_y + (35.0F * s->dpi_scale), 30.0F * s->dpi_scale, 30.0F * s->dpi_scale, ALIGN_X_CENTER,
                        ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);

    // Draw Metadata Card details
    if (s->info_open)
    {
        ImageEntry *e = &s->images[active_img_idx];
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

        // Fetch actual texture size if loaded in high-res D3D slot
        int actual_w = e->full_width;
        int actual_h = e->full_height;
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, e->path) : NULL;
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

        // Draw close button close icon text
        float close_w = 20.0F * s->dpi_scale;
        float close_h = 20.0F * s->dpi_scale;
        float close_x = info_x + info_w - close_w - (10.0F * s->dpi_scale);
        float close_y = info_y + (10.0F * s->dpi_scale);
        r_draw_text_aligned(s, L"\uE711", close_x, close_y, close_w, close_h, ALIGN_X_CENTER, ALIGN_Y_CENTER,
                            s->dwrite_format_icons, s->theme.text_main);

        float pad = 15.0F * s->dpi_scale;
        float item_h = 24.0F * s->dpi_scale;

        // Render line items beautiful Segoe UI Variable text
        r_draw_text_aligned(s, L"IMAGE METADATA", info_x + pad, info_y + pad, info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_semibold, s->theme.text_main);

        wchar_t line[256];
        swprintf(line, 256, L"Name:  %ls", name_trunc);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 1.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);

        swprintf(line, 256, L"Path:  %ls", path_trunc);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 2.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);

        swprintf(line, 256, L"Size:  %ls", sz_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 3.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);

        swprintf(line, 256, L"Dims:  %ls", dim_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 4.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);

        swprintf(line, 256, L"Created:  %ls", tc_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 5.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);

        swprintf(line, 256, L"Modified: %ls", tm_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + (item_h * 6.2F), info_w - (pad * 2.0F), item_h,
                            ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_mono, s->theme.text_main);
    }

    r_present(s);
    s->needs_redraw = 0;
}

int gal_handle_fullimage_click(AppState *s, int x, int y)
{
    if (s->view_mode != VIEW_FULLIMAGE)
        return 0;

    float dpi = s->dpi_scale > 0.0F ? s->dpi_scale : 1.0F;

    // --- 1. Hit test back button ---
    if ((float) x >= 20.0F * dpi && (float) x <= 100.0F * dpi && (float) y >= 20.0F * dpi && (float) y <= 50.0F * dpi)
    {
        gal_close_full(s);
        return 1;
    }

    // --- 2. Hit test info button ---
    if ((float) x >= (float) s->window_width - (100.0F * dpi) && (float) x <= (float) s->window_width - (20.0F * dpi) &&
        (float) y >= 20.0F * dpi && (float) y <= 50.0F * dpi)
    {
        s->info_open = !s->info_open;
        s->needs_redraw = 1;
        return 1;
    }

    // --- 2.5 Zoom badge hit test ---
    if (s->zoom_ui_timer > 0.0F && s->zoom_level > 1.0F)
    {
        float cx = (float) s->window_width / 2.0F;
        float bx = cx - (60.0F * dpi);
        float by = 20.0F * dpi;
        float bw = 120.0F * dpi;
        float bh = 30.0F * dpi;
        if ((float) x >= bx && (float) x <= bx + bw && (float) y >= by && (float) y <= by + bh)
        {
            s->zoom_level = 1.0F;
            s->zoom_ui_timer = 0.0F;
            gal_clamp_zoom_pan(s);
            s->needs_redraw = 1;
            return 1;
        }
    }

    float info_x = (float) s->window_width - (320.0F * dpi);
    float info_y = s->layout.topbar_height + (25.0F * dpi);
    float info_w = 300.0F * dpi;
    float info_h = 240.0F * dpi;

    // --- 2.6 Hit test metadata close button ---
    if (s->info_open)
    {
        float close_w = 20.0F * dpi;
        float close_h = 20.0F * dpi;
        float close_x = info_x + info_w - close_w - (10.0F * dpi);
        float close_y = info_y + (10.0F * dpi);
        if ((float) x >= close_x && (float) x <= close_x + close_w && (float) y >= close_y &&
            (float) y <= close_y + close_h)
        {
            s->info_open = 0;
            s->needs_redraw = 1;
            return 1;
        }
    }

    // --- 3. Click inside Info Box & Click Outside handling ---
    int closed_info = 0;
    if (s->info_open)
    {
        if ((float) x >= info_x && (float) x <= info_x + info_w && (float) y >= info_y && (float) y <= info_y + info_h)
        {
            return 1; // Click inside info box -> consume click
        }
        // Click was outside info box -> close it
        s->info_open = 0;
        s->needs_redraw = 1;
        closed_info = 1;
    }

    // --- 4. Bottom Navigation Strip ---
    float strip_y = (float) s->window_height - (130.0F * dpi);
    float strip_h = 130.0F * dpi;

    if ((float) y >= strip_y && (float) y <= strip_y + strip_h)
    {
        int active_img_idx_in_strip = -1;
        int total_images = 0;

        if (s->grid_items && s->strip_image_count > 0)
        {
            total_images = s->strip_image_count;
            for (int i = 0; i < s->strip_image_count; i++)
            {
                if (s->strip_image_grid_indices[i] == s->selected_index)
                {
                    active_img_idx_in_strip = i;
                    break;
                }
            }
        }
        else
        {
            total_images = s->count;
            active_img_idx_in_strip = s->selected_index;
        }

        // Prev Arrow circular button hit test
        if ((float) x >= 20.0F * dpi && (float) x <= 50.0F * dpi && (float) y >= strip_y + (35.0F * dpi) &&
            (float) y <= strip_y + (65.0F * dpi))
        {
            if (active_img_idx_in_strip > 0)
            {
                int new_grid_idx = (s->grid_items && s->strip_image_count > 0) ?
                                       s->strip_image_grid_indices[active_img_idx_in_strip - 1] :
                                       active_img_idx_in_strip - 1;
                gal_select_full_image(s, new_grid_idx);
            }
            return 1;
        }

        // Next Arrow circular button hit test
        if ((float) x >= (float) s->window_width - (50.0F * dpi) &&
            (float) x <= (float) s->window_width - (20.0F * dpi) && (float) y >= strip_y + (35.0F * dpi) &&
            (float) y <= strip_y + (65.0F * dpi))
        {
            if (active_img_idx_in_strip >= 0 && active_img_idx_in_strip < total_images - 1)
            {
                int new_grid_idx = (s->grid_items && s->strip_image_count > 0) ?
                                       s->strip_image_grid_indices[active_img_idx_in_strip + 1] :
                                       active_img_idx_in_strip + 1;
                gal_select_full_image(s, new_grid_idx);
            }
            return 1;
        }

        // Individual thumbnail hit testing in the strip
        float avail_w = (float) s->window_width - (140.0F * dpi);
        int thumb_w = (int) (80 * dpi);
        int thumb_pad = (int) (10 * dpi);
        int col_w = thumb_w + thumb_pad;

        if (total_images > 0 && active_img_idx_in_strip != -1)
        {
            int num_strip_thumbs = (int) (avail_w / (float) col_w);
            if (num_strip_thumbs < 1)
                num_strip_thumbs = 1;
            if (num_strip_thumbs > total_images)
                num_strip_thumbs = total_images;

            int half_n = num_strip_thumbs / 2;
            int start_idx = active_img_idx_in_strip - half_n;
            if (start_idx < 0)
                start_idx = 0;
            int end_idx = start_idx + num_strip_thumbs - 1;
            if (end_idx >= total_images)
            {
                end_idx = total_images - 1;
                start_idx = end_idx - num_strip_thumbs + 1;
                if (start_idx < 0)
                    start_idx = 0;
            }

            float total_thumbs_w = (float) ((num_strip_thumbs * thumb_w) + ((num_strip_thumbs - 1) * thumb_pad));
            float thumbs_start_x = (55.0F * dpi) + ((avail_w - total_thumbs_w) / 2.0F);

            for (int k = start_idx; k <= end_idx; k++)
            {
                int i = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[k] : k;
                float tx = thumbs_start_x + (float) ((k - start_idx) * col_w);
                float ty = strip_y + (10.0F * dpi);

                if ((float) x >= tx && (float) x <= tx + (float) thumb_w && (float) y >= ty &&
                    (float) y <= ty + (float) thumb_w)
                {
                    if (s->selected_index != i)
                    {
                        gal_select_full_image(s, i);
                    }
                    return 1;
                }
            }
        }
    }

    return closed_info;
}
