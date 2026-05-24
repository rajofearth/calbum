// =========================================================================
// image_loader.c — WIC + BC1 compression
// =========================================================================
#include "types.h"
#include <stdio.h>
#include <stdlib.h>

static IWICImagingFactory *g_wic_factory = NULL;
static void *g_decode_buffer = NULL;

int il_init_wic(void)
{
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&g_wic_factory);
    if (SUCCEEDED(hr)) {
        g_decode_buffer = malloc(16 * 1024 * 1024);
    }
    return SUCCEEDED(hr) ? 1 : 0;
}

void il_shutdown_wic(void)
{
    if (g_wic_factory) {
        g_wic_factory->lpVtbl->Release(g_wic_factory);
        g_wic_factory = NULL;
    }
    if (g_decode_buffer) {
        free(g_decode_buffer);
        g_decode_buffer = NULL;
    }
}

extern void stb_compress_dxt_block(unsigned char *dest, const unsigned char *src, int alpha, int mode);

void* il_load_and_compress(const wchar_t *path, int thumb_size, int *out_size)
{
    if (!g_wic_factory) return NULL;

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = g_wic_factory->lpVtbl->CreateDecoderFromFilename(
        g_wic_factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return NULL;

    IWICBitmapFrameDecode *frame = NULL;
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    decoder->lpVtbl->Release(decoder);
    if (FAILED(hr)) return NULL;

    UINT w = 0, h = 0;
    frame->lpVtbl->GetSize(frame, &w, &h);

    UINT tw = thumb_size, th = thumb_size;
    if (w > h) th = (h * thumb_size) / w;
    else       tw = (w * thumb_size) / h;
    if (tw == 0) tw = 1;
    if (th == 0) th = 1;

    IWICBitmapScaler *scaler = NULL;
    hr = g_wic_factory->lpVtbl->CreateBitmapScaler(g_wic_factory, &scaler);
    if (SUCCEEDED(hr)) {
        hr = scaler->lpVtbl->Initialize(scaler, (IWICBitmapSource*)frame, tw, th, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) { scaler->lpVtbl->Release(scaler); scaler = NULL; }
    }
    frame->lpVtbl->Release(frame);
    if (!scaler) return NULL;

    IWICFormatConverter *converter = NULL;
    hr = g_wic_factory->lpVtbl->CreateFormatConverter(g_wic_factory, &converter);
    if (SUCCEEDED(hr)) {
        hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)scaler,
            &GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { converter->lpVtbl->Release(converter); converter = NULL; }
    }
    scaler->lpVtbl->Release(scaler);
    if (!converter) return NULL;

    UINT stride = thumb_size * 4;
    UINT total_size = stride * thumb_size;
    unsigned char *rgba = (unsigned char *)calloc(1, total_size);
    if (rgba) {
        UINT dx = (thumb_size - tw) / 2;
        UINT dy = (thumb_size - th) / 2;
        WICRect rect = {0, 0, (INT)tw, (INT)th};
        
        unsigned char *tight = (unsigned char*)malloc(tw * 4 * th);
        if (tight) {
            hr = converter->lpVtbl->CopyPixels(converter, &rect, tw * 4, tw * 4 * th, tight);
            if (SUCCEEDED(hr)) {
                for (UINT y = 0; y < th; y++) {
                    memcpy(rgba + (dy + y) * stride + dx * 4, tight + y * tw * 4, tw * 4);
                }
            }
            free(tight);
        }
    }
    converter->lpVtbl->Release(converter);

    if (!rgba || FAILED(hr)) {
        if (rgba) free(rgba);
        return NULL;
    }

    int blocks_x = thumb_size / 4;
    int blocks_y = thumb_size / 4;
    int bc1_size = blocks_x * blocks_y * 8;
    unsigned char *bc1 = (unsigned char *)malloc(bc1_size);
    
    if (bc1) {
        unsigned char block[64];
        unsigned char *dst = bc1;
        for (int by = 0; by < blocks_y; by++) {
            for (int bx = 0; bx < blocks_x; bx++) {
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        int sx = bx * 4 + x;
                        int sy = by * 4 + y;
                        memcpy(&block[(y * 4 + x) * 4], &rgba[sy * stride + sx * 4], 4);
                    }
                }
                stb_compress_dxt_block(dst, block, 0, 0);
                dst += 8;
            }
        }
    }
    free(rgba);

    if (out_size) *out_size = bc1_size;
    return bc1;
}

void il_free_bc1_data(void* data)
{
    if (data) free(data);
}

int il_get_image_dimensions(const wchar_t *path, int *out_w, int *out_h)
{
    if (!g_wic_factory) return 0;

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = g_wic_factory->lpVtbl->CreateDecoderFromFilename(
        g_wic_factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return 0;

    IWICBitmapFrameDecode *frame = NULL;
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    decoder->lpVtbl->Release(decoder);
    if (FAILED(hr)) return 0;

    UINT w = 0, h = 0;
    frame->lpVtbl->GetSize(frame, &w, &h);
    frame->lpVtbl->Release(frame);

    *out_w = (int)w;
    *out_h = (int)h;
    return 1;
}

void* il_load_full_image(const wchar_t *path, int *out_w, int *out_h)
{
    if (!g_wic_factory) return NULL;

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = g_wic_factory->lpVtbl->CreateDecoderFromFilename(
        g_wic_factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return NULL;

    IWICBitmapFrameDecode *frame = NULL;
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    decoder->lpVtbl->Release(decoder);
    if (FAILED(hr)) return NULL;

    UINT w = 0, h = 0;
    frame->lpVtbl->GetSize(frame, &w, &h);

    // Limit maximum dimension to 2048px for smooth Direct3D uploading and low memory footprint
    UINT tw = w, th = h;
    UINT max_dim = 2048;
    if (w > max_dim || h > max_dim) {
        if (w > h) {
            th = (h * max_dim) / w;
            tw = max_dim;
        } else {
            tw = (w * max_dim) / h;
            th = max_dim;
        }
    }

    IWICBitmapSource *source = (IWICBitmapSource*)frame;
    IWICBitmapScaler *scaler = NULL;
    if (tw != w || th != h) {
        hr = g_wic_factory->lpVtbl->CreateBitmapScaler(g_wic_factory, &scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->lpVtbl->Initialize(scaler, (IWICBitmapSource*)frame, tw, th, WICBitmapInterpolationModeLinear);
            if (SUCCEEDED(hr)) {
                source = (IWICBitmapSource*)scaler;
            } else {
                scaler->lpVtbl->Release(scaler);
                scaler = NULL;
            }
        }
    }

    IWICFormatConverter *converter = NULL;
    hr = g_wic_factory->lpVtbl->CreateFormatConverter(g_wic_factory, &converter);
    if (SUCCEEDED(hr)) {
        hr = converter->lpVtbl->Initialize(converter, source,
            &GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
    }

    if (scaler) scaler->lpVtbl->Release(scaler);
    frame->lpVtbl->Release(frame);

    if (FAILED(hr) || !converter) {
        if (converter) converter->lpVtbl->Release(converter);
        return NULL;
    }

    UINT stride = tw * 4;
    UINT total_size = stride * th;
    void *rgba = NULL;
    if (g_decode_buffer && total_size <= 16 * 1024 * 1024) {
        rgba = g_decode_buffer;
        WICRect rect = {0, 0, (INT)tw, (INT)th};
        hr = converter->lpVtbl->CopyPixels(converter, &rect, stride, total_size, (BYTE*)rgba);
        if (FAILED(hr)) {
            rgba = NULL;
        }
    }
    converter->lpVtbl->Release(converter);

    if (rgba) {
        *out_w = (int)tw;
        *out_h = (int)th;
    }
    return rgba;
}
