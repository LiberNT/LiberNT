/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading and writing a cached file without building a request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

typedef struct _FSRTL_FAST_IO_HOLD
{
    PFSRTL_ADVANCED_FCB_HEADER Header;
    PERESOURCE Resource;
    PIRP TopLevel;
    BOOLEAN TopLevelSet;
} FSRTL_FAST_IO_HOLD, *PFSRTL_FAST_IO_HOLD;

/* PRIVATE FUNCTIONS **********************************************************/

static
PFAST_IO_DISPATCH
FsRtlpFastIoOf(
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    if ((DeviceObject == NULL) || (DeviceObject->DriverObject == NULL))
    {
        return NULL;
    }

    return DeviceObject->DriverObject->FastIoDispatch;
}

static
BOOLEAN
FsRtlpFastIoHas(
    _In_opt_ PFAST_IO_DISPATCH Fast,
    _In_ ULONG MemberOffset,
    _In_ PVOID Member)
{
    if (Fast == NULL) return FALSE;
    if (Fast->SizeOfFastIoDispatch < MemberOffset + sizeof(PVOID)) return FALSE;

    return (BOOLEAN)(Member != NULL);
}

#define FSRTL_HAS_FAST_IO(Fast, Member)                                       \
    FsRtlpFastIoHas((Fast),                                                   \
                    FIELD_OFFSET(FAST_IO_DISPATCH, Member),                   \
                    ((Fast) != NULL) ? (PVOID)(Fast)->Member : NULL)

static
VOID
FsRtlpEnterFastIo(
    _Out_ PFSRTL_FAST_IO_HOLD Hold,
    _In_ PFILE_OBJECT FileObject)
{
    Hold->Header = (PFSRTL_ADVANCED_FCB_HEADER)FileObject->FsContext;
    Hold->Resource = NULL;
    Hold->TopLevel = NULL;
    Hold->TopLevelSet = FALSE;
}

static
BOOLEAN
FsRtlpAcquireFcbShared(
    _Inout_ PFSRTL_FAST_IO_HOLD Hold,
    _In_ BOOLEAN Wait)
{
    PERESOURCE Resource = Hold->Header->Resource;

    FsRtlEnterFileSystem();

    if (Resource == NULL) return TRUE;

    if (!ExAcquireResourceSharedLite(Resource, Wait))
    {
        FsRtlExitFileSystem();
        return FALSE;
    }

    Hold->Resource = Resource;

    return TRUE;
}

static
BOOLEAN
FsRtlpAcquireFcbExclusive(
    _Inout_ PFSRTL_FAST_IO_HOLD Hold,
    _In_ BOOLEAN Wait)
{
    PERESOURCE Resource = Hold->Header->Resource;

    FsRtlEnterFileSystem();

    if (Resource == NULL) return TRUE;

    if (!ExAcquireResourceExclusiveLite(Resource, Wait))
    {
        FsRtlExitFileSystem();
        return FALSE;
    }

    Hold->Resource = Resource;

    return TRUE;
}

static
VOID
FsRtlpReleaseFcb(
    _Inout_ PFSRTL_FAST_IO_HOLD Hold)
{
    if (Hold->Resource != NULL)
    {
        ExReleaseResourceLite(Hold->Resource);
        Hold->Resource = NULL;
    }

    FsRtlExitFileSystem();
}

static
VOID
FsRtlpSetTopLevel(
    _Inout_ PFSRTL_FAST_IO_HOLD Hold)
{
    if (IoGetTopLevelIrp() != NULL) return;

    IoSetTopLevelIrp((PIRP)FSRTL_FAST_IO_TOP_LEVEL_IRP);
    Hold->TopLevelSet = TRUE;
}

static
VOID
FsRtlpResetTopLevel(
    _Inout_ PFSRTL_FAST_IO_HOLD Hold)
{
    if (!Hold->TopLevelSet) return;

    IoSetTopLevelIrp(NULL);
    Hold->TopLevelSet = FALSE;
}

static
BOOLEAN
FsRtlpFastIoAllowed(
    _In_ PFSRTL_FAST_IO_HOLD Hold,
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ BOOLEAN Read,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    PFAST_IO_DISPATCH Fast;

    if (Hold->Header->IsFastIoPossible == FastIoIsNotPossible) return FALSE;
    if (Hold->Header->IsFastIoPossible == FastIoIsPossible) return TRUE;

    Fast = FsRtlpFastIoOf(DeviceObject);

    if (!FSRTL_HAS_FAST_IO(Fast, FastIoCheckIfPossible)) return FALSE;

    return Fast->FastIoCheckIfPossible(FileObject,
                                       FileOffset,
                                       Length,
                                       Wait,
                                       LockKey,
                                       Read,
                                       IoStatus,
                                       DeviceObject);
}

static
BOOLEAN
FsRtlpIsAppendOffset(
    _In_ PLARGE_INTEGER FileOffset)
{
    return (BOOLEAN)((FileOffset->LowPart == FILE_WRITE_TO_END_OF_FILE) &&
                     (FileOffset->HighPart == -1));
}

static
VOID
FsRtlpPublishSizes(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFSRTL_ADVANCED_FCB_HEADER Header)
{
    CC_FILE_SIZES Sizes;

    Sizes.AllocationSize = Header->AllocationSize;
    Sizes.FileSize = Header->FileSize;
    Sizes.ValidDataLength = Header->ValidDataLength;

    CcSetFileSizes(FileObject, &Sizes);
}

/*
 * Makes the stream long enough for a write that ends past the valid data,
 * zeroing whatever the write does not cover so the gap never shows old data.
 * The allocation itself is the file system's to grow, so a write past it is
 * refused here.
 */
static
BOOLEAN
FsRtlpGrowForWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFSRTL_ADVANCED_FCB_HEADER Header,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER End,
    _In_ BOOLEAN Wait)
{
    LARGE_INTEGER Gap = Header->ValidDataLength;

    if (End->QuadPart > Header->AllocationSize.QuadPart) return FALSE;

    if (End->QuadPart > Header->FileSize.QuadPart)
    {
        Header->FileSize = *End;
        SetFlag(Header->Flags, FSRTL_FLAG_FILE_LENGTH_CHANGED);
        FsRtlpPublishSizes(FileObject, Header);
    }

    if (FileOffset->QuadPart > Gap.QuadPart)
    {
        if (!CcZeroData(FileObject, &Gap, FileOffset, Wait)) return FALSE;
    }

    return TRUE;
}

static
VOID
FsRtlpCommitWriteEnd(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFSRTL_ADVANCED_FCB_HEADER Header,
    _In_ PLARGE_INTEGER End)
{
    if (End->QuadPart <= Header->ValidDataLength.QuadPart) return;

    Header->ValidDataLength = *End;
    SetFlag(Header->Flags, FSRTL_FLAG_FILE_LENGTH_CHANGED);
    FsRtlpPublishSizes(FileObject, Header);
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlIncrementCcFastReadNoWait(VOID)
{
    InterlockedIncrement((PLONG)&CcFastReadNoWait);
}

VOID
NTAPI
FsRtlIncrementCcFastReadWait(VOID)
{
    InterlockedIncrement((PLONG)&CcFastReadWait);
}

VOID
NTAPI
FsRtlIncrementCcFastReadNotPossible(VOID)
{
    InterlockedIncrement((PLONG)&CcFastReadNotPossible);
}

VOID
NTAPI
FsRtlIncrementCcFastReadResourceMiss(VOID)
{
    InterlockedIncrement((PLONG)&CcFastReadResourceMiss);
}

VOID
NTAPI
FsRtlUpdateDiskCounters(
    _In_ ULONG64 BytesRead,
    _In_ ULONG64 BytesWritten)
{
    PPROCESS_DISK_COUNTERS Counters = PsGetCurrentProcess()->DiskCounters;

    if (Counters == NULL) return;

    if (BytesRead != 0)
    {
        InterlockedExchangeAdd64((PLONGLONG)&Counters->BytesRead,
                                 (LONGLONG)BytesRead);
        InterlockedIncrement64((PLONGLONG)&Counters->ReadOperationCount);
    }

    if (BytesWritten != 0)
    {
        InterlockedExchangeAdd64((PLONGLONG)&Counters->BytesWritten,
                                 (LONGLONG)BytesWritten);
        InterlockedIncrement64((PLONGLONG)&Counters->WriteOperationCount);
    }
}

BOOLEAN
NTAPI
FsRtlCopyRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    FSRTL_FAST_IO_HOLD Hold;
    LARGE_INTEGER End;
    ULONG Readable;
    BOOLEAN Result = TRUE;

    PAGED_CODE();

    if (Length == 0)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return TRUE;
    }

    End.QuadPart = FileOffset->QuadPart + Length;

    if ((End.QuadPart <= 0) || (FileOffset->QuadPart < 0)) return FALSE;

    FsRtlpEnterFastIo(&Hold, FileObject);
    if (Hold.Header == NULL) return FALSE;

    if (!FsRtlpAcquireFcbShared(&Hold, Wait))
    {
        FsRtlIncrementCcFastReadResourceMiss();
        return FALSE;
    }

    if (!FsRtlpFastIoAllowed(&Hold,
                             FileObject,
                             FileOffset,
                             Length,
                             Wait,
                             LockKey,
                             TRUE,
                             IoStatus,
                             DeviceObject))
    {
        FsRtlpReleaseFcb(&Hold);
        FsRtlIncrementCcFastReadNotPossible();
        return FALSE;
    }

    if (FileOffset->QuadPart >= Hold.Header->FileSize.QuadPart)
    {
        FsRtlpReleaseFcb(&Hold);
        IoStatus->Status = STATUS_END_OF_FILE;
        IoStatus->Information = 0;
        return TRUE;
    }

    Readable = Length;

    if (End.QuadPart > Hold.Header->FileSize.QuadPart)
    {
        Readable = (ULONG)(Hold.Header->FileSize.QuadPart -
                           FileOffset->QuadPart);
    }

    FsRtlpSetTopLevel(&Hold);

    _SEH2_TRY
    {
        Result = CcCopyRead(FileObject,
                            FileOffset,
                            Readable,
                            Wait,
                            Buffer,
                            IoStatus);
    }
    _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = FALSE;
    }
    _SEH2_END;

    FsRtlpResetTopLevel(&Hold);

    if (Result)
    {
        FileObject->CurrentByteOffset.QuadPart =
            FileOffset->QuadPart + IoStatus->Information;
        SetFlag(FileObject->Flags, FO_FILE_FAST_IO_READ);

        if (Wait) FsRtlIncrementCcFastReadWait();
        else FsRtlIncrementCcFastReadNoWait();
    }

    FsRtlpReleaseFcb(&Hold);

    return Result;
}

BOOLEAN
NTAPI
FsRtlCopyWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    FSRTL_FAST_IO_HOLD Hold;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    BOOLEAN Exclusive;
    BOOLEAN Append;
    BOOLEAN Grow = FALSE;
    BOOLEAN Result = TRUE;

    PAGED_CODE();

    End.QuadPart = 0;
    Start.QuadPart = 0;

    if (Length == 0)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return TRUE;
    }

    if (FlagOn(FileObject->Flags, FO_WRITE_THROUGH)) return FALSE;

    if (!CcCopyWriteWontFlush(FileObject, FileOffset, Length)) return FALSE;

    if (!CcCanIWrite(FileObject, Length, Wait, FALSE)) return FALSE;

    FsRtlpEnterFastIo(&Hold, FileObject);
    if (Hold.Header == NULL) return FALSE;

    Append = FsRtlpIsAppendOffset(FileOffset);

    if (!Append && (FileOffset->QuadPart < 0)) return FALSE;

    Exclusive = Append;

    if (!Append)
    {
        End.QuadPart = FileOffset->QuadPart + Length;
        if (End.QuadPart <= 0) return FALSE;

        Exclusive = (BOOLEAN)(End.QuadPart >
                              Hold.Header->ValidDataLength.QuadPart);
    }

    if (Exclusive)
    {
        if (!FsRtlpAcquireFcbExclusive(&Hold, Wait)) return FALSE;
    }
    else if (!FsRtlpAcquireFcbShared(&Hold, Wait))
    {
        return FALSE;
    }

    if (Append)
    {
        Start = Hold.Header->FileSize;
        End.QuadPart = Start.QuadPart + Length;

        if (End.QuadPart <= 0)
        {
            FsRtlpReleaseFcb(&Hold);
            return FALSE;
        }

        FileOffset = &Start;
    }

    if (End.QuadPart > Hold.Header->ValidDataLength.QuadPart)
    {
        if (!Exclusive ||
            (End.QuadPart > Hold.Header->AllocationSize.QuadPart))
        {
            FsRtlpReleaseFcb(&Hold);
            return FALSE;
        }

        Grow = TRUE;
    }

    if (!FsRtlpFastIoAllowed(&Hold,
                             FileObject,
                             FileOffset,
                             Length,
                             Wait,
                             LockKey,
                             FALSE,
                             IoStatus,
                             DeviceObject))
    {
        FsRtlpReleaseFcb(&Hold);
        return FALSE;
    }

    FsRtlpSetTopLevel(&Hold);

    _SEH2_TRY
    {
        if (Grow)
        {
            Result = FsRtlpGrowForWrite(FileObject,
                                        Hold.Header,
                                        FileOffset,
                                        &End,
                                        Wait);
        }

        if (Result)
        {
            Result = CcCopyWrite(FileObject, FileOffset, Length, Wait, Buffer);
        }

        if (Result && Grow)
        {
            FsRtlpCommitWriteEnd(FileObject, Hold.Header, &End);
        }
    }
    _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = FALSE;
    }
    _SEH2_END;

    FsRtlpResetTopLevel(&Hold);

    if (Result)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = Length;

        FileObject->CurrentByteOffset.QuadPart = End.QuadPart;
        SetFlag(FileObject->Flags, FO_FILE_MODIFIED);
        SetFlag(Hold.Header->Flags, FSRTL_FLAG_FILE_MODIFIED);
    }

    FsRtlpReleaseFcb(&Hold);

    return Result;
}

BOOLEAN
NTAPI
FsRtlMdlReadDev(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Outptr_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    FSRTL_FAST_IO_HOLD Hold;
    LARGE_INTEGER End;
    ULONG Readable;
    BOOLEAN Result = TRUE;

    PAGED_CODE();

    if (Length == 0)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return TRUE;
    }

    if (FileObject->PrivateCacheMap == NULL) return FALSE;

    End.QuadPart = FileOffset->QuadPart + Length;
    if ((End.QuadPart <= 0) || (FileOffset->QuadPart < 0)) return FALSE;

    FsRtlpEnterFastIo(&Hold, FileObject);
    if (Hold.Header == NULL) return FALSE;

    if (!FsRtlpAcquireFcbShared(&Hold, TRUE)) return FALSE;

    if (!FsRtlpFastIoAllowed(&Hold,
                             FileObject,
                             FileOffset,
                             Length,
                             TRUE,
                             LockKey,
                             TRUE,
                             IoStatus,
                             DeviceObject))
    {
        FsRtlpReleaseFcb(&Hold);
        return FALSE;
    }

    if (FileOffset->QuadPart >= Hold.Header->FileSize.QuadPart)
    {
        FsRtlpReleaseFcb(&Hold);
        IoStatus->Status = STATUS_END_OF_FILE;
        IoStatus->Information = 0;
        return TRUE;
    }

    Readable = Length;

    if (End.QuadPart > Hold.Header->FileSize.QuadPart)
    {
        Readable = (ULONG)(Hold.Header->FileSize.QuadPart -
                           FileOffset->QuadPart);
    }

    FsRtlpSetTopLevel(&Hold);

    _SEH2_TRY
    {
        CcMdlRead(FileObject, FileOffset, Readable, MdlChain, IoStatus);
    }
    _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = FALSE;
    }
    _SEH2_END;

    FsRtlpResetTopLevel(&Hold);

    if (Result)
    {
        FileObject->CurrentByteOffset.QuadPart =
            FileOffset->QuadPart + IoStatus->Information;
        SetFlag(FileObject->Flags, FO_FILE_FAST_IO_READ);
    }

    FsRtlpReleaseFcb(&Hold);

    return Result;
}

BOOLEAN
NTAPI
FsRtlMdlRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Outptr_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(DeviceObject);

    PAGED_CODE();

    if (FSRTL_HAS_FAST_IO(Fast, MdlRead))
    {
        return Fast->MdlRead(FileObject,
                             FileOffset,
                             Length,
                             LockKey,
                             MdlChain,
                             IoStatus,
                             DeviceObject);
    }

    return FsRtlMdlReadDev(FileObject,
                           FileOffset,
                           Length,
                           LockKey,
                           MdlChain,
                           IoStatus,
                           DeviceObject);
}

BOOLEAN
NTAPI
FsRtlMdlReadCompleteDev(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    CcMdlReadComplete(FileObject, MdlChain);

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlMdlReadComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(DeviceObject);

    PAGED_CODE();

    if (FSRTL_HAS_FAST_IO(Fast, MdlReadComplete))
    {
        return Fast->MdlReadComplete(FileObject, MdlChain, DeviceObject);
    }

    return FsRtlMdlReadCompleteDev(FileObject, MdlChain, DeviceObject);
}

BOOLEAN
NTAPI
FsRtlPrepareMdlWriteDev(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Outptr_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    FSRTL_FAST_IO_HOLD Hold;
    LARGE_INTEGER End;
    BOOLEAN Grow;
    BOOLEAN Result = TRUE;

    PAGED_CODE();

    if (Length == 0)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return TRUE;
    }

    if (FileObject->PrivateCacheMap == NULL) return FALSE;
    if (FileOffset->QuadPart < 0) return FALSE;

    End.QuadPart = FileOffset->QuadPart + Length;
    if (End.QuadPart <= 0) return FALSE;

    if (!CcCanIWrite(FileObject, Length, TRUE, FALSE)) return FALSE;

    FsRtlpEnterFastIo(&Hold, FileObject);
    if (Hold.Header == NULL) return FALSE;

    Grow = (BOOLEAN)(End.QuadPart > Hold.Header->ValidDataLength.QuadPart);

    if (Grow)
    {
        if (!FsRtlpAcquireFcbExclusive(&Hold, TRUE)) return FALSE;

        if (End.QuadPart > Hold.Header->AllocationSize.QuadPart)
        {
            FsRtlpReleaseFcb(&Hold);
            return FALSE;
        }
    }
    else if (!FsRtlpAcquireFcbShared(&Hold, TRUE))
    {
        return FALSE;
    }

    if (!FsRtlpFastIoAllowed(&Hold,
                             FileObject,
                             FileOffset,
                             Length,
                             TRUE,
                             LockKey,
                             FALSE,
                             IoStatus,
                             DeviceObject))
    {
        FsRtlpReleaseFcb(&Hold);
        return FALSE;
    }

    FsRtlpSetTopLevel(&Hold);

    _SEH2_TRY
    {
        if (Grow)
        {
            Result = FsRtlpGrowForWrite(FileObject,
                                        Hold.Header,
                                        FileOffset,
                                        &End,
                                        TRUE);
        }

        if (Result)
        {
            CcPrepareMdlWrite(FileObject,
                              FileOffset,
                              Length,
                              MdlChain,
                              IoStatus);
        }

        if (Result && Grow)
        {
            FsRtlpCommitWriteEnd(FileObject, Hold.Header, &End);
        }
    }
    _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = FALSE;
    }
    _SEH2_END;

    FsRtlpResetTopLevel(&Hold);

    if (Result)
    {
        SetFlag(FileObject->Flags, FO_FILE_MODIFIED);
        SetFlag(Hold.Header->Flags, FSRTL_FLAG_FILE_MODIFIED);
    }

    FsRtlpReleaseFcb(&Hold);

    return Result;
}

BOOLEAN
NTAPI
FsRtlPrepareMdlWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Outptr_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(DeviceObject);

    PAGED_CODE();

    if (FSRTL_HAS_FAST_IO(Fast, PrepareMdlWrite))
    {
        return Fast->PrepareMdlWrite(FileObject,
                                     FileOffset,
                                     Length,
                                     LockKey,
                                     MdlChain,
                                     IoStatus,
                                     DeviceObject);
    }

    return FsRtlPrepareMdlWriteDev(FileObject,
                                   FileOffset,
                                   Length,
                                   LockKey,
                                   MdlChain,
                                   IoStatus,
                                   DeviceObject);
}

BOOLEAN
NTAPI
FsRtlMdlWriteCompleteDev(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (FlagOn(FileObject->Flags, FO_WRITE_THROUGH)) return FALSE;

    CcMdlWriteComplete(FileObject, FileOffset, MdlChain);

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlMdlWriteComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(DeviceObject);

    PAGED_CODE();

    if (FSRTL_HAS_FAST_IO(Fast, MdlWriteComplete))
    {
        return Fast->MdlWriteComplete(FileObject,
                                      FileOffset,
                                      MdlChain,
                                      DeviceObject);
    }

    return FsRtlMdlWriteCompleteDev(FileObject,
                                    FileOffset,
                                    MdlChain,
                                    DeviceObject);
}

/* EOF */
