/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU readback of GPU surface publications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string.h>
#include <reactos/dwmframe.h>

#include "dxsurface.h"
#include "dxformat.h"

#define DWM_READBACK_MAX_DEVICES 8
#define DWM_READBACK_MAX_DIMENSION 16384u

typedef HRESULT (WINAPI *CreateFactoryProc)(REFIID, void **);
typedef HRESULT (WINAPI *CreateDeviceProc)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

struct ReadbackDevice
{
    LUID Luid;
    BOOL Used;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
};

struct ReadbackSurface
{
    ULONG Share;
    ULONG Generation;
    ULONGLONG UpdateId;
    ReadbackDevice *Owner;
    ID3D11Texture2D *Opened;
    ID3D11Texture2D *Staging;
    BYTE *Pixels;
    ULONG Width;
    ULONG Height;
    ULONG Format;
};

static ReadbackDevice g_ReadbackDevices[DWM_READBACK_MAX_DEVICES];
static ReadbackSurface g_ReadbackSurfaces[DWM_MAX_WINDOWS];
static HMODULE g_ReadbackRuntime;
static HMODULE g_ReadbackDxgi;

template <class T>
static void
ReleaseObject(T *&Object)
{
    if (Object != NULL)
    {
        Object->Release();
        Object = NULL;
    }
}

static BOOL
SameLuid(const LUID &Left, const LUID &Right)
{
    return Left.LowPart == Right.LowPart && Left.HighPart == Right.HighPart;
}

static void
CreateReadbackDevice(ReadbackDevice *Slot)
{
    if (g_ReadbackRuntime == NULL)
        g_ReadbackRuntime = LoadLibraryW(L"d3d11.dll");
    if (g_ReadbackDxgi == NULL)
        g_ReadbackDxgi = LoadLibraryW(L"dxgi.dll");
    if (g_ReadbackRuntime == NULL || g_ReadbackDxgi == NULL)
        return;

    CreateFactoryProc CreateFactory = reinterpret_cast<CreateFactoryProc>(GetProcAddress(g_ReadbackDxgi, "CreateDXGIFactory1"));
    CreateDeviceProc CreateDevice = reinterpret_cast<CreateDeviceProc>(GetProcAddress(g_ReadbackRuntime, "D3D11CreateDevice"));
    IDXGIFactory1 *Factory = NULL;
    if (CreateFactory == NULL || CreateDevice == NULL ||
        FAILED(CreateFactory(IID_IDXGIFactory1, (void **)&Factory)))
        return;

    for (UINT Index = 0; Slot->Device == NULL; ++Index)
    {
        IDXGIAdapter1 *Adapter = NULL;
        DXGI_ADAPTER_DESC1 Desc;

        if (Factory->EnumAdapters1(Index, &Adapter) != S_OK)
            break;
        if (SUCCEEDED(Adapter->GetDesc1(&Desc)) && SameLuid(Desc.AdapterLuid, Slot->Luid) &&
            FAILED(CreateDevice(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                &Slot->Device, NULL, &Slot->Context)))
        {
            Slot->Device = NULL;
            Slot->Context = NULL;
            ReleaseObject(Adapter);
            break;
        }
        ReleaseObject(Adapter);
    }
    ReleaseObject(Factory);
}

static ReadbackDevice *
GetReadbackDevice(const LUID &Luid)
{
    ReadbackDevice *Free = NULL;

    for (ULONG Index = 0; Index < DWM_READBACK_MAX_DEVICES; ++Index)
    {
        ReadbackDevice *Slot = &g_ReadbackDevices[Index];

        if (Slot->Used && SameLuid(Slot->Luid, Luid))
            return Slot->Device != NULL ? Slot : NULL;
        if (!Slot->Used && Free == NULL)
            Free = Slot;
    }
    if (Free == NULL)
        return NULL;
    Free->Used = TRUE;
    Free->Luid = Luid;
    CreateReadbackDevice(Free);
    return Free->Device != NULL ? Free : NULL;
}

static void
ResetReadbackSurface(ReadbackSurface *Surface)
{
    ReleaseObject(Surface->Staging);
    ReleaseObject(Surface->Opened);
    if (Surface->Pixels != NULL)
        HeapFree(GetProcessHeap(), 0, Surface->Pixels);
    memset(Surface, 0, sizeof(*Surface));
}

static BOOL
OpenReadbackSurface(ReadbackSurface *Surface, const DWM_WIN *Window)
{
    D3D11_TEXTURE2D_DESC Desc;
    ReadbackDevice *Owner;

    ResetReadbackSurface(Surface);
    Surface->Share = Window->DxGlobalShare;
    Surface->Generation = Window->DxGeneration;
    Owner = GetReadbackDevice(Window->DxAdapterLuid);
    if (Owner == NULL ||
        FAILED(Owner->Device->OpenSharedResource((HANDLE)(ULONG_PTR)Window->DxGlobalShare,
                                                 IID_ID3D11Texture2D, (void **)&Surface->Opened)))
        return FALSE;
    Surface->Opened->GetDesc(&Desc);
    if (Desc.Width != Window->DxWidth || Desc.Height != Window->DxHeight ||
        Desc.MipLevels != 1 || Desc.ArraySize != 1 || Desc.SampleDesc.Count != 1 ||
        Desc.Format != (DXGI_FORMAT)Window->DxFormat)
        return FALSE;
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    if (FAILED(Owner->Device->CreateTexture2D(&Desc, NULL, &Surface->Staging)))
        return FALSE;
    Surface->Pixels = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Desc.Width * Desc.Height * sizeof(ULONG));
    if (Surface->Pixels == NULL)
        return FALSE;
    Surface->Owner = Owner;
    Surface->Width = Desc.Width;
    Surface->Height = Desc.Height;
    Surface->Format = Window->DxFormat;
    return TRUE;
}

static BOOL
CopyReadbackSurface(ReadbackSurface *Surface)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ID3D11DeviceContext *Context = Surface->Owner->Context;
    ULONG RowBytes = Surface->Width * sizeof(ULONG);

    Context->CopyResource(Surface->Staging, Surface->Opened);
    if (FAILED(Context->Map(Surface->Staging, 0, D3D11_MAP_READ, 0, &Mapped)))
        return FALSE;
    for (ULONG Y = 0; Y < Surface->Height; ++Y)
    {
        const BYTE *Source = (const BYTE *)Mapped.pData + (SIZE_T)Y * Mapped.RowPitch;
        BYTE *Destination = Surface->Pixels + (SIZE_T)Y * RowBytes;

        if (Surface->Format == DWM_DX_FORMAT_B8G8R8A8_UNORM)
        {
            memcpy(Destination, Source, RowBytes);
            continue;
        }
        DwmDxConvertRow(Surface->Format, Source, Destination, Surface->Width);
    }
    Context->Unmap(Surface->Staging, 0);
    return TRUE;
}

extern "C" const BYTE *
DwmDxReadbackSurface(const DWM_WIN *Window, ULONG *Pitch)
{
    ReadbackSurface *Surface;

    *Pitch = 0;
    if (Window->DxGlobalShare == 0 || Window->DxGeneration == 0 || Window->DxUpdateId == 0 ||
        Window->DxWidth == 0 || Window->DxHeight == 0 ||
        Window->DxWidth > DWM_READBACK_MAX_DIMENSION || Window->DxHeight > DWM_READBACK_MAX_DIMENSION ||
        (Window->DxFormat != DWM_DX_FORMAT_B8G8R8A8_UNORM && Window->DxFormat != DWM_DX_FORMAT_R8G8B8A8_UNORM &&
         Window->DxFormat != DWM_DX_FORMAT_R10G10B10A2_UNORM && Window->DxFormat != DWM_DX_FORMAT_R16G16B16A16_FLOAT))
        return NULL;

    Surface = &g_ReadbackSurfaces[Window->SurfaceId % DWM_MAX_WINDOWS];
    if ((Surface->Share != Window->DxGlobalShare || Surface->Generation != Window->DxGeneration) &&
        !OpenReadbackSurface(Surface, Window))
        return NULL;
    if (Surface->Staging == NULL)
        return NULL;
    if (Surface->UpdateId != Window->DxUpdateId)
    {
        if (!CopyReadbackSurface(Surface))
            return NULL;
        Surface->UpdateId = Window->DxUpdateId;
    }
    *Pitch = Surface->Width * sizeof(ULONG);
    return Surface->Pixels;
}

extern "C" BOOL
DwmDxReadbackCurrent(const DWM_WIN *Window)
{
    const ReadbackSurface *Surface = &g_ReadbackSurfaces[Window->SurfaceId % DWM_MAX_WINDOWS];

    return Surface->Staging != NULL && Window->DxUpdateId != 0 &&
           Surface->Share == Window->DxGlobalShare &&
           Surface->Generation == Window->DxGeneration &&
           Surface->UpdateId == Window->DxUpdateId;
}

extern "C" void
DwmDxReadbackCleanup(void)
{
    for (ULONG Index = 0; Index < DWM_MAX_WINDOWS; ++Index)
        ResetReadbackSurface(&g_ReadbackSurfaces[Index]);
    for (ULONG Index = 0; Index < DWM_READBACK_MAX_DEVICES; ++Index)
    {
        ReleaseObject(g_ReadbackDevices[Index].Context);
        ReleaseObject(g_ReadbackDevices[Index].Device);
        g_ReadbackDevices[Index].Used = FALSE;
    }
}
