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

int r_init(AppState *s)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION,
                                   &s->d3d_device, &feature_level, &s->d3d_context);
    if (FAILED(hr))
        return 0;

    IDXGIDevice2 *dxgi_device;
    if (SUCCEEDED(s->d3d_device->lpVtbl->QueryInterface(s->d3d_device, &IID_IDXGIDevice2, (void **) &dxgi_device)))
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
                if (SUCCEEDED(dxgi_factory->lpVtbl->CreateSwapChainForHwnd(dxgi_factory, (IUnknown *) s->d3d_device,
                                                                           s->hwnd, &scd1, NULL, NULL, &sc1)))
                {
                    sc1->lpVtbl->QueryInterface(sc1, &IID_IDXGISwapChain, (void **) &s->swap_chain);
                    sc1->lpVtbl->Release(sc1);
                }
                dxgi_factory->lpVtbl->Release(dxgi_factory);
            }
            dxgi_adapter->lpVtbl->Release(dxgi_adapter);
        }
        dxgi_device->lpVtbl->Release(dxgi_device);
    }
    if (!s->swap_chain)
        return 0;

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

    s->d3d_device->lpVtbl->CreateVertexShader(s->d3d_device, vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                              vs_blob->lpVtbl->GetBufferSize(vs_blob), NULL, &s->vs);
    s->d3d_device->lpVtbl->CreatePixelShader(s->d3d_device, ps_blob->lpVtbl->GetBufferPointer(ps_blob),
                                             ps_blob->lpVtbl->GetBufferSize(ps_blob), NULL, &s->ps);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        {"INST_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(InstanceData, x), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(InstanceData, w), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_TEX", 0, DXGI_FORMAT_R32_SINT, 0, offsetof(InstanceData, tex_index), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, opacity), D3D11_INPUT_PER_INSTANCE_DATA,
         1},
        {"INST_RADIUS", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, corner_radius),
         D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    s->d3d_device->lpVtbl->CreateInputLayout(s->d3d_device, ied, 5, vs_blob->lpVtbl->GetBufferPointer(vs_blob),
                                             vs_blob->lpVtbl->GetBufferSize(vs_blob), &s->input_layout);

    if (vs_blob)
        vs_blob->lpVtbl->Release(vs_blob);
    if (ps_blob)
        ps_blob->lpVtbl->Release(ps_blob);

    D3D11_BUFFER_DESC bd = {0};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(InstanceData) * 4096;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    s->d3d_device->lpVtbl->CreateBuffer(s->d3d_device, &bd, NULL, &s->instance_buffer);

    bd.ByteWidth = 16;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    s->d3d_device->lpVtbl->CreateBuffer(s->d3d_device, &bd, NULL, &s->constant_buffer);

    bd.ByteWidth = sizeof(Theme);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    s->d3d_device->lpVtbl->CreateBuffer(s->d3d_device, &bd, NULL, &s->theme_buffer);

    D3D11_SAMPLER_DESC sd = {0};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s->d3d_device->lpVtbl->CreateSamplerState(s->d3d_device, &sd, &s->sampler);

    D3D11_BLEND_DESC bld = {0};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    s->d3d_device->lpVtbl->CreateBlendState(s->d3d_device, &bld, &s->blend_state);

    D3D11_TEXTURE2D_DESC tdesc = {0};
    tdesc.Width = THUMB_SIZE;
    tdesc.Height = THUMB_SIZE;
    tdesc.MipLevels = 1;
    tdesc.ArraySize = MAX_GPU_TEXTURES;
    tdesc.Format = DXGI_FORMAT_BC1_UNORM;
    tdesc.SampleDesc.Count = 1;
    tdesc.Usage = D3D11_USAGE_DEFAULT;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    s->d3d_device->lpVtbl->CreateTexture2D(s->d3d_device, &tdesc, NULL, &s->tex_pool.texture_array);
    if (s->tex_pool.texture_array)
    {
        s->d3d_device->lpVtbl->CreateShaderResourceView(s->d3d_device, (ID3D11Resource *) s->tex_pool.texture_array,
                                                        NULL, &s->tex_pool.texture_array_srv);
    }

    for (int i = 0; i < MAX_GPU_TEXTURES; i++)
    {
        s->tex_pool.last_used[i] = -1;
        s->tex_pool.slot_owner[i] = -1;
    }
    s->tex_pool.frame_counter = 0;

    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        s->full_slots[i].texture = NULL;
        s->full_slots[i].srv = NULL;
        s->full_slots[i].path[0] = L'\0';
        s->full_slots[i].w = 0;
        s->full_slots[i].h = 0;
    }
    s->active_full_srv = NULL;

    // Init D2D1 and DirectWrite
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **) &s->d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **) &s->dwrite_factory);

    if (s->dwrite_factory)
    {
        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"en-US", &s->dwrite_format_regular);

        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"en-US", &s->dwrite_format_semibold);

        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.5F, L"en-US", &s->dwrite_format_small);

        const wchar_t *mono_font = L"Consolas";
        IDWriteFontCollection *collection = NULL;
        if (SUCCEEDED(s->dwrite_factory->lpVtbl->GetSystemFontCollection(s->dwrite_factory, &collection, FALSE)) &&
            collection)
        {
            UINT32 index = 0;
            BOOL exists = FALSE;
            collection->lpVtbl->FindFamilyName(collection, L"Zed Mono", &index, &exists);
            if (exists)
                mono_font = L"Zed Mono";
            else
            {
                collection->lpVtbl->FindFamilyName(collection, L"Lilex", &index, &exists);
                if (exists)
                    mono_font = L"Lilex";
            }
            collection->lpVtbl->Release(collection);
        }

        s->dwrite_factory->lpVtbl->CreateTextFormat(s->dwrite_factory, mono_font, NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F,
                                                    L"en-US", &s->dwrite_format_mono);

        s->dwrite_factory->lpVtbl->CreateTextFormat(s->dwrite_factory, mono_font, NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0F,
                                                    L"en-US", &s->dwrite_format_mono_small);

        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe UI Variable", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.5F, L"en-US", &s->dwrite_format_small_semibold);

        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe Fluent Icons", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 18.0F, L"en-US", &s->dwrite_format_icons);

        s->dwrite_factory->lpVtbl->CreateTextFormat(
            s->dwrite_factory, L"Segoe Fluent Icons", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 48.0F, L"en-US", &s->dwrite_format_icons_large);

        s->dwrite_format = s->dwrite_format_regular;
    }

    // Create cached text layouts for static icon glyphs
    if (s->dwrite_factory && s->dwrite_format_icons)
    {
        s->dwrite_factory->lpVtbl->CreateTextLayout(s->dwrite_factory, L"\uE72B", 1, s->dwrite_format_icons, 9999.0F,
                                                    9999.0F, &s->layout_back);
        if (s->layout_back)
        {
            s->layout_back->lpVtbl->SetTextAlignment(s->layout_back, DWRITE_TEXT_ALIGNMENT_CENTER);
            s->layout_back->lpVtbl->SetParagraphAlignment(s->layout_back, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        s->dwrite_factory->lpVtbl->CreateTextLayout(s->dwrite_factory, L"\uE946", 1, s->dwrite_format_icons, 9999.0F,
                                                    9999.0F, &s->layout_info);
        if (s->layout_info)
        {
            s->layout_info->lpVtbl->SetTextAlignment(s->layout_info, DWRITE_TEXT_ALIGNMENT_CENTER);
            s->layout_info->lpVtbl->SetParagraphAlignment(s->layout_info, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    r_resize(s);
    return 1;
}

void r_shutdown(AppState *s)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        SAFE_RELEASE(s->full_slots[i].srv);
        SAFE_RELEASE(s->full_slots[i].texture);
    }
    s->active_full_srv = NULL;
    SAFE_RELEASE(s->tex_pool.texture_array_srv);
    SAFE_RELEASE(s->tex_pool.texture_array);
    SAFE_RELEASE(s->sampler);
    SAFE_RELEASE(s->blend_state);
    SAFE_RELEASE(s->instance_buffer);
    SAFE_RELEASE(s->constant_buffer);
    SAFE_RELEASE(s->theme_buffer);
    SAFE_RELEASE(s->input_layout);
    SAFE_RELEASE(s->vs);
    SAFE_RELEASE(s->ps);
    SAFE_RELEASE(s->rtv);
    SAFE_RELEASE(s->swap_chain);
    SAFE_RELEASE(s->d3d_context);
    SAFE_RELEASE(s->layout_back);
    SAFE_RELEASE(s->layout_info);
    SAFE_RELEASE(s->d2d_brush);
    SAFE_RELEASE(s->dwrite_format_regular);
    SAFE_RELEASE(s->dwrite_format_semibold);
    SAFE_RELEASE(s->dwrite_format_small);
    SAFE_RELEASE(s->dwrite_format_mono);
    SAFE_RELEASE(s->dwrite_format_mono_small);
    SAFE_RELEASE(s->dwrite_format_small_semibold);
    SAFE_RELEASE(s->dwrite_format_icons);
    SAFE_RELEASE(s->dwrite_format_icons_large);
    SAFE_RELEASE(s->dwrite_factory);
    SAFE_RELEASE(s->d2d_rtv);
    SAFE_RELEASE(s->d2d_factory);
    SAFE_RELEASE(s->blur_tex);
    SAFE_RELEASE(s->blur_srv);
    SAFE_RELEASE(s->back_buffer);
    SAFE_RELEASE(s->d3d_device);
}

void r_resize(AppState *s)
{
    if (s->d3d_context)
    {
        s->d3d_context->lpVtbl->OMSetRenderTargets(s->d3d_context, 0, NULL, NULL);
        ID3D11ShaderResourceView *null_srvs[3] = {NULL, NULL, NULL};
        s->d3d_context->lpVtbl->PSSetShaderResources(s->d3d_context, 0, 3, null_srvs);
    }

    SAFE_RELEASE(s->rtv);
    SAFE_RELEASE(s->blur_tex);
    SAFE_RELEASE(s->blur_srv);
    SAFE_RELEASE(s->back_buffer);
    SAFE_RELEASE(s->d2d_rtv);
    SAFE_RELEASE(s->d2d_brush);

    if (!s->swap_chain)
        return;
    s->swap_chain->lpVtbl->ResizeBuffers(s->swap_chain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D *backBuffer;
    s->swap_chain->lpVtbl->GetBuffer(s->swap_chain, 0, &IID_ID3D11Texture2D, (void **) &backBuffer);
    if (backBuffer)
    {
        s->d3d_device->lpVtbl->CreateRenderTargetView(s->d3d_device, (ID3D11Resource *) backBuffer, NULL, &s->rtv);

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->lpVtbl->GetDesc(backBuffer, &desc);
        desc.MipLevels = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        desc.Usage = D3D11_USAGE_DEFAULT;

        s->d3d_device->lpVtbl->CreateTexture2D(s->d3d_device, &desc, NULL, &s->blur_tex);
        s->d3d_device->lpVtbl->CreateShaderResourceView(s->d3d_device, (ID3D11Resource *) s->blur_tex, NULL,
                                                        &s->blur_srv);

        s->back_buffer = backBuffer;
    }

    IDXGISurface *dxgi_surface = NULL;
    s->swap_chain->lpVtbl->GetBuffer(s->swap_chain, 0, &IID_IDXGISurface, (void **) &dxgi_surface);
    if (dxgi_surface && s->d2d_factory)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = {0};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = 0.0F;
        props.dpiY = 0.0F;
        props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

        s->d2d_factory->lpVtbl->CreateDxgiSurfaceRenderTarget(s->d2d_factory, dxgi_surface, &props, &s->d2d_rtv);
        dxgi_surface->lpVtbl->Release(dxgi_surface);

        if (s->d2d_rtv)
        {
            D2D1_COLOR_F color = {1.0F, 1.0F, 1.0F, 1.0F};
            s->d2d_rtv->lpVtbl->CreateSolidColorBrush(s->d2d_rtv, &color, NULL, &s->d2d_brush);
        }
    }

    if (s->d3d_context)
    {
        D3D11_VIEWPORT vp = {0};
        vp.Width = (float) s->window_width;
        vp.Height = (float) s->window_height;
        vp.MaxDepth = 1.0F;
        s->d3d_context->lpVtbl->RSSetViewports(s->d3d_context, 1, &vp);

        if (s->constant_buffer)
        {
            D3D11_MAPPED_SUBRESOURCE ms;
            s->d3d_context->lpVtbl->Map(s->d3d_context, (ID3D11Resource *) s->constant_buffer, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0, &ms);
            float *data = (float *) ms.pData;
            data[0] = (float) s->window_width;
            data[1] = (float) s->window_height;
            data[2] = s->dpi_scale;
            data[3] = 0.0F;
            s->d3d_context->lpVtbl->Unmap(s->d3d_context, (ID3D11Resource *) s->constant_buffer, 0);
        }
    }
}

void r_clear(AppState *s, float r, float g, float b)
{
    if (!s->rtv)
        return;
    float color[4] = {r, g, b, 1.0F};
    s->d3d_context->lpVtbl->ClearRenderTargetView(s->d3d_context, s->rtv, color);
}

void r_clear_theme(AppState *s)
{
    if (!s->rtv)
        return;
    s->d3d_context->lpVtbl->ClearRenderTargetView(s->d3d_context, s->rtv, s->theme.bg);
}

void r_present(AppState *s)
{
    if (s->swap_chain)
        s->swap_chain->lpVtbl->Present(s->swap_chain, 1, 0);
    s->tex_pool.frame_counter++;
}

void r_copy_backbuffer_for_blur(AppState *s)
{
    if (s->back_buffer && s->blur_tex)
    {
        s->d3d_context->lpVtbl->CopySubresourceRegion(s->d3d_context, (ID3D11Resource *) s->blur_tex, 0, 0, 0, 0,
                                                      (ID3D11Resource *) s->back_buffer, 0, NULL);
        if (s->blur_srv)
            s->d3d_context->lpVtbl->GenerateMips(s->d3d_context, s->blur_srv);
    }
}
