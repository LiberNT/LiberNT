/*
 * PROJECT:     LiberNT Thumbnail Cache
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Local thumbnail cache and shared bitmap objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#define CONST_VTABLE

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <thumbcache.h>

#include <initguid.h>

DEFINE_GUID(CLSID_LocalThumbnailCache, 0x50ef4544, 0xac9f, 0x4a8e, 0xb2, 0x1b, 0x8a, 0x26, 0x18, 0x0d, 0xb1, 0x3f);
DEFINE_GUID(CLSID_SharedBitmap, 0x4db26476, 0x6787, 0x4046, 0xb8, 0x36, 0xe8, 0x41, 0x2a, 0x9e, 0x8a, 0x27);

#define CACHE_ENTRIES 256

typedef struct _CACHE_ENTRY
{
    WTS_THUMBNAILID Id;
    UINT Size;
    HBITMAP Bitmap;
    WTS_ALPHATYPE Alpha;
    ULONGLONG Stamp;
} CACHE_ENTRY;

typedef struct _SHARED_BITMAP
{
    ISharedBitmap ISharedBitmap_iface;
    LONG Ref;
    HBITMAP Bitmap;
    WTS_ALPHATYPE Alpha;
} SHARED_BITMAP;

typedef struct _THUMBNAIL_CACHE
{
    IThumbnailCache IThumbnailCache_iface;
    LONG Ref;
} THUMBNAIL_CACHE;

static LONG ModuleObjects;
static LONG ModuleLocks;
static CRITICAL_SECTION CacheLock;
static CACHE_ENTRY Cache[CACHE_ENTRIES];
static ULONGLONG CacheClock;

static HBITMAP
CopyBitmap(HBITMAP Source)
{
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER) } };
    HBITMAP Copy;
    BITMAP Bitmap;
    void *Bits;
    HDC Dc;

    if (!GetObjectW(Source, sizeof(Bitmap), &Bitmap))
        return NULL;
    Info.bmiHeader.biWidth = Bitmap.bmWidth;
    Info.bmiHeader.biHeight = -Bitmap.bmHeight;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Copy = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
    if (!Copy)
        return NULL;
    Dc = GetDC(NULL);
    if (!GetDIBits(Dc, Source, 0, Bitmap.bmHeight, Bits, &Info, DIB_RGB_COLORS))
    {
        DeleteObject(Copy);
        Copy = NULL;
    }
    ReleaseDC(NULL, Dc);
    return Copy;
}

static inline SHARED_BITMAP *
impl_from_ISharedBitmap(ISharedBitmap *iface)
{
    return CONTAINING_RECORD(iface, SHARED_BITMAP, ISharedBitmap_iface);
}

static HRESULT WINAPI
SharedBitmap_QueryInterface(ISharedBitmap *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ISharedBitmap))
    {
        *ppv = iface;
        ISharedBitmap_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
SharedBitmap_AddRef(ISharedBitmap *iface)
{
    return InterlockedIncrement(&impl_from_ISharedBitmap(iface)->Ref);
}

static ULONG WINAPI
SharedBitmap_Release(ISharedBitmap *iface)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        if (This->Bitmap)
            DeleteObject(This->Bitmap);
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&ModuleObjects);
    }
    return Ref;
}

static HRESULT WINAPI
SharedBitmap_GetSharedBitmap(ISharedBitmap *iface, HBITMAP *phbm)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);

    if (!phbm)
        return E_POINTER;
    *phbm = This->Bitmap;
    return This->Bitmap ? S_OK : E_UNEXPECTED;
}

static HRESULT WINAPI
SharedBitmap_GetSize(ISharedBitmap *iface, SIZE *pSize)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);
    BITMAP Bitmap;

    if (!pSize)
        return E_POINTER;
    if (!This->Bitmap || !GetObjectW(This->Bitmap, sizeof(Bitmap), &Bitmap))
        return E_UNEXPECTED;
    pSize->cx = Bitmap.bmWidth;
    pSize->cy = abs(Bitmap.bmHeight);
    return S_OK;
}

static HRESULT WINAPI
SharedBitmap_GetFormat(ISharedBitmap *iface, WTS_ALPHATYPE *pat)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);

    if (!pat)
        return E_POINTER;
    if (!This->Bitmap)
        return E_UNEXPECTED;
    *pat = This->Alpha;
    return S_OK;
}

static HRESULT WINAPI
SharedBitmap_InitializeBitmap(ISharedBitmap *iface, HBITMAP hbm, WTS_ALPHATYPE wtsAT)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);

    if (!hbm)
        return E_INVALIDARG;
    if (This->Bitmap && This->Bitmap != hbm)
        DeleteObject(This->Bitmap);
    This->Bitmap = hbm;
    This->Alpha = wtsAT;
    return S_OK;
}

static HRESULT WINAPI
SharedBitmap_Detach(ISharedBitmap *iface, HBITMAP *phbm)
{
    SHARED_BITMAP *This = impl_from_ISharedBitmap(iface);

    if (!phbm)
        return E_POINTER;
    *phbm = This->Bitmap;
    if (!This->Bitmap)
        return E_UNEXPECTED;
    This->Bitmap = NULL;
    return S_OK;
}

static const ISharedBitmapVtbl SharedBitmapVtbl =
{
    SharedBitmap_QueryInterface,
    SharedBitmap_AddRef,
    SharedBitmap_Release,
    SharedBitmap_GetSharedBitmap,
    SharedBitmap_GetSize,
    SharedBitmap_GetFormat,
    SharedBitmap_InitializeBitmap,
    SharedBitmap_Detach,
};

static HRESULT
SharedBitmap_Construct(HBITMAP Bitmap, WTS_ALPHATYPE Alpha, REFIID riid, void **ppv)
{
    SHARED_BITMAP *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    HRESULT hr;

    if (!This)
        return E_OUTOFMEMORY;
    This->ISharedBitmap_iface.lpVtbl = &SharedBitmapVtbl;
    This->Ref = 1;
    This->Bitmap = Bitmap;
    This->Alpha = Alpha;
    InterlockedIncrement(&ModuleObjects);
    hr = ISharedBitmap_QueryInterface(&This->ISharedBitmap_iface, riid, ppv);
    ISharedBitmap_Release(&This->ISharedBitmap_iface);
    return hr;
}

static void
HashBytes(ULONGLONG *Hash, const void *Data, SIZE_T Size)
{
    const BYTE *Bytes = Data;
    SIZE_T i;

    for (i = 0; i < Size; i++)
    {
        *Hash ^= Bytes[i];
        *Hash *= 0x100000001b3ULL;
    }
}

static HRESULT
ItemThumbnailId(IShellItem *Item, WTS_THUMBNAILID *Id)
{
    WIN32_FILE_ATTRIBUTE_DATA Data;
    ULONGLONG Low = 0xcbf29ce484222325ULL, High = 0x84222325cbf29ce4ULL;
    LPWSTR Name = NULL;
    HRESULT hr;
    SIZE_T i;

    hr = IShellItem_GetDisplayName(Item, SIGDN_FILESYSPATH, &Name);
    if (FAILED(hr))
        hr = IShellItem_GetDisplayName(Item, SIGDN_DESKTOPABSOLUTEPARSING, &Name);
    if (FAILED(hr))
        return hr;

    ZeroMemory(&Data, sizeof(Data));
    GetFileAttributesExW(Name, GetFileExInfoStandard, &Data);
    CharUpperW(Name);
    for (i = 0; Name[i]; i++)
    {
        HashBytes(&Low, &Name[i], sizeof(WCHAR));
        HashBytes(&High, &Name[i], sizeof(WCHAR));
        High = (High << 7) | (High >> 57);
    }
    HashBytes(&Low, &Data.ftLastWriteTime, sizeof(Data.ftLastWriteTime));
    HashBytes(&Low, &Data.nFileSizeLow, sizeof(Data.nFileSizeLow));
    HashBytes(&High, &Data.ftLastWriteTime, sizeof(Data.ftLastWriteTime));
    HashBytes(&High, &Data.nFileSizeHigh, sizeof(Data.nFileSizeHigh));
    CoTaskMemFree(Name);

    memcpy(Id->rgbKey, &Low, sizeof(Low));
    memcpy(Id->rgbKey + sizeof(Low), &High, sizeof(High));
    return S_OK;
}

static HRESULT
CacheLookup(const WTS_THUMBNAILID *Id, UINT Size, ISharedBitmap **ppvThumb)
{
    HBITMAP Copy = NULL;
    WTS_ALPHATYPE Alpha = WTSAT_UNKNOWN;
    BOOL Found = FALSE;
    HRESULT hr;
    UINT i;

    EnterCriticalSection(&CacheLock);
    for (i = 0; i < CACHE_ENTRIES; i++)
    {
        if (Cache[i].Bitmap && Cache[i].Size == Size && !memcmp(&Cache[i].Id, Id, sizeof(*Id)))
        {
            Cache[i].Stamp = ++CacheClock;
            Found = TRUE;
            Alpha = Cache[i].Alpha;
            if (ppvThumb)
                Copy = CopyBitmap(Cache[i].Bitmap);
            break;
        }
    }
    LeaveCriticalSection(&CacheLock);

    if (!Found)
        return STG_E_FILENOTFOUND;
    if (!ppvThumb)
        return S_OK;
    if (!Copy)
        return E_OUTOFMEMORY;
    hr = SharedBitmap_Construct(Copy, Alpha, &IID_ISharedBitmap, (void **)ppvThumb);
    if (FAILED(hr))
        DeleteObject(Copy);
    return hr;
}

static void
CacheStore(const WTS_THUMBNAILID *Id, UINT Size, HBITMAP Bitmap, WTS_ALPHATYPE Alpha)
{
    HBITMAP Copy = CopyBitmap(Bitmap);
    UINT i, Slot = 0;

    if (!Copy)
        return;
    EnterCriticalSection(&CacheLock);
    for (i = 0; i < CACHE_ENTRIES; i++)
    {
        if (Cache[i].Bitmap && Cache[i].Size == Size && !memcmp(&Cache[i].Id, Id, sizeof(*Id)))
        {
            Slot = i;
            break;
        }
        if (!Cache[i].Bitmap || Cache[i].Stamp < Cache[Slot].Stamp)
            Slot = i;
        if (!Cache[i].Bitmap)
            break;
    }
    if (Cache[Slot].Bitmap)
        DeleteObject(Cache[Slot].Bitmap);
    Cache[Slot].Id = *Id;
    Cache[Slot].Size = Size;
    Cache[Slot].Bitmap = Copy;
    Cache[Slot].Alpha = Alpha;
    Cache[Slot].Stamp = ++CacheClock;
    LeaveCriticalSection(&CacheLock);
}

static inline THUMBNAIL_CACHE *
impl_from_IThumbnailCache(IThumbnailCache *iface)
{
    return CONTAINING_RECORD(iface, THUMBNAIL_CACHE, IThumbnailCache_iface);
}

static HRESULT WINAPI
ThumbnailCache_QueryInterface(IThumbnailCache *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IThumbnailCache))
    {
        *ppv = iface;
        IThumbnailCache_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
ThumbnailCache_AddRef(IThumbnailCache *iface)
{
    return InterlockedIncrement(&impl_from_IThumbnailCache(iface)->Ref);
}

static ULONG WINAPI
ThumbnailCache_Release(IThumbnailCache *iface)
{
    THUMBNAIL_CACHE *This = impl_from_IThumbnailCache(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&ModuleObjects);
    }
    return Ref;
}

static HRESULT WINAPI
ThumbnailCache_GetThumbnail(IThumbnailCache *iface, IShellItem *pShellItem, UINT cxyRequestedThumbSize,
                            WTS_FLAGS flags, ISharedBitmap **ppvThumb, WTS_CACHEFLAGS *pOutFlags,
                            WTS_THUMBNAILID *pThumbnailID)
{
    IThumbnailProvider *Provider = NULL;
    WTS_ALPHATYPE Alpha = WTSAT_UNKNOWN;
    WTS_THUMBNAILID Id;
    HBITMAP Bitmap = NULL;
    HRESULT hr;

    if (ppvThumb)
        *ppvThumb = NULL;
    if (pOutFlags)
        *pOutFlags = WTS_DEFAULT;
    if (!pShellItem || !cxyRequestedThumbSize)
        return E_INVALIDARG;

    hr = ItemThumbnailId(pShellItem, &Id);
    if (FAILED(hr))
        return hr;
    if (pThumbnailID)
        *pThumbnailID = Id;

    if (!(flags & WTS_FORCEEXTRACTION))
    {
        hr = CacheLookup(&Id, cxyRequestedThumbSize, ppvThumb);
        if (SUCCEEDED(hr))
        {
            if (pOutFlags)
                *pOutFlags = WTS_CACHED;
            return hr;
        }
    }
    if (flags & WTS_INCACHEONLY)
        return STG_E_FILENOTFOUND;

    hr = IShellItem_BindToHandler(pShellItem, NULL, &BHID_ThumbnailHandler, &IID_IThumbnailProvider, (void **)&Provider);
    if (SUCCEEDED(hr))
    {
        hr = IThumbnailProvider_GetThumbnail(Provider, cxyRequestedThumbSize, &Bitmap, &Alpha);
        IThumbnailProvider_Release(Provider);
    }
    if (FAILED(hr) || !Bitmap)
    {
        if (Bitmap)
            DeleteObject(Bitmap);
        return WTS_E_FAILEDEXTRACTION;
    }

    if (!(flags & WTS_EXTRACTDONOTCACHE))
    {
        CacheStore(&Id, cxyRequestedThumbSize, Bitmap, Alpha);
        if (pOutFlags)
            *pOutFlags = WTS_CACHED;
    }
    if (!ppvThumb)
    {
        DeleteObject(Bitmap);
        return S_OK;
    }
    hr = SharedBitmap_Construct(Bitmap, Alpha, &IID_ISharedBitmap, (void **)ppvThumb);
    if (FAILED(hr))
        DeleteObject(Bitmap);
    return hr;
}

static HRESULT WINAPI
ThumbnailCache_GetThumbnailByID(IThumbnailCache *iface, WTS_THUMBNAILID thumbnailID, UINT cxyRequestedThumbSize,
                                ISharedBitmap **ppvThumb, WTS_CACHEFLAGS *pOutFlags)
{
    HRESULT hr;

    if (ppvThumb)
        *ppvThumb = NULL;
    if (pOutFlags)
        *pOutFlags = WTS_DEFAULT;
    hr = CacheLookup(&thumbnailID, cxyRequestedThumbSize, ppvThumb);
    if (SUCCEEDED(hr) && pOutFlags)
        *pOutFlags = WTS_CACHED;
    return hr;
}

static const IThumbnailCacheVtbl ThumbnailCacheVtbl =
{
    ThumbnailCache_QueryInterface,
    ThumbnailCache_AddRef,
    ThumbnailCache_Release,
    ThumbnailCache_GetThumbnail,
    ThumbnailCache_GetThumbnailByID,
};

static HRESULT
ThumbnailCache_Create(REFIID riid, void **ppv)
{
    THUMBNAIL_CACHE *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    HRESULT hr;

    if (!This)
        return E_OUTOFMEMORY;
    This->IThumbnailCache_iface.lpVtbl = &ThumbnailCacheVtbl;
    This->Ref = 1;
    InterlockedIncrement(&ModuleObjects);
    hr = IThumbnailCache_QueryInterface(&This->IThumbnailCache_iface, riid, ppv);
    IThumbnailCache_Release(&This->IThumbnailCache_iface);
    return hr;
}

static HRESULT
SharedBitmap_Create(REFIID riid, void **ppv)
{
    return SharedBitmap_Construct(NULL, WTSAT_UNKNOWN, riid, ppv);
}

typedef HRESULT (*CREATE_INSTANCE)(REFIID riid, void **ppv);

typedef struct _CLASS_FACTORY
{
    IClassFactory IClassFactory_iface;
    CREATE_INSTANCE Create;
} CLASS_FACTORY;

static inline CLASS_FACTORY *
impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, CLASS_FACTORY, IClassFactory_iface);
}

static HRESULT WINAPI
Factory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Factory_AddRef(IClassFactory *iface)
{
    InterlockedIncrement(&ModuleLocks);
    return 2;
}

static ULONG WINAPI
Factory_Release(IClassFactory *iface)
{
    InterlockedDecrement(&ModuleLocks);
    return 1;
}

static HRESULT WINAPI
Factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return impl_from_IClassFactory(iface)->Create(riid, ppv);
}

static HRESULT WINAPI
Factory_LockServer(IClassFactory *iface, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&ModuleLocks);
    else
        InterlockedDecrement(&ModuleLocks);
    return S_OK;
}

static const IClassFactoryVtbl ClassFactoryVtbl =
{
    Factory_QueryInterface,
    Factory_AddRef,
    Factory_Release,
    Factory_CreateInstance,
    Factory_LockServer,
};

static CLASS_FACTORY ThumbnailCacheFactory = { { &ClassFactoryVtbl }, ThumbnailCache_Create };
static CLASS_FACTORY SharedBitmapFactory = { { &ClassFactoryVtbl }, SharedBitmap_Create };

HRESULT WINAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualCLSID(rclsid, &CLSID_LocalThumbnailCache))
        return IClassFactory_QueryInterface(&ThumbnailCacheFactory.IClassFactory_iface, riid, ppv);
    if (IsEqualCLSID(rclsid, &CLSID_SharedBitmap))
        return IClassFactory_QueryInterface(&SharedBitmapFactory.IClassFactory_iface, riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI
DllCanUnloadNow(void)
{
    return (ModuleObjects || ModuleLocks) ? S_FALSE : S_OK;
}

BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UINT i;

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(Instance);
            InitializeCriticalSection(&CacheLock);
            break;
        case DLL_PROCESS_DETACH:
            if (Reserved)
                break;
            for (i = 0; i < CACHE_ENTRIES; i++)
            {
                if (Cache[i].Bitmap)
                    DeleteObject(Cache[i].Bitmap);
            }
            DeleteCriticalSection(&CacheLock);
            break;
    }
    return TRUE;
}
