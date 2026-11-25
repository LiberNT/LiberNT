/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMT swap chain, DOD indirect swap chain and bundle object system calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);
typedef NTSTATUS (NTAPI *PFN_QUERYOBJECT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

#define SWAP_SURFACES 4
#define SWAP_MAX_SURFACES 51

typedef struct _SWAP_BASIC_INFORMATION
{
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG Reserved[10];
} SWAP_BASIC_INFORMATION;

static D3DKMT_HANDLE SwapDevice;
static HANDLE SwapSurfaces[SWAP_MAX_SURFACES];

static NTSTATUS
SwapCall(
    _In_ PCSTR Name,
    _In_opt_ PVOID Args)
{
    CHAR Win32uName[96];
    PFN_KMT1 Function;

    lstrcpyA(Win32uName, "NtGdiDdDDI");
    lstrcatA(Win32uName, Name);
    Function = (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Win32uName);
    ok(Function != NULL, "win32u does not export %s\n", Win32uName);
    return Function != NULL ? Function(Args) : (NTSTATUS)0xDEADDEAD;
}

static ACCESS_MASK
SwapGranted(
    _In_ HANDLE Handle)
{
    PFN_QUERYOBJECT pNtQueryObject = (PFN_QUERYOBJECT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    SWAP_BASIC_INFORMATION Basic;
    ULONG Length;

    if (pNtQueryObject(Handle, 0, &Basic, sizeof(Basic), &Length) != STATUS_SUCCESS)
        return 0xFFFFFFFF;
    return Basic.GrantedAccess;
}

static BOOLEAN
SwapTypeIs(
    _In_ HANDLE Handle,
    _In_ PCWSTR Expected)
{
    PFN_QUERYOBJECT pNtQueryObject = (PFN_QUERYOBJECT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    BYTE Buffer[512];
    UNICODE_STRING *Name = (UNICODE_STRING *)Buffer;
    ULONG Length;

    if (pNtQueryObject(Handle, 2, Buffer, sizeof(Buffer), &Length) != STATUS_SUCCESS)
        return FALSE;
    return Name->Length == lstrlenW(Expected) * sizeof(WCHAR) && !memcmp(Name->Buffer, Expected, Name->Length);
}

static HANDLE
SwapShare(
    _In_ D3DKMT_HANDLE Object)
{
    PFN_KMTSHARE pShareObjects = (PFN_KMTSHARE)GetProcAddress(LoadLibraryA("win32u.dll"), "NtGdiDdDDIShareObjects");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Shared = NULL;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    if (pShareObjects == NULL || !NT_SUCCESS(pShareObjects(1, &Object, &Attributes, GENERIC_ALL, &Shared)))
        return NULL;
    return Shared;
}

static HANDLE
SwapCreateSurface(VOID)
{
    D3DKMT_CREATESTANDARDALLOCATION Standard;
    D3DDDI_ALLOCATIONINFO2 Info;
    D3DKMT_CREATEALLOCATION Create;

    ZeroMemory(&Standard, sizeof(Standard));
    ZeroMemory(&Info, sizeof(Info));
    ZeroMemory(&Create, sizeof(Create));
    Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    Standard.ExistingHeapData.Size = 0x10000;
    Info.pSystemMem = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Create.hDevice = SwapDevice;
    Create.pStandardAllocation = &Standard;
    Create.NumAllocations = 1;
    Create.pAllocationInfo2 = &Info;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Create.Flags.NtSecuritySharing = 1;
    Create.Flags.ExistingSysMem = 1;
    Create.Flags.StandardAllocation = 1;
    if (!NT_SUCCESS(SwapCall("CreateAllocation", &Create)))
    {
        /* TODO: drop the plain allocation once LiberNT creates standard existing-heap allocations */
        ZeroMemory(&Info, sizeof(Info));
        ZeroMemory(&Create, sizeof(Create));
        Create.hDevice = SwapDevice;
        Create.NumAllocations = 1;
        Create.pAllocationInfo2 = &Info;
        Create.Flags.CreateResource = 1;
        Create.Flags.CreateShared = 1;
        Create.Flags.NtSecuritySharing = 1;
        if (!NT_SUCCESS(SwapCall("CreateAllocation", &Create)))
            return NULL;
    }
    return SwapShare(Create.hResource);
}

static HANDLE
SwapCreateSyncHandle(VOID)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = SwapDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Shared = 1;
    Create.Info.Flags.NtSecuritySharing = 1;
    if (!NT_SUCCESS(SwapCall("CreateSynchronizationObject", &Create)))
        return NULL;
    return SwapShare(Create.hSyncObject);
}

static NTSTATUS
SwapCreate(
    _In_ UINT Count,
    _In_opt_ HANDLE *Surfaces,
    _In_ BOOL Producer,
    _In_ ACCESS_MASK Access,
    _In_opt_ POBJECT_ATTRIBUTES Attributes,
    _Out_ HANDLE *SwapChain)
{
    D3DKMT_CREATESWAPCHAIN Create;
    NTSTATUS Status;

    ZeroMemory(&Create, sizeof(Create));
    Create.bProducer = Producer;
    Create.hDevice = SwapDevice;
    Create.pObjectAttributes = Attributes;
    Create.DesiredAccess = Access;
    Create.SurfaceCount = Count;
    Create.pNtSurfaceHandles = Surfaces;
    Status = SwapCall("CreateSwapChain", &Create);
    *SwapChain = Create.hNtSwapChain;
    return Status;
}

static NTSTATUS
SwapMetadata(
    _In_ HANDLE SwapChain,
    _In_ BOOL Set,
    _In_ BOOL Global,
    _In_ UINT Size,
    _Inout_opt_ PVOID Buffer,
    _Out_opt_ UINT *Copied)
{
    D3DKMT_GETSETSWAPCHAINMETADATA Metadata;
    NTSTATUS Status;

    ZeroMemory(&Metadata, sizeof(Metadata));
    Metadata.hNtSwapChain = SwapChain;
    Metadata.bSetMetaData = Set;
    Metadata.bProducer = TRUE;
    Metadata.bGlobalMetaData = Global;
    Metadata.BufferSize = Size;
    Metadata.pBuffer = Buffer;
    Metadata.DataCopied = 0xEEEE;
    Status = SwapCall("GetSetSwapChainMetadata", &Metadata);
    if (Copied != NULL)
        *Copied = Metadata.DataCopied;
    return Status;
}

static NTSTATUS
SwapOpen(
    _In_opt_ HANDLE SwapChain,
    _In_opt_ POBJECT_ATTRIBUTES Attributes,
    _In_ BOOL Producer,
    _In_ ACCESS_MASK Access,
    _In_ ACCESS_MASK TextureAccess,
    _Inout_ UINT *Count,
    _Out_writes_opt_(*Count) HANDLE *Surfaces,
    _Out_opt_ HANDLE *Opened)
{
    D3DKMT_OPENSWAPCHAIN Open;
    NTSTATUS Status;

    ZeroMemory(&Open, sizeof(Open));
    Open.hNtSwapChain = SwapChain;
    Open.pObjectAttributes = Attributes;
    Open.bProducer = Producer;
    Open.hDevice = SwapDevice;
    Open.DesiredAccess = Access;
    Open.DesiredAccessTextures = TextureAccess;
    Open.SurfaceCount = *Count;
    Open.pNtSurfaceHandles = Surfaces;
    Status = SwapCall("OpenSwapChain", &Open);
    *Count = Open.SurfaceCount;
    if (Opened != NULL)
        *Opened = Open.hNtSwapChain;
    return Status;
}

static VOID
SwapTestValidation(VOID)
{
    static const PCSTR Names[] = { "CreateSwapChain", "OpenSwapChain", "AddSurfaceToSwapChain", "RemoveSurfaceFromSwapChain",
                                   "AbandonSwapChain", "UnOrderedPresentSwapChain", "AcquireSwapChain", "ReleaseSwapChain",
                                   "GetSetSwapChainMetadata", "SetDodIndirectSwapchain", "CreateBundleObject", "ExtractBundleObject" };
    ULONG_PTR Zero[16];
    NTSTATUS Status;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Names); ++Index)
    {
        Status = SwapCall(Names[Index], NULL);
        ok(Status == STATUS_INVALID_PARAMETER, "%s NULL: status 0x%08lx\n", Names[Index], Status);
        Status = SwapCall(Names[Index], (PVOID)0x10);
        ok(Status == STATUS_INVALID_PARAMETER, "%s bad pointer: status 0x%08lx\n", Names[Index], Status);
    }

    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("CreateSwapChain", Zero), STATUS_INVALID_PARAMETER);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("OpenSwapChain", Zero), STATUS_INVALID_PARAMETER);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("AbandonSwapChain", Zero), STATUS_INVALID_HANDLE);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("AcquireSwapChain", Zero), STATUS_INVALID_HANDLE);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("ReleaseSwapChain", Zero), STATUS_INVALID_HANDLE);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("GetSetSwapChainMetadata", Zero), STATUS_INVALID_HANDLE);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("SetDodIndirectSwapchain", Zero), STATUS_INVALID_PARAMETER);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("CreateBundleObject", Zero), STATUS_INVALID_PARAMETER);
    ZeroMemory(Zero, sizeof(Zero));
    ok_hex(SwapCall("ExtractBundleObject", Zero), STATUS_INVALID_HANDLE);

    ZeroMemory(Zero, sizeof(Zero));
    Zero[0] = 0x1234;
    ok_hex(SwapCall("AddSurfaceToSwapChain", Zero), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCall("RemoveSurfaceFromSwapChain", Zero), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCall("UnOrderedPresentSwapChain", Zero), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCall("AbandonSwapChain", Zero), STATUS_INVALID_HANDLE);
    ok_hex(SwapCall("ExtractBundleObject", Zero), STATUS_INVALID_HANDLE);
}

static VOID
SwapTestWrongType(
    _In_ HANDLE Sync)
{
    D3DKMT_ABANDONSWAPCHAIN Abandon = { Sync };
    D3DKMT_RELEASESWAPCHAIN Release;
    D3DKMT_ACQUIRESWAPCHAIN Acquire;
    D3DKMT_EXTRACTBUNDLEOBJECT Extract;
    HANDLE Surfaces[2] = { SwapSurfaces[0], Sync };
    HANDLE SwapChain;
    UINT Count = SWAP_SURFACES;
    HANDLE Out[SWAP_SURFACES];
    DWORD Access = GENERIC_ALL;

    ok_hex(SwapCall("AbandonSwapChain", &Abandon), STATUS_OBJECT_TYPE_MISMATCH);
    ZeroMemory(&Release, sizeof(Release));
    Release.hNtSwapChain = Sync;
    ok_hex(SwapCall("ReleaseSwapChain", &Release), STATUS_OBJECT_TYPE_MISMATCH);
    ZeroMemory(&Acquire, sizeof(Acquire));
    Acquire.hNtSwapChain = Sync;
    ok_hex(SwapCall("AcquireSwapChain", &Acquire), STATUS_OBJECT_TYPE_MISMATCH);
    ok_hex(SwapMetadata(Sync, FALSE, TRUE, 0, NULL, NULL), STATUS_OBJECT_TYPE_MISMATCH);
    ok_hex(SwapOpen(Sync, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_OBJECT_TYPE_MISMATCH);
    ZeroMemory(&Extract, sizeof(Extract));
    Extract.hNtBundleHandle = Sync;
    Extract.cObjects = 1;
    Extract.pdwDesiredAccess = &Access;
    Extract.phNtHandles = Out;
    ok_hex(SwapCall("ExtractBundleObject", &Extract), STATUS_OBJECT_TYPE_MISMATCH);
    ok_hex(SwapCreate(2, Surfaces, TRUE, GENERIC_ALL, NULL, &SwapChain), STATUS_OBJECT_TYPE_MISMATCH);
}

static VOID
SwapTestCreate(VOID)
{
    D3DKMT_CREATESWAPCHAIN Create;
    HANDLE Surfaces[2] = { SwapSurfaces[0], NULL };
    HANDLE Event = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE SwapChain;
    NTSTATUS Status;

    ok_hex(SwapCreate(0, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCreate(51, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCreate(2, NULL, TRUE, GENERIC_ALL, NULL, &SwapChain), STATUS_INVALID_PARAMETER);
    ok_hex(SwapCreate(2, Surfaces, TRUE, GENERIC_ALL, NULL, &SwapChain), STATUS_INVALID_HANDLE);

    Status = SwapCreate(50, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok(SwapTypeIs(SwapChain, L"DxgkSharedSwapChainObject"), "wrong swap chain object type\n");
        ok_hex(SwapGranted(SwapChain), STANDARD_RIGHTS_ALL);
        CloseHandle(SwapChain);
    }
    Status = SwapCreate(2, SwapSurfaces, TRUE, GENERIC_READ, NULL, &SwapChain);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok_hex(SwapGranted(SwapChain), READ_CONTROL);
        CloseHandle(SwapChain);
    }
    Status = SwapCreate(2, SwapSurfaces, FALSE, 0, NULL, &SwapChain);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok_hex(SwapGranted(SwapChain), 0);
        CloseHandle(SwapChain);
    }

    ZeroMemory(&Create, sizeof(Create));
    Create.bProducer = TRUE;
    Create.hDevice = SwapDevice;
    Create.DesiredAccess = GENERIC_ALL;
    Create.SurfaceCount = 2;
    Create.pNtSurfaceHandles = SwapSurfaces;
    Create.BufferAvailableEvent = Event;
    Status = SwapCall("CreateSwapChain", &Create);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
        CloseHandle(Create.hNtSwapChain);
    Create.BufferAvailableEvent = (HANDLE)0x1234;
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_INVALID_HANDLE);
    Create.BufferAvailableEvent = SwapSurfaces[0];
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_OBJECT_TYPE_MISMATCH);
    Create.BufferAvailableEvent = Event;
    Create.bFailAcquireIfSurfaceBusy = TRUE;
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_NOT_IMPLEMENTED);
    Create.BufferAvailableEvent = NULL;
    Create.bFailAcquireIfSurfaceBusy = 7;
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_INVALID_PARAMETER);
    Create.bFailAcquireIfSurfaceBusy = FALSE;
    Create.hDevice = 0;
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_INVALID_PARAMETER);
    Create.hDevice = 0x40000002;
    ok_hex(SwapCall("CreateSwapChain", &Create), STATUS_INVALID_PARAMETER);
    Create.hDevice = SwapDevice;
    Create.bProducer = 7;
    Status = SwapCall("CreateSwapChain", &Create);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
        CloseHandle(Create.hNtSwapChain);
    CloseHandle(Event);
}

static VOID
SwapTestOpen(VOID)
{
    D3DKMT_OPENSWAPCHAIN Open;
    HANDLE Event = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE Out[SWAP_SURFACES];
    HANDLE SwapChain;
    HANDLE Opened;
    UINT Count;
    NTSTATUS Status;

    if (SwapCreate(2, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain) != STATUS_SUCCESS)
    {
        skip("no swap chain\n");
        return;
    }
    ZeroMemory(&Open, sizeof(Open));
    Open.hNtSwapChain = (HANDLE)0x1234;
    Open.DesiredAccess = GENERIC_ALL;
    Open.SurfaceCount = 2;
    Open.pNtSurfaceHandles = Out;
    ok_hex(SwapCall("OpenSwapChain", &Open), STATUS_INVALID_PARAMETER);
    Open.hNtSwapChain = SwapChain;
    Open.hDevice = SwapDevice;
    Open.bFailAcquireIfSurfaceBusy = TRUE;
    Open.BufferAvailableEvent = Event;
    ok_hex(SwapCall("OpenSwapChain", &Open), STATUS_NOT_IMPLEMENTED);
    Open.bFailAcquireIfSurfaceBusy = FALSE;
    Open.BufferAvailableEvent = (HANDLE)0x1234;
    ok_hex(SwapCall("OpenSwapChain", &Open), STATUS_INVALID_HANDLE);
    CloseHandle(Event);
    Count = 0;
    ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, NULL, NULL), STATUS_BUFFER_TOO_SMALL);
    ok_int(Count, 2);
    Count = 0;
    ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_INVALID_PARAMETER);
    Count = 3;
    ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_INVALID_PARAMETER);
    Count = 2;
    ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, NULL, NULL), STATUS_INVALID_PARAMETER);
    Count = 2;
    ok_hex(SwapOpen(SwapChain, NULL, TRUE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_ALREADY_REGISTERED);

    Count = 2;
    ZeroMemory(Out, sizeof(Out));
    Status = SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_READ, &Count, Out, &Opened);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok(Opened == SwapChain, "open by handle returned %p, expected %p\n", Opened, SwapChain);
        ok(Out[0] != NULL && Out[1] != NULL, "no surface handles\n");
        ok(SwapTypeIs(Out[0], L"DxgkSharedResource"), "wrong surface object type\n");
        ok_hex(SwapGranted(Out[0]), READ_CONTROL);
        CloseHandle(Out[0]);
        CloseHandle(Out[1]);
    }
    Count = 2;
    ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_ALREADY_REGISTERED);
    CloseHandle(SwapChain);

    if (SwapCreate(2, SwapSurfaces, FALSE, GENERIC_ALL, NULL, &SwapChain) == STATUS_SUCCESS)
    {
        Count = 2;
        ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_ALREADY_REGISTERED);
        Count = 2;
        Status = SwapOpen(SwapChain, NULL, TRUE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL);
        ok_hex(Status, STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            CloseHandle(Out[0]);
            CloseHandle(Out[1]);
        }
        CloseHandle(SwapChain);
    }
}

static VOID
SwapTestNamed(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    HANDLE Out[SWAP_SURFACES];
    HANDLE SwapChain;
    HANDLE Second;
    HANDLE Opened;
    UINT Count;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\BaseNamedObjects\\LiberNTSwapChainTest");
    InitializeObjectAttributes(&Attributes, &Name, 0, NULL, NULL);
    Status = SwapCreate(2, SwapSurfaces, TRUE, GENERIC_ALL, &Attributes, &SwapChain);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        return;
    ok_hex(SwapCreate(2, SwapSurfaces, TRUE, GENERIC_ALL, &Attributes, &Second), STATUS_OBJECT_NAME_COLLISION);

    Count = 2;
    Opened = NULL;
    ok_hex(SwapOpen(NULL, &Attributes, FALSE, SYNCHRONIZE, GENERIC_ALL, &Count, Out, &Opened), STATUS_ACCESS_DENIED);
    ok(Opened == NULL, "denied open returned %p\n", Opened);

    Count = 2;
    ZeroMemory(Out, sizeof(Out));
    Status = SwapOpen(NULL, &Attributes, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, &Opened);
    ok_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok(Opened != NULL && Opened != SwapChain, "open by name returned %p\n", Opened);
        ok_hex(SwapGranted(Out[0]), SHARED_ALLOCATION_ALL_ACCESS);
        CloseHandle(Opened);
        CloseHandle(Out[0]);
        CloseHandle(Out[1]);
    }

    RtlInitUnicodeString(&Name, L"\\BaseNamedObjects\\LiberNTSwapChainMissing");
    Count = 2;
    ok_hex(SwapOpen(NULL, &Attributes, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_OBJECT_NAME_NOT_FOUND);
    CloseHandle(SwapChain);
}

static VOID
SwapTestAccess(VOID)
{
    D3DKMT_ABANDONSWAPCHAIN Abandon;
    BYTE Data[4] = { 1, 2, 3, 4 };
    HANDLE Out[SWAP_SURFACES];
    HANDLE SwapChain;
    UINT Count = 2;

    if (SwapCreate(2, SwapSurfaces, TRUE, 0, NULL, &SwapChain) == STATUS_SUCCESS)
    {
        ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, sizeof(Data), Data, NULL), STATUS_ACCESS_DENIED);
        ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, NULL, NULL), STATUS_ACCESS_DENIED);
        ok_hex(SwapOpen(SwapChain, NULL, FALSE, GENERIC_ALL, GENERIC_ALL, &Count, Out, NULL), STATUS_ACCESS_DENIED);
        Abandon.hNtSwapChain = SwapChain;
        ok_hex(SwapCall("AbandonSwapChain", &Abandon), STATUS_ACCESS_DENIED);
        CloseHandle(SwapChain);
    }
    if (SwapCreate(2, SwapSurfaces, TRUE, READ_CONTROL, NULL, &SwapChain) == STATUS_SUCCESS)
    {
        ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, sizeof(Data), Data, NULL), STATUS_SUCCESS);
        CloseHandle(SwapChain);
    }
    if (SwapCreate(2, SwapSurfaces, TRUE, DELETE, NULL, &SwapChain) == STATUS_SUCCESS)
    {
        ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, sizeof(Data), Data, NULL), STATUS_ACCESS_DENIED);
        CloseHandle(SwapChain);
    }
}

static VOID
SwapTestMetadata(VOID)
{
    static BYTE Big[0x2001];
    BYTE Eight[8] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 };
    BYTE Two[2] = { 0x22, 0x22 };
    BYTE Read[16];
    HANDLE SwapChain;
    UINT Copied;

    if (SwapCreate(2, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain) != STATUS_SUCCESS)
    {
        skip("no swap chain\n");
        return;
    }
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, sizeof(Read), Read, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, Read, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, NULL, &Copied), STATUS_SUCCESS);
    ok_int(Copied, 0);
    ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, 0, Eight, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, sizeof(Eight), Eight, &Copied), STATUS_SUCCESS);
    ok_int(Copied, 0xEEEE);

    memset(Read, 0xCC, sizeof(Read));
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, sizeof(Read), Read, &Copied), STATUS_SUCCESS);
    ok_int(Copied, 8);
    ok(!memcmp(Read, Eight, 8) && Read[8] == 0xCC, "global metadata not copied\n");

    memset(Read, 0xCC, sizeof(Read));
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 4, Read, &Copied), STATUS_BUFFER_OVERFLOW);
    ok_int(Copied, 0xEEEE);
    ok(Read[3] == 0x11 && Read[4] == 0xCC, "partial copy wrong\n");

    ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, sizeof(Two), Two, NULL), STATUS_SUCCESS);
    memset(Read, 0xCC, sizeof(Read));
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, sizeof(Read), Read, &Copied), STATUS_SUCCESS);
    ok_int(Copied, 8);
    ok(Read[0] == 0x22 && Read[1] == 0x22 && Read[2] == 0x11 && Read[7] == 0x11, "stored metadata did not keep its tail\n");

    ok_hex(SwapMetadata(SwapChain, FALSE, FALSE, sizeof(Read), Read, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, TRUE, FALSE, sizeof(Two), Two, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, 0x2000, Big, NULL), STATUS_SUCCESS);
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, NULL, &Copied), STATUS_SUCCESS);
    ok_int(Copied, 0x2000);
    ok_hex(SwapMetadata(SwapChain, TRUE, TRUE, 0x2001, Big, NULL), STATUS_INVALID_PARAMETER);
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, NULL, NULL), STATUS_DELETE_PENDING);
    CloseHandle(SwapChain);
}

static VOID
SwapTestLifecycle(VOID)
{
    D3DKMT_ABANDONSWAPCHAIN Abandon;
    D3DKMT_ACQUIRESWAPCHAIN Acquire;
    D3DKMT_RELEASESWAPCHAIN Release;
    D3DKMT_ADDSURFACETOSWAPCHAIN Add;
    D3DKMT_REMOVESURFACEFROMSWAPCHAIN Remove;
    D3DKMT_UNORDEREDPRESENTSWAPCHAIN Present;
    D3DKMT_SETDODINDIRECTSWAPCHAIN Dod;
    D3DKMT_CREATEBUNDLEOBJECT Bundle;
    OBJECT_ATTRIBUTES Attributes;
    POBJECT_ATTRIBUTES AttributeList[1] = { &Attributes };
    HANDLE SwapChain;

    if (SwapCreate(2, SwapSurfaces, TRUE, GENERIC_ALL, NULL, &SwapChain) != STATUS_SUCCESS)
    {
        skip("no swap chain\n");
        return;
    }
    ZeroMemory(&Acquire, sizeof(Acquire));
    Acquire.hNtSwapChain = SwapChain;
    Acquire.bProducer = TRUE;
    ZeroMemory(&Release, sizeof(Release));
    Release.hNtSwapChain = SwapChain;
    Release.bProducer = TRUE;
    ok_hex(SwapCall("ReleaseSwapChain", &Release), STATUS_INVALID_PARAMETER);

    ZeroMemory(&Add, sizeof(Add));
    Add.hNtSwapChain = SwapChain;
    Add.bProducer = TRUE;
    Add.hNtSurfaceHandle = SwapSurfaces[2];
    ok_hex(SwapCall("AddSurfaceToSwapChain", &Add), STATUS_NOT_SUPPORTED);
    Add.bProducer = FALSE;
    ok_hex(SwapCall("AddSurfaceToSwapChain", &Add), STATUS_INVALID_PARAMETER);
    Add.bProducer = TRUE;
    Add.hNtSwapChain = NULL;
    ok_hex(SwapCall("AddSurfaceToSwapChain", &Add), STATUS_INVALID_HANDLE);
    ZeroMemory(&Remove, sizeof(Remove));
    Remove.hNtSwapChain = SwapChain;
    Remove.bProducer = TRUE;
    Remove.hNtSurfaceHandle = SwapSurfaces[0];
    ok_hex(SwapCall("RemoveSurfaceFromSwapChain", &Remove), STATUS_NOT_SUPPORTED);
    ZeroMemory(&Present, sizeof(Present));
    Present.hNtSwapChain = SwapChain;
    Present.bProducer = TRUE;
    ok_hex(SwapCall("UnOrderedPresentSwapChain", &Present), STATUS_NOT_SUPPORTED);

    ZeroMemory(&Dod, sizeof(Dod));
    Dod.hDevice = SwapDevice;
    Dod.hNtSwapChain = SwapChain;
    ok_hex(SwapCall("SetDodIndirectSwapchain", &Dod), STATUS_INVALID_PARAMETER);
    Dod.bTerminateOutput = TRUE;
    ok_hex(SwapCall("SetDodIndirectSwapchain", &Dod), STATUS_INVALID_PARAMETER);

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    ZeroMemory(&Bundle, sizeof(Bundle));
    Bundle.cObjects = 1;
    Bundle.phObjects = &SwapDevice;
    Bundle.ppObjectAttributes = AttributeList;
    Bundle.pBundleObjectAttributes = &Attributes;
    Bundle.dwBundleDesiredAccess = GENERIC_ALL;
    ok_hex(SwapCall("CreateBundleObject", &Bundle), STATUS_INVALID_PARAMETER);

    Abandon.hNtSwapChain = SwapChain;
    ok_hex(SwapCall("AbandonSwapChain", &Abandon), STATUS_SUCCESS);
    ok_hex(SwapCall("AbandonSwapChain", &Abandon), STATUS_SUCCESS);
    ok_hex(SwapMetadata(SwapChain, FALSE, TRUE, 0, NULL, NULL), STATUS_DELETE_PENDING);
    Present.hNtSwapChain = SwapChain;
    ok_hex(SwapCall("UnOrderedPresentSwapChain", &Present), STATUS_DELETE_PENDING);
    ok_hex(SwapCall("AcquireSwapChain", &Acquire), STATUS_DELETE_PENDING);
    ok_hex(SwapCall("ReleaseSwapChain", &Release), STATUS_DELETE_PENDING);
    CloseHandle(SwapChain);
}

START_TEST(swapchain)
{
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CREATEDEVICE Device;
    HANDLE Sync;
    UINT Index;

    LoadLibraryA("gdi32.dll");
    SwapTestValidation();

    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(SwapCall("EnumAdapters2", &Enum)) || Enum.NumAdapters == 0)
    {
        skip("no D3DKMT adapter\n");
        return;
    }
    ZeroMemory(&Device, sizeof(Device));
    Device.hAdapter = Adapters[Enum.NumAdapters - 1].hAdapter;
    if (!NT_SUCCESS(SwapCall("CreateDevice", &Device)))
    {
        skip("no D3DKMT device\n");
        return;
    }
    SwapDevice = Device.hDevice;
    for (Index = 0; Index < SWAP_SURFACES; ++Index)
        SwapSurfaces[Index] = SwapCreateSurface();
    if (SwapSurfaces[SWAP_SURFACES - 1] == NULL)
    {
        skip("no shareable surface\n");
        return;
    }
    for (Index = SWAP_SURFACES; Index < ARRAYSIZE(SwapSurfaces); ++Index)
        SwapSurfaces[Index] = SwapSurfaces[Index % SWAP_SURFACES];

    Sync = SwapCreateSyncHandle();
    ok(Sync != NULL, "no shared sync object\n");
    if (Sync != NULL)
    {
        SwapTestWrongType(Sync);
        CloseHandle(Sync);
    }
    SwapTestCreate();
    SwapTestOpen();
    SwapTestNamed();
    SwapTestAccess();
    SwapTestMetadata();
    SwapTestLifecycle();
}
