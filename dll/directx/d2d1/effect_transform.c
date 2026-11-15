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

static inline struct d2d_transform *impl_from_ID2D1OffsetTransform(ID2D1OffsetTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BlendTransform(ID2D1BlendTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BorderTransform(ID2D1BorderTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BoundsAdjustmentTransform(
        ID2D1BoundsAdjustmentTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_vertex_buffer *impl_from_ID2D1VertexBuffer(ID2D1VertexBuffer *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_vertex_buffer, ID2D1VertexBuffer_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_QueryInterface(ID2D1VertexBuffer *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1VertexBuffer)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1VertexBuffer_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_vertex_buffer_AddRef(ID2D1VertexBuffer *iface)
{
    struct d2d_vertex_buffer *buffer = impl_from_ID2D1VertexBuffer(iface);
    ULONG refcount = InterlockedIncrement(&buffer->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_vertex_buffer_Release(ID2D1VertexBuffer *iface)
{
    struct d2d_vertex_buffer *buffer = impl_from_ID2D1VertexBuffer(iface);
    ULONG refcount = InterlockedDecrement(&buffer->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(buffer);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_Map(ID2D1VertexBuffer *iface, BYTE **data, UINT32 size)
{
    FIXME("iface %p, data %p, size %u.\n", iface, data, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_Unmap(ID2D1VertexBuffer *iface)
{
    FIXME("iface %p.\n", iface);

    return E_NOTIMPL;
}

static const ID2D1VertexBufferVtbl d2d_vertex_buffer_vtbl =
{
    d2d_vertex_buffer_QueryInterface,
    d2d_vertex_buffer_AddRef,
    d2d_vertex_buffer_Release,
    d2d_vertex_buffer_Map,
    d2d_vertex_buffer_Unmap,
};

HRESULT d2d_vertex_buffer_create(ID2D1VertexBuffer **buffer)
{
    struct d2d_vertex_buffer *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1VertexBuffer_iface.lpVtbl = &d2d_vertex_buffer_vtbl;
    object->refcount = 1;

    *buffer = &object->ID2D1VertexBuffer_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_offset_transform_QueryInterface(ID2D1OffsetTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1OffsetTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1OffsetTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_offset_transform_AddRef(ID2D1OffsetTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_offset_transform_Release(ID2D1OffsetTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_offset_transform_GetInputCount(ID2D1OffsetTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static void STDMETHODCALLTYPE d2d_offset_transform_SetOffset(ID2D1OffsetTransform *iface,
        D2D1_POINT_2L offset)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);

    TRACE("iface %p, offset %s.\n", iface, debug_d2d_point_2l(&offset));

    transform->offset = offset;
}

static D2D1_POINT_2L * STDMETHODCALLTYPE d2d_offset_transform_GetOffset(ID2D1OffsetTransform *iface,
        D2D1_POINT_2L *offset)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);

    TRACE("iface %p.\n", iface);

    *offset = transform->offset;
    return offset;
}

static const ID2D1OffsetTransformVtbl d2d_offset_transform_vtbl =
{
    d2d_offset_transform_QueryInterface,
    d2d_offset_transform_AddRef,
    d2d_offset_transform_Release,
    d2d_offset_transform_GetInputCount,
    d2d_offset_transform_SetOffset,
    d2d_offset_transform_GetOffset,
};

HRESULT d2d_offset_transform_create(D2D1_POINT_2L offset, ID2D1OffsetTransform **transform)
{
    struct d2d_transform *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_offset_transform_vtbl;
    object->refcount = 1;
    object->offset = offset;

    *transform = (ID2D1OffsetTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_blend_transform_QueryInterface(ID2D1BlendTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BlendTransform)
            || IsEqualGUID(iid, &IID_ID2D1ConcreteTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BlendTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_blend_transform_AddRef(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_blend_transform_Release(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_blend_transform_GetInputCount(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->input_count;
}

static HRESULT STDMETHODCALLTYPE d2d_blend_transform_SetOutputBuffer(ID2D1BlendTransform *iface,
        D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_blend_transform_SetCached(ID2D1BlendTransform *iface,
        BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_blend_transform_SetDescription(ID2D1BlendTransform *iface,
        const D2D1_BLEND_DESCRIPTION *description)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p, description %p.\n", iface, description);

    transform->blend_desc = *description;
}

static void STDMETHODCALLTYPE d2d_blend_transform_GetDescription(ID2D1BlendTransform *iface,
        D2D1_BLEND_DESCRIPTION *description)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p, description %p.\n", iface, description);

    *description = transform->blend_desc;
}

static const ID2D1BlendTransformVtbl d2d_blend_transform_vtbl =
{
    d2d_blend_transform_QueryInterface,
    d2d_blend_transform_AddRef,
    d2d_blend_transform_Release,
    d2d_blend_transform_GetInputCount,
    d2d_blend_transform_SetOutputBuffer,
    d2d_blend_transform_SetCached,
    d2d_blend_transform_SetDescription,
    d2d_blend_transform_GetDescription,
};

HRESULT d2d_blend_transform_create(UINT32 input_count, const D2D1_BLEND_DESCRIPTION *blend_desc,
        ID2D1BlendTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!input_count)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_blend_transform_vtbl;
    object->refcount = 1;
    object->input_count = input_count;
    object->blend_desc = *blend_desc;

    *transform = (ID2D1BlendTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_border_transform_QueryInterface(ID2D1BorderTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BorderTransform)
            || IsEqualGUID(iid, &IID_ID2D1ConcreteTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BorderTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_border_transform_AddRef(ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_border_transform_Release(ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_border_transform_GetInputCount(ID2D1BorderTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static HRESULT STDMETHODCALLTYPE d2d_border_transform_SetOutputBuffer(
        ID2D1BorderTransform *iface, D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_border_transform_SetCached(
        ID2D1BorderTransform *iface, BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_border_transform_SetExtendModeX(
        ID2D1BorderTransform *iface, D2D1_EXTEND_MODE mode)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    switch (mode)
    {
        case D2D1_EXTEND_MODE_CLAMP:
        case D2D1_EXTEND_MODE_WRAP:
        case D2D1_EXTEND_MODE_MIRROR:
            transform->border.mode_x = mode;
            break;
        default:
            ;
    }
}

static void STDMETHODCALLTYPE d2d_border_transform_SetExtendModeY(
        ID2D1BorderTransform *iface, D2D1_EXTEND_MODE mode)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    switch (mode)
    {
        case D2D1_EXTEND_MODE_CLAMP:
        case D2D1_EXTEND_MODE_WRAP:
        case D2D1_EXTEND_MODE_MIRROR:
            transform->border.mode_y = mode;
            break;
        default:
            ;
    }
}

static D2D1_EXTEND_MODE STDMETHODCALLTYPE d2d_border_transform_GetExtendModeX(
        ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->border.mode_x;
}

static D2D1_EXTEND_MODE STDMETHODCALLTYPE d2d_border_transform_GetExtendModeY(
        ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->border.mode_y;
}

static const ID2D1BorderTransformVtbl d2d_border_transform_vtbl =
{
    d2d_border_transform_QueryInterface,
    d2d_border_transform_AddRef,
    d2d_border_transform_Release,
    d2d_border_transform_GetInputCount,
    d2d_border_transform_SetOutputBuffer,
    d2d_border_transform_SetCached,
    d2d_border_transform_SetExtendModeX,
    d2d_border_transform_SetExtendModeY,
    d2d_border_transform_GetExtendModeX,
    d2d_border_transform_GetExtendModeY,
};

HRESULT d2d_border_transform_create(D2D1_EXTEND_MODE mode_x, D2D1_EXTEND_MODE mode_y,
        ID2D1BorderTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_border_transform_vtbl;
    object->refcount = 1;
    object->border.mode_x = mode_x;
    object->border.mode_y = mode_y;

    *transform = (ID2D1BorderTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_bounds_adjustment_transform_QueryInterface(
        ID2D1BoundsAdjustmentTransform *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BoundsAdjustmentTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BoundsAdjustmentTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_bounds_adjustment_transform_AddRef(
        ID2D1BoundsAdjustmentTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_bounds_adjustment_transform_Release(
        ID2D1BoundsAdjustmentTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_bounds_adjustment_transform_GetInputCount(
        ID2D1BoundsAdjustmentTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static void STDMETHODCALLTYPE d2d_bounds_adjustment_transform_SetOutputBounds(
        ID2D1BoundsAdjustmentTransform *iface, const D2D1_RECT_L *bounds)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);

    TRACE("iface %p.\n", iface);

    transform->bounds = *bounds;
}

static void STDMETHODCALLTYPE d2d_bounds_adjustment_transform_GetOutputBounds(
        ID2D1BoundsAdjustmentTransform *iface, D2D1_RECT_L *bounds)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);

    TRACE("iface %p.\n", iface);

    *bounds = transform->bounds;
}

static const ID2D1BoundsAdjustmentTransformVtbl d2d_bounds_adjustment_transform_vtbl =
{
    d2d_bounds_adjustment_transform_QueryInterface,
    d2d_bounds_adjustment_transform_AddRef,
    d2d_bounds_adjustment_transform_Release,
    d2d_bounds_adjustment_transform_GetInputCount,
    d2d_bounds_adjustment_transform_SetOutputBounds,
    d2d_bounds_adjustment_transform_GetOutputBounds,
};

HRESULT d2d_bounds_adjustment_transform_create(const D2D1_RECT_L *rect,
        ID2D1BoundsAdjustmentTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_bounds_adjustment_transform_vtbl;
    object->refcount = 1;
    object->bounds = *rect;

    *transform = (ID2D1BoundsAdjustmentTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static struct d2d_transform_node * d2d_transform_graph_get_node(const struct d2d_transform_graph *graph,
        ID2D1TransformNode *object)
{
    struct d2d_transform_node *node;

    LIST_FOR_EACH_ENTRY(node, &graph->nodes, struct d2d_transform_node, entry)
    {
        if (node->object == object)
            return node;
    }

    return NULL;
}

static HRESULT d2d_transform_graph_add_node(struct d2d_transform_graph *graph,
        ID2D1TransformNode *object)
{
    struct d2d_transform_node *node;

    if (!(node = calloc(1, sizeof(*node))))
        return E_OUTOFMEMORY;
    node->input_count = ID2D1TransformNode_GetInputCount(object);
    if (!(node->inputs = calloc(node->input_count, sizeof(*node->inputs))))
    {
        free(node);
        return E_OUTOFMEMORY;
    }

    node->object = object;
    ID2D1TransformNode_AddRef(node->object);
    list_add_tail(&graph->nodes, &node->entry);

    return S_OK;
}

static void d2d_transform_node_disconnect(struct d2d_transform_node *node)
{
    struct d2d_transform_node *output = node->output;
    unsigned int i;

    if (!output)
        return;

    for (i = 0; i < output->input_count; ++i)
    {
        if (output->inputs[i] == node)
        {
            output->inputs[i] = NULL;
            break;
        }
    }
}

static void d2d_transform_graph_delete_node(struct d2d_transform_graph *graph,
        struct d2d_transform_node *node)
{
    unsigned int i;

    list_remove(&node->entry);
    ID2D1TransformNode_Release(node->object);

    for (i = 0; i < graph->input_count; ++i)
    {
        if (graph->inputs[i].node == node)
            memset(&graph->inputs[i].node, 0, sizeof(graph->inputs[i].node));
    }

    if (graph->output == node)
        graph->output = NULL;

    if (node->render_info)
        ID2D1DrawInfo_Release(&node->render_info->ID2D1DrawInfo_iface);

    d2d_transform_node_disconnect(node);

    free(node->inputs);
    free(node);
}

static void d2d_transform_graph_clear(struct d2d_transform_graph *graph)
{
    struct d2d_transform_node *node, *node_next;

    LIST_FOR_EACH_ENTRY_SAFE(node, node_next, &graph->nodes, struct d2d_transform_node, entry)
    {
        d2d_transform_graph_delete_node(graph, node);
    }
    graph->passthrough = false;
}

static inline struct d2d_transform_graph *impl_from_ID2D1TransformGraph(ID2D1TransformGraph *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform_graph, ID2D1TransformGraph_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_QueryInterface(ID2D1TransformGraph *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1TransformGraph)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1TransformGraph_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_transform_graph_AddRef(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph =impl_from_ID2D1TransformGraph(iface);
    ULONG refcount = InterlockedIncrement(&graph->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_transform_graph_Release(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    ULONG refcount = InterlockedDecrement(&graph->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_transform_graph_clear(graph);
        free(graph->inputs);
        free(graph);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_transform_graph_GetInputCount(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p.\n", iface);

    return graph->input_count;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetSingleTransformNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;
    unsigned int i, input_count;
    HRESULT hr;

    TRACE("iface %p, object %p.\n", iface, object);

    d2d_transform_graph_clear(graph);
    if (FAILED(hr = d2d_transform_graph_add_node(graph, object)))
        return hr;

    node = d2d_transform_graph_get_node(graph, object);
    graph->output = node;

    input_count = ID2D1TransformNode_GetInputCount(object);
    if (graph->input_count != input_count)
        return E_INVALIDARG;

    for (i = 0; i < graph->input_count; ++i)
    {
        graph->inputs[i].node = node;
        graph->inputs[i].index = i;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_AddNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p, object %p.\n", iface, object);

    if (d2d_transform_graph_get_node(graph, object))
        return E_INVALIDARG;

    return d2d_transform_graph_add_node(graph, object);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_RemoveNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;

    TRACE("iface %p, object %p.\n", iface, object);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    d2d_transform_graph_delete_node(graph, node);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetOutputNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;

    TRACE("iface %p, object %p.\n", iface, object);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    graph->output = node;
    graph->passthrough = false;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_ConnectNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *from_node, ID2D1TransformNode *to_node, UINT32 index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *from, *to;

    TRACE("iface %p, from_node %p, to_node %p, index %u.\n", iface, from_node, to_node, index);

    if (!(from = d2d_transform_graph_get_node(graph, from_node)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (!(to = d2d_transform_graph_get_node(graph, to_node)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (index >= to->input_count)
        return E_INVALIDARG;

    d2d_transform_node_disconnect(from);
    to->inputs[index] = from;
    from->output = to;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_ConnectToEffectInput(ID2D1TransformGraph *iface,
        UINT32 input_index, ID2D1TransformNode *object, UINT32 node_index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;
    unsigned int count;

    TRACE("iface %p, input_index %u, object %p, node_index %u.\n", iface, input_index, object, node_index);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (input_index >= graph->input_count)
        return E_INVALIDARG;

    count = ID2D1TransformNode_GetInputCount(object);
    if (node_index >= count)
        return E_INVALIDARG;

    graph->inputs[input_index].node = node;
    graph->inputs[input_index].index = node_index;
    graph->passthrough = false;

    return S_OK;
}

static void STDMETHODCALLTYPE d2d_transform_graph_Clear(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p.\n", iface);

    d2d_transform_graph_clear(graph);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetPassthroughGraph(ID2D1TransformGraph *iface, UINT32 index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p, index %u.\n", iface, index);

    if (index >= graph->input_count)
        return E_INVALIDARG;

    d2d_transform_graph_clear(graph);

    graph->passthrough = true;
    graph->passthrough_input = index;

    return S_OK;
}

static const ID2D1TransformGraphVtbl d2d_transform_graph_vtbl =
{
    d2d_transform_graph_QueryInterface,
    d2d_transform_graph_AddRef,
    d2d_transform_graph_Release,
    d2d_transform_graph_GetInputCount,
    d2d_transform_graph_SetSingleTransformNode,
    d2d_transform_graph_AddNode,
    d2d_transform_graph_RemoveNode,
    d2d_transform_graph_SetOutputNode,
    d2d_transform_graph_ConnectNode,
    d2d_transform_graph_ConnectToEffectInput,
    d2d_transform_graph_Clear,
    d2d_transform_graph_SetPassthroughGraph,
};

HRESULT d2d_transform_graph_create(UINT32 input_count, struct d2d_transform_graph **graph)
{
    struct d2d_transform_graph *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformGraph_iface.lpVtbl = &d2d_transform_graph_vtbl;
    object->refcount = 1;
    list_init(&object->nodes);

    if (!(object->inputs = calloc(input_count, sizeof(*object->inputs))))
    {
        free(object);
        return E_OUTOFMEMORY;
    }
    object->input_count = input_count;

    *graph = object;

    return S_OK;
}
