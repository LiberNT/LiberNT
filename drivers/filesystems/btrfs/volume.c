/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mounting, verifying and tearing down a volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_SUPPORTED_INCOMPAT \
    (BTRFS_INCOMPAT_MIXED_BACKREF | BTRFS_INCOMPAT_DEFAULT_SUBVOL | BTRFS_INCOMPAT_MIXED_GROUPS | \
     BTRFS_INCOMPAT_COMPRESS_LZO | BTRFS_INCOMPAT_COMPRESS_ZSTD | BTRFS_INCOMPAT_BIG_METADATA | \
     BTRFS_INCOMPAT_EXTENDED_IREF | BTRFS_INCOMPAT_SKINNY_METADATA | BTRFS_INCOMPAT_NO_HOLES | \
     BTRFS_INCOMPAT_METADATA_UUID | BTRFS_INCOMPAT_RAID1C34 | BTRFS_INCOMPAT_SIMPLE_QUOTA)

static
BOOLEAN
BtrfsIsPowerOfTwo(
    _In_ ULONG Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

static
NTSTATUS
BtrfsReadSuperblock(
    _In_ PDEVICE_OBJECT Device,
    _Out_ PBTRFS_SUPERBLOCK Super)
{
    NTSTATUS Status;

    Status = BtrfsReadDevice(Device, BTRFS_SUPERBLOCK_OFFSET, Super, sizeof(*Super), TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    if (Super->Magic != BTRFS_MAGIC) return STATUS_UNRECOGNIZED_VOLUME;
    if (!BtrfsChecksumSupported(Super->ChecksumType)) return STATUS_UNRECOGNIZED_VOLUME;
    if (!BtrfsVerifyChecksum(Super->ChecksumType, Super->FsId, BTRFS_SUPERBLOCK_SIZE - BTRFS_CSUM_SIZE, Super->Checksum))
        return STATUS_UNRECOGNIZED_VOLUME;
    if (Super->ByteNr != BTRFS_SUPERBLOCK_OFFSET) return STATUS_UNRECOGNIZED_VOLUME;

    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsCheckSuperblock(
    _In_ const BTRFS_SUPERBLOCK *Super)
{
    if (!BtrfsIsPowerOfTwo(Super->SectorSize) || Super->SectorSize < 4096 || Super->SectorSize > BTRFS_MAX_NODESIZE)
        return STATUS_UNRECOGNIZED_VOLUME;
    if (!BtrfsIsPowerOfTwo(Super->NodeSize) || Super->NodeSize < Super->SectorSize || Super->NodeSize > BTRFS_MAX_NODESIZE)
        return STATUS_UNRECOGNIZED_VOLUME;
    if (Super->SysChunkArraySize > BTRFS_SYSTEM_CHUNK_ARRAY_SIZE) return STATUS_UNRECOGNIZED_VOLUME;
    if (Super->RootLevel >= BTRFS_MAX_LEVEL || Super->ChunkRootLevel >= BTRFS_MAX_LEVEL) return STATUS_UNRECOGNIZED_VOLUME;

    if (Super->IncompatFlags & ~BTRFS_SUPPORTED_INCOMPAT)
    {
        DPRINT1("Btrfs: volume uses features %I64x that this driver does not read\n", Super->IncompatFlags & ~BTRFS_SUPPORTED_INCOMPAT);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    /* TODO: a file system spread over several devices is not assembled */
    if (Super->DeviceCount != 1)
    {
        DPRINT1("Btrfs: volume spans %I64u devices, only single-device volumes are mounted\n", Super->DeviceCount);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsFindDefaultRoot(
    _Inout_ PBTRFS_VCB Vcb)
{
    UNICODE_STRING DefaultName = RTL_CONSTANT_STRING(L"default");
    ULONGLONG Id = BTRFS_FS_TREE_OBJECTID;
    BTRFS_KEY Location;
    UCHAR Type;
    NTSTATUS Status;

    if (Vcb->Super.IncompatFlags & BTRFS_INCOMPAT_DEFAULT_SUBVOL)
    {
        Status = BtrfsLookupName(Vcb, &Vcb->RootTree, Vcb->Super.RootDirObjectId, &DefaultName, FALSE, &Location, &Type);
        if (NT_SUCCESS(Status) && Location.Type == BTRFS_ROOT_ITEM_KEY) Id = Location.ObjectId;
        else if (Status != STATUS_OBJECT_NAME_NOT_FOUND && !NT_SUCCESS(Status)) return Status;
    }

    return BtrfsFindRoot(Vcb, Id, &Vcb->DefaultRoot);
}

static
VOID
BtrfsSetVolumeLabel(
    _In_ PBTRFS_VCB Vcb,
    _Inout_ PVPB Vpb)
{
    UNICODE_STRING Label;
    SIZE_T Length;
    ULONG Serial = 0, Index;

    for (Index = 0; Index < BTRFS_UUID_SIZE; Index++)
        Serial ^= (ULONG)Vcb->Super.FsId[Index] << ((Index % 4) * 8);
    Vpb->SerialNumber = Serial;
    Vpb->VolumeLabelLength = 0;

    for (Length = 0; Length < BTRFS_LABEL_SIZE && Vcb->Super.Label[Length] != 0; Length++);
    if (Length == 0 || !NT_SUCCESS(BtrfsUtf8ToUnicode(Vcb->Super.Label, (ULONG)Length, &Label))) return;

    Vpb->VolumeLabelLength = min(Label.Length, MAXIMUM_VOLUME_LABEL_LENGTH);
    RtlCopyMemory(Vpb->VolumeLabel, Label.Buffer, Vpb->VolumeLabelLength);
    ExFreePoolWithTag(Label.Buffer, TAG_BTRFS);
}

static
VOID
BtrfsDeleteVcb(
    _In_ PBTRFS_VCB Vcb)
{
    PDEVICE_OBJECT VolumeDevice = Vcb->VolumeDeviceObject;
    KIRQL OldIrql;

    ASSERT(Vcb->References == 0);
    ASSERT(IsListEmpty(&Vcb->FcbList));

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&BtrfsGlobal.Resource, TRUE);
    if (Vcb->VcbLinks.Flink != NULL) RemoveEntryList(&Vcb->VcbLinks);
    ExReleaseResourceLite(&BtrfsGlobal.Resource);
    KeLeaveCriticalRegion();

    IoAcquireVpbSpinLock(&OldIrql);
    if (Vcb->Vpb != NULL && Vcb->Vpb->DeviceObject == VolumeDevice)
    {
        Vcb->Vpb->DeviceObject = NULL;
        Vcb->Vpb->Flags &= ~VPB_MOUNTED;
    }
    IoReleaseVpbSpinLock(OldIrql);

    BtrfsUninitializeTransactions(Vcb);
    BtrfsFlushNodeCache(Vcb);
    BtrfsFreeChunks(Vcb);
    if (Vcb->NotifySync != NULL) FsRtlNotifyUninitializeSync(&Vcb->NotifySync);
    ExDeleteResourceLite(&Vcb->TreeLock);
    ExDeleteResourceLite(&Vcb->Resource);
    Vcb->Signature = 0;

    IoDeleteDevice(VolumeDevice);
}

VOID
BtrfsReferenceVcb(
    _In_ PBTRFS_VCB Vcb)
{
    ASSERT(Vcb->Signature == BTRFS_VCB_SIGNATURE);
    ASSERT(Vcb->References > 0);
    InterlockedIncrement((PLONG)&Vcb->References);
}

VOID
BtrfsDereferenceVcb(
    _In_ PBTRFS_VCB Vcb)
{
    ASSERT(Vcb->Signature == BTRFS_VCB_SIGNATURE);
    ASSERT(Vcb->References > 0);
    if (InterlockedDecrement((PLONG)&Vcb->References) == 0) BtrfsDeleteVcb(Vcb);
}

NTSTATUS
BtrfsMountVolume(
    _In_ PDEVICE_OBJECT TargetDevice,
    _In_ PVPB Vpb)
{
    GET_LENGTH_INFORMATION LengthInfo;
    DISK_GEOMETRY Geometry;
    PDEVICE_OBJECT VolumeDevice = NULL;
    PBTRFS_SUPERBLOCK Super;
    BTRFS_INODE_ITEM RootInode;
    PBTRFS_VCB Vcb;
    NTSTATUS Status;

    PAGED_CODE();

    if (TargetDevice->DeviceType != FILE_DEVICE_DISK && TargetDevice->DeviceType != FILE_DEVICE_VIRTUAL_DISK)
        return STATUS_UNRECOGNIZED_VOLUME;

    Super = ExAllocatePoolUninitialized(PagedPool, sizeof(BTRFS_SUPERBLOCK), TAG_BTRFS);
    if (Super == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = BtrfsReadSuperblock(TargetDevice, Super);
    if (NT_SUCCESS(Status)) Status = BtrfsCheckSuperblock(Super);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Super, TAG_BTRFS);
        return (Status == STATUS_INSUFFICIENT_RESOURCES) ? Status : STATUS_UNRECOGNIZED_VOLUME;
    }

    Status = IoCreateDevice(BtrfsGlobal.DriverObject, sizeof(BTRFS_VCB), NULL, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Super, TAG_BTRFS);
        return Status;
    }

    Vcb = VolumeDevice->DeviceExtension;
    RtlZeroMemory(Vcb, sizeof(*Vcb));
    Vcb->Signature = BTRFS_VCB_SIGNATURE;
    Vcb->VolumeDeviceObject = VolumeDevice;
    Vcb->TargetDeviceObject = TargetDevice;
    Vcb->Vpb = Vpb;
    Vcb->References = 1;
    ExInitializeResourceLite(&Vcb->Resource);
    InitializeListHead(&Vcb->FcbList);
    InitializeListHead(&Vcb->NodeLru);
    InitializeListHead(&Vcb->DirNotifyList);
    ExInitializeFastMutex(&Vcb->NodeCacheLock);
    ExInitializeResourceLite(&Vcb->TreeLock);
    InitializeListHead(&Vcb->LiveRoots);
    InitializeListHead(&Vcb->DirtyNodes);
    InitializeListHead(&Vcb->PendingRefs);
    FsRtlNotifyInitializeSync(&Vcb->NotifySync);

    RtlCopyMemory(&Vcb->Super, Super, sizeof(*Super));
    ExFreePoolWithTag(Super, TAG_BTRFS);

    Vcb->NodeSize = Vcb->Super.NodeSize;
    Vcb->SectorSize = Vcb->Super.SectorSize;
    Vcb->ChecksumType = Vcb->Super.ChecksumType;
    Vcb->ChecksumSize = BtrfsChecksumSize(Vcb->ChecksumType);
    Vcb->DeviceId = Vcb->Super.DevItem.DeviceId;
    Vcb->HeaderFsId = (Vcb->Super.IncompatFlags & BTRFS_INCOMPAT_METADATA_UUID) ? Vcb->Super.MetadataUuid : Vcb->Super.FsId;

    Vcb->DeviceSectorSize = 512;
    if (NT_SUCCESS(BtrfsDeviceControl(TargetDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, &Geometry, sizeof(Geometry), TRUE)) &&
        Geometry.BytesPerSector != 0)
    {
        Vcb->DeviceSectorSize = Geometry.BytesPerSector;
    }
    if (NT_SUCCESS(BtrfsDeviceControl(TargetDevice, IOCTL_DISK_GET_LENGTH_INFO, &LengthInfo, sizeof(LengthInfo), TRUE)))
        Vcb->PartitionLength = (ULONGLONG)LengthInfo.Length.QuadPart;

    Vcb->RootTree.Id = BTRFS_ROOT_TREE_OBJECTID;
    Vcb->RootTree.ByteNr = Vcb->Super.Root;
    Vcb->RootTree.Level = Vcb->Super.RootLevel;

    Status = BtrfsLoadSystemChunks(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadChunkTree(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsInitializeTransactions(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsFindDefaultRoot(Vcb);
    if (NT_SUCCESS(Status)) Status = BtrfsLoadInode(Vcb, &Vcb->DefaultRoot, Vcb->DefaultRoot.RootDirId, &RootInode);
    if (NT_SUCCESS(Status) && (RootInode.Mode & BTRFS_S_IFMT) != BTRFS_S_IFDIR) Status = STATUS_FILE_CORRUPT_ERROR;

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Btrfs: the volume cannot be mounted (%lx)\n", Status);
        Vcb->References = 0;
        Vcb->Vpb = NULL;
        BtrfsDeleteVcb(Vcb);
        return (Status == STATUS_INSUFFICIENT_RESOURCES) ? Status : STATUS_UNRECOGNIZED_VOLUME;
    }

    Vcb->Writable = NT_SUCCESS(BtrfsCheckWritable(Vcb));

    VolumeDevice->StackSize = (CCHAR)(TargetDevice->StackSize + 1);
    VolumeDevice->SectorSize = (USHORT)Vcb->DeviceSectorSize;
    VolumeDevice->AlignmentRequirement = TargetDevice->AlignmentRequirement;
    VolumeDevice->Flags |= DO_DIRECT_IO;

    BtrfsSetVolumeLabel(Vcb, Vpb);
    Vpb->DeviceObject = VolumeDevice;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&BtrfsGlobal.Resource, TRUE);
    InsertTailList(&BtrfsGlobal.VcbList, &Vcb->VcbLinks);
    ExReleaseResourceLite(&BtrfsGlobal.Resource);
    KeLeaveCriticalRegion();

    VolumeDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsVerifyVolume(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVPB Vpb = Stack->Parameters.VerifyVolume.Vpb;
    PBTRFS_VCB Vcb = BtrfsDeviceVcb(Stack->Parameters.VerifyVolume.DeviceObject);
    PBTRFS_SUPERBLOCK Super;
    NTSTATUS Status;

    PAGED_CODE();

    if (Vcb == NULL) return STATUS_WRONG_VOLUME;

    Super = ExAllocatePoolUninitialized(PagedPool, sizeof(BTRFS_SUPERBLOCK), TAG_BTRFS);
    if (Super == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = BtrfsReadSuperblock(Vpb->RealDevice, Super);
    if (NT_SUCCESS(Status) &&
        (RtlCompareMemory(Super->FsId, Vcb->Super.FsId, BTRFS_UUID_SIZE) != BTRFS_UUID_SIZE ||
         Super->Generation != Vcb->Super.Generation))
    {
        Status = STATUS_WRONG_VOLUME;
    }
    ExFreePoolWithTag(Super, TAG_BTRFS);

    if (NT_SUCCESS(Status))
    {
        Vpb->RealDevice->Flags &= ~DO_VERIFY_VOLUME;
        return STATUS_SUCCESS;
    }

    Vcb->Dismounted = TRUE;
    return STATUS_WRONG_VOLUME;
}
