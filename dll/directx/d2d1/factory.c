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

#define D2D1_INIT_GUID
#include "factory_private.h"

#include "xmllite.h"
#include "wine/list.h"

WINE_DECLARE_DEBUG_CHANNEL(winediag);
WINE_DEFAULT_DEBUG_CHANNEL(d2d);

struct d2d_settings d2d_settings =
{
    ~0u,    /* No ID2D1Factory version limit by default. */
};

static inline struct d2d_factory *impl_from_ID2D1Multithread(ID2D1Multithread *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_factory, ID2D1Multithread_iface);
}

static HRESULT d2d_factory_reload_sysmetrics(struct d2d_factory *factory)
{
    HDC hdc;

    if (!(hdc = GetDC(NULL)))
    {
        factory->dpi_x = factory->dpi_y = 96.0f;
        return E_FAIL;
    }

    factory->dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    factory->dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);

    ReleaseDC(NULL, hdc);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_QueryInterface(ID2D1Factory7 *iface, REFIID iid, void **out)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if ((IsEqualGUID(iid, &IID_ID2D1Factory7) && d2d_settings.max_version_factory >= 7)
            || (IsEqualGUID(iid, &IID_ID2D1Factory6) && d2d_settings.max_version_factory >= 6)
            || (IsEqualGUID(iid, &IID_ID2D1Factory5) && d2d_settings.max_version_factory >= 5)
            || (IsEqualGUID(iid, &IID_ID2D1Factory4) && d2d_settings.max_version_factory >= 4)
            || (IsEqualGUID(iid, &IID_ID2D1Factory3) && d2d_settings.max_version_factory >= 3)
            || (IsEqualGUID(iid, &IID_ID2D1Factory2) && d2d_settings.max_version_factory >= 2)
            || (IsEqualGUID(iid, &IID_ID2D1Factory1) && d2d_settings.max_version_factory >= 1)
            || IsEqualGUID(iid, &IID_ID2D1Factory)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1Factory7_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ID2D1Multithread))
    {
        ID2D1Factory7_AddRef(iface);
        *out = &factory->ID2D1Multithread_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_factory_AddRef(ID2D1Factory7 *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    ULONG refcount = InterlockedIncrement(&factory->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_factory_Release(ID2D1Factory7 *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    ULONG refcount = InterlockedDecrement(&factory->refcount);
    struct d2d_effect_registration *reg, *reg2;

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        if (factory->shared_shader_set)
            d2d_shader_set_release(factory->shared_shader_set);
        if (factory->device)
            ID3D10Device1_Release(factory->device);
        LIST_FOR_EACH_ENTRY_SAFE(reg, reg2, &factory->effects, struct d2d_effect_registration, entry)
        {
            d2d_effect_registration_cleanup(reg);
        }
        DeleteCriticalSection(&factory->cs);
        free(factory);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ReloadSystemMetrics(ID2D1Factory7 *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);

    TRACE("iface %p.\n", iface);

    return d2d_factory_reload_sysmetrics(factory);
}

static void STDMETHODCALLTYPE d2d_factory_GetDesktopDpi(ID2D1Factory7 *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    *dpi_x = factory->dpi_x;
    *dpi_y = factory->dpi_y;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateRectangleGeometry(ID2D1Factory7 *iface,
        const D2D1_RECT_F *rect, ID2D1RectangleGeometry **geometry)
{
    struct d2d_geometry *object;
    HRESULT hr;

    TRACE("iface %p, rect %s, geometry %p.\n", iface, debug_d2d_rect_f(rect), geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_rectangle_geometry_init(object, (ID2D1Factory *)iface, rect)))
    {
        WARN("Failed to initialise rectangle geometry, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created rectangle geometry %p.\n", object);
    *geometry = (ID2D1RectangleGeometry *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateRoundedRectangleGeometry(ID2D1Factory7 *iface,
        const D2D1_ROUNDED_RECT *rounded_rect, ID2D1RoundedRectangleGeometry **geometry)
{
    struct d2d_geometry *object;
    HRESULT hr;

    TRACE("iface %p, rounded_rect %s, geometry %p.\n", iface, debug_d2d_rounded_rect(rounded_rect), geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_rounded_rectangle_geometry_init(object, (ID2D1Factory *)iface, rounded_rect)))
    {
        WARN("Failed to initialise rounded rectangle geometry, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created rounded rectangle geometry %p.\n", object);
    *geometry = (ID2D1RoundedRectangleGeometry *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateEllipseGeometry(ID2D1Factory7 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1EllipseGeometry **geometry)
{
    struct d2d_geometry *object;
    HRESULT hr;

    TRACE("iface %p, ellipse %s, geometry %p.\n", iface, debug_d2d_ellipse(ellipse), geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_ellipse_geometry_init(object, (ID2D1Factory *)iface, ellipse)))
    {
        WARN("Failed to initialise ellipse geometry, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created ellipse geometry %p.\n", object);
    *geometry = (ID2D1EllipseGeometry *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateGeometryGroup(ID2D1Factory7 *iface,
        D2D1_FILL_MODE fill_mode, ID2D1Geometry **geometries, UINT32 geometry_count, ID2D1GeometryGroup **group)
{
    struct d2d_geometry *object;
    HRESULT hr;

    TRACE("iface %p, fill_mode %#x, geometries %p, geometry_count %u, group %p.\n",
            iface, fill_mode, geometries, geometry_count, group);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_geometry_group_init(object, (ID2D1Factory *)iface, fill_mode, geometries, geometry_count)))
    {
        WARN("Failed to initialise geometry group, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created geometry group %p.\n", object);
    *group = (ID2D1GeometryGroup *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateTransformedGeometry(ID2D1Factory7 *iface,
        ID2D1Geometry *src_geometry, const D2D1_MATRIX_3X2_F *transform,
        ID2D1TransformedGeometry **transformed_geometry)
{
    struct d2d_geometry *object;

    TRACE("iface %p, src_geometry %p, transform %p, transformed_geometry %p.\n",
            iface, src_geometry, transform, transformed_geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    d2d_transformed_geometry_init(object, (ID2D1Factory *)iface, src_geometry, transform);

    TRACE("Created transformed geometry %p.\n", object);
    *transformed_geometry = (ID2D1TransformedGeometry *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreatePathGeometry(ID2D1Factory7 *iface,
        ID2D1PathGeometry **geometry)
{
    struct d2d_geometry *object;

    TRACE("iface %p, geometry %p.\n", iface, geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    d2d_path_geometry_init(object, (ID2D1Factory *)iface);

    TRACE("Created path geometry %p.\n", object);
    *geometry = (ID2D1PathGeometry *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateStrokeStyle(ID2D1Factory7 *iface,
        const D2D1_STROKE_STYLE_PROPERTIES *desc, const float *dashes, UINT32 dash_count,
        ID2D1StrokeStyle **stroke_style)
{
    struct d2d_stroke_style *object;
    D2D1_STROKE_STYLE_PROPERTIES1 desc1;
    HRESULT hr;

    TRACE("iface %p, desc %p, dashes %p, dash_count %u, stroke_style %p.\n",
            iface, desc, dashes, dash_count, stroke_style);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    desc1.startCap = desc->startCap;
    desc1.endCap = desc->endCap;
    desc1.dashCap = desc->dashCap;
    desc1.lineJoin = desc->lineJoin;
    desc1.miterLimit = desc->miterLimit;
    desc1.dashStyle = desc->dashStyle;
    desc1.dashOffset = desc->dashOffset;
    desc1.transformType = D2D1_STROKE_TRANSFORM_TYPE_NORMAL;

    if (FAILED(hr = d2d_stroke_style_init(object, (ID2D1Factory *)iface, &desc1, dashes, dash_count)))
    {
        WARN("Failed to initialise stroke style, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created stroke style %p.\n", object);
    *stroke_style = (ID2D1StrokeStyle *)&object->ID2D1StrokeStyle1_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateDrawingStateBlock(ID2D1Factory7 *iface,
        const D2D1_DRAWING_STATE_DESCRIPTION *desc, IDWriteRenderingParams *text_rendering_params,
        ID2D1DrawingStateBlock **state_block)
{
    D2D1_DRAWING_STATE_DESCRIPTION1 state_desc;
    struct d2d_state_block *object;

    TRACE("iface %p, desc %p, text_rendering_params %p, state_block %p.\n",
            iface, desc, text_rendering_params, state_block);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (desc)
    {
        memcpy(&state_desc, desc, sizeof(*desc));
        state_desc.primitiveBlend = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
        state_desc.unitMode = D2D1_UNIT_MODE_DIPS;
    }

    d2d_state_block_init(object, (ID2D1Factory *)iface, desc ? &state_desc : NULL, text_rendering_params);

    TRACE("Created state block %p.\n", object);
    *state_block = (ID2D1DrawingStateBlock *)&object->ID2D1DrawingStateBlock1_iface;

    return S_OK;
}

static HRESULT d2d_factory_get_device(struct d2d_factory *factory, ID3D10Device1 **device)
{
    HRESULT hr = S_OK;

    if (!factory->device && FAILED(hr = D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, D3D10_CREATE_DEVICE_BGRA_SUPPORT,
            D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &factory->device)))
        WARN("Failed to create device, hr %#lx.\n", hr);

    *device = factory->device;
    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateWicBitmapRenderTarget(ID2D1Factory7 *iface,
        IWICBitmap *target, const D2D1_RENDER_TARGET_PROPERTIES *desc, ID2D1RenderTarget **render_target)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    struct d2d_wic_render_target *object;
    ID3D10Device1 *device;
    HRESULT hr;

    TRACE("iface %p, target %p, desc %p, render_target %p.\n", iface, target, desc, render_target);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_factory_get_device(factory, &device)))
    {
        free(object);
        return hr;
    }

    if (FAILED(hr = d2d_wic_render_target_init(object, (ID2D1Factory1 *)iface, device, target, desc)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *render_target = object->dxgi_target;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateHwndRenderTarget(ID2D1Factory7 *iface,
        const D2D1_RENDER_TARGET_PROPERTIES *desc, const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwnd_rt_desc,
        ID2D1HwndRenderTarget **render_target)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    struct d2d_hwnd_render_target *object;
    ID3D10Device1 *device;
    HRESULT hr;

    TRACE("iface %p, desc %p, hwnd_rt_desc %p, render_target %p.\n", iface, desc, hwnd_rt_desc, render_target);

    if (FAILED(hr = d2d_factory_get_device(factory, &device)))
        return hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_hwnd_render_target_init(object, (ID2D1Factory1 *)iface, device, desc, hwnd_rt_desc)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *render_target = &object->ID2D1HwndRenderTarget_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateDxgiSurfaceRenderTarget(ID2D1Factory7 *iface,
        IDXGISurface *surface, const D2D1_RENDER_TARGET_PROPERTIES *desc, ID2D1RenderTarget **render_target)
{
    IDXGIDevice *dxgi_device;
    ID2D1Device *device;
    HRESULT hr;

    TRACE("iface %p, surface %p, desc %p, render_target %p.\n", iface, surface, desc, render_target);

    if (FAILED(hr = IDXGISurface_GetDevice(surface, &IID_IDXGIDevice, (void **)&dxgi_device)))
    {
        WARN("Failed to get DXGI device, hr %#lx.\n", hr);
        return hr;
    }

    hr = ID2D1Factory1_CreateDevice((ID2D1Factory1 *)iface, dxgi_device, &device);
    IDXGIDevice_Release(dxgi_device);
    if (FAILED(hr))
    {
        WARN("Failed to create D2D device, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_d3d_create_render_target(unsafe_impl_from_ID2D1Device((ID2D1Device1 *)device), surface,
            NULL, NULL, desc, (void **)render_target);
    ID2D1Device_Release(device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateDCRenderTarget(ID2D1Factory7 *iface,
        const D2D1_RENDER_TARGET_PROPERTIES *desc, ID2D1DCRenderTarget **render_target)
{
    struct d2d_factory *factory = impl_from_ID2D1Factory7(iface);
    struct d2d_dc_render_target *object;
    ID3D10Device1 *device;
    HRESULT hr;

    TRACE("iface %p, desc %p, render_target %p.\n", iface, desc, render_target);

    if (FAILED(hr = d2d_factory_get_device(factory, &device)))
        return hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_dc_render_target_init(object, (ID2D1Factory1 *)iface, device, desc)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *render_target = &object->ID2D1DCRenderTarget_iface;

    return S_OK;
}

struct d2d_shader_set *d2d_factory_get_shader_set(ID2D1Factory1 *iface, IDXGIDevice *dxgi_device)
{
    struct d2d_factory *factory = unsafe_impl_from_ID2D1Factory((ID2D1Factory *)iface);
    struct d2d_shader_set *set = NULL;
    IDXGIDevice *shared;

    EnterCriticalSection(&factory->cs);
    if (factory->device && SUCCEEDED(ID3D10Device1_QueryInterface(factory->device, &IID_IDXGIDevice, (void **)&shared)))
    {
        if (shared == dxgi_device)
        {
            if (!factory->shared_shader_set)
                d2d_shader_set_create(&factory->shared_shader_set);
            if ((set = factory->shared_shader_set))
                InterlockedIncrement(&set->refcount);
        }
        IDXGIDevice_Release(shared);
    }
    LeaveCriticalSection(&factory->cs);

    return set;
}

HRESULT d2d_factory_create_device(ID2D1Factory1 *factory, IDXGIDevice *dxgi_device,
        bool allow_get_dxgi_device, REFIID iid, void **device)
{
    struct d2d_device *object;
    HRESULT hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_device_init(object, factory, dxgi_device, allow_get_dxgi_device)))
    {
        ID2D1Device6_Release(&object->ID2D1Device6_iface);
        return hr;
    }

    TRACE("Create device %p.\n", object);

    hr = ID2D1Device6_QueryInterface(&object->ID2D1Device6_iface, iid, device);
    ID2D1Device6_Release(&object->ID2D1Device6_iface);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateStrokeStyle1(ID2D1Factory7 *iface,
        const D2D1_STROKE_STYLE_PROPERTIES1 *desc, const float *dashes, UINT32 dash_count,
        ID2D1StrokeStyle1 **stroke_style)
{
    struct d2d_stroke_style *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, dashes %p, dash_count %u, stroke_style %p.\n",
            iface, desc, dashes, dash_count, stroke_style);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_stroke_style_init(object, (ID2D1Factory *)iface,
            desc, dashes, dash_count)))
    {
        WARN("Failed to initialise stroke style, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created stroke style %p.\n", object);
    *stroke_style = &object->ID2D1StrokeStyle1_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreatePathGeometry1(ID2D1Factory7 *iface,
        ID2D1PathGeometry1 **geometry)
{
    struct d2d_geometry *object;

    TRACE("iface %p, geometry %p.\n", iface, geometry);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    d2d_path_geometry_init(object, (ID2D1Factory *)iface);

    TRACE("Created path geometry %p.\n", object);
    *geometry = (ID2D1PathGeometry1 *)&object->ID2D1Geometry_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateDrawingStateBlock1(ID2D1Factory7 *iface,
        const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *text_rendering_params,
        ID2D1DrawingStateBlock1 **state_block)
{
    struct d2d_state_block *object;

    TRACE("iface %p, desc %p, text_rendering_params %p, state_block %p.\n",
            iface, desc, text_rendering_params, state_block);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    d2d_state_block_init(object, (ID2D1Factory *)iface, desc, text_rendering_params);

    TRACE("Created state block %p.\n", object);
    *state_block = &object->ID2D1DrawingStateBlock1_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_CreateGdiMetafile(ID2D1Factory7 *iface,
        IStream *stream, ID2D1GdiMetafile **metafile)
{
    FIXME("iface %p, stream %p, metafile %p stub!\n", iface, stream, metafile);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory2_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device1 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device1, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory3_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device2 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device2, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory4_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device3 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device3, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory5_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device4 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device4, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory6_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device5 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device5, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d2d_factory_ID2D1Factory7_CreateDevice(ID2D1Factory7 *iface,
        IDXGIDevice *dxgi_device, ID2D1Device6 **device)
{
    TRACE("iface %p, dxgi_device %p, device %p.\n", iface, dxgi_device, device);

    return d2d_factory_create_device((ID2D1Factory1 *)iface, dxgi_device, true, &IID_ID2D1Device6, (void **)device);
}

static const struct ID2D1Factory7Vtbl d2d_factory_vtbl =
{
    d2d_factory_QueryInterface,
    d2d_factory_AddRef,
    d2d_factory_Release,
    d2d_factory_ReloadSystemMetrics,
    d2d_factory_GetDesktopDpi,
    d2d_factory_CreateRectangleGeometry,
    d2d_factory_CreateRoundedRectangleGeometry,
    d2d_factory_CreateEllipseGeometry,
    d2d_factory_CreateGeometryGroup,
    d2d_factory_CreateTransformedGeometry,
    d2d_factory_CreatePathGeometry,
    d2d_factory_CreateStrokeStyle,
    d2d_factory_CreateDrawingStateBlock,
    d2d_factory_CreateWicBitmapRenderTarget,
    d2d_factory_CreateHwndRenderTarget,
    d2d_factory_CreateDxgiSurfaceRenderTarget,
    d2d_factory_CreateDCRenderTarget,
    d2d_factory_CreateDevice,
    d2d_factory_CreateStrokeStyle1,
    d2d_factory_CreatePathGeometry1,
    d2d_factory_CreateDrawingStateBlock1,
    d2d_factory_CreateGdiMetafile,
    d2d_factory_RegisterEffectFromStream,
    d2d_factory_RegisterEffectFromString,
    d2d_factory_UnregisterEffect,
    d2d_factory_GetRegisteredEffects,
    d2d_factory_GetEffectProperties,
    d2d_factory_ID2D1Factory2_CreateDevice,
    d2d_factory_ID2D1Factory3_CreateDevice,
    d2d_factory_ID2D1Factory4_CreateDevice,
    d2d_factory_ID2D1Factory5_CreateDevice,
    d2d_factory_ID2D1Factory6_CreateDevice,
    d2d_factory_ID2D1Factory7_CreateDevice,
};

static HRESULT STDMETHODCALLTYPE d2d_factory_mt_QueryInterface(ID2D1Multithread *iface, REFIID iid, void **out)
{
    struct d2d_factory *factory = impl_from_ID2D1Multithread(iface);
    return d2d_factory_QueryInterface(&factory->ID2D1Factory7_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_factory_mt_AddRef(ID2D1Multithread *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Multithread(iface);
    return d2d_factory_AddRef(&factory->ID2D1Factory7_iface);
}

static ULONG STDMETHODCALLTYPE d2d_factory_mt_Release(ID2D1Multithread *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Multithread(iface);
    return d2d_factory_Release(&factory->ID2D1Factory7_iface);
}

static BOOL STDMETHODCALLTYPE d2d_factory_mt_GetMultithreadProtected(ID2D1Multithread *iface)
{
    return TRUE;
}

static void STDMETHODCALLTYPE d2d_factory_mt_Enter(ID2D1Multithread *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Multithread(iface);

    TRACE("%p.\n", iface);

    EnterCriticalSection(&factory->cs);
}

static void STDMETHODCALLTYPE d2d_factory_mt_Leave(ID2D1Multithread *iface)
{
    struct d2d_factory *factory = impl_from_ID2D1Multithread(iface);

    TRACE("%p.\n", iface);

    LeaveCriticalSection(&factory->cs);
}

static BOOL STDMETHODCALLTYPE d2d_factory_st_GetMultithreadProtected(ID2D1Multithread *iface)
{
    return FALSE;
}

static void STDMETHODCALLTYPE d2d_factory_st_Enter(ID2D1Multithread *iface)
{
}

static void STDMETHODCALLTYPE d2d_factory_st_Leave(ID2D1Multithread *iface)
{
}

static const struct ID2D1MultithreadVtbl d2d_factory_multithread_vtbl =
{
    d2d_factory_mt_QueryInterface,
    d2d_factory_mt_AddRef,
    d2d_factory_mt_Release,
    d2d_factory_mt_GetMultithreadProtected,
    d2d_factory_mt_Enter,
    d2d_factory_mt_Leave,
};

static const struct ID2D1MultithreadVtbl d2d_factory_multithread_noop_vtbl =
{
    d2d_factory_mt_QueryInterface,
    d2d_factory_mt_AddRef,
    d2d_factory_mt_Release,
    d2d_factory_st_GetMultithreadProtected,
    d2d_factory_st_Enter,
    d2d_factory_st_Leave,
};

static void d2d_factory_init(struct d2d_factory *factory, D2D1_FACTORY_TYPE factory_type,
        const D2D1_FACTORY_OPTIONS *factory_options)
{
    if (factory_options && factory_options->debugLevel != D2D1_DEBUG_LEVEL_NONE)
        WARN("Ignoring debug level %#x.\n", factory_options->debugLevel);

    factory->ID2D1Factory7_iface.lpVtbl = &d2d_factory_vtbl;
    factory->ID2D1Multithread_iface.lpVtbl = factory_type == D2D1_FACTORY_TYPE_SINGLE_THREADED ?
            &d2d_factory_multithread_noop_vtbl : &d2d_factory_multithread_vtbl;
    factory->factory_type = factory_type;
    factory->refcount = 1;
    d2d_factory_reload_sysmetrics(factory);
    list_init(&factory->effects);
    InitializeCriticalSection(&factory->cs);
    InitOnceInitialize(&factory->init_builtins);
}

HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE factory_type, REFIID iid,
        const D2D1_FACTORY_OPTIONS *factory_options, void **factory)
{
    struct d2d_factory *object;
    HRESULT hr;

    TRACE("factory_type %#x, iid %s, factory_options %p, factory %p.\n",
            factory_type, debugstr_guid(iid), factory_options, factory);

    if (factory_type != D2D1_FACTORY_TYPE_SINGLE_THREADED &&
            factory_type != D2D1_FACTORY_TYPE_MULTI_THREADED)
    {
        return E_INVALIDARG;
    }

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    d2d_factory_init(object, factory_type, factory_options);

    TRACE("Created factory %p.\n", object);

    hr = ID2D1Factory7_QueryInterface(&object->ID2D1Factory7_iface, iid, factory);
    ID2D1Factory7_Release(&object->ID2D1Factory7_iface);

    return hr;
}

void WINAPI D2D1MakeRotateMatrix(float angle, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix)
{
    float theta, sin_theta, cos_theta;

    TRACE("angle %.8e, center %s, matrix %p.\n", angle, debug_d2d_point_2f(&center), matrix);

    theta = angle * (M_PI / 180.0f);
    sin_theta = sinf(theta);
    cos_theta = cosf(theta);

    /* translate(center) * rotate(theta) * translate(-center) */
    matrix->_11 = cos_theta;
    matrix->_12 = sin_theta;
    matrix->_21 = -sin_theta;
    matrix->_22 = cos_theta;
    matrix->_31 = center.x - center.x * cos_theta + center.y * sin_theta;
    matrix->_32 = center.y - center.x * sin_theta - center.y * cos_theta;
}

void WINAPI D2D1MakeSkewMatrix(float angle_x, float angle_y, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix)
{
    float tan_x, tan_y;

    TRACE("angle_x %.8e, angle_y %.8e, center %s, matrix %p.\n", angle_x, angle_y, debug_d2d_point_2f(&center), matrix);

    tan_x = tan(angle_x * (M_PI / 180.0f));
    tan_y = tan(angle_y * (M_PI / 180.0f));

    /* translate(-center) * skew() * translate(center) */
    matrix->_11 = 1.0f;
    matrix->_12 = tan_y;
    matrix->_21 = tan_x;
    matrix->_22 = 1.0f;
    matrix->_31 = -tan_x * center.y;
    matrix->_32 = -tan_y * center.x;
}

BOOL WINAPI D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F *matrix)
{
    TRACE("matrix %p.\n", matrix);

    return (matrix->_11 * matrix->_22 - matrix->_21 * matrix->_12) != 0.0f;
}

BOOL WINAPI D2D1InvertMatrix(D2D1_MATRIX_3X2_F *matrix)
{
    D2D1_MATRIX_3X2_F m = *matrix;

    TRACE("matrix %p.\n", matrix);

    return d2d_matrix_invert(matrix, &m);
}

float WINAPI D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F *matrix)
{
    const float (*m)[2] = matrix->m;
    float a1, a2, c;

    TRACE("matrix %p.\n", matrix);

    /* 2x2 matrix, _31 and _32 are ignored. */
    a1 = m[0][0] * m[0][0] + m[1][0] * m[1][0];
    a2 = m[0][1] * m[0][1] + m[1][1] * m[1][1];
    c = m[0][0] * m[0][1] + m[1][0] * m[1][1];

    /* Maximum scale factor of matrix M refers to maximum value of |Mv|/|v| over all vectors v, where |.| is
     * vector length. That is defined as matrix spectral norm. Spectral norm equals to the maximum of the
     * singular values s1, s2 for 2x2 matrix M.
     * s_i^2 = e_i where e_i (e1, e2) are eigenvalues of (transpose(M) * M)
     * e1 + e2 = trace(transpose(M) * M) = a1 + a2
     * e1 * e2 = det(transpose(M) * M) = a1 * a2 - c ^ 2. */
    return sqrtf(0.5f * (a1 + a2 + sqrtf((a1 - a2) * (a1 - a2) + 4 * c * c)));
}

HRESULT WINAPI D2D1CreateDevice(IDXGIDevice *dxgi_device,
        const D2D1_CREATION_PROPERTIES *properties, ID2D1Device **device)
{
    D2D1_CREATION_PROPERTIES default_properties = {0};
    D2D1_FACTORY_OPTIONS factory_options;
    D2D1_FACTORY_TYPE factory_type;
    ID3D11Device *d3d_device;
    ID2D1Factory1 *factory;
    HRESULT hr;

    TRACE("dxgi_device %p, properties %p, device %p.\n", dxgi_device, properties, device);

    if (!properties)
    {
        if (SUCCEEDED(IDXGIDevice_QueryInterface(dxgi_device, &IID_ID3D11Device, (void **)&d3d_device)))
        {
            if (!(ID3D11Device_GetCreationFlags(d3d_device) & D3D11_CREATE_DEVICE_SINGLETHREADED))
                default_properties.threadingMode = D2D1_THREADING_MODE_MULTI_THREADED;
            ID3D11Device_Release(d3d_device);
        }
        properties = &default_properties;
    }

    switch (properties->threadingMode)
    {
    case D2D1_THREADING_MODE_SINGLE_THREADED:
        factory_type = D2D1_FACTORY_TYPE_SINGLE_THREADED;
        break;
    case D2D1_THREADING_MODE_MULTI_THREADED:
        factory_type = D2D1_FACTORY_TYPE_MULTI_THREADED;
        break;
    default:
        return E_INVALIDARG;
    }

    factory_options.debugLevel = properties->debugLevel;
    if (FAILED(hr = D2D1CreateFactory(factory_type, &IID_ID2D1Factory1, &factory_options, (void **)&factory)))
        return hr;

    hr = ID2D1Factory1_CreateDevice(factory, dxgi_device, device);
    ID2D1Factory1_Release(factory);
    return hr;
}

HRESULT WINAPI D2D1CreateDeviceContext(IDXGISurface *dxgi_surface,
        const D2D1_CREATION_PROPERTIES *properties, ID2D1DeviceContext **context)
{
    IDXGIDevice *dxgi_device;
    ID2D1Device *device;
    HRESULT hr;

    TRACE("dxgi_surface %p, properties %p, context %p.\n", dxgi_surface, properties, context);

    if (FAILED(hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_device)))
        return hr;

    if (SUCCEEDED(hr = D2D1CreateDevice(dxgi_device, properties, &device)))
    {
        hr = ID2D1Device_CreateDeviceContext(device, properties ? properties->options : D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context);
        ID2D1Device_Release(device);
    }

    IDXGIDevice_Release(dxgi_device);
    return hr;
}

void WINAPI D2D1SinCos(float angle, float *s, float *c)
{
    TRACE("angle %.8e, s %p, c %p.\n", angle, s, c);

    *s = sinf(angle);
    *c = cosf(angle);
}

float WINAPI D2D1Tan(float angle)
{
    TRACE("angle %.8e.\n", angle);

    return tanf(angle);
}

float WINAPI D2D1Vec3Length(float x, float y, float z)
{
    TRACE("x %.8e, y %.8e, z %.8e.\n", x, y, z);

    return sqrtf(x * x + y * y + z * z);
}

/* See IEC 61966-2-1:1999; also described in the EXT_texture_sRGB OpenGL
 * extension, among others. */
static float srgb_transfer_function(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    else if (x >= 1.0f)
        return 1.0f;
    else if (x <= 0.0031308f)
        return 12.92f * x;
    else
        return 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static float srgb_inverse_transfer_function(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    else if (x >= 1.0f)
        return 1.0f;
    else if (x <= 0.04045f)
        return x / 12.92f;
    else
        return powf((x + 0.055f) / 1.055f, 2.4f);
}

D2D1_COLOR_F WINAPI D2D1ConvertColorSpace(D2D1_COLOR_SPACE src_colour_space,
        D2D1_COLOR_SPACE dst_colour_space, const D2D1_COLOR_F *colour)
{
    D2D1_COLOR_F ret;

    TRACE("src_colour_space %#x, dst_colour_space %#x, colour %s.\n",
            src_colour_space, dst_colour_space, debug_d2d_color_f(colour));

    if (src_colour_space == D2D1_COLOR_SPACE_CUSTOM || dst_colour_space == D2D1_COLOR_SPACE_CUSTOM)
    {
        ret.r = 0.0f;
        ret.g = 0.0f;
        ret.b = 0.0f;
        ret.a = 0.0f;

        return ret;
    }

    if (src_colour_space == dst_colour_space)
        return *colour;

    if (src_colour_space == D2D1_COLOR_SPACE_SRGB && dst_colour_space == D2D1_COLOR_SPACE_SCRGB)
    {
        ret.r = srgb_inverse_transfer_function(colour->r);
        ret.g = srgb_inverse_transfer_function(colour->g);
        ret.b = srgb_inverse_transfer_function(colour->b);
        ret.a = colour->a;

        return ret;
    }

    if (src_colour_space == D2D1_COLOR_SPACE_SCRGB && dst_colour_space == D2D1_COLOR_SPACE_SRGB)
    {
        ret.r = srgb_transfer_function(colour->r);
        ret.g = srgb_transfer_function(colour->g);
        ret.b = srgb_transfer_function(colour->b);
        ret.a = colour->a;

        return ret;
    }

    FIXME("Unhandled conversion from source colour space %#x to destination colour space %#x.\n",
            src_colour_space, dst_colour_space);
    ret.r = 0.0f;
    ret.g = 0.0f;
    ret.b = 0.0f;
    ret.a = 0.0f;

    return ret;
}

static bool get_config_key_u32(HKEY default_key, HKEY application_key, const char *name, uint32_t *value)
{
    DWORD type, size;
    uint32_t data;

    size = sizeof(data);
    if (application_key && !RegQueryValueExA(application_key,
            name, 0, &type, (BYTE *)&data, &size) && type == REG_DWORD)
        goto success;

    size = sizeof(data);
    if (default_key && !RegQueryValueExA(default_key,
            name, 0, &type, (BYTE *)&data, &size) && type == REG_DWORD)
        goto success;

    return false;

success:
    *value = data;
    return true;
}

static void d2d_settings_init(void)
{
    HKEY default_key, tmp_key, application_key = NULL;
    char buffer[MAX_PATH + 10];
    DWORD len;

    if (RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\Direct2D", &default_key))
        default_key = NULL;

    len = GetModuleFileNameA(0, buffer, MAX_PATH);
    if (len && len < MAX_PATH)
    {
        char *p, *appname = buffer;

        if ((p = strrchr(appname, '/')))
            appname = p + 1;
        if ((p = strrchr(appname, '\\')))
            appname = p + 1;
        strcat(appname, "\\Direct2D");

        if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\AppDefaults", &tmp_key))
        {
            if (RegOpenKeyA(tmp_key, appname, &application_key))
                application_key = NULL;
            RegCloseKey(tmp_key);
        }
    }

    if (!default_key && !application_key)
        return;

    if (get_config_key_u32(default_key, application_key, "max_version_factory", &d2d_settings.max_version_factory))
        ERR_(winediag)("Limiting maximum Direct2D factory version to %#x.\n", d2d_settings.max_version_factory);

    if (application_key)
        RegCloseKey(application_key);
    if (default_key)
        RegCloseKey(default_key);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        d2d_settings_init();
    if (reason == DLL_PROCESS_DETACH && !reserved)
        d2d_path_cache_free();
    return TRUE;
}
