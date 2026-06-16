// =========================================================================
// lib/gpu/fullimage.c — Full-resolution image GPU cache management
// =========================================================================
#include "src/types.h"

FullImageSlot *r_get_full_image_slot(AppState *s, const wchar_t *path)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (s->gpu.full_slots[i].texture && _wcsicmp(s->gpu.full_slots[i].path, path) == 0)
            return &s->gpu.full_slots[i];
    }
    return NULL;
}

void r_free_full_image_slot(AppState *s, int i)
{
    if (i < 0 || i >= FULL_CACHE_SIZE)
        return;
    if (s->gpu.d3d_device)
    {
        SAFE_RELEASE(s->gpu.full_slots[i].srv);
        SAFE_RELEASE(s->gpu.full_slots[i].texture);
    }
    else
    {
        s->gpu.full_slots[i].srv = NULL;
        s->gpu.full_slots[i].texture = NULL;
    }
    s->gpu.full_slots[i].path[0] = L'\0';
    s->gpu.full_slots[i].w = 0;
    s->gpu.full_slots[i].h = 0;
}

int r_alloc_full_image_slot(AppState *s)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!s->gpu.full_slots[i].texture)
            return i;
    }

    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!fiv_is_in_strip(s, s->gpu.full_slots[i].path))
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
        r_free_full_image_slot(s, i);
    s->gpu.active_full_srv = NULL;
    if (s->gpu.d3d_context)
        s->gpu.d3d_context->lpVtbl->Flush(s->gpu.d3d_context);
}

int r_load_full_image(AppState *s, const wchar_t *path)
{
    FullImageSlot *slot = r_get_full_image_slot(s, path);
    if (slot)
    {
        s->gpu.active_full_srv = slot->srv;
        return 1;
    }

    int w = 0;
    int h = 0;
    void *rgba = il_load_full_image(path, &w, &h);
    if (!rgba)
        return 0;

    int slot_idx = r_alloc_full_image_slot(s);
    FullImageSlot *new_slot = &s->gpu.full_slots[slot_idx];

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

    if (FAILED(s->gpu.d3d_device->lpVtbl->CreateTexture2D(s->gpu.d3d_device, &desc, &init_data, &new_slot->texture)))
        return 0;

    if (FAILED(s->gpu.d3d_device->lpVtbl->CreateShaderResourceView(
            s->gpu.d3d_device, (ID3D11Resource *) new_slot->texture, NULL, &new_slot->srv)))
    {
        new_slot->texture->lpVtbl->Release(new_slot->texture);
        new_slot->texture = NULL;
        return 0;
    }

    wcsncpy(new_slot->path, path, MAX_PATH_LEN - 1);
    new_slot->path[MAX_PATH_LEN - 1] = L'\0';
    new_slot->w = w;
    new_slot->h = h;

    s->gpu.active_full_srv = new_slot->srv;
    return 1;
}
