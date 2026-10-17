/*
 * PROJECT:     LiberNT Photo Metadata Handler
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Photo thumbnail provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wingdi.h>
#include <wincodec.h>
#include <thumbcache.h>

typedef struct _PHOTO_THUMBNAIL_PROVIDER
{
    IThumbnailProvider IThumbnailProvider_iface;
    IInitializeWithStream IInitializeWithStream_iface;
    LONG Ref;
    IStream *Stream;
} PHOTO_THUMBNAIL_PROVIDER;

static inline PHOTO_THUMBNAIL_PROVIDER *
impl_from_provider(IThumbnailProvider *iface)
{
    return CONTAINING_RECORD(iface, PHOTO_THUMBNAIL_PROVIDER, IThumbnailProvider_iface);
}

static inline PHOTO_THUMBNAIL_PROVIDER *
impl_from_initialize(IInitializeWithStream *iface)
{
    return CONTAINING_RECORD(iface, PHOTO_THUMBNAIL_PROVIDER, IInitializeWithStream_iface);
}

static HRESULT WINAPI
provider_QueryInterface(IThumbnailProvider *iface, REFIID riid, void **ppv)
{
    PHOTO_THUMBNAIL_PROVIDER *This = impl_from_provider(iface);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IThumbnailProvider))
        *ppv = &This->IThumbnailProvider_iface;
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
provider_AddRef(IThumbnailProvider *iface)
{
    return InterlockedIncrement(&impl_from_provider(iface)->Ref);
}

static ULONG WINAPI
provider_Release(IThumbnailProvider *iface)
{
    PHOTO_THUMBNAIL_PROVIDER *This = impl_from_provider(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        if (This->Stream)
            IStream_Release(This->Stream);
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&ModuleObjects);
    }
    return Ref;
}

static BOOL
FormatHasAlpha(IWICImagingFactory *Factory, IWICBitmapFrameDecode *Frame)
{
    IWICComponentInfo *Component = NULL;
    IWICPixelFormatInfo2 *Pixel = NULL;
    WICPixelFormatGUID Format;
    BOOL Transparent = FALSE;

    if (SUCCEEDED(IWICBitmapFrameDecode_GetPixelFormat(Frame, &Format)) &&
        SUCCEEDED(IWICImagingFactory_CreateComponentInfo(Factory, &Format, &Component)) &&
        SUCCEEDED(IWICComponentInfo_QueryInterface(Component, &IID_IWICPixelFormatInfo2, (void **)&Pixel)) &&
        FAILED(IWICPixelFormatInfo2_SupportsTransparency(Pixel, &Transparent)))
    {
        Transparent = FALSE;
    }
    if (Pixel)
        IWICPixelFormatInfo2_Release(Pixel);
    if (Component)
        IWICComponentInfo_Release(Component);
    return Transparent;
}

static HRESULT WINAPI
provider_GetThumbnail(IThumbnailProvider *iface, UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
    PHOTO_THUMBNAIL_PROVIDER *This = impl_from_provider(iface);
    IWICImagingFactory *Factory = NULL;
    IWICBitmapDecoder *Decoder = NULL;
    IWICBitmapFrameDecode *Frame = NULL;
    IWICBitmapScaler *Scaler = NULL;
    IWICFormatConverter *Converter = NULL;
    IWICBitmapSource *Source;
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER) } };
    LARGE_INTEGER Zero = { { 0 } };
    UINT Width, Height, Longest, TargetWidth, TargetHeight;
    HBITMAP Bitmap = NULL;
    BOOL Transparent;
    void *Bits = NULL;
    HRESULT hr;

    if (!phbmp || !pdwAlpha)
        return E_POINTER;
    *phbmp = NULL;
    *pdwAlpha = WTSAT_UNKNOWN;
    if (!This->Stream)
        return E_UNEXPECTED;
    if (!cx)
        return E_INVALIDARG;

    hr = IStream_Seek(This->Stream, Zero, STREAM_SEEK_SET, NULL);
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&Factory);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateDecoderFromStream(Factory, This->Stream, NULL, WICDecodeMetadataCacheOnDemand, &Decoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapDecoder_GetFrame(Decoder, 0, &Frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameDecode_GetSize(Frame, &Width, &Height);
    if (SUCCEEDED(hr) && (!Width || !Height))
        hr = E_FAIL;
    if (FAILED(hr))
        goto done;

    Longest = max(Width, Height);
    TargetWidth = Width;
    TargetHeight = Height;
    if (Longest > cx)
    {
        TargetWidth = max(1, (UINT)(((ULONGLONG)Width * cx + Longest / 2) / Longest));
        TargetHeight = max(1, (UINT)(((ULONGLONG)Height * cx + Longest / 2) / Longest));
    }

    Source = (IWICBitmapSource *)Frame;
    if (TargetWidth != Width || TargetHeight != Height)
    {
        hr = IWICImagingFactory_CreateBitmapScaler(Factory, &Scaler);
        if (SUCCEEDED(hr))
            hr = IWICBitmapScaler_Initialize(Scaler, Source, TargetWidth, TargetHeight, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
            goto done;
        Source = (IWICBitmapSource *)Scaler;
    }

    Transparent = FormatHasAlpha(Factory, Frame);
    hr = IWICImagingFactory_CreateFormatConverter(Factory, &Converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(Converter, Source,
                                            Transparent ? &GUID_WICPixelFormat32bppPBGRA : &GUID_WICPixelFormat32bppBGR,
                                            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        goto done;

    Info.bmiHeader.biWidth = TargetWidth;
    Info.bmiHeader.biHeight = -(LONG)TargetHeight;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
    if (!Bitmap)
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    hr = IWICFormatConverter_CopyPixels(Converter, NULL, TargetWidth * 4, TargetWidth * 4 * TargetHeight, Bits);
    if (FAILED(hr))
        goto done;
    if (!Transparent)
    {
        BYTE *Pixel = Bits;
        UINT i;

        for (i = 0; i < TargetWidth * TargetHeight; i++)
            Pixel[i * 4 + 3] = 0xFF;
    }

    *phbmp = Bitmap;
    *pdwAlpha = Transparent ? WTSAT_ARGB : WTSAT_RGB;
    Bitmap = NULL;

done:
    if (Bitmap)
        DeleteObject(Bitmap);
    if (Converter)
        IWICFormatConverter_Release(Converter);
    if (Scaler)
        IWICBitmapScaler_Release(Scaler);
    if (Frame)
        IWICBitmapFrameDecode_Release(Frame);
    if (Decoder)
        IWICBitmapDecoder_Release(Decoder);
    if (Factory)
        IWICImagingFactory_Release(Factory);
    return hr;
}

static const IThumbnailProviderVtbl ProviderVtbl =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_GetThumbnail,
};

static HRESULT WINAPI
initialize_QueryInterface(IInitializeWithStream *iface, REFIID riid, void **ppv)
{
    return provider_QueryInterface(&impl_from_initialize(iface)->IThumbnailProvider_iface, riid, ppv);
}

static ULONG WINAPI
initialize_AddRef(IInitializeWithStream *iface)
{
    return provider_AddRef(&impl_from_initialize(iface)->IThumbnailProvider_iface);
}

static ULONG WINAPI
initialize_Release(IInitializeWithStream *iface)
{
    return provider_Release(&impl_from_initialize(iface)->IThumbnailProvider_iface);
}

static HRESULT WINAPI
initialize_Initialize(IInitializeWithStream *iface, IStream *pstream, DWORD grfMode)
{
    PHOTO_THUMBNAIL_PROVIDER *This = impl_from_initialize(iface);

    if (!pstream)
        return E_INVALIDARG;
    if (This->Stream)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    IStream_AddRef(pstream);
    This->Stream = pstream;
    return S_OK;
}

static const IInitializeWithStreamVtbl InitializeVtbl =
{
    initialize_QueryInterface,
    initialize_AddRef,
    initialize_Release,
    initialize_Initialize,
};

HRESULT
PhotoThumbnailProvider_Create(REFIID riid, void **ppv)
{
    PHOTO_THUMBNAIL_PROVIDER *This;
    HRESULT hr;

    if (!(This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IThumbnailProvider_iface.lpVtbl = &ProviderVtbl;
    This->IInitializeWithStream_iface.lpVtbl = &InitializeVtbl;
    This->Ref = 1;
    InterlockedIncrement(&ModuleObjects);
    hr = provider_QueryInterface(&This->IThumbnailProvider_iface, riid, ppv);
    provider_Release(&This->IThumbnailProvider_iface);
    return hr;
}
