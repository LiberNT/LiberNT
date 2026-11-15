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

#ifndef LIBERNT_D2D1_EFFECT_H
#define LIBERNT_D2D1_EFFECT_H

#include "d2d1_common.h"

struct d2d_effect_context
{
    ID2D1EffectContext ID2D1EffectContext_iface;
    LONG refcount;

    struct d2d_device_context *device_context;
};

void d2d_effect_context_init(struct d2d_effect_context *effect_context,
        struct d2d_device_context *device_context);

struct d2d_effect_property
{
    WCHAR *name;
    D2D1_PROPERTY_TYPE type;
    UINT32 index;
    BOOL readonly;
    union
    {
        size_t offset;
        void *ptr;
    } data;
    UINT32 size;
    PD2D1_PROPERTY_SET_FUNCTION set_function;
    PD2D1_PROPERTY_GET_FUNCTION get_function;
    struct d2d_effect_properties *subproperties;
};

struct d2d_effect_properties
{
    ID2D1Properties ID2D1Properties_iface;
    LONG refcount;
    struct d2d_effect *effect;

    struct d2d_effect_property *properties;
    size_t offset;
    size_t size;
    size_t count;
    size_t custom_count;
    struct
    {
        BYTE *ptr;
        size_t size;
        size_t count;
    } data;
};

struct d2d_effect_registration
{
    struct list entry;
    PD2D1_EFFECT_FACTORY factory;
    UINT32 registration_count;
    BOOL builtin;
    CLSID id;

    struct d2d_effect_properties *properties;
};

struct d2d_transform
{
    ID2D1TransformNode ID2D1TransformNode_iface;
    LONG refcount;

    union
    {
        D2D1_POINT_2L offset;
        D2D1_BLEND_DESCRIPTION blend_desc;
        struct
        {
            D2D1_EXTEND_MODE mode_x;
            D2D1_EXTEND_MODE mode_y;
        } border;
        D2D1_RECT_L bounds;
    };

    UINT32 input_count;
};

enum d2d_render_info_mask
{
    D2D_RENDER_INFO_PIXEL_SHADER = 0x1,
};

struct d2d_render_info
{
    ID2D1DrawInfo ID2D1DrawInfo_iface;
    LONG refcount;

    unsigned int mask;
    GUID pixel_shader;
};

struct d2d_transform_node
{
    struct list entry;
    ID2D1TransformNode *object;
    struct d2d_render_info *render_info;
    struct d2d_transform_node **inputs;
    unsigned int input_count;
    struct d2d_transform_node *output;
};

struct d2d_transform_node_connection
{
    struct d2d_transform_node *node;
    unsigned int index;
};

struct d2d_transform_graph
{
    ID2D1TransformGraph ID2D1TransformGraph_iface;
    LONG refcount;

    struct d2d_transform_node_connection *inputs;
    unsigned int input_count;

    struct d2d_transform_node *output;

    bool passthrough;
    unsigned int passthrough_input;

    struct list nodes;
};

struct d2d_effect
{
    ID2D1Effect ID2D1Effect_iface;
    ID2D1Image ID2D1Image_iface;
    LONG refcount;

    ID2D1EffectImpl *impl;
    struct d2d_effect_properties properties;
    struct d2d_effect_context *effect_context;
    struct d2d_transform_graph *graph;
    ID2D1Image **inputs;
    size_t inputs_size;
    size_t input_count;
};

HRESULT d2d_effect_create(struct d2d_device_context *context, const CLSID *effect_id,
        ID2D1Effect **effect);
void d2d_effect_init_properties(struct d2d_effect *effect, struct d2d_effect_properties *properties);
HRESULT d2d_effect_properties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value);
HRESULT d2d_effect_subproperties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value);
struct d2d_effect_property * d2d_effect_properties_get_property_by_name(
        const struct d2d_effect_properties *properties, const WCHAR *name);
void d2d_effect_properties_cleanup(struct d2d_effect_properties *props);
HRESULT d2d_factory_register_builtin_effect(struct d2d_factory *factory, REFCLSID effect_id,
        const WCHAR *property_xml, const D2D1_PROPERTY_BINDING *bindings, UINT32 binding_count,
        PD2D1_EFFECT_FACTORY effect_factory);

struct d2d_vertex_buffer
{
    ID2D1VertexBuffer ID2D1VertexBuffer_iface;
    LONG refcount;
};

#endif
