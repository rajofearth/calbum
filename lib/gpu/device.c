// =========================================================================
// lib/gpu/device.c — D3D11 device, swap chain, init/shutdown/resize
// =========================================================================
#include "src/types.h"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stddef.h>
#include <string.h>

// shader_src is defined in shader.c (included before device.c in build order)
extern const char *shader_src;

int r_init(GpuState *r, TextState *txt, HWND hwnd)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION,
                                   &r->d3d_device, &feature_level, &r->d3d_context);
    if (FAILED(hr))
    {
        log_error(L"r_init: D3D11CreateDevice failed (hr=0x%08X)", (unsigned int) hr);
        return 0;
    }

    IDXGIDevice2 *dxgi_device;
    HRESULT hr_sc = E_FAIL;
    if (SUCCEEDED(r->d3d_device->lpVtbl->QueryInterface(r->d3d_device, &IID_IDXGIDevice2, (void **) &dxgi_device)))
    {
        IDXGIAdapter *dxgi_adapter;
        if (SUCCEEDED(dxgi_device->lpVtbl->GetParent(dxgi_device, &IID_IDXGIAdapter, (void **) &dxgi_adapter)))
        {
            IDXGIFactory2 *dxgi_factory;
            if (SUCCEEDED(dxgi_adapter->lpVtbl->GetParent(dxgi_adapter, &IID_IDXGIFactory2, (void **) &dxgi_factory)))
            {
                DXGI_SWAP_CHAIN_DESC1 scd1 = {0};
                scd1.Width = 0;
                scd1.Height = 0;
                scd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                scd1.SampleDesc.Count = 1;
                scd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                scd1.BufferCount = 2;
                scd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                scd1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

                IDXGISwapChain1 *sc1;
                hr_sc = dxgi_factory->lpVtbl->CreateSwapChainForHwnd(dxgi_factory, (IUnknown *) r->d3d_device, hwnd,
                                                                     &scd1, NULL, NULL, &sc1);
                if (SUCCEEDED(hr_sc))
                {
                    sc1->lpVtbl->QueryInterface(sc1, &IID_IDXGISwapChain, (void **) &r->swap_chain);
                    sc1->lpVtbl->Release(sc1);
                }
                dxgi_factory->lpVtbl->Release(dxgi_factory);
            }
            dxgi_adapter->lpVtbl->Release(dxgi_adapter);
        }
        dxgi_device->lpVtbl->Release(dxgi_device);
    }
    if (!r->swap_chain)
    {
        log_error(L"r_init: CreateSwapChainForHwnd failed (hr=0x%08X)", (unsigned int) hr_sc);
        return 0;
    }

    ID3DBlob *vs_blob = NULL;
    ID3DBlob *ps_blob = NULL;
    ID3DBlob *err = NULL;
    D3DCompile(shader_src, strlen(shader_src), NULL, NULL, NULL, "vs_main", "vs_5_0", 0, 0, &vs_blob, &err);
    if (err)
    {
        OutputDebugStringA(err->lpVtbl->GetBufferPointer(err));
        err->lpVtbl->Release(err);
    }
    D3DCompile(shader_src, strlen(shader_src), NULL, NULL, NULL, "ps_main", "ps_5_0", 0, 0, &ps_blob, &err);
    if (err)
    {
        OutputDebugStringA(err->lpVtbl->GetBufferPointer(err));
        err->lpVtbl->Release(err);
    }

    r->d3d_device->lpVtbl->CreateVertexShader(r->d3d_device, vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                              vs_blob->lpVtbl->GetBufferSize(vs_blob), NULL, &r->vs);
    r->d3d_device->lpVtbl->CreatePixelShader(r->d3d_device, ps_blob->lpVtbl->GetBufferPointer(ps_blob),
                                             ps_blob->lpVtbl->GetBufferSize(ps_blob), NULL, &r->ps);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        {"INST_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(InstanceData, x), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(InstanceData, w), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_TEX", 0, DXGI_FORMAT_R32_SINT, 0, offsetof(InstanceData, tex_index), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, opacity), D3D11_INPUT_PER_INSTANCE_DATA,
         1},
        {"INST_RADIUS", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, corner_radius),
         D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    r->d3d_device->lpVtbl->CreateInputLayout(r->d3d_device, ied, 5, vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                             vs_blob->lpVtbl->GetBufferSize(vs_blob), &r->input_layout);

    if (vs_blob)
        vs_blob->lpVtbl->Release(vs_blob);
    if (ps_blob)
        ps_blob->lpVtbl->Release(ps_blob);

    D3D11_BUFFER_DESC bd = {0};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(InstanceData) * 4096;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    r->d3d_device->lpVtbl->CreateBuffer(r->d3d_device, &bd, NULL, &r->instance_buffer);

    bd.ByteWidth = 16;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    r->d3d_device->lpVtbl->CreateBuffer(r->d3d_device, &bd, NULL, &r->constant_buffer);

    bd.ByteWidth = sizeof(Theme);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    r->d3d_device->lpVtbl->CreateBuffer(r->d3d_device, &bd, NULL, &r->theme_buffer);

    D3D11_SAMPLER_DESC sd = {0};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    r->d3d_device->lpVtbl->CreateSamplerState(r->d3d_device, &sd, &r->sampler);

    D3D11_BLEND_DESC bld = {0};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    r->d3d_device->lpVtbl->CreateBlendState(r->d3d_device, &bld, &r->blend_state);

    D3D11_TEXTURE2D_DESC tdesc = {0};
    tdesc.Width = THUMB_SIZE;
    tdesc.Height = THUMB_SIZE;
    tdesc.MipLevels = 1;
    tdesc.ArraySize = MAX_GPU_TEXTURES;
    tdesc.Format = DXGI_FORMAT_BC1_UNORM;
    tdesc.SampleDesc.Count = 1;
    tdesc.Usage = D3D11_USAGE_DEFAULT;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    r->d3d_device->lpVtbl->CreateTexture2D(r->d3d_device, &tdesc, NULL, &r->tex_pool.texture_array);
    if (r->tex_pool.texture_array)
    {
        r->d3d_device->lpVtbl->CreateShaderResourceView(r->d3d_device, (ID3D11Resource *) r->tex_pool.texture_array,
                                                        NULL, &r->tex_pool.texture_array_srv);
    }

    for (int i = 0; i < MAX_GPU_TEXTURES; i++)
    {
        r->tex_pool.last_used[i] = -1;
        r->tex_pool.slot_owner[i] = -1;
    }
    r->tex_pool.frame_counter = 0;

    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        r->full_slots[i].texture = NULL;
        r->full_slots[i].srv = NULL;
        r->full_slots[i].path[0] = L'\0';
        r->full_slots[i].w = 0;
        r->full_slots[i].h = 0;
    }
    r->active_full_srv = NULL;

    // Init D2D1 and DirectWrite
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **) &txt->d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **) &txt->dwrite_factory);

    if (txt->dwrite_factory)
    {
        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"en-US", &txt->dwrite_format_regular);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"en-US", &txt->dwrite_format_semibold);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.5F, L"en-US", &txt->dwrite_format_small);

        const wchar_t *mono_font = L"Consolas";
        IDWriteFontCollection *collection = NULL;
        if (SUCCEEDED(txt->dwrite_factory->lpVtbl->GetSystemFontCollection(txt->dwrite_factory, &collection, FALSE)) &&
            collection)
        {
            UINT32 index = 0;
            BOOL exists = FALSE;
            collection->lpVtbl->FindFamilyName(collection, L"Zed Mono", &index, &exists);
            if (exists)
            {
                mono_font = L"Zed Mono";
            }
            else
            {
                collection->lpVtbl->FindFamilyName(collection, L"Lilex", &index, &exists);
                if (exists)
                    mono_font = L"Lilex";
            }
            collection->lpVtbl->Release(collection);
        }

        txt->dwrite_factory->lpVtbl->CreateTextFormat(txt->dwrite_factory, mono_font, NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F,
                                                      L"en-US", &txt->dwrite_format_mono);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(txt->dwrite_factory, mono_font, NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0F,
                                                      L"en-US", &txt->dwrite_format_mono_small);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.5F, L"en-US", &txt->dwrite_format_small_semibold);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe Fluent Icons", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 18.0F, L"en-US", &txt->dwrite_format_icons);

        txt->dwrite_factory->lpVtbl->CreateTextFormat(
            txt->dwrite_factory, L"Segoe Fluent Icons", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 48.0F, L"en-US", &txt->dwrite_format_icons_large);

        txt->dwrite_format = txt->dwrite_format_regular;
    }

    // Create cached text layouts for static icon glyphs
    if (txt->dwrite_factory && txt->dwrite_format_icons)
    {
        txt->dwrite_factory->lpVtbl->CreateTextLayout(txt->dwrite_factory, L"\uE72B", 1, txt->dwrite_format_icons,
                                                      9999.0F, 9999.0F, &txt->layout_back);
        if (txt->layout_back)
        {
            txt->layout_back->lpVtbl->SetTextAlignment(txt->layout_back, DWRITE_TEXT_ALIGNMENT_CENTER);
            txt->layout_back->lpVtbl->SetParagraphAlignment(txt->layout_back, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        txt->dwrite_factory->lpVtbl->CreateTextLayout(txt->dwrite_factory, L"\uE946", 1, txt->dwrite_format_icons,
                                                      9999.0F, 9999.0F, &txt->layout_info);
        if (txt->layout_info)
        {
            txt->layout_info->lpVtbl->SetTextAlignment(txt->layout_info, DWRITE_TEXT_ALIGNMENT_CENTER);
            txt->layout_info->lpVtbl->SetParagraphAlignment(txt->layout_info, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // r_resize is done by the caller after setting window dimensions
    return 1;
}

void r_shutdown(GpuState *r, TextState *txt)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        SAFE_RELEASE(r->full_slots[i].srv);
        SAFE_RELEASE(r->full_slots[i].texture);
    }
    r->active_full_srv = NULL;
    SAFE_RELEASE(r->tex_pool.texture_array_srv);
    SAFE_RELEASE(r->tex_pool.texture_array);
    SAFE_RELEASE(r->sampler);
    SAFE_RELEASE(r->blend_state);
    SAFE_RELEASE(r->instance_buffer);
    SAFE_RELEASE(r->constant_buffer);
    SAFE_RELEASE(r->theme_buffer);
    SAFE_RELEASE(r->input_layout);
    SAFE_RELEASE(r->vs);
    SAFE_RELEASE(r->ps);
    SAFE_RELEASE(r->rtv);
    SAFE_RELEASE(r->swap_chain);
    SAFE_RELEASE(r->d3d_context);
    SAFE_RELEASE(txt->layout_back);
    SAFE_RELEASE(txt->layout_info);
    SAFE_RELEASE(txt->d2d_brush);
    SAFE_RELEASE(txt->dwrite_format_regular);
    SAFE_RELEASE(txt->dwrite_format_semibold);
    SAFE_RELEASE(txt->dwrite_format_small);
    SAFE_RELEASE(txt->dwrite_format_mono);
    SAFE_RELEASE(txt->dwrite_format_mono_small);
    SAFE_RELEASE(txt->dwrite_format_small_semibold);
    SAFE_RELEASE(txt->dwrite_format_icons);
    SAFE_RELEASE(txt->dwrite_format_icons_large);
    SAFE_RELEASE(txt->dwrite_factory);
    SAFE_RELEASE(txt->d2d_rtv);
    SAFE_RELEASE(txt->d2d_factory);
    SAFE_RELEASE(r->blur_tex);
    SAFE_RELEASE(r->blur_srv);
    SAFE_RELEASE(r->back_buffer);
    SAFE_RELEASE(r->d3d_device);
}

void r_resize(GpuState *r, TextState *txt, int width, int height, float dpi_scale)
{
    if (r->d3d_context)
    {
        r->d3d_context->lpVtbl->OMSetRenderTargets(r->d3d_context, 0, NULL, NULL);
        ID3D11ShaderResourceView *null_srvs[3] = {NULL, NULL, NULL};
        r->d3d_context->lpVtbl->PSSetShaderResources(r->d3d_context, 0, 3, null_srvs);
    }

    SAFE_RELEASE(r->rtv);
    SAFE_RELEASE(r->blur_tex);
    SAFE_RELEASE(r->blur_srv);
    SAFE_RELEASE(r->back_buffer);
    SAFE_RELEASE(txt->d2d_rtv);
    SAFE_RELEASE(txt->d2d_brush);

    if (!r->swap_chain)
        return;
    r->swap_chain->lpVtbl->ResizeBuffers(r->swap_chain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D *backBuffer;
    r->swap_chain->lpVtbl->GetBuffer(r->swap_chain, 0, &IID_ID3D11Texture2D, (void **) &backBuffer);
    if (backBuffer)
    {
        HRESULT hr_rtv =
            r->d3d_device->lpVtbl->CreateRenderTargetView(r->d3d_device, (ID3D11Resource *) backBuffer, NULL, &r->rtv);
        if (FAILED(hr_rtv))
            log_error(L"r_resize: CreateRenderTargetView failed (hr=0x%08X)", (unsigned int) hr_rtv);

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->lpVtbl->GetDesc(backBuffer, &desc);
        desc.MipLevels = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        desc.Usage = D3D11_USAGE_DEFAULT;

        r->d3d_device->lpVtbl->CreateTexture2D(r->d3d_device, &desc, NULL, &r->blur_tex);
        r->d3d_device->lpVtbl->CreateShaderResourceView(r->d3d_device, (ID3D11Resource *) r->blur_tex, NULL,
                                                        &r->blur_srv);

        r->back_buffer = backBuffer;
    }

    IDXGISurface *dxgi_surface = NULL;
    r->swap_chain->lpVtbl->GetBuffer(r->swap_chain, 0, &IID_IDXGISurface, (void **) &dxgi_surface);
    if (dxgi_surface && txt->d2d_factory)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = {0};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = 0.0F;
        props.dpiY = 0.0F;
        props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

        txt->d2d_factory->lpVtbl->CreateDxgiSurfaceRenderTarget(txt->d2d_factory, dxgi_surface, &props, &txt->d2d_rtv);
        dxgi_surface->lpVtbl->Release(dxgi_surface);

        if (txt->d2d_rtv)
        {
            D2D1_COLOR_F color = {1.0F, 1.0F, 1.0F, 1.0F};
            txt->d2d_rtv->lpVtbl->CreateSolidColorBrush(txt->d2d_rtv, &color, NULL, &txt->d2d_brush);
        }
    }

    if (r->d3d_context)
    {
        D3D11_VIEWPORT vp = {0};
        vp.Width = (float) width;
        vp.Height = (float) height;
        vp.MaxDepth = 1.0F;
        r->d3d_context->lpVtbl->RSSetViewports(r->d3d_context, 1, &vp);

        if (r->constant_buffer)
        {
            D3D11_MAPPED_SUBRESOURCE ms;
            r->d3d_context->lpVtbl->Map(r->d3d_context, (ID3D11Resource *) r->constant_buffer, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0, &ms);
            float *data = (float *) ms.pData;
            data[0] = (float) width;
            data[1] = (float) height;
            data[2] = dpi_scale;
            data[3] = 0.0F;
            r->d3d_context->lpVtbl->Unmap(r->d3d_context, (ID3D11Resource *) r->constant_buffer, 0);
        }
    }
}

void r_clear(GpuState *r_, float r, float g, float b)
{
    if (!r_->rtv)
        return;
    float color[4] = {r, g, b, 1.0F};
    r_->d3d_context->lpVtbl->ClearRenderTargetView(r_->d3d_context, r_->rtv, color);
}

void r_clear_theme(GpuState *r, const float bg[4])
{
    if (!r->rtv)
        return;
    r->d3d_context->lpVtbl->ClearRenderTargetView(r->d3d_context, r->rtv, bg);
}

void r_present(GpuState *r)
{
    if (r->swap_chain)
        r->swap_chain->lpVtbl->Present(r->swap_chain, 1, 0);
    r->tex_pool.frame_counter++;
}

void r_copy_backbuffer_for_blur(GpuState *r)
{
    if (r->back_buffer && r->blur_tex)
    {
        r->d3d_context->lpVtbl->CopySubresourceRegion(r->d3d_context, (ID3D11Resource *) r->blur_tex, 0, 0, 0, 0,
                                                      (ID3D11Resource *) r->back_buffer, 0, NULL);
        if (r->blur_srv)
            r->d3d_context->lpVtbl->GenerateMips(r->d3d_context, r->blur_srv);
    }
}
