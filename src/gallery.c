// =========================================================================
// gallery.c — Gallery grid using D3D11 instanced rendering
// =========================================================================
#include "types.h"
#include "ui.h"
#include "layout.h"
#include "utils.h"
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

static int cmp_date_created(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->created_time < eb->created_time) return -1;
    if (ea->created_time > eb->created_time) return 1;
    return _wcsicmp(ea->path, eb->path);
}

static int cmp_date_modified(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->last_modified < eb->last_modified) return -1;
    if (ea->last_modified > eb->last_modified) return 1;
    return _wcsicmp(ea->path, eb->path);
}

static int cmp_size(const void *a, const void *b) {
    ImageEntry *ea = (ImageEntry*)a; ImageEntry *eb = (ImageEntry*)b;
    if (ea->file_size < eb->file_size) return -1;
    if (ea->file_size > eb->file_size) return 1;
    return _wcsicmp(ea->path, eb->path);
}

void gal_apply_sort(AppState *s)
{
    if (s->count == 0) return;
    
    // Save currently selected path
    wchar_t selected_path[MAX_PATH_LEN] = {0};
    if (s->selected_index >= 0) {
        int limit = s->grid_items ? s->grid_item_count : s->count;
        if (s->selected_index < limit) {
            if (s->grid_items) {
                if (s->grid_items[s->selected_index].type == ITEM_FOLDER) {
                    if (s->grid_items[s->selected_index].folder_path) {
                        wcsncpy(selected_path, s->grid_items[s->selected_index].folder_path, MAX_PATH_LEN - 1);
                    }
                } else {
                    int img_idx = s->grid_items[s->selected_index].image_index;
                    if (img_idx >= 0 && img_idx < s->count) {
                        wcsncpy(selected_path, s->images[img_idx].path, MAX_PATH_LEN - 1);
                    }
                }
            } else {
                wcsncpy(selected_path, s->images[s->selected_index].path, MAX_PATH_LEN - 1);
            }
        }
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
    
    if (s->grid_items) {
        app_populate_grid_items(s);
    }
    
    // Restore selection
    if (selected_path[0]) {
        if (s->grid_items) {
            for (int i = 0; i < s->grid_item_count; i++) {
                if (s->grid_items[i].type == ITEM_FOLDER) {
                    if (s->grid_items[i].folder_path && _wcsicmp(s->grid_items[i].folder_path, selected_path) == 0) {
                        s->selected_index = i;
                        break;
                    }
                } else {
                    int img_idx = s->grid_items[i].image_index;
                    if (img_idx >= 0 && img_idx < s->count) {
                        if (_wcsicmp(s->images[img_idx].path, selected_path) == 0) {
                            s->selected_index = i;
                            break;
                        }
                    }
                }
            }
        } else {
            for (int i = 0; i < s->count; i++) {
                if (_wcsicmp(s->images[i].path, selected_path) == 0) {
                    s->selected_index = i;
                    break;
                }
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

void gal_select_full_image(AppState *s, int index)
{
    int limit = s->grid_items ? s->grid_item_count : s->count;
    if (index < 0 || index >= limit) return;

    s->selected_index = index;
    s->zoom_level = 1.0f;
    s->zoom_ui_timer = 0.0f;
    s->zoom_pan_x = 0.0f;
    s->zoom_pan_y = 0.0f;
    s->is_panning = 0;

    if (s->images) {
        int img_idx = (s->grid_items) ? s->grid_items[index].image_index : index;
        if (img_idx < 0 || img_idx >= s->count) return;
        ImageEntry *e = &s->images[img_idx];
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
    int limit = s->grid_items ? s->grid_item_count : s->count;
    if (index < 0 || index >= limit) return;
    s->view_mode = VIEW_FULLIMAGE;
    gal_select_full_image(s, index);
}

void gal_render_gallery(HDC hdc, AppState *s)
{
    (void)hdc; // Unused parameter
    r_clear(s, 0.12f, 0.12f, 0.12f);

    int total_items = s->grid_items ? s->grid_item_count : s->count;
    if (total_items == 0) {
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

        int hovered = ui_is_hovered(x, y, thumb_size, thumb_size, (float)pt.x, (float)pt.y);

        // Hover Border: draw a subtle translucent white border (only if not selected)
        if (s->selected_index != i && hovered) {
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = x - 2.0f * s->dpi_scale;
            instances[inst_count].y = y - 2.0f * s->dpi_scale;
            instances[inst_count].w = thumb_size + 4.0f * s->dpi_scale;
            instances[inst_count].h = thumb_size + 4.0f * s->dpi_scale;
            instances[inst_count].tex_index = TOKEN_BORDER; // White border
            instances[inst_count].opacity = 0.35f;
            instances[inst_count].corner_radius = s->layout.thumb_radius + 2.0f * s->dpi_scale;
            inst_count++;
        }

        GridItemType type = s->grid_items ? s->grid_items[i].type : ITEM_IMAGE;
        int img_idx = s->grid_items ? s->grid_items[i].image_index : i;

        if (type == ITEM_FOLDER) {
            // Draw main folder backplate card
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = x;
            instances[inst_count].y = y;
            instances[inst_count].w = thumb_size;
            instances[inst_count].h = thumb_size;
            instances[inst_count].tex_index = TOKEN_PANEL; // Gray backplate
            instances[inst_count].opacity = (hovered || s->selected_index == i) ? 0.35f : 0.2f;
            instances[inst_count].corner_radius = s->layout.thumb_radius;
            inst_count++;
        } else {
            // Draw main image thumbnail
            if (img_idx >= 0 && img_idx < s->count) {
                if (s->images[img_idx].texture_slot == -1 && !s->images[img_idx].thumb_requested) {
                    s->images[img_idx].thumb_requested = 1;
                    aw_request_thumbnail(s, s->images[img_idx].path, THUMB_SIZE, s->hwnd);
                }

                instances[inst_count] = (InstanceData){0};
                instances[inst_count].x = x;
                instances[inst_count].y = y;
                instances[inst_count].w = thumb_size;
                instances[inst_count].h = thumb_size;
                instances[inst_count].tex_index = (s->images[img_idx].state == IMG_STATE_RESIDENT_GPU) ? s->images[img_idx].texture_slot : TOKEN_DEFAULT;
                instances[inst_count].opacity = (hovered || s->selected_index == i) ? 1.0f : 0.85f;
                instances[inst_count].corner_radius = s->layout.thumb_radius;

                if (s->images[img_idx].state == IMG_STATE_RESIDENT_GPU && s->images[img_idx].texture_slot != -1) {
                    s->tex_pool.last_used[s->images[img_idx].texture_slot] = s->tex_pool.frame_counter;
                }
                inst_count++;
            }
        }

        // Selection Outline: draw a glowing accent color outline on top of the item
        if (s->selected_index == i) {
            instances[inst_count] = (InstanceData){0};
            instances[inst_count].x = x;
            instances[inst_count].y = y;
            instances[inst_count].w = thumb_size;
            instances[inst_count].h = thumb_size;
            instances[inst_count].tex_index = TOKEN_SELECTION_OUTLINE; // Accent outline token
            instances[inst_count].opacity = 1.0f;
            instances[inst_count].corner_radius = s->layout.thumb_radius;
            inst_count++;
        }

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
            instances[inst_count].tex_index = TOKEN_PANEL;
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
            instances[inst_count].tex_index = TOKEN_SCROLLBAR;
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

    // Render Folder text & icons in D2D pass
    if (s->grid_items) {
        for (int i = lay.first_visible; i < lay.last_visible; i++) {
            if (s->grid_items[i].type == ITEM_FOLDER) {
                int row = i / lay.cols, col = i % lay.cols;
                float x = (float)(lay.left_margin + col * lay.pad);
                float y = (float)(s->layout.topbar_height + s->layout.panel_padding + row * lay.pad - lay.scroll_int);

                if (y + thumb_size < 0 || y > s->window_height) continue;

                // 1. Draw folder icon
                const wchar_t *icon = (_wcsicmp(s->grid_items[i].folder_name, L"..") == 0) ? L"\uEB52" : L"\uE8B7";
                float icon_y = y + 16.0f * s->dpi_scale;
                r_draw_text_aligned(s, icon, x, icon_y, thumb_size, thumb_size - 44.0f * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons_large, s->theme.accent);

                // 2. Draw folder name
                float name_x = x + 8.0f * s->dpi_scale;
                float name_y = y + thumb_size - 36.0f * s->dpi_scale;
                float name_w = thumb_size - 16.0f * s->dpi_scale;
                float name_h = 24.0f * s->dpi_scale;
                r_draw_text_aligned(s, s->grid_items[i].folder_name, name_x, name_y, name_w, name_h, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_semibold, s->theme.text_main);
            }
        }
    }

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

    int total_items = s->grid_items ? s->grid_item_count : s->count;
    if (total_items == 0 || s->selected_index < 0 || s->selected_index >= total_items) {
        r_present(s);
        return;
    }

    int active_img_idx = s->grid_items ? s->grid_items[s->selected_index].image_index : s->selected_index;
    if (active_img_idx < 0 || active_img_idx >= s->count) {
        r_present(s);
        return;
    }

    // Try loading full-resolution image when debounce delay has expired
    if (s->images && s->full_load_timer <= 0.0) {
        if (r_load_full_image(s, s->images[active_img_idx].path)) {
            // Preload ALL visible images in the bottom strip in memory parallelly (staggered, 1 per frame)
            float main_w = (float)s->window_width - 40.0f * s->dpi_scale;
            float avail_w = main_w - 100.0f * s->dpi_scale;
            int thumb_w = (int)(80 * s->dpi_scale);
            int thumb_pad = (int)(10 * s->dpi_scale);
            int col_w = thumb_w + thumb_pad;

            int num_strip_thumbs = (int)(avail_w / col_w);
            if (num_strip_thumbs < 1) num_strip_thumbs = 1;

            if (s->grid_items && s->strip_image_count > 0) {
                int active_img_idx_in_strip = -1;
                for (int i = 0; i < s->strip_image_count; i++) {
                    if (s->strip_image_grid_indices[i] == s->selected_index) {
                        active_img_idx_in_strip = i;
                        break;
                    }
                }

                if (active_img_idx_in_strip != -1) {
                    int half_n = num_strip_thumbs / 2;
                    int start_strip_idx = active_img_idx_in_strip - half_n;
                    if (start_strip_idx < 0) start_strip_idx = 0;
                    int end_strip_idx = start_strip_idx + num_strip_thumbs - 1;
                    if (end_strip_idx >= s->strip_image_count) {
                        end_strip_idx = s->strip_image_count - 1;
                        start_strip_idx = end_strip_idx - num_strip_thumbs + 1;
                        if (start_strip_idx < 0) start_strip_idx = 0;
                    }

                    for (int k = start_strip_idx; k <= end_strip_idx; k++) {
                        int grid_idx = s->strip_image_grid_indices[k];
                        if (grid_idx == s->selected_index) continue;
                        int img_idx = s->grid_items[grid_idx].image_index;
                        if (!r_get_full_image_slot(s, s->images[img_idx].path)) {
                            r_load_full_image(s, s->images[img_idx].path);
                            s->needs_redraw = 1; // trigger another render on next frame to stagger load
                            break;
                        }
                    }
                }
            } else {
                // Fallback for tests
                int num_strip_thumbs_fallback = num_strip_thumbs > s->count ? s->count : num_strip_thumbs;
                int half_n = num_strip_thumbs_fallback / 2;
                int start_idx = s->selected_index - half_n;
                if (start_idx < 0) start_idx = 0;
                int end_idx = start_idx + num_strip_thumbs_fallback - 1;
                if (end_idx >= s->count) {
                    end_idx = s->count - 1;
                    start_idx = end_idx - num_strip_thumbs_fallback + 1;
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
    }

    static InstanceData instances[4096];
    int inst_count = 0;

    // --- 1. Main Image Area ---
    float main_x = 20.0f * s->dpi_scale;
    float main_y = s->layout.topbar_height + 20.0f * s->dpi_scale;
    float main_w = (float)s->window_width - 40.0f * s->dpi_scale;
    float main_h = (float)s->window_height - (s->layout.topbar_height + 160.0f * s->dpi_scale);

    if (main_w > 0 && main_h > 0) {
        FullImageSlot *slot = s->images ? r_get_full_image_slot(s, s->images[active_img_idx].path) : NULL;
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
            instances[inst_count].tex_index = TOKEN_FULL_IMAGE; // Samples from register(t1)
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
    instances[inst_count].tex_index = TOKEN_PANEL; // Solid dark gray backplate
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
    instances[inst_count].tex_index = TOKEN_PANEL; // Gray backplate
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

    int start_idx = 0;
    int end_idx = 0;
    int active_img_idx_in_strip = -1;
    int total_images = 0;

    if (s->grid_items && s->strip_image_count > 0) {
        total_images = s->strip_image_count;
        for (int i = 0; i < s->strip_image_count; i++) {
            if (s->strip_image_grid_indices[i] == s->selected_index) {
                active_img_idx_in_strip = i;
                break;
            }
        }
    } else {
        total_images = s->count;
        active_img_idx_in_strip = s->selected_index;
    }

    if (total_images > 0 && active_img_idx_in_strip != -1) {
        int num_strip_thumbs = (int)(avail_w / col_w);
        if (num_strip_thumbs < 1) num_strip_thumbs = 1;
        if (num_strip_thumbs > total_images) num_strip_thumbs = total_images;

        int half_n = num_strip_thumbs / 2;
        start_idx = active_img_idx_in_strip - half_n;
        if (start_idx < 0) start_idx = 0;
        end_idx = start_idx + num_strip_thumbs - 1;
        if (end_idx >= total_images) {
            end_idx = total_images - 1;
            start_idx = end_idx - num_strip_thumbs + 1;
            if (start_idx < 0) start_idx = 0;
        }

        float total_thumbs_w = (float)(num_strip_thumbs * thumb_w + (num_strip_thumbs - 1) * thumb_pad);
        float thumbs_start_x = 55.0f * s->dpi_scale + (avail_w - total_thumbs_w) / 2.0f;

        for (int k = start_idx; k <= end_idx; k++) {
            int i = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[k] : k;
            int img_idx = s->grid_items ? s->grid_items[i].image_index : i;
            if (img_idx < 0 || img_idx >= s->count) continue;

            float tx = thumbs_start_x + (float)((k - start_idx) * col_w);
            float ty = strip_y + 10.0f * s->dpi_scale;

            // Lazy load strip thumbnails
            if (s->images[img_idx].texture_slot == -1 && !s->images[img_idx].thumb_requested) {
                s->images[img_idx].thumb_requested = 1;
                aw_request_thumbnail(s, s->images[img_idx].path, THUMB_SIZE, s->hwnd);
            }

            // Draw selection border if active
            if (i == s->selected_index) {
                instances[inst_count] = (InstanceData){0};
                instances[inst_count].x = tx - 4.0f * s->dpi_scale;
                instances[inst_count].y = ty - 4.0f * s->dpi_scale;
                instances[inst_count].w = (float)(thumb_w + 8.0f * s->dpi_scale);
                instances[inst_count].h = (float)(thumb_h + 8.0f * s->dpi_scale);
                instances[inst_count].tex_index = TOKEN_ACCENT; // Accent border
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
            instances[inst_count].tex_index = s->images[img_idx].texture_slot;
            instances[inst_count].opacity = 1.0f;
            instances[inst_count].corner_radius = 6.0f * s->dpi_scale;

            if (s->images[img_idx].texture_slot != -1) {
                s->tex_pool.last_used[s->images[img_idx].texture_slot] = s->tex_pool.frame_counter;
            }
            inst_count++;
        }
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
    r_draw_text_aligned(s, L"\uE72B", 20.0f * s->dpi_scale, 20.0f * s->dpi_scale, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);
    r_draw_text_aligned(s, L"\uE946", info_btn_x, 20.0f * s->dpi_scale, 80.0f * s->dpi_scale, 30.0f * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);

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
    r_draw_text_aligned(s, L"\uE76B", 20.0f * s->dpi_scale, strip_y + 35.0f * s->dpi_scale, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);
    r_draw_text_aligned(s, L"\uE76C", (float)s->window_width - 50.0f * s->dpi_scale, strip_y + 35.0f * s->dpi_scale, 30.0f * s->dpi_scale, 30.0f * s->dpi_scale, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);

    // Draw Metadata Card details
    if (s->info_open) {
        ImageEntry *e = &s->images[active_img_idx];
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
        r_draw_text_aligned(s, L"\uE711", close_x, close_y, close_w, close_h, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->dwrite_format_icons, s->theme.text_main);

        float pad = 15.0f * s->dpi_scale;
        float item_h = 24.0f * s->dpi_scale;

        // Render line items beautiful Segoe UI Variable text
        r_draw_text_aligned(s, L"IMAGE METADATA", info_x + pad, info_y + pad, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_semibold, s->theme.text_main);
        
        wchar_t line[256];
        swprintf(line, 256, L"Name:  %ls", name_trunc);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 1.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Path:  %ls", path_trunc);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 2.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Size:  %ls", sz_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 3.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Dims:  %ls", dim_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 4.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Created:  %ls", tc_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 5.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);

        swprintf(line, 256, L"Modified: %ls", tm_buf);
        r_draw_text_aligned(s, line, info_x + pad, info_y + pad + item_h * 6.2f, info_w - pad * 2.0f, item_h, ALIGN_X_LEFT, ALIGN_Y_CENTER, s->dwrite_format_regular, s->theme.text_main);
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
        int active_img_idx_in_strip = -1;
        int total_images = 0;

        if (s->grid_items && s->strip_image_count > 0) {
            total_images = s->strip_image_count;
            for (int i = 0; i < s->strip_image_count; i++) {
                if (s->strip_image_grid_indices[i] == s->selected_index) {
                    active_img_idx_in_strip = i;
                    break;
                }
            }
        } else {
            total_images = s->count;
            active_img_idx_in_strip = s->selected_index;
        }

        // Prev Arrow circular button hit test
        if (x >= 20 * dpi && x <= 50 * dpi && y >= strip_y + 35.0f * dpi && y <= strip_y + 65.0f * dpi) {
            if (active_img_idx_in_strip > 0) {
                int new_grid_idx = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[active_img_idx_in_strip - 1] : active_img_idx_in_strip - 1;
                gal_select_full_image(s, new_grid_idx);
            }
            return 1;
        }

        // Next Arrow circular button hit test
        if (x >= s->window_width - 50 * dpi && x <= s->window_width - 20 * dpi && y >= strip_y + 35.0f * dpi && y <= strip_y + 65.0f * dpi) {
            if (active_img_idx_in_strip >= 0 && active_img_idx_in_strip < total_images - 1) {
                int new_grid_idx = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[active_img_idx_in_strip + 1] : active_img_idx_in_strip + 1;
                gal_select_full_image(s, new_grid_idx);
            }
            return 1;
        }

        // Individual thumbnail hit testing in the strip
        float avail_w = (float)s->window_width - 140.0f * dpi;
        int thumb_w = (int)(80 * dpi);
        int thumb_pad = (int)(10 * dpi);
        int col_w = thumb_w + thumb_pad;

        if (total_images > 0 && active_img_idx_in_strip != -1) {
            int num_strip_thumbs = (int)(avail_w / col_w);
            if (num_strip_thumbs < 1) num_strip_thumbs = 1;
            if (num_strip_thumbs > total_images) num_strip_thumbs = total_images;

            int half_n = num_strip_thumbs / 2;
            int start_idx = active_img_idx_in_strip - half_n;
            if (start_idx < 0) start_idx = 0;
            int end_idx = start_idx + num_strip_thumbs - 1;
            if (end_idx >= total_images) {
                end_idx = total_images - 1;
                start_idx = end_idx - num_strip_thumbs + 1;
                if (start_idx < 0) start_idx = 0;
            }

            float total_thumbs_w = (float)(num_strip_thumbs * thumb_w + (num_strip_thumbs - 1) * thumb_pad);
            float thumbs_start_x = 55.0f * dpi + (avail_w - total_thumbs_w) / 2.0f;

            for (int k = start_idx; k <= end_idx; k++) {
                int i = (s->grid_items && s->strip_image_count > 0) ? s->strip_image_grid_indices[k] : k;
                float tx = thumbs_start_x + (float)((k - start_idx) * col_w);
                float ty = strip_y + 10.0f * dpi;

                if (x >= tx && x <= tx + thumb_w && y >= ty && y <= ty + thumb_w) {
                    if (s->selected_index != i) {
                        gal_select_full_image(s, i);
                    }
                    return 1;
                }
            }
        }
    }

    return closed_info;
}

void gal_activate_item(AppState *s, int idx)
{
    int limit = s->grid_items ? s->grid_item_count : s->count;
    if (idx < 0 || idx >= limit) return;

    if (s->grid_items && s->grid_items[idx].type == ITEM_FOLDER) {
        if (!s->grid_items[idx].folder_path) return;
        wcsncpy(s->viewing_dir, s->grid_items[idx].folder_path, MAX_PATH_LEN - 1);
        s->viewing_dir[MAX_PATH_LEN - 1] = L'\0';
        app_populate_grid_items(s);
        s->scroll_target_y = s->scroll_current_y = 0.0f;
        s->selected_index = 0;
        gal_update_layout(s);
        app_update_title(s);
        s->needs_redraw = 1;
    } else {
        gal_open_full(s, idx);
    }
}
