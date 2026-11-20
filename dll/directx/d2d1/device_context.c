/*
 * Copyright 2014 Henri Verbeet for CodeWeavers, 2026 Earzu Chan
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

#include "device_private.h"
#include <float.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

#define INITIAL_CLIP_STACK_SIZE 4

static void d2d_device_context_discard_layers(struct d2d_device_context *context);
static void STDMETHODCALLTYPE d2d_device_context_PushAxisAlignedClip(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE antialias_mode);

BOOL d2d_clip_stack_init(struct d2d_clip_stack *stack)
{
    if (!(stack->stack = malloc(INITIAL_CLIP_STACK_SIZE * sizeof(*stack->stack))))
        return FALSE;

    stack->size = INITIAL_CLIP_STACK_SIZE;
    stack->count = 0;

    return TRUE;
}

static void d2d_clip_stack_cleanup(struct d2d_clip_stack *stack)
{
    free(stack->stack);
}

static BOOL d2d_clip_stack_push(struct d2d_clip_stack *stack, const D2D1_RECT_F *rect)
{
    D2D1_RECT_F r;

    if (!d2d_array_reserve((void **)&stack->stack, &stack->size, stack->count + 1, sizeof(*stack->stack)))
        return FALSE;

    r = *rect;
    if (stack->count)
        d2d_rect_intersect(&r, &stack->stack[stack->count - 1]);
    stack->stack[stack->count++] = r;

    return TRUE;
}

static void d2d_clip_stack_pop(struct d2d_clip_stack *stack)
{
    if (!stack->count)
        return;
    --stack->count;
}

void d2d_device_context_set_error(struct d2d_device_context *context, HRESULT code)
{
    WARN("code %#lx.\n", code);
    context->error.code = code;
    context->error.tag1 = context->drawing_state.tag1;
    context->error.tag2 = context->drawing_state.tag2;
}

static inline struct d2d_device_context *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, IUnknown_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_inner_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1DeviceContext6)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext5)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext4)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext3)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext2)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext1)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext)
            || IsEqualGUID(iid, &IID_ID2D1RenderTarget)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1DeviceContext6_AddRef(&context->ID2D1DeviceContext6_iface);
        *out = &context->ID2D1DeviceContext6_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ID2D1GdiInteropRenderTarget))
    {
        ID2D1GdiInteropRenderTarget_AddRef(&context->ID2D1GdiInteropRenderTarget_iface);
        *out = &context->ID2D1GdiInteropRenderTarget_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_device_context_inner_AddRef(IUnknown *iface)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&context->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_device_context_inner_Release(IUnknown *iface)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&context->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        unsigned int i, j, k;

        d2d_clip_stack_cleanup(&context->clip_stack);
        d2d_device_context_discard_layers(context);
        for (i = 0; i < context->layer_stack.initialized; ++i)
        {
            if (context->layer_stack.stack[i].bitmap)
                ID2D1Bitmap1_Release(&context->layer_stack.stack[i].bitmap->ID2D1Bitmap1_iface);
        }
        free(context->layer_stack.stack);
        d2d_coverage_cleanup(&context->coverage);
        d2d_device_context_release_mask(context);
        IDWriteRenderingParams_Release(context->default_text_rendering_params);
        if (context->text_rendering_params)
            IDWriteRenderingParams_Release(context->text_rendering_params);
        d2d_device_context_release_blend_states(context);
        ID3D11RasterizerState_Release(context->rs);
        d2d_glyph_atlas_destroy(context->glyph_atlas[0]);
        d2d_glyph_atlas_destroy(context->glyph_atlas[1]);
        if (context->vb_stream.buffer)
            ID3D11Buffer_Release(context->vb_stream.buffer);
        if (context->ib_stream.buffer)
            ID3D11Buffer_Release(context->ib_stream.buffer);
        ID3D11Buffer_Release(context->vb);
        ID3D11Buffer_Release(context->ib);
        ID3D11Buffer_Release(context->ps_cb);
        ID3D11PixelShader_Release(context->ps);
        ID3D11Buffer_Release(context->vs_cb);
        for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
        {
            ID3D11VertexShader_Release(context->shape_resources[i].vs);
            ID3D11InputLayout_Release(context->shape_resources[i].il);
        }
        for (i = 0; i < D2D_SAMPLER_INTERPOLATION_MODE_COUNT; ++i)
        {
            for (j = 0; j < D2D_SAMPLER_EXTEND_MODE_COUNT; ++j)
            {
                for (k = 0; k < D2D_SAMPLER_EXTEND_MODE_COUNT; ++k)
                {
                    if (context->sampler_states[i][j][k])
                        ID3D11SamplerState_Release(context->sampler_states[i][j][k]);
                }
            }
        }
        d2d_device_context_state_out(context);
        if (context->d3d_state)
            ID3DDeviceContextState_Release(context->d3d_state);
        if (context->target.object)
            IUnknown_Release(context->target.object);
        ID3D11Device1_Release(context->d3d_device);
        ID2D1Factory_Release(context->factory);
        ID2D1Device6_Release(&context->device->ID2D1Device6_iface);
        d2d_device_indexed_objects_clear(&context->vertex_buffers);
        free(context);
    }

    return refcount;
}

const struct IUnknownVtbl d2d_device_context_inner_unknown_vtbl =
{
    d2d_device_context_inner_QueryInterface,
    d2d_device_context_inner_AddRef,
    d2d_device_context_inner_Release,
};

static HRESULT STDMETHODCALLTYPE d2d_device_context_QueryInterface(ID2D1DeviceContext6 *iface,
        REFIID iid, void **out)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(context->outer_unknown, iid, out);
}

ULONG STDMETHODCALLTYPE d2d_device_context_AddRef(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(context->outer_unknown);
}

ULONG STDMETHODCALLTYPE d2d_device_context_Release(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(context->outer_unknown);
}

static void STDMETHODCALLTYPE d2d_device_context_GetFactory(ID2D1DeviceContext6 *iface, ID2D1Factory **factory)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    *factory = render_target->factory;
    ID2D1Factory_AddRef(*factory);
}

HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmap(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, size {%u, %u}, src_data %p, pitch %u, desc %p, bitmap %p.\n",
            iface, size.width, size.height, src_data, pitch, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        bitmap_desc.bitmapOptions = 0;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create(context, size, src_data, pitch, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapFromWicBitmap(ID2D1DeviceContext6 *iface,
        IWICBitmapSource *bitmap_source, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, bitmap_source %p, desc %p, bitmap %p.\n",
            iface, bitmap_source, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        bitmap_desc.bitmapOptions = 0;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_from_wic_bitmap(context, bitmap_source, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSharedBitmap(ID2D1DeviceContext6 *iface,
        REFIID iid, void *data, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, iid %s, data %p, desc %p, bitmap %p.\n",
            iface, debugstr_guid(iid), data, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        if (IsEqualIID(iid, &IID_IDXGISurface) || IsEqualIID(iid, &IID_IDXGISurface1)
                || IsEqualIID(iid, &IID_IDXGISurface2))
            bitmap_desc.bitmapOptions = d2d_get_bitmap_options_for_surface(data);
        else
            bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_shared(context, iid, data, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapBrush(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, bitmap_brush_desc %p, brush_desc %p, brush %p.\n",
            iface, bitmap, bitmap_brush_desc, brush_desc, brush);

    if (SUCCEEDED(hr = d2d_bitmap_brush_create(context->factory, bitmap, (const D2D1_BITMAP_BRUSH_PROPERTIES1 *)bitmap_brush_desc,
            brush_desc, &object)))
        *brush = (ID2D1BitmapBrush *)&object->ID2D1Brush_iface;

    return hr;
}

HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSolidColorBrush(ID2D1DeviceContext6 *iface,
        const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *desc, ID2D1SolidColorBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, color %p, desc %p, brush %p.\n", iface, color, desc, brush);

    if (SUCCEEDED(hr = d2d_solid_color_brush_create(render_target->factory, color, desc, &object)))
        *brush = (ID2D1SolidColorBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateGradientStopCollection(ID2D1DeviceContext6 *iface,
        const D2D1_GRADIENT_STOP *stops, UINT32 stop_count, D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend_mode,
        ID2D1GradientStopCollection **gradient)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_gradient *object;
    HRESULT hr;

    TRACE("iface %p, stops %p, stop_count %u, gamma %#x, extend_mode %#x, gradient %p.\n",
            iface, stops, stop_count, gamma, extend_mode, gradient);

    if (SUCCEEDED(hr = d2d_gradient_create(render_target->factory, render_target->d3d_device,
            stops, stop_count, gamma, extend_mode, &object)))
        *gradient = &object->ID2D1GradientStopCollection_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLinearGradientBrush(ID2D1DeviceContext6 *iface,
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1LinearGradientBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p.\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    if (SUCCEEDED(hr = d2d_linear_gradient_brush_create(render_target->factory, gradient_brush_desc, brush_desc,
        gradient, &object)))
        *brush = (ID2D1LinearGradientBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateRadialGradientBrush(ID2D1DeviceContext6 *iface,
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1RadialGradientBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p.\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    if (SUCCEEDED(hr = d2d_radial_gradient_brush_create(render_target->factory,
            gradient_brush_desc, brush_desc, gradient, &object)))
        *brush = (ID2D1RadialGradientBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateCompatibleRenderTarget(ID2D1DeviceContext6 *iface,
        const D2D1_SIZE_F *size, const D2D1_SIZE_U *pixel_size, const D2D1_PIXEL_FORMAT *format,
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget **rt)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap_render_target *object;
    HRESULT hr;

    TRACE("iface %p, size %p, pixel_size %p, format %p, options %#x, render_target %p.\n",
            iface, size, pixel_size, format, options, rt);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_bitmap_render_target_init(object, render_target, size, pixel_size,
            format, options)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *rt = &object->ID2D1BitmapRenderTarget_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLayer(ID2D1DeviceContext6 *iface,
        const D2D1_SIZE_F *size, ID2D1Layer **layer)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_layer *object;
    HRESULT hr;

    TRACE("iface %p, size %p, layer %p.\n", iface, size, layer);

    if (SUCCEEDED(hr = d2d_layer_create(render_target->factory, size, &object)))
        *layer = &object->ID2D1Layer_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateMesh(ID2D1DeviceContext6 *iface, ID2D1Mesh **mesh)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_mesh *object;
    HRESULT hr;

    TRACE("iface %p, mesh %p.\n", iface, mesh);

    if (SUCCEEDED(hr = d2d_mesh_create(render_target->factory, &object)))
        *mesh = &object->ID2D1Mesh_iface;

    return hr;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTransform(ID2D1DeviceContext6 *iface,
        const D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_transform(context->target.command_list, transform);

    context->drawing_state.transform = *transform;
}

void STDMETHODCALLTYPE d2d_device_context_GetTransform(ID2D1DeviceContext6 *iface,
        D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    *transform = render_target->drawing_state.transform;
}

static void STDMETHODCALLTYPE d2d_device_context_SetAntialiasMode(ID2D1DeviceContext6 *iface,
        D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, antialias_mode %#x stub!\n", iface, antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_antialias_mode(context->target.command_list, antialias_mode);

    context->drawing_state.antialiasMode = antialias_mode;
}

static D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_device_context_GetAntialiasMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.antialiasMode;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTextAntialiasMode(ID2D1DeviceContext6 *iface,
        D2D1_TEXT_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, antialias_mode %#x.\n", iface, antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_text_antialias_mode(context->target.command_list, antialias_mode);

    context->drawing_state.textAntialiasMode = antialias_mode;
}

static D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_device_context_GetTextAntialiasMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.textAntialiasMode;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTextRenderingParams(ID2D1DeviceContext6 *iface,
        IDWriteRenderingParams *text_rendering_params)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_text_rendering_params(context->target.command_list, text_rendering_params);

    if (text_rendering_params)
        IDWriteRenderingParams_AddRef(text_rendering_params);
    if (context->text_rendering_params)
        IDWriteRenderingParams_Release(context->text_rendering_params);
    context->text_rendering_params = text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_GetTextRenderingParams(ID2D1DeviceContext6 *iface,
        IDWriteRenderingParams **text_rendering_params)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if ((*text_rendering_params = render_target->text_rendering_params))
        IDWriteRenderingParams_AddRef(*text_rendering_params);
}

static void STDMETHODCALLTYPE d2d_device_context_SetTags(ID2D1DeviceContext6 *iface, D2D1_TAG tag1, D2D1_TAG tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, tag1 %s, tag2 %s.\n", iface, wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_tags(context->target.command_list, tag1, tag2);

    context->drawing_state.tag1 = tag1;
    context->drawing_state.tag2 = tag2;
}

static void STDMETHODCALLTYPE d2d_device_context_GetTags(ID2D1DeviceContext6 *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    *tag1 = render_target->drawing_state.tag1;
    *tag2 = render_target->drawing_state.tag2;
}

static void STDMETHODCALLTYPE d2d_device_context_PushAxisAlignedClip(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE antialias_mode);

static BOOL d2d_layer_bitmap_matches(const struct d2d_bitmap *bitmap, const struct d2d_bitmap *target,
        const struct d2d_device_context *context)
{
    return bitmap->pixel_size.width == target->pixel_size.width
            && bitmap->pixel_size.height == target->pixel_size.height
            && bitmap->format.format == target->format.format
            && bitmap->dpi_x == context->desc.dpiX && bitmap->dpi_y == context->desc.dpiY;
}

static void d2d_device_context_layer_release(struct d2d_device_context_layer *layer)
{
    if (layer->parameters.geometricMask)
        ID2D1Geometry_Release(layer->parameters.geometricMask);
    if (layer->parameters.opacityBrush)
        ID2D1Brush_Release(layer->parameters.opacityBrush);
    layer->parameters.geometricMask = NULL;
    layer->parameters.opacityBrush = NULL;
}

static void d2d_device_context_compose_layer(struct d2d_device_context *context,
        const struct d2d_device_context_layer *layer, BOOL apply_parameters)
{
    D2D1_BITMAP_BRUSH_PROPERTIES1 bitmap_brush_desc;
    ID2D1TransformedGeometry *transformed = NULL;
    ID2D1RectangleGeometry *rectangle = NULL;
    struct d2d_brush *brush, *opacity_brush;
    D2D1_MATRIX_3X2_F transform;
    D2D1_BRUSH_PROPERTIES brush_desc;
    D2D1_PRIMITIVE_BLEND primitive_blend;
    D2D1_ANTIALIAS_MODE antialias_mode;
    D2D1_RECT_F rect;
    HRESULT hr;

    if (!d2d_matrix_invert(&brush_desc.transform, &layer->transform))
        return;
    brush_desc.opacity = apply_parameters ? layer->parameters.opacity : 1.0f;
    bitmap_brush_desc.extendModeX = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.extendModeY = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.interpolationMode = D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    if (FAILED(hr = d2d_bitmap_brush_create(context->factory, (ID2D1Bitmap *)&layer->bitmap->ID2D1Bitmap1_iface,
            &bitmap_brush_desc, &brush_desc, &brush)))
    {
        ERR("Failed to create layer brush, hr %#lx.\n", hr);
        return;
    }

    if (apply_parameters && layer->parameters.geometricMask)
    {
        hr = ID2D1Factory_CreateTransformedGeometry(context->factory, layer->parameters.geometricMask,
                &layer->parameters.maskTransform, &transformed);
    }
    else
    {
        d2d_rect_set(&rect, 0.0f, 0.0f, layer->bitmap->pixel_size.width * 96.0f / layer->bitmap->dpi_x,
                layer->bitmap->pixel_size.height * 96.0f / layer->bitmap->dpi_y);
        if (SUCCEEDED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, &rect, &rectangle)))
        {
            hr = ID2D1Factory_CreateTransformedGeometry(context->factory, (ID2D1Geometry *)rectangle,
                    &brush_desc.transform, &transformed);
            ID2D1RectangleGeometry_Release(rectangle);
        }
    }

    if (SUCCEEDED(hr))
    {
        opacity_brush = apply_parameters ? unsafe_impl_from_ID2D1Brush(layer->parameters.opacityBrush) : NULL;
        transform = context->drawing_state.transform;
        context->drawing_state.transform = layer->transform;
        antialias_mode = context->drawing_state.antialiasMode;
        context->drawing_state.antialiasMode = apply_parameters && layer->parameters.geometricMask
                ? layer->parameters.maskAntialiasMode : D2D1_ANTIALIAS_MODE_ALIASED;
        primitive_blend = context->drawing_state.primitiveBlend;
        context->drawing_state.primitiveBlend = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
        d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)transformed),
                brush, opacity_brush);
        context->drawing_state.primitiveBlend = primitive_blend;
        context->drawing_state.antialiasMode = antialias_mode;
        context->drawing_state.transform = transform;
        ID2D1TransformedGeometry_Release(transformed);
    }
    else
    {
        ERR("Failed to create layer geometry, hr %#lx.\n", hr);
    }

    ID2D1Brush_Release(&brush->ID2D1Brush_iface);
}

static void d2d_device_context_push_layer(struct d2d_device_context *context,
        const D2D1_LAYER_PARAMETERS1 *parameters)
{
    static const float transparent[4];
    struct d2d_layer_stack *stack = &context->layer_stack;
    struct d2d_device_context_layer *layer;
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    ID3D11DeviceContext *d3d_context;
    struct d2d_bitmap *target;
    HRESULT hr;

    if (!d2d_array_reserve((void **)&stack->stack, &stack->size, stack->count + 1, sizeof(*stack->stack)))
    {
        d2d_device_context_set_error(context, E_OUTOFMEMORY);
        return;
    }
    if (stack->count == stack->initialized)
        memset(&stack->stack[stack->initialized++], 0, sizeof(*stack->stack));

    layer = &stack->stack[stack->count++];
    layer->parameters = *parameters;
    layer->transform = context->drawing_state.transform;
    layer->target = NULL;
    layer->active = FALSE;
    layer->clip = FALSE;
    if (layer->parameters.geometricMask)
        ID2D1Geometry_AddRef(layer->parameters.geometricMask);
    if (layer->parameters.opacityBrush)
        ID2D1Brush_AddRef(layer->parameters.opacityBrush);

    if (context->target.type != D2D_TARGET_BITMAP || FAILED(context->error.code))
        return;

    target = context->target.bitmap;
    if (layer->bitmap && !d2d_layer_bitmap_matches(layer->bitmap, target, context))
    {
        ID2D1Bitmap1_Release(&layer->bitmap->ID2D1Bitmap1_iface);
        layer->bitmap = NULL;
    }
    if (!layer->bitmap)
    {
        bitmap_desc.pixelFormat.format = target->format.format;
        bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmap_desc.dpiX = context->desc.dpiX;
        bitmap_desc.dpiY = context->desc.dpiY;
        bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        bitmap_desc.colorContext = NULL;
        if (FAILED(hr = d2d_bitmap_create(context, target->pixel_size, NULL, 0, &bitmap_desc, &layer->bitmap)))
        {
            WARN("Failed to create layer bitmap, hr %#lx.\n", hr);
            layer->bitmap = NULL;
            d2d_device_context_set_error(context, hr);
            return;
        }
    }

    if (context->cs)
        EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    if (parameters->layerOptions & D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND)
        ID3D11DeviceContext_CopyResource(d3d_context, layer->bitmap->resource, target->resource);
    else
        ID3D11DeviceContext_ClearRenderTargetView(d3d_context, layer->bitmap->rtv, transparent);
    ID3D11DeviceContext_Release(d3d_context);
    if (context->cs)
        LeaveCriticalSection(context->cs);

    if (parameters->contentBounds.left > -FLT_MAX || parameters->contentBounds.top > -FLT_MAX
            || parameters->contentBounds.right < FLT_MAX || parameters->contentBounds.bottom < FLT_MAX)
    {
        d2d_device_context_PushAxisAlignedClip(&context->ID2D1DeviceContext6_iface, &parameters->contentBounds,
                D2D1_ANTIALIAS_MODE_ALIASED);
        layer->clip = TRUE;
    }

    layer->target = target;
    layer->active = TRUE;
    context->target.bitmap = layer->bitmap;
}

static void d2d_device_context_pop_layer(struct d2d_device_context *context, BOOL apply_parameters)
{
    struct d2d_device_context_layer *layer = &context->layer_stack.stack[--context->layer_stack.count];

    if (layer->active)
    {
        context->target.bitmap = layer->target;
        d2d_device_context_compose_layer(context, layer, apply_parameters);
        if (layer->clip)
            d2d_clip_stack_pop(&context->clip_stack);
    }
    d2d_device_context_layer_release(layer);
}

static void d2d_device_context_discard_layers(struct d2d_device_context *context)
{
    struct d2d_layer_stack *stack = &context->layer_stack;

    while (stack->count)
    {
        struct d2d_device_context_layer *layer = &stack->stack[--stack->count];

        if (layer->active)
        {
            context->target.bitmap = layer->target;
            if (layer->clip)
                d2d_clip_stack_pop(&context->clip_stack);
        }
        d2d_device_context_layer_release(layer);
    }
}

static void STDMETHODCALLTYPE d2d_device_context_PushLayer(ID2D1DeviceContext6 *iface,
        const D2D1_LAYER_PARAMETERS *layer_parameters, ID2D1Layer *layer)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_LAYER_PARAMETERS1 parameters;

    TRACE("iface %p, layer_parameters %p, layer %p.\n", iface, layer_parameters, layer);

    memcpy(&parameters, layer_parameters, sizeof(*layer_parameters));
    parameters.layerOptions = D2D1_LAYER_OPTIONS1_NONE;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_push_layer(context->target.command_list, context, &parameters);
    else
        d2d_device_context_push_layer(context, &parameters);
}

static void STDMETHODCALLTYPE d2d_device_context_PopLayer(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_pop_layer(context->target.command_list);
    else if (!context->layer_stack.count)
        d2d_device_context_set_error(context, D2DERR_PUSH_POP_UNBALANCED);
    else
        d2d_device_context_pop_layer(context, TRUE);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_Flush(ID2D1DeviceContext6 *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, tag1 %p, tag2 %p stub!\n", iface, tag1, tag2);

    d2d_device_context_state_out(context);
    if (context->ops && context->ops->device_context_present)
        context->ops->device_context_present(context->outer_unknown);

    return S_OK;
}

static void STDMETHODCALLTYPE d2d_device_context_SaveDrawingState(ID2D1DeviceContext6 *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_state_block *state_block_impl;

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    if (!(state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block))) return;
    state_block_impl->drawing_state = render_target->drawing_state;
    if (render_target->text_rendering_params)
        IDWriteRenderingParams_AddRef(render_target->text_rendering_params);
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_Release(state_block_impl->text_rendering_params);
    state_block_impl->text_rendering_params = render_target->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_RestoreDrawingState(ID2D1DeviceContext6 *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_state_block *state_block_impl;

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    if (!(state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block))) return;
    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        struct d2d_command_list *command_list = context->target.command_list;

        if (context->drawing_state.antialiasMode != state_block_impl->drawing_state.antialiasMode)
            d2d_command_list_set_antialias_mode(command_list, state_block_impl->drawing_state.antialiasMode);
        d2d_command_list_set_text_antialias_mode(command_list, state_block_impl->drawing_state.textAntialiasMode);
        d2d_command_list_set_tags(command_list, state_block_impl->drawing_state.tag1, state_block_impl->drawing_state.tag2);
        d2d_command_list_set_transform(command_list, &state_block_impl->drawing_state.transform);
        d2d_command_list_set_primitive_blend(command_list, state_block_impl->drawing_state.primitiveBlend);
        d2d_command_list_set_unit_mode(command_list, state_block_impl->drawing_state.unitMode);
        d2d_command_list_set_text_rendering_params(command_list, state_block_impl->text_rendering_params);
    }

    context->drawing_state = state_block_impl->drawing_state;
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_AddRef(state_block_impl->text_rendering_params);
    if (context->text_rendering_params)
        IDWriteRenderingParams_Release(context->text_rendering_params);
    context->text_rendering_params = state_block_impl->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_PushAxisAlignedClip(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_RECT_F transformed_rect;
    float x_scale, y_scale;
    D2D1_POINT_2F point;

    TRACE("iface %p, clip_rect %s, antialias_mode %#x.\n", iface, debug_d2d_rect_f(clip_rect), antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_push_clip(context->target.command_list, clip_rect, antialias_mode);

    if (antialias_mode != D2D1_ANTIALIAS_MODE_ALIASED)
        FIXME("Ignoring antialias_mode %#x.\n", antialias_mode);

    x_scale = context->desc.dpiX / 96.0f;
    y_scale = context->desc.dpiY / 96.0f;
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->top * y_scale);
    d2d_rect_set(&transformed_rect, point.x, point.y, point.x, point.y);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->top * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);

    if (!d2d_clip_stack_push(&context->clip_stack, &transformed_rect))
        WARN("Failed to push clip rect.\n");
}

static void STDMETHODCALLTYPE d2d_device_context_PopAxisAlignedClip(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_pop_clip(context->target.command_list);

    d2d_clip_stack_pop(&context->clip_stack);
}

static void STDMETHODCALLTYPE d2d_device_context_BeginDraw(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_begin_draw(context->target.command_list, context);

    memset(&context->error, 0, sizeof(context->error));
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_EndDraw(ID2D1DeviceContext6 *iface,
        D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    HRESULT hr;

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        FIXME("Unimplemented for command list target.\n");
        return E_NOTIMPL;
    }

    if (context->layer_stack.count)
    {
        while (context->layer_stack.count)
            d2d_device_context_pop_layer(context, FALSE);
        if (SUCCEEDED(context->error.code))
            d2d_device_context_set_error(context, D2DERR_PUSH_POP_UNBALANCED);
    }

    d2d_device_context_state_out(context);
    if (tag1)
        *tag1 = context->error.tag1;
    if (tag2)
        *tag2 = context->error.tag2;

    if (context->ops && context->ops->device_context_present)
    {
        if (FAILED(hr = context->ops->device_context_present(context->outer_unknown)))
            context->error.code = hr;
    }

    return context->error.code;
}

static D2D1_PIXEL_FORMAT * STDMETHODCALLTYPE d2d_device_context_GetPixelFormat(ID2D1DeviceContext6 *iface,
        D2D1_PIXEL_FORMAT *format)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, format %p.\n", iface, format);

    *format = render_target->desc.pixelFormat;
    return format;
}

static void STDMETHODCALLTYPE d2d_device_context_SetDpi(ID2D1DeviceContext6 *iface, float dpi_x, float dpi_y)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, dpi_x %.8e, dpi_y %.8e.\n", iface, dpi_x, dpi_y);

    if (dpi_x == 0.0f && dpi_y == 0.0f)
    {
        dpi_x = 96.0f;
        dpi_y = 96.0f;
    }
    else if (dpi_x <= 0.0f || dpi_y <= 0.0f)
        return;

    render_target->desc.dpiX = dpi_x;
    render_target->desc.dpiY = dpi_y;
}

static void STDMETHODCALLTYPE d2d_device_context_GetDpi(ID2D1DeviceContext6 *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    *dpi_x = render_target->desc.dpiX;
    *dpi_y = render_target->desc.dpiY;
}

static D2D1_SIZE_F * STDMETHODCALLTYPE d2d_device_context_GetSize(ID2D1DeviceContext6 *iface, D2D1_SIZE_F *size)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, size %p.\n", iface, size);

    size->width = render_target->pixel_size.width / (render_target->desc.dpiX / 96.0f);
    size->height = render_target->pixel_size.height / (render_target->desc.dpiY / 96.0f);
    return size;
}

static D2D1_SIZE_U * STDMETHODCALLTYPE d2d_device_context_GetPixelSize(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U *pixel_size)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, pixel_size %p.\n", iface, pixel_size);

    *pixel_size = render_target->pixel_size;
    return pixel_size;
}

static UINT32 STDMETHODCALLTYPE d2d_device_context_GetMaximumBitmapSize(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    switch (ID3D11Device1_GetFeatureLevel(context->d3d_device))
    {
        case D3D_FEATURE_LEVEL_9_1:
        case D3D_FEATURE_LEVEL_9_2:
            return D3D_FL9_1_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        case D3D_FEATURE_LEVEL_9_3:
            return D3D_FL9_3_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        case D3D_FEATURE_LEVEL_10_0:
        case D3D_FEATURE_LEVEL_10_1:
            return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        default:
            return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    }
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsSupported(ID2D1DeviceContext6 *iface, const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmap(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch,
        const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, size {%u, %u}, src_data %p, pitch %u, desc %p, bitmap %p.\n",
            iface, size.width, size.height, src_data, pitch, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create(context, size, src_data, pitch, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmapFromWicBitmap(
        ID2D1DeviceContext6 *iface, IWICBitmapSource *bitmap_source,
        const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, bitmap_source %p, desc %p, bitmap %p.\n", iface, bitmap_source, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create_from_wic_bitmap(context, bitmap_source, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContext(ID2D1DeviceContext6 *iface,
        D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 profile_size, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, space %#x, profile %p, profile_size %u, color_context %p stub!\n",
            iface, space, profile, profile_size, color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromFilename(ID2D1DeviceContext6 *iface,
        const WCHAR *filename, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, filename %s, color_context %p stub!\n", iface, debugstr_w(filename), color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromWicColorContext(ID2D1DeviceContext6 *iface,
        IWICColorContext *wic_color_context, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, wic_color_context %p, color_context %p stub!\n", iface, wic_color_context, color_context);

    return E_NOTIMPL;
}

static BOOL d2d_bitmap_check_options_with_surface(unsigned int options, unsigned int surface_options)
{
    switch (options)
    {
        case D2D1_BITMAP_OPTIONS_NONE:
        case D2D1_BITMAP_OPTIONS_TARGET:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE:
        case D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ:
        case D2D1_BITMAP_OPTIONS_CANNOT_DRAW:
            break;
        default:
            WARN("Invalid bitmap options %#x.\n", options);
            return FALSE;
    }

    if (options && (options & D2D1_BITMAP_OPTIONS_TARGET) != (surface_options & D2D1_BITMAP_OPTIONS_TARGET))
        return FALSE;
    if (!(options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW) && (surface_options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW))
        return FALSE;
    if (options & D2D1_BITMAP_OPTIONS_TARGET)
    {
        if (options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE && !(surface_options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE))
            return FALSE;
        return TRUE;
    }

    if (options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW)
    {
        if (!(surface_options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW))
            return FALSE;

        if (options & D2D1_BITMAP_OPTIONS_CPU_READ && !(surface_options & D2D1_BITMAP_OPTIONS_CPU_READ))
            return FALSE;
    }

    return TRUE;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapFromDxgiSurface(ID2D1DeviceContext6 *iface,
        IDXGISurface *surface, const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    unsigned int surface_options;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, surface %p, desc %p, bitmap %p.\n", iface, surface, desc, bitmap);

    surface_options = d2d_get_bitmap_options_for_surface(surface);

    if (desc)
    {
        if (!d2d_bitmap_check_options_with_surface(desc->bitmapOptions, surface_options))
        {
            WARN("Incompatible bitmap options %#x, surface options %#x.\n",
                    desc->bitmapOptions, surface_options);
            return E_INVALIDARG;
        }
    }
    else
    {
        DXGI_SURFACE_DESC surface_desc;

        if (FAILED(hr = IDXGISurface_GetDesc(surface, &surface_desc)))
        {
            WARN("Failed to get surface desc, hr %#lx.\n", hr);
            return hr;
        }

        memset(&bitmap_desc, 0, sizeof(bitmap_desc));
        bitmap_desc.pixelFormat.format = surface_desc.Format;
        bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmap_desc.bitmapOptions = surface_options;
        desc = &bitmap_desc;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_shared(context, &IID_IDXGISurface, surface, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateEffect(ID2D1DeviceContext6 *iface,
        REFCLSID effect_id, ID2D1Effect **effect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, effect_id %s, effect %p.\n", iface, debugstr_guid(effect_id), effect);

    return d2d_effect_create(context, effect_id, effect);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateGradientStopCollection(
        ID2D1DeviceContext6 *iface, const D2D1_GRADIENT_STOP *stops, UINT32 stop_count,
        D2D1_COLOR_SPACE preinterpolation_space, D2D1_COLOR_SPACE postinterpolation_space,
        D2D1_BUFFER_PRECISION buffer_precision, D2D1_EXTEND_MODE extend_mode,
        D2D1_COLOR_INTERPOLATION_MODE color_interpolation_mode, ID2D1GradientStopCollection1 **gradient)
{
    FIXME("iface %p, stops %p, stop_count %u, preinterpolation_space %#x, postinterpolation_space %#x, "
            "buffer_precision %#x, extend_mode %#x, color_interpolation_mode %#x, gradient %p stub!\n",
            iface, stops, stop_count, preinterpolation_space, postinterpolation_space,
            buffer_precision, extend_mode, color_interpolation_mode, gradient);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageBrush(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, const D2D1_IMAGE_BRUSH_PROPERTIES *image_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1ImageBrush **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, image %p, image_brush_desc %p, brush_desc %p, brush %p.\n", iface, image, image_brush_desc,
            brush_desc, brush);

    if (SUCCEEDED(hr = d2d_image_brush_create(context->factory, image, image_brush_desc,
            brush_desc, &object)))
        *brush = (ID2D1ImageBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmapBrush(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES1 *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush1 **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, bitmap_brush_desc %p, brush_desc %p, brush %p.\n", iface, bitmap, bitmap_brush_desc,
            brush_desc, brush);

    if (SUCCEEDED(hr = d2d_bitmap_brush_create(context->factory, bitmap, bitmap_brush_desc, brush_desc, &object)))
        *brush = (ID2D1BitmapBrush1 *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateCommandList(ID2D1DeviceContext6 *iface,
        ID2D1CommandList **command_list)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_command_list *object;
    HRESULT hr;

    TRACE("iface %p, command_list %p.\n", iface, command_list);

    if (SUCCEEDED(hr = d2d_command_list_create(context->factory, &object)))
        *command_list = &object->ID2D1CommandList_iface;

    return hr;
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsDxgiFormatSupported(ID2D1DeviceContext6 *iface, DXGI_FORMAT format)
{
    FIXME("iface %p, format %#x stub!\n", iface, format);

    return FALSE;
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsBufferPrecisionSupported(ID2D1DeviceContext6 *iface,
        D2D1_BUFFER_PRECISION buffer_precision)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    DXGI_FORMAT format;
    UINT support = 0;
    HRESULT hr;

    TRACE("iface %p, buffer_precision %u.\n", iface, buffer_precision);

    switch (buffer_precision)
    {
        case D2D1_BUFFER_PRECISION_8BPC_UNORM: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB: format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
        case D2D1_BUFFER_PRECISION_16BPC_UNORM: format = DXGI_FORMAT_R16G16B16A16_UNORM; break;
        case D2D1_BUFFER_PRECISION_16BPC_FLOAT: format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case D2D1_BUFFER_PRECISION_32BPC_FLOAT: format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        default:
            WARN("Unexpected precision %u.\n", buffer_precision);
            return FALSE;
    }

    if (FAILED(hr = ID3D11Device1_CheckFormatSupport(context->d3d_device, format, &support)))
    {
        WARN("Format support check failed, hr %#lx.\n", hr);
    }

    return !!(support & D3D11_FORMAT_SUPPORT_BUFFER);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetImageLocalBounds(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, D2D1_RECT_F *local_bounds)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D_SIZE_U pixel_size;
    ID2D1Bitmap *bitmap;
    D2D_SIZE_F size;

    TRACE("iface %p, image %p, local_bounds %p.\n", iface, image, local_bounds);

    if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        local_bounds->left = 0.0f;
        local_bounds->top  = 0.0f;
        switch (context->drawing_state.unitMode)
        {
            case D2D1_UNIT_MODE_DIPS:
                size = ID2D1Bitmap_GetSize(bitmap);
                local_bounds->right  = size.width;
                local_bounds->bottom = size.height;
                break;

            case D2D1_UNIT_MODE_PIXELS:
                pixel_size = ID2D1Bitmap_GetPixelSize(bitmap);
                local_bounds->right  = pixel_size.width;
                local_bounds->bottom = pixel_size.height;
                break;

            default:
                WARN("Unknown unit mode %#x.\n", context->drawing_state.unitMode);
                break;
        }
        ID2D1Bitmap_Release(bitmap);

        return S_OK;
    }
    else
    {
        FIXME("Unable to get local bounds of image %p.\n", image);

        return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetImageWorldBounds(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, D2D1_RECT_F *world_bounds)
{
    FIXME("iface %p, image %p, world_bounds %p stub!\n", iface, image, world_bounds);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_GetDevice(ID2D1DeviceContext6 *iface, ID2D1Device **device)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID2D1Device *)&context->device->ID2D1Device6_iface;
    ID2D1Device_AddRef(*device);
}

static void d2d_device_context_reset_target(struct d2d_device_context *context)
{
    if (!context->target.object)
        return;

    d2d_device_context_discard_layers(context);
    IUnknown_Release(context->target.object);
    memset(&context->target, 0, sizeof(context->target));

    /* Note that DPI settings are kept. */
    memset(&context->desc.pixelFormat, 0, sizeof(context->desc.pixelFormat));
    memset(&context->pixel_size, 0, sizeof(context->pixel_size));
}

static void STDMETHODCALLTYPE d2d_device_context_SetTarget(ID2D1DeviceContext6 *iface, ID2D1Image *target)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_command_list *command_list_impl;
    struct d2d_bitmap *bitmap_impl;
    ID2D1CommandList *command_list;
    ID2D1Bitmap *bitmap;

    TRACE("iface %p, target %p.\n", iface, target);

    if (!target)
    {
        d2d_device_context_reset_target(context);
        return;
    }

    if (SUCCEEDED(ID2D1Image_QueryInterface(target, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        bitmap_impl = unsafe_impl_from_ID2D1Bitmap(bitmap);

        if (!(bitmap_impl->options & D2D1_BITMAP_OPTIONS_TARGET))
        {
            ID2D1Bitmap_Release(bitmap);
            d2d_device_context_set_error(context, D2DERR_INVALID_TARGET);
            return;
        }

        d2d_device_context_reset_target(context);

        /* Set sizes and pixel format. */
        context->pixel_size = bitmap_impl->pixel_size;
        context->desc.pixelFormat = bitmap_impl->format;
        context->target.bitmap = bitmap_impl;
        context->target.object = target;
        context->target.type = D2D_TARGET_BITMAP;
    }
    else if (SUCCEEDED(ID2D1Image_QueryInterface(target, &IID_ID2D1CommandList, (void **)&command_list)))
    {
        command_list_impl = unsafe_impl_from_ID2D1CommandList(command_list);

        d2d_device_context_reset_target(context);

        context->target.command_list = command_list_impl;
        context->target.object = target;
        context->target.type = D2D_TARGET_COMMAND_LIST;
    }
    else
    {
        WARN("Unsupported target type.\n");
    }
}

static void STDMETHODCALLTYPE d2d_device_context_GetTarget(ID2D1DeviceContext6 *iface, ID2D1Image **target)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, target %p.\n", iface, target);

    *target = context->target.object ? context->target.object : NULL;
    if (*target)
        ID2D1Image_AddRef(*target);
}

static void STDMETHODCALLTYPE d2d_device_context_SetRenderingControls(ID2D1DeviceContext6 *iface, const D2D1_RENDERING_CONTROLS *rendering_controls)
{
    FIXME("iface %p, rendering_controls %p stub!\n", iface, rendering_controls);
}

static void STDMETHODCALLTYPE d2d_device_context_GetRenderingControls(ID2D1DeviceContext6 *iface, D2D1_RENDERING_CONTROLS *rendering_controls)
{
    FIXME("iface %p, rendering_controls %p stub!\n", iface, rendering_controls);
}

static void STDMETHODCALLTYPE d2d_device_context_SetPrimitiveBlend(ID2D1DeviceContext6 *iface,
        D2D1_PRIMITIVE_BLEND primitive_blend)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, primitive_blend %u.\n", iface, primitive_blend);

    if (primitive_blend > D2D1_PRIMITIVE_BLEND_MAX)
    {
        WARN("Unknown blend mode %u.\n", primitive_blend);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_primitive_blend(context->target.command_list, primitive_blend);

    context->drawing_state.primitiveBlend = primitive_blend;
}

static D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE d2d_device_context_GetPrimitiveBlend(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return context->drawing_state.primitiveBlend;
}

static void STDMETHODCALLTYPE d2d_device_context_SetUnitMode(ID2D1DeviceContext6 *iface, D2D1_UNIT_MODE unit_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, unit_mode %#x.\n", iface, unit_mode);

    if (unit_mode != D2D1_UNIT_MODE_DIPS && unit_mode != D2D1_UNIT_MODE_PIXELS)
    {
        WARN("Unknown unit mode %#x.\n", unit_mode);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_unit_mode(context->target.command_list, unit_mode);

    context->drawing_state.unitMode = unit_mode;
}

static D2D1_UNIT_MODE STDMETHODCALLTYPE d2d_device_context_GetUnitMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return context->drawing_state.unitMode;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGdiMetafile(ID2D1DeviceContext6 *iface,
        ID2D1GdiMetafile *metafile, const D2D1_POINT_2F *target_offset)
{
    FIXME("iface %p, metafile %p, target_offset %s stub!\n",
            iface, metafile, debug_d2d_point_2f(target_offset));
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_PushLayer(ID2D1DeviceContext6 *iface,
        const D2D1_LAYER_PARAMETERS1 *layer_parameters, ID2D1Layer *layer)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, layer_parameters %p, layer %p.\n", iface, layer_parameters, layer);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_push_layer(context->target.command_list, context, layer_parameters);
    else
        d2d_device_context_push_layer(context, layer_parameters);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_InvalidateEffectInputRectangle(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, UINT32 input, const D2D1_RECT_F *input_rect)
{
    FIXME("iface %p, effect %p, input %u, input_rect %s stub!\n",
            iface, effect, input, debug_d2d_rect_f(input_rect));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectInvalidRectangleCount(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, UINT32 *rect_count)
{
    FIXME("iface %p, effect %p, rect_count %p stub!\n", iface, effect, rect_count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectInvalidRectangles(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, D2D1_RECT_F *rectangles, UINT32 rect_count)
{
    FIXME("iface %p, effect %p, rectangles %p, rect_count %u stub!\n", iface, effect, rectangles, rect_count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectRequiredInputRectangles(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, const D2D1_RECT_F *image_rect, const D2D1_EFFECT_INPUT_DESCRIPTION *desc,
        D2D1_RECT_F *input_rect, UINT32 input_count)
{
    FIXME("iface %p, effect %p, image_rect %s, desc %p, input_rect %p, input_count %u stub!\n",
            iface, effect, debug_d2d_rect_f(image_rect), desc, input_rect, input_count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateFilledGeometryRealization(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, float tolerance, ID2D1GeometryRealization **realization)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *object;
    HRESULT hr;

    TRACE("iface %p, geometry %p, tolerance %.8e, realization %p.\n", iface, geometry, tolerance,
            realization);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_geometry_realization_init(object, context->factory, geometry)))
    {
        WARN("Failed to initialise geometry realization, hr %#lx.\n", hr);
        free(object);
        return hr;
    }
    object->filled = true;

    TRACE("Created geometry realization %p.\n", object);
    *realization = &object->ID2D1GeometryRealization_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateStrokedGeometryRealization(
        ID2D1DeviceContext6 *iface, ID2D1Geometry *geometry, float tolerance, float stroke_width,
        ID2D1StrokeStyle *stroke_style, ID2D1GeometryRealization **realization)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *object;
    HRESULT hr;

    TRACE("iface %p, geometry %p, tolerance %.8e, stroke_width %.8e, stroke_style %p, realization %p.\n",
            iface, geometry, tolerance, stroke_width, stroke_style, realization);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_geometry_realization_init(object, context->factory, geometry)))
    {
        WARN("Failed to initialise geometry realization, hr %#lx.\n", hr);
        free(object);
        return hr;
    }
    object->stroke_width = stroke_width;
    object->stroke_style = stroke_style;
    if (object->stroke_style)
        ID2D1StrokeStyle_AddRef(object->stroke_style);

    TRACE("Created geometry realization %p.\n", object);
    *realization = &object->ID2D1GeometryRealization_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateInk(ID2D1DeviceContext6 *iface,
        const D2D1_INK_POINT *start_point, ID2D1Ink **ink)
{
    FIXME("iface %p, start_point %p, ink %p stub!\n", iface, start_point, ink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateInkStyle(ID2D1DeviceContext6 *iface,
        const D2D1_INK_STYLE_PROPERTIES *ink_style_properties, ID2D1InkStyle **ink_style)
{
    FIXME("iface %p, ink_style_properties %p, ink_style %p stub!\n", iface, ink_style_properties, ink_style);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateGradientMesh(ID2D1DeviceContext6 *iface,
        const D2D1_GRADIENT_MESH_PATCH *patches, UINT32 patches_count,
        ID2D1GradientMesh **gradient_mesh)
{
    FIXME("iface %p, patches %p, patches_count %u, gradient_mesh %p stub!\n", iface, patches,
            patches_count, gradient_mesh);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageSourceFromWic(ID2D1DeviceContext6 *iface,
        IWICBitmapSource *wic_bitmap_source, D2D1_IMAGE_SOURCE_LOADING_OPTIONS loading_options,
        D2D1_ALPHA_MODE alpha_mode, ID2D1ImageSourceFromWic **image_source)
{
    FIXME("iface %p, wic_bitmap_source %p, loading_options %#x, alpha_mode %u, image_source %p stub!\n",
            iface, wic_bitmap_source, loading_options, alpha_mode, image_source);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLookupTable3D(ID2D1DeviceContext6 *iface,
        D2D1_BUFFER_PRECISION precision, const UINT32 *extents, const BYTE *data,
        UINT32 data_count, const UINT32 *strides, ID2D1LookupTable3D **lookup_table)
{
    FIXME("iface %p, precision %u, extents %p, data %p, data_count %u, strides %p, lookup_table %p stub!\n",
            iface, precision, extents, data, data_count, strides, lookup_table);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageSourceFromDxgi(ID2D1DeviceContext6 *iface,
        IDXGISurface **surfaces, UINT32 surface_count, DXGI_COLOR_SPACE_TYPE color_space,
        D2D1_IMAGE_SOURCE_FROM_DXGI_OPTIONS options, ID2D1ImageSource **image_source)
{
    FIXME("iface %p, surfaces %p, surface_count %u, color_space %u, options %#x, image_source %p stub!\n",
            iface, surfaces, surface_count, color_space, options, image_source);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetGradientMeshWorldBounds(ID2D1DeviceContext6 *iface,
        ID2D1GradientMesh *gradient_mesh, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, gradient_mesh %p, bounds %p stub!\n", iface, gradient_mesh, bounds);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawInk(ID2D1DeviceContext6 *iface, ID2D1Ink *ink,
        ID2D1Brush *brush, ID2D1InkStyle *ink_style)
{
    FIXME("iface %p, ink %p, brush %p, ink_style %p stub!\n", iface, ink, brush, ink_style);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGradientMesh(ID2D1DeviceContext6 *iface,
        ID2D1GradientMesh *gradient_mesh)
{
    FIXME("iface %p, gradient_mesh %p stub!\n", iface, gradient_mesh);
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext2_DrawGdiMetafile(
        ID2D1DeviceContext6 *iface, ID2D1GdiMetafile *gdi_metafile, const D2D1_RECT_F *dst_rect,
        const D2D1_RECT_F *src_rect)
{
    FIXME("iface %p, gdi_metafile %p, dst_rect %s, src_rect %s stub!\n", iface, gdi_metafile,
            debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateTransformedImageSource(ID2D1DeviceContext6 *iface,
        ID2D1ImageSource *source, const D2D1_TRANSFORMED_IMAGE_SOURCE_PROPERTIES *props,
        ID2D1TransformedImageSource **transformed)
{
    FIXME("iface %p, source %p, props %p, transformed %p stub!\n", iface, source, props, transformed);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSpriteBatch(ID2D1DeviceContext6 *iface,
        ID2D1SpriteBatch **sprite_batch)
{
    FIXME("iface %p, sprite_batch %p stub!\n", iface, sprite_batch);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawSpriteBatch(ID2D1DeviceContext6 *iface,
        ID2D1SpriteBatch *sprite_batch, UINT32 start_index, UINT32 sprite_count, ID2D1Bitmap *bitmap,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, D2D1_SPRITE_OPTIONS sprite_options)
{
    FIXME("iface %p, sprite_batch %p, start_index %u, sprite_count %u, bitmap %p, interpolation_mode %u,"
            "sprite_options %u stub!\n", iface, sprite_batch, start_index, sprite_count, bitmap,
            interpolation_mode, sprite_options);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSvgDocument(ID2D1DeviceContext6 *iface,
        IStream *input_xml_stream, D2D1_SIZE_F viewport_size, ID2D1SvgDocument **svg_document)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, input_xml_stream %p, viewport_size {%.8e, %.8e}, svg_document %p.\n", iface,
            input_xml_stream, viewport_size.width, viewport_size.height, svg_document);

    if (!svg_document)
        return E_INVALIDARG;
    return d2d_svg_document_create(context->factory, input_xml_stream, viewport_size, svg_document);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawSvgDocument(ID2D1DeviceContext6 *iface,
        ID2D1SvgDocument *svg_document)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, svg_document %p.\n", iface, svg_document);

    if (!svg_document)
        return;
    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        /* TODO: recording SVG documents into command lists */
        FIXME("Command list target not supported.\n");
        return;
    }
    d2d_svg_document_draw(svg_document, (ID2D1DeviceContext *)iface);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromDxgiColorSpace(
        ID2D1DeviceContext6 *iface, DXGI_COLOR_SPACE_TYPE color_space, ID2D1ColorContext1 **color_context)
{
    FIXME("iface %p, color_space %u, color_context %p stub!\n", iface, color_space, color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromSimpleColorProfile(
        ID2D1DeviceContext6 *iface, const D2D1_SIMPLE_COLOR_PROFILE *simple_profile, ID2D1ColorContext1 **color_context)
{
    FIXME("iface %p, simple_profile %p, color_context %p stub!\n", iface, simple_profile, color_context);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_BlendImage(ID2D1DeviceContext6 *iface, ID2D1Image *image,
        D2D1_BLEND_MODE blend_mode, const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect,
        D2D1_INTERPOLATION_MODE interpolation_mode)
{
    FIXME("iface %p, image %p, blend_mode %u, target_offset %s, image_rect %s, interpolation_mode %u stub!\n",
            iface, image, blend_mode, debug_d2d_point_2f(target_offset), debug_d2d_rect_f(image_rect),
            interpolation_mode);
}

const struct ID2D1DeviceContext6Vtbl d2d_device_context_vtbl =
{
    d2d_device_context_QueryInterface,
    d2d_device_context_AddRef,
    d2d_device_context_Release,
    d2d_device_context_GetFactory,
    d2d_device_context_CreateBitmap,
    d2d_device_context_CreateBitmapFromWicBitmap,
    d2d_device_context_CreateSharedBitmap,
    d2d_device_context_CreateBitmapBrush,
    d2d_device_context_CreateSolidColorBrush,
    d2d_device_context_CreateGradientStopCollection,
    d2d_device_context_CreateLinearGradientBrush,
    d2d_device_context_CreateRadialGradientBrush,
    d2d_device_context_CreateCompatibleRenderTarget,
    d2d_device_context_CreateLayer,
    d2d_device_context_CreateMesh,
    d2d_device_context_DrawLine,
    d2d_device_context_DrawRectangle,
    d2d_device_context_FillRectangle,
    d2d_device_context_DrawRoundedRectangle,
    d2d_device_context_FillRoundedRectangle,
    d2d_device_context_DrawEllipse,
    d2d_device_context_FillEllipse,
    d2d_device_context_DrawGeometry,
    d2d_device_context_FillGeometry,
    d2d_device_context_FillMesh,
    d2d_device_context_FillOpacityMask,
    d2d_device_context_DrawBitmap,
    d2d_device_context_DrawText,
    d2d_device_context_DrawTextLayout,
    d2d_device_context_DrawGlyphRun,
    d2d_device_context_SetTransform,
    d2d_device_context_GetTransform,
    d2d_device_context_SetAntialiasMode,
    d2d_device_context_GetAntialiasMode,
    d2d_device_context_SetTextAntialiasMode,
    d2d_device_context_GetTextAntialiasMode,
    d2d_device_context_SetTextRenderingParams,
    d2d_device_context_GetTextRenderingParams,
    d2d_device_context_SetTags,
    d2d_device_context_GetTags,
    d2d_device_context_PushLayer,
    d2d_device_context_PopLayer,
    d2d_device_context_Flush,
    d2d_device_context_SaveDrawingState,
    d2d_device_context_RestoreDrawingState,
    d2d_device_context_PushAxisAlignedClip,
    d2d_device_context_PopAxisAlignedClip,
    d2d_device_context_Clear,
    d2d_device_context_BeginDraw,
    d2d_device_context_EndDraw,
    d2d_device_context_GetPixelFormat,
    d2d_device_context_SetDpi,
    d2d_device_context_GetDpi,
    d2d_device_context_GetSize,
    d2d_device_context_GetPixelSize,
    d2d_device_context_GetMaximumBitmapSize,
    d2d_device_context_IsSupported,
    d2d_device_context_ID2D1DeviceContext_CreateBitmap,
    d2d_device_context_ID2D1DeviceContext_CreateBitmapFromWicBitmap,
    d2d_device_context_CreateColorContext,
    d2d_device_context_CreateColorContextFromFilename,
    d2d_device_context_CreateColorContextFromWicColorContext,
    d2d_device_context_CreateBitmapFromDxgiSurface,
    d2d_device_context_CreateEffect,
    d2d_device_context_ID2D1DeviceContext_CreateGradientStopCollection,
    d2d_device_context_CreateImageBrush,
    d2d_device_context_ID2D1DeviceContext_CreateBitmapBrush,
    d2d_device_context_CreateCommandList,
    d2d_device_context_IsDxgiFormatSupported,
    d2d_device_context_IsBufferPrecisionSupported,
    d2d_device_context_GetImageLocalBounds,
    d2d_device_context_GetImageWorldBounds,
    d2d_device_context_GetGlyphRunWorldBounds,
    d2d_device_context_GetDevice,
    d2d_device_context_SetTarget,
    d2d_device_context_GetTarget,
    d2d_device_context_SetRenderingControls,
    d2d_device_context_GetRenderingControls,
    d2d_device_context_SetPrimitiveBlend,
    d2d_device_context_GetPrimitiveBlend,
    d2d_device_context_SetUnitMode,
    d2d_device_context_GetUnitMode,
    d2d_device_context_ID2D1DeviceContext_DrawGlyphRun,
    d2d_device_context_DrawImage,
    d2d_device_context_DrawGdiMetafile,
    d2d_device_context_ID2D1DeviceContext_DrawBitmap,
    d2d_device_context_ID2D1DeviceContext_PushLayer,
    d2d_device_context_InvalidateEffectInputRectangle,
    d2d_device_context_GetEffectInvalidRectangleCount,
    d2d_device_context_GetEffectInvalidRectangles,
    d2d_device_context_GetEffectRequiredInputRectangles,
    d2d_device_context_ID2D1DeviceContext_FillOpacityMask,
    d2d_device_context_CreateFilledGeometryRealization,
    d2d_device_context_CreateStrokedGeometryRealization,
    d2d_device_context_DrawGeometryRealization,
    d2d_device_context_CreateInk,
    d2d_device_context_CreateInkStyle,
    d2d_device_context_CreateGradientMesh,
    d2d_device_context_CreateImageSourceFromWic,
    d2d_device_context_CreateLookupTable3D,
    d2d_device_context_CreateImageSourceFromDxgi,
    d2d_device_context_GetGradientMeshWorldBounds,
    d2d_device_context_DrawInk,
    d2d_device_context_DrawGradientMesh,
    d2d_device_context_ID2D1DeviceContext2_DrawGdiMetafile,
    d2d_device_context_CreateTransformedImageSource,
    d2d_device_context_CreateSpriteBatch,
    d2d_device_context_DrawSpriteBatch,
    d2d_device_context_CreateSvgGlyphStyle,
    d2d_device_context_ID2D1DeviceContext4_DrawText,
    d2d_device_context_ID2D1DeviceContext4_DrawTextLayout,
    d2d_device_context_DrawColorBitmapGlyphRun,
    d2d_device_context_DrawSvgGlyphRun,
    d2d_device_context_GetColorBitmapGlyphImage,
    d2d_device_context_GetSvgGlyphImage,
    d2d_device_context_CreateSvgDocument,
    d2d_device_context_DrawSvgDocument,
    d2d_device_context_CreateColorContextFromDxgiColorSpace,
    d2d_device_context_CreateColorContextFromSimpleColorProfile,
    d2d_device_context_BlendImage,
};

static inline struct d2d_device_context *impl_from_ID2D1GdiInteropRenderTarget(ID2D1GdiInteropRenderTarget *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, ID2D1GdiInteropRenderTarget_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_QueryInterface(ID2D1GdiInteropRenderTarget *iface,
        REFIID iid, void **out)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(render_target->outer_unknown, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_AddRef(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(render_target->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_Release(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(render_target->outer_unknown);
}

static HRESULT d2d_gdi_interop_get_surface(struct d2d_device_context *context, IDXGISurface1 **surface)
{
    ID3D11Resource *resource;
    HRESULT hr;

    if (context->target.type != D2D_TARGET_BITMAP)
    {
        FIXME("Unimplemented for target type %u.\n", context->target.type);
        return E_NOTIMPL;
    }

    if (!(context->target.bitmap->options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE))
        return D2DERR_TARGET_NOT_GDI_COMPATIBLE;

    ID3D11RenderTargetView_GetResource(context->target.bitmap->rtv, &resource);
    hr = ID3D11Resource_QueryInterface(resource, &IID_IDXGISurface1, (void **)surface);
    ID3D11Resource_Release(resource);
    if (FAILED(hr))
    {
        *surface = NULL;
        WARN("Failed to get DXGI surface, %#lx.\n", hr);
        return hr;
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_GetDC(ID2D1GdiInteropRenderTarget *iface,
        D2D1_DC_INITIALIZE_MODE mode, HDC *dc)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    HRESULT hr;

    TRACE("iface %p, mode %d, dc %p.\n", iface, mode, dc);

    *dc = NULL;

    if (render_target->target.hdc)
        return D2DERR_WRONG_STATE;

    if (FAILED(hr = d2d_gdi_interop_get_surface(render_target, &surface)))
        return hr;

    hr = IDXGISurface1_GetDC(surface, mode != D2D1_DC_INITIALIZE_MODE_COPY, &render_target->target.hdc);
    IDXGISurface1_Release(surface);

    if (SUCCEEDED(hr))
        *dc = render_target->target.hdc;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_ReleaseDC(ID2D1GdiInteropRenderTarget *iface,
        const RECT *update)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    RECT update_rect;
    HRESULT hr;

    TRACE("iface %p, update rect %s.\n", iface, wine_dbgstr_rect(update));

    if (!render_target->target.hdc)
        return D2DERR_WRONG_STATE;

    if (FAILED(hr = d2d_gdi_interop_get_surface(render_target, &surface)))
        return hr;

    render_target->target.hdc = NULL;
    if (update)
        update_rect = *update;
    hr = IDXGISurface1_ReleaseDC(surface, update ? &update_rect : NULL);
    IDXGISurface1_Release(surface);

    return hr;
}

const struct ID2D1GdiInteropRenderTargetVtbl d2d_gdi_interop_render_target_vtbl =
{
    d2d_gdi_interop_render_target_QueryInterface,
    d2d_gdi_interop_render_target_AddRef,
    d2d_gdi_interop_render_target_Release,
    d2d_gdi_interop_render_target_GetDC,
    d2d_gdi_interop_render_target_ReleaseDC,
};

HRESULT d2d_d3d_create_render_target(struct d2d_device *device, IDXGISurface *surface, IUnknown *outer_unknown,
        const struct d2d_device_context_ops *ops, const D2D1_RENDER_TARGET_PROPERTIES *desc, void **render_target)
{
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_device_context *object;
    ID2D1Bitmap1 *bitmap;
    HRESULT hr;

    if (desc->type != D2D1_RENDER_TARGET_TYPE_DEFAULT && desc->type != D2D1_RENDER_TARGET_TYPE_HARDWARE)
        WARN("Ignoring render target type %#x.\n", desc->type);
    if (desc->usage != D2D1_RENDER_TARGET_USAGE_NONE)
        FIXME("Ignoring render target usage %#x.\n", desc->usage);
    if (desc->minLevel != D2D1_FEATURE_LEVEL_DEFAULT)
        WARN("Ignoring feature level %#x.\n", desc->minLevel);

    bitmap_desc.dpiX = desc->dpiX;
    bitmap_desc.dpiY = desc->dpiY;

    if (bitmap_desc.dpiX == 0.0f && bitmap_desc.dpiY == 0.0f)
    {
        bitmap_desc.dpiX = 96.0f;
        bitmap_desc.dpiY = 96.0f;
    }
    else if (bitmap_desc.dpiX <= 0.0f || bitmap_desc.dpiY <= 0.0f)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_device_context_init(object, device, outer_unknown, ops)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    ID2D1DeviceContext6_SetDpi(&object->ID2D1DeviceContext6_iface, bitmap_desc.dpiX, bitmap_desc.dpiY);

    if (surface)
    {
        bitmap_desc.pixelFormat = desc->pixelFormat;
        bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (desc->usage & D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE)
            bitmap_desc.bitmapOptions |= D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE;
        bitmap_desc.colorContext = NULL;

        if (FAILED(hr = ID2D1DeviceContext6_CreateBitmapFromDxgiSurface(&object->ID2D1DeviceContext6_iface,
                surface, &bitmap_desc, &bitmap)))
        {
            WARN("Failed to create target bitmap, hr %#lx.\n", hr);
            IUnknown_Release(&object->IUnknown_iface);
            return hr;
        }

        ID2D1DeviceContext6_SetTarget(&object->ID2D1DeviceContext6_iface, (ID2D1Image *)bitmap);
        ID2D1Bitmap1_Release(bitmap);
    }
    else
        object->desc.pixelFormat = desc->pixelFormat;

    TRACE("Created render target %p.\n", object);
    *render_target = outer_unknown ? &object->IUnknown_iface : (IUnknown *)&object->ID2D1DeviceContext6_iface;

    return S_OK;
}
