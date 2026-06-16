// =========================================================================
// renderer.c — Direct3D 11 Renderer
// =========================================================================
#include "types.h"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stddef.h>

// ── Shaders ─────────────────────────────────────────────────────────────
static const char *shader_src =
    "cbuffer Constants : register(b0) {\n"
    "    float2 window_size;\n"
    "    float dpi_scale;\n"
    "    float padding;\n"
    "};\n"
    "cbuffer ThemeConstants : register(b1) {\n"
    "    float4 theme_bg;\n"
    "    float4 theme_panel;\n"
    "    float4 theme_border;\n"
    "    float4 theme_text_main;\n"
    "    float4 theme_text_muted;\n"
    "    float4 theme_accent;\n"
    "    float4 theme_scrollbar;\n"
    "};\n"
    "struct VS_INPUT {\n"
    "    uint vertex_id : SV_VertexID;\n"
    "    float2 pos     : INST_POS;\n"
    "    float2 size    : INST_SIZE;\n"
    "    int    tex_idx : INST_TEX;\n"
    "    float  opacity : INST_OPACITY;\n"
    "    float  radius  : INST_RADIUS;\n"
    "};\n"
    "struct PS_INPUT {\n"
    "    float4 pos     : SV_POSITION;\n"
    "    float2 uv      : TEXCOORD;\n"
    "    int    tex_idx : TEX_INDEX;\n"
    "    float  opacity : OPACITY;\n"
    "    float2 size    : SIZE;\n"
    "    float  radius  : RADIUS;\n"
    "};\n"
    "PS_INPUT vs_main(VS_INPUT input) {\n"
    "    PS_INPUT output;\n"
    "    float2 uvs[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };\n"
    "    float2 uv = uvs[input.vertex_id];\n"
    "    float2 pos = input.pos + uv * input.size;\n"
    "    pos.x = (pos.x / window_size.x) * 2.0 - 1.0;\n"
    "    pos.y = 1.0 - (pos.y / window_size.y) * 2.0;\n"
    "    output.pos = float4(pos, 0.0, 1.0);\n"
    "    output.uv = uv;\n"
    "    output.tex_idx = input.tex_idx;\n"
    "    output.opacity = input.opacity;\n"
    "    output.size = input.size;\n"
    "    output.radius = input.radius;\n"
    "    return output;\n"
    "}\n"
    "Texture2DArray textures : register(t0);\n"
    "Texture2D full_texture : register(t1);\n"
    "Texture2D blur_texture : register(t2);\n"
    "SamplerState samp : register(s0);\n"
    "float rounded_rect_sdf(float2 p, float2 size, float radius) {\n"
    "    float2 half_size = size * 0.5;\n"
    "    float2 d = abs(p - half_size) - half_size + radius;\n"
    "    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;\n"
    "}\n"
    "float4 ps_main(PS_INPUT input) : SV_TARGET {\n"
    "    float2 pixel_pos = input.uv * input.size;\n"
    "    float d = rounded_rect_sdf(pixel_pos, input.size, input.radius);\n"
    "    float alpha = (input.tex_idx == -6) ? clamp(-d / 12.0, 0.0, 1.0) * 0.35 : ((input.tex_idx == -8) ? clamp(0.5 "
    "- abs(d + 1.25) + 1.25, 0.0, 1.0) : clamp(0.5 - d, 0.0, 1.0));\n"
    "    if (alpha <= 0.0) discard;\n"
    "    float4 color;\n"
    "    if (input.tex_idx == -2) color = theme_border;\n"
    "    else if (input.tex_idx == -3) color = theme_panel;\n"
    "    else if (input.tex_idx == -4) color = theme_scrollbar;\n"
    "    else if (input.tex_idx == -5) color = full_texture.Sample(samp, input.uv);\n"
    "    else if (input.tex_idx == -6) color = float4(0.0, 0.0, 0.0, 1.0);\n"
    "    else if (input.tex_idx == -7) color = theme_accent;\n"
    "    else if (input.tex_idx == -8) color = theme_accent;\n"
    "    else if (input.tex_idx == -9) {\n"
    "        float2 uv = input.pos.xy / window_size;\n"
    "        float2 texel = (1.5 * dpi_scale) / window_size;\n"
    "        float4 sum = 0.0;\n"
    "        sum += blur_texture.SampleLevel(samp, uv, 2.5) * 0.16;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 1.0,  0.0) * texel, 2.5) * 0.08;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-1.0,  0.0) * texel, 2.5) * 0.08;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0,  1.0) * texel, 2.5) * 0.08;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0, -1.0) * texel, 2.5) * 0.08;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 1.0,  1.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-1.0,  1.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 1.0, -1.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-1.0, -1.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 2.0,  0.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-2.0,  0.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0,  2.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0, -2.0) * texel, 2.5) * 0.04;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 2.0,  2.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-2.0,  2.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 2.0, -2.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-2.0, -2.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 3.0,  0.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-3.0,  0.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0,  3.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 0.0, -3.0) * texel, 2.5) * 0.02;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 3.0,  3.0) * texel, 2.5) * 0.01;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-3.0,  3.0) * texel, 2.5) * 0.01;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2( 3.0, -3.0) * texel, 2.5) * 0.01;\n"
    "        sum += blur_texture.SampleLevel(samp, uv + float2(-3.0, -3.0) * texel, 2.5) * 0.01;\n"
    "        color.rgb = lerp(sum.rgb, theme_panel.rgb, 0.6);\n"
    "        color.a = 1.0;\n"
    "    }\n"
    "    else if (input.tex_idx < 0) color = theme_panel;\n"
    "    else color = textures.Sample(samp, float3(input.uv, input.tex_idx));\n"
    "    color.a *= input.opacity * alpha;\n"
    "    color.rgb *= color.a;\n"
    "    return color;\n"
    "}\n";

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

        // Find best monospace font available
        const wchar_t *mono_font = L"Consolas";
        IDWriteFontCollection *collection = NULL;
        if (SUCCEEDED(s->dwrite_factory->lpVtbl->GetSystemFontCollection(s->dwrite_factory, &collection, FALSE)) &&
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
                {
                    mono_font = L"Lilex";
                }
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

        // Default mapping to keep old code happy
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

void r_resize(AppState *s)
{
    if (s->d3d_context)
    {
        s->d3d_context->lpVtbl->OMSetRenderTargets(s->d3d_context, 0, NULL, NULL);
        ID3D11ShaderResourceView *null_srvs[3] = {NULL, NULL, NULL};
        s->d3d_context->lpVtbl->PSSetShaderResources(s->d3d_context, 0, 3, null_srvs);
    }

    if (s->rtv)
    {
        s->rtv->lpVtbl->Release(s->rtv);
        s->rtv = NULL;
    }
    if (s->blur_tex)
    {
        s->blur_tex->lpVtbl->Release(s->blur_tex);
        s->blur_tex = NULL;
    }
    if (s->blur_srv)
    {
        s->blur_srv->lpVtbl->Release(s->blur_srv);
        s->blur_srv = NULL;
    }
    if (s->back_buffer)
    {
        s->back_buffer->lpVtbl->Release(s->back_buffer);
        s->back_buffer = NULL;
    }

    if (s->d2d_rtv)
    {
        ((IUnknown *) s->d2d_rtv)->lpVtbl->Release((IUnknown *) s->d2d_rtv);
        s->d2d_rtv = NULL;
    }
    if (s->d2d_brush)
    {
        ((IUnknown *) s->d2d_brush)->lpVtbl->Release((IUnknown *) s->d2d_brush);
        s->d2d_brush = NULL;
    }

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
        desc.MipLevels = 0;                                                     // generate full mip chain
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; // required for GenerateMips
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; // required for GenerateMips
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

void r_evict_texture(AppState *s, int slot)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES)
        return;

    int img_idx = s->tex_pool.slot_owner[slot];
    if (img_idx >= 0 && img_idx < s->count && s->images[img_idx].texture_slot == slot)
    {
        s->images[img_idx].texture_slot = -1;
        s->images[img_idx].thumb_requested = 0;
        s->images[img_idx].state = IMG_STATE_READY;
    }
    s->tex_pool.slot_owner[slot] = -1;
    s->tex_pool.last_used[slot] = -1;
}

int r_alloc_texture_slot(AppState *s, int image_index)
{
    int best_slot = -1;
    int oldest_time = s->tex_pool.frame_counter + 1;

    for (int i = 0; i < MAX_GPU_TEXTURES; i++)
    {
        if (s->tex_pool.last_used[i] == -1)
        {
            best_slot = i;
            break;
        }
        if (s->tex_pool.last_used[i] < oldest_time)
        {
            oldest_time = s->tex_pool.last_used[i];
            best_slot = i;
        }
    }

    if (best_slot != -1 && s->tex_pool.last_used[best_slot] != -1)
    {
        r_evict_texture(s, best_slot);
    }

    s->tex_pool.slot_owner[best_slot] = image_index;
    s->tex_pool.last_used[best_slot] = s->tex_pool.frame_counter;
    return best_slot;
}

void r_upload_texture(AppState *s, int slot, void *bc1_data)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES)
        return;
    if (!s->tex_pool.texture_array)
        return;

    UINT pitch = (THUMB_SIZE / 4) * 8;
    s->d3d_context->lpVtbl->UpdateSubresource(s->d3d_context, (ID3D11Resource *) s->tex_pool.texture_array, slot, NULL,
                                              bc1_data, pitch, 0);
}

void r_copy_backbuffer_for_blur(AppState *s)
{
    if (s->back_buffer && s->blur_tex)
    {
        s->d3d_context->lpVtbl->CopySubresourceRegion(s->d3d_context, (ID3D11Resource *) s->blur_tex, 0, 0, 0, 0,
                                                      (ID3D11Resource *) s->back_buffer, 0, NULL);
        if (s->blur_srv)
        {
            s->d3d_context->lpVtbl->GenerateMips(s->d3d_context, s->blur_srv);
        }
    }
}

void r_draw_instances(AppState *s, void *instances, int count)
{
    if (count == 0)
        return;

    D3D11_MAPPED_SUBRESOURCE ms;
    s->d3d_context->lpVtbl->Map(s->d3d_context, (ID3D11Resource *) s->instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                &ms);
    memcpy(ms.pData, instances, sizeof(InstanceData) * count);
    s->d3d_context->lpVtbl->Unmap(s->d3d_context, (ID3D11Resource *) s->instance_buffer, 0);

    s->d3d_context->lpVtbl->OMSetRenderTargets(s->d3d_context, 1, &s->rtv, NULL);
    float blendFactor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    s->d3d_context->lpVtbl->OMSetBlendState(s->d3d_context, s->blend_state, blendFactor, 0xFFFFFFFF);

    s->d3d_context->lpVtbl->IASetInputLayout(s->d3d_context, s->input_layout);
    s->d3d_context->lpVtbl->IASetPrimitiveTopology(s->d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    UINT stride = sizeof(InstanceData);
    UINT offset = 0;
    s->d3d_context->lpVtbl->IASetVertexBuffers(s->d3d_context, 0, 1, &s->instance_buffer, &stride, &offset);

    s->d3d_context->lpVtbl->VSSetShader(s->d3d_context, s->vs, NULL, 0);
    s->d3d_context->lpVtbl->VSSetConstantBuffers(s->d3d_context, 0, 1, &s->constant_buffer);
    s->d3d_context->lpVtbl->PSSetShader(s->d3d_context, s->ps, NULL, 0);
    s->d3d_context->lpVtbl->PSSetConstantBuffers(s->d3d_context, 0, 1, &s->constant_buffer);
    s->d3d_context->lpVtbl->PSSetConstantBuffers(s->d3d_context, 1, 1, &s->theme_buffer);
    s->d3d_context->lpVtbl->PSSetSamplers(s->d3d_context, 0, 1, &s->sampler);

    ID3D11ShaderResourceView *srvs[3] = {s->tex_pool.texture_array_srv, s->active_full_srv, s->blur_srv};
    s->d3d_context->lpVtbl->PSSetShaderResources(s->d3d_context, 0, 3, srvs);

    s->d3d_context->lpVtbl->DrawInstanced(s->d3d_context, 4, count, 0, 0);
}

void r_draw_text_ext(AppState *s, const wchar_t *text, float x, float y, float w, float h,
                     struct IDWriteTextFormat *format, float color[4])
{
    if (!s->d2d_rtv || !format)
        return;

    D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
    s->d2d_brush->lpVtbl->SetColor(s->d2d_brush, &c);

    D2D1_RECT_F layoutRect = {x, y, x + w, y + h};
    ID2D1RenderTarget_DrawText(s->d2d_rtv, text, (UINT32) wcslen(text), format, &layoutRect,
                               (ID2D1Brush *) s->d2d_brush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void r_draw_text(AppState *s, const wchar_t *text, float x, float y, float w, float h)
{
    float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    r_draw_text_ext(s, text, x, y, w, h, s->dwrite_format, white);
}

void r_draw_text_aligned(AppState *s, const wchar_t *text,
                         /* x, y, w, h */ float x, float y, float w, float h, int align_x, int align_y,
                         struct IDWriteTextFormat *format, float color[4])
{
    if (!s->d2d_rtv || !s->dwrite_factory || !format)
        return;

    // Fast path: cached layouts for single-character icon glyphs
    if (wcslen(text) == 1 && s->dwrite_format_icons == format)
    {
        IDWriteTextLayout *layout = NULL;
        if (text[0] == 0xE72B)
        {
            layout = s->layout_back;
        }
        else if (text[0] == 0xE946)
        {
            layout = s->layout_info;
        }

        if (layout)
        {
            // Resize cached layout to current button dimensions so center alignment
            // positions the icon correctly (cached layouts are created at 9999x9999).
            layout->lpVtbl->SetMaxWidth(layout, w);
            layout->lpVtbl->SetMaxHeight(layout, h);
            D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
            s->d2d_brush->lpVtbl->SetColor(s->d2d_brush, &c);
            D2D1_POINT_2F origin = {x, y};
            s->d2d_rtv->lpVtbl->DrawTextLayout(s->d2d_rtv, origin, layout, (ID2D1Brush *) s->d2d_brush,
                                               D2D1_DRAW_TEXT_OPTIONS_NONE);
            return;
        }
    }

    // Fall through to CreateTextLayout for non-cached strings
    IDWriteTextLayout *layout = NULL;
    HRESULT hr = s->dwrite_factory->lpVtbl->CreateTextLayout(s->dwrite_factory, text, (UINT32) wcslen(text), format, w,
                                                             h, &layout);

    if (SUCCEEDED(hr) && layout)
    {
        layout->lpVtbl->SetTextAlignment(layout, (DWRITE_TEXT_ALIGNMENT) align_x);
        layout->lpVtbl->SetParagraphAlignment(layout, (DWRITE_PARAGRAPH_ALIGNMENT) align_y);

        D2D1_COLOR_F c = {color[0], color[1], color[2], color[3]};
        s->d2d_brush->lpVtbl->SetColor(s->d2d_brush, &c);

        D2D1_POINT_2F origin = {x, y};
        s->d2d_rtv->lpVtbl->DrawTextLayout(s->d2d_rtv, origin, layout, (ID2D1Brush *) s->d2d_brush,
                                           D2D1_DRAW_TEXT_OPTIONS_NONE);

        ((IUnknown *) layout)->lpVtbl->Release((IUnknown *) layout);
    }
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

FullImageSlot *r_get_full_image_slot(AppState *s, const wchar_t *path)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (s->full_slots[i].texture && _wcsicmp(s->full_slots[i].path, path) == 0)
        {
            return &s->full_slots[i];
        }
    }
    return NULL;
}

void r_free_full_image_slot(AppState *s, int i)
{
    if (i < 0 || i >= FULL_CACHE_SIZE)
        return;
    if (s->d3d_device)
    {
        if (s->full_slots[i].srv)
        {
            s->full_slots[i].srv->lpVtbl->Release(s->full_slots[i].srv);
            s->full_slots[i].srv = NULL;
        }
        if (s->full_slots[i].texture)
        {
            s->full_slots[i].texture->lpVtbl->Release(s->full_slots[i].texture);
            s->full_slots[i].texture = NULL;
        }
    }
    else
    {
        s->full_slots[i].srv = NULL;
        s->full_slots[i].texture = NULL;
    }
    s->full_slots[i].path[0] = L'\0';
    s->full_slots[i].w = 0;
    s->full_slots[i].h = 0;
}

int r_alloc_full_image_slot(AppState *s)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!s->full_slots[i].texture)
        {
            return i;
        }
    }

    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!fiv_is_in_strip(s, s->full_slots[i].path))
        {
            r_free_full_image_slot(s, i);
            return i;
        }
    }

    r_free_full_image_slot(s, 0);
    return 0;
}

void r_free_full_image(AppState *s)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        r_free_full_image_slot(s, i);
    }
    s->active_full_srv = NULL;
    if (s->d3d_context)
    {
        s->d3d_context->lpVtbl->Flush(s->d3d_context);
    }
}

int r_load_full_image(AppState *s, const wchar_t *path)
{
    FullImageSlot *slot = r_get_full_image_slot(s, path);
    if (slot)
    {
        s->active_full_srv = slot->srv;
        return 1;
    }

    int w = 0;
    int h = 0;
    void *rgba = il_load_full_image(path, &w, &h);
    if (!rgba)
        return 0;

    int slot_idx = r_alloc_full_image_slot(s);
    FullImageSlot *new_slot = &s->full_slots[slot_idx];

    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = (UINT) w;
    desc.Height = (UINT) h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data = {0};
    init_data.pSysMem = rgba;
    init_data.SysMemPitch = (UINT) (w * 4);

    HRESULT hr = s->d3d_device->lpVtbl->CreateTexture2D(s->d3d_device, &desc, &init_data, &new_slot->texture);
    if (FAILED(hr))
    {
        return 0;
    }

    hr = s->d3d_device->lpVtbl->CreateShaderResourceView(s->d3d_device, (ID3D11Resource *) new_slot->texture, NULL,
                                                         &new_slot->srv);
    if (FAILED(hr))
    {
        new_slot->texture->lpVtbl->Release(new_slot->texture);
        new_slot->texture = NULL;
        return 0;
    }

    wcsncpy(new_slot->path, path, MAX_PATH_LEN - 1);
    new_slot->path[MAX_PATH_LEN - 1] = L'\0';
    new_slot->w = w;
    new_slot->h = h;

    s->active_full_srv = new_slot->srv;
    return 1;
}

float r_measure_text_width(AppState *s, const wchar_t *text, struct IDWriteTextFormat *format)
{
    if (!s->dwrite_factory || !format || !text)
        return 0.0F;

    IDWriteTextLayout *layout = NULL;
    HRESULT hr = s->dwrite_factory->lpVtbl->CreateTextLayout(s->dwrite_factory, text, (UINT32) wcslen(text), format,
                                                             9999.0F, 9999.0F, &layout);

    float width = 0.0F;
    if (SUCCEEDED(hr) && layout)
    {
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(layout->lpVtbl->GetMetrics(layout, &metrics)))
        {
            width = metrics.width;
        }
        ((IUnknown *) layout)->lpVtbl->Release((IUnknown *) layout);
    }
    return width;
}
