/*
 * PROJECT:     LiberNT Photo Metadata Handler
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Image property handler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wincodec.h>
#include <propkey.h>
#include <propvarutil.h>
#include <stdio.h>

typedef struct _IMAGE_PROPERTY_HANDLER
{
    IPropertyStore IPropertyStore_iface;
    IPropertyStoreCapabilities IPropertyStoreCapabilities_iface;
    IInitializeWithStream IInitializeWithStream_iface;
    LONG Ref;
    IPropertyStore *Values;
    DWORD Mode;
} IMAGE_PROPERTY_HANDLER;

static inline IMAGE_PROPERTY_HANDLER *
impl_from_store(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, IMAGE_PROPERTY_HANDLER, IPropertyStore_iface);
}

static inline IMAGE_PROPERTY_HANDLER *
impl_from_capabilities(IPropertyStoreCapabilities *iface)
{
    return CONTAINING_RECORD(iface, IMAGE_PROPERTY_HANDLER, IPropertyStoreCapabilities_iface);
}

static inline IMAGE_PROPERTY_HANDLER *
impl_from_initialize(IInitializeWithStream *iface)
{
    return CONTAINING_RECORD(iface, IMAGE_PROPERTY_HANDLER, IInitializeWithStream_iface);
}

static HRESULT WINAPI
store_QueryInterface(IPropertyStore *iface, REFIID riid, void **ppv)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyStore))
        *ppv = &This->IPropertyStore_iface;
    else if (IsEqualIID(riid, &IID_IPropertyStoreCapabilities))
        *ppv = &This->IPropertyStoreCapabilities_iface;
    else if (IsEqualIID(riid, &IID_IInitializeWithStream))
        *ppv = &This->IInitializeWithStream_iface;
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI
store_AddRef(IPropertyStore *iface)
{
    return InterlockedIncrement(&impl_from_store(iface)->Ref);
}

static ULONG WINAPI
store_Release(IPropertyStore *iface)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        if (This->Values)
            IPropertyStore_Release(This->Values);
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&ModuleObjects);
    }
    return Ref;
}

static HRESULT WINAPI
store_GetCount(IPropertyStore *iface, DWORD *cProps)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);

    if (!cProps)
        return E_POINTER;
    if (!This->Values)
    {
        *cProps = 0;
        return E_UNEXPECTED;
    }
    return IPropertyStore_GetCount(This->Values, cProps);
}

static HRESULT WINAPI
store_GetAt(IPropertyStore *iface, DWORD iProp, PROPERTYKEY *pkey)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);

    if (!This->Values)
        return E_UNEXPECTED;
    return IPropertyStore_GetAt(This->Values, iProp, pkey);
}

static HRESULT WINAPI
store_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *pv)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);

    if (!pv)
        return E_POINTER;
    PropVariantInit(pv);
    if (!This->Values)
        return E_UNEXPECTED;
    return IPropertyStore_GetValue(This->Values, key, pv);
}

static HRESULT WINAPI
store_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT propvar)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI
store_Commit(IPropertyStore *iface)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_store(iface);

    if (!This->Values)
        return E_UNEXPECTED;
    return (This->Mode & (STGM_WRITE | STGM_READWRITE)) ? S_OK : STG_E_ACCESSDENIED;
}

static const IPropertyStoreVtbl ImageStoreVtbl =
{
    store_QueryInterface,
    store_AddRef,
    store_Release,
    store_GetCount,
    store_GetAt,
    store_GetValue,
    store_SetValue,
    store_Commit,
};

static HRESULT WINAPI
capabilities_QueryInterface(IPropertyStoreCapabilities *iface, REFIID riid, void **ppv)
{
    return store_QueryInterface(&impl_from_capabilities(iface)->IPropertyStore_iface, riid, ppv);
}

static ULONG WINAPI
capabilities_AddRef(IPropertyStoreCapabilities *iface)
{
    return store_AddRef(&impl_from_capabilities(iface)->IPropertyStore_iface);
}

static ULONG WINAPI
capabilities_Release(IPropertyStoreCapabilities *iface)
{
    return store_Release(&impl_from_capabilities(iface)->IPropertyStore_iface);
}

static HRESULT WINAPI
capabilities_IsPropertyWritable(IPropertyStoreCapabilities *iface, REFPROPERTYKEY key)
{
    return S_FALSE;
}

static const IPropertyStoreCapabilitiesVtbl ImageCapabilitiesVtbl =
{
    capabilities_QueryInterface,
    capabilities_AddRef,
    capabilities_Release,
    capabilities_IsPropertyWritable,
};

static void
StoreValue(IPropertyStore *Store, REFPROPERTYKEY Key, PROPVARIANT *Value, HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        IPropertyStore_SetValue(Store, Key, Value);
        PropVariantClear(Value);
    }
}

static void
StoreString(IPropertyStore *Store, REFPROPERTYKEY Key, LPCWSTR Text)
{
    PROPVARIANT Value;
    SIZE_T Size = (wcslen(Text) + 1) * sizeof(WCHAR);

    PropVariantInit(&Value);
    Value.vt = VT_LPWSTR;
    if (!(Value.pwszVal = CoTaskMemAlloc(Size)))
        return;
    memcpy(Value.pwszVal, Text, Size);
    StoreValue(Store, Key, &Value, S_OK);
}

static void
StoreUInt32(IPropertyStore *Store, REFPROPERTYKEY Key, ULONG Number)
{
    PROPVARIANT Value;

    PropVariantInit(&Value);
    Value.vt = VT_UI4;
    Value.ulVal = Number;
    StoreValue(Store, Key, &Value, S_OK);
}

static void
StoreDouble(IPropertyStore *Store, REFPROPERTYKEY Key, double Number)
{
    PROPVARIANT Value;

    PropVariantInit(&Value);
    Value.vt = VT_R8;
    Value.dblVal = Number;
    StoreValue(Store, Key, &Value, S_OK);
}

static HRESULT
ReadImage(IStream *Stream, IPropertyStore *Store)
{
    IWICImagingFactory *Factory = NULL;
    IWICBitmapDecoder *Decoder = NULL;
    IWICBitmapDecoderInfo *Info = NULL;
    IWICBitmapFrameDecode *Frame = NULL;
    IWICComponentInfo *Component = NULL;
    IWICPixelFormatInfo *Pixel = NULL;
    WICPixelFormatGUID Format;
    GUID Container;
    WCHAR Mime[256], Text[64];
    UINT Width = 0, Height = 0, Bits = 0, Length = 0;
    double DpiX = 0, DpiY = 0;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&Factory);
    if (FAILED(hr))
        return hr;
    hr = IWICImagingFactory_CreateDecoderFromStream(Factory, Stream, NULL, WICDecodeMetadataCacheOnDemand, &Decoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapDecoder_GetFrame(Decoder, 0, &Frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameDecode_GetSize(Frame, &Width, &Height);
    if (FAILED(hr))
        goto done;

    swprintf(Text, ARRAYSIZE(Text), L"\x202a%u x %u\x202c", Width, Height);
    StoreString(Store, &PKEY_Image_Dimensions, Text);
    StoreUInt32(Store, &PKEY_Image_HorizontalSize, Width);
    StoreUInt32(Store, &PKEY_Image_VerticalSize, Height);
    if (SUCCEEDED(IWICBitmapFrameDecode_GetPixelFormat(Frame, &Format)) &&
        SUCCEEDED(IWICImagingFactory_CreateComponentInfo(Factory, &Format, &Component)) &&
        SUCCEEDED(IWICComponentInfo_QueryInterface(Component, &IID_IWICPixelFormatInfo, (void **)&Pixel)) &&
        SUCCEEDED(IWICPixelFormatInfo_GetBitsPerPixel(Pixel, &Bits)))
        StoreUInt32(Store, &PKEY_Image_BitDepth, Bits);
    StoreString(Store, &PKEY_Media_ClassPrimaryID, L"{6FB2E74A-B8CB-40BB-93F3-FAC5F00FA203}");
    if (SUCCEEDED(IWICBitmapFrameDecode_GetResolution(Frame, &DpiX, &DpiY)))
    {
        StoreDouble(Store, &PKEY_Image_HorizontalResolution, DpiX);
        StoreDouble(Store, &PKEY_Image_VerticalResolution, DpiY);
    }
    if (SUCCEEDED(IWICBitmapDecoder_GetDecoderInfo(Decoder, &Info)) &&
        SUCCEEDED(IWICBitmapDecoderInfo_GetMimeTypes(Info, ARRAYSIZE(Mime), Mime, &Length)) && *Mime)
    {
        WCHAR *Comma = wcschr(Mime, L',');
        if (Comma)
            *Comma = 0;
        StoreString(Store, &PKEY_MIMEType, Mime);
    }
    if (SUCCEEDED(IWICBitmapDecoder_GetContainerFormat(Decoder, &Container)) &&
        IsEqualGUID(&Container, &GUID_ContainerFormatPng))
    {
        PCWSTR Profile = L"PNG_LRG";
        PROPVARIANT Value;
        StoreValue(Store, &PKEY_Media_DlnaProfileID, &Value, InitPropVariantFromStringVector(&Profile, 1, &Value));
    }

done:
    if (Pixel)
        IWICPixelFormatInfo_Release(Pixel);
    if (Component)
        IWICComponentInfo_Release(Component);
    if (Info)
        IWICBitmapDecoderInfo_Release(Info);
    if (Frame)
        IWICBitmapFrameDecode_Release(Frame);
    if (Decoder)
        IWICBitmapDecoder_Release(Decoder);
    IWICImagingFactory_Release(Factory);
    return hr;
}

static HRESULT WINAPI
initialize_QueryInterface(IInitializeWithStream *iface, REFIID riid, void **ppv)
{
    return store_QueryInterface(&impl_from_initialize(iface)->IPropertyStore_iface, riid, ppv);
}

static ULONG WINAPI
initialize_AddRef(IInitializeWithStream *iface)
{
    return store_AddRef(&impl_from_initialize(iface)->IPropertyStore_iface);
}

static ULONG WINAPI
initialize_Release(IInitializeWithStream *iface)
{
    return store_Release(&impl_from_initialize(iface)->IPropertyStore_iface);
}

static HRESULT WINAPI
initialize_Initialize(IInitializeWithStream *iface, IStream *pstream, DWORD grfMode)
{
    IMAGE_PROPERTY_HANDLER *This = impl_from_initialize(iface);
    IPropertyStore *Values;
    HRESULT hr;

    if (!pstream)
        return E_INVALIDARG;
    if (This->Values)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    hr = PSCreateMemoryPropertyStore(&IID_IPropertyStore, (void **)&Values);
    if (FAILED(hr))
        return hr;
    hr = ReadImage(pstream, Values);
    if (FAILED(hr))
    {
        IPropertyStore_Release(Values);
        return hr;
    }
    This->Values = Values;
    This->Mode = grfMode;
    return S_OK;
}

static const IInitializeWithStreamVtbl ImageInitializeVtbl =
{
    initialize_QueryInterface,
    initialize_AddRef,
    initialize_Release,
    initialize_Initialize,
};

HRESULT
ImagePropertyHandler_Create(REFIID riid, void **ppv)
{
    IMAGE_PROPERTY_HANDLER *This;
    HRESULT hr;

    if (!(This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IPropertyStore_iface.lpVtbl = &ImageStoreVtbl;
    This->IPropertyStoreCapabilities_iface.lpVtbl = &ImageCapabilitiesVtbl;
    This->IInitializeWithStream_iface.lpVtbl = &ImageInitializeVtbl;
    This->Ref = 1;
    InterlockedIncrement(&ModuleObjects);
    hr = store_QueryInterface(&This->IPropertyStore_iface, riid, ppv);
    store_Release(&This->IPropertyStore_iface);
    return hr;
}
