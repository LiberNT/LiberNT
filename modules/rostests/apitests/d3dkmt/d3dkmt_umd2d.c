/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end softgpu UMD linear 2D execution
 */

#include "precomp.h"
#include <winuser.h>
#include <d3dumddi.h>

#define UMD2D_WIDTH  64u
#define UMD2D_HEIGHT 64u
#define UMDFLIP_HOLD_MS 4000u

typedef HRESULT (WINAPI *PFN_D3DUmdRtCreateDeviceCallbacks)(
    D3DKMT_HANDLE,
    D3DKMT_HANDLE,
    D3DDDI_DEVICECALLBACKS *,
    HANDLE *);
typedef HRESULT (WINAPI *PFN_D3DUmdRtDestroyDeviceCallbacks)(HANDLE);

static BOOL
Umd2DQueryDriverName(
    PFND3DKMT_QUERYADAPTERINFO pQueryAdapterInfo,
    D3DKMT_HANDLE hAdapter,
    WCHAR *Name,
    SIZE_T NameChars)
{
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_UMDFILENAMEINFO Info;
    NTSTATUS Status;

    memset(&Info, 0, sizeof(Info));
    Info.Version = KMTUMDVERSION_DX9;
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Info;
    Query.PrivateDriverDataSize = sizeof(Info);

    Status = pQueryAdapterInfo(&Query);
    if (!NT_SUCCESS(Status) ||
        Info.UmdFileName[0] == UNICODE_NULL)
    {
        return FALSE;
    }
    wcsncpy(Name, Info.UmdFileName, NameChars - 1);
    Name[NameChars - 1] = UNICODE_NULL;
    return TRUE;
}

static BOOL
Umd2DIsSoftGpu(
    CONST WCHAR *Name)
{
    CONST WCHAR *Base;
    CONST WCHAR *Cursor;

    Base = Name;
    for (Cursor = Name; Cursor != NULL && *Cursor != UNICODE_NULL;
         ++Cursor)
    {
        if (*Cursor == L'\\' || *Cursor == L'/')
            Base = Cursor + 1;
    }
    return lstrcmpiW(Base, L"softgpuum.dll") == 0;
}

static HRESULT
Umd2DCreateResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hRuntimeResource,
    D3DDDI_RESOURCEFLAGS Flags,
    UINT Width,
    UINT Height,
    HANDLE *hResource)
{
    D3DDDI_SURFACEINFO Surface;
    D3DDDIARG_CREATERESOURCE Create;
    HRESULT Result;

    if (Funcs == NULL || Funcs->pfnCreateResource == NULL ||
        hResource == NULL)
    {
        return E_INVALIDARG;
    }

    *hResource = NULL;
    memset(&Surface, 0, sizeof(Surface));
    Surface.Width = Width;
    Surface.Height = Height;
    Surface.Depth = 1;

    memset(&Create, 0, sizeof(Create));
    Create.Format = D3DDDIFMT_A8R8G8B8;
    Create.Pool = D3DDDIPOOL_VIDEOMEMORY;
    Create.MultisampleType = D3DDDIMULTISAMPLE_NONE;
    Create.pSurfList = &Surface;
    Create.SurfCount = 1;
    Create.MipLevels = 1;
    Create.hResource = hRuntimeResource;
    Create.Flags = Flags;
    Create.Rotation = D3DDDI_ROTATION_IDENTITY;

    Result = Funcs->pfnCreateResource(hDevice, &Create);
    if (SUCCEEDED(Result))
        *hResource = Create.hResource;
    return Result;
}

static HRESULT
Umd2DLockResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hResource,
    BOOL ReadOnly,
    D3DDDIARG_LOCK *Lock)
{
    memset(Lock, 0, sizeof(*Lock));
    Lock->hResource = hResource;
    Lock->Flags.ReadOnly = ReadOnly ? 1 : 0;
    return Funcs->pfnLock(hDevice, Lock);
}

static HRESULT
Umd2DUnlockResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hResource)
{
    D3DDDIARG_UNLOCK Unlock;

    memset(&Unlock, 0, sizeof(Unlock));
    Unlock.hResource = hResource;
    return Funcs->pfnUnlock(hDevice, &Unlock);
}

static BOOL
Umd2DCheckPixels(
    CONST D3DDDIARG_LOCK *Lock,
    UINT Expected)
{
    UINT X;
    UINT Y;

    if (Lock == NULL || Lock->pSurfData == NULL ||
        Lock->Pitch < UMD2D_WIDTH * sizeof(UINT))
    {
        return FALSE;
    }

    for (Y = 0; Y < UMD2D_HEIGHT; ++Y)
    {
        CONST UINT *Row =
            (CONST UINT *)((CONST BYTE *)Lock->pSurfData +
                           (SIZE_T)Y * Lock->Pitch);
        for (X = 0; X < UMD2D_WIDTH; ++X)
        {
            if (Row[X] != Expected)
                return FALSE;
        }
    }
    return TRUE;
}

static void
Umd2DWritePixels(
    CONST D3DDDIARG_LOCK *Lock,
    UINT Value)
{
    UINT X;
    UINT Y;

    for (Y = 0; Y < UMD2D_HEIGHT; ++Y)
    {
        UINT *Row = (UINT *)((BYTE *)Lock->pSurfData +
                             (SIZE_T)Y * Lock->Pitch);
        for (X = 0; X < UMD2D_WIDTH; ++X)
            Row[X] = Value;
    }
}

static NTSTATUS
Umd2DWaitForIdle(
    PFND3DKMT_WAITFORIDLE pWaitForIdle,
    D3DKMT_HANDLE hDevice)
{
    D3DKMT_WAITFORIDLE Wait;

    memset(&Wait, 0, sizeof(Wait));
    Wait.hDevice = hDevice;
    return pWaitForIdle(&Wait);
}

typedef struct _UMD2D_SESSION
{
    PFND3DKMT_WAITFORIDLE pWaitForIdle;
    PFN_D3DUmdRtDestroyDeviceCallbacks pDestroyCallbacks;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hKmtDevice;
    HMODULE Runtime;
    HMODULE Umd;
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
    D3DDDI_ADAPTERFUNCS AdapterFuncs;
    D3DDDI_DEVICEFUNCS DeviceFuncs;
    HANDLE hRuntimeDevice;
    HANDLE hUmdAdapter;
    HANDLE hUmdDevice;
} UMD2D_SESSION, *PUMD2D_SESSION;

static BOOL
Umd2DOpenSession(
    PUMD2D_SESSION Session)
{
    PFND3DKMT_QUERYADAPTERINFO pQueryAdapterInfo;
    PFN_D3DUmdRtCreateDeviceCallbacks pCreateCallbacks;
    PFND3DDDI_OPENADAPTER pOpenAdapter;
    WCHAR UmdName[MAX_PATH];
    D3DDDIARG_OPENADAPTER Open;
    D3DDDIARG_CREATEDEVICE CreateDevice;
    FORMATOP Formats[2];
    D3DDDIARG_GETCAPS Caps;
    UINT FormatCount = 0;
    HRESULT Result;
    UINT Index;

    memset(Session, 0, sizeof(*Session));
    pQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)
        LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    Session->pWaitForIdle = (PFND3DKMT_WAITFORIDLE)
        LoadD3DKMTProc("D3DKMTWaitForIdle");
    if (pQueryAdapterInfo == NULL || Session->pWaitForIdle == NULL)
    {
        skip("required D3DKMT query/idle exports are absent\n");
        return FALSE;
    }

    Session->hAdapter = OpenAdapterFromDisplay1();
    if (Session->hAdapter == 0)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return FALSE;
    }
    if (!Umd2DQueryDriverName(pQueryAdapterInfo,
                              Session->hAdapter,
                              UmdName,
                              ARRAYSIZE(UmdName)))
    {
        skip("adapter reports no DX9 user-mode driver\n");
        return FALSE;
    }
    if (!Umd2DIsSoftGpu(UmdName))
    {
        skip("adapter uses %S, not softgpuum.dll\n", UmdName);
        return FALSE;
    }

    Session->hKmtDevice = CreateTestDevice(Session->hAdapter);
    if (Session->hKmtDevice == 0)
    {
        skip("could not create the KMT device\n");
        return FALSE;
    }

    Session->Runtime = LoadLibraryW(L"d3dumdrt.dll");
    if (Session->Runtime == NULL)
    {
        skip("d3dumdrt.dll is unavailable\n");
        return FALSE;
    }
    pCreateCallbacks = (PFN_D3DUmdRtCreateDeviceCallbacks)
        GetProcAddress(Session->Runtime, "D3DUmdRtCreateDeviceCallbacks");
    Session->pDestroyCallbacks = (PFN_D3DUmdRtDestroyDeviceCallbacks)
        GetProcAddress(Session->Runtime, "D3DUmdRtDestroyDeviceCallbacks");
    if (pCreateCallbacks == NULL || Session->pDestroyCallbacks == NULL)
    {
        skip("d3dumdrt.dll lacks its device callback exports\n");
        return FALSE;
    }

    Result = pCreateCallbacks(Session->hAdapter,
                              Session->hKmtDevice,
                              &Session->Callbacks,
                              &Session->hRuntimeDevice);
    ok(Result == S_OK,
       "runtime callback device creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        return FALSE;

    Session->Umd = LoadLibraryW(UmdName);
    ok(Session->Umd != NULL,
       "LoadLibrary(%S) failed, error %lu\n",
       UmdName,
       GetLastError());
    if (Session->Umd == NULL)
        return FALSE;
    pOpenAdapter = (PFND3DDDI_OPENADAPTER)
        GetProcAddress(Session->Umd, "OpenAdapter");
    ok(pOpenAdapter != NULL,
       "softgpuum.dll exports no OpenAdapter\n");
    if (pOpenAdapter == NULL)
        return FALSE;

    memset(&Open, 0, sizeof(Open));
    Open.hAdapter = (HANDLE)(ULONG_PTR)Session->hAdapter;
    Open.Interface = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.Version = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.pAdapterCallbacks = &Session->AdapterCallbacks;
    Open.pAdapterFuncs = &Session->AdapterFuncs;
    Result = pOpenAdapter(&Open);
    ok(Result == S_OK,
       "OpenAdapter failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        return FALSE;
    Session->hUmdAdapter = Open.hAdapter;

    if (Open.DriverVersion !=
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION)
    {
        skip("UMD interface 0x%04X does not match image interface "
             "0x%04X\n",
             (unsigned)Open.DriverVersion,
             (unsigned)REACTOS_EXPECTED_UMD_INTERFACE_VERSION);
        return FALSE;
    }

    memset(&Caps, 0, sizeof(Caps));
    Caps.Type = D3DDDICAPS_GETFORMATCOUNT;
    Caps.pData = &FormatCount;
    Caps.DataSize = sizeof(FormatCount);
    Result = Session->AdapterFuncs.pfnGetCaps(Session->hUmdAdapter, &Caps);
    ok(Result == S_OK && FormatCount == ARRAYSIZE(Formats),
       "format count returned hr 0x%08lX, count %u\n",
       (long)Result,
       FormatCount);

    memset(Formats, 0, sizeof(Formats));
    Caps.Type = D3DDDICAPS_GETFORMATDATA;
    Caps.pData = Formats;
    Caps.DataSize = sizeof(Formats);
    Result = Session->AdapterFuncs.pfnGetCaps(Session->hUmdAdapter, &Caps);
    ok(Result == S_OK,
       "format enumeration failed 0x%08lX\n",
       (long)Result);
    for (Index = 0; Index < ARRAYSIZE(Formats); ++Index)
    {
        ok((Formats[Index].Operations & FORMATOP_PIXELSIZE) != 0,
           "format %u is not pixel-addressable\n",
           Index);
        ok((Formats[Index].Operations &
            FORMATOP_OFFSCREENPLAIN) != 0,
           "format %u is not an offscreen 2D surface\n",
           Index);
        ok((Formats[Index].Operations &
            FORMATOP_3DACCELERATION) == 0,
           "format %u falsely advertises 3D acceleration\n",
           Index);
    }

    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hDevice = Session->hRuntimeDevice;
    CreateDevice.Interface =
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    CreateDevice.Version =
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    CreateDevice.pCallbacks = &Session->Callbacks;
    CreateDevice.pDeviceFuncs = &Session->DeviceFuncs;
    Result = Session->AdapterFuncs.pfnCreateDevice(
                 Session->hUmdAdapter,
                 &CreateDevice);
    ok(Result == S_OK,
       "UMD device creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        return FALSE;
    Session->hUmdDevice = CreateDevice.hDevice;

    ok(Session->DeviceFuncs.pfnCreateResource != NULL,
       "no pfnCreateResource\n");
    ok(Session->DeviceFuncs.pfnDestroyResource != NULL,
       "no pfnDestroyResource\n");
    ok(Session->DeviceFuncs.pfnLock != NULL && Session->DeviceFuncs.pfnUnlock != NULL,
       "no lock/unlock pair\n");
    ok(Session->DeviceFuncs.pfnColorFill != NULL,
       "no pfnColorFill\n");
    ok(Session->DeviceFuncs.pfnBlt != NULL,
       "no pfnBlt\n");
    ok(Session->DeviceFuncs.pfnSetDisplayMode != NULL,
       "no pfnSetDisplayMode\n");
    ok(Session->DeviceFuncs.pfnPresent != NULL,
       "no pfnPresent\n");
    return Session->DeviceFuncs.pfnCreateResource != NULL &&
           Session->DeviceFuncs.pfnDestroyResource != NULL &&
           Session->DeviceFuncs.pfnLock != NULL &&
           Session->DeviceFuncs.pfnUnlock != NULL &&
           Session->DeviceFuncs.pfnColorFill != NULL &&
           Session->DeviceFuncs.pfnBlt != NULL;
}

static void
Umd2DDestroyResource(
    PUMD2D_SESSION Session,
    HANDLE *hResource,
    CONST CHAR *Name)
{
    HRESULT Result;

    if (*hResource == NULL || Session->hUmdDevice == NULL ||
        Session->DeviceFuncs.pfnDestroyResource == NULL)
    {
        return;
    }
    Result = Session->DeviceFuncs.pfnDestroyResource(Session->hUmdDevice,
                                                     *hResource);
    ok(Result == S_OK,
       "%s destruction failed 0x%08lX\n",
       Name,
       (long)Result);
    *hResource = NULL;
}

static void
Umd2DCloseSession(
    PUMD2D_SESSION Session)
{
    HRESULT Result;

    if (Session->hUmdDevice != NULL &&
        Session->DeviceFuncs.pfnDestroyDevice != NULL)
    {
        Result = Session->DeviceFuncs.pfnDestroyDevice(Session->hUmdDevice);
        ok(Result == S_OK,
           "UMD device destruction failed 0x%08lX\n",
           (long)Result);
    }
    Session->hUmdDevice = NULL;
    if (Session->hUmdAdapter != NULL &&
        Session->AdapterFuncs.pfnCloseAdapter != NULL)
    {
        Result = Session->AdapterFuncs.pfnCloseAdapter(Session->hUmdAdapter);
        ok(Result == S_OK,
           "UMD adapter close failed 0x%08lX\n",
           (long)Result);
        Session->hUmdAdapter = NULL;
    }
    if (Session->hRuntimeDevice != NULL && Session->pDestroyCallbacks != NULL)
    {
        Result = Session->pDestroyCallbacks(Session->hRuntimeDevice);
        ok(Result == S_OK,
           "runtime callback device destruction failed 0x%08lX\n",
           (long)Result);
        Session->hRuntimeDevice = NULL;
    }
    if (Session->Umd != NULL)
        FreeLibrary(Session->Umd);
    if (Session->Runtime != NULL)
        FreeLibrary(Session->Runtime);
    if (Session->hKmtDevice != 0)
        DestroyTestDevice(Session->hKmtDevice);
    if (Session->hAdapter != 0)
        CloseAdapter(Session->hAdapter);
    memset(Session, 0, sizeof(*Session));
}

static void
Test_SoftGpu2DEndToEnd(void)
{
    UMD2D_SESSION Session;
    CONST D3DDDI_DEVICEFUNCS *Funcs = &Session.DeviceFuncs;
    HANDLE hSource = NULL;
    HANDLE hDestination = NULL;
    ULONG_PTR SourceRuntimeCookie = 0x534F5552;
    ULONG_PTR DestinationRuntimeCookie = 0x44455354;
    D3DDDI_RESOURCEFLAGS ResourceFlags;
    D3DDDIARG_LOCK Lock;
    D3DDDIARG_COLORFILL Fill;
    D3DDDIARG_BLT Blt;
    UINT SourceColor = 0xFF1248A0;
    UINT FillColor = 0xFF70B030;
    BOOL SourceLocked = FALSE;
    BOOL DestinationLocked = FALSE;
    HRESULT Result;
    NTSTATUS Status;

    if (!Umd2DOpenSession(&Session))
        goto Cleanup;

    memset(&ResourceFlags, 0, sizeof(ResourceFlags));
    Result = Umd2DCreateResource(
                 Funcs,
                 Session.hUmdDevice,
                 (HANDLE)&SourceRuntimeCookie,
                 ResourceFlags,
                 UMD2D_WIDTH,
                 UMD2D_HEIGHT,
                 &hSource);
    ok(Result == S_OK && hSource != NULL,
       "source resource creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || hSource == NULL)
        goto Cleanup;

    Result = Umd2DCreateResource(
                 Funcs,
                 Session.hUmdDevice,
                 (HANDLE)&DestinationRuntimeCookie,
                 ResourceFlags,
                 UMD2D_WIDTH,
                 UMD2D_HEIGHT,
                 &hDestination);
    ok(Result == S_OK && hDestination != NULL,
       "destination resource creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || hDestination == NULL)
        goto Cleanup;

    Result = Umd2DLockResource(Funcs,
                               Session.hUmdDevice,
                               hSource,
                               FALSE,
                               &Lock);
    ok(Result == S_OK && Lock.pSurfData != NULL,
       "source lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || Lock.pSurfData == NULL)
        goto Cleanup;
    SourceLocked = TRUE;
    Umd2DWritePixels(&Lock, SourceColor);
    Result = Umd2DUnlockResource(Funcs,
                                 Session.hUmdDevice,
                                 hSource);
    ok(Result == S_OK,
       "source unlock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    SourceLocked = FALSE;

    memset(&Fill, 0, sizeof(Fill));
    Fill.hResource = hDestination;
    Fill.DstRect.right = UMD2D_WIDTH;
    Fill.DstRect.bottom = UMD2D_HEIGHT;
    Fill.Color = FillColor;
    Result = Funcs->pfnColorFill(Session.hUmdDevice, &Fill);
    ok(Result == S_OK,
       "GPU color fill failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    Status = Umd2DWaitForIdle(Session.pWaitForIdle, Session.hKmtDevice);
    ok(NT_SUCCESS(Status),
       "wait after color fill failed 0x%08lX\n",
       (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Result = Umd2DLockResource(Funcs,
                               Session.hUmdDevice,
                               hDestination,
                               TRUE,
                               &Lock);
    ok(Result == S_OK,
       "destination readback lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = TRUE;
    ok(Umd2DCheckPixels(&Lock, FillColor),
       "GPU color fill did not reach allocation memory\n");
    Result = Umd2DUnlockResource(Funcs,
                                 Session.hUmdDevice,
                                 hDestination);
    ok(Result == S_OK,
       "destination readback unlock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = FALSE;

    memset(&Blt, 0, sizeof(Blt));
    Blt.hSrcResource = hSource;
    Blt.SrcRect.right = UMD2D_WIDTH;
    Blt.SrcRect.bottom = UMD2D_HEIGHT;
    Blt.hDstResource = hDestination;
    Blt.DstRect.right = UMD2D_WIDTH;
    Blt.DstRect.bottom = UMD2D_HEIGHT;
    Result = Funcs->pfnBlt(Session.hUmdDevice, &Blt);
    ok(Result == S_OK,
       "GPU blit failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    Status = Umd2DWaitForIdle(Session.pWaitForIdle, Session.hKmtDevice);
    ok(NT_SUCCESS(Status),
       "wait after blit failed 0x%08lX\n",
       (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Result = Umd2DLockResource(Funcs,
                               Session.hUmdDevice,
                               hDestination,
                               TRUE,
                               &Lock);
    ok(Result == S_OK,
       "blit readback lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = TRUE;
    ok(Umd2DCheckPixels(&Lock, SourceColor),
       "GPU blit did not copy source pixels to destination\n");
    Result = Umd2DUnlockResource(Funcs,
                                 Session.hUmdDevice,
                                 hDestination);
    ok(Result == S_OK,
       "blit readback unlock failed 0x%08lX\n",
       (long)Result);
    if (SUCCEEDED(Result))
        DestinationLocked = FALSE;

Cleanup:
    if (Session.hUmdDevice != NULL && Funcs->pfnUnlock != NULL)
    {
        if (DestinationLocked && hDestination != NULL)
            (VOID)Umd2DUnlockResource(Funcs, Session.hUmdDevice, hDestination);
        if (SourceLocked && hSource != NULL)
            (VOID)Umd2DUnlockResource(Funcs, Session.hUmdDevice, hSource);
    }
    Umd2DDestroyResource(&Session, &hDestination, "destination");
    Umd2DDestroyResource(&Session, &hSource, "source");
    Umd2DCloseSession(&Session);
}

static BOOL
Umd2DCreateFilledPrimary(
    PUMD2D_SESSION Session,
    ULONG_PTR *RuntimeCookie,
    UINT Width,
    UINT Height,
    UINT Color,
    HANDLE *hResource)
{
    D3DDDI_RESOURCEFLAGS Flags;
    D3DDDIARG_COLORFILL Fill;
    HRESULT Result;
    NTSTATUS Status;

    memset(&Flags, 0, sizeof(Flags));
    Flags.Primary = 1;
    Result = Umd2DCreateResource(&Session->DeviceFuncs,
                                 Session->hUmdDevice,
                                 (HANDLE)RuntimeCookie,
                                 Flags,
                                 Width,
                                 Height,
                                 hResource);
    ok(Result == S_OK && *hResource != NULL,
       "%ux%u primary creation failed 0x%08lX\n",
       Width,
       Height,
       (long)Result);
    if (FAILED(Result) || *hResource == NULL)
        return FALSE;

    memset(&Fill, 0, sizeof(Fill));
    Fill.hResource = *hResource;
    Fill.DstRect.right = (LONG)Width;
    Fill.DstRect.bottom = (LONG)Height;
    Fill.Color = Color;
    Result = Session->DeviceFuncs.pfnColorFill(Session->hUmdDevice, &Fill);
    ok(Result == S_OK,
       "primary color fill failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        return FALSE;
    Status = Umd2DWaitForIdle(Session->pWaitForIdle, Session->hKmtDevice);
    ok(NT_SUCCESS(Status),
       "wait after primary fill failed 0x%08lX\n",
       (long)Status);
    return NT_SUCCESS(Status);
}

static BOOL
Umd2DPrimaryHolds(
    PUMD2D_SESSION Session,
    HANDLE hResource,
    UINT Color)
{
    D3DDDIARG_LOCK Lock;
    HRESULT Result;
    BOOL Holds;

    Result = Umd2DLockResource(&Session->DeviceFuncs,
                               Session->hUmdDevice,
                               hResource,
                               TRUE,
                               &Lock);
    ok(Result == S_OK,
       "primary readback lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        return FALSE;
    Holds = Umd2DCheckPixels(&Lock, Color);
    (VOID)Umd2DUnlockResource(&Session->DeviceFuncs,
                              Session->hUmdDevice,
                              hResource);
    return Holds;
}

static HRESULT
Umd2DFlip(
    PUMD2D_SESSION Session,
    HANDLE hResource)
{
    D3DDDIARG_PRESENT Present;
    HRESULT Result;

    memset(&Present, 0, sizeof(Present));
    Present.hSrcResource = hResource;
    Present.Flags.Flip = 1;
    Present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Result = Session->DeviceFuncs.pfnPresent(Session->hUmdDevice, &Present);
    if (SUCCEEDED(Result))
        (VOID)Umd2DWaitForIdle(Session->pWaitForIdle, Session->hKmtDevice);
    return Result;
}

static NTSTATUS
Umd2DSetSourceOwner(
    PFND3DKMT_SETVIDPNSOURCEOWNER pSetOwner,
    D3DKMT_HANDLE hDevice,
    BOOL Exclusive)
{
    D3DKMT_SETVIDPNSOURCEOWNER Owner;
    D3DKMT_VIDPNSOURCEOWNER_TYPE Type = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0;

    memset(&Owner, 0, sizeof(Owner));
    Owner.hDevice = hDevice;
    if (Exclusive)
    {
        Owner.pType = &Type;
        Owner.pVidPnSourceId = &SourceId;
        Owner.VidPnSourceCount = 1;
    }
    return pSetOwner(&Owner);
}

static void
Test_SoftGpuFlip(void)
{
    UMD2D_SESSION Session;
    PFND3DKMT_SETVIDPNSOURCEOWNER pSetOwner;
    D3DDDIARG_SETDISPLAYMODE SetMode;
    DEVMODEW Mode;
    HANDLE hRed = NULL;
    HANDLE hGreen = NULL;
    ULONG_PTR RedRuntimeCookie = 0x52454430;
    ULONG_PTR GreenRuntimeCookie = 0x47524E30;
    UINT Red = 0xFFC00000;
    UINT Green = 0xFF00A000;
    BOOL Owned = FALSE;
    HRESULT Result;
    NTSTATUS Status;

    pSetOwner = (PFND3DKMT_SETVIDPNSOURCEOWNER)
        LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner");
    if (pSetOwner == NULL)
    {
        skip("D3DKMTSetVidPnSourceOwner is absent\n");
        return;
    }
    if (!Umd2DOpenSession(&Session))
        goto Cleanup;
    if (Session.DeviceFuncs.pfnSetDisplayMode == NULL ||
        Session.DeviceFuncs.pfnPresent == NULL)
    {
        goto Cleanup;
    }

    memset(&Mode, 0, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Mode) ||
        Mode.dmPelsWidth == 0 || Mode.dmPelsHeight == 0)
    {
        skip("current display mode is unknown\n");
        goto Cleanup;
    }

    if (!Umd2DCreateFilledPrimary(&Session, &RedRuntimeCookie, Mode.dmPelsWidth, Mode.dmPelsHeight, Red, &hRed) ||
        !Umd2DCreateFilledPrimary(&Session, &GreenRuntimeCookie, Mode.dmPelsWidth, Mode.dmPelsHeight, Green, &hGreen))
    {
        goto Cleanup;
    }

    Status = Umd2DSetSourceOwner(pSetOwner, Session.hKmtDevice, TRUE);
    ok(NT_SUCCESS(Status),
       "exclusive source ownership failed 0x%08lX\n",
       (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Owned = TRUE;

    memset(&SetMode, 0, sizeof(SetMode));
    SetMode.hResource = hRed;
    Result = Session.DeviceFuncs.pfnSetDisplayMode(Session.hUmdDevice, &SetMode);
    ok(Result == S_OK,
       "SetDisplayMode to the red primary failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    trace("scanning out red %lux%lu\n", Mode.dmPelsWidth, Mode.dmPelsHeight);
    Sleep(UMDFLIP_HOLD_MS);

    Result = Umd2DFlip(&Session, hGreen);
    ok(Result == S_OK,
       "flip to the green primary failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    trace("flipped to green\n");
    Sleep(UMDFLIP_HOLD_MS);

    Result = Umd2DFlip(&Session, hRed);
    ok(Result == S_OK,
       "flip back to the red primary failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    trace("flipped to red\n");
    Sleep(UMDFLIP_HOLD_MS);

    ok(Umd2DPrimaryHolds(&Session, hRed, Red),
       "red primary changed while it was scanned out\n");
    ok(Umd2DPrimaryHolds(&Session, hGreen, Green),
       "green primary changed after it was flipped away\n");

Cleanup:
    if (Owned)
    {
        Status = Umd2DSetSourceOwner(pSetOwner, Session.hKmtDevice, FALSE);
        ok(NT_SUCCESS(Status),
           "releasing source ownership failed 0x%08lX\n",
           (long)Status);
        trace("released source ownership\n");
        Sleep(UMDFLIP_HOLD_MS);
    }
    Umd2DDestroyResource(&Session, &hGreen, "green primary");
    Umd2DDestroyResource(&Session, &hRed, "red primary");
    Umd2DCloseSession(&Session);
}

START_TEST(umd2d)
{
    Test_SoftGpu2DEndToEnd();
}

START_TEST(umdflip)
{
    Test_SoftGpuFlip();
}

/* EOF */
