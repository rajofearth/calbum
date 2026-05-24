// =========================================================================
// image_loader.c — Image file decoding via stb_image
//
// Thread-safe: each call allocates + frees its own stb data.
// stb_image.h must be #included (with STB_IMAGE_IMPLEMENTATION)
// BEFORE this file in build.c.
// =========================================================================
#include "types.h"
#include <stdlib.h>

HBITMAP il_create_hbitmap(unsigned char *data, int width, int height, int channels)
{
    int stride = width * 4;
    unsigned char *bgra = (unsigned char *)malloc(stride * height);
    if (!bgra) return NULL;

    if (channels >= 3) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                int s = (y * width + x) * channels;
                int d = y * stride + x * 4;
                bgra[d+0] = data[s+2]; bgra[d+1] = data[s+1];
                bgra[d+2] = data[s+0]; bgra[d+3] = (channels >= 4) ? data[s+3] : 255;
            }
    } else if (channels == 1) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                unsigned char v = data[y * width + x];
                int d = y * stride + x * 4;
                bgra[d+0]=bgra[d+1]=bgra[d+2]=v; bgra[d+3]=255;
            }
    } else { free(bgra); return NULL; }

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize   = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth  = width;
    bi.bmiHeader.biHeight = -height; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbmp && bits) memcpy(bits, bgra, stride * height);
    free(bgra);
    return hbmp;
}

HBITMAP il_load_thumbnail(const wchar_t *path, int thumb_size)
{
    char utf8[MAX_PATH_LEN * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);

    int w, h, ch;
    unsigned char *data = stbi_load(utf8, &w, &h, &ch, 4);
    if (!data) return NULL;

    int tw = thumb_size, th = thumb_size;
    if (w > h) th = (h * thumb_size) / w;
    else       tw = (w * thumb_size) / h;

    unsigned char *thumb = (unsigned char *)malloc(tw * th * 4);
    if (!thumb) { stbi_image_free(data); return NULL; }

    float sx = (float)w / tw, sy = (float)h / th;
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            int si = ((int)(ty * sy) * w + (int)(tx * sx)) * 4;
            int di = (ty * tw + tx) * 4;
            thumb[di+0]=data[si+0]; thumb[di+1]=data[si+1];
            thumb[di+2]=data[si+2]; thumb[di+3]=data[si+3];
        }
    stbi_image_free(data);

    HBITMAP hbmp = il_create_hbitmap(thumb, tw, th, 4);
    free(thumb);
    return hbmp;
}

HBITMAP il_load_full(const wchar_t *path, int *out_w, int *out_h)
{
    char utf8[MAX_PATH_LEN * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
    int w, h, ch;
    unsigned char *data = stbi_load(utf8, &w, &h, &ch, 4);
    if (!data) return NULL;
    *out_w = w; *out_h = h;
    HBITMAP hbmp = il_create_hbitmap(data, w, h, 4);
    stbi_image_free(data);
    return hbmp;
}

void il_free_bitmap(HBITMAP bmp) { if (bmp) DeleteObject(bmp); }
