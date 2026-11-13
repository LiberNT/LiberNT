/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading file data, symbolic link targets and the volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
NTSTATUS
BtrfsReadAligned(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Logical,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Destination)
{
    ULONGLONG Start = Logical & ~((ULONGLONG)Vcb->SectorSize - 1);
    PUCHAR Bounce;
    NTSTATUS Status = STATUS_SUCCESS;

    Bounce = ExAllocatePoolUninitialized(PagedPool, BTRFS_IO_CHUNK, TAG_BTRFS_IO);
    if (Bounce == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    while (Length != 0)
    {
        ULONG Skip = (ULONG)(Logical - Start);
        ULONG Part = min(Length, BTRFS_IO_CHUNK - Skip);
        ULONG Span = (Skip + Part + Vcb->SectorSize - 1) & ~(Vcb->SectorSize - 1);
        ULONG Mirror, Mirrors = BtrfsMirrorCount(Vcb, Start);

        ASSERT(Span <= BTRFS_IO_CHUNK);
        for (Mirror = 0; Mirror < Mirrors; Mirror++)
        {
            Status = BtrfsReadLogical(Vcb, Start, Bounce, Span, Mirror);
            if (NT_SUCCESS(Status)) break;
        }
        if (!NT_SUCCESS(Status)) break;

        /* TODO: file data is not checked against the checksum tree */
        RtlCopyMemory(Destination, Bounce + Skip, Part);

        Destination += Part;
        Length -= Part;
        Logical += Part;
        Start = Logical & ~((ULONGLONG)Vcb->SectorSize - 1);
    }

    ExFreePoolWithTag(Bounce, TAG_BTRFS_IO);
    return Status;
}

static
NTSTATUS
BtrfsCopyCompressed(
    _In_ PBTRFS_FCB Fcb,
    _In_ const BTRFS_FILE_EXTENT_ITEM *Extent,
    _In_ ULONGLONG From,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Destination)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    PUCHAR Input, Output;
    ULONG InputLength, OutputLength;
    NTSTATUS Status;

    if (Extent->RamBytes == 0 || Extent->RamBytes > BTRFS_MAX_UNCOMPRESSED) return STATUS_FILE_CORRUPT_ERROR;
    if (Extent->DiskNumBytes == 0 || Extent->DiskNumBytes > BTRFS_MAX_UNCOMPRESSED) return STATUS_FILE_CORRUPT_ERROR;
    OutputLength = (ULONG)Extent->RamBytes;
    InputLength = (ULONG)Extent->DiskNumBytes;
    if (From > OutputLength || Length > OutputLength - From) return STATUS_FILE_CORRUPT_ERROR;

    ExAcquireFastMutex(&Fcb->ExtentCacheLock);
    if (Fcb->CachedExtent != NULL && Fcb->CachedExtentByteNr == Extent->DiskByteNr && Fcb->CachedExtentLength == OutputLength)
    {
        RtlCopyMemory(Destination, Fcb->CachedExtent + From, Length);
        ExReleaseFastMutex(&Fcb->ExtentCacheLock);
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&Fcb->ExtentCacheLock);

    Input = ExAllocatePoolUninitialized(PagedPool, InputLength, TAG_BTRFS_IO);
    Output = ExAllocatePoolUninitialized(PagedPool, OutputLength, TAG_BTRFS_IO);
    if (Input == NULL || Output == NULL)
    {
        if (Input != NULL) ExFreePoolWithTag(Input, TAG_BTRFS_IO);
        if (Output != NULL) ExFreePoolWithTag(Output, TAG_BTRFS_IO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = BtrfsReadAligned(Vcb, Extent->DiskByteNr, InputLength, Input);
    if (NT_SUCCESS(Status)) Status = BtrfsDecompress(Vcb, Extent->Compression, Input, InputLength, Output, OutputLength);
    ExFreePoolWithTag(Input, TAG_BTRFS_IO);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Output, TAG_BTRFS_IO);
        return Status;
    }

    RtlCopyMemory(Destination, Output + From, Length);

    ExAcquireFastMutex(&Fcb->ExtentCacheLock);
    if (Fcb->CachedExtent != NULL) ExFreePoolWithTag(Fcb->CachedExtent, TAG_BTRFS_IO);
    Fcb->CachedExtent = Output;
    Fcb->CachedExtentByteNr = Extent->DiskByteNr;
    Fcb->CachedExtentLength = OutputLength;
    ExReleaseFastMutex(&Fcb->ExtentCacheLock);

    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsCopyExtent(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG FileOffset,
    _In_reads_bytes_(Size) const BTRFS_FILE_EXTENT_ITEM *Extent,
    _In_ ULONG Size,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG End,
    _Inout_updates_bytes_(End - Offset) PUCHAR Buffer)
{
    ULONGLONG ExtentEnd, Start, Stop;
    NTSTATUS Status;

    if (Size < BTRFS_FILE_EXTENT_INLINE_DATA) return STATUS_FILE_CORRUPT_ERROR;
    if (Extent->Encryption != 0 || Extent->OtherEncoding != 0) return STATUS_NOT_SUPPORTED;

    if (Extent->Type == BTRFS_FILE_EXTENT_INLINE)
    {
        const UCHAR *Inline = (const UCHAR *)Extent + BTRFS_FILE_EXTENT_INLINE_DATA;
        ULONG InlineLength = Size - BTRFS_FILE_EXTENT_INLINE_DATA;
        PUCHAR Decoded = NULL;
        ULONG DecodedLength = InlineLength;

        if (Extent->Compression != BTRFS_COMPRESS_NONE)
        {
            if (Extent->RamBytes == 0 || Extent->RamBytes > BTRFS_MAX_UNCOMPRESSED) return STATUS_FILE_CORRUPT_ERROR;
            DecodedLength = (ULONG)Extent->RamBytes;
            Decoded = ExAllocatePoolUninitialized(PagedPool, DecodedLength, TAG_BTRFS_IO);
            if (Decoded == NULL) return STATUS_INSUFFICIENT_RESOURCES;

            Status = BtrfsDecompress(Fcb->Vcb, Extent->Compression, Inline, InlineLength, Decoded, DecodedLength);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Decoded, TAG_BTRFS_IO);
                return Status;
            }
            Inline = Decoded;
        }

        ExtentEnd = FileOffset + DecodedLength;
        Start = max(FileOffset, Offset);
        Stop = min(ExtentEnd, End);
        if (Start < Stop) RtlCopyMemory(Buffer + (Start - Offset), Inline + (Start - FileOffset), (SIZE_T)(Stop - Start));

        if (Decoded != NULL) ExFreePoolWithTag(Decoded, TAG_BTRFS_IO);
        return STATUS_SUCCESS;
    }

    if (Size < sizeof(BTRFS_FILE_EXTENT_ITEM)) return STATUS_FILE_CORRUPT_ERROR;
    if (Extent->Type != BTRFS_FILE_EXTENT_REG && Extent->Type != BTRFS_FILE_EXTENT_PREALLOC) return STATUS_FILE_CORRUPT_ERROR;
    if (FileOffset + Extent->NumBytes < FileOffset) return STATUS_FILE_CORRUPT_ERROR;

    ExtentEnd = FileOffset + Extent->NumBytes;
    Start = max(FileOffset, Offset);
    Stop = min(ExtentEnd, End);
    if (Start >= Stop) return STATUS_SUCCESS;
    if (Extent->Type == BTRFS_FILE_EXTENT_PREALLOC || Extent->DiskByteNr == 0) return STATUS_SUCCESS;

    if (Extent->Compression == BTRFS_COMPRESS_NONE)
    {
        ULONGLONG Within = Extent->Offset + (Start - FileOffset);

        if (Within + (Stop - Start) > Extent->DiskNumBytes) return STATUS_FILE_CORRUPT_ERROR;
        return BtrfsReadAligned(Fcb->Vcb, Extent->DiskByteNr + Within, (ULONG)(Stop - Start), Buffer + (Start - Offset));
    }

    return BtrfsCopyCompressed(Fcb, Extent, Extent->Offset + (Start - FileOffset), (ULONG)(Stop - Start), Buffer + (Start - Offset));
}

NTSTATUS
BtrfsReadFileData(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG End;
    BTRFS_PATH Path;
    BTRFS_KEY Key;
    NTSTATUS Status;

    ASSERT(!Fcb->Directory && !Fcb->Volume);

    RtlZeroMemory(Buffer, Length);
    if (Length == 0) return STATUS_SUCCESS;

    BtrfsAcquireTreeShared(Vcb);
    if (Offset >= Fcb->InodeItem.Size)
    {
        BtrfsReleaseTree(Vcb);
        return STATUS_SUCCESS;
    }
    End = min(Offset + Length, Fcb->InodeItem.Size);

    Key.ObjectId = Fcb->Inode;
    Key.Type = BTRFS_EXTENT_DATA_KEY;
    Key.Offset = Offset;

    Status = BtrfsSearch(Vcb, &Fcb->Root, &Key, &Path);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
    {
        BtrfsReleasePath(Vcb, &Path);
        BtrfsReleaseTree(Vcb);
        return Status;
    }

    if (Status == STATUS_NOT_FOUND || BtrfsCompareKeys(&BtrfsPathItem(&Path)->Key, &Key) != 0)
    {
        NTSTATUS Back = BtrfsPreviousItem(Vcb, &Path);

        if (NT_SUCCESS(Back))
        {
            PBTRFS_ITEM Previous = BtrfsPathItem(&Path);

            if (Previous->Key.ObjectId == Fcb->Inode && Previous->Key.Type == BTRFS_EXTENT_DATA_KEY)
                Status = STATUS_SUCCESS;
            else
                Status = BtrfsNextItem(Vcb, &Path);
        }
        else if (Back != STATUS_NO_MORE_ENTRIES)
        {
            Status = Back;
        }
    }

    while (NT_SUCCESS(Status))
    {
        PBTRFS_ITEM Item = BtrfsPathItem(&Path);
        PBTRFS_FILE_EXTENT_ITEM Extent;
        ULONG Size;

        if (Item->Key.ObjectId != Fcb->Inode || Item->Key.Type != BTRFS_EXTENT_DATA_KEY || Item->Key.Offset >= End) break;

        Extent = BtrfsPathItemData(Vcb, &Path, &Size);
        if (Extent == NULL)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        Status = BtrfsCopyExtent(Fcb, Item->Key.Offset, Extent, Size, Offset, End, Buffer);
        if (!NT_SUCCESS(Status)) break;

        Status = BtrfsNextItem(Vcb, &Path);
    }

    BtrfsReleasePath(Vcb, &Path);
    BtrfsReleaseTree(Vcb);
    return (Status == STATUS_NO_MORE_ENTRIES || Status == STATUS_NOT_FOUND || NT_SUCCESS(Status)) ? STATUS_SUCCESS : Status;
}

NTSTATUS
BtrfsReadSymlink(
    _In_ PBTRFS_FCB Fcb,
    _Out_ PCHAR *Target,
    _Out_ PULONG Length)
{
    PCHAR Buffer;
    NTSTATUS Status;

    *Target = NULL;
    *Length = 0;
    if (Fcb->InodeItem.Size == 0 || Fcb->InodeItem.Size > 4096) return STATUS_FILE_CORRUPT_ERROR;

    Buffer = ExAllocatePoolUninitialized(PagedPool, (SIZE_T)Fcb->InodeItem.Size, TAG_BTRFS);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = BtrfsReadFileData(Fcb, 0, (PUCHAR)Buffer, (ULONG)Fcb->InodeItem.Size);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_BTRFS);
        return Status;
    }

    *Target = Buffer;
    *Length = (ULONG)Fcb->InodeItem.Size;
    return STATUS_SUCCESS;
}

static
PVOID
BtrfsRequestBuffer(
    _In_ PIRP Irp)
{
    if (Irp->MdlAddress != NULL) return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    return Irp->UserBuffer;
}

static
NTSTATUS
BtrfsReadVolume(
    _In_ PBTRFS_VCB Vcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PUCHAR Buffer = BtrfsRequestBuffer(Irp), Bounce;
    NTSTATUS Status = STATUS_SUCCESS;

    *Information = 0;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if ((Offset % Vcb->DeviceSectorSize) != 0 || (Length % Vcb->DeviceSectorSize) != 0) return STATUS_INVALID_PARAMETER;
    if (Vcb->PartitionLength != 0)
    {
        if (Offset >= Vcb->PartitionLength) return STATUS_END_OF_FILE;
        Length = (ULONG)min((ULONGLONG)Length, Vcb->PartitionLength - Offset);
    }

    Bounce = ExAllocatePoolUninitialized(PagedPool, BTRFS_IO_CHUNK, TAG_BTRFS_IO);
    if (Bounce == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    while (*Information < Length)
    {
        ULONG Part = min(Length - (ULONG)*Information, BTRFS_IO_CHUNK);

        Status = BtrfsReadDevice(Vcb->TargetDeviceObject, Offset + *Information, Bounce, Part, FALSE);
        if (!NT_SUCCESS(Status)) break;

        RtlCopyMemory(Buffer + *Information, Bounce, Part);
        *Information += Part;
    }

    ExFreePoolWithTag(Bounce, TAG_BTRFS_IO);
    return Status;
}

NTSTATUS
NTAPI
BtrfsDispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    BOOLEAN Paging = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache = BooleanFlagOn(Irp->Flags, IRP_NOCACHE);
    LARGE_INTEGER ByteOffset = Stack->Parameters.Read.ByteOffset;
    ULONG Length = Stack->Parameters.Read.Length;
    ULONG_PTR Information = 0;
    PERESOURCE Resource;
    ULONGLONG FileSize;
    NTSTATUS Status;

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == BTRFS_FCB_SIGNATURE);

    if (Stack->MinorFunction & IRP_MN_COMPLETE)
    {
        CcMdlReadComplete(FileObject, Irp->MdlAddress);
        Irp->MdlAddress = NULL;
        return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    if (Fcb->Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (Fcb->Directory) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION && ByteOffset.HighPart == -1)
        ByteOffset = FileObject->CurrentByteOffset;
    if (ByteOffset.QuadPart < 0) return BtrfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Length == 0) return BtrfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (Fcb->Volume)
    {
        Status = BtrfsReadVolume(Fcb->Vcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length, &Information);
        if (NT_SUCCESS(Status) && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;
        return BtrfsCompleteRequest(Irp, Status, Information);
    }

    Resource = Paging ? &Fcb->PagingIoResource : &Fcb->MainResource;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);

    FileSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    if (!Paging && (ULONGLONG)ByteOffset.QuadPart >= FileSize)
    {
        Status = STATUS_END_OF_FILE;
    }
    else if (NoCache || Paging)
    {
        PUCHAR Buffer = BtrfsRequestBuffer(Irp);
        ULONG Wanted = Length;

        if (!Paging)
        {
            if ((ByteOffset.QuadPart % Fcb->Vcb->DeviceSectorSize) != 0 || (Length % Fcb->Vcb->DeviceSectorSize) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Done;
            }
            Wanted = (ULONG)min((ULONGLONG)Length, FileSize - (ULONGLONG)ByteOffset.QuadPart);
        }

        if (Buffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Status = BtrfsReadFileData(Fcb, (ULONGLONG)ByteOffset.QuadPart, Buffer, Paging ? Length : Wanted);
            Information = Paging ? Length : Wanted;
        }
    }
    else
    {
        ULONG Wanted = (ULONG)min((ULONGLONG)Length, FileSize - (ULONGLONG)ByteOffset.QuadPart);

        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            if (FileObject->PrivateCacheMap == NULL)
                CcInitializeCacheMap(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize, FALSE, &BtrfsGlobal.CacheCallbacks, Fcb);

            if (Stack->MinorFunction & IRP_MN_MDL)
            {
                CcMdlRead(FileObject, &ByteOffset, Wanted, &Irp->MdlAddress, &Irp->IoStatus);
                Status = Irp->IoStatus.Status;
                Information = Irp->IoStatus.Information;
            }
            else
            {
                PVOID Buffer = BtrfsRequestBuffer(Irp);

                if (Buffer == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
                else
                {
                    CcCopyRead(FileObject, &ByteOffset, Wanted, TRUE, Buffer, &Irp->IoStatus);
                    Status = Irp->IoStatus.Status;
                    Information = Irp->IoStatus.Information;
                }
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Information = 0;
        }
        _SEH2_END;
    }

Done:
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status) && !Paging && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;

    return BtrfsCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}
