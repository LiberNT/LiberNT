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

static void d2d_device_context_draw_real(struct d2d_device_context *render_target, enum d2d_shape_type shape_type,
        ID3D11Buffer *ib, unsigned int index_count, ID3D11Buffer *vb, unsigned int vb_stride,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush);
static HRESULT d2d_device_context_update_ps_cb(struct d2d_device_context *context,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush, BOOL outline, BOOL is_arc);
static HRESULT d2d_device_context_update_vs_cb(struct d2d_device_context *context,
        const D2D_MATRIX_3X2_F *geometry_transform, float stroke_width);
static BOOL d2d_device_context_draw_coverage(struct d2d_device_context *context,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush,
        BOOL outline, float stroke_width);

static BOOL d2d_device_context_stream_write(struct d2d_device_context *context, struct d2d_stream *stream,
        const void *data, UINT size, UINT *offset)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    D3D11_BUFFER_DESC desc;
    D3D11_MAP map_type;
    HRESULT hr;

    size = (size + 15) & ~15u;
    if (size > stream->size)
    {
        if (stream->buffer)
            ID3D11Buffer_Release(stream->buffer);
        stream->buffer = NULL;
        stream->size = max(size, max(stream->size * 2, 65536));
        stream->pos = 0;

        desc.ByteWidth = stream->size;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = stream->bind;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;
        if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, NULL, &stream->buffer)))
        {
            WARN("Failed to create stream buffer, hr %#lx.\n", hr);
            stream->size = 0;
            return FALSE;
        }
    }

    map_type = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (!stream->pos || stream->pos + size > stream->size)
    {
        map_type = D3D11_MAP_WRITE_DISCARD;
        stream->pos = 0;
    }

    if (context->cs)
        EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)stream->buffer, 0, map_type, 0, &map_desc);
    if (SUCCEEDED(hr))
    {
        memcpy((BYTE *)map_desc.pData + stream->pos, data, size);
        ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)stream->buffer, 0);
    }
    ID3D11DeviceContext_Release(d3d_context);
    if (context->cs)
        LeaveCriticalSection(context->cs);
    if (FAILED(hr))
    {
        WARN("Failed to map stream buffer, hr %#lx.\n", hr);
        return FALSE;
    }

    *offset = stream->pos;
    stream->pos += size;
    return TRUE;
}

static BOOL d2d_device_context_stream_vertices(struct d2d_device_context *context, const void *vertices,
        UINT vertices_size)
{
    context->vb_stream.bind = D3D11_BIND_VERTEX_BUFFER;
    return d2d_device_context_stream_write(context, &context->vb_stream, vertices, vertices_size,
            &context->draw_vb_offset);
}

static BOOL d2d_device_context_stream_indexed(struct d2d_device_context *context, const void *faces,
        UINT faces_size, const void *vertices, UINT vertices_size)
{
    context->ib_stream.bind = D3D11_BIND_INDEX_BUFFER;
    if (!d2d_device_context_stream_write(context, &context->ib_stream, faces, faces_size,
            &context->draw_ib_offset))
        return FALSE;
    return d2d_device_context_stream_vertices(context, vertices, vertices_size);
}

static void d2d_device_context_draw(struct d2d_device_context *rt, enum d2d_shape_type st,
        ID3D11Buffer *ib, unsigned int ic, ID3D11Buffer *vb, unsigned int vs,
        struct d2d_brush *b, struct d2d_brush *o)
{
    d2d_device_context_draw_real(rt, st, ib, ic, vb, vs, b, o);
}

static void d2d_device_context_draw_real(struct d2d_device_context *render_target, enum d2d_shape_type shape_type,
        ID3D11Buffer *ib, unsigned int index_count, ID3D11Buffer *vb, unsigned int vb_stride,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    struct d2d_shape_resources *shape_resources = &render_target->shape_resources[shape_type];
    ID3D11Device1 *device = render_target->d3d_device;
    ID3D11DeviceContext1 *context;
    UINT ib_offset;
    struct d2d_shadow_state *sh;
    ID3D11Buffer *vs_cb = render_target->vs_cb, *ps_cb = render_target->ps_cb;
    D3D11_RECT scissor_rect;
    unsigned int offset;
    D3D11_VIEWPORT vp;

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = render_target->pixel_size.width;
    vp.Height = render_target->pixel_size.height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    if (render_target->cs)
        EnterCriticalSection(render_target->cs);

    ID3D11Device1_GetImmediateContext1(device, &context);
    if (!render_target->state_in)
    {
        ID3D11DeviceContext1_SwapDeviceContextState(context, render_target->d3d_state, &render_target->prev_state);
        render_target->state_in = TRUE;
    }
    sh = &render_target->sh;

    if (!sh->valid)
        ID3D11DeviceContext1_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (sh->il != shape_resources->il)
        ID3D11DeviceContext1_IASetInputLayout(context, sh->il = shape_resources->il);
    offset = render_target->draw_vb_offset;
    ib_offset = render_target->draw_ib_offset;
    render_target->draw_ib_offset = 0;
    render_target->draw_vb_offset = 0;
    if (!sh->valid || sh->ib != ib || sh->ib_offset != ib_offset)
    {
        ID3D11DeviceContext1_IASetIndexBuffer(context, ib, DXGI_FORMAT_R16_UINT, ib_offset);
        sh->ib = ib;
        sh->ib_offset = ib_offset;
    }
    if (!sh->valid || sh->vb != vb || sh->vb_offset != offset || sh->vb_stride != vb_stride)
    {
        ID3D11DeviceContext1_IASetVertexBuffers(context, 0, 1, &vb, &vb_stride, &offset);
        sh->vb = vb;
        sh->vb_offset = offset;
        sh->vb_stride = vb_stride;
    }
    if (sh->vs_cb != vs_cb)
    {
        ID3D11DeviceContext1_VSSetConstantBuffers(context, 0, 1, &vs_cb);
        sh->vs_cb = vs_cb;
    }
    if (sh->vs != shape_resources->vs)
        ID3D11DeviceContext1_VSSetShader(context, sh->vs = shape_resources->vs, NULL, 0);
    if (sh->ps_cb != ps_cb)
    {
        ID3D11DeviceContext1_PSSetConstantBuffers(context, 0, 1, &ps_cb);
        sh->ps_cb = ps_cb;
    }
    if (sh->ps != render_target->ps)
        ID3D11DeviceContext1_PSSetShader(context, sh->ps = render_target->ps, NULL, 0);
    if (sh->vp.Width != vp.Width || sh->vp.Height != vp.Height || !sh->valid)
    {
        ID3D11DeviceContext1_RSSetViewports(context, 1, &vp);
        sh->vp = vp;
    }
    if (render_target->mask_active)
    {
        scissor_rect.left = 0;
        scissor_rect.top = 0;
        scissor_rect.right = render_target->pixel_size.width;
        scissor_rect.bottom = render_target->pixel_size.height;
    }
    else
    if (render_target->clip_stack.count)
    {
        const D2D1_RECT_F *clip_rect;

        clip_rect = &render_target->clip_stack.stack[render_target->clip_stack.count - 1];
        if (render_target->ps_shadow_valid && render_target->ps_shadow.clip_on)
        {
            scissor_rect.left = floorf(clip_rect->left);
            scissor_rect.top = floorf(clip_rect->top);
            scissor_rect.right = ceilf(clip_rect->right);
            scissor_rect.bottom = ceilf(clip_rect->bottom);
        }
        else
        {
        scissor_rect.left = ceilf(clip_rect->left - 0.5f);
        scissor_rect.top = ceilf(clip_rect->top - 0.5f);
        scissor_rect.right = ceilf(clip_rect->right - 0.5f);
        scissor_rect.bottom = ceilf(clip_rect->bottom - 0.5f);
        }
    }
    else
    {
        scissor_rect.left = 0.0f;
        scissor_rect.top = 0.0f;
        scissor_rect.right = render_target->pixel_size.width;
        scissor_rect.bottom = render_target->pixel_size.height;
    }
    if (!sh->valid || memcmp(&sh->scissor, &scissor_rect, sizeof(scissor_rect)))
    {
        ID3D11DeviceContext1_RSSetScissorRects(context, 1, &scissor_rect);
        sh->scissor = scissor_rect;
    }
    if (sh->rs != render_target->rs)
        ID3D11DeviceContext1_RSSetState(context, sh->rs = render_target->rs);
    if (sh->rtv != render_target->target.bitmap->rtv)
    {
        ID3D11DeviceContext1_OMSetRenderTargets(context, 1, &render_target->target.bitmap->rtv, NULL);
        sh->rtv = render_target->target.bitmap->rtv;
    }
    if (brush)
    {
        ID3D11BlendState *bs = render_target->bs[render_target->drawing_state.primitiveBlend];

        if (sh->bs != bs)
            ID3D11DeviceContext1_OMSetBlendState(context, sh->bs = bs, NULL, D3D11_DEFAULT_SAMPLE_MASK);
        d2d_brush_bind_resources(brush, render_target, 0);
    }
    else if (sh->bs)
    {
        ID3D11DeviceContext1_OMSetBlendState(context, sh->bs = NULL, NULL, D3D11_DEFAULT_SAMPLE_MASK);
    }
    sh->valid = TRUE;
    if (opacity_brush)
        d2d_brush_bind_resources(opacity_brush, render_target, 1);
    if (render_target->mask_active)
        ID3D11DeviceContext1_PSSetShaderResources(context, 4, 1, &render_target->mask_srv);

    if (ib)
        ID3D11DeviceContext1_DrawIndexed(context, index_count, 0, 0);
    else
        ID3D11DeviceContext1_Draw(context, index_count, 0);

    ID3D11DeviceContext1_Release(context);

    if (render_target->cs)
        LeaveCriticalSection(render_target->cs);
}

void d2d_device_context_state_out(struct d2d_device_context *context)
{
    ID3D11DeviceContext1 *d3d_context;

    if (!context->state_in)
        return;
    if (context->cs)
        EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext1(context->d3d_device, &d3d_context);
    ID3D11DeviceContext1_SwapDeviceContextState(d3d_context, context->prev_state, NULL);
    ID3D11DeviceContext1_Release(d3d_context);
    ID3DDeviceContextState_Release(context->prev_state);
    context->prev_state = NULL;
    context->state_in = FALSE;
    if (context->cs)
        LeaveCriticalSection(context->cs);
}

static void d2d_device_context_request_gpu_clip(struct d2d_device_context *context)
{
    context->ps_clip_on = context->clip_stack.count && d2d_coverage_clip_is_fractional(
            &context->clip_stack.stack[context->clip_stack.count - 1]);
}

static BOOL d2d_device_context_draw_line_aa(struct d2d_device_context *context, D2D1_POINT_2F p0, D2D1_POINT_2F p1,
        struct d2d_brush *brush, float stroke_width)
{
    D2D1_MATRIX_3X2_F world, to_device, from_device, identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    struct d2d_aa_vertex vertices[D2D_CONVEX_MAX_VERTICES];
    struct d2d_face faces[D2D_CONVEX_MAX_FACES];
    D2D1_POINT_2F quad[4];
    size_t vertex_count, face_count;
    float dx, dy, len, hw;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_PER_PRIMITIVE || !(stroke_width > 0.0f))
        return FALSE;
    dx = p1.x - p0.x;
    dy = p1.y - p0.y;
    len = sqrtf(dx * dx + dy * dy);
    if (!(len > 1e-4f))
        return FALSE;
    hw = stroke_width * 0.5f / len;

    quad[0].x = p0.x + dy * hw;
    quad[0].y = p0.y - dx * hw;
    quad[1].x = p1.x + dy * hw;
    quad[1].y = p1.y - dx * hw;
    quad[2].x = p1.x - dy * hw;
    quad[2].y = p1.y + dx * hw;
    quad[3].x = p0.x - dy * hw;
    quad[3].y = p0.y + dx * hw;

    world = context->drawing_state.transform;
    to_device._11 = context->desc.dpiX / 96.0f;
    to_device._12 = to_device._21 = to_device._31 = to_device._32 = 0.0f;
    to_device._22 = context->desc.dpiY / 96.0f;
    d2d_matrix_multiply(&world, &to_device);
    if (!d2d_matrix_invert(&from_device, &world))
        return FALSE;

    if (!(vertex_count = d2d_convex_aa_polygon(quad, 4, &identity, &world, &from_device, 0.0f,
            vertices, faces, &face_count)))
        return FALSE;

    d2d_device_context_request_gpu_clip(context);
    if (SUCCEEDED(d2d_device_context_update_vs_cb(context, &identity, 0.0f))
            && SUCCEEDED(d2d_device_context_update_ps_cb(context, brush, NULL, FALSE, FALSE))
            && d2d_device_context_stream_indexed(context, faces, face_count * sizeof(*faces),
            vertices, vertex_count * sizeof(*vertices)))
        d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE_AA, context->ib_stream.buffer,
                face_count * 3, context->vb_stream.buffer, sizeof(*vertices), brush, NULL);
    return TRUE;
}

void STDMETHODCALLTYPE d2d_device_context_DrawLine(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    TRACE("iface %p, p0 %s, p1 %s, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, debug_d2d_point_2f(&p0), debug_d2d_point_2f(&p1), brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_line(context->target.command_list, context, p0, p1, brush, stroke_width, stroke_style);
        return;
    }

    if (!stroke_style && context->target.type != D2D_TARGET_UNKNOWN
            && d2d_device_context_draw_line_aa(context, p0, p1, unsafe_impl_from_ID2D1Brush(brush), stroke_width))
        return;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(context->factory, &geometry)))
    {
        WARN("Failed to create path geometry, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        WARN("Failed to open geometry sink, hr %#lx.\n", hr);
        ID2D1PathGeometry_Release(geometry);
        return;
    }

    ID2D1GeometrySink_BeginFigure(sink, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddLine(sink, p1);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        WARN("Failed to close geometry sink, hr %#lx.\n", hr);
    ID2D1GeometrySink_Release(sink);

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1PathGeometry_Release(geometry);
}

void STDMETHODCALLTYPE d2d_device_context_DrawRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, debug_d2d_rect_f(rect), brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_rectangle(context->target.command_list, context, rect, brush, stroke_width, stroke_style);
        return;
    }

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RectangleGeometry_Release(geometry);
}

BOOL d2d_device_context_fill_rectangle(struct d2d_device_context *context, const D2D1_RECT_F *rect,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    static const D2D_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    D2D1_MATRIX_3X2_F world, to_device;
    D2D1_POINT_2F quad[4];

    if (context->target.type != D2D_TARGET_BITMAP || !brush)
        return FALSE;
    if (!(rect->right > rect->left && rect->bottom > rect->top))
        return FALSE;

    if (context->drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE)
    {
        world = context->drawing_state.transform;
        to_device._11 = context->desc.dpiX / 96.0f;
        to_device._12 = to_device._21 = to_device._31 = to_device._32 = 0.0f;
        to_device._22 = context->desc.dpiY / 96.0f;
        d2d_matrix_multiply(&world, &to_device);
        if (!d2d_rect_is_pixel_aligned(rect, &world))
            return FALSE;
    }
    else if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        return FALSE;
    }

    quad[0].x = rect->left;
    quad[0].y = rect->top;
    quad[1].x = rect->left;
    quad[1].y = rect->bottom;
    quad[2].x = rect->right;
    quad[2].y = rect->top;
    quad[3].x = rect->right;
    quad[3].y = rect->bottom;

    d2d_device_context_request_gpu_clip(context);
    if (FAILED(d2d_device_context_update_vs_cb(context, &identity, 0.0f))
            || FAILED(d2d_device_context_update_ps_cb(context, brush, opacity_brush, FALSE, FALSE))
            || !d2d_device_context_stream_vertices(context, quad, sizeof(quad)))
        return FALSE;

    d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE, context->ib, 6, context->vb_stream.buffer,
            sizeof(*quad), brush, opacity_brush);
    return TRUE;
}

void STDMETHODCALLTYPE d2d_device_context_FillRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p.\n", iface, debug_d2d_rect_f(rect), brush);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_fill_rectangle(context->target.command_list, context, rect, brush);
        return;
    }

    if (d2d_device_context_fill_rectangle(context, rect, unsafe_impl_from_ID2D1Brush(brush), NULL))
        return;

    {
        hr = ID2D1Factory_CreateRectangleGeometry(context->factory, rect, &geometry);
    }
    if (FAILED(hr))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RectangleGeometry_Release(geometry);
}

void STDMETHODCALLTYPE d2d_device_context_DrawRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, rect, brush, stroke_width, stroke_style);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

void STDMETHODCALLTYPE d2d_device_context_FillRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p.\n", iface, rect, brush);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

void STDMETHODCALLTYPE d2d_device_context_DrawEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, ellipse, brush, stroke_width, stroke_style);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1EllipseGeometry_Release(geometry);
}

void STDMETHODCALLTYPE d2d_device_context_FillEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p.\n", iface, ellipse, brush);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1EllipseGeometry_Release(geometry);
}

static HRESULT d2d_device_context_update_ps_cb(struct d2d_device_context *context,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush, BOOL outline, BOOL is_arc)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    struct d2d_ps_cb cb_data;
    HRESULT hr;

    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.outline = outline;
    cb_data.is_arc = is_arc;
    cb_data.mask = context->mask_active;
    cb_data.blend_min = context->drawing_state.primitiveBlend == D2D1_PRIMITIVE_BLEND_MIN;
    cb_data.mask_origin[0] = context->mask_x;
    cb_data.mask_origin[1] = context->mask_y;
    {
        BOOL want = context->ps_clip_on;

        context->ps_clip_on = FALSE;
        if (want && context->clip_stack.count)
        {
        const D2D1_RECT_F *clip = &context->clip_stack.stack[context->clip_stack.count - 1];

        cb_data.clip_on = TRUE;
        cb_data.clip[0] = clip->left;
        cb_data.clip[1] = clip->top;
        cb_data.clip[2] = clip->right;
        cb_data.clip[3] = clip->bottom;
        }
    }
    if (!d2d_brush_fill_cb(brush, &cb_data.colour_brush))
        WARN("Failed to initialize colour brush buffer.\n");
    if (!d2d_brush_fill_cb(opacity_brush, &cb_data.opacity_brush))
        WARN("Failed to initialize opacity brush buffer.\n");

    if (context->ps_shadow_valid && !memcmp(&context->ps_shadow, &cb_data, sizeof(cb_data)))
        return S_OK;

    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->ps_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map constant buffer, hr %#lx.\n", hr);
        context->ps_shadow_valid = FALSE;
        ID3D11DeviceContext_Release(d3d_context);
        return hr;
    }

    memcpy(map_desc.pData, &cb_data, sizeof(cb_data));
    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->ps_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);
    context->ps_shadow = cb_data;
    context->ps_shadow_valid = TRUE;

    return S_OK;
}

static HRESULT d2d_device_context_update_vs_cb(struct d2d_device_context *context,
        const D2D_MATRIX_3X2_F *geometry_transform, float stroke_width)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    const D2D1_MATRIX_3X2_F *w;
    struct d2d_vs_cb cb_data;
    float tmp_x, tmp_y;
    HRESULT hr;

    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.transform_geometry._11 = geometry_transform->_11;
    cb_data.transform_geometry._21 = geometry_transform->_21;
    cb_data.transform_geometry._31 = geometry_transform->_31;
    cb_data.transform_geometry._12 = geometry_transform->_12;
    cb_data.transform_geometry._22 = geometry_transform->_22;
    cb_data.transform_geometry._32 = geometry_transform->_32;
    cb_data.transform_geometry.stroke_width = stroke_width;

    w = &context->drawing_state.transform;

    tmp_x = context->desc.dpiX / 96.0f;
    cb_data.transform_rtx.x = w->_11 * tmp_x;
    cb_data.transform_rtx.y = w->_21 * tmp_x;
    cb_data.transform_rtx.z = w->_31 * tmp_x;
    cb_data.transform_rtx.w = 2.0f / context->pixel_size.width;

    tmp_y = context->desc.dpiY / 96.0f;
    cb_data.transform_rty.x = w->_12 * tmp_y;
    cb_data.transform_rty.y = w->_22 * tmp_y;
    cb_data.transform_rty.z = w->_32 * tmp_y;
    cb_data.transform_rty.w = -2.0f / context->pixel_size.height;

    if (context->vs_shadow_valid && !memcmp(&context->vs_shadow, &cb_data, sizeof(cb_data)))
        return S_OK;

    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->vs_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map constant buffer, hr %#lx.\n", hr);
        context->vs_shadow_valid = FALSE;
        ID3D11DeviceContext_Release(d3d_context);
        return hr;
    }

    memcpy(map_desc.pData, &cb_data, sizeof(cb_data));
    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->vs_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);
    context->vs_shadow = cb_data;
    context->vs_shadow_valid = TRUE;

    return S_OK;
}

static void d2d_device_context_draw_geometry(struct d2d_device_context *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, float stroke_width)
{
    HRESULT hr;

    if (d2d_device_context_draw_coverage(render_target, geometry, brush, NULL, TRUE, stroke_width))
        return;

    if (FAILED(hr = d2d_device_context_update_vs_cb(render_target, &geometry->transform, stroke_width)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_device_context_update_ps_cb(render_target, brush, NULL, TRUE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (geometry->outline.face_count)
    {
        if (!d2d_device_context_stream_indexed(render_target,
                geometry->outline.faces, geometry->outline.face_count * sizeof(*geometry->outline.faces),
                geometry->outline.vertices, geometry->outline.vertex_count * sizeof(*geometry->outline.vertices)))
            return;

        d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_OUTLINE, render_target->ib_stream.buffer,
                3 * geometry->outline.face_count, render_target->vb_stream.buffer,
                sizeof(*geometry->outline.vertices), brush, NULL);
    }

    if (geometry->outline.bezier_face_count)
    {
        if (!d2d_device_context_stream_indexed(render_target,
                geometry->outline.bezier_faces,
                geometry->outline.bezier_face_count * sizeof(*geometry->outline.bezier_faces),
                geometry->outline.beziers, geometry->outline.bezier_count * sizeof(*geometry->outline.beziers)))
            return;

        d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_BEZIER_OUTLINE, render_target->ib_stream.buffer,
                3 * geometry->outline.bezier_face_count, render_target->vb_stream.buffer,
                sizeof(*geometry->outline.beziers), brush, NULL);
    }

    if (geometry->outline.arc_face_count)
    {
        if (!d2d_device_context_stream_indexed(render_target,
                geometry->outline.arc_faces,
                geometry->outline.arc_face_count * sizeof(*geometry->outline.arc_faces),
                geometry->outline.arcs, geometry->outline.arc_count * sizeof(*geometry->outline.arcs)))
            return;

        if (SUCCEEDED(d2d_device_context_update_ps_cb(render_target, brush, NULL, TRUE, TRUE)))
            d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_ARC_OUTLINE, render_target->ib_stream.buffer,
                    3 * geometry->outline.arc_face_count, render_target->vb_stream.buffer,
                    sizeof(*geometry->outline.arcs), brush, NULL);
    }
}

void STDMETHODCALLTYPE d2d_device_context_DrawGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);
    struct d2d_stroke_style *stroke_style_impl = unsafe_impl_from_ID2D1StrokeStyle(stroke_style);

    TRACE("iface %p, geometry %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, geometry, brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_geometry(context->target.command_list, context, geometry, brush,
                stroke_width, stroke_style);
        return;
    }

    if (stroke_style)
        FIXME("Ignoring stroke style %p.\n", stroke_style);

    if (stroke_style_impl)
    {
        if (stroke_style_impl->desc.transformType == D2D1_STROKE_TRANSFORM_TYPE_FIXED)
            stroke_width /= context->drawing_state.transform.m11;
    }

    d2d_device_context_draw_geometry(context, geometry_impl, brush_impl, stroke_width);
}

void d2d_device_context_release_mask(struct d2d_device_context *context)
{
    if (context->mask_srv)
        ID3D11ShaderResourceView_Release(context->mask_srv);
    if (context->mask_texture)
        ID3D11Texture2D_Release(context->mask_texture);
    context->mask_srv = NULL;
    context->mask_texture = NULL;
    context->mask_width = context->mask_height = 0;
    free(context->mask_staging);
    context->mask_staging = NULL;
    context->mask_staging_size = 0;
}

/* One mask texture serves every draw: each draw writes its edge tiles with UpdateSubresource, which keeps
 * the contents earlier draws read, so the mask costs one texture however many draws a frame makes. */
static BOOL d2d_device_context_ensure_mask_texture(struct d2d_device_context *context, UINT width, UINT height)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11ShaderResourceView *srv;
    ID3D11Texture2D *texture;
    HRESULT hr;

    if (context->mask_texture && width <= context->mask_width && height <= context->mask_height)
        return TRUE;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (max(width, context->mask_width) + 63) & ~63u;
    desc.Height = (max(height, context->mask_height) + 63) & ~63u;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    if (FAILED(hr = ID3D11Device1_CreateTexture2D(context->d3d_device, &desc, NULL, &texture)))
    {
        WARN("Failed to create coverage mask texture, hr %#lx.\n", hr);
        return FALSE;
    }
    if (FAILED(hr = ID3D11Device1_CreateShaderResourceView(context->d3d_device, (ID3D11Resource *)texture,
            &srv_desc, &srv)))
    {
        WARN("Failed to create coverage mask view, hr %#lx.\n", hr);
        ID3D11Texture2D_Release(texture);
        return FALSE;
    }

    if (context->mask_srv)
        ID3D11ShaderResourceView_Release(context->mask_srv);
    if (context->mask_texture)
        ID3D11Texture2D_Release(context->mask_texture);
    context->mask_texture = texture;
    context->mask_srv = srv;
    context->mask_width = desc.Width;
    context->mask_height = desc.Height;
    return TRUE;
}

/* Only the pixels of edge runs are read from the mask, so the upload covers the box around those runs. */
static BOOL d2d_device_context_upload_coverage(struct d2d_device_context *context, UINT width, UINT height)
{
    const struct d2d_coverage *coverage = &context->coverage;
    ID3D11DeviceContext *d3d_context;
    UINT left = width, top = height, right = 0, bottom = 0, pitch;
    D3D11_BOX box;
    size_t i, size;
    BYTE *staging;

    for (i = 0; i < coverage->run_count; ++i)
    {
        const struct d2d_coverage_run *run = &coverage->runs[i];

        if (!run->edge)
            continue;
        left = min(left, (UINT)run->x);
        top = min(top, (UINT)run->y);
        right = max(right, (UINT)(run->x + run->width));
        bottom = max(bottom, (UINT)(run->y + run->height));
    }
    if (right <= left || bottom <= top)
        return TRUE;
    assert(right <= width);
    assert(bottom <= height);

    if (!d2d_device_context_ensure_mask_texture(context, width, height))
        return FALSE;

    pitch = right - left;
    size = (size_t)pitch * (bottom - top);
    if (size > context->mask_staging_size)
    {
        if (!(staging = realloc(context->mask_staging, size)))
        {
            WARN("Failed to allocate %Iu bytes of coverage staging memory.\n", size);
            return FALSE;
        }
        context->mask_staging = staging;
        context->mask_staging_size = size;
    }
    staging = context->mask_staging;

    for (i = 0; i < coverage->run_count; ++i)
    {
        const struct d2d_coverage_run *run = &coverage->runs[i];
        const BYTE *tile = &coverage->tiles[run->tile * D2D_COVERAGE_TILE * D2D_COVERAGE_TILE];
        int x, row, w;

        if (!run->edge)
            continue;

        for (x = run->x; x < run->x + run->width; x += D2D_COVERAGE_TILE)
        {
            w = min(D2D_COVERAGE_TILE, run->x + run->width - x);
            for (row = 0; row < run->height; ++row)
                memcpy(staging + (size_t)(run->y - top + row) * pitch + (x - left), tile + row * D2D_COVERAGE_TILE, w);
            tile += D2D_COVERAGE_TILE * D2D_COVERAGE_TILE;
        }
    }

    box.left = left;
    box.top = top;
    box.front = 0;
    box.right = right;
    box.bottom = bottom;
    box.back = 1;
    if (context->cs)
        EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    ID3D11DeviceContext_UpdateSubresource(d3d_context, (ID3D11Resource *)context->mask_texture, 0, &box,
            staging, pitch, 0);
    ID3D11DeviceContext_Release(d3d_context);
    if (context->cs)
        LeaveCriticalSection(context->cs);

    return TRUE;
}

static void d2d_device_context_draw_coverage_runs(struct d2d_device_context *context,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush,
        const D2D1_MATRIX_3X2_F *from_device, const RECT *area, BOOL edge)
{
    enum {chunk = 256};
    const struct d2d_coverage *coverage = &context->coverage;
    D2D1_POINT_2F vertices[4 * chunk];
    struct d2d_face faces[2 * chunk];
    size_t i, count = 0;
    UINT16 base;
    float x0, y0, x1, y1;

    context->mask_active = edge;
    context->mask_x = area->left;
    context->mask_y = area->top;

    for (i = 0; i <= coverage->run_count; ++i)
    {
        if (i < coverage->run_count && coverage->runs[i].edge != edge)
            continue;
        if (i < coverage->run_count)
        {
            const struct d2d_coverage_run *run = &coverage->runs[i];

            x0 = area->left + run->x;
            y0 = area->top + run->y;
            x1 = x0 + run->width;
            y1 = y0 + run->height;
            base = count * 4;
            d2d_point_transform(&vertices[base], from_device, x0, y0);
            d2d_point_transform(&vertices[base + 1], from_device, x0, y1);
            d2d_point_transform(&vertices[base + 2], from_device, x1, y0);
            d2d_point_transform(&vertices[base + 3], from_device, x1, y1);
            faces[count * 2].v[0] = base;
            faces[count * 2].v[1] = base + 1;
            faces[count * 2].v[2] = base + 2;
            faces[count * 2 + 1].v[0] = base + 2;
            faces[count * 2 + 1].v[1] = base + 1;
            faces[count * 2 + 1].v[2] = base + 3;
            if (++count < chunk)
                continue;
        }
        if (!count)
            continue;

        if (SUCCEEDED(d2d_device_context_update_vs_cb(context, &geometry->transform, 0.0f))
                && SUCCEEDED(d2d_device_context_update_ps_cb(context, brush, opacity_brush, FALSE, FALSE))
                && d2d_device_context_stream_indexed(context, faces, count * 2 * sizeof(*faces),
                vertices, count * 4 * sizeof(*vertices)))
            d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE, context->ib_stream.buffer, count * 6,
                    context->vb_stream.buffer, sizeof(*vertices), brush, opacity_brush);
        count = 0;
    }

    context->mask_active = FALSE;
}

static BOOL d2d_device_context_draw_coverage(struct d2d_device_context *context,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush,
        BOOL outline, float stroke_width)
{
    D2D1_MATRIX_3X2_F world, to_device, from_device;
    const D2D1_RECT_F *clip_rect = NULL;
    D2D1_RECT_F bounds;
    float margin = 0.0f;
    RECT area;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_PER_PRIMITIVE)
        return FALSE;
    if (outline)
    {
        if (!geometry->outline.face_count && !geometry->outline.bezier_count && !geometry->outline.arc_count)
            return FALSE;
    }
    else if (!geometry->fill.face_count && !geometry->fill.bezier_vertex_count && !geometry->fill.arc_vertex_count)
    {
        return FALSE;
    }

    world = context->drawing_state.transform;
    to_device._11 = context->desc.dpiX / 96.0f;
    to_device._12 = to_device._21 = to_device._31 = to_device._32 = 0.0f;
    to_device._22 = context->desc.dpiY / 96.0f;
    d2d_matrix_multiply(&world, &to_device);

    if (!outline && d2d_geometry_is_pixel_aligned_rect(geometry, &world)
            && !(context->clip_stack.count && d2d_coverage_clip_is_fractional(
            &context->clip_stack.stack[context->clip_stack.count - 1])))
        return FALSE;

    to_device = geometry->transform;
    d2d_matrix_multiply(&to_device, &world);
    if (!d2d_matrix_invert(&from_device, &to_device))
        return FALSE;
    if (geometry->convex.valid)
    {
        struct d2d_aa_vertex vertices[D2D_CONVEX_MAX_VERTICES];
        struct d2d_face faces[D2D_CONVEX_MAX_FACES];
        size_t vertex_count, face_count;

        if ((vertex_count = d2d_geometry_convex_aa(geometry, &world, &from_device,
                outline ? stroke_width : 0.0f, vertices, faces, &face_count)))
        {
            d2d_device_context_request_gpu_clip(context);
            if (SUCCEEDED(d2d_device_context_update_vs_cb(context, &geometry->transform, 0.0f))
                    && SUCCEEDED(d2d_device_context_update_ps_cb(context, brush, opacity_brush, FALSE, FALSE))
                    && d2d_device_context_stream_indexed(context, faces, face_count * sizeof(*faces),
                    vertices, vertex_count * sizeof(*vertices)))
                d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE_AA, context->ib_stream.buffer,
                        face_count * 3, context->vb_stream.buffer, sizeof(*vertices), brush, opacity_brush);
            return TRUE;
        }
    }

    if (!outline && !geometry->fill.arc_vertex_count && (geometry->fill_polygon || geometry->flat.vertex_count))
    {
        struct d2d_aa_vertex *vertices;
        struct d2d_face *faces;
        size_t vertex_count, face_count;

        if (d2d_geometry_polygon_aa(geometry, &world, &from_device, &vertices, &vertex_count, &faces, &face_count))
        {
            BOOL drawn = FALSE;

            d2d_device_context_request_gpu_clip(context);
            if (SUCCEEDED(d2d_device_context_update_vs_cb(context, &geometry->transform, 0.0f))
                    && SUCCEEDED(d2d_device_context_update_ps_cb(context, brush, opacity_brush, FALSE, FALSE))
                    && d2d_device_context_stream_indexed(context, faces, face_count * sizeof(*faces),
                    vertices, vertex_count * sizeof(*vertices)))
            {
                d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE_AA, context->ib_stream.buffer,
                        face_count * 3, context->vb_stream.buffer, sizeof(*vertices), brush, opacity_brush);
                drawn = TRUE;
            }
            free(faces);
            free(vertices);
            if (drawn)
                return TRUE;
        }
    }

    if (!d2d_geometry_get_device_bounds(geometry, outline, &world, &bounds))
        return FALSE;

    if (outline)
    {
        const D2D1_MATRIX_3X2_F *g = &geometry->transform;
        float scale = fabsf(world._11) + fabsf(world._12) + fabsf(world._21) + fabsf(world._22);

        scale *= fabsf(g->_11) + fabsf(g->_12) + fabsf(g->_21) + fabsf(g->_22);
        margin = ceilf(stroke_width * scale) * 2.0f + 2.0f;
    }

    area.left = 0;
    area.top = 0;
    area.right = context->pixel_size.width;
    area.bottom = context->pixel_size.height;
    if (context->clip_stack.count)
    {
        const D2D1_RECT_F *clip = &context->clip_stack.stack[context->clip_stack.count - 1];

        area.left = max(area.left, (LONG)floorf(clip->left));
        area.top = max(area.top, (LONG)floorf(clip->top));
        area.right = min(area.right, (LONG)ceilf(clip->right));
        area.bottom = min(area.bottom, (LONG)ceilf(clip->bottom));
        clip_rect = clip;
    }
    area.left = max(area.left, (LONG)floorf(bounds.left - margin));
    area.top = max(area.top, (LONG)floorf(bounds.top - margin));
    area.right = min(area.right, (LONG)ceilf(bounds.right + margin));
    area.bottom = min(area.bottom, (LONG)ceilf(bounds.bottom + margin));
    if (area.right <= area.left || area.bottom <= area.top)
        return TRUE;

    if (!d2d_geometry_rasterize(geometry, outline, stroke_width, &world, &area, clip_rect, &context->coverage))
        return FALSE;
    if (!context->coverage.run_count)
        return TRUE;
    if (context->coverage.edge_count && !d2d_device_context_upload_coverage(context, area.right - area.left,
            area.bottom - area.top))
        return FALSE;

    d2d_device_context_draw_coverage_runs(context, geometry, brush, opacity_brush, &from_device, &area, FALSE);
    d2d_device_context_draw_coverage_runs(context, geometry, brush, opacity_brush, &from_device, &area, TRUE);

    return TRUE;
}

void d2d_device_context_fill_geometry(struct d2d_device_context *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    HRESULT hr;

    if (d2d_device_context_draw_coverage(render_target, geometry, brush, opacity_brush, FALSE, 0.0f))
        return;

    if (FAILED(hr = d2d_device_context_update_vs_cb(render_target, &geometry->transform, 0.0f)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_device_context_update_ps_cb(render_target, brush, opacity_brush, FALSE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (geometry->fill.face_count)
    {
        if (!d2d_device_context_stream_indexed(render_target,
                geometry->fill.faces, geometry->fill.face_count * sizeof(*geometry->fill.faces),
                geometry->fill.vertices, geometry->fill.vertex_count * sizeof(*geometry->fill.vertices)))
            return;

        d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_TRIANGLE, render_target->ib_stream.buffer,
                3 * geometry->fill.face_count, render_target->vb_stream.buffer,
                sizeof(*geometry->fill.vertices), brush, opacity_brush);
    }

    if (geometry->fill.bezier_vertex_count)
    {
        if (!d2d_device_context_stream_vertices(render_target, geometry->fill.bezier_vertices,
                geometry->fill.bezier_vertex_count * sizeof(*geometry->fill.bezier_vertices)))
            return;

        d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_CURVE, NULL, geometry->fill.bezier_vertex_count,
                render_target->vb_stream.buffer, sizeof(*geometry->fill.bezier_vertices), brush, opacity_brush);
    }

    if (geometry->fill.arc_vertex_count)
    {
        if (!d2d_device_context_stream_vertices(render_target, geometry->fill.arc_vertices,
                geometry->fill.arc_vertex_count * sizeof(*geometry->fill.arc_vertices)))
            return;

        if (SUCCEEDED(d2d_device_context_update_ps_cb(render_target, brush, opacity_brush, FALSE, TRUE)))
            d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_CURVE, NULL, geometry->fill.arc_vertex_count,
                    render_target->vb_stream.buffer, sizeof(*geometry->fill.arc_vertices), brush, opacity_brush);
    }
}

void STDMETHODCALLTYPE d2d_device_context_FillGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_brush *opacity_brush_impl = unsafe_impl_from_ID2D1Brush(opacity_brush);
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);

    TRACE("iface %p, geometry %p, brush %p, opacity_brush %p.\n", iface, geometry, brush, opacity_brush);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (opacity_brush && brush_impl->type != D2D_BRUSH_TYPE_BITMAP)
    {
        d2d_device_context_set_error(context, D2DERR_INCOMPATIBLE_BRUSH_TYPES);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_geometry(context->target.command_list, context, geometry, brush, opacity_brush);
    else
        d2d_device_context_fill_geometry(context, geometry_impl, brush_impl, opacity_brush_impl);
}

static void d2d_device_context_fill_mesh(struct d2d_device_context *context, const struct d2d_mesh *mesh,
        struct d2d_brush *brush)
{
    static const D2D1_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    HRESULT hr;

    if (!mesh->count)
        return;
    if (FAILED(hr = d2d_device_context_update_vs_cb(context, &identity, 0.0f)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }
    if (FAILED(hr = d2d_device_context_update_ps_cb(context, brush, NULL, FALSE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }
    if (!d2d_device_context_stream_vertices(context, mesh->triangles, mesh->count * sizeof(*mesh->triangles)))
        return;
    /* TODO: INFO_NEEDED: what Windows does with a mesh filled outside D2D1_ANTIALIAS_MODE_ALIASED; it is filled aliased */
    d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE, NULL, 3 * mesh->count, context->vb_stream.buffer,
            sizeof(D2D1_POINT_2F), brush, NULL);
}

void STDMETHODCALLTYPE d2d_device_context_FillMesh(ID2D1DeviceContext6 *iface,
        ID2D1Mesh *mesh, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, mesh %p, brush %p.\n", iface, mesh, brush);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_mesh(context->target.command_list, context, mesh, brush);
    else
        d2d_device_context_fill_mesh(context, unsafe_impl_from_ID2D1Mesh(mesh), unsafe_impl_from_ID2D1Brush(brush));
}

void STDMETHODCALLTYPE d2d_device_context_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content,
        const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, mask %p, brush %p, content %#x, dst_rect %s, src_rect %s stub!\n",
            iface, mask, brush, content, debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if ((unsigned int)content > D2D1_OPACITY_MASK_CONTENT_TEXT_GDI_COMPATIBLE)
    {
        d2d_device_context_set_error(context, E_INVALIDARG);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_opacity_mask(context->target.command_list, context, mask, brush, dst_rect, src_rect);
}

static void d2d_device_context_draw_bitmap(struct d2d_device_context *context, ID2D1Bitmap *bitmap,
        const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_POINT_2F *offset,
        const D2D1_MATRIX_4X4_F *perspective_transform)
{
    D2D1_BITMAP_BRUSH_PROPERTIES1 bitmap_brush_desc;
    D2D1_BRUSH_PROPERTIES brush_desc;
    struct d2d_brush *brush;
    D2D1_SIZE_F size;
    D2D1_RECT_F s, d;
    HRESULT hr;

    if (perspective_transform)
        FIXME("Perspective transform is ignored.\n");

    size = ID2D1Bitmap_GetSize(bitmap);
    d2d_rect_set(&s, 0.0f, 0.0f, size.width, size.height);
    if (src_rect && src_rect->left <= src_rect->right
            && src_rect->top <= src_rect->bottom)
    {
        d2d_rect_intersect(&s, src_rect);
    }

    if (s.left == s.right || s.top == s.bottom)
        return;

    if (dst_rect)
    {
        d = *dst_rect;
    }
    else
    {
        d.left = 0.0f;
        d.top = 0.0f;
        d.right = s.right - s.left;
        d.bottom = s.bottom - s.top;
    }

    if (offset)
    {
        d.left += offset->x;
        d.top += offset->y;
        d.right += offset->x;
        d.bottom += offset->y;
    }

    bitmap_brush_desc.extendModeX = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.extendModeY = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.interpolationMode = interpolation_mode;

    brush_desc.opacity = opacity;
    brush_desc.transform._11 = fabsf((d.right - d.left) / (s.right - s.left));
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._31 = min(d.left, d.right) - min(s.left, s.right) * brush_desc.transform._11;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._22 = fabsf((d.bottom - d.top) / (s.bottom - s.top));
    brush_desc.transform._32 = min(d.top, d.bottom) - min(s.top, s.bottom) * brush_desc.transform._22;

    if (FAILED(hr = d2d_bitmap_brush_create(context->factory, bitmap, &bitmap_brush_desc, &brush_desc, &brush)))
    {
        ERR("Failed to create bitmap brush, hr %#lx.\n", hr);
        return;
    }

    d2d_device_context_FillRectangle(&context->ID2D1DeviceContext6_iface, &d, &brush->ID2D1Brush_iface);
    ID2D1Brush_Release(&brush->ID2D1Brush_iface);
}

void STDMETHODCALLTYPE d2d_device_context_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, bitmap %p, dst_rect %s, opacity %.8e, interpolation_mode %#x, src_rect %s.\n",
            iface, bitmap, debug_d2d_rect_f(dst_rect), opacity, interpolation_mode, debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (interpolation_mode != D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            && interpolation_mode != D2D1_BITMAP_INTERPOLATION_MODE_LINEAR)
    {
        d2d_device_context_set_error(context, E_INVALIDARG);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_bitmap(context->target.command_list, bitmap, dst_rect, opacity,
                d2d1_1_interp_mode_from_d2d1(interpolation_mode), src_rect, NULL);
    }
    else
    {
        d2d_device_context_draw_bitmap(context, bitmap, dst_rect, opacity,
                d2d1_1_interp_mode_from_d2d1(interpolation_mode), src_rect, NULL, NULL);
    }
}

static BOOL d2d_device_context_clear_target(struct d2d_device_context *context, const D2D1_COLOR_F *colour)
{
    ID3D11DeviceContext *d3d_context;
    float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (context->clip_stack.count)
    {
        const D2D1_RECT_F *clip = &context->clip_stack.stack[context->clip_stack.count - 1];

        if (clip->left > 0.5f || clip->top > 0.5f
                || clip->right < context->pixel_size.width - 0.5f || clip->bottom < context->pixel_size.height - 0.5f)
            return FALSE;
    }

    if (colour)
    {
        c[0] = colour->r;
        c[1] = colour->g;
        c[2] = colour->b;
        c[3] = colour->a;
    }
    if (context->desc.pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE)
        c[3] = 1.0f;
    c[0] *= c[3];
    c[1] *= c[3];
    c[2] *= c[3];

    if (context->cs)
        EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);
    ID3D11DeviceContext_ClearRenderTargetView(d3d_context, context->target.bitmap->rtv, c);
    ID3D11DeviceContext_Release(d3d_context);
    if (context->cs)
        LeaveCriticalSection(context->cs);
    return TRUE;
}

void STDMETHODCALLTYPE d2d_device_context_Clear(ID2D1DeviceContext6 *iface, const D2D1_COLOR_F *colour)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    struct d2d_ps_cb *ps_cb_data;
    struct d2d_vs_cb *vs_cb_data;
    D2D1_COLOR_F *c;
    HRESULT hr;

    TRACE("iface %p, colour %p.\n", iface, colour);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_clear(context->target.command_list, colour);
        return;
    }

    if (d2d_device_context_clear_target(context, colour))
        return;
    context->vs_shadow_valid = FALSE;
    context->ps_shadow_valid = FALSE;
    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->vs_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map vs constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return;
    }

    vs_cb_data = map_desc.pData;
    vs_cb_data->transform_geometry._11 = 1.0f;
    vs_cb_data->transform_geometry._21 = 0.0f;
    vs_cb_data->transform_geometry._31 = 0.0f;
    vs_cb_data->transform_geometry.pad0 = 0.0f;
    vs_cb_data->transform_geometry._12 = 0.0f;
    vs_cb_data->transform_geometry._22 = 1.0f;
    vs_cb_data->transform_geometry._32 = 0.0f;
    vs_cb_data->transform_geometry.stroke_width = 0.0f;
    vs_cb_data->transform_rtx.x = 1.0f;
    vs_cb_data->transform_rtx.y = 0.0f;
    vs_cb_data->transform_rtx.z = 1.0f;
    vs_cb_data->transform_rtx.w = 1.0f;
    vs_cb_data->transform_rty.x = 0.0f;
    vs_cb_data->transform_rty.y = 1.0f;
    vs_cb_data->transform_rty.z = 1.0f;
    vs_cb_data->transform_rty.w = -1.0f;

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->vs_cb, 0);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->ps_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map ps constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return;
    }

    ps_cb_data = map_desc.pData;
    memset(ps_cb_data, 0, sizeof(*ps_cb_data));
    ps_cb_data->colour_brush.type = D2D_BRUSH_TYPE_SOLID;
    ps_cb_data->colour_brush.opacity = 1.0f;
    ps_cb_data->opacity_brush.type = D2D_BRUSH_TYPE_COUNT;
    c = &ps_cb_data->colour_brush.u.solid.colour;
    if (colour)
        *c = *colour;
    if (context->desc.pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE)
        c->a = 1.0f;
    c->r *= c->a;
    c->g *= c->a;
    c->b *= c->a;

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->ps_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);

    d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE, context->ib, 6,
            context->vb, context->vb_stride, NULL, NULL);
}

void d2d_device_context_release_blend_states(struct d2d_device_context *context)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(context->bs); ++i)
    {
        if (context->bs[i])
            ID3D11BlendState_Release(context->bs[i]);
        context->bs[i] = NULL;
    }
}

HRESULT d2d_device_context_create_blend_states(struct d2d_device_context *context)
{
    static const struct
    {
        D3D11_BLEND dst;
        D3D11_BLEND_OP op;
    }
    blends[] =
    {
        [D2D1_PRIMITIVE_BLEND_SOURCE_OVER] = {D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD},
        [D2D1_PRIMITIVE_BLEND_COPY] = {D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_OP_ADD},
        [D2D1_PRIMITIVE_BLEND_MIN] = {D3D11_BLEND_ONE, D3D11_BLEND_OP_MIN},
        [D2D1_PRIMITIVE_BLEND_ADD] = {D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD},
        [D2D1_PRIMITIVE_BLEND_MAX] = {D3D11_BLEND_ONE, D3D11_BLEND_OP_MAX},
    };
    D3D11_BLEND_DESC blend_desc;
    unsigned int i;
    HRESULT hr;

    C_ASSERT(ARRAY_SIZE(blends) == ARRAY_SIZE(context->bs));

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    for (i = 0; i < ARRAY_SIZE(blends); ++i)
    {
        blend_desc.RenderTarget[0].DestBlend = blends[i].dst;
        blend_desc.RenderTarget[0].DestBlendAlpha = blends[i].dst;
        blend_desc.RenderTarget[0].BlendOp = blends[i].op;
        blend_desc.RenderTarget[0].BlendOpAlpha = blends[i].op;
        if (FAILED(hr = ID3D11Device1_CreateBlendState(context->d3d_device, &blend_desc, &context->bs[i])))
        {
            WARN("Failed to create blend state %u, hr %#lx.\n", i, hr);
            return hr;
        }
    }
    return S_OK;
}

void STDMETHODCALLTYPE d2d_device_context_DrawImage(ID2D1DeviceContext6 *iface, ID2D1Image *image,
        const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect, D2D1_INTERPOLATION_MODE interpolation_mode,
        D2D1_COMPOSITE_MODE composite_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1Bitmap *bitmap;

    TRACE("iface %p, image %p, target_offset %s, image_rect %s, interpolation_mode %#x, composite_mode %#x.\n",
            iface, image, debug_d2d_point_2f(target_offset), debug_d2d_rect_f(image_rect),
            interpolation_mode, composite_mode);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_image(context->target.command_list, image, target_offset, image_rect,
                interpolation_mode, composite_mode);
        return;
    }

    if (composite_mode != D2D1_COMPOSITE_MODE_SOURCE_OVER)
        FIXME("Unhandled composite mode %#x.\n", composite_mode);

    if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        d2d_device_context_draw_bitmap(context, bitmap, NULL, 1.0f, interpolation_mode, image_rect, target_offset, NULL);

        ID2D1Bitmap_Release(bitmap);
        return;
    }

    FIXME("Unhandled image %p.\n", image);
}

void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_MATRIX_4X4_F *perspective_transform)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, bitmap %p, dst_rect %s, opacity %.8e, interpolation_mode %#x, "
            "src_rect %s, perspective_transform %p.\n",
            iface, bitmap, debug_d2d_rect_f(dst_rect), opacity, interpolation_mode,
            debug_d2d_rect_f(src_rect), perspective_transform);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_bitmap(context->target.command_list, bitmap, dst_rect, opacity, interpolation_mode,
                src_rect, perspective_transform);
    }
    else
    {
        d2d_device_context_draw_bitmap(context, bitmap, dst_rect, opacity, interpolation_mode, src_rect,
                NULL, perspective_transform);
    }
}

void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, mask %p, brush %p, dst_rect %s, src_rect %s stub!\n",
            iface, mask, brush, debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_opacity_mask(context->target.command_list, context, mask, brush, dst_rect, src_rect);
}

void STDMETHODCALLTYPE d2d_device_context_DrawGeometryRealization(ID2D1DeviceContext6 *iface,
        ID2D1GeometryRealization *realization, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *r = unsafe_impl_from_ID2D1GeometryRealization(realization);

    FIXME("iface %p, realization %p, brush %p semi-stub!\n", iface, realization, brush);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        if (r->filled)
        {
            d2d_command_list_fill_geometry(context->target.command_list, context, r->geometry, brush, NULL);
        }
        else
        {
            d2d_command_list_draw_geometry(context->target.command_list, context, r->geometry, brush,
                    r->stroke_width, r->stroke_style);
        }
        return;
    }
}
