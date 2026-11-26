/*
 * PROJECT:     LiberNT win32k
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hand win32k over to a graphics kernel that has no win32k bridge
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <windef.h>
#include <d3dkmthk.h>
#include <ntimage.h>
#include <ntddvdeo.h>
#include "wddm_bridge.h"

#define NDEBUG
#include <debug.h>

#define DXGK_HANDOVER_IOCTL CTL_CODE(FILE_DEVICE_VIDEO, 0x815, METHOD_NEITHER, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define DXGK_GDI_INIT_WIN32K_CALLBACKS CTL_CODE(FILE_DEVICE_VIDEO, 0x807, METHOD_NEITHER, FILE_ANY_ACCESS)
#define DXGK_GDI_USE_DEVICE_IN_SESSION CTL_CODE(FILE_DEVICE_VIDEO, 0x80A, METHOD_NEITHER, FILE_ANY_ACCESS)
#define DXGK_HANDOVER_SIZE 0x328
#define DXGK_HANDOVER_VERSION 0x30
#define DXGK_HANDOVER_CALLOUTS 32
#define DXGK_HANDOVER_PROCESS_CALLOUT 2
#define DXGK_HANDOVER_ENUM_ADAPTERS2 6
#define DXGK_HANDOVER_CLOSE_ADAPTER 8
#define DXGK_HANDOVER_GET_PATHS_MODALITY 15
#define DXGK_HANDOVER_FUNCTIONALIZE_PATHS_MODALITY 16
#define DXGK_HANDOVER_APPLY_PATHS_MODALITY 17
#define DXGK_HANDOVER_FINALIZE_PATHS_MODALITY 18
#define DXGK_HANDOVER_PERSIST_PATHS_MODALITY 19
#define DXGK_HANDOVER_FREE_PATHS_MODALITY 20
#define DXGK_PATHS_MODALITY_CAPACITY 8
#define DXGK_PATHS_MODALITY_SIZE(PathCount) (0x38 + (PathCount) * 0x128)
#define DXGK_PATHS_MODALITY_QUERY_RECOMMENDED 0x10000000
#define DXGK_PATHS_MODALITY_FUNCTIONALIZE 0x8000
#define DXGK_PATHS_MODALITY_APPLY 0x24000
#define DXGK_PATHS_MODALITY_PERSIST 0x80000000
#define DXGK_ENGINE_SLOTS 59
#define DXGK_ENGINE_VERSION 0xC

typedef struct _DXGK_HANDOVER
{
    USHORT Size;
    USHORT Version;
    ULONG Reserved;
    PVOID Win32kImage;
    ULONG_PTR Reserved2;
    PVOID Callouts[DXGK_HANDOVER_CALLOUTS];
    UCHAR Tail[DXGK_HANDOVER_SIZE - 0x118];
} DXGK_HANDOVER, *PDXGK_HANDOVER;

C_ASSERT(FIELD_OFFSET(DXGK_HANDOVER, Callouts) == 0x18 || sizeof(PVOID) != 8);
C_ASSERT(sizeof(DXGK_HANDOVER) == DXGK_HANDOVER_SIZE || sizeof(PVOID) != 8);

typedef struct _DXGKWIN32KENG_INTERFACE_HEADER
{
    USHORT Size;
    USHORT Version;
    ULONG Reserved;
} DXGKWIN32KENG_INTERFACE_HEADER, *PDXGKWIN32KENG_INTERFACE_HEADER;

typedef struct _DXGKWIN32KENG_INTERFACE
{
    DXGKWIN32KENG_INTERFACE_HEADER Header;
    PVOID Slots[DXGK_ENGINE_SLOTS];
} DXGKWIN32KENG_INTERFACE;

C_ASSERT(sizeof(DXGKWIN32KENG_INTERFACE) == 0x1E0 || sizeof(PVOID) != 8);

typedef struct _DXGK_QUERY_WIN32_INFO
{
    ULONG Type;
    ULONG Size;
    PVOID Buffer;
} DXGK_QUERY_WIN32_INFO, *PDXGK_QUERY_WIN32_INFO;

typedef LONG (__cdecl *PFN_DXGK_PROCESS_CALLOUT)(PVOID *, const DXGKWIN32KENG_INTERFACE_HEADER *, UCHAR);

/* Header of the graphics kernel's path set; the path records that follow it are opaque here */
typedef struct _DXGK_PATHS_MODALITY
{
    ULONG Reserved;
    UCHAR SetId[16];
    USHORT Count;
    USHORT Capacity;
} DXGK_PATHS_MODALITY, *PDXGK_PATHS_MODALITY;

C_ASSERT(sizeof(DXGK_PATHS_MODALITY) == 0x18);
C_ASSERT(DXGK_PATHS_MODALITY_SIZE(1) > sizeof(DXGK_PATHS_MODALITY));

typedef LONG (__cdecl *PFN_DXGK_GET_PATHS_MODALITY)(ULONG, PDXGK_PATHS_MODALITY, ULONG_PTR *, PDXGK_PATHS_MODALITY, ULONG_PTR);
typedef LONG (__cdecl *PFN_DXGK_FUNCTIONALIZE_PATHS_MODALITY)(ULONG, PDXGK_PATHS_MODALITY, PVOID, PVOID);
typedef LONG (__cdecl *PFN_DXGK_APPLY_PATHS_MODALITY)(ULONG, PDXGK_PATHS_MODALITY, PVOID, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                                      ULONG_PTR, PVOID, ULONG_PTR);
typedef LONG (__cdecl *PFN_DXGK_FINALIZE_PATHS_MODALITY)(PDXGK_PATHS_MODALITY, PVOID, PVOID);
typedef LONG (__cdecl *PFN_DXGK_PERSIST_PATHS_MODALITY)(ULONG, PDXGK_PATHS_MODALITY, PVOID, ULONG_PTR);
typedef ULONG (__cdecl *PFN_DXGK_FREE_PATHS_MODALITY)(PDXGK_PATHS_MODALITY);
typedef LONG (__cdecl *PFN_DXGK_ENUM_ADAPTERS2)(D3DKMT_ENUMADAPTERS2 *, KPROCESSOR_MODE, BOOLEAN, KWAIT_REASON);
typedef LONG (__cdecl *PFN_DXGK_CLOSE_ADAPTER)(const D3DKMT_CLOSEADAPTER *, KPROCESSOR_MODE);

NTSTATUS
NTAPI
SMgrNotifySessionChange(
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Win32kCallout,
    _In_opt_ PVOID Parameter3,
    _In_opt_ PVOID Parameter4,
    _In_ ULONG Parameter5);

BOOL
IntCompositionIsAttachedProcess(VOID);

extern const IMAGE_DOS_HEADER __ImageBase;

static PFN_DXGK_PROCESS_CALLOUT g_DxgkProcessCallout;
static PVOID g_DxgkCallouts[DXGK_HANDOVER_CALLOUTS];
static LONG g_DxgkTopologyApplied;
static ULONG g_DxgkAdapterUniqueness;
static ULONG g_DxgkMonitorUniqueness;

/* FIXME: the engine slots below without a behaviour of their own return a neutral value; their contracts are INFO_NEEDED */
static ULONG_PTR __cdecl EngNeutral(VOID) { return 0; }
static LONG __cdecl EngNotSupported(VOID) { return STATUS_NOT_SUPPORTED; }

static
int
__cdecl
EngIsDwmProcess(VOID)
{
    return IntCompositionIsAttachedProcess() ? TRUE : FALSE;
}

static
PULONG
__cdecl
EngGetAdapterUniquenessPointer(VOID)
{
    /* FIXME: the value does not change when an adapter arrives or leaves */
    return &g_DxgkAdapterUniqueness;
}

static
PULONG
__cdecl
EngGetMonitorUniquenessPointer(VOID)
{
    /* FIXME: the value does not change when a monitor arrives or leaves */
    return &g_DxgkMonitorUniqueness;
}

static
int
__cdecl
EngAdjustMonitorPosition(
    _Inout_ PRECT MonitorRect,
    _In_ UINT Parameter2,
    _In_ UINT Parameter3)
{
    UNREFERENCED_PARAMETER(Parameter2);
    UNREFERENCED_PARAMETER(Parameter3);

    if (MonitorRect == NULL)
        return FALSE;
    ASSERT(MonitorRect->left <= MonitorRect->right);
    ASSERT(MonitorRect->top <= MonitorRect->bottom);
    /* FIXME: the monitor keeps the position the graphics kernel gives it; the desktop layout rules and the two other arguments are INFO_NEEDED */
    return TRUE;
}

static
LONG
__cdecl
EngQueryWin32Info(
    _Inout_ PDXGK_QUERY_WIN32_INFO Info)
{
    if (Info == NULL || Info->Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (Info->Type)
    {
        case 0:
            if (Info->Size != sizeof(ULONG))
                return STATUS_INFO_LENGTH_MISMATCH;
            *(PULONG)Info->Buffer = 0;
            return STATUS_SUCCESS;

        case 3:
            if (Info->Size != sizeof(BOOLEAN))
                return STATUS_INFO_LENGTH_MISMATCH;
            *(PBOOLEAN)Info->Buffer = FALSE;
            return STATUS_SUCCESS;

        default:
            /* TODO: type 1 (a 32-byte record) and the other types; their contents are INFO_NEEDED */
            return STATUS_NOT_SUPPORTED;
    }
}

static const DXGKWIN32KENG_INTERFACE g_DxgkEngineInterface =
{
    { sizeof(DXGKWIN32KENG_INTERFACE), DXGK_ENGINE_VERSION, 0 },
    {
        /*  0 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /*  4 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /*  8 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 12 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 16 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 20 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 24 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 28 */ (PVOID)EngIsDwmProcess, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 32 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 36 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 40 */ (PVOID)EngNeutral, (PVOID)EngAdjustMonitorPosition, (PVOID)EngNeutral, (PVOID)EngGetAdapterUniquenessPointer,
        /* 44 */ (PVOID)EngNeutral, (PVOID)EngQueryWin32Info, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 48 */ (PVOID)EngNeutral, (PVOID)EngGetMonitorUniquenessPointer, (PVOID)EngNeutral, (PVOID)EngNeutral,
        /* 52 */ (PVOID)EngNotSupported, (PVOID)EngNotSupported, (PVOID)EngNeutral, (PVOID)EngNotSupported,
        /* 56 */ (PVOID)EngNotSupported, (PVOID)EngNotSupported, (PVOID)EngNotSupported,
    }
};

/* FIXME: the callouts of a graphics kernel without the bridge (VideoPnpNotifyCallout and the others) are not handled */
static
VOID
NTAPI
WddmHandOverWin32kCallout(
    _In_ PVOID Params)
{
    UNREFERENCED_PARAMETER(Params);
}

NTSTATUS
WddmHandOverInit(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDXGK_HANDOVER HandOver;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceObject != NULL);

    if (sizeof(PVOID) != 8)
        return STATUS_NOT_SUPPORTED;

    if (ReadPointerAcquire((PVOID volatile *)&g_DxgkProcessCallout) != NULL)
        return STATUS_SUCCESS;

    HandOver = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*HandOver), 'oHxD');
    if (HandOver == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    HandOver->Size = DXGK_HANDOVER_SIZE;
    HandOver->Version = DXGK_HANDOVER_VERSION;
    HandOver->Win32kImage = (PVOID)&__ImageBase;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(DXGK_HANDOVER_IOCTL, DeviceObject, HandOver, sizeof(*HandOver),
                                        HandOver, sizeof(*HandOver), TRUE, &Event, &IoStatus);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(HandOver, 'oHxD');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && HandOver->Callouts[DXGK_HANDOVER_PROCESS_CALLOUT] == NULL)
        Status = STATUS_INVALID_DEVICE_STATE;

    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(g_DxgkCallouts, HandOver->Callouts, sizeof(g_DxgkCallouts));
        WritePointerRelease((PVOID volatile *)&g_DxgkProcessCallout, HandOver->Callouts[DXGK_HANDOVER_PROCESS_CALLOUT]);
        DPRINT1("WddmHandOver: the graphics kernel accepted win32k\n");
    }
    else
    {
        DPRINT1("WddmHandOver: the graphics kernel refused win32k: 0x%08lx\n", Status);
    }

    ExFreePoolWithTag(HandOver, 'oHxD');
    return Status;
}

static
NTSTATUS
WddmHandOverInternalIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _Inout_updates_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceObject != NULL);
    ASSERT(Buffer != NULL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, Buffer, Size, Buffer, Size, TRUE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
WddmHandOverApplyDisplayTopology(VOID)
{
    PFN_DXGK_GET_PATHS_MODALITY GetPathsModality;
    PFN_DXGK_PERSIST_PATHS_MODALITY PersistPathsModality;
    PFN_DXGK_FUNCTIONALIZE_PATHS_MODALITY FunctionalizePathsModality;
    PFN_DXGK_APPLY_PATHS_MODALITY ApplyPathsModality;
    PFN_DXGK_FINALIZE_PATHS_MODALITY FinalizePathsModality;
    PFN_DXGK_FREE_PATHS_MODALITY FreePathsModality;
    PDXGK_PATHS_MODALITY Paths;
    ULONG_PTR Requested[4];
    ULONG_PTR PersistContext[32];
    LONG Previous;
    LONG Status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(WddmHandOverIsActive());

    /* TODO: the topology is applied once; display changes the graphics kernel reports through its callout do not re-apply it */
    Previous = InterlockedCompareExchange(&g_DxgkTopologyApplied, 1, 0);
    ASSERT(Previous == 0 || Previous == 1);
    if (Previous != 0)
        return;

    GetPathsModality = (PFN_DXGK_GET_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_GET_PATHS_MODALITY];
    PersistPathsModality = (PFN_DXGK_PERSIST_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_PERSIST_PATHS_MODALITY];
    FunctionalizePathsModality = (PFN_DXGK_FUNCTIONALIZE_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_FUNCTIONALIZE_PATHS_MODALITY];
    ApplyPathsModality = (PFN_DXGK_APPLY_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_APPLY_PATHS_MODALITY];
    FinalizePathsModality = (PFN_DXGK_FINALIZE_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_FINALIZE_PATHS_MODALITY];
    FreePathsModality = (PFN_DXGK_FREE_PATHS_MODALITY)g_DxgkCallouts[DXGK_HANDOVER_FREE_PATHS_MODALITY];
    if (GetPathsModality == NULL || PersistPathsModality == NULL || FunctionalizePathsModality == NULL ||
        ApplyPathsModality == NULL || FinalizePathsModality == NULL || FreePathsModality == NULL)
    {
        return;
    }

    /* FIXME: a path set holding n paths takes 0x38 + n * 0x128 bytes, the size the engine allocates; the path records are private */
    Paths = ExAllocatePoolZero(PagedPool, DXGK_PATHS_MODALITY_SIZE(DXGK_PATHS_MODALITY_CAPACITY), 'pCxD');
    if (Paths == NULL)
        return;
    Paths->Count = DXGK_PATHS_MODALITY_CAPACITY;
    Paths->Capacity = DXGK_PATHS_MODALITY_CAPACITY;
    RtlZeroMemory(Requested, sizeof(Requested));
    Requested[0] = DXGK_PATHS_MODALITY_CAPACITY;
    RtlZeroMemory(PersistContext, sizeof(PersistContext));

    /* FIXME: the flags and scalar arguments take the values the engine passes at its first display set-up and the persist context is left zero; their meaning is INFO_NEEDED */
    Status = GetPathsModality(DXGK_PATHS_MODALITY_QUERY_RECOMMENDED, Paths, Requested, Paths, 0);
    ASSERT(Paths->Count <= DXGK_PATHS_MODALITY_CAPACITY);
    if (Status >= 0 && Paths->Count != 0)
    {
        Status = PersistPathsModality(DXGK_PATHS_MODALITY_PERSIST, Paths, PersistContext, 1);
        if (Status < 0)
            DPRINT1("WddmHandOver: the graphics kernel did not record the display topology: 0x%08lx\n", Status);
        Status = FunctionalizePathsModality(DXGK_PATHS_MODALITY_FUNCTIONALIZE, Paths, NULL, NULL);
        ASSERT(Paths->Count <= DXGK_PATHS_MODALITY_CAPACITY);
        if (Status >= 0)
            Status = ApplyPathsModality(DXGK_PATHS_MODALITY_APPLY, Paths, NULL, 0xC, 0, 0x100, 1, NULL, 0x10);
        if (Status >= 0)
            Status = FinalizePathsModality(Paths, NULL, NULL);
        if (Status < 0)
            DPRINT1("WddmHandOver: the graphics kernel refused the display topology: 0x%08lx\n", Status);
    }
    else
    {
        DPRINT1("WddmHandOver: no display topology from the graphics kernel: 0x%08lx, %u paths\n", Status, Paths->Count);
    }
    FreePathsModality(Paths);
    ExFreePoolWithTag(Paths, 'pCxD');
}

NTSTATUS
WddmHandOverInitGdiDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PVIDEO_WIN32K_CALLBACKS Callbacks)
{
    VIDEO_DEVICE_SESSION_STATUS SessionStatus;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(WddmHandOverIsActive());

    Status = WddmHandOverInternalIoctl(DeviceObject, DXGK_GDI_INIT_WIN32K_CALLBACKS, Callbacks, sizeof(*Callbacks));
    if (!NT_SUCCESS(Status))
        return Status;

    /* FIXME: the display session takes the device once; a second request with bEnable 0 that follows it is INFO_NEEDED */
    SessionStatus.bEnable = TRUE;
    SessionStatus.bSuccess = FALSE;
    Status = WddmHandOverInternalIoctl(DeviceObject, DXGK_GDI_USE_DEVICE_IN_SESSION, &SessionStatus, sizeof(SessionStatus));
    if (NT_SUCCESS(Status) && !SessionStatus.bSuccess)
        Status = STATUS_DEVICE_BUSY;
    if (NT_SUCCESS(Status))
        WddmHandOverApplyDisplayTopology();
    return Status;
}

BOOLEAN
WddmHandOverIsActive(VOID)
{
    return ReadPointerAcquire((PVOID volatile *)&g_DxgkProcessCallout) != NULL;
}

NTSTATUS
WddmHandOverIsPresentSource(
    _In_ LUID AdapterLuid,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ PBOOLEAN IsSource)
{
    PFN_DXGK_ENUM_ADAPTERS2 EnumAdapters2;
    PFN_DXGK_CLOSE_ADAPTER CloseAdapter;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CLOSEADAPTER Close;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(IsSource != NULL);

    *IsSource = FALSE;
    if (!WddmHandOverIsActive())
        return STATUS_DEVICE_NOT_READY;
    EnumAdapters2 = (PFN_DXGK_ENUM_ADAPTERS2)g_DxgkCallouts[DXGK_HANDOVER_ENUM_ADAPTERS2];
    CloseAdapter = (PFN_DXGK_CLOSE_ADAPTER)g_DxgkCallouts[DXGK_HANDOVER_CLOSE_ADAPTER];
    if (EnumAdapters2 == NULL || CloseAdapter == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(Adapters, sizeof(Adapters));
    RtlZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = RTL_NUMBER_OF(Adapters);
    Enum.pAdapters = Adapters;
    /* FIXME: the meaning of the third argument is unknown; it is the value the system call passes */
    Status = EnumAdapters2(&Enum, KernelMode, TRUE, Executive);
    if (!NT_SUCCESS(Status))
        return Status;
    ASSERT(Enum.NumAdapters <= RTL_NUMBER_OF(Adapters));
    for (Index = 0; Index < Enum.NumAdapters && Index < RTL_NUMBER_OF(Adapters); Index++)
    {
        NTSTATUS CloseStatus;

        if (Adapters[Index].AdapterLuid.LowPart == AdapterLuid.LowPart &&
            Adapters[Index].AdapterLuid.HighPart == AdapterLuid.HighPart &&
            VidPnSourceId < Adapters[Index].NumOfSources)
        {
            *IsSource = TRUE;
        }
        Close.hAdapter = Adapters[Index].hAdapter;
        CloseStatus = CloseAdapter(&Close, KernelMode);
        if (!NT_SUCCESS(CloseStatus))
            DPRINT1("WddmHandOver: closing adapter 0x%x failed: 0x%08lx\n", Close.hAdapter, CloseStatus);
    }
    return STATUS_SUCCESS;
}

const VOID *
NTAPI
RosWddmHandOverEngineInterface(VOID)
{
    return WddmHandOverIsActive() ? &g_DxgkEngineInterface.Header : NULL;
}

VOID
WddmHandOverProcessCallout(
    _Inout_ PVOID *DxgProcess,
    _In_ BOOLEAN Create)
{
    PFN_DXGK_PROCESS_CALLOUT Callout;
    LONG Status;

    PAGED_CODE();
    ASSERT(DxgProcess != NULL);

    Callout = ReadPointerAcquire((PVOID volatile *)&g_DxgkProcessCallout);
    if (Callout == NULL)
        return;

    if (Create)
    {
        if (*DxgProcess != NULL)
            return;
        Status = Callout(DxgProcess, &g_DxgkEngineInterface.Header, TRUE);
        if (Status < 0)
            DPRINT1("WddmHandOver: the process callout failed: 0x%08lx\n", Status);
        return;
    }

    if (*DxgProcess == NULL)
        return;
    Status = Callout(DxgProcess, &g_DxgkEngineInterface.Header, FALSE);
    if (Status < 0)
        DPRINT1("WddmHandOver: the process teardown callout failed: 0x%08lx\n", Status);
    *DxgProcess = NULL;
}

/* TODO: the end of a session is not reported to the graphics kernel */
NTSTATUS
WddmHandOverStartSession(
    _Inout_ PVOID *CsrDxgProcess)
{
    PAGED_CODE();
    ASSERT(CsrDxgProcess != NULL);

    if (!WddmHandOverIsActive())
        return STATUS_SUCCESS;

    WddmHandOverProcessCallout(CsrDxgProcess, TRUE);
    if (*CsrDxgProcess == NULL)
        return STATUS_UNSUCCESSFUL;

    /* FIXME: the meaning of the first, third, fourth and fifth parameters is INFO_NEEDED; a session start passes 0 */
    return SMgrNotifySessionChange(NULL, (PVOID)WddmHandOverWin32kCallout, NULL, NULL, 0);
}
