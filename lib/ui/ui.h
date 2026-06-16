// =========================================================================
// ui.h — Reusable IMGUI widget component library
//
// Public API for the UI widgets module.  Include this header to use
// panel, button, badge and text-drawing helpers in the D3D11/D2D render
// pipeline.  All types (AppState, InstanceData, RenderToken) come from
// the project-wide src/types.h header.
// =========================================================================
#ifndef CALBUM_UI_H
#define CALBUM_UI_H

#include "../../src/types.h"

// Check if a point (mx, my) is inside rect (x, y, w, h)
int ui_is_hovered(float x, float y, float w, float h, float mx, float my);

// Renders a sleek translucent backplate panel with an optional border
void ui_panel(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity,
              int has_border, float corner_radius);
void ui_blur_panel(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity,
                   int has_border, float corner_radius);

// Renders a reusable premium button backdrop, returns 1 if hovered
int ui_button(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, float mx,
              float my, float corner_radius);

// Draws centered text for the button in the D2D pass
void ui_button_text(AppState *s, const wchar_t *text, float x, float y, float w, float h);

// Renders a reusable premium text badge backdrop, returns 1 if hovered
int ui_badge(InstanceData *instances, int *inst_count, float x, float y, float w, float h, float opacity, int active,
             float mx, float my, float corner_radius);

// Draws centered text for the badge in the D2D pass
void ui_badge_text(AppState *s, const wchar_t *text, float x, float y, float w, float h);

#endif
