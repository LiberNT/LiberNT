/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume mount, the device interface of lwext4, the core lock and the periodic flush
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_SUPERBLOCK_MAGIC_VALUE 0xEF53

VOID
Ext4LockCore(
    _In_ PEXT4_VCB Vcb)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->CoreLock, TRUE);
}

VOID
Ext4UnlockCore(
    _In_ PEXT4_VCB Vcb)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->CoreLock));
    ASSERT(Vcb->Core.TransDepth == 0);
    ExReleaseResourceLite(&Vcb->CoreLock);
    KeLeaveCriticalRegion();
}

NTSTATUS
Ext4Status(
    _In_ PEXT4_VCB Vcb,
    _In_ int Error)
{
    switch (Error)
    {
        case EOK: return STATUS_SUCCESS;
        case ENOENT: return STATUS_OBJECT_NAME_NOT_FOUND;
        case EEXIST: return STATUS_OBJECT_NAME_COLLISION;
        case ENOSPC: return STATUS_DISK_FULL;
        case ENOMEM: return STATUS_INSUFFICIENT_RESOURCES;
        case ENOTDIR: return STATUS_NOT_A_DIRECTORY;
        case EISDIR: return STATUS_FILE_IS_A_DIRECTORY;
        case ENOTEMPTY: return STATUS_DIRECTORY_NOT_EMPTY;
        case EROFS: return STATUS_MEDIA_WRITE_PROTECTED;
        case ENAMETOOLONG: return STATUS_OBJECT_NAME_INVALID;
        case EMLINK: return STATUS_TOO_MANY_LINKS;
        case EPERM:
        case EACCES: return STATUS_ACCESS_DENIED;
        case ENODATA: return STATUS_NOT_FOUND;
        case ENOTSUP: return STATUS_NOT_SUPPORTED;
        case ERANGE: return STATUS_BUFFER_TOO_SMALL;
        case EFBIG: return STATUS_FILE_TOO_LARGE;
        case ENXIO: return STATUS_VOLUME_DISMOUNTED;
        case EIO:
            /* lwext4 reports a failed device request and a structure it cannot trust with the same code */
            return NT_SUCCESS(Vcb->DeviceStatus) ? STATUS_FILE_CORRUPT_ERROR : Vcb->DeviceStatus;
        default: return STATUS_INVALID_PARAMETER;
    }
}

static
int
Ext4DeviceOpen(
    struct ext4_blockdev *Device)
{
    UNREFERENCED_PARAMETER(Device);
    return EOK;
}

static
int
Ext4DeviceClose(
    struct ext4_blockdev *Device)
{
    UNREFERENCED_PARAMETER(Device);
    return EOK;
}

static
int
Ext4DeviceRead(
    struct ext4_blockdev *Device,
    void *Buffer,
    uint64_t Sector,
    uint32_t Count)
{
    PEXT4_VCB Vcb = Device->bdif->p_user;
    NTSTATUS Status;

    Status = Ext4ReadDevice(Vcb->TargetDeviceObject, Sector * Vcb->DeviceSectorSize, Buffer, Count * Vcb->DeviceSectorSize, TRUE);
    if (NT_SUCCESS(Status)) return EOK;

    Vcb->DeviceStatus = Status;
    return EIO;
}

static
int
Ext4DeviceWrite(
    struct ext4_blockdev *Device,
    const void *Buffer,
    uint64_t Sector,
    uint32_t Count)
{
    PEXT4_VCB Vcb = Device->bdif->p_user;
    NTSTATUS Status;

    ASSERT(Vcb->Writable || !Vcb->CoreMounted);
    Status = Ext4WriteDevice(Vcb->TargetDeviceObject, Sector * Vcb->DeviceSectorSize, Buffer, Count * Vcb->DeviceSectorSize);
    if (NT_SUCCESS(Status)) return EOK;

    Vcb->DeviceStatus = Status;
    return EIO;
}

static
void
Ext4CoreNow(
    E4_VOLUME *Volume,
    E4_TIME *Time)
{
    UNREFERENCED_PARAMETER(Volume);
    Ext4CurrentTime(Time);
}

NTSTATUS
Ext4FlushVolume(
    _In_ PEXT4_VCB Vcb)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!Vcb->CoreMounted || !Vcb->Writable) return STATUS_SUCCESS;

    Ext4LockCore(Vcb);
    if (Vcb->CoreMounted)
    {
        InterlockedExchange(&Vcb->CoreDirty, 0);
        Status = Ext4Status(Vcb, E4Sync(&Vcb->Core));
    }
    Ext4UnlockCore(Vcb);

    if (NT_SUCCESS(Status)) Status = Ext4FlushDevice(Vcb->TargetDeviceObject);
    return Status;
}

static
VOID
NTAPI
Ext4FlushWorker(
    _In_ PVOID Context)
{
    PEXT4_VCB Vcb = Context;

    ASSERT(Vcb->Signature == EXT4_VCB_SIGNATURE);
    Ext4FlushVolume(Vcb);
    InterlockedExchange(&Vcb->FlushQueued, 0);
    KeSetEvent(&Vcb->FlushIdle, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
Ext4FlushDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PEXT4_VCB Vcb = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    ASSERT(Vcb != NULL);
    if (InterlockedCompareExchange(&Vcb->FlushQueued, 1, 0) != 0) return;
    KeClearEvent(&Vcb->FlushIdle);
    ExQueueWorkItem(&Vcb->FlushWork, DelayedWorkQueue);
}

/* A change reaches the journal when it is made; the timer bounds how long it waits for its place on the disk */
VOID
Ext4MarkCoreDirty(
    _In_ PEXT4_VCB Vcb)
{
    LARGE_INTEGER Due;

    if (InterlockedExchange(&Vcb->CoreDirty, 1) != 0) return;
    Due.QuadPart = -(LONGLONG)EXT4_FLUSH_SECONDS * 10000000;
    KeSetTimer(&Vcb->FlushTimer, Due, &Vcb->FlushDpc);
}

VOID
Ext4StopCore(
    _In_ PEXT4_VCB Vcb)
{
    PAGED_CODE();

    KeCancelTimer(&Vcb->FlushTimer);
    KeFlushQueuedDpcs();
    KeWaitForSingleObject(&Vcb->FlushIdle, Executive, KernelMode, FALSE, NULL);

    Ext4LockCore(Vcb);
    if (Vcb->CoreMounted)
    {
        E4Unmount(&Vcb->Core);
        Vcb->CoreMounted = FALSE;
    }
    Ext4UnlockCore(Vcb);

    if (Vcb->Writable) Ext4FlushDevice(Vcb->TargetDeviceObject);
}

VOID
Ext4SetVolumeLabel(
    _In_ PEXT4_VCB Vcb,
    _Inout_ PVPB Vpb)
{
    UNICODE_STRING Label;
    E4_STATFS Stat;
    SIZE_T Length;
    ULONG Serial = 0, Index;

    E4StatFs(&Vcb->Core, &Stat);
    for (Index = 0; Index < sizeof(Stat.Uuid); Index++)
        Serial ^= (ULONG)Stat.Uuid[Index] << ((Index % 4) * 8);
    Vpb->SerialNumber = Serial;
    Vpb->VolumeLabelLength = 0;

    for (Length = 0; Length < 16 && Stat.Label[Length] != 0; Length++);
    if (Length == 0 || !NT_SUCCESS(Ext4Utf8ToUnicode(Stat.Label, (ULONG)Length, &Label))) return;

    Vpb->VolumeLabelLength = min(Label.Length, MAXIMUM_VOLUME_LABEL_LENGTH);
    RtlCopyMemory(Vpb->VolumeLabel, Label.Buffer, Vpb->VolumeLabelLength);
    ExFreePoolWithTag(Label.Buffer, TAG_EXT4);
}

static
VOID
Ext4DeleteVcb(
    _In_ PEXT4_VCB Vcb)
{
    PDEVICE_OBJECT VolumeDevice = Vcb->VolumeDeviceObject;
    KIRQL OldIrql;
    ULONG Index;

    ASSERT(Vcb->References == 0);
    for (Index = 0; Index < EXT4_FCB_BUCKETS; Index++)
        ASSERT(IsListEmpty(&Vcb->FcbTable[Index]));

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Ext4Global.Resource, TRUE);
    if (Vcb->VcbLinks.Flink != NULL) RemoveEntryList(&Vcb->VcbLinks);
    ExReleaseResourceLite(&Ext4Global.Resource);
    KeLeaveCriticalRegion();

    IoAcquireVpbSpinLock(&OldIrql);
    if (Vcb->Vpb != NULL && Vcb->Vpb->DeviceObject == VolumeDevice)
    {
        Vcb->Vpb->DeviceObject = NULL;
        Vcb->Vpb->Flags &= ~VPB_MOUNTED;
    }
    IoReleaseVpbSpinLock(OldIrql);

    Ext4StopCore(Vcb);
    if (Vcb->SectorBuffer != NULL) ExFreePoolWithTag(Vcb->SectorBuffer, TAG_EXT4);
    if (Vcb->NotifySync != NULL) FsRtlNotifyUninitializeSync(&Vcb->NotifySync);
    ExDeleteResourceLite(&Vcb->CoreLock);
    ExDeleteResourceLite(&Vcb->Resource);
    Vcb->Signature = 0;

    IoDeleteDevice(VolumeDevice);
}

VOID
Ext4ReferenceVcb(
    _In_ PEXT4_VCB Vcb)
{
    LONG References;

    ASSERT(Vcb->Signature == EXT4_VCB_SIGNATURE);
    References = InterlockedIncrement(&Vcb->References);
    ASSERT(References > 1);
    UNREFERENCED_PARAMETER(References);
}

VOID
Ext4DereferenceVcb(
    _In_ PEXT4_VCB Vcb)
{
    LONG References;

    ASSERT(Vcb->Signature == EXT4_VCB_SIGNATURE);
    References = InterlockedDecrement(&Vcb->References);
    ASSERT(References >= 0);
    if (References == 0) Ext4DeleteVcb(Vcb);
}

static
NTSTATUS
Ext4ProbeSuperblock(
    _In_ PDEVICE_OBJECT Device,
    _In_ ULONG SectorSize)
{
    PUCHAR Buffer;
    NTSTATUS Status;
    USHORT Magic;
    ULONG Length = ALIGN_UP_BY(2048, SectorSize);

    Buffer = ExAllocatePoolUninitialized(NonPagedPoolNx, Length, TAG_EXT4);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = Ext4ReadDevice(Device, 0, Buffer, Length, TRUE);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(&Magic, Buffer + 1024 + 0x38, sizeof(Magic));
        if (Magic != EXT4_SUPERBLOCK_MAGIC_VALUE) Status = STATUS_UNRECOGNIZED_VOLUME;
    }
    ExFreePoolWithTag(Buffer, TAG_EXT4);
    return Status;
}

NTSTATUS
Ext4MountVolume(
    _In_ PDEVICE_OBJECT TargetDevice,
    _In_ PVPB Vpb)
{
    GET_LENGTH_INFORMATION LengthInfo;
    DISK_GEOMETRY Geometry;
    PDEVICE_OBJECT VolumeDevice = NULL;
    BOOLEAN ReadOnly;
    E4_ATTR RootAttr;
    PEXT4_VCB Vcb;
    NTSTATUS Status;
    ULONG SectorSize = 512;
    ULONG Index;
    int Error;

    PAGED_CODE();

    if (TargetDevice->DeviceType != FILE_DEVICE_DISK && TargetDevice->DeviceType != FILE_DEVICE_VIRTUAL_DISK)
        return STATUS_UNRECOGNIZED_VOLUME;

    if (NT_SUCCESS(Ext4DeviceControl(TargetDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &Geometry, sizeof(Geometry), TRUE)) &&
        Geometry.BytesPerSector != 0)
    {
        SectorSize = Geometry.BytesPerSector;
    }
    if (SectorSize > 4096 || (SectorSize & (SectorSize - 1)) != 0) return STATUS_UNRECOGNIZED_VOLUME;

    Status = Ext4ProbeSuperblock(TargetDevice, SectorSize);
    if (!NT_SUCCESS(Status)) return (Status == STATUS_INSUFFICIENT_RESOURCES) ? Status : STATUS_UNRECOGNIZED_VOLUME;

    Status = Ext4DeviceControl(TargetDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &LengthInfo, sizeof(LengthInfo), TRUE);
    if (!NT_SUCCESS(Status)) return STATUS_UNRECOGNIZED_VOLUME;

    Status = IoCreateDevice(Ext4Global.DriverObject, sizeof(EXT4_VCB), NULL, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &VolumeDevice);
    if (!NT_SUCCESS(Status)) return Status;

    Vcb = VolumeDevice->DeviceExtension;
    RtlZeroMemory(Vcb, sizeof(*Vcb));
    Vcb->Signature = EXT4_VCB_SIGNATURE;
    Vcb->VolumeDeviceObject = VolumeDevice;
    Vcb->TargetDeviceObject = TargetDevice;
    Vcb->Vpb = Vpb;
    Vcb->References = 1;
    Vcb->DeviceSectorSize = SectorSize;
    Vcb->PartitionLength = (ULONGLONG)LengthInfo.Length.QuadPart;
    ExInitializeResourceLite(&Vcb->Resource);
    ExInitializeResourceLite(&Vcb->CoreLock);
    for (Index = 0; Index < EXT4_FCB_BUCKETS; Index++)
        InitializeListHead(&Vcb->FcbTable[Index]);
    InitializeListHead(&Vcb->DirNotifyList);
    FsRtlNotifyInitializeSync(&Vcb->NotifySync);
    KeInitializeTimer(&Vcb->FlushTimer);
    KeInitializeDpc(&Vcb->FlushDpc, Ext4FlushDpcRoutine, Vcb);
    ExInitializeWorkItem(&Vcb->FlushWork, Ext4FlushWorker, Vcb);
    KeInitializeEvent(&Vcb->FlushIdle, NotificationEvent, TRUE);

    Vcb->SectorBuffer = ExAllocatePoolUninitialized(NonPagedPoolNx, SectorSize, TAG_EXT4);
    if (Vcb->SectorBuffer == NULL)
    {
        Vcb->References = 0;
        Vcb->Vpb = NULL;
        Ext4DeleteVcb(Vcb);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcb->DeviceInterface.open = Ext4DeviceOpen;
    Vcb->DeviceInterface.close = Ext4DeviceClose;
    Vcb->DeviceInterface.bread = Ext4DeviceRead;
    Vcb->DeviceInterface.bwrite = Ext4DeviceWrite;
    Vcb->DeviceInterface.ph_bsize = SectorSize;
    Vcb->DeviceInterface.ph_bcnt = Vcb->PartitionLength / SectorSize;
    Vcb->DeviceInterface.ph_bbuf = Vcb->SectorBuffer;
    Vcb->DeviceInterface.p_user = Vcb;
    Vcb->Core.Device.bdif = &Vcb->DeviceInterface;
    Vcb->Core.Device.part_offset = 0;
    Vcb->Core.Device.part_size = Vcb->PartitionLength;
    Vcb->Core.Now = Ext4CoreNow;
    Vcb->Core.Context = Vcb;

    ReadOnly = BooleanFlagOn(TargetDevice->Characteristics, FILE_READ_ONLY_DEVICE) ||
               !NT_SUCCESS(Ext4DeviceControl(TargetDevice, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, TRUE));

    Ext4LockCore(Vcb);
    Error = E4Mount(&Vcb->Core, ReadOnly, EXT4_CACHE_BLOCKS);
    if (Error == EOK)
    {
        Vcb->CoreMounted = TRUE;
        Vcb->BlockSize = Vcb->Core.BlockSize;
        Vcb->Writable = !Vcb->Core.ReadOnly;
        Error = E4GetAttr(&Vcb->Core, E4_ROOT_INODE, &RootAttr);
        if (Error == EOK && (RootAttr.Mode & E4_S_IFMT) != E4_S_IFDIR) Error = EIO;
    }
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ext4: the volume cannot be mounted (%lx)\n", Status);
        Vcb->References = 0;
        Vcb->Vpb = NULL;
        Ext4DeleteVcb(Vcb);
        return (Status == STATUS_INSUFFICIENT_RESOURCES) ? Status : STATUS_UNRECOGNIZED_VOLUME;
    }

    VolumeDevice->StackSize = (CCHAR)(TargetDevice->StackSize + 1);
    VolumeDevice->SectorSize = (USHORT)SectorSize;
    VolumeDevice->AlignmentRequirement = TargetDevice->AlignmentRequirement;
    VolumeDevice->Flags |= DO_DIRECT_IO;

    Ext4SetVolumeLabel(Vcb, Vpb);
    Vpb->DeviceObject = VolumeDevice;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Ext4Global.Resource, TRUE);
    InsertTailList(&Ext4Global.VcbList, &Vcb->VcbLinks);
    ExReleaseResourceLite(&Ext4Global.Resource);
    KeLeaveCriticalRegion();

    VolumeDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    DPRINT1("ext4: mounted a %lu-byte block volume %s\n", Vcb->BlockSize, Vcb->Writable ? "read-write" : "read-only");
    return STATUS_SUCCESS;
}

NTSTATUS
Ext4VerifyVolume(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVPB Vpb = Stack->Parameters.VerifyVolume.Vpb;
    PEXT4_VCB Vcb = Ext4DeviceVcb(Stack->Parameters.VerifyVolume.DeviceObject);
    NTSTATUS Status;

    PAGED_CODE();

    if (Vcb == NULL) return STATUS_WRONG_VOLUME;

    /* TODO: the volume identifier is not compared; a volume that still carries an ext superblock is taken as the same one */
    Status = Ext4ProbeSuperblock(Vpb->RealDevice, Vcb->DeviceSectorSize);
    if (NT_SUCCESS(Status))
    {
        Vpb->RealDevice->Flags &= ~DO_VERIFY_VOLUME;
        return STATUS_SUCCESS;
    }

    Vcb->Dismounted = TRUE;
    return STATUS_WRONG_VOLUME;
}
