/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Fast I/O and the cache manager's callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static
BOOLEAN
NTAPI
BtrfsFastIoCheckIfPossible(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ BOOLEAN CheckForReadOperation,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;
    LARGE_INTEGER LargeLength;

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Fcb == NULL || Fcb->Directory || Fcb->Volume || !CheckForReadOperation || Fcb->Vcb->Dismounted) return FALSE;

    LargeLength.QuadPart = Length;
    return FsRtlFastCheckLockForRead(&Fcb->FileLock, FileOffset, &LargeLength, LockKey, FileObject, PsGetCurrentProcess());
}

static
BOOLEAN
NTAPI
BtrfsFastIoRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;

    if (Fcb == NULL || Fcb->Directory || Fcb->Volume || Fcb->Vcb->Dismounted) return FALSE;
    return FsRtlCopyRead(FileObject, FileOffset, Length, Wait, LockKey, Buffer, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
BtrfsFastIoWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(LockKey);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

static
VOID
NTAPI
BtrfsAcquireForCreateSection(
    _In_ PFILE_OBJECT FileObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;

    ASSERT(Fcb != NULL && Fcb->Signature == BTRFS_FCB_SIGNATURE);
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
}

static
VOID
NTAPI
BtrfsReleaseForCreateSection(
    _In_ PFILE_OBJECT FileObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;

    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();
}

static
NTSTATUS
NTAPI
BtrfsAcquireForCcFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->PagingIoResource, TRUE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BtrfsReleaseForCcFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PBTRFS_FCB Fcb = FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    ExReleaseResourceLite(&Fcb->PagingIoResource);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

static
BOOLEAN
NTAPI
BtrfsAcquireForLazyWrite(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    PBTRFS_FCB Fcb = Context;

    if (!ExAcquireResourceSharedLite(&Fcb->PagingIoResource, Wait)) return FALSE;
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

static
VOID
NTAPI
BtrfsReleaseFromLazyWrite(
    _In_ PVOID Context)
{
    PBTRFS_FCB Fcb = Context;

    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&Fcb->PagingIoResource);
}

static
BOOLEAN
NTAPI
BtrfsAcquireForReadAhead(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    PBTRFS_FCB Fcb = Context;

    if (!ExAcquireResourceSharedLite(&Fcb->MainResource, Wait)) return FALSE;
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

static
VOID
NTAPI
BtrfsReleaseFromReadAhead(
    _In_ PVOID Context)
{
    PBTRFS_FCB Fcb = Context;

    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&Fcb->MainResource);
}

VOID
BtrfsInitializeFastIo(
    _Out_ PFAST_IO_DISPATCH Dispatch,
    _Out_ PCACHE_MANAGER_CALLBACKS Callbacks)
{
    RtlZeroMemory(Dispatch, sizeof(*Dispatch));
    Dispatch->SizeOfFastIoDispatch = sizeof(*Dispatch);
    Dispatch->FastIoCheckIfPossible = BtrfsFastIoCheckIfPossible;
    Dispatch->FastIoRead = BtrfsFastIoRead;
    Dispatch->FastIoWrite = BtrfsFastIoWrite;
    Dispatch->AcquireFileForNtCreateSection = BtrfsAcquireForCreateSection;
    Dispatch->ReleaseFileForNtCreateSection = BtrfsReleaseForCreateSection;
    Dispatch->AcquireForCcFlush = BtrfsAcquireForCcFlush;
    Dispatch->ReleaseForCcFlush = BtrfsReleaseForCcFlush;
    Dispatch->MdlRead = FsRtlMdlReadDev;
    Dispatch->MdlReadComplete = FsRtlMdlReadCompleteDev;

    Callbacks->AcquireForLazyWrite = BtrfsAcquireForLazyWrite;
    Callbacks->ReleaseFromLazyWrite = BtrfsReleaseFromLazyWrite;
    Callbacks->AcquireForReadAhead = BtrfsAcquireForReadAhead;
    Callbacks->ReleaseFromReadAhead = BtrfsReleaseFromReadAhead;
}
