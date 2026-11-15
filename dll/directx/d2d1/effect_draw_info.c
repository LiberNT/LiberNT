/*
 * Copyright 2018 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */


#include "effect_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static struct d2d_render_info *impl_from_ID2D1DrawInfo(ID2D1DrawInfo *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_render_info, ID2D1DrawInfo_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_QueryInterface(ID2D1DrawInfo *iface, REFIID iid,
        void **obj)
{
    TRACE("iface %p, iid %s, obj %p.\n", iface, debugstr_guid(iid), obj);

    if (IsEqualGUID(iid, &IID_ID2D1DrawInfo)
            || IsEqualGUID(iid, &IID_ID2D1RenderInfo)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *obj = iface;
        ID2D1DrawInfo_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_draw_info_AddRef(ID2D1DrawInfo *iface)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);
    ULONG refcount = InterlockedIncrement(&render_info->refcount);

    TRACE("iface %p refcount %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_draw_info_Release(ID2D1DrawInfo *iface)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);
    ULONG refcount = InterlockedDecrement(&render_info->refcount);

    TRACE("iface %p refcount %lu.\n", iface, refcount);

    if (!refcount)
        free(render_info);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetInputDescription(ID2D1DrawInfo *iface,
        UINT32 index, D2D1_INPUT_DESCRIPTION description)
{
    FIXME("iface %p, index %u stub.\n", iface, index);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetOutputBuffer(ID2D1DrawInfo *iface,
        D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_draw_info_SetCached(ID2D1DrawInfo *iface, BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_draw_info_SetInstructionCountHint(ID2D1DrawInfo *iface,
        UINT32 count)
{
    FIXME("iface %p, count %u stub.\n", iface, count);
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetPixelShaderConstantBuffer(ID2D1DrawInfo *iface,
        const BYTE *buffer, UINT32 size)
{
    FIXME("iface %p, buffer %p, size %u stub.\n", iface, buffer, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetResourceTexture(ID2D1DrawInfo *iface,
        UINT32 index, ID2D1ResourceTexture *texture)
{
    FIXME("iface %p, index %u, texture %p stub.\n", iface, index, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetVertexShaderConstantBuffer(ID2D1DrawInfo *iface,
        const BYTE *buffer, UINT32 size)
{
    FIXME("iface %p, buffer %p, size %u stub.\n", iface, buffer, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetPixelShader(ID2D1DrawInfo *iface,
        REFGUID id, D2D1_PIXEL_OPTIONS options)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);

    TRACE("iface %p, id %s, options %u.\n", iface, debugstr_guid(id), options);

    render_info->mask |= D2D_RENDER_INFO_PIXEL_SHADER;
    render_info->pixel_shader = *id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetVertexProcessing(ID2D1DrawInfo *iface,
        ID2D1VertexBuffer *buffer, D2D1_VERTEX_OPTIONS options,
        const D2D1_BLEND_DESCRIPTION *description, const D2D1_VERTEX_RANGE *range,
        const GUID *shader)
{
    FIXME("iface %p, buffer %p, options %#x, description %p, range %p, shader %s stub.\n",
            iface, buffer, options, description, range, debugstr_guid(shader));

    return E_NOTIMPL;
}

static const ID2D1DrawInfoVtbl d2d_draw_info_vtbl =
{
    d2d_draw_info_QueryInterface,
    d2d_draw_info_AddRef,
    d2d_draw_info_Release,
    d2d_draw_info_SetInputDescription,
    d2d_draw_info_SetOutputBuffer,
    d2d_draw_info_SetCached,
    d2d_draw_info_SetInstructionCountHint,
    d2d_draw_info_SetPixelShaderConstantBuffer,
    d2d_draw_info_SetResourceTexture,
    d2d_draw_info_SetVertexShaderConstantBuffer,
    d2d_draw_info_SetPixelShader,
    d2d_draw_info_SetVertexProcessing,
};

HRESULT d2d_effect_render_info_create(struct d2d_render_info **obj)
{
    struct d2d_render_info *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1DrawInfo_iface.lpVtbl = &d2d_draw_info_vtbl;
    object->refcount = 1;

    *obj = object;

    return S_OK;
}
