/*
 * PROJECT:     LiberNT Direct3D 11
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows Runtime Direct3D device and surface objects over DXGI interfaces
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "d3d11_private.h"
#include "initguid.h"
#include "roapi.h"
#include "winstring.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_Graphics_DirectX_Direct3D11
#include "windows.foundation.h"
#include "windows.graphics.directx.direct3d11.h"
#include "windows.graphics.directx.direct3d11.interop.h"
#include "weakref.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

struct winrt_object
{
    union
    {
        IInspectable IInspectable_iface;
        IDirect3DDevice IDirect3DDevice_iface;
        IDirect3DSurface IDirect3DSurface_iface;
    };
    IClosable IClosable_iface;
    IDirect3DDxgiInterfaceAccess IDirect3DDxgiInterfaceAccess_iface;
    struct weak_reference_source weak_reference_source;
    IUnknown *marshal;
    const IID *iid;
    const WCHAR *class_name;
    SRWLOCK lock;
    IUnknown *dxgi;
};

static inline struct winrt_object *impl_from_IInspectable(IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct winrt_object, IInspectable_iface);
}

static HRESULT STDMETHODCALLTYPE winrt_object_QueryInterface(IInspectable *iface, REFIID iid, void **out)
{
    struct winrt_object *object = impl_from_IInspectable(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAgileObject)
            || IsEqualGUID(iid, object->iid))
        *out = &object->IInspectable_iface;
    else if (IsEqualGUID(iid, &IID_IClosable))
        *out = &object->IClosable_iface;
    else if (IsEqualGUID(iid, &IID_IDirect3DDxgiInterfaceAccess))
        *out = &object->IDirect3DDxgiInterfaceAccess_iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &object->weak_reference_source.IWeakReferenceSource_iface;
    else if (IsEqualGUID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(object->marshal, iid, out);
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE winrt_object_AddRef(IInspectable *iface)
{
    struct winrt_object *object = impl_from_IInspectable(iface);
    ULONG refcount = weak_reference_strong_add_ref(&object->weak_reference_source);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);
    assert(refcount > 1);
    return refcount;
}

static ULONG STDMETHODCALLTYPE winrt_object_Release(IInspectable *iface)
{
    struct winrt_object *object = impl_from_IInspectable(iface);
    ULONG refcount = weak_reference_strong_release(&object->weak_reference_source);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);
    assert(refcount != ~0u);

    if (!refcount)
    {
        if (object->dxgi)
            IUnknown_Release(object->dxgi);
        IUnknown_Release(object->marshal);
        free(object);
    }
    return refcount;
}

static HRESULT STDMETHODCALLTYPE winrt_object_GetIids(IInspectable *iface, ULONG *count, IID **iids)
{
    struct winrt_object *object = impl_from_IInspectable(iface);
    IID *array;

    TRACE("iface %p, count %p, iids %p.\n", iface, count, iids);

    if (!(array = CoTaskMemAlloc(3 * sizeof(*array))))
        return E_OUTOFMEMORY;
    array[0] = *object->iid;
    array[1] = IID_IWeakReferenceSource;
    array[2] = IID_IClosable;
    *count = 3;
    *iids = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE winrt_object_GetRuntimeClassName(IInspectable *iface, HSTRING *class_name)
{
    struct winrt_object *object = impl_from_IInspectable(iface);

    TRACE("iface %p, class_name %p.\n", iface, class_name);

    return WindowsCreateString(object->class_name, wcslen(object->class_name), class_name);
}

static HRESULT STDMETHODCALLTYPE winrt_object_GetTrustLevel(IInspectable *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);

    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT winrt_object_get_dxgi(struct winrt_object *object, REFIID iid, void **out)
{
    HRESULT hr = RO_E_CLOSED;

    AcquireSRWLockShared(&object->lock);
    if (object->dxgi)
        hr = IUnknown_QueryInterface(object->dxgi, iid, out);
    ReleaseSRWLockShared(&object->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE winrt_device_Trim(IDirect3DDevice *iface)
{
    struct winrt_object *object = impl_from_IInspectable((IInspectable *)iface);
    IDXGIDevice3 *device;
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    if (FAILED(hr = winrt_object_get_dxgi(object, &IID_IDXGIDevice3, (void **)&device)))
        return hr;
    IDXGIDevice3_Trim(device);
    IDXGIDevice3_Release(device);
    return S_OK;
}

static const struct IDirect3DDeviceVtbl winrt_device_vtbl =
{
    (void *)winrt_object_QueryInterface,
    (void *)winrt_object_AddRef,
    (void *)winrt_object_Release,
    (void *)winrt_object_GetIids,
    (void *)winrt_object_GetRuntimeClassName,
    (void *)winrt_object_GetTrustLevel,
    winrt_device_Trim,
};

static HRESULT STDMETHODCALLTYPE winrt_surface_get_Description(IDirect3DSurface *iface,
        Direct3DSurfaceDescription *value)
{
    struct winrt_object *object = impl_from_IInspectable((IInspectable *)iface);
    DXGI_SURFACE_DESC desc;
    IDXGISurface *surface;
    HRESULT hr;

    TRACE("iface %p, value %p.\n", iface, value);

    if (!value)
        return E_INVALIDARG;
    if (FAILED(hr = winrt_object_get_dxgi(object, &IID_IDXGISurface, (void **)&surface)))
        return hr;
    hr = IDXGISurface_GetDesc(surface, &desc);
    IDXGISurface_Release(surface);
    if (FAILED(hr))
        return hr;

    value->Width = desc.Width;
    value->Height = desc.Height;
    value->Format = (DirectXPixelFormat)desc.Format;
    value->MultisampleDescription.Count = desc.SampleDesc.Count;
    value->MultisampleDescription.Quality = desc.SampleDesc.Quality;
    return S_OK;
}

static const struct IDirect3DSurfaceVtbl winrt_surface_vtbl =
{
    (void *)winrt_object_QueryInterface,
    (void *)winrt_object_AddRef,
    (void *)winrt_object_Release,
    (void *)winrt_object_GetIids,
    (void *)winrt_object_GetRuntimeClassName,
    (void *)winrt_object_GetTrustLevel,
    winrt_surface_get_Description,
};

static inline struct winrt_object *impl_from_IClosable(IClosable *iface)
{
    return CONTAINING_RECORD(iface, struct winrt_object, IClosable_iface);
}

static HRESULT STDMETHODCALLTYPE winrt_closable_QueryInterface(IClosable *iface, REFIID iid, void **out)
{
    return winrt_object_QueryInterface(&impl_from_IClosable(iface)->IInspectable_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE winrt_closable_AddRef(IClosable *iface)
{
    return winrt_object_AddRef(&impl_from_IClosable(iface)->IInspectable_iface);
}

static ULONG STDMETHODCALLTYPE winrt_closable_Release(IClosable *iface)
{
    return winrt_object_Release(&impl_from_IClosable(iface)->IInspectable_iface);
}

static HRESULT STDMETHODCALLTYPE winrt_closable_GetIids(IClosable *iface, ULONG *count, IID **iids)
{
    return winrt_object_GetIids(&impl_from_IClosable(iface)->IInspectable_iface, count, iids);
}

static HRESULT STDMETHODCALLTYPE winrt_closable_GetRuntimeClassName(IClosable *iface, HSTRING *class_name)
{
    return winrt_object_GetRuntimeClassName(&impl_from_IClosable(iface)->IInspectable_iface, class_name);
}

static HRESULT STDMETHODCALLTYPE winrt_closable_GetTrustLevel(IClosable *iface, TrustLevel *trust_level)
{
    return winrt_object_GetTrustLevel(&impl_from_IClosable(iface)->IInspectable_iface, trust_level);
}

static HRESULT STDMETHODCALLTYPE winrt_closable_Close(IClosable *iface)
{
    struct winrt_object *object = impl_from_IClosable(iface);
    IUnknown *dxgi;

    TRACE("iface %p.\n", iface);

    AcquireSRWLockExclusive(&object->lock);
    dxgi = object->dxgi;
    object->dxgi = NULL;
    ReleaseSRWLockExclusive(&object->lock);

    if (dxgi)
        IUnknown_Release(dxgi);
    return S_OK;
}

static const struct IClosableVtbl winrt_closable_vtbl =
{
    winrt_closable_QueryInterface,
    winrt_closable_AddRef,
    winrt_closable_Release,
    winrt_closable_GetIids,
    winrt_closable_GetRuntimeClassName,
    winrt_closable_GetTrustLevel,
    winrt_closable_Close,
};

static inline struct winrt_object *impl_from_IDirect3DDxgiInterfaceAccess(IDirect3DDxgiInterfaceAccess *iface)
{
    return CONTAINING_RECORD(iface, struct winrt_object, IDirect3DDxgiInterfaceAccess_iface);
}

static HRESULT STDMETHODCALLTYPE winrt_access_QueryInterface(IDirect3DDxgiInterfaceAccess *iface,
        REFIID iid, void **out)
{
    return winrt_object_QueryInterface(&impl_from_IDirect3DDxgiInterfaceAccess(iface)->IInspectable_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE winrt_access_AddRef(IDirect3DDxgiInterfaceAccess *iface)
{
    return winrt_object_AddRef(&impl_from_IDirect3DDxgiInterfaceAccess(iface)->IInspectable_iface);
}

static ULONG STDMETHODCALLTYPE winrt_access_Release(IDirect3DDxgiInterfaceAccess *iface)
{
    return winrt_object_Release(&impl_from_IDirect3DDxgiInterfaceAccess(iface)->IInspectable_iface);
}

static HRESULT STDMETHODCALLTYPE winrt_access_GetInterface(IDirect3DDxgiInterfaceAccess *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out)
        return E_POINTER;
    return winrt_object_get_dxgi(impl_from_IDirect3DDxgiInterfaceAccess(iface), iid, out);
}

static const struct IDirect3DDxgiInterfaceAccessVtbl winrt_access_vtbl =
{
    winrt_access_QueryInterface,
    winrt_access_AddRef,
    winrt_access_Release,
    winrt_access_GetInterface,
};

static HRESULT winrt_object_create(IUnknown *dxgi, const void *vtbl, const IID *iid,
        const WCHAR *class_name, IInspectable **out)
{
    struct winrt_object *object;
    HRESULT hr;

    if (!dxgi)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;
    object->IInspectable_iface.lpVtbl = vtbl;
    object->IClosable_iface.lpVtbl = &winrt_closable_vtbl;
    object->IDirect3DDxgiInterfaceAccess_iface.lpVtbl = &winrt_access_vtbl;
    object->iid = iid;
    object->class_name = class_name;
    InitializeSRWLock(&object->lock);

    if (FAILED(hr = weak_reference_source_init(&object->weak_reference_source, (IUnknown *)&object->IInspectable_iface)))
    {
        free(object);
        return hr;
    }
    if (FAILED(hr = CoCreateFreeThreadedMarshaler((IUnknown *)&object->IInspectable_iface, &object->marshal)))
    {
        IWeakReference_Release(object->weak_reference_source.weak_reference);
        free(object);
        return hr;
    }

    IUnknown_AddRef(dxgi);
    object->dxgi = dxgi;

    TRACE("Created %s %p for %p.\n", debugstr_w(class_name), object, dxgi);
    *out = &object->IInspectable_iface;
    return S_OK;
}

HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice *device, IInspectable **out)
{
    TRACE("device %p, out %p.\n", device, out);

    return winrt_object_create((IUnknown *)device, &winrt_device_vtbl, &IID_IDirect3DDevice,
            L"Windows.Graphics.DirectX.Direct3D11.IDirect3DDevice", out);
}

HRESULT WINAPI CreateDirect3D11SurfaceFromDXGISurface(IDXGISurface *surface, IInspectable **out)
{
    TRACE("surface %p, out %p.\n", surface, out);

    return winrt_object_create((IUnknown *)surface, &winrt_surface_vtbl, &IID_IDirect3DSurface,
            L"Windows.Graphics.DirectX.Direct3D11.IDirect3DSurface", out);
}
