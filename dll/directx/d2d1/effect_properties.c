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

static HRESULT d2d_effect_parse_float_array(D2D1_PROPERTY_TYPE type, const WCHAR *value,
        float *vec)
{
    unsigned int i, num_components;
    WCHAR *end_ptr;

    /* Type values are sequential. */
    switch (type)
    {
        case D2D1_PROPERTY_TYPE_VECTOR2:
        case D2D1_PROPERTY_TYPE_VECTOR3:
        case D2D1_PROPERTY_TYPE_VECTOR4:
            num_components = (type - D2D1_PROPERTY_TYPE_VECTOR2) + 2;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_3X2:
            num_components = 6;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_4X3:
        case D2D1_PROPERTY_TYPE_MATRIX_4X4:
        case D2D1_PROPERTY_TYPE_MATRIX_5X4:
            num_components = (type - D2D1_PROPERTY_TYPE_MATRIX_4X3) * 4 + 12;
            break;
        default:
            return E_UNEXPECTED;
    }

    if (*(value++) != '(') return E_INVALIDARG;

    for (i = 0; i < num_components; ++i)
    {
        vec[i] = wcstof(value, &end_ptr);
        if (value == end_ptr) return E_INVALIDARG;
        value = end_ptr;

        /* Trailing characters after last component are ignored. */
        if (i == num_components - 1) continue;
        if (*(value++) != ',') return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT d2d_effect_properties_internal_add(struct d2d_effect_properties *props,
        const WCHAR *name, UINT32 index, BOOL subprop, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    static const UINT32 sizes[] =
    {
        [D2D1_PROPERTY_TYPE_UNKNOWN]       = 0,
        [D2D1_PROPERTY_TYPE_STRING]        = 0,
        [D2D1_PROPERTY_TYPE_BOOL]          = sizeof(BOOL),
        [D2D1_PROPERTY_TYPE_UINT32]        = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_INT32]         = sizeof(INT32),
        [D2D1_PROPERTY_TYPE_FLOAT]         = sizeof(float),
        [D2D1_PROPERTY_TYPE_VECTOR2]       = sizeof(D2D_VECTOR_2F),
        [D2D1_PROPERTY_TYPE_VECTOR3]       = sizeof(D2D_VECTOR_3F),
        [D2D1_PROPERTY_TYPE_VECTOR4]       = sizeof(D2D_VECTOR_4F),
        [D2D1_PROPERTY_TYPE_BLOB]          = 0,
        [D2D1_PROPERTY_TYPE_IUNKNOWN]      = sizeof(IUnknown *),
        [D2D1_PROPERTY_TYPE_ENUM]          = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_ARRAY]         = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_CLSID]         = sizeof(CLSID),
        [D2D1_PROPERTY_TYPE_MATRIX_3X2]    = sizeof(D2D_MATRIX_3X2_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X3]    = sizeof(D2D_MATRIX_4X3_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X4]    = sizeof(D2D_MATRIX_4X4_F),
        [D2D1_PROPERTY_TYPE_MATRIX_5X4]    = sizeof(D2D_MATRIX_5X4_F),
        [D2D1_PROPERTY_TYPE_COLOR_CONTEXT] = sizeof(ID2D1ColorContext *),
    };
    struct d2d_effect_property *p;
    HRESULT hr;

    assert(type >= D2D1_PROPERTY_TYPE_STRING && type <= D2D1_PROPERTY_TYPE_COLOR_CONTEXT);

    if (!d2d_array_reserve((void **)&props->properties, &props->size, props->count + 1,
            sizeof(*props->properties)))
    {
        return E_OUTOFMEMORY;
    }

    /* TODO: we could save some space for properties that have both getter and setter. */
    if (!d2d_array_reserve((void **)&props->data.ptr, &props->data.size,
            props->data.count + sizes[type], sizeof(*props->data.ptr)))
    {
        return E_OUTOFMEMORY;
    }
    props->data.count += sizes[type];

    p = &props->properties[props->count++];
    memset(p, 0, sizeof(*p));
    p->index = index;
    if (p->index < 0x80000000)
    {
        props->custom_count++;
        /* FIXME: this should probably be controlled by subproperty */
        p->readonly = FALSE;
    }
    else if (subprop)
        p->readonly = TRUE;
    else
        p->readonly = index != D2D1_PROPERTY_CACHED && index != D2D1_PROPERTY_PRECISION;
    p->name = wcsdup(name);
    p->type = type;
    if (p->type == D2D1_PROPERTY_TYPE_STRING)
    {
        p->data.ptr = wcsdup(value);
        p->size = value ? (wcslen(value) + 1) * sizeof(WCHAR) : sizeof(WCHAR);
    }
    else if (p->type == D2D1_PROPERTY_TYPE_BLOB)
    {
        p->data.ptr = NULL;
        p->size = 0;
    }
    else
    {
        void *src = NULL;
        WCHAR *end_ptr;
        UINT32 _uint32;
        float _vec[20];
        CLSID _clsid;
        BOOL _bool;

        p->data.offset = props->offset;
        p->size = sizes[type];
        props->offset += p->size;

        if (value)
        {
            switch (p->type)
            {
                case D2D1_PROPERTY_TYPE_UINT32:
                case D2D1_PROPERTY_TYPE_INT32:
                    _uint32 = wcstoul(value, NULL, 0);
                    src = &_uint32;
                    break;
                case D2D1_PROPERTY_TYPE_FLOAT:
                    _vec[0] = wcstof(value, &end_ptr);
                    src = &_vec[0];
                    break;
                case D2D1_PROPERTY_TYPE_ENUM:
                    _uint32 = wcstoul(value, NULL, 10);
                    src = &_uint32;
                    break;
                case D2D1_PROPERTY_TYPE_BOOL:
                    if (!wcscmp(value, L"true")) _bool = TRUE;
                    else if (!wcscmp(value, L"false")) _bool = FALSE;
                    else return E_INVALIDARG;
                    src = &_bool;
                    break;
                case D2D1_PROPERTY_TYPE_CLSID:
                    CLSIDFromString(value, &_clsid);
                    src = &_clsid;
                    break;
                case D2D1_PROPERTY_TYPE_VECTOR2:
                case D2D1_PROPERTY_TYPE_VECTOR3:
                case D2D1_PROPERTY_TYPE_VECTOR4:
                case D2D1_PROPERTY_TYPE_MATRIX_3X2:
                case D2D1_PROPERTY_TYPE_MATRIX_4X3:
                case D2D1_PROPERTY_TYPE_MATRIX_4X4:
                case D2D1_PROPERTY_TYPE_MATRIX_5X4:
                    if (FAILED(hr = d2d_effect_parse_float_array(p->type, value, _vec)))
                    {
                        WARN("Failed to parse float array %s for type %u.\n",
                                wine_dbgstr_w(value), p->type);
                        return hr;
                    }
                    src = _vec;
                    break;
                case D2D1_PROPERTY_TYPE_IUNKNOWN:
                case D2D1_PROPERTY_TYPE_COLOR_CONTEXT:
                    break;
                default:
                    FIXME("Initial value for property type %u is not handled.\n", p->type);
            }

            if (src && p->size) memcpy(props->data.ptr + p->data.offset, src, p->size);
        }
        else if (p->size)
            memset(props->data.ptr + p->data.offset, 0, p->size);
    }

    return S_OK;
}

HRESULT d2d_effect_properties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    return d2d_effect_properties_internal_add(props, name, index, FALSE, type, value);
}

HRESULT d2d_effect_subproperties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    return d2d_effect_properties_internal_add(props, name, index, TRUE, type, value);
}

HRESULT d2d_effect_duplicate_properties(struct d2d_effect *effect,
        struct d2d_effect_properties *dst, const struct d2d_effect_properties *src)
{
    HRESULT hr;
    size_t i;

    *dst = *src;
    dst->effect = effect;

    if (!(dst->data.ptr = malloc(dst->data.size)))
        return E_OUTOFMEMORY;
    memcpy(dst->data.ptr, src->data.ptr, dst->data.size);

    if (!(dst->properties = calloc(dst->size, sizeof(*dst->properties))))
        return E_OUTOFMEMORY;

    for (i = 0; i < dst->count; ++i)
    {
        struct d2d_effect_property *d = &dst->properties[i];
        const struct d2d_effect_property *s = &src->properties[i];

        *d = *s;
        d->name = wcsdup(s->name);
        if (d->type == D2D1_PROPERTY_TYPE_STRING)
            d->data.ptr = wcsdup((WCHAR *)s->data.ptr);

        if (s->subproperties)
        {
            if (!(d->subproperties = calloc(1, sizeof(*d->subproperties))))
                return E_OUTOFMEMORY;
            if (FAILED(hr = d2d_effect_duplicate_properties(effect, d->subproperties, s->subproperties)))
                return hr;
        }
    }

    return S_OK;
}

struct d2d_effect_property * d2d_effect_properties_get_property_by_index(
        const struct d2d_effect_properties *properties, UINT32 index)
{
    unsigned int i;

    for (i = 0; i < properties->count; ++i)
    {
        if (properties->properties[i].index == index)
            return &properties->properties[i];
    }

    return NULL;
}

struct d2d_effect_property * d2d_effect_properties_get_property_by_name(
        const struct d2d_effect_properties *properties, const WCHAR *name)
{
    unsigned int i;

    for (i = 0; i < properties->count; ++i)
    {
        if (!wcscmp(properties->properties[i].name, name))
            return &properties->properties[i];
    }

    return NULL;
}

UINT32 d2d_effect_properties_get_value_size(const struct d2d_effect_properties *properties,
        UINT32 index)
{
    struct d2d_effect *effect = properties->effect;
    struct d2d_effect_property *prop;
    UINT32 size;

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return 0;

    if (prop->get_function)
    {
        if (FAILED(prop->get_function((IUnknown *)effect->impl, NULL, 0, &size))) return 0;
        return size;
    }

    return prop->size;
}

HRESULT d2d_effect_return_string(const WCHAR *str, WCHAR *buffer, UINT32 buffer_size)
{
    UINT32 size = str ? wcslen(str) : 0;
    if (size >= buffer_size) return D2DERR_INSUFFICIENT_BUFFER;
    if (str) memcpy(buffer, str, (size + 1) * sizeof(*buffer));
    else *buffer = 0;
    return S_OK;
}

HRESULT d2d_effect_property_get_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 size)
{
    struct d2d_effect *effect = properties->effect;
    PD2D1_PROPERTY_GET_FUNCTION get_function = effect ? prop->get_function : NULL;
    UINT32 actual_size;

    memset(value, 0, size);

    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && prop->type != type) return E_INVALIDARG;
    /* Do not check sizes for variable-length properties. */
    if (prop->type != D2D1_PROPERTY_TYPE_STRING
            && prop->type != D2D1_PROPERTY_TYPE_BLOB
            && prop->size != size)
    {
        return E_INVALIDARG;
    }

    if (get_function)
        return get_function((IUnknown *)effect->impl, value, size, &actual_size);

    switch (prop->type)
    {
        case D2D1_PROPERTY_TYPE_BLOB:
            memset(value, 0, size);
            break;
        case D2D1_PROPERTY_TYPE_STRING:
            return d2d_effect_return_string(prop->data.ptr, (WCHAR *)value, size / sizeof(WCHAR));
        default:
            memcpy(value, properties->data.ptr + prop->data.offset, size);
            break;
    }

    return S_OK;
}

HRESULT d2d_effect_property_get_uint32_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, UINT32 *value)
{
    return d2d_effect_property_get_value(properties, prop, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)value, sizeof(*value));
}

HRESULT d2d_effect_property_set_value(struct d2d_effect_properties *properties,
        struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 size)
{
    struct d2d_effect *effect = properties->effect;
    if (prop->readonly || !effect) return E_INVALIDARG;
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && prop->type != type) return E_INVALIDARG;
    if (prop->get_function && !prop->set_function) return E_INVALIDARG;
    if (prop->index < 0x80000000 && !prop->set_function) return E_INVALIDARG;

    if (prop->set_function)
        return prop->set_function((IUnknown *)effect->impl, value, size);

    if (prop->size != size) return E_INVALIDARG;

    switch (prop->type)
    {
        case D2D1_PROPERTY_TYPE_BOOL:
        case D2D1_PROPERTY_TYPE_UINT32:
        case D2D1_PROPERTY_TYPE_ENUM:
            memcpy(properties->data.ptr + prop->data.offset, value, size);
            break;
        default:
            FIXME("Unhandled type %u.\n", prop->type);
    }

    return S_OK;
}

void d2d_effect_properties_cleanup(struct d2d_effect_properties *props)
{
    struct d2d_effect_property *p;
    size_t i;

    for (i = 0; i < props->count; ++i)
    {
        p = &props->properties[i];
        free(p->name);
        if (p->type == D2D1_PROPERTY_TYPE_STRING)
            free(p->data.ptr);
        if (p->subproperties)
        {
            d2d_effect_properties_cleanup(p->subproperties);
            free(p->subproperties);
        }
    }
    free(props->properties);
    free(props->data.ptr);
}
