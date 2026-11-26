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
#include "wddm_bridge.h"

#define NDEBUG
#include <debug.h>

#define DXGK_HANDOVER_IOCTL CTL_CODE(FILE_DEVICE_VIDEO, 0x815, METHOD_NEITHER, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define DXGK_HANDOVER_SIZE 0x328
#define DXGK_HANDOVER_VERSION 0x30
#define DXGK_HANDOVER_CALLOUTS 32
#define DXGK_HANDOVER_PROCESS_CALLOUT 2
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
        /* 40 */ (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngNeutral, (PVOID)EngGetAdapterUniquenessPointer,
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

BOOLEAN
WddmHandOverIsActive(VOID)
{
    return ReadPointerAcquire((PVOID volatile *)&g_DxgkProcessCallout) != NULL;
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
