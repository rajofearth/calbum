// =========================================================================
// gallery.c — Gallery grid using D3D11 instanced rendering
// =========================================================================
#include "types.h"
#include "../lib/ui/ui.h"
#include <d2d1.h>
#include <math.h>

void gal_update_layout_scales(AppState *s)
{
    if (s->ui.dpi_scale <= 0.0F)
        s->ui.dpi_scale = 1.0F;

    s->ui.layout.grid_gap = 12.0F * s->ui.dpi_scale;
    s->ui.layout.panel_padding = 12.0F * s->ui.dpi_scale;
    s->ui.layout.thumb_radius = 4.0F * s->ui.dpi_scale;
    s->ui.layout.card_radius = 6.0F * s->ui.dpi_scale;
    s->ui.layout.button_height = 26.0F * s->ui.dpi_scale;
    s->ui.layout.topbar_height = 38.0F * s->ui.dpi_scale;
    s->ui.layout.scrollbar_w = 6.0F * s->ui.dpi_scale;
}

static void gal_apply_sort_option(AppState *s, int cmd)
{
    if (cmd >= 1 && cmd <= 3)
    {
        if (cmd == 1)
            s->view.sort_mode = SORT_DATE_CREATED;
        if (cmd == 2)
            s->view.sort_mode = SORT_DATE_MODIFIED;
        if (cmd == 3)
            s->view.sort_mode = SORT_SIZE;
        gal_apply_sort(s);
    }
    else if (cmd == 4 || cmd == 5)
    {
        s->view.sort_descending = (cmd == 5) ? 1 : 0;
        gal_apply_sort(s);
    }
}

int gal_handle_ui_click(AppState *s, int x, int y)
{
    if (s->view.view_mode != VIEW_GALLERY)
        return 0;

    float dpi = s->ui.dpi_scale > 0.0F ? s->ui.dpi_scale : 1.0F;
    int btn_w = (int) (90 * dpi);
    int btn_h = (int) (s->ui.layout.button_height);
    int btn_x = (int) ((float) s->window_width - (float) btn_w - (20.0F * dpi));
    int btn_y = (int) (10 * dpi);

    if (s->ui.sort_menu_open)
    {
        int menu_w = (int) (160 * dpi);
        int menu_h = 5 * (int) (30 * dpi);
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + (int) (5 * dpi);

        if (x >= menu_x && x <= menu_x + menu_w && y >= menu_y && y <= menu_y + menu_h)
        {
            int row = (y - menu_y) / (int) (30 * dpi);
            gal_apply_sort_option(s, row + 1);
        }
        s->ui.sort_menu_open = 0;
        s->needs_redraw = 1;
        return 1;
    }

    // Sort Button hit test
    if (x >= btn_x && x <= btn_x + btn_w && y >= btn_y && y <= btn_y + btn_h)
    {
        s->ui.sort_menu_open = 1;
        s->needs_redraw = 1;
        return 1;
    }

    // Allow clicking thumbnails behind transparent area
    return 0;
}

void gal_update_layout(AppState *s)
{
    int ms = gal_max_scroll(s);
    if (s->view.scroll_target_y > (float) ms)
        s->view.scroll_target_y = (float) ms;
    if (s->view.scroll_current_y > (float) ms)
        s->view.scroll_current_y = (float) ms;
    if (s->view.view_mode == VIEW_FULLIMAGE)
    {
        gal_clamp_zoom_pan(s);
    }
    s->needs_redraw = 1;
}

void gal_tick_smooth_scroll(AppState *s)
{
    if (s->view.view_mode != VIEW_GALLERY)
        return;
    float diff = s->view.scroll_target_y - s->view.scroll_current_y;
    if (diff > -0.5F && diff < 0.5F)
    {
        s->view.scroll_current_y = s->view.scroll_target_y;
        return;
    }
    float factor = ease_out_factor(SMOOTH_SCROLL_SPEED, (float) s->delta_time);
    s->view.scroll_current_y += diff * factor;
    s->needs_redraw = 1;
}

void gal_scroll(AppState *s, float delta)
{
    s->view.scroll_target_y -= delta;
    int ms = gal_max_scroll(s);
    if (s->view.scroll_target_y < 0.0F)
        s->view.scroll_target_y = 0.0F;
    if (s->view.scroll_target_y > (float) ms)
        s->view.scroll_target_y = (float) ms;
    s->needs_redraw = 1;
}

static void format_folder_contents(int image_count, int folder_count, wchar_t *buf, int len)
{
    buf[0] = L'\0';
    if (image_count > 0 && folder_count > 0)
    {
        swprintf(buf, len, L"%d image%s \u2022 %d folder%s", image_count, image_count == 1 ? L"" : L"s", folder_count,
                 folder_count == 1 ? L"" : L"s");
    }
    else if (image_count > 0)
    {
        swprintf(buf, len, L"%d image%s", image_count, image_count == 1 ? L"" : L"s");
    }
    else if (folder_count > 0)
    {
        swprintf(buf, len, L"%d folder%s", folder_count, folder_count == 1 ? L"" : L"s");
    }
}

// ── Gallery render helpers ────────────────────────────────────────────────

// Builds InstanceData for visible thumbnails/folders in the grid
static void gal_render_grid_thumbnails(AppState *s, InstanceData *instances, int *inst_count, const GridLayout *lay,
                                       POINT pt, float thumb_size)
{
    for (int i = lay->first_visible; i < lay->last_visible; i++)
    {
        int row = i / lay->cols;
        int col = i % lay->cols;
        float x = (float) (lay->left_margin + (col * lay->pad));
        float y = s->ui.layout.topbar_height + s->ui.layout.panel_padding + (float) (row * lay->pad) -
                  (float) lay->scroll_int;

        if (y + thumb_size < 0 || y > (float) s->window_height)
            continue;

        int hovered = ui_is_hovered(x, y, thumb_size, thumb_size, (float) pt.x, (float) pt.y);

        GridItemType type = s->data.grid_items ? s->data.grid_items[i].type : ITEM_IMAGE;
        int img_idx = s->data.grid_items ? s->data.grid_items[i].image_index : i;

        // Subtle border behind the card (drawn first so image/panel covers the center)
        float border_opacity;
        if (s->view.selected_index == i)
        {
            border_opacity = 0.0F;
        }
        else
        {
            border_opacity = hovered ? 0.8F : 0.3F;
        }

        if (type == ITEM_FOLDER)
        {
            if (s->view.selected_index == i)
            {
                border_opacity = 0.0F;
            }
            else
            {
                border_opacity = hovered ? 0.8F : 0.4F;
            }

            // Draw border behind
            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = x - (1.0F * s->ui.dpi_scale);
            instances[*inst_count].y = y - (1.0F * s->ui.dpi_scale);
            instances[*inst_count].w = thumb_size + (2.0F * s->ui.dpi_scale);
            instances[*inst_count].h = thumb_size + (2.0F * s->ui.dpi_scale);
            instances[*inst_count].tex_index = TOKEN_BORDER;
            instances[*inst_count].opacity = border_opacity;
            instances[*inst_count].corner_radius = s->ui.layout.thumb_radius + (1.0F * s->ui.dpi_scale);
            (*inst_count)++;

            // Draw main folder backplate card on top
            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = x;
            instances[*inst_count].y = y;
            instances[*inst_count].w = thumb_size;
            instances[*inst_count].h = thumb_size;
            instances[*inst_count].tex_index = TOKEN_PANEL;
            instances[*inst_count].opacity = (hovered || s->view.selected_index == i) ? 1.0F : 0.75F;
            instances[*inst_count].corner_radius = s->ui.layout.thumb_radius;
            (*inst_count)++;
        }
        else
        {
            // Draw main image thumbnail
            if (img_idx >= 0 && img_idx < s->data.count)
            {
                if (s->data.images[img_idx].texture_slot == -1 && !s->data.images[img_idx].thumb_requested)
                {
                    s->data.images[img_idx].thumb_requested = 1;
                    if (!aw_request_thumbnail(s, s->data.images[img_idx].path, THUMB_SIZE, s->hwnd))
                    {
                        s->data.images[img_idx].thumb_requested = 0; // allow retry next frame
                    }
                }

                // Draw border behind
                instances[*inst_count] = (InstanceData){0};
                instances[*inst_count].x = x - (1.0F * s->ui.dpi_scale);
                instances[*inst_count].y = y - (1.0F * s->ui.dpi_scale);
                instances[*inst_count].w = thumb_size + (2.0F * s->ui.dpi_scale);
                instances[*inst_count].h = thumb_size + (2.0F * s->ui.dpi_scale);
                instances[*inst_count].tex_index = TOKEN_BORDER;
                instances[*inst_count].opacity = border_opacity;
                instances[*inst_count].corner_radius = s->ui.layout.thumb_radius + (1.0F * s->ui.dpi_scale);
                (*inst_count)++;

                int tex = TOKEN_DEFAULT;
                float load_opacity = 1.0F;

                if (s->data.images[img_idx].state == IMG_STATE_RESIDENT_GPU &&
                    s->data.images[img_idx].texture_slot != -1)
                {
                    tex = s->data.images[img_idx].texture_slot;
                }
                else
                {
                    // Subtle pulse for non-resident thumbnails so user can visually
                    // distinguish "loading" from "blank"
                    float pulse = 0.7F + (0.3F * (float) sin((double) s->gpu.tex_pool.frame_counter * 0.15));
                    load_opacity = pulse;
                }

                instances[*inst_count] = (InstanceData){0};
                instances[*inst_count].x = x;
                instances[*inst_count].y = y;
                instances[*inst_count].w = thumb_size;
                instances[*inst_count].h = thumb_size;
                instances[*inst_count].tex_index = tex;
                instances[*inst_count].opacity = load_opacity;
                instances[*inst_count].corner_radius = s->ui.layout.thumb_radius;

                if (s->data.images[img_idx].state == IMG_STATE_RESIDENT_GPU &&
                    s->data.images[img_idx].texture_slot != -1)
                {
                    s->gpu.tex_pool.last_used[s->data.images[img_idx].texture_slot] = s->gpu.tex_pool.frame_counter;
                }
                (*inst_count)++;

                // Warning icon overlay for FAILED state
                // (must be inside BeginDraw/EndDraw — D2D calls outside are silently dropped
                //  and put the render target into an error state, breaking all subsequent D2D)
                if (s->data.images[img_idx].state == IMG_STATE_FAILED)
                {
                    s->txt.d2d_rtv->lpVtbl->BeginDraw(s->txt.d2d_rtv);
                    r_draw_text_aligned(s, L"\u26A0", x + thumb_size - (16.0F * s->ui.dpi_scale),
                                        y + thumb_size - (16.0F * s->ui.dpi_scale), 16.0F * s->ui.dpi_scale,
                                        16.0F * s->ui.dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER,
                                        s->txt.dwrite_format_icons, s->ui.theme.accent);
                    s->txt.d2d_rtv->lpVtbl->EndDraw(s->txt.d2d_rtv, NULL, NULL);
                }
            }
        }

        // Selection Outline: draw a glowing accent color outline on top of the item
        if (s->view.selected_index == i)
        {
            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = x;
            instances[*inst_count].y = y;
            instances[*inst_count].w = thumb_size;
            instances[*inst_count].h = thumb_size;
            instances[*inst_count].tex_index = TOKEN_SELECTION_OUTLINE; // Accent outline token
            instances[*inst_count].opacity = 1.0F;
            instances[*inst_count].corner_radius = s->ui.layout.thumb_radius;
            (*inst_count)++;
        }

        if (*inst_count >= MAX_INSTANCES - 16)
            break; // Leave room for scrollbar, topbar and buttons
    }
}

// Scrollbar fade animation + track/thumb rendering
static void gal_render_scrollbar(AppState *s, InstanceData *instances, int *inst_count, POINT pt, int ms)
{
    if (ms > 0)
    {
        if (fabsf(s->view.scroll_target_y - s->view.scroll_current_y) > 0.5F)
        {
            s->ui.scrollbar_fade_timer = 2.0F;
        }

        float hit_x = (float) s->window_width - (16.0F * s->ui.dpi_scale);
        int scrollbar_hovered =
            ui_is_hovered(hit_x, 0.0F, 16.0F * s->ui.dpi_scale, (float) s->window_height, (float) pt.x, (float) pt.y);
        if (scrollbar_hovered)
        {
            s->ui.scrollbar_fade_timer = 2.0F;
        }

        if (s->ui.scrollbar_fade_timer > 0.0F)
        {
            s->ui.scrollbar_fade_timer -= (float) s->delta_time;
        }

        float target_opacity = (s->ui.scrollbar_fade_timer > 0.0F) ? 1.0F : 0.0F;
        s->ui.scrollbar_opacity +=
            (target_opacity - s->ui.scrollbar_opacity) * ease_out_factor(10.0F, (float) s->delta_time);

        float target_hover_t = scrollbar_hovered ? 1.0F : 0.0F;
        s->ui.scrollbar_hover_t +=
            (target_hover_t - s->ui.scrollbar_hover_t) * ease_out_factor(12.0F, (float) s->delta_time);

        if (s->ui.scrollbar_opacity > 0.01F)
        {
            float track_w = lerpf(6.0F, 10.0F, s->ui.scrollbar_hover_t) * s->ui.dpi_scale;
            float track_x = (float) s->window_width - track_w - (4.0F * s->ui.dpi_scale);
            float track_y = 8.0F * s->ui.dpi_scale;
            float track_h = (float) s->window_height - (16.0F * s->ui.dpi_scale);

            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = track_x;
            instances[*inst_count].y = track_y;
            instances[*inst_count].w = track_w;
            instances[*inst_count].h = track_h;
            instances[*inst_count].tex_index = TOKEN_PANEL;
            instances[*inst_count].opacity = 0.15F * s->ui.scrollbar_opacity;
            instances[*inst_count].corner_radius = track_w * 0.5F;
            (*inst_count)++;

            float thumb_h = ((float) s->window_height / (float) (ms + s->window_height)) * track_h;
            if (thumb_h < 24.0F * s->ui.dpi_scale)
                thumb_h = 24.0F * s->ui.dpi_scale;
            float thumb_y = track_y + ((s->view.scroll_current_y / (float) ms) * (track_h - thumb_h));

            instances[*inst_count] = (InstanceData){0};
            instances[*inst_count].x = track_x;
            instances[*inst_count].y = thumb_y;
            instances[*inst_count].w = track_w;
            instances[*inst_count].h = thumb_h;
            instances[*inst_count].tex_index = TOKEN_SCROLLBAR;
            instances[*inst_count].opacity = s->ui.scrollbar_opacity * (scrollbar_hovered ? 1.0F : 0.7F);
            instances[*inst_count].corner_radius = track_w * 0.5F;
            (*inst_count)++;
        }
    }
}

// Top bar backdrop + border
static void gal_render_topbar(AppState *s, InstanceData *instances, int *inst_count)
{
    // Top Bar Backdrop (drawn after grid items to mask scrolled overflow)
    instances[*inst_count] = (InstanceData){0};
    instances[*inst_count].x = 0.0F;
    instances[*inst_count].y = 0.0F;
    instances[*inst_count].w = (float) s->window_width;
    instances[*inst_count].h = s->ui.layout.topbar_height;
    instances[*inst_count].tex_index = TOKEN_PANEL; // One Dark panel color
    instances[*inst_count].opacity = 1.0F;
    instances[*inst_count].corner_radius = 0.0F;
    (*inst_count)++;

    // Top Bar Bottom Border
    instances[*inst_count] = (InstanceData){0};
    instances[*inst_count].x = 0.0F;
    instances[*inst_count].y = s->ui.layout.topbar_height - (1.0F * s->ui.dpi_scale);
    instances[*inst_count].w = (float) s->window_width;
    instances[*inst_count].h = 1.0F * s->ui.dpi_scale;
    instances[*inst_count].tex_index = TOKEN_BORDER; // One Dark border color
    instances[*inst_count].opacity = 1.0F;
    instances[*inst_count].corner_radius = 0.0F;
    (*inst_count)++;
}

// Sort button UI + dropdown (InstanceData only, draws + resets inst_count)
static void gal_render_sort_menu(AppState *s, InstanceData *instances, int *inst_count, POINT pt, int btn_x, int btn_y,
                                 int btn_w, int btn_h)
{
    ui_button(instances, inst_count, (float) btn_x, (float) btn_y, (float) btn_w, (float) btn_h, 0.0F, (float) pt.x,
              (float) pt.y, 4.0F * s->ui.dpi_scale);

    // Draw the main gallery, topbar, and button first
    r_draw_instances(s, instances, *inst_count);
    *inst_count = 0;

    if (s->ui.sort_menu_open)
    {
        r_copy_backbuffer_for_blur(s);

        int menu_w = (int) (160 * s->ui.dpi_scale);
        int menu_h = 5 * (int) (30 * s->ui.dpi_scale);
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + (int) (5 * s->ui.dpi_scale);

        ui_blur_panel(instances, inst_count, (float) menu_x, (float) menu_y, (float) menu_w, (float) menu_h, 0.92F, 1,
                      s->ui.layout.card_radius);
        r_draw_instances(s, instances, *inst_count);
    }
}

// D2D text pass: folder labels, breadcrumb, sort button text, and sort menu items
static void gal_render_folder_text(AppState *s, const GridLayout *lay, float thumb_size, int btn_x, int btn_y,
                                   int btn_w, int btn_h)
{
    // Render Folder text & icons in D2D pass
    if (s->data.grid_items)
    {
        for (int i = lay->first_visible; i < lay->last_visible; i++)
        {
            if (s->data.grid_items[i].type == ITEM_FOLDER)
            {
                int row = i / lay->cols;
                int col = i % lay->cols;
                float x = (float) (lay->left_margin + (col * lay->pad));
                float y = s->ui.layout.topbar_height + s->ui.layout.panel_padding + (float) (row * lay->pad) -
                          (float) lay->scroll_int;

                if (y + thumb_size < 0 || y > (float) s->window_height)
                    continue;

                int show_counts = (_wcsicmp(s->data.grid_items[i].folder_name, L"..") != 0 &&
                                   (s->data.grid_items[i].image_count > 0 || s->data.grid_items[i].folder_count > 0));

                // 1. Draw folder icon (top-left, small 24dp size)
                const wchar_t *icon = (_wcsicmp(s->data.grid_items[i].folder_name, L"..") == 0) ? L"\uEB52" : L"\uE8B7";
                float icon_x = x + (16.0F * s->ui.dpi_scale);
                float icon_y = y + (16.0F * s->ui.dpi_scale);
                float icon_size = 24.0F * s->ui.dpi_scale;
                r_draw_text_aligned(s, icon, icon_x, icon_y, icon_size, icon_size, ALIGN_X_LEFT, ALIGN_Y_CENTER,
                                    s->txt.dwrite_format_icons, s->ui.theme.accent);

                // 2. Draw folder counts (Option B) using monospace font at the bottom-left
                if (show_counts)
                {
                    wchar_t count_buf[128];
                    format_folder_contents(s->data.grid_items[i].image_count, s->data.grid_items[i].folder_count,
                                           count_buf, 128);

                    float count_x = x + (16.0F * s->ui.dpi_scale);
                    float count_y = y + thumb_size - (30.0F * s->ui.dpi_scale);
                    float count_w = thumb_size - (32.0F * s->ui.dpi_scale);
                    float count_h = 14.0F * s->ui.dpi_scale;

                    r_draw_text_aligned(s, count_buf, count_x, count_y, count_w, count_h, ALIGN_X_LEFT, ALIGN_Y_BOTTOM,
                                        s->txt.dwrite_format_mono_small, s->ui.theme.text_muted);
                }

                // 3. Draw folder name (in the middle, left-aligned)
                float name_x = x + (16.0F * s->ui.dpi_scale);
                float name_y = y + (52.0F * s->ui.dpi_scale);
                float name_w = thumb_size - (32.0F * s->ui.dpi_scale);
                float name_h = 44.0F * s->ui.dpi_scale; // wrapping space
                r_draw_text_aligned(s, s->data.grid_items[i].folder_name, name_x, name_y, name_w, name_h, ALIGN_X_LEFT,
                                    ALIGN_Y_TOP, s->txt.dwrite_format_semibold, s->ui.theme.text_main);
            }
        }
    }

    // Render breadcrumb / viewing path in D2D pass (memoized to avoid per-frame COM overhead)
    float text_x = 16.0F * s->ui.dpi_scale;
    float text_h = 16.0F * s->ui.dpi_scale;
    float text_y = (s->ui.layout.topbar_height - text_h) / 2.0F; // vertically centered

    wchar_t temp_dir[MAX_PATH_LEN];
    wcsncpy(temp_dir, s->data.viewing_dir, MAX_PATH_LEN - 1);
    temp_dir[MAX_PATH_LEN - 1] = L'\0';
    int len = (int) wcslen(temp_dir);
    if (len > 3 && temp_dir[len - 1] == L'\\')
    {
        temp_dir[len - 1] = L'\0';
    }

    wchar_t *last_sep = wcsrchr(temp_dir, L'\\');
    wchar_t parent_path[MAX_PATH_LEN] = {0};
    wchar_t child_dir[MAX_PATH_LEN] = {0};

    if (last_sep && (last_sep - temp_dir > 0) && *(last_sep + 1) != L'\0')
    {
        wcsncpy(parent_path, temp_dir, last_sep - temp_dir);
        wcscpy(child_dir, last_sep + 1);
    }
    else
    {
        wcscpy(child_dir, temp_dir);
    }

    static wchar_t cached_dir[MAX_PATH_LEN] = {0};
    static float cached_parent_w = 0.0F;
    static float cached_dpi = 0.0F;
    static wchar_t cached_display_parent[MAX_PATH_LEN * 2] = {0};

    if (wcscmp(cached_dir, s->data.viewing_dir) != 0 || cached_dpi != s->ui.dpi_scale)
    {
        wcsncpy(cached_dir, s->data.viewing_dir, MAX_PATH_LEN - 1);
        cached_dpi = s->ui.dpi_scale;

        wchar_t parent_formatted[(MAX_PATH_LEN * 2)] = {0};
        int pf_idx = 0;
        for (int p = 0; parent_path[p] != L'\0' && pf_idx < (MAX_PATH_LEN * 2) - 10; p++)
        {
            if (parent_path[p] == L'\\')
            {
                wcscpy(parent_formatted + pf_idx, L" / ");
                pf_idx += 3;
            }
            else
            {
                parent_formatted[pf_idx++] = parent_path[p];
            }
        }
        parent_formatted[pf_idx] = L'\0';

        wchar_t display_parent[(MAX_PATH_LEN * 2) + 32] = {0};
        if (parent_path[0] != L'\0')
        {
            swprintf(display_parent, (MAX_PATH_LEN * 2) + 32, L"calbum / %ls / ", parent_formatted);
        }
        else
        {
            swprintf(display_parent, (MAX_PATH_LEN * 2) + 32, L"calbum / ");
        }

        // DWrite GetMetrics workaround: append a dummy character 'x' to measure trailing space width
        wchar_t measure_buf[(MAX_PATH_LEN * 2) + 64];
        swprintf(measure_buf, sizeof(measure_buf) / sizeof(wchar_t), L"%lsx", display_parent);

        float total_w = r_measure_text_width(s, measure_buf, s->txt.dwrite_format_small);
        float char_w = r_measure_text_width(s, L"x", s->txt.dwrite_format_small);
        cached_parent_w = total_w - char_w;

        wcsncpy(cached_display_parent, display_parent, (MAX_PATH_LEN * 2) - 1);
    }

    // Draw parent path segment in muted color using system small font (from cache)
    r_draw_text_aligned(s, cached_display_parent, text_x, text_y, cached_parent_w + 5.0F, text_h, ALIGN_X_LEFT,
                        ALIGN_Y_CENTER, s->txt.dwrite_format_small, s->ui.theme.text_muted);

    // Draw active folder name in main color
    float child_max_w = (float) btn_x - text_x - cached_parent_w - 10.0F;
    if (child_max_w > 10.0F)
    {
        r_draw_text_aligned(s, child_dir, text_x + cached_parent_w, text_y, child_max_w, text_h, ALIGN_X_LEFT,
                            ALIGN_Y_CENTER, s->txt.dwrite_format_small, s->ui.theme.text_main);
    }

    ui_button_text(s, L"Sort \x25BC", (float) btn_x, (float) btn_y, (float) btn_w, (float) btn_h);

    if (s->ui.sort_menu_open)
    {
        int menu_w = (int) (160 * s->ui.dpi_scale);
        int menu_x = btn_x + btn_w - menu_w;
        int menu_y = btn_y + btn_h + (int) (5 * s->ui.dpi_scale);

        const wchar_t *opts[] = {s->view.sort_mode == SORT_DATE_CREATED ? L"\u2713 Date created" : L"  Date created",
                                 s->view.sort_mode == SORT_DATE_MODIFIED ? L"\u2713 Date modified" : L"  Date modified",
                                 s->view.sort_mode == SORT_SIZE ? L"\u2713 Size" : L"  Size",
                                 !s->view.sort_descending ? L"\u2713 Ascending" : L"  Ascending",
                                 s->view.sort_descending ? L"\u2713 Descending" : L"  Descending"};

        float option_h = 30.0F * s->ui.dpi_scale;
        for (int i = 0; i < 5; i++)
        {
            r_draw_text_ext(s, opts[i], (float) menu_x + (12.0F * s->ui.dpi_scale),
                            (float) menu_y + (5.0F * s->ui.dpi_scale) + ((float) i * option_h), (float) menu_w,
                            option_h, s->txt.dwrite_format_regular, s->ui.theme.text_main);
        }
    }
}

void gal_render_gallery(HDC hdc, AppState *s)
{
    (void) hdc;
    r_clear_theme(s);

    int total_items = s->data.grid_items ? s->data.grid_item_count : s->data.count;
    if (total_items == 0)
    {
        if (s->worker.scanning)
        {
            float muted[4] = {0.663F, 0.686F, 0.737F, 1.0F};
            s->txt.d2d_rtv->lpVtbl->BeginDraw(s->txt.d2d_rtv);
            r_draw_text_aligned(s, L"Scanning\u2026", 0, 0, (float) s->window_width, (float) s->window_height,
                                ALIGN_X_CENTER, ALIGN_Y_CENTER, s->txt.dwrite_format, muted);
            s->txt.d2d_rtv->lpVtbl->EndDraw(s->txt.d2d_rtv, NULL, NULL);
            r_present(s);
            return;
        }
        // Empty-folder guidance — only render if D3D/D2D is initialized
        // (first WM_PAINT can arrive before r_init via ShowWindow/UpdateWindow)
        if (s->gpu.d3d_context)
        {
            static InstanceData instances2[MAX_INSTANCES];
            int inst_count2 = 0;
            gal_render_topbar(s, instances2, &inst_count2);
            r_clear_theme(s);
            r_draw_instances(s, instances2, inst_count2);
            s->txt.d2d_rtv->lpVtbl->BeginDraw(s->txt.d2d_rtv);
            r_draw_text_aligned(s, L"No images here \u2014 drop a folder to browse",
                                ((float) s->window_width * 0.5F) - 200.0F, ((float) s->window_height * 0.5F) - 20.0F,
                                400.0F, 40.0F, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->txt.dwrite_format_semibold,
                                s->ui.theme.text_muted);
            s->txt.d2d_rtv->lpVtbl->EndDraw(s->txt.d2d_rtv, NULL, NULL);
        }
        r_present(s);
        return;
    }

    GridLayout lay;
    gal_calc_layout(s, &lay);

    static InstanceData instances[MAX_INSTANCES];
    int inst_count = 0;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(s->hwnd, &pt);

    float thumb_size = 160.0F * s->ui.dpi_scale;

    gal_render_grid_thumbnails(s, instances, &inst_count, &lay, pt, thumb_size);

    int ms = gal_max_scroll(s);
    gal_render_scrollbar(s, instances, &inst_count, pt, ms);

    gal_render_topbar(s, instances, &inst_count);

    int btn_w = (int) (90 * s->ui.dpi_scale);
    int btn_h = (int) (s->ui.layout.button_height);
    int btn_x = (int) ((float) s->window_width - (float) btn_w - (16.0F * s->ui.dpi_scale));
    int btn_y = (int) ((s->ui.layout.topbar_height - (float) btn_h) / 2.0F);

    gal_render_sort_menu(s, instances, &inst_count, pt, btn_x, btn_y, btn_w, btn_h);

    // Begin D2D render batch (folder text + breadcrumb + sort text)
    s->txt.d2d_rtv->lpVtbl->BeginDraw(s->txt.d2d_rtv);

    gal_render_folder_text(s, &lay, thumb_size, btn_x, btn_y, btn_w, btn_h);

    s->txt.d2d_rtv->lpVtbl->EndDraw(s->txt.d2d_rtv, NULL, NULL);

    r_present(s);
    s->needs_redraw = 0;
}

void gal_activate_item(AppState *s, int idx)
{
    int limit = s->data.grid_items ? s->data.grid_item_count : s->data.count;
    if (idx < 0 || idx >= limit)
        return;

    if (s->data.grid_items && s->data.grid_items[idx].type == ITEM_FOLDER)
    {
        if (!s->data.grid_items[idx].folder_path)
            return;
        wcsncpy(s->data.viewing_dir, s->data.grid_items[idx].folder_path, MAX_PATH_LEN - 1);
        s->data.viewing_dir[MAX_PATH_LEN - 1] = L'\0';
        app_populate_grid_items(s);
        s->view.scroll_target_y = s->view.scroll_current_y = 0.0F;
        s->view.selected_index = 0;
        gal_update_layout(s);
        app_update_title(s);
        s->needs_redraw = 1;
    }
    else
    {
        gal_open_full(s, idx);
    }
}
