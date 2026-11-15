/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading file data, symbolic link targets and the raw volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

static
PVOID
Ext4RequestBuffer(
    _In_ PIRP Irp)
{
    if (Irp->MdlAddress != NULL) return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    return Irp->UserBuffer;
}

static
NTSTATUS
Ext4ReadLinkData(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    uint32_t TargetLength = 0;
    NTSTATUS Status;
    PCHAR Target;

    Target = ExAllocatePoolUninitialized(PagedPool, E4_SYMLINK_MAX + 1, TAG_EXT4_IO);
    if (Target == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4ReadLink(&Vcb->Core, Fcb->Inode, Target, E4_SYMLINK_MAX, &TargetLength));
    Ext4UnlockCore(Vcb);

    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Buffer, Length);
        if (Offset < TargetLength)
            RtlCopyMemory(Buffer, Target + Offset, min((ULONGLONG)Length, TargetLength - Offset));
    }
    ExFreePoolWithTag(Target, TAG_EXT4_IO);
    return Status;
}

/* Fills Buffer with the bytes of the file from Offset on; what no block backs, and what lies past the end, is zero.
 * Offset is a multiple of the device sector size. */
NTSTATUS
Ext4ReadFileData(
    _In_ PEXT4_FCB Fcb,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    ULONG BlockSize = Vcb->BlockSize;
    ULONG Sector = Vcb->DeviceSectorSize;
    ULONGLONG FileSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT((Offset % Sector) == 0);

    if (Fcb->Symlink) return Ext4ReadLinkData(Fcb, Offset, Buffer, Length);

    while (Length != 0)
    {
        ULONG InBlock = (ULONG)(Offset % BlockSize);
        ULONG Blocks = (ULONG)(((ULONGLONG)Length + InBlock + BlockSize - 1) / BlockSize);
        uint64_t Physical = 0;
        uint32_t Mapped = 0;
        ULONG Chunk;

        if (Offset >= FileSize)
        {
            RtlZeroMemory(Buffer, Length);
            break;
        }

        Ext4LockCore(Vcb);
        Status = Ext4Status(Vcb, E4Map(&Vcb->Core, Fcb->Inode, Offset / BlockSize, Blocks, false, &Physical, &Mapped, NULL));
        Ext4UnlockCore(Vcb);
        if (!NT_SUCCESS(Status)) break;
        ASSERT(Mapped != 0);

        Chunk = (ULONG)min((ULONGLONG)Length, (ULONGLONG)Mapped * BlockSize - InBlock);
        if (Physical == 0)
        {
            RtlZeroMemory(Buffer, Chunk);
        }
        else
        {
            ULONGLONG DeviceOffset = Physical * BlockSize + InBlock;
            ULONG Direct = Chunk & ~(Sector - 1);

            if (Direct != 0)
            {
                Status = Ext4ReadDevice(Vcb->TargetDeviceObject, DeviceOffset, Buffer, Direct, FALSE);
                if (!NT_SUCCESS(Status)) break;
            }
            if (Direct != Chunk)
            {
                PUCHAR Bounce = ExAllocatePoolUninitialized(NonPagedPoolNx, Sector, TAG_EXT4_IO);

                if (Bounce == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Status = Ext4ReadDevice(Vcb->TargetDeviceObject, DeviceOffset + Direct, Bounce, Sector, FALSE);
                if (NT_SUCCESS(Status)) RtlCopyMemory(Buffer + Direct, Bounce, Chunk - Direct);
                ExFreePoolWithTag(Bounce, TAG_EXT4_IO);
                if (!NT_SUCCESS(Status)) break;
            }
            /* A block can hold stale bytes after the end of the file only until the next write of that block */
            if (Offset + Chunk > FileSize) RtlZeroMemory(Buffer + (FileSize - Offset), (SIZE_T)(Offset + Chunk - FileSize));
        }

        Buffer += Chunk;
        Offset += Chunk;
        Length -= Chunk;
    }

    return Status;
}

static
NTSTATUS
Ext4ReadVolume(
    _In_ PEXT4_VCB Vcb,
    _In_ PIRP Irp,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PUCHAR Buffer = Ext4RequestBuffer(Irp), Bounce;
    NTSTATUS Status = STATUS_SUCCESS;

    *Information = 0;
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if ((Offset % Vcb->DeviceSectorSize) != 0 || (Length % Vcb->DeviceSectorSize) != 0) return STATUS_INVALID_PARAMETER;
    if (Offset >= Vcb->PartitionLength) return STATUS_END_OF_FILE;
    Length = (ULONG)min((ULONGLONG)Length, Vcb->PartitionLength - Offset);

    Bounce = ExAllocatePoolUninitialized(NonPagedPoolNx, EXT4_IO_CHUNK, TAG_EXT4_IO);
    if (Bounce == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    while (*Information < Length)
    {
        ULONG Part = min(Length - (ULONG)*Information, EXT4_IO_CHUNK);

        Status = Ext4ReadDevice(Vcb->TargetDeviceObject, Offset + *Information, Bounce, Part, FALSE);
        if (!NT_SUCCESS(Status)) break;

        RtlCopyMemory(Buffer + *Information, Bounce, Part);
        *Information += Part;
    }

    ExFreePoolWithTag(Bounce, TAG_EXT4_IO);
    return Status;
}

static
NTSTATUS
Ext4ReadWorker(
    _In_ PEXT4_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_ LARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN Paging = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache = BooleanFlagOn(Irp->Flags, IRP_NOCACHE);
    ULONGLONG FileSize = (ULONGLONG)Fcb->Header.FileSize.QuadPart;
    ULONGLONG Offset = (ULONGLONG)ByteOffset.QuadPart;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Wanted;

    *Information = 0;
    if (!Paging && Offset >= FileSize) return STATUS_END_OF_FILE;
    Wanted = Paging ? Length : (ULONG)min((ULONGLONG)Length, FileSize - Offset);

    if (Fcb->Symlink)
    {
        PUCHAR Buffer = Ext4RequestBuffer(Irp);

        if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        Status = Ext4ReadLinkData(Fcb, Offset, Buffer, Wanted);
        if (NT_SUCCESS(Status)) *Information = Wanted;
        return Status;
    }

    if (NoCache || Paging)
    {
        PUCHAR Buffer = Ext4RequestBuffer(Irp);

        if (!Paging && ((Offset % Fcb->Vcb->DeviceSectorSize) != 0 || (Length % Fcb->Vcb->DeviceSectorSize) != 0))
            return STATUS_INVALID_PARAMETER;
        if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = Ext4ReadFileData(Fcb, Offset, Buffer, Wanted);
        if (NT_SUCCESS(Status)) *Information = Wanted;
        return Status;
    }

    _SEH2_TRY
    {
        if (FileObject->PrivateCacheMap == NULL)
            CcInitializeCacheMap(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize, FALSE, &Ext4Global.CacheCallbacks, Fcb);

        if (Stack->MinorFunction & IRP_MN_MDL)
        {
            CcMdlRead(FileObject, &ByteOffset, Wanted, &Irp->MdlAddress, &Irp->IoStatus);
            Status = Irp->IoStatus.Status;
            *Information = Irp->IoStatus.Information;
        }
        else
        {
            PVOID Buffer = Ext4RequestBuffer(Irp);

            if (Buffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                CcCopyRead(FileObject, &ByteOffset, Wanted, TRUE, Buffer, &Irp->IoStatus);
                Status = Irp->IoStatus.Status;
                *Information = Irp->IoStatus.Information;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Information = 0;
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    BOOLEAN Paging = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    LARGE_INTEGER ByteOffset = Stack->Parameters.Read.ByteOffset;
    ULONG Length = Stack->Parameters.Read.Length;
    ULONG_PTR Information = 0;
    PERESOURCE Resource;
    NTSTATUS Status;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);

    if (Stack->MinorFunction & IRP_MN_COMPLETE)
    {
        CcMdlReadComplete(FileObject, Irp->MdlAddress);
        Irp->MdlAddress = NULL;
        return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    if (Fcb->Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (Fcb->Directory) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION && ByteOffset.HighPart == -1)
        ByteOffset = FileObject->CurrentByteOffset;
    if (ByteOffset.QuadPart < 0) return Ext4CompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Length == 0) return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (Fcb->Volume)
    {
        Status = Ext4ReadVolume(Fcb->Vcb, Irp, (ULONGLONG)ByteOffset.QuadPart, Length, &Information);
        if (NT_SUCCESS(Status) && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;
        return Ext4CompleteRequest(Irp, Status, Information);
    }

    Resource = Paging ? &Fcb->PagingIoResource : &Fcb->MainResource;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);

    if (!Paging && !FsRtlCheckLockForReadAccess(&Fcb->FileLock, Irp))
        Status = STATUS_FILE_LOCK_CONFLICT;
    else
        Status = Ext4ReadWorker(Fcb, FileObject, Irp, ByteOffset, Length, &Information);

    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status) && !Paging && BooleanFlagOn(FileObject->Flags, FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Information;

    return Ext4CompleteRequest(Irp, Status, NT_SUCCESS(Status) ? Information : 0);
}
