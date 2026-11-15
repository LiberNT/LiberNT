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

#include "factory_private.h"
#include "xmllite.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

void d2d_effect_registration_cleanup(struct d2d_effect_registration *reg)
{
    ID2D1Properties_Release(&reg->properties->ID2D1Properties_iface);
    free(reg);
}

static BOOL WINAPI d2d_factory_builtins_initonce(INIT_ONCE *once, void *param, void **context)
{
    d2d_effects_init_builtins(param);

    return TRUE;
}

static void d2d_factory_init_builtin_effects(struct d2d_factory *factory)
{
    InitOnceExecuteOnce(&factory->init_builtins, d2d_factory_builtins_initonce, factory, NULL);
}

void d2d_factory_register_effect(struct d2d_factory *factory, struct d2d_effect_registration *effect)
{
    list_add_tail(&factory->effects, &effect->entry);
}

struct d2d_effect_registration * d2d_factory_get_registered_effect(ID2D1Factory *iface,
        const GUID *id)
{
    struct d2d_factory *factory = unsafe_impl_from_ID2D1Factory(iface);
    struct d2d_effect_registration *reg;

    d2d_factory_init_builtin_effects(factory);

    LIST_FOR_EACH_ENTRY(reg, &factory->effects, struct d2d_effect_registration, entry)
    {
        if (IsEqualGUID(id, &reg->id)) return reg;
    }

    return NULL;
}

static HRESULT parse_effect_get_next_xml_node(IXmlReader *reader, XmlNodeType expected_type,
        const WCHAR *expected_name, unsigned int *depth)
{
    const WCHAR *node_name;
    XmlNodeType node_type;
    HRESULT hr;

    assert(expected_type != XmlNodeType_Whitespace);

    while ((hr = IXmlReader_Read(reader, &node_type)) == S_OK)
    {
        if (node_type == XmlNodeType_Whitespace)
            continue;

        if (expected_type != XmlNodeType_None && node_type != expected_type)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        if (expected_name)
        {
            if (FAILED(hr = IXmlReader_GetLocalName(reader, &node_name, NULL)))
                return hr;

            if (wcscmp(node_name, expected_name))
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        if (depth)
            IXmlReader_GetDepth(reader, depth);
        return S_OK;
    }

    return hr;
}

static HRESULT parse_effect_skip_element(IXmlReader *reader, unsigned int element_depth)
{
    XmlNodeType node_type;
    unsigned int depth;
    HRESULT hr;

    if (IXmlReader_IsEmptyElement(reader)) return S_OK;

    while ((hr = IXmlReader_Read(reader, &node_type)) == S_OK)
    {
        IXmlReader_GetDepth(reader, &depth);
        if (node_type == XmlNodeType_EndElement && depth == element_depth + 1)
        {
            return S_OK;
        }
    }

    return hr;
}

static HRESULT parse_effect_get_attribute(IXmlReader *reader, const WCHAR *name, WCHAR **ret)
{
    const WCHAR *value;

    *ret = NULL;

    if (IXmlReader_MoveToAttributeByName(reader, name, NULL) != S_OK)
        return E_INVALIDARG;
    if (IXmlReader_GetValue(reader, &value, NULL) != S_OK)
        return E_INVALIDARG;
    if (!(*ret = wcsdup(value)))
        return E_OUTOFMEMORY;

    return S_OK;
}

static HRESULT parse_effect_get_property_type(IXmlReader *reader, D2D1_PROPERTY_TYPE *type)
{
    static const WCHAR *types[] =
    {
        [D2D1_PROPERTY_TYPE_UNKNOWN]       = L"",
        [D2D1_PROPERTY_TYPE_STRING]        = L"string",
        [D2D1_PROPERTY_TYPE_BOOL]          = L"bool",
        [D2D1_PROPERTY_TYPE_UINT32]        = L"uint32",
        [D2D1_PROPERTY_TYPE_INT32]         = L"int32",
        [D2D1_PROPERTY_TYPE_FLOAT]         = L"float",
        [D2D1_PROPERTY_TYPE_VECTOR2]       = L"vector2",
        [D2D1_PROPERTY_TYPE_VECTOR3]       = L"vector3",
        [D2D1_PROPERTY_TYPE_VECTOR4]       = L"vector4",
        [D2D1_PROPERTY_TYPE_BLOB]          = L"blob",
        [D2D1_PROPERTY_TYPE_IUNKNOWN]      = L"iunknown",
        [D2D1_PROPERTY_TYPE_ENUM]          = L"enum",
        [D2D1_PROPERTY_TYPE_ARRAY]         = L"array",
        [D2D1_PROPERTY_TYPE_CLSID]         = L"clsid",
        [D2D1_PROPERTY_TYPE_MATRIX_3X2]    = L"matrix3x2",
        [D2D1_PROPERTY_TYPE_MATRIX_4X3]    = L"matrix4x3",
        [D2D1_PROPERTY_TYPE_MATRIX_4X4]    = L"matrix4x4",
        [D2D1_PROPERTY_TYPE_MATRIX_5X4]    = L"matrix5x4",
        [D2D1_PROPERTY_TYPE_COLOR_CONTEXT] = L"colorcontext",
    };
    unsigned int i;
    WCHAR *value;
    HRESULT hr;

    if (FAILED(hr = parse_effect_get_attribute(reader, L"type", &value))) return hr;

    *type = D2D1_PROPERTY_TYPE_UNKNOWN;

    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        if (!wcscmp(value, types[i]))
        {
            *type = i;
            break;
        }
    }

    free(value);

    return *type == D2D1_PROPERTY_TYPE_UNKNOWN ? E_INVALIDARG : S_OK;
}

static struct d2d_effect_property * parse_effect_get_property(const struct d2d_effect_registration *effect,
        const WCHAR *name)
{
    unsigned int i;

    for (i = 0; i < effect->properties->count; ++i)
    {
        if (!wcscmp(name, effect->properties->properties[i].name))
            return &effect->properties->properties[i];
    }

    return NULL;
}

static UINT32 parse_effect_get_property_index(struct d2d_effect_properties *props,
        const WCHAR *name)
{
    if (!wcscmp(name, L"DisplayName")) return D2D1_PROPERTY_DISPLAYNAME;
    if (!wcscmp(name, L"Author")) return D2D1_PROPERTY_AUTHOR;
    if (!wcscmp(name, L"Category")) return D2D1_PROPERTY_CATEGORY;
    if (!wcscmp(name, L"Description")) return D2D1_PROPERTY_DESCRIPTION;
    return props->custom_count;
}

static HRESULT parse_effect_property(IXmlReader *reader, struct d2d_effect_registration *effect)
{
    WCHAR *name = NULL, *value = NULL;
    D2D1_PROPERTY_TYPE type;
    unsigned int depth;
    UINT32 index;
    HRESULT hr;

    if (FAILED(hr = parse_effect_get_attribute(reader, L"name", &name)))
        return hr;

    if (FAILED(hr = parse_effect_get_property_type(reader, &type)))
    {
        free(name);
        return hr;
    }

    /* Check for duplicates. */
    if (parse_effect_get_property(effect, name))
        hr = E_INVALIDARG;

    parse_effect_get_attribute(reader, L"value", &value);

    if (SUCCEEDED(hr))
    {
        /* FIXME: sub properties are ignored */
        IXmlReader_MoveToElement(reader);
        IXmlReader_GetDepth(reader, &depth);
        hr = parse_effect_skip_element(reader, depth);
    }

    if (SUCCEEDED(hr))
    {
        index = parse_effect_get_property_index(effect->properties, name);
        hr = d2d_effect_properties_add(effect->properties, name, index, type, value);
    }

    free(value);
    free(name);

    return hr;
}

static HRESULT parse_effect_inputs(IXmlReader *reader, struct d2d_effect_registration *effect)
{
    struct d2d_effect_property *inputs, *min_inputs, *max_inputs;
    struct d2d_effect_properties *subproperties;
    UINT32 min_inputs_value, max_inputs_value;
    unsigned int depth, input_count = 0;
    XmlNodeType node_type;
    WCHAR *name, *value;
    WCHAR buffW[16];
    HRESULT hr;

    if (FAILED(hr = d2d_effect_properties_add(effect->properties, L"Inputs",
            D2D1_PROPERTY_INPUTS, D2D1_PROPERTY_TYPE_ARRAY, NULL)))
        return hr;

    if (!(inputs = d2d_effect_properties_get_property_by_name(effect->properties, L"Inputs")))
        return E_FAIL;
    if (!(inputs->subproperties = calloc(1, sizeof(*inputs->subproperties))))
        return E_OUTOFMEMORY;
    d2d_effect_init_properties(NULL, inputs->subproperties);
    subproperties = inputs->subproperties;

    d2d_effect_subproperties_add(subproperties, L"IsReadOnly", D2D1_SUBPROPERTY_ISREADONLY,
            D2D1_PROPERTY_TYPE_BOOL, L"true");
    d2d_effect_subproperties_add(subproperties, L"DisplayName", D2D1_SUBPROPERTY_DISPLAYNAME,
            D2D1_PROPERTY_TYPE_STRING, L"Inputs");

    if (SUCCEEDED(parse_effect_get_attribute(reader, L"minimum", &value)))
    {
        hr = d2d_effect_properties_add(effect->properties, L"MinInputs", D2D1_PROPERTY_MIN_INPUTS,
                D2D1_PROPERTY_TYPE_UINT32, value);
        free(value);
        if (FAILED(hr)) return hr;
    }
    if (SUCCEEDED(parse_effect_get_attribute(reader, L"maximum", &value)))
    {
        hr = d2d_effect_properties_add(effect->properties, L"MaxInputs", D2D1_PROPERTY_MAX_INPUTS,
                D2D1_PROPERTY_TYPE_UINT32, value);
        free(value);
        if (FAILED(hr)) return hr;
    }

    min_inputs = d2d_effect_properties_get_property_by_name(effect->properties, L"MinInputs");
    max_inputs = d2d_effect_properties_get_property_by_name(effect->properties, L"MaxInputs");

    if (!IXmlReader_IsEmptyElement(reader))
    {
        while (parse_effect_get_next_xml_node(reader, XmlNodeType_None, L"Input", &depth) == S_OK)
        {
            if (FAILED(hr = IXmlReader_GetNodeType(reader, &node_type))) return hr;
            if (node_type == XmlNodeType_EndElement) continue;
            if (node_type != XmlNodeType_Element) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

            if (FAILED(hr = parse_effect_get_attribute(reader, L"name", &name))) return hr;

            swprintf(buffW, ARRAY_SIZE(buffW), L"%u", input_count);
            d2d_effect_subproperties_add(subproperties, buffW, input_count, D2D1_PROPERTY_TYPE_STRING, name);
            input_count++;

            free(name);
        }
        *(UINT32 *)(effect->properties->data.ptr + inputs->data.offset) = input_count;

        if (FAILED(hr = IXmlReader_GetNodeType(reader, &node_type))) return hr;
        if (node_type != XmlNodeType_EndElement) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    if (min_inputs)
        d2d_effect_property_get_uint32_value(effect->properties, min_inputs, &min_inputs_value);
    if (max_inputs)
        d2d_effect_property_get_uint32_value(effect->properties, max_inputs, &max_inputs_value);

    /* Validate the range */
    if (min_inputs && max_inputs)
    {
        if (min_inputs_value > max_inputs_value)
        {
            WARN("Invalid input count range %u - %u.\n", min_inputs_value, max_inputs_value);
            return E_INVALIDARG;
        }
    }

    /* Validate actual input count with specified range. */
    if (min_inputs && min_inputs_value > input_count)
    {
        WARN("Too few inputs were declared, expected at least %u.\n", min_inputs_value);
        return E_INVALIDARG;
    }

    if (max_inputs && max_inputs_value < input_count)
    {
        WARN("Too many inputs were declared, expected at most %u.\n", max_inputs_value);
        return E_INVALIDARG;
    }

    /* Apply default value to a missing property. If both properties are missing, add them. */
    if (min_inputs != max_inputs)
    {
        swprintf(buffW, ARRAY_SIZE(buffW), L"%u", min_inputs ? min_inputs_value : max_inputs_value);
        if (min_inputs)
            hr = d2d_effect_properties_add(effect->properties, L"MaxInputs", D2D1_PROPERTY_MAX_INPUTS, D2D1_PROPERTY_TYPE_UINT32, buffW);
        else
            hr = d2d_effect_properties_add(effect->properties, L"MinInputs", D2D1_PROPERTY_MIN_INPUTS, D2D1_PROPERTY_TYPE_UINT32, buffW);
    }
    else if (!min_inputs)
    {
        swprintf(buffW, ARRAY_SIZE(buffW), L"%u", input_count);
        hr = d2d_effect_properties_add(effect->properties, L"MinInputs", D2D1_PROPERTY_MIN_INPUTS, D2D1_PROPERTY_TYPE_UINT32, buffW);
        if (SUCCEEDED(hr))
            hr = d2d_effect_properties_add(effect->properties, L"MaxInputs", D2D1_PROPERTY_MAX_INPUTS, D2D1_PROPERTY_TYPE_UINT32, buffW);
    }

    return hr;
}

static HRESULT parse_effect_xml(IXmlReader *reader, struct d2d_effect_registration *effect)
{
    const WCHAR *node_name;
    XmlNodeType node_type;
    unsigned int depth;
    HRESULT hr;

    /* Xml declaration node is mandatory. */
    if ((hr = parse_effect_get_next_xml_node(reader, XmlNodeType_XmlDeclaration, L"xml", NULL)) != S_OK)
        return hr;

    /* Top level "Effect" element. */
    if ((hr = parse_effect_get_next_xml_node(reader, XmlNodeType_Element, L"Effect", NULL)) != S_OK)
        return hr;

    /* Loop inside effect node */
    while ((hr = parse_effect_get_next_xml_node(reader, XmlNodeType_None, NULL, &depth)) == S_OK)
    {
        if (FAILED(hr = IXmlReader_GetNodeType(reader, &node_type))) return hr;
        if (node_type != XmlNodeType_Element && node_type != XmlNodeType_EndElement) continue;
        if (FAILED(hr = IXmlReader_GetLocalName(reader, &node_name, NULL))) return hr;
        if (node_type == XmlNodeType_EndElement) break;

        if (!wcscmp(node_name, L"Property"))
            hr = parse_effect_property(reader, effect);
        else if (!wcscmp(node_name, L"Inputs"))
            hr = parse_effect_inputs(reader, effect);
        else
        {
            WARN("Unexpected element %s.\n", debugstr_w(node_name));
            hr = parse_effect_skip_element(reader, depth);
        }

        if (FAILED(hr))
            return hr;
    }

    return hr;
}

static HRESULT d2d_factory_register_effect_from_stream(struct d2d_factory *factory,
        REFCLSID effect_id, IStream *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory, BOOL builtin)
{
    struct d2d_effect_registration *effect;
    IXmlReader *reader;
    unsigned int i;
    HRESULT hr;

    LIST_FOR_EACH_ENTRY_REV(effect, &factory->effects, struct d2d_effect_registration, entry)
    {
        if (IsEqualGUID(effect_id, &effect->id))
        {
            if (effect->builtin) return E_INVALIDARG;
            ++effect->registration_count;
            return S_OK;
        }
    }

    if (FAILED(hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL)))
        return hr;

    if (FAILED(hr = IXmlReader_SetInput(reader, (IUnknown *)property_xml)))
    {
        IXmlReader_Release(reader);
        return hr;
    }

    if (!(effect = calloc(1, sizeof(*effect))))
    {
        IXmlReader_Release(reader);
        return E_OUTOFMEMORY;
    }
    if (!(effect->properties = calloc(1, sizeof(*effect->properties))))
    {
        IXmlReader_Release(reader);
        free(effect);
        return E_OUTOFMEMORY;
    }
    d2d_effect_init_properties(NULL, effect->properties);
    effect->builtin = builtin;

    hr = parse_effect_xml(reader, effect);
    IXmlReader_Release(reader);
    if (FAILED(hr))
    {
        WARN("Failed to parse effect xml, hr %#lx.\n", hr);
        d2d_effect_registration_cleanup(effect);
        return hr;
    }

    /* Check required properties. */
    if (!parse_effect_get_property(effect, L"DisplayName")
            || !parse_effect_get_property(effect, L"Author")
            || !parse_effect_get_property(effect, L"Category")
            || !parse_effect_get_property(effect, L"Description")
            || !parse_effect_get_property(effect, L"Inputs"))
    {
        WARN("Missing required properties.\n");
        d2d_effect_registration_cleanup(effect);
        return E_INVALIDARG;
    }

    /* Bind getter and setter. */
    for (i = 0; i < binding_count; ++i)
    {
        struct d2d_effect_property *property;

        if (!(property = parse_effect_get_property(effect, bindings[i].propertyName)))
        {
            WARN("Failed to bind to missing property.\n");
            d2d_effect_registration_cleanup(effect);
            return D2DERR_INVALID_PROPERTY;
        }

        property->get_function = bindings[i].getFunction;
        property->set_function = bindings[i].setFunction;
    }

    effect->registration_count = 1;
    effect->id = *effect_id;
    effect->factory = effect_factory;

    d2d_factory_register_effect(factory, effect);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE d2d_factory_RegisterEffectFromStream(ID2D1Factory7 *iface,
        REFCLSID effect_id, IStream *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);

    TRACE("iface %p, effect_id %s, property_xml %p, bindings %p, binding_count %u, effect_factory %p.\n",
            iface, debugstr_guid(effect_id), property_xml, bindings, binding_count, effect_factory);

    d2d_factory_init_builtin_effects(factory);

    return d2d_factory_register_effect_from_stream(factory, effect_id, property_xml, bindings,
            binding_count, effect_factory, FALSE);
}

static HRESULT d2d_factory_register_effect_from_string(struct d2d_factory *factory,
        REFCLSID effect_id, const WCHAR *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory, BOOL builtin)
{
    static const LARGE_INTEGER zero;
    IStream *stream;
    ULONG size;
    HRESULT hr;

    if (FAILED(hr = CreateStreamOnHGlobal(NULL, TRUE, &stream)))
        return hr;

    size = sizeof(*property_xml) * (wcslen(property_xml) + 1);
    if (SUCCEEDED(hr = IStream_Write(stream, property_xml, size, NULL)))
        hr = IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);

    if (SUCCEEDED(hr))
        hr = d2d_factory_register_effect_from_stream(factory, effect_id, stream, bindings,
                binding_count, effect_factory, builtin);

    IStream_Release(stream);
    return hr;
}

HRESULT d2d_factory_register_builtin_effect(struct d2d_factory *factory, REFCLSID effect_id,
        const WCHAR *property_xml, const D2D1_PROPERTY_BINDING *bindings, UINT32 binding_count,
        PD2D1_EFFECT_FACTORY effect_factory)
{
    return d2d_factory_register_effect_from_string(factory, effect_id, property_xml, bindings,
            binding_count, effect_factory, TRUE);
}

HRESULT STDMETHODCALLTYPE d2d_factory_RegisterEffectFromString(ID2D1Factory7 *iface,
        REFCLSID effect_id, const WCHAR *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);

    TRACE("iface %p, effect_id %s, property_xml %s, bindings %p, binding_count %u, effect_factory %p.\n",
          iface, debugstr_guid(effect_id), debugstr_w(property_xml), bindings, binding_count, effect_factory);

    d2d_factory_init_builtin_effects(factory);

    return d2d_factory_register_effect_from_string(factory, effect_id, property_xml, bindings,
            binding_count, effect_factory, FALSE);
}

HRESULT STDMETHODCALLTYPE d2d_factory_UnregisterEffect(ID2D1Factory7 *iface, REFCLSID effect_id)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    struct d2d_effect_registration *effect;

    TRACE("iface %p, effect_id %s.\n", iface, debugstr_guid(effect_id));

    d2d_factory_init_builtin_effects(factory);

    LIST_FOR_EACH_ENTRY_REV(effect, &factory->effects, struct d2d_effect_registration, entry)
    {
        if (IsEqualGUID(effect_id, &effect->id))
        {
            if (effect->builtin) break;
            if (!--effect->registration_count)
            {
                list_remove(&effect->entry);
                d2d_effect_registration_cleanup(effect);
            }
            return S_OK;
        }
    }

    return D2DERR_EFFECT_IS_NOT_REGISTERED;
}

HRESULT STDMETHODCALLTYPE d2d_factory_GetRegisteredEffects(ID2D1Factory7 *iface,
        CLSID *effects, UINT32 effect_count, UINT32 *returned, UINT32 *registered)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    struct d2d_effect_registration *effect;
    UINT32 ret, reg;

    TRACE("iface %p, effects %p, effect_count %u, returned %p, registered %p.\n",
            iface, effects, effect_count, returned, registered);

    if (!returned) returned = &ret;
    if (!registered) registered = &reg;

    *registered = 0;
    *returned = 0;

    d2d_factory_init_builtin_effects(factory);

    LIST_FOR_EACH_ENTRY(effect, &factory->effects, struct d2d_effect_registration, entry)
    {
        if (effects && effect_count)
        {
            *effects = effect->id;
            effects++;
            effect_count--;
            *returned += 1;
        }

        *registered += 1;
    }

    if (!effects) return S_OK;
    return *returned == *registered ? S_OK : D2DERR_INSUFFICIENT_BUFFER;
}

HRESULT STDMETHODCALLTYPE d2d_factory_GetEffectProperties(ID2D1Factory7 *iface,
        REFCLSID effect_id, ID2D1Properties **props)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    const struct d2d_effect_registration *reg;

    TRACE("iface %p, effect_id %s, props %p.\n", iface, debugstr_guid(effect_id), props);

    d2d_factory_init_builtin_effects(factory);

    if (!(reg = d2d_factory_get_registered_effect((ID2D1Factory *)iface, effect_id)))
    {
        WARN("Effect id %s not found.\n", wine_dbgstr_guid(effect_id));
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    *props = &reg->properties->ID2D1Properties_iface;
    ID2D1Properties_AddRef(*props);

    return S_OK;
}
