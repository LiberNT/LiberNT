/*
 * PROJECT:     LiberNT Storage
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter of the boot RAM volume that grows it with write space taken from memory as it is written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddvol.h>

#include <expandcore.h>
#include "rdscore.h"

#define NDEBUG
#include <debug.h>

#define RDS_TAG                 'csdR'
#define RDS_DEFAULT_SPACE_MB    512
#define RDS_MAXIMUM_SPACE_MB    4096

typedef struct _RDS_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    IO_REMOVE_LOCK RemoveLock;
    RDS_STORE Store;
    BOOLEAN Active;
} RDS_EXTENSION, *PRDS_EXTENSION;

static ULONGLONG RdsSpace;

static
void*
RdsAllocate(
    void* Context)
{
    UNREFERENCED_PARAMETER(Context);
    return ExAllocatePoolZero(NonPagedPoolNx, RDS_CHUNK_SIZE, RDS_TAG);
}

static
void
RdsFree(
    void* Context,
    void* Chunk)
{
    UNREFERENCED_PARAMETER(Context);
    ExFreePoolWithTag(Chunk, RDS_TAG);
}

static
void*
RdsLoad(
    void* Context,
    void* volatile* Slot)
{
    UNREFERENCED_PARAMETER(Context);
    return ReadPointerAcquire((PVOID volatile*)Slot);
}

static
void*
RdsPublish(
    void* Context,
    void* volatile* Slot,
    void* Chunk)
{
    UNREFERENCED_PARAMETER(Context);
    return InterlockedCompareExchangePointer((PVOID volatile*)Slot, Chunk, NULL);
}

static const RDS_OPS RdsOps = { NULL, RdsAllocate, RdsFree, RdsLoad, RdsPublish };

static
NTSTATUS
RdsCallLower(
    _In_ PRDS_EXTENSION Extension,
    _In_ UCHAR MajorFunction,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Inout_updates_bytes_(Length) PVOID Buffer)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Start;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Start.QuadPart = (LONGLONG)Offset;
    Irp = IoBuildSynchronousFsdRequest(MajorFunction, Extension->Lower, Buffer, Length, &Start, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(Extension->Lower, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) && IoStatus.Information != Length)
        Status = STATUS_UNEXPECTED_IO_ERROR;
    return Status;
}

static
int
RdsLowerIo(
    void* Context,
    uint64_t Offset,
    uint32_t Length,
    void* Buffer,
    int Write)
{
    return NT_SUCCESS(RdsCallLower((PRDS_EXTENSION)Context,
                                   Write ? IRP_MJ_WRITE : IRP_MJ_READ,
                                   Offset,
                                   Length,
                                   Buffer));
}

static
NTSTATUS
RdsQueryLowerLength(
    _In_ PRDS_EXTENSION Extension,
    _Out_ PULONGLONG Length)
{
    GET_LENGTH_INFORMATION Information;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *Length = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_LENGTH_INFO,
                                        Extension->Lower,
                                        NULL,
                                        0,
                                        &Information,
                                        sizeof(Information),
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Extension->Lower, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) && (IoStatus.Information < sizeof(Information) || Information.Length.QuadPart <= 0))
        Status = STATUS_UNEXPECTED_IO_ERROR;
    if (NT_SUCCESS(Status))
        *Length = (ULONGLONG)Information.Length.QuadPart;
    return Status;
}

static
ULONGLONG
RdsLimitToMemory(
    _In_ ULONGLONG Space,
    _In_ ULONGLONG Loaded)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    ULONGLONG Memory = 0;
    ULONG Index;

    Ranges = MmGetPhysicalMemoryRanges();
    if (!Ranges)
        return Space;

    for (Index = 0; Ranges[Index].BaseAddress.QuadPart != 0 || Ranges[Index].NumberOfBytes.QuadPart != 0; Index++)
        Memory += (ULONGLONG)Ranges[Index].NumberOfBytes.QuadPart;
    ExFreePool(Ranges);

    if (Memory == 0)
        return Space;
    return Memory > Loaded ? min(Space, (Memory - Loaded) / 2) : 0;
}

static
VOID
RdsStart(
    _In_ PRDS_EXTENSION Extension)
{
    void* volatile* Chunks;
    PUCHAR BootSector = NULL;
    PVOID Workspace = NULL;
    ULONGLONG Physical;
    ULONGLONG Logical;
    ULONGLONG VolumeSectors;
    uint32_t SectorSize;
    uint32_t Count;
    NTSTATUS Status;
    int Result;

    PAGED_CODE();
    ASSERT(!Extension->Active);

    if (RdsSpace == 0)
        return;

    Status = RdsQueryLowerLength(Extension, &Physical);
    if (!NT_SUCCESS(Status))
        return;

    BootSector = ExAllocatePoolZero(NonPagedPoolNx, RDS_BOOT_SECTOR, RDS_TAG);
    if (!BootSector)
        return;

    Status = RdsCallLower(Extension, IRP_MJ_READ, 0, RDS_BOOT_SECTOR, BootSector);
    Result = NT_SUCCESS(Status)
        ? RdsPlanGrowth(BootSector, Physical, RdsLimitToMemory(RdsSpace, Physical), &Logical, &VolumeSectors, &SectorSize)
        : RDS_IO_ERROR;
    ExFreePoolWithTag(BootSector, RDS_TAG);
    if (Result != RDS_OK)
    {
        DPRINT("RDSCRATCH: the boot volume stays at its size (%d)\n", Result);
        return;
    }

    Count = RdsChunkCount(Physical, Logical);
    if (Count == 0)
        return;
    Chunks = ExAllocatePoolZero(NonPagedPoolNx, (SIZE_T)Count * sizeof(PVOID), RDS_TAG);
    Workspace = ExAllocatePoolZero(NonPagedPoolNx, EXPAND_WORKSPACE_BYTES, RDS_TAG);
    if (!Chunks || !Workspace)
    {
        if (Chunks)
            ExFreePoolWithTag((PVOID)Chunks, RDS_TAG);
        if (Workspace)
            ExFreePoolWithTag(Workspace, RDS_TAG);
        return;
    }

    RdsStoreInit(&Extension->Store, Physical, Logical, Chunks, &RdsOps);
    ASSERT(Extension->Store.ChunkCount == Count);

    /* Writes past the loaded part may land before the growth fails, so the space stays served either way */
    Extension->Active = TRUE;
    Result = RdsGrowVolume(&Extension->Store, RdsLowerIo, Extension, VolumeSectors, SectorSize, Workspace);
    ExFreePoolWithTag(Workspace, RDS_TAG);

    DPRINT1("RDSCRATCH: boot volume %I64u MB with %I64u MB of write space (%d)\n",
            Physical >> 20, (Logical - Physical) >> 20, Result);
}

static
VOID
RdsStop(
    _In_ PRDS_EXTENSION Extension)
{
    if (!Extension->Active)
        return;

    Extension->Active = FALSE;
    RdsStoreRelease(&Extension->Store);
    ExFreePoolWithTag((PVOID)Extension->Store.Chunks, RDS_TAG);
    Extension->Store.Chunks = NULL;
}

static
NTSTATUS
RdsPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Extension->Lower, Irp);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
RdsHeadCompleted(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    if (NT_SUCCESS(Irp->IoStatus.Status))
        Irp->IoStatus.Information += (ULONG_PTR)Context;

    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static
PUCHAR
RdsSystemBuffer(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    if (Irp->MdlAddress)
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
    if (DeviceObject->Flags & DO_BUFFERED_IO)
        return Irp->AssociatedIrp.SystemBuffer;
    return Irp->RequestorMode == KernelMode ? Irp->UserBuffer : NULL;
}

static
NTSTATUS
NTAPI
RdsReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION Next;
    ULONGLONG Offset;
    ULONGLONG End;
    PUCHAR Buffer;
    ULONG Length;
    ULONG Head;
    NTSTATUS Status;
    BOOLEAN Write = Stack->MajorFunction == IRP_MJ_WRITE;

    if (!Extension->Active)
        return RdsPassThrough(DeviceObject, Irp);

    Offset = (ULONGLONG)(Write ? Stack->Parameters.Write.ByteOffset.QuadPart : Stack->Parameters.Read.ByteOffset.QuadPart);
    Length = Write ? Stack->Parameters.Write.Length : Stack->Parameters.Read.Length;
    End = Offset + Length;
    if (End <= Extension->Store.Physical || Length == 0)
        return RdsPassThrough(DeviceObject, Irp);

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if ((LONGLONG)Offset < 0 || End < Offset || End > Extension->Store.Logical)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Release;
    }

    Buffer = RdsSystemBuffer(DeviceObject, Irp);
    if (!Buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Release;
    }

    Head = Offset < Extension->Store.Physical ? (ULONG)(Extension->Store.Physical - Offset) : 0;
    ASSERT(Head < Length);
    switch (RdsStoreTransfer(&Extension->Store, Offset + Head, Length - Head, Buffer + Head, Write))
    {
        case RDS_OK:
            Status = STATUS_SUCCESS;
            break;
        case RDS_NO_MEMORY:
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }
    if (!NT_SUCCESS(Status))
        goto Release;

    if (Head == 0)
    {
        Irp->IoStatus.Information = Length;
        goto Release;
    }

    IoCopyCurrentIrpStackLocationToNext(Irp);
    Next = IoGetNextIrpStackLocation(Irp);
    if (Write)
        Next->Parameters.Write.Length = Head;
    else
        Next->Parameters.Read.Length = Head;
    IoSetCompletionRoutine(Irp, RdsHeadCompleted, (PVOID)(ULONG_PTR)(Length - Head), TRUE, TRUE, TRUE);
    return IoCallDriver(Extension->Lower, Irp);

Release:
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
Complete:
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static
VOID
RdsPatchSize(
    _In_ PRDS_EXTENSION Extension,
    _In_ ULONG IoControlCode,
    _Inout_ PVOID Output,
    _In_ ULONG_PTR Returned)
{
    LONGLONG Logical = (LONGLONG)Extension->Store.Logical;
    LONGLONG Physical = (LONGLONG)Extension->Store.Physical;
    PDISK_GEOMETRY Geometry;
    ULONGLONG CylinderBytes;
    ULONG Index;

    switch (IoControlCode)
    {
        case IOCTL_DISK_GET_LENGTH_INFO:
            if (Returned >= sizeof(GET_LENGTH_INFORMATION))
                ((PGET_LENGTH_INFORMATION)Output)->Length.QuadPart = Logical;
            break;

        case IOCTL_DISK_GET_PARTITION_INFO:
            if (Returned >= sizeof(PARTITION_INFORMATION))
                ((PPARTITION_INFORMATION)Output)->PartitionLength.QuadPart = Logical;
            break;

        case IOCTL_DISK_GET_PARTITION_INFO_EX:
            if (Returned >= sizeof(PARTITION_INFORMATION_EX))
                ((PPARTITION_INFORMATION_EX)Output)->PartitionLength.QuadPart = Logical;
            break;

        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
            if (Returned < sizeof(DISK_GEOMETRY))
                break;
            Geometry = (PDISK_GEOMETRY)Output;
            CylinderBytes = (ULONGLONG)Geometry->TracksPerCylinder * Geometry->SectorsPerTrack * Geometry->BytesPerSector;
            if (CylinderBytes != 0)
                Geometry->Cylinders.QuadPart = (LONGLONG)((ULONGLONG)Logical / CylinderBytes);
            if (IoControlCode == IOCTL_DISK_GET_DRIVE_GEOMETRY_EX &&
                Returned >= FIELD_OFFSET(DISK_GEOMETRY_EX, Data))
            {
                ((PDISK_GEOMETRY_EX)Output)->DiskSize.QuadPart = Logical;
            }
            break;

        case IOCTL_STORAGE_READ_CAPACITY:
            if (Returned >= sizeof(STORAGE_READ_CAPACITY) && ((PSTORAGE_READ_CAPACITY)Output)->BlockLength != 0)
            {
                ((PSTORAGE_READ_CAPACITY)Output)->DiskLength.QuadPart = Logical;
                ((PSTORAGE_READ_CAPACITY)Output)->NumberOfBlocks.QuadPart =
                    Logical / ((PSTORAGE_READ_CAPACITY)Output)->BlockLength;
            }
            break;

        case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
            if (Returned < sizeof(VOLUME_DISK_EXTENTS))
                break;
            for (Index = 0;
                 Index < ((PVOLUME_DISK_EXTENTS)Output)->NumberOfDiskExtents &&
                 FIELD_OFFSET(VOLUME_DISK_EXTENTS, Extents) + (Index + 1) * sizeof(DISK_EXTENT) <= Returned;
                 Index++)
            {
                if (((PVOLUME_DISK_EXTENTS)Output)->Extents[Index].ExtentLength.QuadPart == Physical)
                    ((PVOLUME_DISK_EXTENTS)Output)->Extents[Index].ExtentLength.QuadPart = Logical;
            }
            break;
    }
}

static
NTSTATUS
NTAPI
RdsDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS Status;

    if (!Extension->Active)
        return RdsPassThrough(DeviceObject, Irp);

    switch (IoControlCode)
    {
        case IOCTL_DISK_GET_LENGTH_INFO:
        case IOCTL_DISK_GET_PARTITION_INFO:
        case IOCTL_DISK_GET_PARTITION_INFO_EX:
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
        case IOCTL_STORAGE_READ_CAPACITY:
        case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
            break;
        default:
            /* TODO: release the chunks of trimmed ranges once the file system sends trim requests */
            return RdsPassThrough(DeviceObject, Irp);
    }

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (IoForwardIrpSynchronously(Extension->Lower, Irp))
        Status = Irp->IoStatus.Status;
    else
        Status = STATUS_UNSUCCESSFUL;
    if (NT_SUCCESS(Status))
        RdsPatchSize(Extension, IoControlCode, Irp->AssociatedIrp.SystemBuffer, Irp->IoStatus.Information);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
RdsPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(Extension->Lower, Irp);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
RdsPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRDS_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_REMOVE_DEVICE:
            IoReleaseRemoveLockAndWait(&Extension->RemoveLock, Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->Lower, Irp);
            RdsStop(Extension);
            IoDetachDevice(Extension->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->Lower, Irp);
            IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
            return Status;
    }
}

static
NTSTATUS
NTAPI
RdsDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    return RdsPassThrough(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
RdsAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PRDS_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Lower;
    NTSTATUS Status;

    PAGED_CODE();

    Lower = IoGetAttachedDeviceReference(PhysicalDeviceObject);
    Status = IoCreateDevice(DriverObject,
                            sizeof(RDS_EXTENSION),
                            NULL,
                            Lower->DeviceType,
                            Lower->Characteristics & ~FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    ObDereferenceObject(Lower);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    IoInitializeRemoveLock(&Extension->RemoveLock, RDS_TAG, 0, 0);

    Extension->Lower = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!Extension->Lower)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= Extension->Lower->Flags & (DO_DIRECT_IO | DO_BUFFERED_IO | DO_POWER_PAGABLE);
    DeviceObject->Characteristics |= Extension->Lower->Characteristics & FILE_DEVICE_SECURE_OPEN;
    DeviceObject->AlignmentRequirement = Extension->Lower->AlignmentRequirement;

    /* The volume grows before any file system can mount it through this stack */
    RdsStart(Extension);

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
ULONGLONG
RdsQuerySpace(
    _In_ PUNICODE_STRING RegistryPath)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];
    ULONG SpaceMb = RDS_DEFAULT_SPACE_MB;
    ULONG Default = RDS_DEFAULT_SPACE_MB;
    UNICODE_STRING Parameters;
    WCHAR Buffer[260];

    RtlInitEmptyUnicodeString(&Parameters, Buffer, sizeof(Buffer));
    if (NT_SUCCESS(RtlAppendUnicodeStringToString(&Parameters, RegistryPath)) &&
        NT_SUCCESS(RtlAppendUnicodeToString(&Parameters, L"\\Parameters")))
    {
        RtlZeroMemory(Table, sizeof(Table));
        Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
        Table[0].Name = L"ScratchSpace";
        Table[0].EntryContext = &SpaceMb;
        Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD;
        Table[0].DefaultData = &Default;
        Table[0].DefaultLength = sizeof(Default);
        (VOID)RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, Parameters.Buffer, Table, NULL, NULL);
    }

    return (ULONGLONG)min(SpaceMb, RDS_MAXIMUM_SPACE_MB) << 20;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    RdsSpace = RdsQuerySpace(RegistryPath);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = RdsDispatch;
    DriverObject->MajorFunction[IRP_MJ_READ] = RdsReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = RdsReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RdsDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = RdsPower;
    DriverObject->MajorFunction[IRP_MJ_PNP] = RdsPnp;
    DriverObject->DriverExtension->AddDevice = RdsAddDevice;
    return STATUS_SUCCESS;
}
