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

static inline struct d2d_effect_context *impl_from_ID2D1EffectContext(ID2D1EffectContext *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_context, ID2D1EffectContext_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_QueryInterface(ID2D1EffectContext *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1EffectContext)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1EffectContext_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_context_AddRef(ID2D1EffectContext *iface)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    ULONG refcount = InterlockedIncrement(&effect_context->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_effect_context_Release(ID2D1EffectContext *iface)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    ULONG refcount = InterlockedDecrement(&effect_context->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        ID2D1DeviceContext6_Release(&effect_context->device_context->ID2D1DeviceContext6_iface);
        free(effect_context);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_effect_context_GetDpi(ID2D1EffectContext *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    ID2D1DeviceContext6_GetDpi(&effect_context->device_context->ID2D1DeviceContext6_iface, dpi_x, dpi_y);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateEffect(ID2D1EffectContext *iface,
        REFCLSID clsid, ID2D1Effect **effect)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, clsid %s, effect %p.\n", iface, debugstr_guid(clsid), effect);

    return ID2D1DeviceContext6_CreateEffect(&effect_context->device_context->ID2D1DeviceContext6_iface,
            clsid, effect);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_GetMaximumSupportedFeatureLevel(ID2D1EffectContext *iface,
        const D3D_FEATURE_LEVEL *levels, UINT32 level_count, D3D_FEATURE_LEVEL *max_level)
{
    FIXME("iface %p, levels %p, level_count %u, max_level %p stub!\n", iface, levels, level_count, max_level);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateTransformNodeFromEffect(ID2D1EffectContext *iface,
        ID2D1Effect *effect, ID2D1TransformNode **node)
{
    FIXME("iface %p, effect %p, node %p stub!\n", iface, effect, node);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBlendTransform(ID2D1EffectContext *iface,
        UINT32 num_inputs, const D2D1_BLEND_DESCRIPTION *description, ID2D1BlendTransform **transform)
{
    TRACE("iface %p, num_inputs %u, description %p, transform %p,\n", iface, num_inputs, description, transform);

    return d2d_blend_transform_create(num_inputs, description, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBorderTransform(ID2D1EffectContext *iface,
        D2D1_EXTEND_MODE mode_x, D2D1_EXTEND_MODE mode_y, ID2D1BorderTransform **transform)
{
    TRACE("iface %p, mode_x %#x, mode_y %#x, transform %p.\n", iface, mode_x, mode_y, transform);

    return d2d_border_transform_create(mode_x, mode_y, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateOffsetTransform(ID2D1EffectContext *iface,
        D2D1_POINT_2L offset, ID2D1OffsetTransform **transform)
{
    TRACE("iface %p, offset %s, transform %p.\n", iface, debug_d2d_point_2l(&offset), transform);

    return d2d_offset_transform_create(offset, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBoundsAdjustmentTransform(ID2D1EffectContext *iface,
        const D2D1_RECT_L *output_rect, ID2D1BoundsAdjustmentTransform **transform)
{
    TRACE("iface %p, output_rect %s, transform %p.\n", iface, debug_d2d_rect_l(output_rect), transform);

    return d2d_bounds_adjustment_transform_create(output_rect, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadPixelShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11PixelShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreatePixelShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create a pixel shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11PixelShader_Release(shader);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadVertexShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11VertexShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreateVertexShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create vertex shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11VertexShader_Release(shader);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadComputeShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11ComputeShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreateComputeShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create a compute shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11ComputeShader_Release(shader);

    return hr;
}

static BOOL STDMETHODCALLTYPE d2d_effect_context_IsShaderLoaded(ID2D1EffectContext *iface, REFGUID shader_id)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;

    TRACE("iface %p, shader_id %s.\n", iface, debugstr_guid(shader_id));

    return d2d_device_get_indexed_object(&device->shaders, shader_id, NULL);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateResourceTexture(ID2D1EffectContext *iface,
        const GUID *id,  const D2D1_RESOURCE_TEXTURE_PROPERTIES *texture_properties,
        const BYTE *data, const UINT32 *strides, UINT32 data_size, ID2D1ResourceTexture **texture)
{
    FIXME("iface %p, id %s, texture_properties %p, data %p, strides %p, data_size %u, texture %p stub!\n",
            iface, debugstr_guid(id), texture_properties, data, strides, data_size, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_FindResourceTexture(ID2D1EffectContext *iface,
        const GUID *id, ID2D1ResourceTexture **texture)
{
    FIXME("iface %p, id %s, texture %p stub!\n", iface, debugstr_guid(id), texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateVertexBuffer(ID2D1EffectContext *iface,
        const D2D1_VERTEX_BUFFER_PROPERTIES *buffer_properties, const GUID *id,
        const D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES *custom_buffer_properties,
        ID2D1VertexBuffer **buffer)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device_context *context = effect_context->device_context;
    HRESULT hr;

    FIXME("iface %p, buffer_properties %p, id %s, custom_buffer_properties %p, buffer %p stub!\n",
            iface, buffer_properties, debugstr_guid(id), custom_buffer_properties, buffer);

    if (id && d2d_device_get_indexed_object(&context->vertex_buffers, id, (IUnknown **)buffer))
        return S_OK;

    if (SUCCEEDED(hr = d2d_vertex_buffer_create(buffer)))
    {
        if (id)
            hr = d2d_device_add_indexed_object(&context->vertex_buffers, id, (IUnknown *)*buffer);
    }

    if (FAILED(hr))
        *buffer = NULL;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_FindVertexBuffer(ID2D1EffectContext *iface,
        const GUID *id, ID2D1VertexBuffer **buffer)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device_context *context = effect_context->device_context;

    TRACE("iface %p, id %s, buffer %p.\n", iface, debugstr_guid(id), buffer);

    if (!d2d_device_get_indexed_object(&context->vertex_buffers, id, (IUnknown **)buffer))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContext(ID2D1EffectContext *iface,
        D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 profile_size, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, space %#x, profile %p, profile_size %u, color_context %p.\n",
            iface, space, profile, profile_size, color_context);

    return ID2D1DeviceContext6_CreateColorContext(&effect_context->device_context->ID2D1DeviceContext6_iface,
            space, profile, profile_size, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContextFromFilename(ID2D1EffectContext *iface,
        const WCHAR *filename, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, filename %s, color_context %p.\n", iface, debugstr_w(filename), color_context);

    return ID2D1DeviceContext6_CreateColorContextFromFilename(&effect_context->device_context->ID2D1DeviceContext6_iface,
            filename, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContextFromWicColorContext(ID2D1EffectContext *iface,
        IWICColorContext *wic_color_context, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, wic_color_context %p, color_context %p.\n", iface, wic_color_context, color_context);

    return ID2D1DeviceContext6_CreateColorContextFromWicColorContext(&effect_context->device_context->ID2D1DeviceContext6_iface,
            wic_color_context, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CheckFeatureSupport(ID2D1EffectContext *iface,
        D2D1_FEATURE feature, void *data, UINT32 data_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    D3D11_FEATURE d3d11_feature;

    TRACE("iface %p, feature %#x, data %p, data_size %u.\n", iface, feature, data, data_size);

    /* Data structures are compatible. */
    switch (feature)
    {
        case D2D1_FEATURE_DOUBLES: d3d11_feature = D3D11_FEATURE_DOUBLES; break;
        case D2D1_FEATURE_D3D10_X_HARDWARE_OPTIONS: d3d11_feature = D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS; break;
        default:
            WARN("Unexpected feature index %d.\n", feature);
            return E_INVALIDARG;
    }

    return ID3D11Device1_CheckFeatureSupport(effect_context->device_context->d3d_device,
            d3d11_feature, data, data_size);
}

static BOOL STDMETHODCALLTYPE d2d_effect_context_IsBufferPrecisionSupported(ID2D1EffectContext *iface,
        D2D1_BUFFER_PRECISION precision)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, precision %u.\n", iface, precision);

    return ID2D1DeviceContext6_IsBufferPrecisionSupported(&effect_context->device_context->ID2D1DeviceContext6_iface,
            precision);
}

static const ID2D1EffectContextVtbl d2d_effect_context_vtbl =
{
    d2d_effect_context_QueryInterface,
    d2d_effect_context_AddRef,
    d2d_effect_context_Release,
    d2d_effect_context_GetDpi,
    d2d_effect_context_CreateEffect,
    d2d_effect_context_GetMaximumSupportedFeatureLevel,
    d2d_effect_context_CreateTransformNodeFromEffect,
    d2d_effect_context_CreateBlendTransform,
    d2d_effect_context_CreateBorderTransform,
    d2d_effect_context_CreateOffsetTransform,
    d2d_effect_context_CreateBoundsAdjustmentTransform,
    d2d_effect_context_LoadPixelShader,
    d2d_effect_context_LoadVertexShader,
    d2d_effect_context_LoadComputeShader,
    d2d_effect_context_IsShaderLoaded,
    d2d_effect_context_CreateResourceTexture,
    d2d_effect_context_FindResourceTexture,
    d2d_effect_context_CreateVertexBuffer,
    d2d_effect_context_FindVertexBuffer,
    d2d_effect_context_CreateColorContext,
    d2d_effect_context_CreateColorContextFromFilename,
    d2d_effect_context_CreateColorContextFromWicColorContext,
    d2d_effect_context_CheckFeatureSupport,
    d2d_effect_context_IsBufferPrecisionSupported,
};

void d2d_effect_context_init(struct d2d_effect_context *effect_context, struct d2d_device_context *device_context)
{
    effect_context->ID2D1EffectContext_iface.lpVtbl = &d2d_effect_context_vtbl;
    effect_context->refcount = 1;
    effect_context->device_context = device_context;
    ID2D1DeviceContext6_AddRef(&device_context->ID2D1DeviceContext6_iface);
}
