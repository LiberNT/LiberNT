/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume parameter blocks, mounting and the file system list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY IopDiskFileSystemQueueHead;
LIST_ENTRY IopCdRomFileSystemQueueHead;
LIST_ENTRY IopTapeFileSystemQueueHead;
LIST_ENTRY IopNetworkFileSystemQueueHead;
LIST_ENTRY IopFsNotifyChangeQueueHead;
LIST_ENTRY IopFsRegistrationOps;
KSPIN_LOCK IopVpbSpinLock;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
IoInitVpbImplementation(VOID)
{
    KeInitializeSpinLock(&IopVpbSpinLock);
}

CODE_SEG("INIT")
VOID
NTAPI
IoInitFileSystemImplementation(VOID)
{
    InitializeListHead(&IopDiskFileSystemQueueHead);
    InitializeListHead(&IopCdRomFileSystemQueueHead);
    InitializeListHead(&IopTapeFileSystemQueueHead);
    InitializeListHead(&IopNetworkFileSystemQueueHead);
    InitializeListHead(&IopFsNotifyChangeQueueHead);
    InitializeListHead(&IopFsRegistrationOps);
}

ULONG
NTAPI
IopInterlockedIncrementUlong(
    IN KSPIN_LOCK_QUEUE_NUMBER Queue,
    IN PULONG Ulong)
{
    KIRQL OldIrql;
    ULONG Old;

    UNREFERENCED_PARAMETER(Queue);

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);
    Old = *Ulong;
    (*Ulong)++;
    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return Old;
}

ULONG
NTAPI
IopInterlockedDecrementUlong(
    IN KSPIN_LOCK_QUEUE_NUMBER Queue,
    IN PULONG Ulong)
{
    KIRQL OldIrql;
    ULONG Old;

    UNREFERENCED_PARAMETER(Queue);

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);
    Old = *Ulong;
    (*Ulong)--;
    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return Old;
}

NTSTATUS
NTAPI
IopCreateVpb(
    IN PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    PVPB Vpb;

    Vpb = ExAllocatePoolZero(NonPagedPool, sizeof(VPB), TAG_VPB);
    if (Vpb == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Vpb->Type = IO_TYPE_VPB;
    Vpb->Size = sizeof(VPB);
    Vpb->RealDevice = DeviceObject;

    DeviceObject->Vpb = Vpb;
    Extension->Vpb = Vpb;

    return STATUS_SUCCESS;
}

VOID
NTAPI
IopDereferenceVpbAndFree(
    IN PVPB Vpb)
{
    KIRQL OldIrql;
    BOOLEAN Free = FALSE;

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    ASSERT(Vpb->ReferenceCount > 0);
    Vpb->ReferenceCount--;

    if (Vpb->ReferenceCount == 0 &&
        Vpb->RealDevice->Vpb != Vpb &&
        !(Vpb->Flags & VPB_PERSISTENT))
    {
        Free = TRUE;
    }

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    if (Free) ExFreePoolWithTag(Vpb, TAG_VPB);
}

BOOLEAN
IopReferenceVerifyVpb(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDEVICE_OBJECT *FileSystemDeviceObject,
    _Out_ PVPB *Vpb)
{
    PVPB LocalVpb;
    KIRQL OldIrql;

    *FileSystemDeviceObject = NULL;
    *Vpb = NULL;

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    LocalVpb = DeviceObject->Vpb;

    if (LocalVpb == NULL || !(LocalVpb->Flags & VPB_MOUNTED))
    {
        KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);
        return FALSE;
    }

    LocalVpb->ReferenceCount++;

    *FileSystemDeviceObject = LocalVpb->DeviceObject;
    *Vpb = LocalVpb;

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return TRUE;
}

PVPB
NTAPI
IopCheckVpbMounted(
    IN POPEN_PACKET OpenPacket,
    IN PDEVICE_OBJECT DeviceObject,
    IN PUNICODE_STRING RemainingName,
    OUT PNTSTATUS Status)
{
    BOOLEAN AllowRawMount;
    PVPB Vpb = NULL;
    KIRQL OldIrql;

    PAGED_CODE();

    *Status = STATUS_SUCCESS;

    if (DeviceObject->Vpb == NULL) return NULL;

    AllowRawMount = (OpenPacket->Options & IO_NO_PARAMETER_CHECKING) == 0;

    while (TRUE)
    {
        KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

        if (DeviceObject->Vpb->Flags & VPB_MOUNTED)
        {
            Vpb = DeviceObject->Vpb;
            Vpb->ReferenceCount++;

            KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

            return Vpb;
        }

        KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

        if (RemainingName->Length == 0 &&
            OpenPacket->RelatedFileObject == NULL &&
            (OpenPacket->DeleteOnly ||
             (OpenPacket->CreateOptions & FILE_NO_INTERMEDIATE_BUFFERING)))
        {
            return NULL;
        }

        *Status = IopMountVolume(DeviceObject,
                                 AllowRawMount,
                                 FALSE,
                                 FALSE,
                                 &Vpb);
        if (!NT_SUCCESS(*Status)) return NULL;

        if (Vpb != NULL) return Vpb;
    }
}

NTSTATUS
IopMountInitializeVpb(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_OBJECT AttachedDevice,
    _In_ BOOLEAN Raw)
{
    PVPB Vpb;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    Vpb = DeviceObject->Vpb;
    if (Vpb == NULL)
    {
        KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    Vpb->Flags |= VPB_MOUNTED;
    if (Raw) Vpb->Flags |= VPB_RAW_MOUNT;

    Vpb->DeviceObject = AttachedDevice;
    Vpb->ReferenceCount++;

    DeviceObject->Characteristics |= FILE_DEVICE_IS_MOUNTED;

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopSendMountVolume(
    _In_ PDEVICE_OBJECT FileSystemDeviceObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN AllowRawMount)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(FileSystemDeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_MOUNT_COMPLETION | IRP_SYNCHRONOUS_PAGING_IO;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    StackPtr->MinorFunction = IRP_MN_MOUNT_VOLUME;
    StackPtr->Flags = AllowRawMount ? SL_ALLOW_RAW_MOUNT : 0;
    StackPtr->Parameters.MountVolume.Vpb = DeviceObject->Vpb;
    StackPtr->Parameters.MountVolume.DeviceObject = DeviceObject;

    Status = IoCallDriver(FileSystemDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
IopLoadFileSystemDriver(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_PAGING_IO;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    StackPtr->MinorFunction = IRP_MN_LOAD_FILE_SYSTEM;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

static
PLIST_ENTRY
IopFileSystemQueueForDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    switch (DeviceObject->DeviceType)
    {
        case FILE_DEVICE_CD_ROM:
        case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
            return &IopCdRomFileSystemQueueHead;

        case FILE_DEVICE_TAPE:
        case FILE_DEVICE_TAPE_FILE_SYSTEM:
            return &IopTapeFileSystemQueueHead;

        case FILE_DEVICE_NETWORK:
        case FILE_DEVICE_NETWORK_FILE_SYSTEM:
            return &IopNetworkFileSystemQueueHead;

        default:
            return &IopDiskFileSystemQueueHead;
    }
}

NTSTATUS
NTAPI
IopMountVolume(
    IN PDEVICE_OBJECT DeviceObject,
    IN BOOLEAN AllowRawMount,
    IN BOOLEAN DeviceIsLocked,
    IN BOOLEAN Alertable,
    OUT PVPB *Vpb)
{
    PDEVICE_OBJECT FileSystemDeviceObject;
    PDEVICE_OBJECT AttachedDevice;
    NTSTATUS Status = STATUS_UNRECOGNIZED_VOLUME;
    PLIST_ENTRY QueueHead;
    PLIST_ENTRY Entry;
    BOOLEAN Retried = FALSE;
    KIRQL OldIrql;

    PAGED_CODE();

    *Vpb = NULL;

    if (!DeviceIsLocked)
    {
        Status = KeWaitForSingleObject(&DeviceObject->DeviceLock,
                                       Executive,
                                       KernelMode,
                                       Alertable,
                                       NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    if (DeviceObject->Vpb != NULL &&
        (DeviceObject->Vpb->Flags & VPB_MOUNTED))
    {
        *Vpb = DeviceObject->Vpb;
        (*Vpb)->ReferenceCount++;

        KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

        if (!DeviceIsLocked)
        {
            KeSetEvent(&DeviceObject->DeviceLock, IO_NO_INCREMENT, FALSE);
        }

        return STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    QueueHead = IopFileSystemQueueForDevice(DeviceObject);

    while (TRUE)
    {
        PDEVICE_OBJECT NeedsDriver = NULL;

        ExAcquireResourceSharedLite(&IopDatabaseResource, TRUE);

        Status = STATUS_UNRECOGNIZED_VOLUME;

        Entry = QueueHead->Flink;
        while (Entry != QueueHead)
        {
            FileSystemDeviceObject = CONTAINING_RECORD(Entry,
                                                       DEVICE_OBJECT,
                                                       Queue.ListEntry);
            Entry = Entry->Flink;

            if (!AllowRawMount &&
                RawFsIsRawFileSystemDeviceObject(FileSystemDeviceObject))
            {
                continue;
            }

            AttachedDevice = IoGetAttachedDevice(FileSystemDeviceObject);

            Status = IopSendMountVolume(AttachedDevice,
                                        DeviceObject,
                                        AllowRawMount);
            if (NT_SUCCESS(Status))
            {
                BOOLEAN Raw;

                Raw = RawFsIsRawFileSystemDeviceObject(FileSystemDeviceObject);

                ExReleaseResourceLite(&IopDatabaseResource);

                Status = IopMountInitializeVpb(DeviceObject,
                                               AttachedDevice,
                                               Raw);
                if (NT_SUCCESS(Status)) *Vpb = DeviceObject->Vpb;

                if (!DeviceIsLocked)
                {
                    KeSetEvent(&DeviceObject->DeviceLock,
                               IO_NO_INCREMENT,
                               FALSE);
                }

                return Status;
            }

            if (Status == STATUS_FS_DRIVER_REQUIRED && !Retried)
            {
                NeedsDriver = FileSystemDeviceObject;
                break;
            }

            if (Status != STATUS_UNRECOGNIZED_VOLUME) break;
        }

        ExReleaseResourceLite(&IopDatabaseResource);

        if (NeedsDriver == NULL) break;

        Retried = TRUE;

        IopLoadFileSystemDriver(NeedsDriver);
    }

    if (!DeviceIsLocked)
    {
        KeSetEvent(&DeviceObject->DeviceLock, IO_NO_INCREMENT, FALSE);
    }

    return Status;
}

VOID
IopNotifyAlreadyRegisteredFileSystems(
    _In_ PLIST_ENTRY ListHead,
    _In_ PDRIVER_FS_NOTIFICATION Routine,
    _In_ BOOLEAN SkipRaw)
{
    PDEVICE_OBJECT DeviceObject;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    Entry = ListHead->Flink;
    while (Entry != ListHead)
    {
        DeviceObject = CONTAINING_RECORD(Entry,
                                         DEVICE_OBJECT,
                                         Queue.ListEntry);
        Entry = Entry->Flink;

        if (SkipRaw && RawFsIsRawFileSystemDeviceObject(DeviceObject))
        {
            continue;
        }

        Routine(DeviceObject, TRUE);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoAcquireVpbSpinLock(
    _Out_ PKIRQL Irql)
{
    KeAcquireSpinLock(&IopVpbSpinLock, Irql);
}

VOID
NTAPI
IoReleaseVpbSpinLock(
    _In_ KIRQL Irql)
{
    KeReleaseSpinLock(&IopVpbSpinLock, Irql);
}

VOID
NTAPI
IoRegisterFileSystem(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIOP_FS_REGISTRATION_OPS Ops;
    PLIST_ENTRY QueueHead;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    QueueHead = IopFileSystemQueueForDevice(DeviceObject);

    ExAcquireResourceExclusiveLite(&IopDatabaseResource, TRUE);

    if (RawFsIsRawFileSystemDeviceObject(DeviceObject))
    {
        InsertTailList(QueueHead, &DeviceObject->Queue.ListEntry);
    }
    else
    {
        InsertHeadList(QueueHead, &DeviceObject->Queue.ListEntry);
    }

    ExReleaseResourceLite(&IopDatabaseResource);

    Entry = IopFsRegistrationOps.Flink;
    while (Entry != &IopFsRegistrationOps)
    {
        Ops = CONTAINING_RECORD(Entry, IOP_FS_REGISTRATION_OPS, ListEntry);
        Entry = Entry->Flink;

        Ops->NotificationRoutine(DeviceObject, TRUE);
    }
}

VOID
NTAPI
IoUnregisterFileSystem(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIOP_FS_REGISTRATION_OPS Ops;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    ExAcquireResourceExclusiveLite(&IopDatabaseResource, TRUE);

    if (DeviceObject->Queue.ListEntry.Flink != NULL)
    {
        RemoveEntryList(&DeviceObject->Queue.ListEntry);
        InitializeListHead(&DeviceObject->Queue.ListEntry);
    }

    ExReleaseResourceLite(&IopDatabaseResource);

    Entry = IopFsRegistrationOps.Flink;
    while (Entry != &IopFsRegistrationOps)
    {
        Ops = CONTAINING_RECORD(Entry, IOP_FS_REGISTRATION_OPS, ListEntry);
        Entry = Entry->Flink;

        Ops->NotificationRoutine(DeviceObject, FALSE);
    }
}

NTSTATUS
NTAPI
IoRegisterFsRegistrationChange(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_FS_NOTIFICATION DriverNotificationRoutine)
{
    PIOP_FS_REGISTRATION_OPS Ops;

    PAGED_CODE();

    Ops = ExAllocatePoolZero(NonPagedPool,
                             sizeof(IOP_FS_REGISTRATION_OPS),
                             TAG_FILE_SYSTEM);
    if (Ops == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Ops->DriverObject = DriverObject;
    Ops->NotificationRoutine = DriverNotificationRoutine;

    ExAcquireResourceExclusiveLite(&IopDatabaseResource, TRUE);
    InsertTailList(&IopFsRegistrationOps, &Ops->ListEntry);
    ExReleaseResourceLite(&IopDatabaseResource);

    IopNotifyAlreadyRegisteredFileSystems(&IopDiskFileSystemQueueHead,
                                          DriverNotificationRoutine,
                                          TRUE);
    IopNotifyAlreadyRegisteredFileSystems(&IopCdRomFileSystemQueueHead,
                                          DriverNotificationRoutine,
                                          TRUE);
    IopNotifyAlreadyRegisteredFileSystems(&IopTapeFileSystemQueueHead,
                                          DriverNotificationRoutine,
                                          TRUE);
    IopNotifyAlreadyRegisteredFileSystems(&IopNetworkFileSystemQueueHead,
                                          DriverNotificationRoutine,
                                          TRUE);

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoUnregisterFsRegistrationChange(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_FS_NOTIFICATION DriverNotificationRoutine)
{
    PIOP_FS_REGISTRATION_OPS Ops;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    ExAcquireResourceExclusiveLite(&IopDatabaseResource, TRUE);

    Entry = IopFsRegistrationOps.Flink;
    while (Entry != &IopFsRegistrationOps)
    {
        Ops = CONTAINING_RECORD(Entry, IOP_FS_REGISTRATION_OPS, ListEntry);
        Entry = Entry->Flink;

        if (Ops->DriverObject != DriverObject) continue;
        if (Ops->NotificationRoutine != DriverNotificationRoutine) continue;

        RemoveEntryList(&Ops->ListEntry);
        ExFreePoolWithTag(Ops, TAG_FILE_SYSTEM);
        break;
    }

    ExReleaseResourceLite(&IopDatabaseResource);
}

NTSTATUS
NTAPI
IoEnumerateRegisteredFiltersList(
    _Out_writes_bytes_opt_(DriverObjectListSize) PDRIVER_OBJECT *DriverObjectList,
    _In_ ULONG DriverObjectListSize,
    _Out_ PULONG ActualNumberDriverObjects)
{
    PIOP_FS_REGISTRATION_OPS Ops;
    PLIST_ENTRY Entry;
    ULONG Capacity;
    ULONG Count = 0;
    ULONG Stored = 0;

    PAGED_CODE();

    Capacity = DriverObjectListSize / (ULONG)sizeof(PDRIVER_OBJECT);

    ExAcquireResourceSharedLite(&IopDatabaseResource, TRUE);

    Entry = IopFsRegistrationOps.Flink;
    while (Entry != &IopFsRegistrationOps)
    {
        Ops = CONTAINING_RECORD(Entry, IOP_FS_REGISTRATION_OPS, ListEntry);
        Entry = Entry->Flink;

        Count++;

        if (Stored < Capacity)
        {
            ObReferenceObject(Ops->DriverObject);
            DriverObjectList[Stored] = Ops->DriverObject;
            Stored++;
        }
    }

    ExReleaseResourceLite(&IopDatabaseResource);

    *ActualNumberDriverObjects = Count;

    if (Count > Capacity)
    {
        while (Stored != 0)
        {
            Stored--;
            ObDereferenceObject(DriverObjectList[Stored]);
        }

        return STATUS_BUFFER_TOO_SMALL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoVerifyVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN AllowRawMount)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT FileSystem;
    NTSTATUS Status;
    KEVENT Event;
    PVPB Vpb;
    PIRP Irp;

    PAGED_CODE();

    Status = KeWaitForSingleObject(&DeviceObject->DeviceLock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (!IopReferenceVerifyVpb(DeviceObject, &FileSystem, &Vpb))
    {
        Status = IopMountVolume(DeviceObject, AllowRawMount, TRUE, FALSE, &Vpb);

        KeSetEvent(&DeviceObject->DeviceLock, IO_NO_INCREMENT, FALSE);

        return Status;
    }

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(FileSystem->StackSize, FALSE);
    if (Irp == NULL)
    {
        IopDereferenceVpbAndFree(Vpb);
        KeSetEvent(&DeviceObject->DeviceLock, IO_NO_INCREMENT, FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_PAGING_IO | IRP_MOUNT_COMPLETION;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    StackPtr->MinorFunction = IRP_MN_VERIFY_VOLUME;
    StackPtr->Flags = AllowRawMount ? SL_ALLOW_RAW_MOUNT : 0;
    StackPtr->Parameters.VerifyVolume.Vpb = Vpb;
    StackPtr->Parameters.VerifyVolume.DeviceObject = DeviceObject;

    Status = IoCallDriver(FileSystem, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    IopDereferenceVpbAndFree(Vpb);

    KeSetEvent(&DeviceObject->DeviceLock, IO_NO_INCREMENT, FALSE);

    return Status;
}

PDEVICE_OBJECT
NTAPI
IoGetBaseFileSystemDeviceObject(
    _In_ PFILE_OBJECT FileObject)
{
    if (FileObject->Vpb != NULL && FileObject->Vpb->DeviceObject != NULL)
    {
        return FileObject->Vpb->DeviceObject;
    }

    if (FileObject->DeviceObject->Vpb != NULL &&
        FileObject->DeviceObject->Vpb->DeviceObject != NULL)
    {
        return FileObject->DeviceObject->Vpb->DeviceObject;
    }

    return FileObject->DeviceObject;
}

NTSTATUS
NTAPI
IoGetDiskDeviceObject(
    _In_ PDEVICE_OBJECT FileSystemDeviceObject,
    _Out_ PDEVICE_OBJECT *DiskDeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    KIRQL OldIrql;
    PVPB Vpb;

    Extension = IoGetDevObjExtension(FileSystemDeviceObject);

    if (!(FileSystemDeviceObject->Flags & DO_DEVICE_HAS_NAME) &&
        Extension->Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    Vpb = Extension->Vpb;
    if (Vpb == NULL || Vpb->RealDevice == NULL)
    {
        KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);
        return STATUS_INVALID_PARAMETER;
    }

    ObReferenceObject(Vpb->RealDevice);
    *DiskDeviceObject = Vpb->RealDevice;

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoSetSystemPartition(
    _In_ PUNICODE_STRING VolumeNameString)
{
    PAGED_CODE();

    IopStoreSystemPartitionInformation(VolumeNameString, VolumeNameString);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoVolumeDeviceToDosName(
    _In_ PVOID VolumeDeviceObject,
    _Out_ PUNICODE_STRING DosName)
{
    PDEVICE_OBJECT DeviceObject = VolumeDeviceObject;
    OBJECT_NAME_INFORMATION *NameInfo = NULL;
    UNICODE_STRING LinkTarget;
    UNICODE_STRING LinkName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    WCHAR DriveName[] = L"\\??\\A:";
    NTSTATUS Status;
    ULONG Length = 0;
    PWCHAR Buffer;
    WCHAR Letter;
    HANDLE Handle;

    PAGED_CODE();

    Status = ObQueryNameString(DeviceObject, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH &&
        Status != STATUS_BUFFER_OVERFLOW &&
        Status != STATUS_BUFFER_TOO_SMALL)
    {
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    NameInfo = ExAllocatePoolZero(PagedPool, Length, TAG_IO_NAME);
    if (NameInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObQueryNameString(DeviceObject, NameInfo, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
        return Status;
    }

    LinkTarget.Buffer = ExAllocatePoolZero(PagedPool,
                                           MAXIMUM_FILENAME_LENGTH *
                                               sizeof(WCHAR),
                                           TAG_IO_NAME);
    if (LinkTarget.Buffer == NULL)
    {
        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    LinkTarget.Length = 0;
    LinkTarget.MaximumLength = MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR);

    for (Letter = L'A'; Letter <= L'Z'; Letter++)
    {
        DriveName[4] = Letter;
        RtlInitUnicodeString(&LinkName, DriveName);

        InitializeObjectAttributes(&ObjectAttributes,
                                   &LinkName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        Status = ZwOpenSymbolicLinkObject(&Handle,
                                          SYMBOLIC_LINK_QUERY,
                                          &ObjectAttributes);
        if (!NT_SUCCESS(Status)) continue;

        LinkTarget.Length = 0;

        Status = ZwQuerySymbolicLinkObject(Handle, &LinkTarget, NULL);
        ZwClose(Handle);

        if (!NT_SUCCESS(Status)) continue;

        if (!RtlEqualUnicodeString(&LinkTarget, &NameInfo->Name, TRUE))
        {
            continue;
        }

        Buffer = ExAllocatePoolZero(PagedPool,
                                    3 * sizeof(WCHAR),
                                    TAG_DEV2DOS);
        if (Buffer == NULL)
        {
            ExFreePoolWithTag(LinkTarget.Buffer, TAG_IO_NAME);
            ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Buffer[0] = Letter;
        Buffer[1] = L':';
        Buffer[2] = UNICODE_NULL;

        DosName->Buffer = Buffer;
        DosName->Length = 2 * sizeof(WCHAR);
        DosName->MaximumLength = 3 * sizeof(WCHAR);

        ExFreePoolWithTag(LinkTarget.Buffer, TAG_IO_NAME);
        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

        return STATUS_SUCCESS;
    }

    ExFreePoolWithTag(LinkTarget.Buffer, TAG_IO_NAME);
    ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS
NTAPI
IoVolumeDeviceToGuidPath(
    _In_ PVOID VolumeDeviceObject,
    _Out_ PUNICODE_STRING GuidPath)
{
    PDEVICE_OBJECT DeviceObject = VolumeDeviceObject;
    PDEVICE_NODE DeviceNode;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_PARAMETER;

    /* TODO: the volume manager does not publish a GUID path yet */
    UNREFERENCED_PARAMETER(GuidPath);

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS
NTAPI
IoVolumeDeviceToGuid(
    _In_ PVOID VolumeDeviceObject,
    _Out_ GUID *Guid)
{
    UNICODE_STRING GuidPath;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&GuidPath, NULL, 0);

    Status = IoVolumeDeviceToGuidPath(VolumeDeviceObject, &GuidPath);
    if (!NT_SUCCESS(Status)) return Status;

    Status = RtlGUIDFromString(&GuidPath, Guid);

    ExFreePool(GuidPath.Buffer);

    return Status;
}
