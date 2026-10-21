/*
 * PROJECT:     LiberNT Hyper-V Dynamic Memory Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reports memory pressure to Hyper-V and balloons guest memory on request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/exfuncs.h>
#include <vmbusapi.h>

#define NDEBUG
#include <debug.h>

#define DM_TAG                  'mDVL'
#define DM_RING_SIZE            (32 * 1024)
#define DM_PACKET_MAX           (8 * 1024)
#define DM_MESSAGE_MAX          4096
#define DM_CHUNK_PAGES          512
#define DM_STATUS_INTERVAL_MS   1000
#define DM_RESPONSE_WAIT_MS     5000
#define DM_FLOOR_MINIMUM_PAGES  ((64 * 1024 * 1024) / PAGE_SIZE)

#define DM_VERSION(Major, Minor) (((ULONG)(Major) << 16) | (ULONG)(Minor))

#define DM_ERROR                0
#define DM_VERSION_REQUEST      1
#define DM_VERSION_RESPONSE     2
#define DM_CAPABILITIES_REPORT  3
#define DM_CAPABILITIES_RESPONSE 4
#define DM_STATUS_REPORT        5
#define DM_BALLOON_REQUEST      6
#define DM_BALLOON_RESPONSE     7
#define DM_UNBALLOON_REQUEST    8
#define DM_UNBALLOON_RESPONSE   9
#define DM_HOT_ADD_REQUEST      10
#define DM_HOT_ADD_RESPONSE     11
#define DM_INFO_MESSAGE         12

#define DM_HEADER_SIZE          8
#define DM_RANGE_HEADER_SIZE    16
#define DM_MAX_RANGES           ((DM_MESSAGE_MAX - DM_RANGE_HEADER_SIZE) / sizeof(ULONG64))
#define DM_RANGE_MAX_PAGES      ((1UL << 24) - 1)

typedef struct _DM_CHUNK
{
    LIST_ENTRY Link;
    PMDL Mdl;
    ULONG PageCount;
} DM_CHUNK, *PDM_CHUNK;

typedef struct _DM_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    BOOLEAN Ready;
    PKTHREAD Thread;
    KEVENT StopEvent;
    KEVENT PacketEvent;
    ULONG TransactionId;
    ULONG Version;
    LIST_ENTRY Chunks;
    ULONG64 BalloonPages;
    ULONG64 PhysicalPages;
    LARGE_INTEGER LastStatus;
    UCHAR Packet[DM_PACKET_MAX];
    UCHAR Message[DM_MESSAGE_MAX];
} DM_EXTENSION, *PDM_EXTENSION;

static const ULONG DmVersions[] =
{
    DM_VERSION(2, 0),
    DM_VERSION(1, 0),
    DM_VERSION(0, 3),
};

static
VOID
DmPut16(
    _Out_writes_bytes_(2) PUCHAR Buffer,
    _In_ USHORT Value)
{
    RtlCopyMemory(Buffer, &Value, sizeof(Value));
}

static
VOID
DmPut32(
    _Out_writes_bytes_(4) PUCHAR Buffer,
    _In_ ULONG Value)
{
    RtlCopyMemory(Buffer, &Value, sizeof(Value));
}

static
VOID
DmPut64(
    _Out_writes_bytes_(8) PUCHAR Buffer,
    _In_ ULONG64 Value)
{
    RtlCopyMemory(Buffer, &Value, sizeof(Value));
}

static
ULONG
DmGet32(
    _In_reads_bytes_(4) const UCHAR *Buffer)
{
    ULONG Value;

    RtlCopyMemory(&Value, Buffer, sizeof(Value));
    return Value;
}

static
ULONG64
DmGet64(
    _In_reads_bytes_(8) const UCHAR *Buffer)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, Buffer, sizeof(Value));
    return Value;
}

static
NTSTATUS
DmSend(
    _Inout_ PDM_EXTENSION Extension,
    _In_ USHORT Type,
    _In_ ULONG Length)
{
    if (!Extension->Open)
        return STATUS_DEVICE_NOT_READY;

    DmPut16(Extension->Message, Type);
    DmPut16(Extension->Message + 2, (USHORT)Length);
    DmPut32(Extension->Message + 4, ++Extension->TransactionId);
    return VmbChannelSendPacket(Extension->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0, Extension->Message, Length);
}

static
VOID
DmQueryMemory(
    _Inout_ PDM_EXTENSION Extension,
    _Out_ PULONG64 Available,
    _Out_ PULONG64 Committed)
{
    SYSTEM_PERFORMANCE_INFORMATION Performance;

    *Available = 0;
    *Committed = 0;
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemPerformanceInformation, &Performance,
                                            sizeof(Performance), NULL)))
    {
        *Available = Performance.AvailablePages;
        *Committed = Performance.CommittedPages;
    }

    if (*Committed < Extension->BalloonPages)
        *Committed = Extension->BalloonPages;
}

static
VOID
DmSendStatus(
    _Inout_ PDM_EXTENSION Extension)
{
    ULONG64 Available;
    ULONG64 Committed;

    DmQueryMemory(Extension, &Available, &Committed);

    RtlZeroMemory(Extension->Message, 48);
    DmPut64(Extension->Message + 8, Available);
    DmPut64(Extension->Message + 16, Committed);
    (VOID)DmSend(Extension, DM_STATUS_REPORT, 48);
    KeQuerySystemTime(&Extension->LastStatus);
}

static
VOID
DmMaybeSendStatus(
    _Inout_ PDM_EXTENSION Extension)
{
    LARGE_INTEGER Now;

    if (!Extension->Ready)
        return;

    KeQuerySystemTime(&Now);
    if (Now.QuadPart - Extension->LastStatus.QuadPart >= (LONGLONG)DM_STATUS_INTERVAL_MS * 10000)
        DmSendStatus(Extension);
}

static
ULONG64
DmFloorPages(
    _In_ PDM_EXTENSION Extension)
{
    return max((ULONG64)DM_FLOOR_MINIMUM_PAGES, Extension->PhysicalPages / 20);
}

static
ULONG
DmAppendRanges(
    _Inout_ PDM_EXTENSION Extension,
    _In_ ULONG RangeCount,
    _In_ PDM_CHUNK Chunk)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Chunk->Mdl);
    ULONG64 Start;
    ULONG64 Count;
    ULONG Index;

    Start = Pages[0];
    Count = 1;
    for (Index = 1; Index <= Chunk->PageCount; Index++)
    {
        if (Index < Chunk->PageCount && Pages[Index] == Start + Count && Count < DM_RANGE_MAX_PAGES)
        {
            Count++;
            continue;
        }

        DmPut64(Extension->Message + DM_RANGE_HEADER_SIZE + RangeCount * sizeof(ULONG64),
                (Start & ((1ULL << 40) - 1)) | (Count << 40));
        RangeCount++;
        if (Index < Chunk->PageCount)
        {
            Start = Pages[Index];
            Count = 1;
        }
    }

    return RangeCount;
}

static
ULONG
DmChunkRangeCount(
    _In_ PDM_CHUNK Chunk)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Chunk->Mdl);
    ULONG Ranges = 1;
    ULONG Run = 1;
    ULONG Index;

    for (Index = 1; Index < Chunk->PageCount; Index++)
    {
        if (Pages[Index] == Pages[Index - 1] + 1 && Run < DM_RANGE_MAX_PAGES)
        {
            Run++;
        }
        else
        {
            Ranges++;
            Run = 1;
        }
    }

    return Ranges;
}

static
NTSTATUS
DmSendBalloonResponse(
    _Inout_ PDM_EXTENSION Extension,
    _In_ ULONG RangeCount,
    _In_ BOOLEAN MorePages)
{
    DmPut32(Extension->Message + 8, 0);
    DmPut32(Extension->Message + 12, (RangeCount << 1) | (MorePages ? 1 : 0));
    return DmSend(Extension, DM_BALLOON_RESPONSE, DM_RANGE_HEADER_SIZE + RangeCount * sizeof(ULONG64));
}

static
VOID
DmBalloon(
    _Inout_ PDM_EXTENSION Extension,
    _In_ ULONG Requested)
{
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Skip;
    ULONG64 Available;
    ULONG64 Committed;
    ULONG RangeCount = 0;
    ULONG Granted = 0;
    ULONG Wanted;
    PDM_CHUNK Chunk;
    PMDL Mdl;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Skip.QuadPart = 0;

    while (Granted < Requested)
    {
        DmQueryMemory(Extension, &Available, &Committed);
        Wanted = min(Requested - Granted, DM_CHUNK_PAGES);
        if (Available <= DmFloorPages(Extension) + Wanted)
            break;

        Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, (SIZE_T)Wanted << PAGE_SHIFT, MmCached,
                                      MM_DONT_ZERO_ALLOCATION);
        if (Mdl == NULL)
            break;

        Chunk = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Chunk), DM_TAG);
        if (Chunk == NULL)
        {
            MmFreePagesFromMdl(Mdl);
            ExFreePool(Mdl);
            break;
        }

        Chunk->Mdl = Mdl;
        Chunk->PageCount = MmGetMdlByteCount(Mdl) >> PAGE_SHIFT;
        if (Chunk->PageCount == 0)
        {
            ExFreePool(Mdl);
            ExFreePoolWithTag(Chunk, DM_TAG);
            break;
        }

        if (RangeCount + DmChunkRangeCount(Chunk) > DM_MAX_RANGES)
        {
            (VOID)DmSendBalloonResponse(Extension, RangeCount, TRUE);
            RangeCount = 0;
            DmMaybeSendStatus(Extension);
        }

        InsertTailList(&Extension->Chunks, &Chunk->Link);
        Extension->BalloonPages += Chunk->PageCount;
        Granted += Chunk->PageCount;
        RangeCount = DmAppendRanges(Extension, RangeCount, Chunk);

        if (Chunk->PageCount < Wanted)
            break;
    }

    (VOID)DmSendBalloonResponse(Extension, RangeCount, FALSE);
    DPRINT("DMVSC: ballooned %lu of %lu pages, %I64u held\n", Granted, Requested, Extension->BalloonPages);
}

static
VOID
DmFreeChunkPages(
    _Inout_ PDM_EXTENSION Extension,
    _Inout_ PDM_CHUNK Chunk,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Chunk->Mdl);
    PPFN_NUMBER Freed;
    ULONG Matched = 0;
    ULONG Kept = 0;
    ULONG Index;
    PMDL Mdl;

    for (Index = 0; Index < Chunk->PageCount; Index++)
    {
        if (Pages[Index] >= Start && Pages[Index] < End)
            Matched++;
    }

    if (Matched == 0)
        return;

    if (Matched == Chunk->PageCount)
    {
        MmFreePagesFromMdl(Chunk->Mdl);
        ExFreePool(Chunk->Mdl);
        Extension->BalloonPages -= Chunk->PageCount;
        Chunk->Mdl = NULL;
        Chunk->PageCount = 0;
        return;
    }

    Mdl = ExAllocatePoolWithTag(NonPagedPool, MmSizeOfMdl(NULL, (SIZE_T)Matched << PAGE_SHIFT), DM_TAG);
    if (Mdl == NULL)
    {
        DPRINT1("DMVSC: cannot return %lu ballooned pages\n", Matched);
        return;
    }

    MmInitializeMdl(Mdl, NULL, (SIZE_T)Matched << PAGE_SHIFT);
    Mdl->MdlFlags |= MDL_PAGES_LOCKED;
    Freed = MmGetMdlPfnArray(Mdl);
    Matched = 0;
    for (Index = 0; Index < Chunk->PageCount; Index++)
    {
        if (Pages[Index] >= Start && Pages[Index] < End)
            Freed[Matched++] = Pages[Index];
        else
            Pages[Kept++] = Pages[Index];
    }

    MmFreePagesFromMdl(Mdl);
    ExFreePoolWithTag(Mdl, DM_TAG);
    Chunk->PageCount = Kept;
    Chunk->Mdl->ByteCount = Kept << PAGE_SHIFT;
    Extension->BalloonPages -= Matched;
}

static
VOID
DmUnballoon(
    _Inout_ PDM_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    PLIST_ENTRY Entry;
    PDM_CHUNK Chunk;
    ULONG64 Range;
    ULONG64 Start;
    ULONG RangeCount;
    ULONG Index;
    BOOLEAN MorePages;

    if (Length < DM_RANGE_HEADER_SIZE)
        return;

    MorePages = (DmGet32(Message + 8) & 1) != 0;
    RangeCount = DmGet32(Message + 12);
    RangeCount = min(RangeCount, (Length - DM_RANGE_HEADER_SIZE) / sizeof(ULONG64));

    for (Index = 0; Index < RangeCount; Index++)
    {
        Range = DmGet64(Message + DM_RANGE_HEADER_SIZE + Index * sizeof(ULONG64));
        Start = Range & ((1ULL << 40) - 1);

        Entry = Extension->Chunks.Flink;
        while (Entry != &Extension->Chunks)
        {
            Chunk = CONTAINING_RECORD(Entry, DM_CHUNK, Link);
            Entry = Entry->Flink;
            DmFreeChunkPages(Extension, Chunk, Start, Start + (Range >> 40));
            if (Chunk->PageCount == 0)
            {
                RemoveEntryList(&Chunk->Link);
                if (Chunk->Mdl != NULL)
                    ExFreePool(Chunk->Mdl);
                ExFreePoolWithTag(Chunk, DM_TAG);
            }
        }
    }

    if (!MorePages)
    {
        (VOID)DmSend(Extension, DM_UNBALLOON_RESPONSE, DM_HEADER_SIZE);
        DPRINT("DMVSC: unballooned, %I64u pages held\n", Extension->BalloonPages);
    }
}

static
VOID
DmHandleMessage(
    _Inout_ PDM_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    USHORT Type;

    RtlCopyMemory(&Type, Message, sizeof(Type));
    switch (Type)
    {
        case DM_BALLOON_REQUEST:
            if (Length >= 12)
                DmBalloon(Extension, DmGet32(Message + 8));
            break;

        case DM_UNBALLOON_REQUEST:
            DmUnballoon(Extension, Message, Length);
            break;

        case DM_HOT_ADD_REQUEST:
            RtlZeroMemory(Extension->Message, 16);
            (VOID)DmSend(Extension, DM_HOT_ADD_RESPONSE, 16);
            break;

        case DM_ERROR:
            DPRINT1("DMVSC: host reported a protocol error\n");
            break;

        default:
            break;
    }
}

static
BOOLEAN
DmReceive(
    _Inout_ PDM_EXTENSION Extension,
    _In_ USHORT WaitType,
    _Out_opt_ PULONG64 Flags)
{
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    const UCHAR *Message;
    ULONG Length;
    ULONG Offset;
    USHORT Type;

    while (NT_SUCCESS(VmbChannelReceivePacket(Extension->Channel, Extension->Packet,
                                              sizeof(Extension->Packet), &Length)))
    {
        Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Extension->Packet;
        Offset = (ULONG)Descriptor->DataOffset8 * 8;
        if (Descriptor->Type != VMBUS_PACKET_TYPE_INBAND || Offset + DM_HEADER_SIZE > Length)
            continue;

        Message = Extension->Packet + Offset;
        Length -= Offset;
        RtlCopyMemory(&Type, Message, sizeof(Type));
        if (WaitType != 0 && Type == WaitType)
        {
            if (Flags != NULL)
                *Flags = Length >= 16 ? DmGet64(Message + 8) : 0;
            return TRUE;
        }

        if (Extension->Ready)
            DmHandleMessage(Extension, Message, Length);
    }

    return FALSE;
}

static
BOOLEAN
DmRequest(
    _Inout_ PDM_EXTENSION Extension,
    _In_ USHORT Type,
    _In_ ULONG Length,
    _In_ USHORT ResponseType,
    _Out_ PULONG64 Flags)
{
    LARGE_INTEGER Timeout;
    LARGE_INTEGER Start;
    LARGE_INTEGER Now;
    PVOID Objects[2];

    *Flags = 0;
    if (!NT_SUCCESS(DmSend(Extension, Type, Length)))
        return FALSE;

    KeQuerySystemTime(&Start);
    for (;;)
    {
        if (DmReceive(Extension, ResponseType, Flags))
            return TRUE;

        KeQuerySystemTime(&Now);
        if (Now.QuadPart - Start.QuadPart >= (LONGLONG)DM_RESPONSE_WAIT_MS * 10000)
            return FALSE;

        Objects[0] = &Extension->StopEvent;
        Objects[1] = &Extension->PacketEvent;
        Timeout.QuadPart = -100LL * 10000;
        if (KeWaitForMultipleObjects(2, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL) ==
            STATUS_WAIT_0)
        {
            return FALSE;
        }
    }
}

static
BOOLEAN
DmNegotiate(
    _Inout_ PDM_EXTENSION Extension)
{
    ULONG64 Flags;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(DmVersions); Index++)
    {
        RtlZeroMemory(Extension->Message, 16);
        DmPut32(Extension->Message + 8, DmVersions[Index]);
        DmPut32(Extension->Message + 12, Index + 1 == RTL_NUMBER_OF(DmVersions) ? 1 : 0);
        if (!DmRequest(Extension, DM_VERSION_REQUEST, 16, DM_VERSION_RESPONSE, &Flags))
            return FALSE;
        if (Flags & 1)
        {
            Extension->Version = DmVersions[Index];
            break;
        }
    }

    if (Extension->Version == 0)
    {
        DPRINT1("DMVSC: host accepted no protocol version\n");
        return FALSE;
    }

    RtlZeroMemory(Extension->Message, 32);
    DmPut64(Extension->Message + 8, 1);
    DmPut64(Extension->Message + 16, 0);
    DmPut64(Extension->Message + 24, MAXULONG64);
    if (!DmRequest(Extension, DM_CAPABILITIES_REPORT, 32, DM_CAPABILITIES_RESPONSE, &Flags) || !(Flags & 1))
    {
        DPRINT1("DMVSC: host rejected the capabilities\n");
        return FALSE;
    }

    return TRUE;
}

static
VOID
NTAPI
DmThread(
    _In_ PVOID Context)
{
    PDM_EXTENSION Extension = Context;
    SYSTEM_BASIC_INFORMATION Basic;
    LARGE_INTEGER Timeout;
    PVOID Objects[2];

    if (NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL)))
        Extension->PhysicalPages = Basic.NumberOfPhysicalPages;

    if (!DmNegotiate(Extension))
        PsTerminateSystemThread(STATUS_SUCCESS);

    Extension->Ready = TRUE;
    DPRINT("DMVSC: protocol %lu.%lu ready\n", Extension->Version >> 16, Extension->Version & 0xFFFF);
    DmSendStatus(Extension);

    Objects[0] = &Extension->StopEvent;
    Objects[1] = &Extension->PacketEvent;
    for (;;)
    {
        Timeout.QuadPart = -(LONGLONG)DM_STATUS_INTERVAL_MS * 10000;
        if (KeWaitForMultipleObjects(2, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL) ==
            STATUS_WAIT_0)
        {
            break;
        }

        (VOID)DmReceive(Extension, 0, NULL);
        DmMaybeSendStatus(Extension);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
DmChannelCallback(
    _In_opt_ PVOID Context)
{
    PDM_EXTENSION Extension = Context;

    if (Extension != NULL)
        KeSetEvent(&Extension->PacketEvent, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
DmStart(
    _Inout_ PDM_EXTENSION Extension)
{
    HANDLE ThreadHandle;
    NTSTATUS Status;

    Extension->Channel = VmbChannelFromDevice(Extension->Pdo);
    if (Extension->Channel == NULL)
        return STATUS_NO_SUCH_DEVICE;

    Status = VmbChannelOpen(Extension->Channel, DM_RING_SIZE, DmChannelCallback, Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DMVSC: VmbChannelOpen failed with 0x%lx\n", Status);
        return Status;
    }
    Extension->Open = TRUE;

    KeClearEvent(&Extension->StopEvent);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, DmThread, Extension);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode,
                                           (PVOID *)&Extension->Thread, NULL);
        ZwClose(ThreadHandle);
    }

    if (!NT_SUCCESS(Status))
    {
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }

    return Status;
}

static
VOID
DmStop(
    _Inout_ PDM_EXTENSION Extension)
{
    if (Extension->Thread != NULL)
    {
        KeSetEvent(&Extension->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Extension->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Extension->Thread);
        Extension->Thread = NULL;
    }

    Extension->Ready = FALSE;
    if (Extension->Open)
    {
        Extension->Open = FALSE;
        VmbChannelClose(Extension->Channel);
    }
}

static
NTSTATUS
DmComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
DmPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDM_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = DmStart(Extension);
            return DmComplete(Irp, Status);

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            DmStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            if (Extension->BalloonPages != 0)
                return DmComplete(Irp, STATUS_DEVICE_BUSY);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            DmStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
DmPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDM_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
DmPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDM_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
DmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDM_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(DM_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    Extension->Pdo = PhysicalDeviceObject;
    InitializeListHead(&Extension->Chunks);
    KeInitializeEvent(&Extension->StopEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Extension->PacketEvent, SynchronizationEvent, FALSE);

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (Extension->LowerDevice == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
DmUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverExtension->AddDevice = DmAddDevice;
    DriverObject->DriverUnload = DmUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DmPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DmPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DmPassThrough;
    return STATUS_SUCCESS;
}
