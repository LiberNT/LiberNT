/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writing file data, flushing and shutting down
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

VOID
BtrfsNotifyChange(
    _In_ PBTRFS_VCB Vcb,
    _In_ PCUNICODE_STRING FullName,
    _In_ ULONG Filter,
    _In_ ULONG Action)
{
    USHORT Offset;

    if (FullName == NULL || FullName->Length == 0 || FullName->Buffer[0] != L'\\') return;

    for (Offset = FullName->Length / sizeof(WCHAR); Offset > 0 && FullName->Buffer[Offset - 1] != L'\\'; Offset--);
    FsRtlNotifyFullReportChange(Vcb->NotifySync, &Vcb->DirNotifyList, (PSTRING)FullName, (USHORT)(Offset * sizeof(WCHAR)),
                                NULL, NULL, Filter, Action, NULL);
}

static
PVOID
BtrfsWriteBuffer(
    _In_ PIRP Irp)
{
    if (Irp->MdlAddress != NULL) return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    return Irp->UserBuffer;
}

/* The file system area of a mounted volume changes only through the file system, unless the volume is locked */
static
NTSTATUS
BtrfsWriteVolume(
    _In_ PBTRFS_VCB Vcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PUCHAR Buffer = BtrfsWriteBuffer(Irp);
    NTSTATUS Status;

    *Information = 0;
    if (!Vcb->Locked) return STATUS_ACCESS_DENIED;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if ((Offset % Vcb->DeviceSectorSize) != 0 || (Length % Vcb->DeviceSectorSize) != 0) return STATUS_INVALID_PARAMETER;
    if (Vcb->PartitionLength != 0)
    {
        if (Offset >= Vcb->PartitionLength) return STATUS_END_OF_FILE;
        Length = (ULONG)min((ULONGLONG)Length, Vcb->PartitionLength - Offset);
    }

    Status = BtrfsWriteDevice(Vcb->TargetDeviceObject, Offset, Buffer, Length);
    if (NT_SUCCESS(Status)) *Information = Length;
    return Status;
}

/* The cache manager and the memory manager write whole pages; what lies past the end of the file is not file data */
static
NTSTATUS
BtrfsPagingWrite(
    _In_ PBTRFS_FCB Fcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG FileSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    PUCHAR Buffer;
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(Irp->MdlAddress != NULL);
    ASSERT((Offset % Vcb->SectorSize) == 0);

    if (Fcb->Removed || Offset >= FileSize) return STATUS_SUCCESS;
    Length = (ULONG)min((ULONGLONG)Length, FileSize - Offset);

    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsWriteFileData(Fcb, Offset, Buffer, Length);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);
    return Status;
}

static
VOID
BtrfsSetFileSizes(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Size)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Rounded = (Size + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1);

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->PagingIoResource));
    Fcb->Header.FileSize.QuadPart = (LONGLONG)Size;
    Fcb->Header.ValidDataLength.QuadPart = (LONGLONG)Size;
    Fcb->Header.AllocationSize.QuadPart = (LONGLONG)Rounded;
}

static
NTSTATUS
BtrfsNonCachedWrite(
    _In_ PBTRFS_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Extended)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    PUCHAR Buffer = BtrfsWriteBuffer(Irp);
    LARGE_INTEGER Start;
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));
    *Extended = FALSE;

    if ((Offset % Vcb->DeviceSectorSize) != 0 || (Length % Vcb->DeviceSectorSize) != 0) return STATUS_INVALID_PARAMETER;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Fcb->SectionObjectPointers.DataSectionObject != NULL)
    {
        IO_STATUS_BLOCK IoStatus;

        Start.QuadPart = (LONGLONG)Offset;
        CcFlushCache(&Fcb->SectionObjectPointers, &Start, Length, &IoStatus);
        if (!NT_SUCCESS(IoStatus.Status)) return IoStatus.Status;
        CcPurgeCacheSection(&Fcb->SectionObjectPointers, &Start, Length, FALSE);
    }

    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Status = BtrfsWriteFileData(Fcb, Offset, Buffer, Length);
    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);

    if (NT_SUCCESS(Status) && Offset + Length > (ULONGLONG)Fcb->Header.FileSize.QuadPart)
    {
        BtrfsSetFileSizes(Fcb, Offset + Length);
        *Extended = TRUE;
    }
    ExReleaseResourceLite(&Fcb->PagingIoResource);

    if (*Extended && FileObject->SectionObjectPointer->SharedCacheMap != NULL)
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    return Status;
}

static
NTSTATUS
BtrfsCachedWrite(
    _In_ PBTRFS_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Extended,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONGLONG OldSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    LARGE_INTEGER Start;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));
    *Extended = FALSE;
    *Information = 0;

    Status = BtrfsReserveCachedWrite(Fcb, Offset, Length);
    if (!NT_SUCCESS(Status)) return Status;

    if (Offset + Length > OldSize)
    {
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        BtrfsSetFileSizes(Fcb, Offset + Length);
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        *Extended = TRUE;
    }

    Start.QuadPart = (LONGLONG)Offset;
    _SEH2_TRY
    {
        if (FileObject->PrivateCacheMap == NULL)
            CcInitializeCacheMap(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize, FALSE, &BtrfsGlobal.CacheCallbacks, Fcb);
        else if (*Extended)
            CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);

        if (Stack->MinorFunction & IRP_MN_MDL)
        {
            CcPrepareMdlWrite(FileObject, &Start, Length, &Irp->MdlAddress, &Irp->IoStatus);
            Status = Irp->IoStatus.Status;
            *Information = Irp->IoStatus.Information;
        }
        else
        {
            PVOID Buffer = BtrfsWriteBuffer(Irp);

            if (Buffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else if (!CcCopyWrite(FileObject, &Start, Length, TRUE, Buffer))
            {
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                *Information = Length;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Information = 0;
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status) && *Extended)
    {
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        BtrfsSetFileSizes(Fcb, OldSize);
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        if (FileObject->PrivateCacheMap != NULL) CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
        *Extended = FALSE;
    }
    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    BOOLEAN Paging = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache = BooleanFlagOn(Irp->Flags, IRP_NOCACHE);
    LARGE_INTEGER ByteOffset = Stack->Parameters.Write.ByteOffset;
    ULONG Length = Stack->Parameters.Write.Length;
    BOOLEAN ToEnd, Extended = FALSE;
    ULONG_PTR Information = 0;
    ULONGLONG Offset;
    PBTRFS_VCB Vcb;
    NTSTATUS Status;

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);
    Vcb = Fcb->Vcb;

    if (Stack->MinorFunction & IRP_MN_COMPLETE)
    {
        CcMdlWriteComplete(FileObject, &ByteOffset, Irp->MdlAddress);
        Irp->MdlAddress = NULL;
        return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    if (Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (Fcb->Directory) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    ToEnd = (ByteOffset.LowPart == FILE_WRITE_TO_END_OF_FILE && ByteOffset.HighPart == -1);
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION && ByteOffset.HighPart == -1)
        ByteOffset = FileObject->CurrentByteOffset;
    if (!ToEnd && ByteOffset.QuadPart < 0) return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Length == 0) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (Fcb->Volume)
    {
        Status = BtrfsWriteVolume(Vcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length, &Information);
        if (NT_SUCCESS(Status) && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;
        return BtrfsCompleteRequest(Irp, Status, Information);
    }

    if (!Vcb->Writable) return BtrfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);

    if (Paging)
    {
        Status = BtrfsPagingWrite(Fcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length);
        return BtrfsCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Length : 0);
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    Offset = ToEnd ? (ULONGLONG)Fcb->Header.FileSize.QuadPart : (ULONGLONG)ByteOffset.QuadPart;
    ByteOffset.QuadPart = (LONGLONG)Offset;

    if (Fcb->Removed)
        Status = STATUS_FILE_DELETED;
    else if (Offset + Length < Offset || Offset + Length > (ULONGLONG)MAXLONGLONG - Vcb->SectorSize)
        Status = STATUS_INVALID_PARAMETER;
    else if (!FsRtlCheckLockForWriteAccess(&Fcb->FileLock, Irp))
        Status = STATUS_FILE_LOCK_CONFLICT;
    else if (NoCache)
    {
        Status = BtrfsNonCachedWrite(Fcb, FileObject, Irp, Offset, Length, &Extended);
        Information = NT_SUCCESS(Status) ? Length : 0;
    }
    else
        Status = BtrfsCachedWrite(Fcb, FileObject, Irp, Offset, Length, &Extended, &Information);

    if (NT_SUCCESS(Status))
    {
        Fcb->Modified = TRUE;
        FileObject->Flags |= FO_FILE_MODIFIED;
        if (Extended) FileObject->Flags |= FO_FILE_SIZE_CHANGED;
        if (BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = (LONGLONG)(Offset + Information);
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}

NTSTATUS
NTAPI
BtrfsDispatchFlushBuffers(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PBTRFS_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    PBTRFS_VCB Vcb;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (!Vcb->Writable) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    KeEnterCriticalRegion();
    if (Fcb->Volume)
    {
        ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
        Status = BtrfsFlushVolume(Vcb);
        ExReleaseResourceLite(&Vcb->Resource);
    }
    else
    {
        ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
        if (!Fcb->Directory && Fcb->SectionObjectPointers.DataSectionObject != NULL)
        {
            IO_STATUS_BLOCK IoStatus;

            CcFlushCache(&Fcb->SectionObjectPointers, NULL, 0, &IoStatus);
            Status = IoStatus.Status;
        }
        ExReleaseResourceLite(&Fcb->MainResource);

        if (NT_SUCCESS(Status))
        {
            BtrfsAcquireTreeExclusive(Vcb);
            Status = BtrfsCommit(Vcb);
            BtrfsReleaseTree(Vcb);
        }
    }
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status)) Status = BtrfsFlushDevice(Vcb->TargetDeviceObject);
    return BtrfsCompleteRequest(Irp, Status, 0);
}

NTSTATUS
NTAPI
BtrfsDispatchShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!BtrfsIsFsDevice(DeviceObject)) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&BtrfsGlobal.Resource, TRUE);
    for (Entry = BtrfsGlobal.VcbList.Flink; Entry != &BtrfsGlobal.VcbList; Entry = Entry->Flink)
    {
        PBTRFS_VCB Vcb = CONTAINING_RECORD(Entry, BTRFS_VCB, VcbLinks);
        NTSTATUS Status;

        ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
        if (!Vcb->Dismounted && Vcb->Writable)
        {
            Status = BtrfsFlushVolume(Vcb);
            if (!NT_SUCCESS(Status)) DPRINT1("Btrfs: the volume could not be written out at shutdown (%lx)\n", Status);
        }
        ExReleaseResourceLite(&Vcb->Resource);
    }
    ExReleaseResourceLite(&BtrfsGlobal.Resource);
    KeLeaveCriticalRegion();

    return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}
