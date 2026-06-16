// =========================================================================
// lib/gpu/d2d.c — Direct2D / DirectWrite text rendering
//
// Note: D2D1 and DWrite system headers are included via device.c
// (included first in the build order) to ensure correct header ordering.
// =========================================================================
#include "src/types.h"

void r_draw_text_ext(AppState *s, const wchar_t *text, float x, float y, float w, float h,
                     struct IDWriteTextFormat *format, float color[4])
{
    if (!s->txt.d2d_rtv || !format)
        return;

    D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
    s->txt.d2d_brush->lpVtbl->SetColor(s->txt.d2d_brush, &c);

    D2D1_RECT_F layoutRect = {x, y, x + w, y + h};
    ID2D1RenderTarget_DrawText(s->txt.d2d_rtv, text, (UINT32) wcslen(text), format, &layoutRect,
                               (ID2D1Brush *) s->txt.d2d_brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                               DWRITE_MEASURING_MODE_NATURAL);
}

void r_draw_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    r_draw_text_ext(s, text, x, y, w, h, s->txt.dwrite_format, white);
}

void r_draw_text_aligned(AppState *s, const wchar_t *text, float x, float y, float w, float h, int align_x, int align_y,
                         struct IDWriteTextFormat *format, float color[4])
{
    if (!s->txt.d2d_rtv || !s->txt.dwrite_factory || !format)
        return;

    // Fast path: cached layouts for single-character icon glyphs
    if (wcslen(text) == 1 && s->txt.dwrite_format_icons == format)
    {
        struct IDWriteTextLayout *layout = NULL;
        if (text[0] == 0xE72B) {
            layout = s->txt.layout_back;
        } else if (text[0] == 0xE946) {
            layout = s->txt.layout_info;
}

        if (layout)
        {
            layout->lpVtbl->SetMaxWidth(layout, w);
            layout->lpVtbl->SetMaxHeight(layout, h);
            D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
            s->txt.d2d_brush->lpVtbl->SetColor(s->txt.d2d_brush, &c);
            D2D1_POINT_2F origin = {x, y};
            s->txt.d2d_rtv->lpVtbl->DrawTextLayout(s->txt.d2d_rtv, origin, layout, (ID2D1Brush *) s->txt.d2d_brush,
                                                   D2D1_DRAW_TEXT_OPTIONS_NONE);
            return;
        }
    }

    // Fall through to CreateTextLayout for non-cached strings
    IDWriteTextLayout *layout = NULL;
    HRESULT hr = s->txt.dwrite_factory->lpVtbl->CreateTextLayout(s->txt.dwrite_factory, text, (UINT32) wcslen(text),
                                                                 format, w, h, &layout);

    if (SUCCEEDED(hr) && layout)
    {
        layout->lpVtbl->SetTextAlignment(layout, (DWRITE_TEXT_ALIGNMENT) align_x);
        layout->lpVtbl->SetParagraphAlignment(layout, (DWRITE_PARAGRAPH_ALIGNMENT) align_y);

        D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
        s->txt.d2d_brush->lpVtbl->SetColor(s->txt.d2d_brush, &c);

        D2D1_POINT_2F origin = {x, y};
        s->txt.d2d_rtv->lpVtbl->DrawTextLayout(s->txt.d2d_rtv, origin, layout, (ID2D1Brush *) s->txt.d2d_brush,
                                               D2D1_DRAW_TEXT_OPTIONS_NONE);

        ((IUnknown *) layout)->lpVtbl->Release((IUnknown *) layout);
    }
}

float r_measure_text_width(AppState *s, const wchar_t *text, struct IDWriteTextFormat *format)
{
    if (!s->txt.dwrite_factory || !format || !text)
        return 0.0F;

    IDWriteTextLayout *layout = NULL;
    HRESULT hr = s->txt.dwrite_factory->lpVtbl->CreateTextLayout(s->txt.dwrite_factory, text, (UINT32) wcslen(text),
                                                                 format, 9999.0F, 9999.0F, &layout);

    float width = 0.0F;
    if (SUCCEEDED(hr) && layout)
    {
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(layout->lpVtbl->GetMetrics(layout, &metrics)))
            width = metrics.width;
        ((IUnknown *) layout)->lpVtbl->Release((IUnknown *) layout);
    }
    return width;
}
