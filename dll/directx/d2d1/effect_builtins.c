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

static inline struct d2d_effect_impl *impl_from_ID2D1EffectImpl(ID2D1EffectImpl *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_impl, ID2D1EffectImpl_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_QueryInterface(ID2D1EffectImpl *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1EffectImpl)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1EffectImpl_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_impl_AddRef(ID2D1EffectImpl *iface)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl(iface);
    return InterlockedIncrement(&effect->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_effect_impl_Release(ID2D1EffectImpl *iface)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl(iface);
    LONG refcount = InterlockedDecrement(&effect->refcount);

    if (!refcount)
    {
        if (effect->destroy)
            effect->destroy(effect);
        free(effect);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_Initialize(ID2D1EffectImpl *iface,
        ID2D1EffectContext *context, ID2D1TransformGraph *graph)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_PrepareForRender(ID2D1EffectImpl *iface, D2D1_CHANGE_TYPE type)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_SetGraph(ID2D1EffectImpl *iface, ID2D1TransformGraph *graph)
{
    return S_OK;
}

static const ID2D1EffectImplVtbl d2d_effect_impl_vtbl =
{
    d2d_effect_impl_QueryInterface,
    d2d_effect_impl_AddRef,
    d2d_effect_impl_Release,
    d2d_effect_impl_Initialize,
    d2d_effect_impl_PrepareForRender,
    d2d_effect_impl_SetGraph,
};

static HRESULT d2d_effect_create_impl(IUnknown **effect_impl, const void *props,
        size_t props_size)
{
    struct d2d_effect_impl *object;

    if (!(object = calloc(1, sizeof(*object) + props_size)))
        return E_OUTOFMEMORY;

    object->ID2D1EffectImpl_iface.lpVtbl = &d2d_effect_impl_vtbl;
    object->refcount = 1;
    if (props_size) memcpy(object + 1, props, props_size);

    *effect_impl = (IUnknown *)&object->ID2D1EffectImpl_iface;

    return S_OK;
}

static UINT32 effect_property_type_size(D2D1_PROPERTY_TYPE prop_type)
{
    static const UINT32 sizes[D2D1_PROPERTY_TYPE_MATRIX_5X4 + 1] =
    {
        [D2D1_PROPERTY_TYPE_BOOL]       = sizeof(BOOL),
        [D2D1_PROPERTY_TYPE_UINT32]     = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_INT32]      = sizeof(INT32),
        [D2D1_PROPERTY_TYPE_FLOAT]      = sizeof(float),
        [D2D1_PROPERTY_TYPE_VECTOR2]    = sizeof(D2D_VECTOR_2F),
        [D2D1_PROPERTY_TYPE_VECTOR3]    = sizeof(D2D_VECTOR_3F),
        [D2D1_PROPERTY_TYPE_VECTOR4]    = sizeof(D2D_VECTOR_4F),
        [D2D1_PROPERTY_TYPE_ENUM]       = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_MATRIX_3X2] = sizeof(D2D_MATRIX_3X2_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X3] = sizeof(D2D_MATRIX_4X3_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X4] = sizeof(D2D_MATRIX_4X4_F),
        [D2D1_PROPERTY_TYPE_MATRIX_5X4] = sizeof(D2D_MATRIX_5X4_F),
    };

    if (prop_type >= ARRAY_SIZE(sizes))
        return 0;

    return sizes[prop_type];
}

static HRESULT effect_impl_prop_get_helper(const void *prop_data, D2D1_PROPERTY_TYPE prop_type,
        BYTE *data, UINT32 data_size, UINT32 *actual_size)
{
    UINT32 size = effect_property_type_size(prop_type);

    if (actual_size)
        *actual_size = size;

    if (data && data_size)
    {
        if (data_size < size)
            return E_NOT_SUFFICIENT_BUFFER;
        memcpy(data, prop_data, size);
    }

    return S_OK;
}

static HRESULT effect_impl_prop_set_helper(void *prop_data, D2D1_PROPERTY_TYPE prop_type,
        const BYTE *data, UINT32 data_size)
{
    if (data_size != effect_property_type_size(prop_type))
        return E_INVALIDARG;

    memcpy(prop_data, data, data_size);
    return S_OK;
}

#define EFFECT_PROPERTY_SET(name, prop, type) \
    static HRESULT __stdcall name##_##prop##_set(IUnknown *iface, const BYTE *data, UINT32 data_size) \
    { \
        struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
        struct name##_properties *props = (struct name##_properties *)(effect + 1); \
        return effect_impl_prop_set_helper(&props->prop, D2D1_PROPERTY_TYPE_##type, data, data_size); \
    } \

#define EFFECT_PROPERTY_GET(name, prop, type) \
    static HRESULT __stdcall name##_##prop##_get(const IUnknown *iface, BYTE *data, \
            UINT32 data_size, UINT32 *actual_size) \
    { \
        struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
        struct name##_properties *props = (struct name##_properties *)(effect + 1); \
        return effect_impl_prop_get_helper(&props->prop, D2D1_PROPERTY_TYPE_##type, data, data_size, actual_size); \
    } \

#define EFFECT_PROPERTY_RW(name, prop, type) \
    EFFECT_PROPERTY_SET(name, prop, type) \
    EFFECT_PROPERTY_GET(name, prop, type)

#define BINDING_RW(name, prop) name##_##prop##_set, name##_##prop##_get

static const WCHAR _2d_affine_transform_description[] =
L"<?xml version='1.0'?>                                                      \
  <Effect>                                                                   \
    <Property name='DisplayName' type='string' value='2D Affine Transform'/> \
    <Property name='Author'      type='string' value='The Wine Project'/>    \
    <Property name='Category'    type='string' value='Stub'/>                \
    <Property name='Description' type='string' value='2D Affine Transform'/> \
    <Inputs>                                                                 \
      <Input name='Source'/>                                                 \
    </Inputs>                                                                \
    <Property name='InterpolationMode' type='enum' />                        \
    <Property name='BorderMode' type='enum' />                               \
    <Property name='TransformMatrix' type='matrix3x2' />                     \
    <Property name='Sharpness' type='float' />                               \
  </Effect>";

struct _2d_affine_transform_properties
{
    D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE interpolation_mode;
    D2D1_BORDER_MODE border_mode;
    D2D1_MATRIX_3X2_F transform_matrix;
    float sharpness;
};

EFFECT_PROPERTY_RW(_2d_affine_transform, interpolation_mode, ENUM)
EFFECT_PROPERTY_RW(_2d_affine_transform, border_mode, ENUM)
EFFECT_PROPERTY_RW(_2d_affine_transform, transform_matrix, MATRIX_3X2)
EFFECT_PROPERTY_RW(_2d_affine_transform, sharpness, FLOAT)

static const D2D1_PROPERTY_BINDING _2d_affine_transform_bindings[] =
{
    { L"InterpolationMode", BINDING_RW(_2d_affine_transform, interpolation_mode) },
    { L"BorderMode", BINDING_RW(_2d_affine_transform, border_mode) },
    { L"TransformMatrix", BINDING_RW(_2d_affine_transform, transform_matrix) },
    { L"Sharpness", BINDING_RW(_2d_affine_transform, sharpness) },
};

static HRESULT __stdcall _2d_affine_transform_factory(IUnknown **effect)
{
    static const struct _2d_affine_transform_properties properties =
    {
        .interpolation_mode = D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR,
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .transform_matrix = { ._11 = 1.0f, ._22 = 1.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR _3d_perspective_transform_description[] =
L"<?xml version='1.0'?>                                                           \
  <Effect>                                                                        \
    <Property name='DisplayName' type='string' value='3D Perspective Transform'/> \
    <Property name='Author'      type='string' value='The Wine Project'/>         \
    <Property name='Category'    type='string' value='Stub'/>                     \
    <Property name='Description' type='string' value='3D Perspective Transform'/> \
    <Inputs>                                                                      \
      <Input name='Source'/>                                                      \
    </Inputs>                                                                     \
    <Property name='InterpolationMode' type='enum' />                             \
    <Property name='BorderMode' type='enum' />                                    \
    <Property name='Depth' type='float' />                                        \
    <Property name='PerspectiveOrigin' type='vector2' />                          \
    <Property name='LocalOffset' type='vector3' />                                \
    <Property name='GlobalOffset' type='vector3' />                               \
    <Property name='RotationOrigin' type='vector3' />                             \
    <Property name='Rotation' type='vector3' />                                   \
  </Effect>";

struct _3d_perspective_transform_properties
{
    D2D1_3DPERSPECTIVETRANSFORM_INTERPOLATION_MODE interpolation_mode;
    D2D1_BORDER_MODE border_mode;
    float depth;
    D2D1_VECTOR_2F perspective_origin;
    D2D1_VECTOR_3F local_offset;
    D2D1_VECTOR_3F global_offset;
    D2D1_VECTOR_3F rotation_origin;
    D2D1_VECTOR_3F rotation;
};

EFFECT_PROPERTY_RW(_3d_perspective_transform, interpolation_mode, ENUM)
EFFECT_PROPERTY_RW(_3d_perspective_transform, border_mode, ENUM)
EFFECT_PROPERTY_RW(_3d_perspective_transform, depth, FLOAT)
EFFECT_PROPERTY_RW(_3d_perspective_transform, perspective_origin, VECTOR2)
EFFECT_PROPERTY_RW(_3d_perspective_transform, local_offset, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, global_offset, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, rotation_origin, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, rotation, VECTOR3)

static const D2D1_PROPERTY_BINDING _3d_perspective_transform_bindings[] =
{
    { L"InterpolationMode", BINDING_RW(_3d_perspective_transform, interpolation_mode) },
    { L"BorderMode", BINDING_RW(_3d_perspective_transform, border_mode) },
    { L"Depth", BINDING_RW(_3d_perspective_transform, depth) },
    { L"PerspectiveOrigin", BINDING_RW(_3d_perspective_transform, perspective_origin) },
    { L"LocalOffset", BINDING_RW(_3d_perspective_transform, local_offset) },
    { L"GlobalOffset", BINDING_RW(_3d_perspective_transform, global_offset) },
    { L"RotationOrigin", BINDING_RW(_3d_perspective_transform, rotation_origin) },
    { L"Rotation", BINDING_RW(_3d_perspective_transform, rotation) },
};

static HRESULT __stdcall _3d_perspective_transform_factory(IUnknown **effect)
{
    static const struct _3d_perspective_transform_properties properties =
    {
        .interpolation_mode = D2D1_3DPERSPECTIVETRANSFORM_INTERPOLATION_MODE_LINEAR,
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .depth = 1000.0f,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR composite_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Composite'/>        \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Composite'/>        \
    <Inputs minimum='1' maximum='0xffffffff' >                            \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Mode' type='enum' />                                  \
  </Effect>";

struct composite_properties
{
    D2D1_COMPOSITE_MODE mode;
};

EFFECT_PROPERTY_RW(composite, mode, ENUM)

static const D2D1_PROPERTY_BINDING composite_bindings[] =
{
    { L"Mode", BINDING_RW(composite, mode) },
};

static HRESULT __stdcall composite_factory(IUnknown **effect)
{
    static const struct composite_properties properties =
    {
        .mode = D2D1_COMPOSITE_MODE_SOURCE_OVER,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR crop_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Crop'/>             \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Crop'/>             \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Rect' type='vector4' />                               \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct crop_properties
{
    D2D1_VECTOR_4F rect;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(crop, rect, VECTOR4)
EFFECT_PROPERTY_RW(crop, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING crop_bindings[] =
{
    { L"Rect", BINDING_RW(crop, rect) },
    { L"BorderMode", BINDING_RW(crop, border_mode) },
};

static HRESULT __stdcall crop_factory(IUnknown **effect)
{
    static const struct crop_properties properties =
    {
        .rect = { -INFINITY, -INFINITY, INFINITY, INFINITY },
        .border_mode = D2D1_BORDER_MODE_SOFT,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR shadow_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Shadow'/>           \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Shadow'/>           \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='BlurStandardDeviation' type='float' />                \
    <Property name='Color' type='vector4' />                              \
    <Property name='Optimization' type='enum' />                          \
  </Effect>";

struct shadow_properties
{
    float blur_standard_deviation;
    D2D_VECTOR_4F color;
    D2D1_SHADOW_OPTIMIZATION optimization;
};

EFFECT_PROPERTY_RW(shadow, blur_standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(shadow, color, VECTOR4)
EFFECT_PROPERTY_RW(shadow, optimization, ENUM)

static const D2D1_PROPERTY_BINDING shadow_bindings[] =
{
    { L"BlurStandardDeviation", BINDING_RW(shadow, blur_standard_deviation) },
    { L"Color", BINDING_RW(shadow, color) },
    { L"Optimization", BINDING_RW(shadow, optimization) },
};

static HRESULT __stdcall shadow_factory(IUnknown **effect)
{
    static const struct shadow_properties properties =
    {
        .blur_standard_deviation = 3.0f,
        .color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .optimization = D2D1_SHADOW_OPTIMIZATION_BALANCED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR grayscale_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Grayscale'/>        \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Grayscale'/>        \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
  </Effect>";

static HRESULT __stdcall grayscale_factory(IUnknown **effect)
{
    return d2d_effect_create_impl(effect, NULL, 0);
}

static const WCHAR color_matrix_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Color Matrix'/>     \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Color Matrix'/>     \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='ColorMatrix' type='matrix5x4' />                      \
    <Property name='AlphaMode' type='enum' />                             \
    <Property name='ClampOutput' type='bool' />                           \
  </Effect>";

struct color_matrix_properties
{
    D2D1_MATRIX_5X4_F color_matrix;
    D2D1_COLORMATRIX_ALPHA_MODE alpha_mode;
    BOOL clamp_output;
};

EFFECT_PROPERTY_RW(color_matrix, color_matrix, MATRIX_5X4)
EFFECT_PROPERTY_RW(color_matrix, alpha_mode, ENUM)
EFFECT_PROPERTY_RW(color_matrix, clamp_output, BOOL)

static const D2D1_PROPERTY_BINDING color_matrix_bindings[] =
{
    { L"ColorMatrix", BINDING_RW(color_matrix, color_matrix) },
    { L"AlphaMode", BINDING_RW(color_matrix, alpha_mode) },
    { L"ClampOutput", BINDING_RW(color_matrix, clamp_output) },
};

static HRESULT __stdcall color_matrix_factory(IUnknown **effect)
{
    static const struct color_matrix_properties properties =
    {
        .color_matrix = { ._11 = 1.0f, ._22 = 1.0f, ._33 = 1.0f, ._44 = 1.0f },
        .alpha_mode = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR flood_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Flood'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Flood'/>            \
    <Inputs minimum='0' maximum='0' >                                     \
    </Inputs>                                                             \
    <Property name='Color' type='vector4' />                              \
  </Effect>";

struct flood_properties
{
    D2D_VECTOR_4F color;
};

EFFECT_PROPERTY_RW(flood, color, VECTOR4)

static const D2D1_PROPERTY_BINDING flood_bindings[] =
{
    { L"Color", BINDING_RW(flood, color) },
};

static HRESULT __stdcall flood_factory(IUnknown **effect)
{
    static const struct flood_properties properties =
    {
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR gaussian_blur_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Gaussian Blur'/>    \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Gaussian Blur'/>    \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='StandardDeviation' type='float' />                    \
    <Property name='Optimization' type='enum' />                          \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct gaussian_blur_properties
{
    float standard_deviation;
    D2D1_GAUSSIANBLUR_OPTIMIZATION optimization;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(gaussian_blur, standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(gaussian_blur, optimization, ENUM)
EFFECT_PROPERTY_RW(gaussian_blur, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING gaussian_blur_bindings[] =
{
    { L"StandardDeviation", BINDING_RW(gaussian_blur, standard_deviation) },
    { L"Optimization", BINDING_RW(gaussian_blur, optimization) },
    { L"BorderMode", BINDING_RW(gaussian_blur, border_mode) },
};

static HRESULT __stdcall gaussian_blur_factory(IUnknown **effect)
{
    static const struct gaussian_blur_properties properties =
    {
        .standard_deviation = 3.0f,
        .optimization = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED,
        .border_mode = D2D1_BORDER_MODE_SOFT,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR point_specular_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Point Specular'/>   \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Point Specular'/>   \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='LightPosition' type='vector3' />                      \
    <Property name='SpecularExponent' type='float' />                     \
    <Property name='SpecularConstant' type='float' />                     \
    <Property name='SurfaceScale' type='float' />                         \
    <Property name='Color' type='vector3' />                              \
    <Property name='KernelUnitLength' type='vector2' />                   \
    <Property name='ScaleMode' type='enum' />                             \
  </Effect>";

struct point_specular_properties
{
    D2D_VECTOR_3F light_position;
    float specular_exponent;
    float specular_constant;
    float surface_scale;
    D2D_VECTOR_3F color;
    D2D_VECTOR_2F kernel_unit_length;
    D2D1_POINTSPECULAR_SCALE_MODE scale_mode;
};

EFFECT_PROPERTY_RW(point_specular, light_position, VECTOR3)
EFFECT_PROPERTY_RW(point_specular, specular_exponent, FLOAT)
EFFECT_PROPERTY_RW(point_specular, specular_constant, FLOAT)
EFFECT_PROPERTY_RW(point_specular, surface_scale, FLOAT)
EFFECT_PROPERTY_RW(point_specular, color, VECTOR3)
EFFECT_PROPERTY_RW(point_specular, kernel_unit_length, VECTOR2)
EFFECT_PROPERTY_RW(point_specular, scale_mode, ENUM)

static const D2D1_PROPERTY_BINDING point_specular_bindings[] =
{
    { L"LightPosition", BINDING_RW(point_specular, light_position) },
    { L"SpecularExponent", BINDING_RW(point_specular, specular_exponent) },
    { L"SpecularConstant", BINDING_RW(point_specular, specular_constant) },
    { L"SurfaceScale", BINDING_RW(point_specular, surface_scale) },
    { L"Color", BINDING_RW(point_specular, color) },
    { L"KernelUnitLength", BINDING_RW(point_specular, kernel_unit_length) },
    { L"ScaleMode", BINDING_RW(point_specular, scale_mode) },
};

static HRESULT __stdcall point_specular_factory(IUnknown **effect)
{
    static const struct point_specular_properties properties =
    {
        .specular_exponent = 1.0f,
        .specular_constant = 1.0f,
        .surface_scale = 1.0f,
        .color = { 1.0f, 1.0f, 1.0f },
        .kernel_unit_length = { 1.0f, 1.0f },
        .scale_mode = D2D1_POINTSPECULAR_SCALE_MODE_LINEAR,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR arithmetic_composite_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Arithmetic Composite'/> \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Arithmetic Composite'/> \
    <Inputs minimum='2' maximum='2' >                                     \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Coefficients' type='vector4' />                       \
    <Property name='ClampOutput' type='bool' />                           \
  </Effect>";

struct arithmetic_composite_properties
{
    D2D_VECTOR_4F coefficients;
    BOOL clamp_output;
};

EFFECT_PROPERTY_RW(arithmetic_composite, coefficients, VECTOR4)
EFFECT_PROPERTY_RW(arithmetic_composite, clamp_output, BOOL)

static const D2D1_PROPERTY_BINDING arithmetic_composite_bindings[] =
{
    { L"Coefficients", BINDING_RW(arithmetic_composite, coefficients) },
    { L"ClampOutput", BINDING_RW(arithmetic_composite, clamp_output) },
};

static HRESULT __stdcall arithmetic_composite_factory(IUnknown **effect)
{
    static const struct arithmetic_composite_properties properties =
    {
        .coefficients = { 1.0f, 0.0f, 0.0f, 0.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR blend_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Blend'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Blend'/>            \
    <Inputs minimum='2' maximum='2' >                                     \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Mode' type='enum' />                                  \
  </Effect>";

struct blend_properties
{
    D2D1_BLEND_MODE mode;
};

EFFECT_PROPERTY_RW(blend, mode, ENUM)

static const D2D1_PROPERTY_BINDING blend_bindings[] =
{
    { L"Mode", BINDING_RW(blend, mode) },
};

static HRESULT __stdcall blend_factory(IUnknown **effect)
{
    static const struct blend_properties properties = {};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR brightness_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Brightness'/>       \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Brightness'/>       \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='WhitePoint' type='vector2' />                         \
    <Property name='BlackPoint' type='vector2' />                         \
  </Effect>";

struct brightness_properties
{
    D2D_VECTOR_2F white_point;
    D2D_VECTOR_2F black_point;
};

EFFECT_PROPERTY_RW(brightness, white_point, VECTOR2)
EFFECT_PROPERTY_RW(brightness, black_point, VECTOR2)

static const D2D1_PROPERTY_BINDING brightness_bindings[] =
{
    { L"WhitePoint", BINDING_RW(brightness, white_point) },
    { L"BlackPoint", BINDING_RW(brightness, black_point) },
};

static HRESULT __stdcall brightness_factory(IUnknown **effect)
{
    static const struct brightness_properties properties =
    {
        .white_point = { 1.0f, 1.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR directional_blur_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Directional Blur'/> \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Directional Blur'/> \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='StandardDeviation' type='float' />                    \
    <Property name='Angle' type='float' />                                \
    <Property name='Optimization' type='enum' />                          \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct directional_blur_properties
{
    float standard_deviation;
    float angle;
    D2D1_DIRECTIONALBLUR_OPTIMIZATION optimization;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(directional_blur, standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(directional_blur, angle, FLOAT)
EFFECT_PROPERTY_RW(directional_blur, optimization, ENUM)
EFFECT_PROPERTY_RW(directional_blur, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING directional_blur_bindings[] =
{
    { L"StandardDeviation", BINDING_RW(directional_blur, standard_deviation) },
    { L"Angle", BINDING_RW(directional_blur, angle) },
    { L"Optimization", BINDING_RW(directional_blur, optimization) },
    { L"BorderMode", BINDING_RW(directional_blur, border_mode) },
};

static HRESULT __stdcall directional_blur_factory(IUnknown **effect)
{
    static const struct directional_blur_properties properties =
    {
        .standard_deviation = 3.0f,
        .optimization = D2D1_DIRECTIONALBLUR_OPTIMIZATION_BALANCED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR hue_rotation_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Hue Rotation'/>     \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Hue Rotation'/>     \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Angle' type='float' />                                \
  </Effect>";

struct hue_rotation_properties
{
    float angle;
};

EFFECT_PROPERTY_RW(hue_rotation, angle, FLOAT)

static const D2D1_PROPERTY_BINDING hue_rotation_bindings[] =
{
    { L"Angle", BINDING_RW(hue_rotation, angle) },
};

static HRESULT __stdcall hue_rotation_factory(IUnknown **effect)
{
    static const struct hue_rotation_properties properties = {};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR saturation_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Saturation'/>       \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Saturation'/>       \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Saturation' type='float' />                           \
  </Effect>";

struct saturation_properties
{
    float saturation;
};

EFFECT_PROPERTY_RW(saturation, saturation, FLOAT)

static const D2D1_PROPERTY_BINDING saturation_bindings[] =
{
    { L"Saturation", BINDING_RW(saturation, saturation) },
};

static HRESULT __stdcall saturation_factory(IUnknown **effect)
{
    static const struct saturation_properties properties =
    {
        .saturation = 0.5f,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR scale_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Scale'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Scale'/>            \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Scale' type='vector2' />                              \
    <Property name='CenterPoint' type='vector2' />                        \
    <Property name='InterpolationMode' type='enum' />                     \
    <Property name='BorderMode' type='enum' />                            \
    <Property name='Sharpness' type='float' />                            \
  </Effect>";

struct scale_properties
{
    D2D_VECTOR_2F scale;
    D2D_VECTOR_2F center_point;
    D2D1_BORDER_MODE border_mode;
    float sharpness;
    D2D1_SCALE_INTERPOLATION_MODE interpolation_mode;
};

EFFECT_PROPERTY_RW(scale, scale, VECTOR2)
EFFECT_PROPERTY_RW(scale, center_point, VECTOR2)
EFFECT_PROPERTY_RW(scale, border_mode, ENUM)
EFFECT_PROPERTY_RW(scale, sharpness, FLOAT)
EFFECT_PROPERTY_RW(scale, interpolation_mode, ENUM)

static const D2D1_PROPERTY_BINDING scale_bindings[] =
{
    { L"Scale", BINDING_RW(scale, scale) },
    { L"CenterPoint", BINDING_RW(scale, center_point) },
    { L"BorderMode", BINDING_RW(scale, border_mode) },
    { L"Sharpness", BINDING_RW(scale, sharpness) },
    { L"InterpolationMode", BINDING_RW(scale, interpolation_mode) },
};

static HRESULT __stdcall scale_factory(IUnknown **effect)
{
    static const struct scale_properties properties =
    {
        .scale = { 1.0f, 1.0f },
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .interpolation_mode = D2D1_SCALE_INTERPOLATION_MODE_LINEAR,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR bitmap_source_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Bitmap Source'/>    \
    <Property name='Author'      type='string' value='LiberNT'/>          \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Bitmap Source'/>    \
    <Inputs minimum='0' maximum='0' >                                     \
    </Inputs>                                                             \
    <Property name='WicBitmapSource' type='iunknown' />                   \
    <Property name='Scale' type='vector2' />                              \
    <Property name='InterpolationMode' type='enum' />                     \
    <Property name='EnableDPICorrection' type='bool' />                   \
    <Property name='AlphaMode' type='enum' />                             \
    <Property name='Orientation' type='enum' />                           \
  </Effect>";

struct bitmap_source_properties
{
    IUnknown *wic_bitmap_source;
    D2D_VECTOR_2F scale;
    D2D1_BITMAPSOURCE_INTERPOLATION_MODE interpolation_mode;
    BOOL enable_dpi_correction;
    D2D1_BITMAPSOURCE_ALPHA_MODE alpha_mode;
    D2D1_BITMAPSOURCE_ORIENTATION orientation;
};

static HRESULT __stdcall bitmap_source_wic_bitmap_source_set(IUnknown *iface, const BYTE *data, UINT32 data_size)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct bitmap_source_properties *props = (struct bitmap_source_properties *)(effect + 1);
    IUnknown *source;

    if (data_size != sizeof(source))
        return E_INVALIDARG;
    memcpy(&source, data, sizeof(source));
    if (source)
        IUnknown_AddRef(source);
    if (props->wic_bitmap_source)
        IUnknown_Release(props->wic_bitmap_source);
    props->wic_bitmap_source = source;
    return S_OK;
}

static HRESULT __stdcall bitmap_source_wic_bitmap_source_get(const IUnknown *iface, BYTE *data,
        UINT32 data_size, UINT32 *actual_size)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct bitmap_source_properties *props = (struct bitmap_source_properties *)(effect + 1);

    if (actual_size)
        *actual_size = sizeof(props->wic_bitmap_source);
    if (!data || !data_size)
        return S_OK;
    if (data_size < sizeof(props->wic_bitmap_source))
        return E_NOT_SUFFICIENT_BUFFER;
    /* TODO: INFO_NEEDED whether reading this property adds a reference to the source */
    if (props->wic_bitmap_source)
        IUnknown_AddRef(props->wic_bitmap_source);
    memcpy(data, &props->wic_bitmap_source, sizeof(props->wic_bitmap_source));
    return S_OK;
}

EFFECT_PROPERTY_RW(bitmap_source, scale, VECTOR2)
EFFECT_PROPERTY_RW(bitmap_source, interpolation_mode, ENUM)
EFFECT_PROPERTY_RW(bitmap_source, enable_dpi_correction, BOOL)
EFFECT_PROPERTY_RW(bitmap_source, alpha_mode, ENUM)
EFFECT_PROPERTY_RW(bitmap_source, orientation, ENUM)

static const D2D1_PROPERTY_BINDING bitmap_source_bindings[] =
{
    { L"WicBitmapSource", BINDING_RW(bitmap_source, wic_bitmap_source) },
    { L"Scale", BINDING_RW(bitmap_source, scale) },
    { L"InterpolationMode", BINDING_RW(bitmap_source, interpolation_mode) },
    { L"EnableDPICorrection", BINDING_RW(bitmap_source, enable_dpi_correction) },
    { L"AlphaMode", BINDING_RW(bitmap_source, alpha_mode) },
    { L"Orientation", BINDING_RW(bitmap_source, orientation) },
};

static void bitmap_source_destroy(struct d2d_effect_impl *effect)
{
    struct bitmap_source_properties *props = (struct bitmap_source_properties *)(effect + 1);

    if (props->wic_bitmap_source)
        IUnknown_Release(props->wic_bitmap_source);
}

static HRESULT __stdcall bitmap_source_factory(IUnknown **effect)
{
    static const struct bitmap_source_properties properties =
    {
        .scale = { 1.0f, 1.0f },
        .interpolation_mode = D2D1_BITMAPSOURCE_INTERPOLATION_MODE_LINEAR,
        .alpha_mode = D2D1_BITMAPSOURCE_ALPHA_MODE_PREMULTIPLIED,
        .orientation = D2D1_BITMAPSOURCE_ORIENTATION_DEFAULT,
    };
    HRESULT hr = d2d_effect_create_impl(effect, &properties, sizeof(properties));

    if (SUCCEEDED(hr))
        impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->destroy = bitmap_source_destroy;
    return hr;
}

void d2d_effects_init_builtins(struct d2d_factory *factory)
{
    static const struct builtin_description
    {
        const CLSID *clsid;
        const WCHAR *description;
        PD2D1_EFFECT_FACTORY factory;
        const D2D1_PROPERTY_BINDING *bindings;
        UINT32 binding_count;
    }
    builtin_effects[] =
    {
#define X(name) name##_description, name##_factory
#define X2(name) name##_description, name##_factory, name##_bindings, ARRAY_SIZE(name##_bindings)
        { &CLSID_D2D12DAffineTransform, X2(_2d_affine_transform) },
        { &CLSID_D2D13DPerspectiveTransform, X2(_3d_perspective_transform) },
        { &CLSID_D2D1Composite, X2(composite) },
        { &CLSID_D2D1Crop, X2(crop) },
        { &CLSID_D2D1Shadow, X2(shadow) },
        { &CLSID_D2D1Grayscale, X(grayscale) },
        { &CLSID_D2D1ColorMatrix, X2(color_matrix) },
        { &CLSID_D2D1Flood, X2(flood) },
        { &CLSID_D2D1GaussianBlur, X2(gaussian_blur) },
        { &CLSID_D2D1PointSpecular, X2(point_specular) },
        { &CLSID_D2D1ArithmeticComposite, X2(arithmetic_composite) },
        { &CLSID_D2D1Blend, X2(blend) },
        { &CLSID_D2D1Brightness, X2(brightness) },
        { &CLSID_D2D1DirectionalBlur, X2(directional_blur) },
        { &CLSID_D2D1HueRotation, X2(hue_rotation) },
        { &CLSID_D2D1Saturation, X2(saturation) },
        { &CLSID_D2D1Scale, X2(scale) },
        { &CLSID_D2D1BitmapSource, X2(bitmap_source) },
#undef X2
#undef X
    };
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < ARRAY_SIZE(builtin_effects); ++i)
    {
        const struct builtin_description *desc = &builtin_effects[i];

        if (FAILED(hr = d2d_factory_register_builtin_effect(factory, desc->clsid, desc->description,
                desc->bindings, desc->binding_count, desc->factory)))
        {
            WARN("Failed to register the effect %s, hr %#lx.\n", wine_dbgstr_guid(desc->clsid), hr);
        }
    }
}

/* Same syntax is used for value and default values. */
