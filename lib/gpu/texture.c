// =========================================================================
// lib/gpu/texture.c — GPU texture pool management and instance drawing
// =========================================================================
#include "src/types.h"

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
        r_evict_texture(s, best_slot);

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
