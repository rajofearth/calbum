// =========================================================================
// renderer.c — GDI rendering abstraction
// =========================================================================
#include "types.h"

void r_clear(HDC hdc, int w, int h, COLORREF color)
{
    RECT r = {0,0,w,h};
    HBRUSH b = CreateSolidBrush(color);
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

void r_fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF color)
{
    RECT r = {x,y,x+w,y+h};
    HBRUSH b = CreateSolidBrush(color);
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

void r_round_rect(HDC hdc, int x, int y, int w, int h, int r, COLORREF color)
{
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_NULL,0,0);
    HBRUSH ob = SelectObject(hdc,b);
    HPEN   op = SelectObject(hdc,p);
    RoundRect(hdc,x,y,x+w,y+h,r*2,r*2);
    SelectObject(hdc,op); DeleteObject(p);
    SelectObject(hdc,ob); DeleteObject(b);
}

void r_draw_bitmap(HDC dst, HDC src_dc, HBITMAP bmp, int x, int y)
{
    BITMAP bm;
    GetObject(bmp,sizeof(bm),&bm);
    SelectObject(src_dc,bmp);
    BitBlt(dst,x,y,bm.bmWidth,bm.bmHeight,src_dc,0,0,SRCCOPY);
}

void r_draw_text(HDC hdc, const wchar_t *text, int x, int y, int w, int h,
                 COLORREF color, int font_size, int flags)
{
    SetBkMode(hdc,TRANSPARENT);
    SetTextColor(hdc,color);
    HFONT font = CreateFontW(font_size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HFONT old = SelectObject(hdc,font);
    RECT r = {x,y,x+w,y+h};
    DrawTextW(hdc,text,-1,&r,flags);
    SelectObject(hdc,old);
    DeleteObject(font);
}

void r_stretch_bitmap(HDC dst, int dx, int dy, int dw, int dh,
                      HDC src_dc, HBITMAP bmp, int sw, int sh)
{
    SelectObject(src_dc,bmp);
    SetStretchBltMode(dst,HALFTONE);
    StretchBlt(dst,dx,dy,dw,dh,src_dc,0,0,sw,sh,SRCCOPY);
}
