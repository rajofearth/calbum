// =========================================================================
// lib/gpu/texture.c — GPU texture pool management and instance drawing
// =========================================================================
#include "src/types.h"

void r_evict_texture(GpuState *r, DataState *data, int slot)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES)
        return;

    int img_idx = r->tex_pool.slot_owner[slot];
    if (img_idx >= 0 && img_idx < data->count && data->images[img_idx].texture_slot == slot)
    {
        data->images[img_idx].texture_slot = -1;
        data->images[img_idx].thumb_requested = 0;
        data->images[img_idx].state = IMG_STATE_READY;
    }
    r->tex_pool.slot_owner[slot] = -1;
    r->tex_pool.last_used[slot] = -1;
}

int r_alloc_texture_slot(GpuState *r, DataState *data, int image_index)
{
    int best_slot = -1;
    int oldest_time = r->tex_pool.frame_counter + 1;

    for (int i = 0; i < MAX_GPU_TEXTURES; i++)
    {
        if (r->tex_pool.last_used[i] == -1)
        {
            best_slot = i;
            break;
        }
        if (r->tex_pool.last_used[i] < oldest_time)
        {
            oldest_time = r->tex_pool.last_used[i];
            best_slot = i;
        }
    }

    if (best_slot != -1 && r->tex_pool.last_used[best_slot] != -1)
        r_evict_texture(r, data, best_slot);

    r->tex_pool.slot_owner[best_slot] = image_index;
    r->tex_pool.last_used[best_slot] = r->tex_pool.frame_counter;
    return best_slot;
}

void r_upload_texture(GpuState *r, int slot, void *bc1_data)
{
    if (slot < 0 || slot >= MAX_GPU_TEXTURES)
        return;
    if (!r->tex_pool.texture_array)
        return;

    UINT pitch = (THUMB_SIZE / 4) * 8;
    r->d3d_context->lpVtbl->UpdateSubresource(r->d3d_context, (ID3D11Resource *) r->tex_pool.texture_array, slot, NULL,
                                              bc1_data, pitch, 0);
}

void r_draw_instances(GpuState *r, void *instances, int count)
{
    if (count == 0)
        return;

    D3D11_MAPPED_SUBRESOURCE ms;
    r->d3d_context->lpVtbl->Map(r->d3d_context, (ID3D11Resource *) r->instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                &ms);
    memcpy(ms.pData, instances, sizeof(InstanceData) * count);
    r->d3d_context->lpVtbl->Unmap(r->d3d_context, (ID3D11Resource *) r->instance_buffer, 0);

    r->d3d_context->lpVtbl->OMSetRenderTargets(r->d3d_context, 1, &r->rtv, NULL);
    float blendFactor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    r->d3d_context->lpVtbl->OMSetBlendState(r->d3d_context, r->blend_state, blendFactor, 0xFFFFFFFF);

    r->d3d_context->lpVtbl->IASetInputLayout(r->d3d_context, r->input_layout);
    r->d3d_context->lpVtbl->IASetPrimitiveTopology(r->d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    UINT stride = sizeof(InstanceData);
    UINT offset = 0;
    r->d3d_context->lpVtbl->IASetVertexBuffers(r->d3d_context, 0, 1, &r->instance_buffer, &stride, &offset);

    r->d3d_context->lpVtbl->VSSetShader(r->d3d_context, r->vs, NULL, 0);
    r->d3d_context->lpVtbl->VSSetConstantBuffers(r->d3d_context, 0, 1, &r->constant_buffer);
    r->d3d_context->lpVtbl->PSSetShader(r->d3d_context, r->ps, NULL, 0);
    r->d3d_context->lpVtbl->PSSetConstantBuffers(r->d3d_context, 0, 1, &r->constant_buffer);
    r->d3d_context->lpVtbl->PSSetConstantBuffers(r->d3d_context, 1, 1, &r->theme_buffer);
    r->d3d_context->lpVtbl->PSSetSamplers(r->d3d_context, 0, 1, &r->sampler);

    ID3D11ShaderResourceView *srvs[3] = {r->tex_pool.texture_array_srv, r->active_full_srv, r->blur_srv};
    r->d3d_context->lpVtbl->PSSetShaderResources(r->d3d_context, 0, 3, srvs);

    r->d3d_context->lpVtbl->DrawInstanced(r->d3d_context, 4, count, 0, 0);
}
