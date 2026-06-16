// =========================================================================
// lib/gpu/fullimage.c — Full-resolution image GPU cache management
// =========================================================================
#include "src/types.h"

FullImageSlot *r_get_full_image_slot(GpuState *r, const wchar_t *path)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (r->full_slots[i].texture && _wcsicmp(r->full_slots[i].path, path) == 0)
            return &r->full_slots[i];
    }
    return NULL;
}

void r_free_full_image_slot(GpuState *r, int i)
{
    if (i < 0 || i >= FULL_CACHE_SIZE)
        return;
    if (r->d3d_device)
    {
        SAFE_RELEASE(r->full_slots[i].srv);
        SAFE_RELEASE(r->full_slots[i].texture);
    }
    else
    {
        r->full_slots[i].srv = NULL;
        r->full_slots[i].texture = NULL;
    }
    r->full_slots[i].path[0] = L'\0';
    r->full_slots[i].w = 0;
    r->full_slots[i].h = 0;
}

int r_alloc_full_image_slot(GpuState *r, DataState *data, ViewState *view, int grid_item_count)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!r->full_slots[i].texture)
            return i;
    }

    if (!data || !view)
    {
        r_free_full_image_slot(r, 0);
        return 0;
    }

    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        if (!fiv_is_in_strip(data, view, grid_item_count, r->full_slots[i].path))
        {
            r_free_full_image_slot(r, i);
            return i;
        }
    }

    r_free_full_image_slot(r, 0);
    return 0;
}

void r_free_full_image(GpuState *r)
{
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
        r_free_full_image_slot(r, i);
    r->active_full_srv = NULL;
    if (r->d3d_context)
        r->d3d_context->lpVtbl->Flush(r->d3d_context);
}

int r_load_full_image(GpuState *r, const wchar_t *path)
{
    FullImageSlot *slot = r_get_full_image_slot(r, path);
    if (slot)
    {
        r->active_full_srv = slot->srv;
        return 1;
    }

    int w = 0;
    int h = 0;
    void *rgba = il_load_full_image(path, &w, &h);
    if (!rgba)
        return 0;

    int slot_idx = r_alloc_full_image_slot(r, NULL, NULL, 0);
    if (slot_idx < 0)
        return 0;
    FullImageSlot *new_slot = &r->full_slots[slot_idx];

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

    HRESULT hr_tex = r->d3d_device->lpVtbl->CreateTexture2D(r->d3d_device, &desc, &init_data, &new_slot->texture);
    if (FAILED(hr_tex))
    {
        log_error(L"r_load_full_image: CreateTexture2D failed (%dx%d, hr=0x%08X)", w, h, (unsigned int) hr_tex);
        return 0;
    }

    HRESULT hr_srv = r->d3d_device->lpVtbl->CreateShaderResourceView(
        r->d3d_device, (ID3D11Resource *) new_slot->texture, NULL, &new_slot->srv);
    if (FAILED(hr_srv))
    {
        log_error(L"r_load_full_image: CreateShaderResourceView failed (hr=0x%08X)", (unsigned int) hr_srv);
        new_slot->texture->lpVtbl->Release(new_slot->texture);
        new_slot->texture = NULL;
        return 0;
    }

    wcsncpy(new_slot->path, path, MAX_PATH_LEN - 1);
    new_slot->path[MAX_PATH_LEN - 1] = L'\0';
    new_slot->w = w;
    new_slot->h = h;

    r->active_full_srv = new_slot->srv;
    return 1;
}
