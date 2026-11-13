/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FAT backend for the boot device file interface (NtfsBl*)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * On the architectures that link bootntfs, the NtfsBl* interface is served by
 * the NTFS library (lib/ntfsenv.cpp).  The NTFS library cannot be built for
 * LoongArch64 yet, so this module provides the same interface on top of the
 * FAT volume the boot loader already mounts: lib/exfat.c owns a single FatFs
 * volume on logical drive "0:" and its diskio layer, and this file simply
 * drives the FatFs API on that volume.
 *
 * The backend is READ-ONLY.  Boot from a FAT ESP only reads the loader image,
 * the kernel, drivers and the registry hives; every mutating entry point
 * returns STATUS_NOT_SUPPORTED so a caller never believes an update was
 * applied to a volume that the loader cannot write.  FatFs itself is built
 * writable, but lib/exfat.c reports the medium as write protected
 * (disk_write() returns RES_WRPRT), so writes could not be honoured anyway.
 *
 * Mount state is deliberately shared with lib/fs.c: NtfsBlUnmount() does not
 * unmount the volume, because fs.c caches its own "mounted" flag and a later
 * FsReadFileA() would then run against a released FATFS object.
 */

#include <bl.h>
#include <ff.h>
#include <ntfsbl.h>

/* The boot loader always mounts its FAT volume on this logical drive. */
#define FATBL_DRIVE         "0:"
#define FATBL_PATH_CHARS    520

typedef struct _FATBL_FILE
{
    FIL File;
    BOOLEAN Directory;
} FATBL_FILE, *PFATBL_FILE;

static BOOLEAN FatBlMounted;

static
NTSTATUS
FatBlStatus(
    _In_ FRESULT Result)
{
    switch (Result)
    {
        case FR_OK:
            return STATUS_SUCCESS;

        case FR_DISK_ERR:
            return STATUS_UNEXPECTED_IO_ERROR;

        case FR_NOT_READY:
        case FR_NOT_ENABLED:
            return STATUS_DEVICE_NOT_READY;

        case FR_NO_FILE:
        case FR_NO_PATH:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case FR_INVALID_NAME:
            return STATUS_OBJECT_NAME_INVALID;

        case FR_DENIED:
            return STATUS_ACCESS_DENIED;

        case FR_EXIST:
            return STATUS_OBJECT_NAME_COLLISION;

        case FR_INVALID_OBJECT:
            return STATUS_INVALID_HANDLE;

        case FR_WRITE_PROTECTED:
            return STATUS_MEDIA_WRITE_PROTECTED;

        case FR_INVALID_DRIVE:
            return STATUS_NO_SUCH_DEVICE;

        case FR_NO_FILESYSTEM:
            return STATUS_UNRECOGNIZED_VOLUME;

        case FR_NOT_ENOUGH_CORE:
            return STATUS_INSUFFICIENT_RESOURCES;

        case FR_TOO_MANY_OPEN_FILES:
            return STATUS_TOO_MANY_OPENED_FILES;

        case FR_LOCKED:
            return STATUS_SHARING_VIOLATION;

        case FR_INT_ERR:
        case FR_MKFS_ABORTED:
        case FR_TIMEOUT:
        case FR_INVALID_PARAMETER:
            return STATUS_UNSUCCESSFUL;
    }

    return STATUS_UNSUCCESSFUL;
}

/*
 * Translate a loader path ("\\dir\\file" or "dir/file") into a FatFs path on
 * the boot drive ("0:/dir/file").  A path that is empty after the leading
 * separators are removed is rejected, matching the NTFS backend.
 */
static
NTSTATUS
FatBlPath(
    _In_ PCSTR Path,
    _Out_writes_(FATBL_PATH_CHARS) PCHAR Output)
{
    ULONG Index = 3, Length = 0;
    CHAR Character;

    if (!Path)
        return STATUS_OBJECT_NAME_INVALID;

    while (Path[Length] == '/' || Path[Length] == '\\')
        Length++;

    if (Path[Length] == ANSI_NULL)
        return STATUS_OBJECT_NAME_INVALID;

    Output[0] = FATBL_DRIVE[0];
    Output[1] = FATBL_DRIVE[1];
    Output[2] = '/';

    for (; Path[Length] != ANSI_NULL; Length++)
    {
        Character = Path[Length];
        if (Character == '\\')
            Character = '/';

        /* Collapse the repeated separators a caller may produce. */
        if (Character == '/' && Output[Index - 1] == '/')
            continue;

        if (Index + 1 >= FATBL_PATH_CHARS)
            return STATUS_NAME_TOO_LONG;

        Output[Index++] = Character;
    }

    while (Index > 3 && Output[Index - 1] == '/')
        Index--;

    Output[Index] = ANSI_NULL;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsBlMount(
    _In_ struct _DK_DEVICE* Device)
{
    if (!Device)
        return STATUS_INVALID_PARAMETER;

    FatBlMounted = FALSE;

    /*
     * lib/exfat.c probes the boot sector and mounts FAT12/FAT16/FAT32 (and
     * exFAT) through FatFs; a successful mount means the volume is readable.
     */
    if (!ExFatMount((PDK_DEVICE)Device))
        return STATUS_UNRECOGNIZED_VOLUME;

    FatBlMounted = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsBlUnmount(VOID)
{
    /*
     * The volume stays mounted: lib/fs.c caches the mount it made through
     * ExFatMount() and would keep using the volume after a release here.
     * Nothing has to be flushed because this backend never writes.
     */
    FatBlMounted = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsBlReadFile(
    _In_ PCSTR Path,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Read)
{
    CHAR Name[FATBL_PATH_CHARS];
    FIL File;
    UINT BytesRead = 0;
    NTSTATUS Status;
    FRESULT Result;

    *Read = 0;
    if (!FatBlMounted)
        return STATUS_DEVICE_NOT_READY;

    Status = FatBlPath(Path, Name);
    if (!NT_SUCCESS(Status))
        return Status;

    Result = f_open(&File, Name, FA_READ);
    if (Result != FR_OK)
        return FatBlStatus(Result);

    Result = f_read(&File, Buffer, Length, &BytesRead);
    f_close(&File);

    if (Result != FR_OK)
        return FatBlStatus(Result);

    *Read = BytesRead;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsBlWriteFile(
    _In_ PCSTR Path,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Size);

    /* TODO: serve the update paths from FAT once the loader can write to the
     *       boot volume; see disk_write() in lib/exfat.c, which reports the
     *       medium as write protected. */
    return FatBlMounted ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NtfsBlCreateDirectory(
    _In_ PCSTR Path)
{
    UNREFERENCED_PARAMETER(Path);

    /* TODO: needs a writable FAT backend, as NtfsBlWriteFile(). */
    return FatBlMounted ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NtfsBlOpenFile(
    _In_ PCSTR Path,
    _Out_ PVOID* Handle,
    _Out_opt_ PULONGLONG Size,
    _Out_opt_ PBOOLEAN IsDirectory)
{
    CHAR Name[FATBL_PATH_CHARS];
    PFATBL_FILE File;
    FILINFO Information;
    NTSTATUS Status;
    FRESULT Result;
    BOOLEAN Directory;

    *Handle = NULL;
    if (!FatBlMounted)
        return STATUS_DEVICE_NOT_READY;

    Status = FatBlPath(Path, Name);
    if (!NT_SUCCESS(Status))
        return Status;

    Result = f_stat(Name, &Information);
    if (Result != FR_OK)
        return FatBlStatus(Result);

    Directory = (Information.fattrib & AM_DIR) ? TRUE : FALSE;

    File = FwAllocate(sizeof(*File));
    if (!File)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(File, sizeof(*File));

    if (!Directory)
    {
        Result = f_open(&File->File, Name, FA_READ);
        if (Result != FR_OK)
        {
            FwFree(File);
            return FatBlStatus(Result);
        }
    }

    File->Directory = Directory;

    if (IsDirectory)
        *IsDirectory = Directory;
    if (Size)
        *Size = Directory ? 0 : Information.fsize;

    *Handle = File;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsBlReadFileAt(
    _In_ PVOID Handle,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Read)
{
    PFATBL_FILE File = Handle;
    UINT BytesRead = 0;
    FRESULT Result;

    *Read = 0;
    if (!File)
        return STATUS_INVALID_HANDLE;

    if (File->Directory)
        return STATUS_FILE_IS_A_DIRECTORY;

    Result = f_lseek(&File->File, Offset);
    if (Result != FR_OK)
        return FatBlStatus(Result);

    Result = f_read(&File->File, Buffer, Length, &BytesRead);
    if (Result != FR_OK)
        return FatBlStatus(Result);

    *Read = BytesRead;
    return STATUS_SUCCESS;
}

VOID
NtfsBlCloseFile(
    _In_opt_ PVOID Handle)
{
    PFATBL_FILE File = Handle;

    if (!File)
        return;

    if (!File->Directory)
        f_close(&File->File);

    FwFree(File);
}

NTSTATUS
NtfsBlDeleteFile(
    _In_ PCSTR Path,
    _In_ BOOLEAN IsDirectory)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(IsDirectory);

    /* TODO: needs a writable FAT backend, as NtfsBlWriteFile(). */
    return FatBlMounted ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NtfsBlSetDirty(
    _In_ BOOLEAN Dirty)
{
    UNREFERENCED_PARAMETER(Dirty);

    /* TODO: the FAT32 clean-shutdown bit lives in FAT[1] and would have to be
     *       written (and flushed), which this read-only backend cannot do. */
    return FatBlMounted ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NtfsBlQuerySpace(
    _Out_ PULONGLONG FreeBytes,
    _Out_ PULONG ClusterSize)
{
    FATFS* FileSystem = NULL;
    DWORD FreeClusters = 0;
    ULONG BytesPerCluster;
    FRESULT Result;

    *FreeBytes = 0;
    *ClusterSize = 0;
    if (!FatBlMounted)
        return STATUS_DEVICE_NOT_READY;

    Result = f_getfree(FATBL_DRIVE, &FreeClusters, &FileSystem);
    if (Result != FR_OK || !FileSystem)
        return FatBlStatus(Result == FR_OK ? FR_INT_ERR : Result);

#if FF_MAX_SS != FF_MIN_SS
    BytesPerCluster = (ULONG)FileSystem->csize * FileSystem->ssize;
#else
    BytesPerCluster = (ULONG)FileSystem->csize * FF_MAX_SS;
#endif

    *ClusterSize = BytesPerCluster;
    *FreeBytes = (ULONGLONG)FreeClusters * BytesPerCluster;
    return STATUS_SUCCESS;
}
