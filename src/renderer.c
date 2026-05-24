// =========================================================================
// renderer.c — Direct3D 11 Renderer
// =========================================================================
#include "types.h"
#include <d3dcompiler.h>
#include <stdio.h>

// ── Shaders ─────────────────────────────────────────────────────────────
static const char* shader_src =
    "cbuffer Constants : register(b0) {\n"
    "    float2 window_size;\n"
    "    float2 padding;\n"
    "};\n"
    "struct VS_INPUT {\n"
    "    uint vertex_id : SV_VertexID;\n"
    "    float2 pos     : INST_POS;\n"
    "    float  size    : INST_SIZE;\n"
    "    int    tex_idx : INST_TEX;\n"
    "    float  opacity : INST_OPACITY;\n"
    "};\n"
    "struct PS_INPUT {\n"
    "    float4 pos     : SV_POSITION;\n"
    "    float2 uv      : TEXCOORD;\n"
    "    int    tex_idx : TEX_INDEX;\n"
    "    float  opacity : OPACITY;\n"
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
    "    return output;\n"
    "}\n"
    "Texture2DArray textures : register(t0);\n"
    "SamplerState samp : register(s0);\n"
    "float4 ps_main(PS_INPUT input) : SV_TARGET {\n"
    "    if (input.tex_idx < 0) return float4(0.2, 0.2, 0.2, input.opacity);\n"
    "    float4 color = textures.Sample(samp, float3(input.uv, input.tex_idx));\n"
    "    return float4(color.rgb, input.opacity);\n"
    "}\n";

int r_init(AppState *s)
{
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = s->hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
        NULL, 0, D3D11_SDK_VERSION, &scd, &s->swap_chain, &s->d3d_device, &feature_level, &s->d3d_context);
    if (FAILED(hr)) return 0;



    ID3DBlob* vs_blob = NULL;
    ID3DBlob* ps_blob = NULL;
    ID3DBlob* err = NULL;
    D3DCompile(shader_src, strlen(shader_src), NULL, NULL, NULL, "vs_main", "vs_5_0", 0, 0, &vs_blob, &err);
    if (err) { OutputDebugStringA(err->lpVtbl->GetBufferPointer(err)); err->lpVtbl->Release(err); }
    D3DCompile(shader_src, strlen(shader_src), NULL, NULL, NULL, "ps_main", "ps_5_0", 0, 0, &ps_blob, &err);
    if (err) { OutputDebugStringA(err->lpVtbl->GetBufferPointer(err)); err->lpVtbl->Release(err); }

    s->d3d_device->lpVtbl->CreateVertexShader(s->d3d_device, vs_blob->lpVtbl->GetBufferPointer(vs_blob), vs_blob->lpVtbl->GetBufferSize(vs_blob), NULL, &s->vs);
    s->d3d_device->lpVtbl->CreatePixelShader(s->d3d_device, ps_blob->lpVtbl->GetBufferPointer(ps_blob), ps_blob->lpVtbl->GetBufferSize(ps_blob), NULL, &s->ps);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "INST_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(InstanceData, x), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, size), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_TEX", 0, DXGI_FORMAT_R32_SINT, 0, offsetof(InstanceData, tex_index), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(InstanceData, opacity), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };
    s->d3d_device->lpVtbl->CreateInputLayout(s->d3d_device, ied, 4, vs_blob->lpVtbl->GetBufferPointer(vs_blob), vs_blob->lpVtbl->GetBufferSize(vs_blob), &s->input_layout);
    
    vs_blob->lpVtbl->Release(vs_blob);
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

    D3D11_SAMPLER_DESC sd = {0};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s->d3d_device->lpVtbl->CreateSamplerState(s->d3d_device, &sd, &s->sampler);

    D3D11_BLEND_DESC bld = {0};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
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
    if (s->tex_pool.texture_array) {
        s->d3d_device->lpVtbl->CreateShaderResourceView(s->d3d_device, (ID3D11Resource*)s->tex_pool.texture_array, NULL, &s->tex_pool.texture_array_srv);
    }

    for(int i=0; i<MAX_GPU_TEXTURES; i++) s->tex_pool.last_used[i] = -1;
    s->tex_pool.frame_counter = 0;

    r_resize(s);
    return 1;
}

void r_resize(AppState *s)
{
    if (s->rtv) { s->rtv->lpVtbl->Release(s->rtv); s->rtv = NULL; }
    if (!s->swap_chain) return;
    s->swap_chain->lpVtbl->ResizeBuffers(s->swap_chain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* backBuffer;
    s->swap_chain->lpVtbl->GetBuffer(s->swap_chain, 0, &IID_ID3D11Texture2D, (void**)&backBuffer);
    if (backBuffer) {
        s->d3d_device->lpVtbl->CreateRenderTargetView(s->d3d_device, (ID3D11Resource*)backBuffer, NULL, &s->rtv);
        backBuffer->lpVtbl->Release(backBuffer);
    }

    D3D11_VIEWPORT vp = {0};
    vp.Width = (float)s->window_width;
    vp.Height = (float)s->window_height;
    vp.MaxDepth = 1.0f;
    s->d3d_context->lpVtbl->RSSetViewports(s->d3d_context, 1, &vp);

    if (s->constant_buffer) {
        D3D11_MAPPED_SUBRESOURCE ms;
        s->d3d_context->lpVtbl->Map(s->d3d_context, (ID3D11Resource*)s->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        float* data = (float*)ms.pData;
        data[0] = (float)s->window_width;
        data[1] = (float)s->window_height;
        data[2] = 0; data[3] = 0;
        s->d3d_context->lpVtbl->Unmap(s->d3d_context, (ID3D11Resource*)s->constant_buffer, 0);
    }
}

void r_clear(AppState *s, float r, float g, float b)
{
    if (!s->rtv) return;
    float color[4] = {r, g, b, 1.0f};
    s->d3d_context->lpVtbl->ClearRenderTargetView(s->d3d_context, s->rtv, color);
}

void r_present(AppState *s)
{
    if (s->swap_chain) s->swap_chain->lpVtbl->Present(s->swap_chain, 1, 0);
    s->tex_pool.frame_counter++;
}

void r_evict_texture(AppState *s, int slot)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES) return;

    for (int i = 0; i < s->count; i++) {
        if (s->images[i].texture_slot == slot) {
            s->images[i].texture_slot = -1;
            if (s->images[i].state == 3) s->images[i].state = 1; // CACHED_ON_DISK
            break;
        }
    }

    s->tex_pool.last_used[slot] = -1;
}

int r_alloc_texture_slot(AppState *s, int image_index)
{
    (void)image_index;
    int best_slot = -1;
    int oldest_time = s->tex_pool.frame_counter + 1;

    for (int i = 0; i < MAX_GPU_TEXTURES; i++) {
        if (s->tex_pool.last_used[i] == -1) {
            best_slot = i;
            break;
        }
        if (s->tex_pool.last_used[i] < oldest_time) {
            oldest_time = s->tex_pool.last_used[i];
            best_slot = i;
        }
    }

    if (best_slot != -1 && s->tex_pool.last_used[best_slot] != -1) {
        r_evict_texture(s, best_slot);
    }

    s->tex_pool.last_used[best_slot] = s->tex_pool.frame_counter;
    return best_slot;
}

void r_upload_texture(AppState *s, int slot, void *bc1_data)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES) return;
    if (!s->tex_pool.texture_array) return;
    
    UINT pitch = (THUMB_SIZE / 4) * 8;
    s->d3d_context->lpVtbl->UpdateSubresource(s->d3d_context, (ID3D11Resource*)s->tex_pool.texture_array, slot, NULL, bc1_data, pitch, 0);
}

void r_draw_instances(AppState *s, void *instances, int count)
{
    if (count == 0) return;

    D3D11_MAPPED_SUBRESOURCE ms;
    s->d3d_context->lpVtbl->Map(s->d3d_context, (ID3D11Resource*)s->instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    memcpy(ms.pData, instances, sizeof(InstanceData) * count);
    s->d3d_context->lpVtbl->Unmap(s->d3d_context, (ID3D11Resource*)s->instance_buffer, 0);

    s->d3d_context->lpVtbl->OMSetRenderTargets(s->d3d_context, 1, &s->rtv, NULL);
    float blendFactor[4] = {0,0,0,0};
    s->d3d_context->lpVtbl->OMSetBlendState(s->d3d_context, s->blend_state, blendFactor, 0xFFFFFFFF);
    
    s->d3d_context->lpVtbl->IASetInputLayout(s->d3d_context, s->input_layout);
    s->d3d_context->lpVtbl->IASetPrimitiveTopology(s->d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    UINT stride = sizeof(InstanceData);
    UINT offset = 0;
    s->d3d_context->lpVtbl->IASetVertexBuffers(s->d3d_context, 0, 1, &s->instance_buffer, &stride, &offset);
    
    s->d3d_context->lpVtbl->VSSetShader(s->d3d_context, s->vs, NULL, 0);
    s->d3d_context->lpVtbl->VSSetConstantBuffers(s->d3d_context, 0, 1, &s->constant_buffer);
    s->d3d_context->lpVtbl->PSSetShader(s->d3d_context, s->ps, NULL, 0);
    s->d3d_context->lpVtbl->PSSetSamplers(s->d3d_context, 0, 1, &s->sampler);

    s->d3d_context->lpVtbl->PSSetShaderResources(s->d3d_context, 0, 1, &s->tex_pool.texture_array_srv);

    s->d3d_context->lpVtbl->DrawInstanced(s->d3d_context, 4, count, 0, 0);
}

void r_shutdown(AppState *s)
{
    if (s->tex_pool.texture_array_srv) s->tex_pool.texture_array_srv->lpVtbl->Release(s->tex_pool.texture_array_srv);
    if (s->tex_pool.texture_array) s->tex_pool.texture_array->lpVtbl->Release(s->tex_pool.texture_array);
    if (s->sampler) s->sampler->lpVtbl->Release(s->sampler);
    if (s->blend_state) s->blend_state->lpVtbl->Release(s->blend_state);
    if (s->instance_buffer) s->instance_buffer->lpVtbl->Release(s->instance_buffer);
    if (s->constant_buffer) s->constant_buffer->lpVtbl->Release(s->constant_buffer);
    if (s->input_layout) s->input_layout->lpVtbl->Release(s->input_layout);
    if (s->vs) s->vs->lpVtbl->Release(s->vs);
    if (s->ps) s->ps->lpVtbl->Release(s->ps);
    if (s->rtv) s->rtv->lpVtbl->Release(s->rtv);
    if (s->swap_chain) s->swap_chain->lpVtbl->Release(s->swap_chain);
    if (s->d3d_context) s->d3d_context->lpVtbl->Release(s->d3d_context);
    if (s->d3d_device) s->d3d_device->lpVtbl->Release(s->d3d_device);
}
