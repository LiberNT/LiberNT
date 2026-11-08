/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTFS library environment and file writes on boot devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>
#include <ntfsbl.h>

#define NTFS_BL_PATH_CHARS 520
#define NTFS_BL_WRITE_CHUNK 0x100000

extern "C" {

BOOLEAN
DkRead(
    _In_ struct _DK_DEVICE* Device,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer);

BOOLEAN
DkWrite(
    _In_ struct _DK_DEVICE* Device,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID* Buffer);

BOOLEAN
DkFlush(
    _In_ struct _DK_DEVICE* Device);

PVOID
BlAllocate(
    _In_ SIZE_T Size);

VOID
BlFree(
    _In_ PVOID Memory);

BOOLEAN
FwGetTime(
    _Out_ PTIME_FIELDS Time);

}

BOOLEAN
NtfsIsNameInExpressionFallback(
    _In_ PUNICODE_STRING Expression,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ PWCHAR UpcaseTable);

static struct _DK_DEVICE* NtfsBlDevice;
static PNtfsVolume NtfsBlVolume;

extern "C" {

void*
NtfsAllocatePoolWithTag(POOL_TYPE PoolType, size_t Size, ULONG Tag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    return BlAllocate(Size);
}

void
NtfsFreePool(void* pObject)
{
    if (pObject)
        BlFree(pObject);
}

void
NtfsInitializeMutex(_Out_ PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

void
NtfsAcquireMutex(_Inout_ PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

void
NtfsReleaseMutex(_Inout_ PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

NTSTATUS
NtfsQuerySystemTime(_Out_ PULONGLONG NtfsTime)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER Time;

    if (!FwGetTime(&Fields) || !RtlTimeFieldsToTime(&Fields, &Time))
        return STATUS_UNSUCCESSFUL;

    *NtfsTime = (ULONGLONG)Time.QuadPart;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsGenerate8dot3Name(_In_ PCUNICODE_STRING Name,
                      _Inout_ PGENERATE_NAME_CONTEXT Context,
                      _Inout_ PUNICODE_STRING ShortName)
{
    return NtfsGenerate8dot3NameFallback(Name, Context, ShortName);
}

BOOLEAN
NtfsIsNameInExpression(_In_ PUNICODE_STRING Expression,
                       _In_ PUNICODE_STRING Name,
                       _In_ BOOLEAN IgnoreCase,
                       _In_opt_ PWCHAR UpcaseTable)
{
    return NtfsIsNameInExpressionFallback(Expression, Name, IgnoreCase, UpcaseTable);
}

NTSTATUS
NtfsReadVolumeContext(_In_opt_ void* Context,
                      _In_ ULONG BytesPerSector,
                      _In_ ULONGLONG Offset,
                      _In_ ULONG Length,
                      _Inout_ PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(BytesPerSector);
    return DkRead(static_cast<struct _DK_DEVICE*>(Context), Offset, Length, Buffer)
        ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR;
}

NTSTATUS
NtfsWriteVolumeContext(_In_opt_ void* Context,
                       _In_ ULONG BytesPerSector,
                       _In_ ULONGLONG Offset,
                       _In_ ULONG Length,
                       _Inout_ PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(BytesPerSector);
    return DkWrite(static_cast<struct _DK_DEVICE*>(Context), Offset, Length, Buffer)
        ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR;
}

NTSTATUS
NtfsFlushVolumeContext(_In_opt_ void* Context)
{
    return DkFlush(static_cast<struct _DK_DEVICE*>(Context)) ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR;
}

NTSTATUS
NtfsReadVolume(_In_ ULONGLONG Offset,
               _In_ ULONG Length,
               _Inout_ PUCHAR Buffer)
{
    return NtfsReadVolumeContext(NtfsBlDevice, 0, Offset, Length, Buffer);
}

NTSTATUS
NtfsWriteVolume(_In_ ULONGLONG Offset,
                _In_ ULONG Length,
                _Inout_ PUCHAR Buffer)
{
    return NtfsWriteVolumeContext(NtfsBlDevice, 0, Offset, Length, Buffer);
}

}

static
NTSTATUS
NtfsBlPath(
    _In_ PCSTR Path,
    _Out_writes_(NTFS_BL_PATH_CHARS) PWCHAR Output,
    _Out_ PULONG Chars)
{
    const UCHAR* Cursor = reinterpret_cast<const UCHAR*>(Path);
    ULONG CodePoint, Needed, Index, Count = 0;

    Output[Count++] = L'\\';
    while (*Cursor)
    {
        if (*Cursor < 0x80)
        {
            CodePoint = *Cursor++;
            Needed = 0;
        }
        else if ((*Cursor & 0xE0) == 0xC0)
        {
            CodePoint = *Cursor++ & 0x1F;
            Needed = 1;
        }
        else if ((*Cursor & 0xF0) == 0xE0)
        {
            CodePoint = *Cursor++ & 0x0F;
            Needed = 2;
        }
        else if ((*Cursor & 0xF8) == 0xF0)
        {
            CodePoint = *Cursor++ & 0x07;
            Needed = 3;
        }
        else
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        for (Index = 0; Index < Needed; Index++)
        {
            if ((Cursor[Index] & 0xC0) != 0x80)
                return STATUS_OBJECT_NAME_INVALID;
            CodePoint = (CodePoint << 6) | (Cursor[Index] & 0x3F);
        }
        Cursor += Needed;

        if (CodePoint == '/' || CodePoint == '\\')
        {
            if (Output[Count - 1] == L'\\')
                return STATUS_OBJECT_NAME_INVALID;
            CodePoint = '\\';
        }

        if (Count + 3 > NTFS_BL_PATH_CHARS)
            return STATUS_NAME_TOO_LONG;

        if (CodePoint >= 0x10000)
        {
            CodePoint -= 0x10000;
            Output[Count++] = static_cast<WCHAR>(0xD800 + (CodePoint >> 10));
            Output[Count++] = static_cast<WCHAR>(0xDC00 + (CodePoint & 0x3FF));
        }
        else
        {
            Output[Count++] = static_cast<WCHAR>(CodePoint);
        }
    }

    if (Count > 1 && Output[Count - 1] == L'\\')
        Count--;

    Output[Count] = UNICODE_NULL;
    *Chars = Count;
    return Count > 1 ? STATUS_SUCCESS : STATUS_OBJECT_NAME_INVALID;
}

static
NTSTATUS
NtfsBlCreate(
    _Inout_updates_(Chars + 1) PWCHAR Path,
    _In_ ULONG Chars,
    _In_ BOOLEAN IsDirectory)
{
    PNtfsMasterFileTable Mft = NtfsVolumeGetMft(NtfsBlVolume);
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;
    WCHAR Saved;
    ULONG End;

    for (End = 1; End <= Chars; End++)
    {
        if (End != Chars && Path[End] != L'\\')
            continue;

        Saved = Path[End];
        Path[End] = UNICODE_NULL;
        Status = NtfsMasterFileTableGetFileRecordFromQuery(Mft, Path, &File);
        if (NT_SUCCESS(Status))
        {
            if (End != Chars || IsDirectory)
            {
                Status = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY)
                    ? STATUS_SUCCESS : STATUS_NOT_A_DIRECTORY;
            }
            else
            {
                Status = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY)
                    ? STATUS_FILE_IS_A_DIRECTORY
                    : NtfsFileRecordSetFileDataSize(File, TypeData, NULL, 0);
            }
        }
        else if (Status == STATUS_NOT_FOUND)
        {
            Status = NtfsMasterFileTableCreateFile(Mft,
                                                   Path,
                                                   End,
                                                   (End != Chars || IsDirectory) ? TRUE : FALSE,
                                                   0,
                                                   &File);
        }
        NtfsFileRecordDestroy(File);
        File = NULL;
        Path[End] = Saved;

        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

extern "C" {

NTSTATUS
NtfsBlMount(
    _In_ struct _DK_DEVICE* Device)
{
    NTSTATUS Status;

    NtfsBlUnmount();
    NtfsBlDevice = Device;
    NtfsSetReadOnlyMode(FALSE);
    Status = NtfsProbePartitionAndOpenVolumeEx(512, Device, &NtfsBlVolume);
    if (!NT_SUCCESS(Status))
    {
        NtfsBlVolume = NULL;
        NtfsBlDevice = NULL;
    }
    return Status;
}

NTSTATUS
NtfsBlUnmount(VOID)
{
    BOOLEAN Flushed = TRUE;

    if (NtfsBlVolume)
    {
        NtfsVolumeDestroy(NtfsBlVolume);
        NtfsBlVolume = NULL;
        Flushed = DkFlush(NtfsBlDevice);
    }
    NtfsBlDevice = NULL;
    return Flushed ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR;
}

NTSTATUS
NtfsBlReadFile(
    _In_ PCSTR Path,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Read)
{
    WCHAR Name[NTFS_BL_PATH_CHARS];
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;
    ULONG Chars;

    *Read = 0;
    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsBlPath(Path, Name, &Chars);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(NtfsBlVolume), Name, &File);
    if (NT_SUCCESS(Status))
    {
        Chars = Length;
        Status = NtfsFileRecordCopyData(File, TypeData, NULL, static_cast<PUCHAR>(Buffer), &Chars, 0);
        if (NT_SUCCESS(Status))
            *Read = Length - Chars;
    }
    NtfsFileRecordDestroy(File);
    return Status;
}

NTSTATUS
NtfsBlWriteFile(
    _In_ PCSTR Path,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    WCHAR Name[NTFS_BL_PATH_CHARS];
    PNtfsFileRecord File = NULL;
    PAttribute Attribute;
    LARGE_INTEGER Offset;
    NTSTATUS Status;
    ULONG Chars, Length;

    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsBlPath(Path, Name, &Chars);
    if (NT_SUCCESS(Status))
        Status = NtfsBlCreate(Name, Chars, FALSE);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(NtfsBlVolume), Name, &File);

    for (Offset.QuadPart = 0; NT_SUCCESS(Status) && (ULONG)Offset.QuadPart < Size; Offset.QuadPart += Length)
    {
        Length = Size - (ULONG)Offset.QuadPart;
        if (Length > NTFS_BL_WRITE_CHUNK)
            Length = NTFS_BL_WRITE_CHUNK;
        Chars = Length;
        Status = NtfsFileRecordWriteFileData(File,
                                             TypeData,
                                             NULL,
                                             const_cast<PUCHAR>(static_cast<const UCHAR*>(Data)) + Offset.QuadPart,
                                             &Length,
                                             &Offset);
        if (NT_SUCCESS(Status) && Length != Chars)
            Status = STATUS_END_OF_FILE;
    }

    if (NT_SUCCESS(Status))
    {
        Attribute = NtfsFileRecordGetAttribute(File, TypeData, NULL);
        if (Attribute && Attribute->IsNonResident)
            Status = NtfsFileRecordSetFileAllocationSize(File, TypeData, NULL, Size);
    }

    NtfsFileRecordDestroy(File);
    return Status;
}

NTSTATUS
NtfsBlCreateDirectory(
    _In_ PCSTR Path)
{
    WCHAR Name[NTFS_BL_PATH_CHARS];
    NTSTATUS Status;
    ULONG Chars;

    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsBlPath(Path, Name, &Chars);
    return NT_SUCCESS(Status) ? NtfsBlCreate(Name, Chars, TRUE) : Status;
}

NTSTATUS
NtfsBlOpenFile(
    _In_ PCSTR Path,
    _Out_ PVOID* Handle,
    _Out_opt_ PULONGLONG Size,
    _Out_opt_ PBOOLEAN IsDirectory)
{
    WCHAR Name[NTFS_BL_PATH_CHARS];
    PNtfsFileRecord File = NULL;
    PAttribute Attribute;
    NTSTATUS Status;
    ULONG Chars;

    *Handle = NULL;
    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsBlPath(Path, Name, &Chars);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(NtfsBlVolume), Name, &File);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IsDirectory)
        *IsDirectory = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) ? TRUE : FALSE;
    if (Size)
    {
        Attribute = NtfsFileRecordGetAttribute(File, TypeData, NULL);
        *Size = Attribute ? GetAttributeDataSize(Attribute) : 0;
    }
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
    ULONG Remaining = Length;
    NTSTATUS Status;

    *Read = 0;
    Status = NtfsFileRecordCopyData(static_cast<PNtfsFileRecord>(Handle),
                                    TypeData,
                                    NULL,
                                    static_cast<PUCHAR>(Buffer),
                                    &Remaining,
                                    Offset);
    if (NT_SUCCESS(Status))
        *Read = Length - Remaining;
    return Status;
}

VOID
NtfsBlCloseFile(
    _In_opt_ PVOID Handle)
{
    NtfsFileRecordDestroy(static_cast<PNtfsFileRecord>(Handle));
}

NTSTATUS
NtfsBlDeleteFile(
    _In_ PCSTR Path,
    _In_ BOOLEAN IsDirectory)
{
    WCHAR Name[NTFS_BL_PATH_CHARS];
    NTSTATUS Status;
    ULONG Chars;

    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsBlPath(Path, Name, &Chars);
    return NT_SUCCESS(Status)
        ? NtfsMasterFileTableDeleteFile(NtfsVolumeGetMft(NtfsBlVolume), Name, Chars, IsDirectory)
        : Status;
}

NTSTATUS
NtfsBlQuerySpace(
    _Out_ PULONGLONG FreeBytes,
    _Out_ PULONG ClusterSize)
{
    NtfsVolumeInformation Information;
    NTSTATUS Status;

    *FreeBytes = 0;
    *ClusterSize = 0;
    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsVolumeQueryInformation(NtfsBlVolume, &Information);
    if (NT_SUCCESS(Status))
    {
        *FreeBytes = Information.FreeClusters * Information.BytesPerCluster;
        *ClusterSize = Information.BytesPerCluster;
    }
    return Status;
}

NTSTATUS
NtfsBlSetDirty(
    _In_ BOOLEAN Dirty)
{
    NTSTATUS Status;

    if (!NtfsBlVolume)
        return STATUS_DEVICE_NOT_READY;

    Status = NtfsVolumeSetFlags(NtfsBlVolume, Dirty ? VOL_DIRTY : 0, Dirty ? 0 : VOL_DIRTY);
    if (NT_SUCCESS(Status) && !DkFlush(NtfsBlDevice))
        Status = STATUS_UNEXPECTED_IO_ERROR;
    return Status;
}

}
