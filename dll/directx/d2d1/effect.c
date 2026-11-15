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

static inline struct d2d_effect *impl_from_ID2D1Effect(ID2D1Effect *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect, ID2D1Effect_iface);
}

static void d2d_effect_cleanup(struct d2d_effect *effect)
{
    unsigned int i;

    for (i = 0; i < effect->input_count; ++i)
    {
        if (effect->inputs[i])
            ID2D1Image_Release(effect->inputs[i]);
    }
    free(effect->inputs);
    ID2D1EffectContext_Release(&effect->effect_context->ID2D1EffectContext_iface);
    if (effect->graph)
        ID2D1TransformGraph_Release(&effect->graph->ID2D1TransformGraph_iface);
    d2d_effect_properties_cleanup(&effect->properties);
    if (effect->impl)
        ID2D1EffectImpl_Release(effect->impl);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_QueryInterface(ID2D1Effect *iface, REFIID iid, void **out)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1Effect)
            || IsEqualGUID(iid, &IID_ID2D1Properties)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1Effect_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID2D1Image)
            || IsEqualGUID(iid, &IID_ID2D1Resource))
    {
        ID2D1Image_AddRef(&effect->ID2D1Image_iface);
        *out = &effect->ID2D1Image_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_AddRef(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    ULONG refcount = InterlockedIncrement(&effect->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_effect_Release(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    ULONG refcount = InterlockedDecrement(&effect->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_effect_cleanup(effect);
        free(effect);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyCount(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p.\n", iface);

    return ID2D1Properties_GetPropertyCount(&effect->properties.ID2D1Properties_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetPropertyName(ID2D1Effect *iface, UINT32 index,
        WCHAR *name, UINT32 name_count)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, name %p, name_count %u.\n", iface, index, name, name_count);

    return ID2D1Properties_GetPropertyName(&effect->properties.ID2D1Properties_iface,
            index, name, name_count);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyNameLength(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u.\n", iface, index);

    return ID2D1Properties_GetPropertyNameLength(&effect->properties.ID2D1Properties_iface, index);
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE d2d_effect_GetType(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return ID2D1Properties_GetType(&effect->properties.ID2D1Properties_iface, index);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyIndex(ID2D1Effect *iface, const WCHAR *name)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    return ID2D1Properties_GetPropertyIndex(&effect->properties.ID2D1Properties_iface, name);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetValueByName(ID2D1Effect *iface, const WCHAR *name,
        D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s, type %u, value %p, value_size %u.\n", iface, debugstr_w(name),
            type, value, value_size);

    return ID2D1Properties_SetValueByName(&effect->properties.ID2D1Properties_iface, name,
            type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetValue(ID2D1Effect *iface, UINT32 index, D2D1_PROPERTY_TYPE type,
        const BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    return ID2D1Properties_SetValue(&effect->properties.ID2D1Properties_iface, index, type,
            value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetValueByName(ID2D1Effect *iface, const WCHAR *name,
        D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s, type %#x, value %p, value_size %u.\n", iface, debugstr_w(name), type,
            value, value_size);

    return ID2D1Properties_GetValueByName(&effect->properties.ID2D1Properties_iface, name, type,
            value, value_size);
}

static HRESULT d2d_effect_get_value(struct d2d_effect *effect, UINT32 index, D2D1_PROPERTY_TYPE type,
        BYTE *value, UINT32 value_size)
{
    return ID2D1Properties_GetValue(&effect->properties.ID2D1Properties_iface, index, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetValue(ID2D1Effect *iface, UINT32 index, D2D1_PROPERTY_TYPE type,
        BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    return d2d_effect_get_value(effect, index, type, value, value_size);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetValueSize(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return ID2D1Properties_GetValueSize(&effect->properties.ID2D1Properties_iface, index);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetSubProperties(ID2D1Effect *iface, UINT32 index,
        ID2D1Properties **props)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, props %p.\n", iface, index, props);

    return ID2D1Properties_GetSubProperties(&effect->properties.ID2D1Properties_iface, index, props);
}

static void STDMETHODCALLTYPE d2d_effect_SetInput(ID2D1Effect *iface, UINT32 index, ID2D1Image *input, BOOL invalidate)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, input %p, invalidate %#x.\n", iface, index, input, invalidate);

    if (index >= effect->input_count)
        return;

    if (input)
        ID2D1Image_AddRef(input);
    if (effect->inputs[index])
        ID2D1Image_Release(effect->inputs[index]);
    effect->inputs[index] = input;
}

static HRESULT d2d_effect_set_input_count(struct d2d_effect *effect, UINT32 count)
{
    bool initialized = effect->inputs != NULL;
    HRESULT hr = S_OK;
    unsigned int i;

    if (count == effect->input_count)
        return S_OK;

    if (count < effect->input_count)
    {
        for (i = count; i < effect->input_count; ++i)
        {
            if (effect->inputs[i])
                ID2D1Image_Release(effect->inputs[i]);
        }
    }
    else
    {
        if (!d2d_array_reserve((void **)&effect->inputs, &effect->inputs_size,
                count, sizeof(*effect->inputs)))
        {
            ERR("Failed to resize inputs array.\n");
            return E_OUTOFMEMORY;
        }

        memset(&effect->inputs[effect->input_count], 0, sizeof(*effect->inputs) * (count - effect->input_count));
    }
    effect->input_count = count;

    if (initialized)
    {
        ID2D1TransformGraph_Release(&effect->graph->ID2D1TransformGraph_iface);
        effect->graph = NULL;

        if (SUCCEEDED(hr = d2d_transform_graph_create(count, &effect->graph)))
        {
            if (FAILED(hr = ID2D1EffectImpl_SetGraph(effect->impl, &effect->graph->ID2D1TransformGraph_iface)))
                WARN("Failed to set a new transform graph, hr %#lx.\n", hr);
        }
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetInputCount(ID2D1Effect *iface, UINT32 count)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    unsigned int min_inputs, max_inputs;

    TRACE("iface %p, count %u.\n", iface, count);

    d2d_effect_get_value(effect, D2D1_PROPERTY_MIN_INPUTS, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)&min_inputs, sizeof(min_inputs));
    d2d_effect_get_value(effect, D2D1_PROPERTY_MAX_INPUTS, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)&max_inputs, sizeof(max_inputs));

    if (count < min_inputs || count > max_inputs)
        return E_INVALIDARG;

    return d2d_effect_set_input_count(effect, count);
}

static void STDMETHODCALLTYPE d2d_effect_GetInput(ID2D1Effect *iface, UINT32 index, ID2D1Image **input)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, input %p.\n", iface, index, input);

    if (index < effect->input_count && effect->inputs[index])
        ID2D1Image_AddRef(*input = effect->inputs[index]);
    else
        *input = NULL;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetInputCount(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p.\n", iface);

    return effect->input_count;
}

static void STDMETHODCALLTYPE d2d_effect_GetOutput(ID2D1Effect *iface, ID2D1Image **output)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, output %p.\n", iface, output);

    ID2D1Image_AddRef(*output = &effect->ID2D1Image_iface);
}

static const ID2D1EffectVtbl d2d_effect_vtbl =
{
    d2d_effect_QueryInterface,
    d2d_effect_AddRef,
    d2d_effect_Release,
    d2d_effect_GetPropertyCount,
    d2d_effect_GetPropertyName,
    d2d_effect_GetPropertyNameLength,
    d2d_effect_GetType,
    d2d_effect_GetPropertyIndex,
    d2d_effect_SetValueByName,
    d2d_effect_SetValue,
    d2d_effect_GetValueByName,
    d2d_effect_GetValue,
    d2d_effect_GetValueSize,
    d2d_effect_GetSubProperties,
    d2d_effect_SetInput,
    d2d_effect_SetInputCount,
    d2d_effect_GetInput,
    d2d_effect_GetInputCount,
    d2d_effect_GetOutput,
};

static inline struct d2d_effect *impl_from_ID2D1Image(ID2D1Image *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect, ID2D1Image_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_image_QueryInterface(ID2D1Image *iface, REFIID iid, void **out)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return d2d_effect_QueryInterface(&effect->ID2D1Effect_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_effect_image_AddRef(ID2D1Image *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p.\n", iface);

    return d2d_effect_AddRef(&effect->ID2D1Effect_iface);
}

static ULONG STDMETHODCALLTYPE d2d_effect_image_Release(ID2D1Image *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p.\n", iface);

    return d2d_effect_Release(&effect->ID2D1Effect_iface);
}

static void STDMETHODCALLTYPE d2d_effect_image_GetFactory(ID2D1Image *iface, ID2D1Factory **factory)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = effect->effect_context->device_context->factory);
}

static const ID2D1ImageVtbl d2d_effect_image_vtbl =
{
    d2d_effect_image_QueryInterface,
    d2d_effect_image_AddRef,
    d2d_effect_image_Release,
    d2d_effect_image_GetFactory,
};

static inline struct d2d_effect_properties *impl_from_ID2D1Properties(ID2D1Properties *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_properties, ID2D1Properties_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_QueryInterface(ID2D1Properties *iface,
        REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &IID_ID2D1Properties) ||
            IsEqualGUID(riid, &IID_IUnknown))
    {
        *obj = iface;
        ID2D1Properties_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_properties_AddRef(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    if (properties->effect)
        return ID2D1Effect_AddRef(&properties->effect->ID2D1Effect_iface);

    return InterlockedIncrement(&properties->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_effect_properties_Release(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    ULONG refcount;

    if (properties->effect)
        return ID2D1Effect_Release(&properties->effect->ID2D1Effect_iface);

    refcount = InterlockedDecrement(&properties->refcount);

    if (!refcount)
    {
        d2d_effect_properties_cleanup(properties);
        free(properties);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyCount(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    TRACE("iface %p.\n", iface);

    return properties->custom_count;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetPropertyName(ID2D1Properties *iface,
        UINT32 index, WCHAR *name, UINT32 name_count)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u, name %p, name_count %u.\n", iface, index, name, name_count);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_return_string(prop->name, name, name_count);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyNameLength(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u.\n", iface, index);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return wcslen(prop->name);
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE d2d_effect_properties_GetType(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x.\n", iface, index);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2D1_PROPERTY_TYPE_UNKNOWN;

    return prop->type;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyIndex(ID2D1Properties *iface,
        const WCHAR *name)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2D1_INVALID_PROPERTY_INDEX;

    return prop->index;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_SetValueByName(ID2D1Properties *iface,
        const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s, type %u, value %p, value_size %u.\n", iface, debugstr_w(name),
            type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_set_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_SetValue(ID2D1Properties *iface,
        UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_set_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetValueByName(ID2D1Properties *iface,
        const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s, type %#x, value %p, value_size %u.\n", iface, debugstr_w(name), type,
            value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_get_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetValue(ID2D1Properties *iface,
        UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_get_value(properties, prop, type, value, value_size);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetValueSize(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return d2d_effect_properties_get_value_size(properties, index);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetSubProperties(ID2D1Properties *iface,
        UINT32 index, ID2D1Properties **props)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u, props %p.\n", iface, index, props);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    if (!prop->subproperties) return D2DERR_NO_SUBPROPERTIES;

    *props = &prop->subproperties->ID2D1Properties_iface;
    ID2D1Properties_AddRef(*props);
    return S_OK;
}

static const ID2D1PropertiesVtbl d2d_effect_properties_vtbl =
{
    d2d_effect_properties_QueryInterface,
    d2d_effect_properties_AddRef,
    d2d_effect_properties_Release,
    d2d_effect_properties_GetPropertyCount,
    d2d_effect_properties_GetPropertyName,
    d2d_effect_properties_GetPropertyNameLength,
    d2d_effect_properties_GetType,
    d2d_effect_properties_GetPropertyIndex,
    d2d_effect_properties_SetValueByName,
    d2d_effect_properties_SetValue,
    d2d_effect_properties_GetValueByName,
    d2d_effect_properties_GetValue,
    d2d_effect_properties_GetValueSize,
    d2d_effect_properties_GetSubProperties,
};

void d2d_effect_init_properties(struct d2d_effect *effect,
        struct d2d_effect_properties *properties)
{
    properties->ID2D1Properties_iface.lpVtbl = &d2d_effect_properties_vtbl;
    properties->effect = effect;
    properties->refcount = 1;
}

bool d2d_transform_node_needs_render_info(const struct d2d_transform_node *node)
{
    static const GUID *iids[] =
    {
        &IID_ID2D1SourceTransform,
        &IID_ID2D1ComputeTransform,
        &IID_ID2D1DrawTransform,
    };
    unsigned int i;
    IUnknown *obj;

    for (i = 0; i < ARRAY_SIZE(iids); ++i)
    {
        if (SUCCEEDED(ID2D1TransformNode_QueryInterface(node->object, iids[i], (void **)&obj)))
        {
            IUnknown_Release(obj);
            return true;
        }
    }

    return false;
}

static HRESULT d2d_effect_transform_graph_initialize_nodes(struct d2d_transform_graph *graph)
{
    ID2D1DrawTransform *draw_transform;
    struct d2d_transform_node *node;
    HRESULT hr;

    LIST_FOR_EACH_ENTRY(node, &graph->nodes, struct d2d_transform_node, entry)
    {
        if (d2d_transform_node_needs_render_info(node))
        {
            if (FAILED(hr = d2d_effect_render_info_create(&node->render_info)))
                return hr;
        }

        if (SUCCEEDED(ID2D1TransformNode_QueryInterface(node->object, &IID_ID2D1DrawTransform,
                (void **)&draw_transform)))
        {
            hr = ID2D1DrawTransform_SetDrawInfo(draw_transform, &node->render_info->ID2D1DrawInfo_iface);
            ID2D1DrawTransform_Release(draw_transform);
            if (FAILED(hr))
            {
                WARN("Failed to set draw info, hr %#lx.\n", hr);
                return hr;
            }
        }
        else
        {
            FIXME("Unsupported node %p.\n", node);
            return E_NOTIMPL;
        }
    }

    return S_OK;
}

HRESULT d2d_effect_create(struct d2d_device_context *context, const CLSID *effect_id,
        ID2D1Effect **effect)
{
    struct d2d_effect_context *effect_context;
    const struct d2d_effect_registration *reg;
    struct d2d_effect *object;
    UINT32 input_count;
    WCHAR clsidW[39];
    HRESULT hr;

    if (!(reg = d2d_factory_get_registered_effect(context->factory, effect_id)))
    {
        WARN("Effect id %s not found.\n", wine_dbgstr_guid(effect_id));
        return D2DERR_EFFECT_IS_NOT_REGISTERED;
    }

    if (!(effect_context = calloc(1, sizeof(*effect_context))))
        return E_OUTOFMEMORY;
    d2d_effect_context_init(effect_context, context);

    if (!(object = calloc(1, sizeof(*object))))
    {
        ID2D1EffectContext_Release(&effect_context->ID2D1EffectContext_iface);
        return E_OUTOFMEMORY;
    }

    object->ID2D1Effect_iface.lpVtbl = &d2d_effect_vtbl;
    object->ID2D1Image_iface.lpVtbl = &d2d_effect_image_vtbl;
    object->refcount = 1;
    object->effect_context = effect_context;

    /* Create properties */
    d2d_effect_duplicate_properties(object, &object->properties, reg->properties);

    StringFromGUID2(effect_id, clsidW, ARRAY_SIZE(clsidW));
    d2d_effect_properties_add(&object->properties, L"CLSID", D2D1_PROPERTY_CLSID, D2D1_PROPERTY_TYPE_CLSID, clsidW);
    d2d_effect_properties_add(&object->properties, L"Cached", D2D1_PROPERTY_CACHED, D2D1_PROPERTY_TYPE_BOOL, L"false");
    d2d_effect_properties_add(&object->properties, L"Precision", D2D1_PROPERTY_PRECISION, D2D1_PROPERTY_TYPE_ENUM, L"0");

    /* Sync instance input count with default input count from the description. */
    d2d_effect_get_value(object, D2D1_PROPERTY_INPUTS, D2D1_PROPERTY_TYPE_ARRAY, (BYTE *)&input_count, sizeof(input_count));
    d2d_effect_set_input_count(object, input_count);

    if (FAILED(hr = d2d_transform_graph_create(input_count, &object->graph)))
    {
        ID2D1EffectContext_Release(&effect_context->ID2D1EffectContext_iface);
        return hr;
    }

    if (FAILED(hr = reg->factory((IUnknown **)&object->impl)))
    {
        WARN("Failed to create implementation object, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    if (FAILED(hr = ID2D1EffectImpl_Initialize(object->impl, &effect_context->ID2D1EffectContext_iface,
            &object->graph->ID2D1TransformGraph_iface)))
    {
        WARN("Failed to initialize effect, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    if (FAILED(hr = d2d_effect_transform_graph_initialize_nodes(object->graph)))
    {
        WARN("Failed to initialize graph nodes, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    *effect = &object->ID2D1Effect_iface;

    TRACE("Created effect %p.\n", *effect);

    return S_OK;
}
