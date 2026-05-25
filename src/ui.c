// =========================================================================
// ui.c — Reusable IMGUI component library implementation
// =========================================================================
#include "ui.h"
#include <string.h>

int ui_is_hovered(float x, float y, float w, float h, float mx, float my)
{
    return (mx >= x && mx <= x + w && my >= y && my <= y + h);
}

void ui_panel(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, int has_border)
{
    // Draw translucent white border
    if (has_border) {
        instances[*inst_count].x = x - 1.0f;
        instances[*inst_count].y = y - 1.0f;
        instances[*inst_count].w = w + 2.0f;
        instances[*inst_count].h = h + 2.0f;
        instances[*inst_count].tex_index = -2; // White color
        instances[*inst_count].opacity = 0.5f; // Translucent
        (*inst_count)++;
    }

    // Draw main solid backdrop panel
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = -3; // Gray backplate
    instances[*inst_count].opacity = opacity;
    (*inst_count)++;
}

int ui_button(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, float mx, float my)
{
    int hovered = ui_is_hovered(x, y, w, h, mx, my);

    // If hovered, draw sleek border highlights
    if (hovered) {
        instances[*inst_count].x = x - 1.0f;
        instances[*inst_count].y = y - 1.0f;
        instances[*inst_count].w = w + 2.0f;
        instances[*inst_count].h = h + 2.0f;
        instances[*inst_count].tex_index = -2; // White border
        instances[*inst_count].opacity = 0.8f;
        (*inst_count)++;
    }

    // Draw main button background
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = -3; // 0.2 gray
    instances[*inst_count].opacity = hovered ? 0.95f : opacity;
    (*inst_count)++;

    return hovered;
}

void ui_button_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    // Center and draw button text
    float font_size = 14.0f;
    float text_w = (float)wcslen(text) * (font_size * 0.55f);
    float text_h = font_size + 4.0f;
    float tx = x + (w - text_w) / 2.0f;
    float ty = y + (h - text_h) / 2.0f;

    r_draw_text(s, text, tx, ty, text_w + 10.0f, h);
}

int ui_badge(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, int active, float mx, float my)
{
    int hovered = ui_is_hovered(x, y, w, h, mx, my);
    int border_active = active || hovered;

    // Draw active or hovered border
    if (border_active) {
        instances[*inst_count].x = x - 1.0f;
        instances[*inst_count].y = y - 1.0f;
        instances[*inst_count].w = w + 2.0f;
        instances[*inst_count].h = h + 2.0f;
        instances[*inst_count].tex_index = -2; // White color
        instances[*inst_count].opacity = hovered ? 0.9f : 0.5f;
        (*inst_count)++;
    }

    // Draw main badge backdrop
    instances[*inst_count].x = x;
    instances[*inst_count].y = y;
    instances[*inst_count].w = w;
    instances[*inst_count].h = h;
    instances[*inst_count].tex_index = -3; // Gray color
    instances[*inst_count].opacity = hovered ? 0.95f : opacity;
    (*inst_count)++;

    return hovered;
}

void ui_badge_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    // Center and draw text
    float font_size = 14.0f;
    float text_w = (float)wcslen(text) * (font_size * 0.55f);
    float text_h = font_size + 4.0f;
    float tx = x + (w - text_w) / 2.0f;
    float ty = y + (h - text_h) / 2.0f;

    r_draw_text(s, text, tx, ty, text_w + 10.0f, h);
}
