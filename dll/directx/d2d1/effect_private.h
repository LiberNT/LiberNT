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


#ifndef LIBERNT_D2D1_EFFECT_PRIVATE_H
#define LIBERNT_D2D1_EFFECT_PRIVATE_H

#include "d2d1_private.h"

struct d2d_effect_impl
{
    ID2D1EffectImpl ID2D1EffectImpl_iface;
    LONG refcount;
    void (*destroy)(struct d2d_effect_impl *effect);

    /* Followed by properties block, its size and format depends on particular effect. */
};

HRESULT d2d_vertex_buffer_create(ID2D1VertexBuffer **buffer);
HRESULT d2d_offset_transform_create(D2D1_POINT_2L offset, ID2D1OffsetTransform **transform);
HRESULT d2d_blend_transform_create(UINT32 input_count, const D2D1_BLEND_DESCRIPTION *blend_desc,
        ID2D1BlendTransform **transform);
HRESULT d2d_border_transform_create(D2D1_EXTEND_MODE mode_x, D2D1_EXTEND_MODE mode_y,
        ID2D1BorderTransform **transform);
HRESULT d2d_bounds_adjustment_transform_create(const D2D1_RECT_L *rect,
        ID2D1BoundsAdjustmentTransform **transform);
HRESULT d2d_transform_graph_create(UINT32 input_count, struct d2d_transform_graph **graph);
HRESULT d2d_effect_duplicate_properties(struct d2d_effect *effect,
        struct d2d_effect_properties *dst, const struct d2d_effect_properties *src);
struct d2d_effect_property *d2d_effect_properties_get_property_by_index(
        const struct d2d_effect_properties *properties, UINT32 index);
UINT32 d2d_effect_properties_get_value_size(const struct d2d_effect_properties *properties, UINT32 index);
HRESULT d2d_effect_return_string(const WCHAR *str, WCHAR *buffer, UINT32 buffer_size);
HRESULT d2d_effect_property_get_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 size);
HRESULT d2d_effect_property_set_value(struct d2d_effect_properties *properties,
        struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 size);
HRESULT d2d_effect_render_info_create(struct d2d_render_info **obj);
bool d2d_transform_node_needs_render_info(const struct d2d_transform_node *node);

#endif
