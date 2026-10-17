/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NTFS FS library
 * FILE:        lib/fslib/ntfslib/ntfslib.c
 * PURPOSE:     NTFS lib
 * PROGRAMMERS: Pierre Schweitzer
 */

#define NTOS_MODE_USER
#include <ndk/umtypes.h>
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <fmifs/fmifs.h>
#include <stdio.h>

#include <ntfsformat.h>
#include <ntfslib_new.h>
#include <ntfs_um.h>

#define NDEBUG
#include <debug.h>

#ifndef FSCTL_ALLOW_EXTENDED_DASD_IO
#define FSCTL_ALLOW_EXTENDED_DASD_IO \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 32, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

typedef struct _NTFS_FORMAT_CONTEXT
{
    HANDLE FileHandle;
    PFMIFSCALLBACK Callback;
    ULONG Percent;
} NTFS_FORMAT_CONTEXT, *PNTFS_FORMAT_CONTEXT;

static
NTSTATUS
NtfsFormatWrite(
    IN PVOID Context,
    IN ULONGLONG Offset,
    IN ULONG Length,
    IN const VOID* Buffer)
{
    PNTFS_FORMAT_CONTEXT FormatContext = (PNTFS_FORMAT_CONTEXT)Context;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER ByteOffset;
    NTSTATUS Status;

    ByteOffset.QuadPart = (LONGLONG)Offset;

    Status = NtWriteFile(FormatContext->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         (PVOID)Buffer,
                         Length,
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(FormatContext->FileHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed at offset %I64u (Status 0x%08x)\n",
                Offset, Status);
        return Status;
    }

    if (IoStatusBlock.Information != Length)
    {
        DPRINT1("Short write at offset %I64u: %lu of %lu bytes\n",
                Offset, (ULONG)IoStatusBlock.Information, Length);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static
PVOID
NtfsFormatAllocate(
    IN PVOID Context,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
}

static
VOID
NtfsFormatFree(
    IN PVOID Context,
    IN PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static
BOOLEAN
NtfsFormatProgress(
    IN PVOID Context,
    IN ULONG PercentComplete)
{
    PNTFS_FORMAT_CONTEXT FormatContext = (PNTFS_FORMAT_CONTEXT)Context;

    if (!FormatContext->Callback)
        return TRUE;

    FormatContext->Percent = PercentComplete;

    return FormatContext->Callback(PROGRESS,
                                   0,
                                   (PVOID)&FormatContext->Percent);
}

static
VOID
NtfsFormatReport(
    IN PFMIFSCALLBACK Callback,
    IN PCSTR Format,
    ...)
{
    TEXTOUTPUT TextOut;
    CHAR TextBuf[256];
    va_list Args;

    if (!Callback)
        return;

    va_start(Args, Format);
    _vsnprintf(TextBuf, sizeof(TextBuf) - 1, Format, Args);
    va_end(Args);
    TextBuf[sizeof(TextBuf) - 1] = ANSI_NULL;

    TextOut.Lines = 1;
    TextOut.Output = TextBuf;
    Callback(OUTPUT, 0, &TextOut);
}

BOOLEAN
NTAPI
NtfsFormat(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN QuickFormat,
    IN BOOLEAN BackwardCompatible,
    IN MEDIA_TYPE MediaType,
    IN PUNICODE_STRING Label,
    IN ULONG ClusterSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    DISK_GEOMETRY DiskGeometry;
    PARTITION_INFORMATION_EX PartitionInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    NTFS_FORMAT_CONTEXT Context;
    NtfsFormatParameters Parameters;
    WCHAR LabelBuffer[33];
    LARGE_INTEGER SystemTime;
    ULONGLONG PartitionLength;
    NTSTATUS Status, LockStatus;

    DPRINT("NtfsFormat(DriveRoot '%wZ')\n", DriveRoot);

    /* NTFS has no backwards-compatible on-disk variant to select. */
    UNREFERENCED_PARAMETER(BackwardCompatible);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Callback = Callback;

    InitializeObjectAttributes(&ObjectAttributes,
                               DriveRoot,
                               0,
                               NULL,
                               NULL);

    Status = NtOpenFile(&Context.FileHandle,
                        FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed with status 0x%08x\n", Status);
        NtfsFormatReport(Callback, "Unable to open the volume (status 0x%08lx)", Status);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(Context.FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with status 0x%08x\n", Status);
        NtfsFormatReport(Callback, "Unable to query the drive geometry (status 0x%08lx)", Status);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    if (DiskGeometry.MediaType == FixedMedia)
    {
        Status = NtDeviceIoControlFile(Context.FileHandle,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &IoStatusBlock,
                                       IOCTL_DISK_GET_PARTITION_INFO_EX,
                                       NULL,
                                       0,
                                       &PartitionInfo,
                                       sizeof(PartitionInfo));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IOCTL_DISK_GET_PARTITION_INFO_EX failed with status 0x%08x\n", Status);
            NtfsFormatReport(Callback, "Unable to query the partition information (status 0x%08lx)", Status);
            NtClose(Context.FileHandle);
            return FALSE;
        }

        PartitionLength = (ULONGLONG)PartitionInfo.PartitionLength.QuadPart;
    }
    else
    {
        RtlZeroMemory(&PartitionInfo, sizeof(PartitionInfo));
        PartitionInfo.PartitionStyle = PARTITION_STYLE_MBR;

        PartitionLength = DiskGeometry.Cylinders.QuadPart *
                          (ULONGLONG)DiskGeometry.TracksPerCylinder *
                          (ULONGLONG)DiskGeometry.SectorsPerTrack *
                          (ULONGLONG)DiskGeometry.BytesPerSector;
    }

    if (DiskGeometry.BytesPerSector == 0)
    {
        DPRINT1("Device reported a zero sector size\n");
        NtfsFormatReport(Callback, "The device reports a sector size of zero");
        NtClose(Context.FileHandle);
        return FALSE;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.TotalSectors = PartitionLength / DiskGeometry.BytesPerSector;
    Parameters.BytesPerSector = DiskGeometry.BytesPerSector;

    /* ClusterSize arrives in bytes; 0 lets the formatter choose. */
    if (ClusterSize != 0)
        Parameters.SectorsPerCluster = ClusterSize / DiskGeometry.BytesPerSector;

    Parameters.SectorsPerTrack = (USHORT)DiskGeometry.SectorsPerTrack;
    Parameters.NumberOfHeads = (USHORT)DiskGeometry.TracksPerCylinder;

    if (PartitionInfo.PartitionStyle == PARTITION_STYLE_MBR)
        Parameters.HiddenSectors = PartitionInfo.Mbr.HiddenSectors;

    /* MediaType is already an NT MEDIA_TYPE here: FormatVolume() converts the
     * FMIFS media flag before calling us. */
    Parameters.MediaDescriptor = (MediaType == FixedMedia) ? 0xF8 : 0xF0;

    NtQuerySystemTime(&SystemTime);
    Parameters.CurrentTime = (ULONGLONG)SystemTime.QuadPart;

    if (Label && Label->Buffer && Label->Length != 0)
    {
        ULONG LabelLength = Label->Length / sizeof(WCHAR);

        if (LabelLength > RTL_NUMBER_OF(LabelBuffer) - 1)
            LabelLength = RTL_NUMBER_OF(LabelBuffer) - 1;

        RtlCopyMemory(LabelBuffer, Label->Buffer, LabelLength * sizeof(WCHAR));
        LabelBuffer[LabelLength] = UNICODE_NULL;
        Parameters.VolumeLabel = LabelBuffer;
    }

    Parameters.QuickFormat = QuickFormat;
    Parameters.IoContext = &Context;
    Parameters.Write = NtfsFormatWrite;
    Parameters.Allocate = NtfsFormatAllocate;
    Parameters.Free = NtfsFormatFree;
    Parameters.Progress = NtfsFormatProgress;

    if (Callback != NULL)
    {
        Context.Percent = 0;
        Callback(PROGRESS, 0, (PVOID)&Context.Percent);
    }

    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to lock volume for formatting (Status: 0x%x)\n",
                LockStatus);
        NtfsFormatReport(Callback, "Unable to lock the volume (status 0x%08lx)", LockStatus);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    /*
     * A mounted filesystem normally bounds raw I/O to the size recorded in
     * its own boot sector. That size can be smaller than the current
     * partition after delete/recreate or resize operations. Allow the
     * formatter to address the complete partition reported above.
     */
    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_ALLOW_EXTENDED_DASD_IO,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    /* RawFS and filesystems that already expose the full device need not
     * implement this control. */
    if (!NT_SUCCESS(LockStatus) &&
        LockStatus != STATUS_INVALID_PARAMETER &&
        LockStatus != STATUS_INVALID_DEVICE_REQUEST &&
        LockStatus != STATUS_NOT_SUPPORTED)
    {
        DPRINT1("Failed to enable extended raw volume I/O (Status: 0x%x)\n",
                LockStatus);
        NtfsFormatReport(Callback, "Unable to enable raw access to the whole volume (status 0x%08lx)", LockStatus);
        NtFsControlFile(Context.FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        FSCTL_UNLOCK_VOLUME,
                        NULL,
                        0,
                        NULL,
                        0);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    Status = NtfsVolumeFormat(&Parameters);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtfsVolumeFormat() failed with status 0x%08x\n", Status);
        NtfsFormatReport(Callback, "Unable to write the NTFS structures (status 0x%08lx)", Status);
    }

    /* Attempt to dismount the formatted volume */
    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_DISMOUNT_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
        DPRINT1("Failed to umount volume (Status: 0x%x)\n", LockStatus);

    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
        DPRINT1("Failed to unlock volume (Status: 0x%x)\n", LockStatus);

    NtClose(Context.FileHandle);

    DPRINT("NtfsFormat() done. Status 0x%08x\n", Status);
    return NT_SUCCESS(Status);
}

typedef struct _NTFS_CHECK_CONTEXT
{
    PFMIFSCALLBACK Callback;
    BOOLEAN Verbose;
    ULONG Reported;
} NTFS_CHECK_CONTEXT, *PNTFS_CHECK_CONTEXT;

static
VOID
NtfsCheckReport(
    IN PVOID Context,
    IN NtfsCheckEvent Event,
    IN UINT64 Argument1,
    IN UINT64 Argument2)
{
    PNTFS_CHECK_CONTEXT Check = (PNTFS_CHECK_CONTEXT)Context;

    if (Event != NtfsCheckLostClusters && !Check->Verbose && Check->Reported >= 32)
        return;
    Check->Reported++;

    switch (Event)
    {
        case NtfsCheckRecordCorrupt:
            NtfsFormatReport(Check->Callback, "File record segment %I64u is corrupt.\n", Argument1);
            break;
        case NtfsCheckRecordBitmap:
            NtfsFormatReport(Check->Callback,
                             Argument2 ? "File record segment %I64u is in use but marked free in the MFT bitmap.\n"
                                       : "File record segment %I64u is free but marked in use in the MFT bitmap.\n",
                             Argument1);
            break;
        case NtfsCheckBadRunList:
            NtfsFormatReport(Check->Callback,
                             "Attribute type 0x%I64x in file record segment %I64u has an invalid mapping pairs array.\n",
                             Argument2, Argument1);
            break;
        case NtfsCheckClusterOutOfRange:
            NtfsFormatReport(Check->Callback,
                             "File record segment %I64u refers to cluster 0x%I64x beyond the end of the volume.\n",
                             Argument1, Argument2);
            break;
        case NtfsCheckCrossLinked:
            NtfsFormatReport(Check->Callback,
                             "File record segment %I64u shares cluster 0x%I64x with another file.\n",
                             Argument1, Argument2);
            break;
        case NtfsCheckIndexCorrupt:
            NtfsFormatReport(Check->Callback,
                             "Index $I30 of file %I64u is corrupt at VCN 0x%I64x.\n",
                             Argument1, Argument2);
            break;
        case NtfsCheckIndexBadEntry:
            NtfsFormatReport(Check->Callback,
                             "An index entry in index $I30 of file %I64u points to unused or stale file 0x%I64x.\n",
                             Argument1, Argument2);
            break;
        case NtfsCheckNameNotIndexed:
            NtfsFormatReport(Check->Callback,
                             "A name of file %I64u is missing from its parent index.\n",
                             Argument1);
            break;
        case NtfsCheckUsedClustersFree:
            NtfsFormatReport(Check->Callback,
                             "Clusters 0x%I64x to 0x%I64x are in use but marked free in the volume bitmap.\n",
                             Argument1, Argument1 + Argument2 - 1);
            break;
        case NtfsCheckLostClusters:
            NtfsFormatReport(Check->Callback,
                             "%I64u clusters are marked allocated in the volume bitmap but not in use.\n",
                             Argument1);
            break;
        case NtfsCheckUsedClustersClaimed:
            NtfsFormatReport(Check->Callback,
                             "Marked clusters 0x%I64x to 0x%I64x as in use in the volume bitmap.\n",
                             Argument1, Argument1 + Argument2 - 1);
            break;
        case NtfsCheckLostClustersFreed:
            NtfsFormatReport(Check->Callback,
                             "Freed clusters 0x%I64x to 0x%I64x in the volume bitmap.\n",
                             Argument1, Argument1 + Argument2 - 1);
            break;
    }
}

static
NTSTATUS
NtfsCheckControl(
    IN HANDLE Handle,
    IN ULONG Code,
    OUT PVOID Output,
    IN ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatusBlock;

    return NtFsControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Code, NULL, 0, Output, OutputLength);
}

BOOLEAN
NTAPI
NtfsChkdsk(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PVOID pUnknown1,
    IN PVOID pUnknown2,
    IN PVOID pUnknown3,
    IN PVOID pUnknown4,
    IN PULONG ExitStatus)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTFS_CHECK_CONTEXT Check;
    NtfsCheckResult Result;
    PNtfsVolume Volume = NULL;
    HANDLE Handle;
    ULONG BytesPerSector = 0;
    ULONG DirtyMask = 0;
    USHORT Flags = 0;
    BOOLEAN Locked = FALSE;
    BOOLEAN Dirty;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ScanDrive);
    UNREFERENCED_PARAMETER(pUnknown1);
    UNREFERENCED_PARAMETER(pUnknown2);
    UNREFERENCED_PARAMETER(pUnknown3);
    UNREFERENCED_PARAMETER(pUnknown4);

    *ExitStatus = (ULONG)STATUS_DISK_CORRUPT_ERROR;
    RtlZeroMemory(&Check, sizeof(Check));
    Check.Callback = Callback;
    Check.Verbose = Verbose;

    InitializeObjectAttributes(&ObjectAttributes, DriveRoot, 0, NULL, NULL);
    Status = NtOpenFile(&Handle,
                        FILE_GENERIC_READ | (FixErrors ? FILE_GENERIC_WRITE : 0) | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        NtfsFormatReport(Callback, "Unable to open the volume (status 0x%08lx).\n", Status);
        return FALSE;
    }

    Status = NtfsCheckControl(Handle, FSCTL_IS_VOLUME_DIRTY, &DirtyMask, sizeof(DirtyMask));
    Dirty = NT_SUCCESS(Status) && (DirtyMask & VOLUME_IS_DIRTY);
    if (CheckOnlyIfDirty && NT_SUCCESS(Status) && !Dirty)
    {
        NtClose(Handle);
        *ExitStatus = (ULONG)STATUS_SUCCESS;
        return TRUE;
    }
    if (CheckOnlyIfDirty && Dirty && !FixErrors)
    {
        NtfsFormatReport(Callback, "The volume is dirty.\n");
        NtClose(Handle);
        return TRUE;
    }

    if (FixErrors)
    {
        Status = NtfsCheckControl(Handle, FSCTL_LOCK_VOLUME, NULL, 0);
        Locked = NT_SUCCESS(Status);
        if (!Locked)
            NtfsFormatReport(Callback, "The volume is in use, so problems will be reported but not fixed.\n");
    }

    Status = NtfsDiskInitializeUm(Handle, &BytesPerSector);
    if (NT_SUCCESS(Status))
    {
        NtfsSetReadOnlyMode(Locked ? FALSE : TRUE);
        Status = NtfsProbePartitionAndOpenVolume(BytesPerSector, &Volume);
        NtfsSetReadOnlyMode(FALSE);
    }
    if (NT_SUCCESS(Status) && NT_SUCCESS(NtfsVolumeQueryFlags(Volume, &Flags)) && (Flags & VOL_DIRTY))
        Dirty = TRUE;
    if (NT_SUCCESS(Status))
    {
        NtfsFormatReport(Callback, "Checking file records, indexes and the volume bitmap...\n");
        Status = NtfsVolumeCheck(Volume, Locked, NtfsCheckReport, &Check, &Result);
    }
    if (NT_SUCCESS(Status) && Locked && Dirty && Result.Errors == 0)
        Status = NtfsVolumeSetFlags(Volume, 0, VOL_DIRTY);

    if (Volume)
        NtfsVolumeDestroy(Volume);
    if (Locked)
    {
        NtfsCheckControl(Handle, FSCTL_DISMOUNT_VOLUME, NULL, 0);
        NtfsCheckControl(Handle, FSCTL_UNLOCK_VOLUME, NULL, 0);
    }
    NtClose(Handle);

    if (!NT_SUCCESS(Status))
    {
        NtfsFormatReport(Callback, "The volume could not be checked (status 0x%08lx).\n", Status);
        return FALSE;
    }

    NtfsFormatReport(Callback, "%I64u file records processed.\n", Result.FileRecords);
    NtfsFormatReport(Callback, "%I64u index entries processed in %I64u directories.\n",
                     Result.IndexEntries, Result.Directories);
    if (Result.ClaimedClusters)
        NtfsFormatReport(Callback, "%I64u clusters in use were marked allocated.\n", Result.ClaimedClusters);
    if (Result.FreedClusters)
        NtfsFormatReport(Callback, "%I64u clusters were returned to free space.\n", Result.FreedClusters);
    if (Result.Errors)
    {
        NtfsFormatReport(Callback, "Problems were found on the volume.\n");
        return TRUE;
    }

    NtfsFormatReport(Callback, "No problems were found on the volume.\n");
    *ExitStatus = (ULONG)STATUS_SUCCESS;
    return TRUE;
}
