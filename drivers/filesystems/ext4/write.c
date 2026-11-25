/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writing file data, allocating its blocks, changing the file size, flushing and shutdown
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_MAX_RUNS 64

typedef struct _EXT4_RUN
{
    ULONGLONG Physical;
    ULONG Blocks;
    BOOLEAN Fresh;
} EXT4_RUN, *PEXT4_RUN;

static
PVOID
Ext4WriteBuffer(
    _In_ PIRP Irp)
{
    if (Irp->MdlAddress != NULL) return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    return Irp->UserBuffer;
}

/* Maps the blocks from FirstBlock on, allocating the missing ones, in one transaction. Stops when the run table is
 * full; *Done tells how many blocks it covered. The same transaction raises the size in the inode to cover them,
 * up to FileSize, so that a crash never leaves blocks past the end of the file. */
static
NTSTATUS
Ext4MapForWrite(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG FirstBlock,
    _In_ ULONG Blocks,
    _In_ ULONGLONG FileSize,
    _Out_writes_(EXT4_MAX_RUNS) PEXT4_RUN Runs,
    _Out_ PULONG RunCount,
    _Out_ PULONG Done)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    BOOLEAN Allocated = FALSE;
    ULONG Count = 0, Covered = 0;
    ULONGLONG Reached = 0;
    int Error;

    *RunCount = 0;
    *Done = 0;

    Ext4LockCore(Vcb);
    Error = E4Begin(&Vcb->Core);
    if (Error != EOK)
    {
        NTSTATUS Status = Ext4Status(Vcb, Error);

        Ext4UnlockCore(Vcb);
        return Status;
    }

    while (Covered < Blocks)
    {
        uint64_t Physical = 0;
        uint32_t Mapped = 0;
        bool Fresh = false;

        Error = E4Map(&Vcb->Core, Fcb->Inode, FirstBlock + Covered, Blocks - Covered, true, &Physical, &Mapped, &Fresh);
        if (Error != EOK) break;
        ASSERT(Physical != 0);
        ASSERT(Mapped != 0);
        if (Fresh) Allocated = TRUE;

        if (Count != 0 && Runs[Count - 1].Physical + Runs[Count - 1].Blocks == Physical && Runs[Count - 1].Fresh == (BOOLEAN)Fresh)
        {
            Runs[Count - 1].Blocks += Mapped;
        }
        else
        {
            if (Count == EXT4_MAX_RUNS) break;
            Runs[Count].Physical = Physical;
            Runs[Count].Blocks = Mapped;
            Runs[Count].Fresh = Fresh;
            Count++;
        }
        Covered += Mapped;
    }

    /* The blocks mapped so far stay with the file even when a later one could not be had */
    if (Covered != 0)
    {
        Reached = min(FileSize, (FirstBlock + Covered) * Vcb->BlockSize);
        Error = Allocated ? E4GrowSize(&Vcb->Core, Fcb->Inode, Reached) : EOK;
    }
    Error = E4End(&Vcb->Core, Error);
    {
        NTSTATUS Status = Ext4Status(Vcb, Error);

        Ext4UnlockCore(Vcb);
        if (!NT_SUCCESS(Status)) return Status;
    }
    ASSERT(Covered != 0);
    if (Allocated && Reached > Fcb->Attr.Size) Fcb->Attr.Size = Reached;

    if (Allocated) Ext4MarkCoreDirty(Vcb);
    *RunCount = Count;
    *Done = Covered;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4WriteBounced(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONGLONG DeviceOffset,
    _In_reads_bytes_opt_(DataLength) const UCHAR *Data,
    _In_ ULONG DataOffset,
    _In_ ULONG DataLength,
    _In_ ULONG TotalLength,
    _In_ BOOLEAN Merge)
{
    PUCHAR Bounce;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT((DeviceOffset % Vcb->DeviceSectorSize) == 0);
    ASSERT((TotalLength % Vcb->DeviceSectorSize) == 0);
    ASSERT(DataOffset + DataLength <= TotalLength);

    Bounce = ExAllocatePoolUninitialized(NonPagedPoolNx, TotalLength, TAG_EXT4_IO);
    if (Bounce == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Merge)
        Status = Ext4ReadDevice(Vcb->TargetDeviceObject, DeviceOffset, Bounce, TotalLength, FALSE);
    else
        RtlZeroMemory(Bounce, TotalLength);
    if (NT_SUCCESS(Status))
    {
        if (DataLength != 0) RtlCopyMemory(Bounce + DataOffset, Data, DataLength);
        Status = Ext4WriteDevice(Vcb->TargetDeviceObject, DeviceOffset, Bounce, TotalLength);
    }
    ExFreePoolWithTag(Bounce, TAG_EXT4_IO);
    return Status;
}

/* Stores Length bytes at Offset, which lie within FileSize. Offset is a multiple of the device sector size, and so
 * is the end unless it is the end of the file. Blocks the file lacks are allocated; what a new block holds outside
 * the bytes written, and what the last block holds past the end of the file, is written as zeros. */
NTSTATUS
Ext4WriteFileData(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _In_ ULONGLONG FileSize)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONG BlockSize = Vcb->BlockSize;
    ULONG Sector = Vcb->DeviceSectorSize;
    NTSTATUS Status = STATUS_SUCCESS;
    PEXT4_RUN Runs;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT((Offset % Sector) == 0);
    ASSERT(Offset + Length <= FileSize);
    ASSERT(((Offset + Length) % Sector) == 0 || Offset + Length == FileSize);

    Runs = ExAllocatePoolUninitialized(NonPagedPoolNx, sizeof(EXT4_RUN) * EXT4_MAX_RUNS, TAG_EXT4_IO);
    if (Runs == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    while (Length != 0 && NT_SUCCESS(Status))
    {
        ULONG InBlock = (ULONG)(Offset % BlockSize);
        ULONG Blocks = (ULONG)(((ULONGLONG)Length + InBlock + BlockSize - 1) / BlockSize);
        ULONG RunCount, Done, Index;

        Status = Ext4MapForWrite(Fcb, Offset / BlockSize, Blocks, FileSize, Runs, &RunCount, &Done);
        if (!NT_SUCCESS(Status)) break;

        for (Index = 0; Index < RunCount && NT_SUCCESS(Status) && Length != 0; Index++)
        {
            ULONGLONG DeviceOffset = Runs[Index].Physical * BlockSize + InBlock;
            ULONGLONG RunBytes = (ULONGLONG)Runs[Index].Blocks * BlockSize - InBlock;
            ULONG Chunk = (ULONG)min((ULONGLONG)Length, RunBytes);
            ULONG Direct = Chunk & ~(Sector - 1);
            ULONG Tail = Chunk - Direct;

            /* The start of a new block that the write does not reach */
            if (Runs[Index].Fresh && InBlock != 0)
                Status = Ext4WriteBounced(Vcb, DeviceOffset - InBlock, NULL, 0, 0, InBlock, FALSE);

            if (NT_SUCCESS(Status) && Direct != 0)
                Status = Ext4WriteDevice(Vcb->TargetDeviceObject, DeviceOffset, Buffer, Direct);

            if (NT_SUCCESS(Status) && (Tail != 0 || ((InBlock + Chunk) % BlockSize) != 0))
            {
                /* The write ends inside a block: at the end of the file the rest of the block is zeros, and so it
                 * is in a new block; otherwise the rest of the last sector keeps what it holds */
                ULONG ToBlockEnd = BlockSize - ((InBlock + Direct) % BlockSize);
                BOOLEAN AtEnd = (Offset + Chunk == FileSize);

                if (Runs[Index].Fresh || AtEnd)
                    Status = Ext4WriteBounced(Vcb, DeviceOffset + Direct, Buffer + Direct, 0, Tail, ToBlockEnd, FALSE);
                else if (Tail != 0)
                    Status = Ext4WriteBounced(Vcb, DeviceOffset + Direct, Buffer + Direct, 0, Tail, Sector, TRUE);
            }

            Buffer += Chunk;
            Offset += Chunk;
            Length -= Chunk;
            InBlock = 0;
        }
        ASSERT(!NT_SUCCESS(Status) || Done != 0);
    }

    ExFreePoolWithTag(Runs, TAG_EXT4_IO);
    return Status;
}

/* Gives the range its blocks without storing data: the cached pages that cover it are written later */
NTSTATUS
Ext4AllocateRange(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG FileSize)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONG BlockSize = Vcb->BlockSize;
    ULONGLONG Block = Offset / BlockSize;
    ULONGLONG End = (Offset + Length + BlockSize - 1) / BlockSize;
    NTSTATUS Status = STATUS_SUCCESS;
    PEXT4_RUN Runs;

    if (Length == 0) return STATUS_SUCCESS;

    Runs = ExAllocatePoolUninitialized(NonPagedPoolNx, sizeof(EXT4_RUN) * EXT4_MAX_RUNS, TAG_EXT4_IO);
    if (Runs == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    while (Block < End)
    {
        ULONG RunCount, Done;

        Status = Ext4MapForWrite(Fcb, Block, (ULONG)min(End - Block, 0x10000ULL), FileSize, Runs, &RunCount, &Done);
        if (!NT_SUCCESS(Status)) break;
        ASSERT(Done != 0);
        Block += Done;
    }

    ExFreePoolWithTag(Runs, TAG_EXT4_IO);
    return Status;
}

static
VOID
Ext4SetHeaderSizes(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Size)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->PagingIoResource));
    Fcb->Header.FileSize.QuadPart = (LONGLONG)Size;
    Fcb->Header.ValidDataLength.QuadPart = (LONGLONG)Size;
    Fcb->Header.AllocationSize.QuadPart = (LONGLONG)ALIGN_UP_BY(Size, (ULONGLONG)Fcb->Vcb->BlockSize);
}

static
VOID
Ext4TouchFcb(
    _In_ PEXT4_FCB Fcb)
{
    Ext4CurrentTime(&Fcb->Attr.Modify);
    Fcb->Attr.Change = Fcb->Attr.Modify;
    Fcb->TimesDirty = TRUE;
}

/* Records the size in the inode, releasing the blocks past a smaller one */
static
NTSTATUS
Ext4StoreSize(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Size)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    NTSTATUS Status;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4SetSize(&Vcb->Core, Fcb->Inode, Size));
    Ext4UnlockCore(Vcb);
    if (NT_SUCCESS(Status))
    {
        Fcb->Attr.Size = Size;
        Ext4MarkCoreDirty(Vcb);
    }
    return Status;
}

NTSTATUS
Ext4SetFileSize(
    _In_ PEXT4_FCB Fcb,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ ULONGLONG Size)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONGLONG OldSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Fcb->MainResource));
    ASSERT(!Fcb->Directory && !Fcb->Volume);

    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Size > (ULONGLONG)MAXLONGLONG - Vcb->BlockSize) return STATUS_INVALID_PARAMETER;
    if (Size == OldSize && Size == Fcb->Attr.Size) return STATUS_SUCCESS;

    if (Size < OldSize)
    {
        LARGE_INTEGER NewSize;

        NewSize.QuadPart = (LONGLONG)Size;
        if (!MmCanFileBeTruncated(&Fcb->SectionObjectPointers, &NewSize)) return STATUS_USER_MAPPED_FILE;
    }

    /* No paging write may map blocks while the blocks past the new end are released */
    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
    Status = Ext4StoreSize(Fcb, Size);
    if (NT_SUCCESS(Status)) Ext4SetHeaderSizes(Fcb, Size);
    ExReleaseResourceLite(&Fcb->PagingIoResource);
    if (!NT_SUCCESS(Status)) return Status;

    Ext4TouchFcb(Fcb);
    if (FileObject != NULL && FileObject->SectionObjectPointer != NULL &&
        FileObject->SectionObjectPointer->SharedCacheMap != NULL)
    {
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    }
    return STATUS_SUCCESS;
}

/* The file system area of a mounted volume changes only through the file system, unless the volume is locked */
static
NTSTATUS
Ext4WriteVolume(
    _In_ PEXT4_VCB Vcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PUCHAR Buffer = Ext4WriteBuffer(Irp);
    NTSTATUS Status;

    *Information = 0;
    if (!Vcb->Locked) return STATUS_ACCESS_DENIED;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if ((Offset % Vcb->DeviceSectorSize) != 0 || (Length % Vcb->DeviceSectorSize) != 0) return STATUS_INVALID_PARAMETER;
    if (Offset >= Vcb->PartitionLength) return STATUS_END_OF_FILE;
    Length = (ULONG)min((ULONGLONG)Length, Vcb->PartitionLength - Offset);

    Status = Ext4WriteDevice(Vcb->TargetDeviceObject, Offset, Buffer, Length);
    if (NT_SUCCESS(Status)) *Information = Length;
    return Status;
}

/* The cache manager and the memory manager write whole pages; what lies past the end of the file is not file data */
static
NTSTATUS
Ext4PagingWrite(
    _In_ PEXT4_FCB Fcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length)
{
    ULONGLONG FileSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    PUCHAR Buffer;

    ASSERT(Irp->MdlAddress != NULL);
    ASSERT((Offset % Fcb->Vcb->DeviceSectorSize) == 0);

    if (Fcb->Orphaned || Offset >= FileSize) return STATUS_SUCCESS;
    Length = (ULONG)min((ULONGLONG)Length, FileSize - Offset);

    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    return Ext4WriteFileData(Fcb, Offset, Buffer, Length, FileSize);
}

static
NTSTATUS
Ext4WriteLinkTarget(
    _In_ PEXT4_FCB Fcb,
    _In_opt_ PEXT4_CCB Ccb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    PUCHAR Buffer = Ext4WriteBuffer(Irp);
    NTSTATUS Status;

    /* A link target is stored whole: the one write that creates the link */
    if (Offset != 0) return STATUS_INVALID_PARAMETER;
    if (Length > E4_SYMLINK_MAX) return STATUS_NAME_TOO_LONG;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Fcb->PendingSymlink)
    {
        if (Ccb == NULL) return STATUS_INVALID_PARAMETER;
        Ext4LockCore(Vcb);
        Status = Ext4Status(Vcb, E4SetSymlink(&Vcb->Core, Ccb->ParentInode, Ccb->Name, Ccb->NameLength,
                                              Fcb->Inode, Fcb->PendingSymlinkMode, (const char *)Buffer, Length));
        Ext4UnlockCore(Vcb);
        if (!NT_SUCCESS(Status)) return Status;
        Fcb->PendingSymlink = FALSE;
    }
    else
    {
        Ext4LockCore(Vcb);
        Status = Ext4Status(Vcb, E4WriteLink(&Vcb->Core, Fcb->Inode, (const char *)Buffer, Length));
        Ext4UnlockCore(Vcb);
        if (!NT_SUCCESS(Status)) return Status;
    }

    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
    Ext4SetHeaderSizes(Fcb, Length);
    ExReleaseResourceLite(&Fcb->PagingIoResource);
    Fcb->Attr.Size = Length;
    Ext4MarkCoreDirty(Vcb);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Ext4NonCachedWrite(
    _In_ PEXT4_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Extended)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    PUCHAR Buffer = Ext4WriteBuffer(Irp);
    ULONGLONG OldSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    ULONGLONG NewSize = max(OldSize, Offset + Length);
    LARGE_INTEGER Start;
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
    Status = Ext4WriteFileData(Fcb, Offset, Buffer, Length, NewSize);
    if (NT_SUCCESS(Status) && NewSize > OldSize)
    {
        Status = Ext4StoreSize(Fcb, NewSize);
        if (NT_SUCCESS(Status))
        {
            Ext4SetHeaderSizes(Fcb, NewSize);
            *Extended = TRUE;
        }
    }
    if (!NT_SUCCESS(Status) && Fcb->Attr.Size > OldSize) (VOID)Ext4StoreSize(Fcb, OldSize);
    ExReleaseResourceLite(&Fcb->PagingIoResource);

    if (*Extended && FileObject->SectionObjectPointer->SharedCacheMap != NULL)
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
    return Status;
}

static
NTSTATUS
Ext4CachedWrite(
    _In_ PEXT4_FCB Fcb,
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

    if (Offset + Length > OldSize)
    {
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        Ext4SetHeaderSizes(Fcb, Offset + Length);
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        *Extended = TRUE;
    }

    Start.QuadPart = (LONGLONG)Offset;
    _SEH2_TRY
    {
        if (FileObject->PrivateCacheMap == NULL)
            CcInitializeCacheMap(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize, FALSE, &Ext4Global.CacheCallbacks, Fcb);
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
            PVOID Buffer = Ext4WriteBuffer(Irp);

            if (Buffer == NULL)
                Status = STATUS_INSUFFICIENT_RESOURCES;
            else if (!CcCopyWrite(FileObject, &Start, Length, TRUE, Buffer))
                Status = STATUS_UNSUCCESSFUL;
            else
                *Information = Length;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Information = 0;
    }
    _SEH2_END;

    /* The blocks are taken once the pages hold the data: a page read in between finds a hole and so zeros, never
     * what a new block happens to hold. A full volume is still reported to the writer.
     * FIXME: a crash before the pages are written leaves the new blocks with their old content inside the file. */
    if (NT_SUCCESS(Status)) Status = Ext4AllocateRange(Fcb, Offset, Length, (ULONGLONG)Fcb->Header.FileSize.QuadPart);
    if (NT_SUCCESS(Status) && *Extended) Status = Ext4StoreSize(Fcb, Offset + Length);

    if (!NT_SUCCESS(Status) && *Extended)
    {
        if (Fcb->Attr.Size > OldSize) (VOID)Ext4StoreSize(Fcb, OldSize);
        ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);
        Ext4SetHeaderSizes(Fcb, OldSize);
        ExReleaseResourceLite(&Fcb->PagingIoResource);
        if (FileObject->PrivateCacheMap != NULL) CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
        *Extended = FALSE;
        *Information = 0;
    }
    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    BOOLEAN Paging = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache = BooleanFlagOn(Irp->Flags, IRP_NOCACHE);
    LARGE_INTEGER ByteOffset = Stack->Parameters.Write.ByteOffset;
    ULONG Length = Stack->Parameters.Write.Length;
    BOOLEAN ToEnd, Extended = FALSE;
    ULONG_PTR Information = 0;
    ULONGLONG Offset;
    PEXT4_VCB Vcb;
    NTSTATUS Status;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    Vcb = Fcb->Vcb;

    if (Stack->MinorFunction & IRP_MN_COMPLETE)
    {
        CcMdlWriteComplete(FileObject, &ByteOffset, Irp->MdlAddress);
        Irp->MdlAddress = NULL;
        return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    if (Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (Fcb->Directory) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    ToEnd = (ByteOffset.LowPart == FILE_WRITE_TO_END_OF_FILE && ByteOffset.HighPart == -1);
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION && ByteOffset.HighPart == -1)
        ByteOffset = FileObject->CurrentByteOffset;
    if (!ToEnd && ByteOffset.QuadPart < 0) return Ext4CompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Length == 0) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (Fcb->Volume)
    {
        Status = Ext4WriteVolume(Vcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length, &Information);
        if (NT_SUCCESS(Status) && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;
        return Ext4CompleteRequest(Irp, Status, Information);
    }

    if (!Vcb->Writable) return Ext4CompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);

    if (Paging)
    {
        Status = Ext4PagingWrite(Fcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length);
        return Ext4CompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Length : 0);
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);

    Offset = ToEnd ? (ULONGLONG)Fcb->Header.FileSize.QuadPart : (ULONGLONG)ByteOffset.QuadPart;
    ByteOffset.QuadPart = (LONGLONG)Offset;

    if (Fcb->Orphaned && Fcb->OpenHandles == 0)
    {
        Status = STATUS_FILE_DELETED;
    }
    else if (Offset + Length < Offset || Offset + Length > (ULONGLONG)MAXLONGLONG - Vcb->BlockSize)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (Fcb->Symlink)
    {
        Status = Ext4WriteLinkTarget(Fcb, (PEXT4_CCB)FileObject->FsContext2, Irp, Offset, Length);
        Information = NT_SUCCESS(Status) ? Length : 0;
    }
    else if (Fcb->ReparseTag != 0)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else if (!FsRtlCheckLockForWriteAccess(&Fcb->FileLock, Irp))
    {
        Status = STATUS_FILE_LOCK_CONFLICT;
    }
    else if (NoCache)
    {
        Status = Ext4NonCachedWrite(Fcb, FileObject, Irp, Offset, Length, &Extended);
        Information = NT_SUCCESS(Status) ? Length : 0;
    }
    else
    {
        Status = Ext4CachedWrite(Fcb, FileObject, Irp, Offset, Length, &Extended, &Information);
    }

    if (NT_SUCCESS(Status))
    {
        Ext4TouchFcb(Fcb);
        FileObject->Flags |= FO_FILE_MODIFIED;
        if (Extended) FileObject->Flags |= FO_FILE_SIZE_CHANGED;
        if (BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = (LONGLONG)(Offset + Information);
    }

    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}

NTSTATUS
NTAPI
Ext4DispatchFlushBuffers(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    PEXT4_VCB Vcb;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Vcb = Fcb->Vcb;
    if (Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (!Vcb->Writable) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);

    KeEnterCriticalRegion();
    if (!Fcb->Volume && !Fcb->Directory)
    {
        ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
        if (Fcb->SectionObjectPointers.DataSectionObject != NULL)
        {
            IO_STATUS_BLOCK IoStatus;

            CcFlushCache(&Fcb->SectionObjectPointers, NULL, 0, &IoStatus);
            Status = IoStatus.Status;
        }
        if (NT_SUCCESS(Status) && !Fcb->Orphaned) Status = Ext4WriteTimes(Fcb);
        ExReleaseResourceLite(&Fcb->MainResource);
    }
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status)) Status = Ext4FlushVolume(Vcb);
    return Ext4CompleteRequest(Irp, Status, 0);
}

NTSTATUS
NTAPI
Ext4DispatchShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!Ext4IsFsDevice(DeviceObject)) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Ext4Global.Resource, TRUE);
    for (Entry = Ext4Global.VcbList.Flink; Entry != &Ext4Global.VcbList; Entry = Entry->Flink)
    {
        PEXT4_VCB Vcb = CONTAINING_RECORD(Entry, EXT4_VCB, VcbLinks);

        ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
        if (!Vcb->Dismounted && Vcb->Writable)
        {
            /* The journal is closed and the volume left clean: nothing writes to it after this point */
            Ext4StopCore(Vcb);
            Vcb->Dismounted = TRUE;
        }
        ExReleaseResourceLite(&Vcb->Resource);
    }
    ExReleaseResourceLite(&Ext4Global.Resource);
    KeLeaveCriticalRegion();

    return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
}
