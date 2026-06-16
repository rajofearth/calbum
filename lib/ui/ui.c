// =========================================================================
// ui.c — Reusable IMGUI component library implementation
// =========================================================================
#include "ui.h"
#include <string.h>

int ui_is_hovered(float x, float y, float w, float h, float mx, float my)
{
    return (mx >= x && mx <= x + w && my >= y && my <= y + h);
}

void ui_panel(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity,
              int has_border, float corner_radius)
{
    if (has_border)
    {
        // Draw soft drop shadow behind the panel (extended 12px, offset down 6px)
        float shadow_padding = 12.0F;
        instances[*inst_count].x = x - shadow_padding;
        instances[*inst_count].y = y - shadow_padding + 6.0F;
        instances[*inst_count].w = w + (shadow_padding * 2.0F);
        instances[*inst_count].h = h + (shadow_padding * 2.0F);
        instances[*inst_count].tex_index = TOKEN_DROP_SHADOW; // Drop shadow token
        instances[*inst_count].opacity = opacity;
        instances[*inst_count].corner_radius = corner_radius + shadow_padding;
        instances[*inst_count]._pad = 0.0F;
        (*inst_count)++;

        // Draw translucent white border
        instances[*inst_count].x = x - 1.0F;
        instances[*inst_count].y = y - 1.0F;
        instances[*inst_count].w = w + 2.0F;
        instances[*inst_count].h = h + 2.0F;
        instances[*inst_count].tex_index = TOKEN_BORDER; // White color
        instances[*inst_count].opacity = 0.5F;           // Translucent
        instances[*inst_count].corner_radius = corner_radius + 1.0F;
        instances[*inst_count]._pad = 0.0F;
        (*inst_count)++;
    }

    // Draw main solid backdrop panel
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = TOKEN_PANEL; // Gray backplate
    instances[*inst_count].opacity = opacity;
    instances[*inst_count].corner_radius = corner_radius;
    instances[*inst_count]._pad = 0.0F;
    (*inst_count)++;
}

void ui_blur_panel(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity,
                   int has_border, float corner_radius)
{
    if (has_border)
    {
        // Draw soft drop shadow behind the panel (extended 12px, offset down 6px)
        float shadow_padding = 12.0F;
        instances[*inst_count].x = x - shadow_padding;
        instances[*inst_count].y = y - shadow_padding + 6.0F;
        instances[*inst_count].w = w + (shadow_padding * 2.0F);
        instances[*inst_count].h = h + (shadow_padding * 2.0F);
        instances[*inst_count].tex_index = TOKEN_DROP_SHADOW; // Drop shadow token
        instances[*inst_count].opacity = opacity;
        instances[*inst_count].corner_radius = corner_radius + shadow_padding;
        instances[*inst_count]._pad = 0.0F;
        (*inst_count)++;

        // Draw translucent white border
        instances[*inst_count].x = x - 1.0F;
        instances[*inst_count].y = y - 1.0F;
        instances[*inst_count].w = w + 2.0F;
        instances[*inst_count].h = h + 2.0F;
        instances[*inst_count].tex_index = TOKEN_BORDER; // White color
        instances[*inst_count].opacity = 0.5F;           // Translucent
        instances[*inst_count].corner_radius = corner_radius + 1.0F;
        instances[*inst_count]._pad = 0.0F;
        (*inst_count)++;
    }

    // Draw main blur backdrop panel
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = TOKEN_BLUR; // Blur backplate
    instances[*inst_count].opacity = opacity;
    instances[*inst_count].corner_radius = corner_radius;
    instances[*inst_count]._pad = 0.0F;
    (*inst_count)++;
}

int ui_button(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, float mx,
              float my, float corner_radius)
{
    int hovered = ui_is_hovered(x, y, w, h, mx, my);

    // Draw subtle button border (1px peek)
    instances[*inst_count].x = x - 1.0F;
    instances[*inst_count].y = y - 1.0F;
    instances[*inst_count].w = w + 2.0F;
    instances[*inst_count].h = h + 2.0F;
    instances[*inst_count].tex_index = TOKEN_BORDER;        // One Dark border color
    instances[*inst_count].opacity = hovered ? 0.8F : 0.0F; // Ghost style: no border unless hovered
    instances[*inst_count].corner_radius = corner_radius + 1.0F;
    instances[*inst_count]._pad = 0.0F;
    (*inst_count)++;

    // Draw main button background
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = hovered ? TOKEN_BORDER : TOKEN_PANEL; // Use lighter border color on hover
    instances[*inst_count].opacity = hovered ? 0.8F : opacity;
    instances[*inst_count].corner_radius = corner_radius;
    instances[*inst_count]._pad = 0.0F;
    (*inst_count)++;

    return hovered;
}

void ui_button_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    r_draw_text_aligned(s, text, x, y, w, h, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->txt.dwrite_format_small_semibold,
                        s->ui.theme.text_main);
}

int ui_badge(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, int active,
             float mx, float my, float corner_radius)
{
    int hovered = ui_is_hovered(x, y, w, h, mx, my);
    int border_active = active || hovered;

    // Draw active or hovered border
    if (border_active)
    {
        instances[*inst_count].x = x - 1.0F;
        instances[*inst_count].y = y - 1.0F;
        instances[*inst_count].w = w + 2.0F;
        instances[*inst_count].h = h + 2.0F;
        instances[*inst_count].tex_index = active ? TOKEN_ACCENT : TOKEN_BORDER; // Accent if active, otherwise white
        instances[*inst_count].opacity = hovered ? 0.9F : 0.5F;
        instances[*inst_count].corner_radius = corner_radius + 1.0F;
        instances[*inst_count]._pad = 0.0F;
        (*inst_count)++;
    }

    // Draw main badge backdrop
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = active ? TOKEN_ACCENT : TOKEN_PANEL; // Accent backplate if active
    float badge_opacity;
    if (active)
    {
        badge_opacity = 0.3F;
    }
    else if (hovered)
    {
        badge_opacity = 0.95F;
    }
    else
    {
        badge_opacity = opacity;
    }
    instances[*inst_count].opacity = badge_opacity;
    instances[*inst_count].corner_radius = corner_radius;
    instances[*inst_count]._pad = 0.0F;
    (*inst_count)++;

    return hovered;
}

void ui_badge_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    r_draw_text_aligned(s, text, x, y, w, h, ALIGN_X_CENTER, ALIGN_Y_CENTER, s->txt.dwrite_format_small_semibold,
                        s->ui.theme.text_main);
}
