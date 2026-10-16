/*
 * PROJECT:     LiberNT volume resize library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shrink and extend basic NTFS volumes through the documented FSCTLs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <ntddstor.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ntstatus.h>
#include <fslib/volresize.h>

#define NDEBUG
#include <debug.h>

#define VR_RESERVE_BYTES        (50ULL * 1024 * 1024)
#define VR_RAW_MINIMUM_BYTES    (1024ULL * 1024)
#define VR_MOVE_CHUNK           65536
#define VR_EXTENT_BATCH         128
#define VR_DIRECTORY_BUFFER     16384
#define VR_STREAM_BUFFER        4096
#define VR_RECORD_BUFFER        8192
#define VR_MAX_DEPTH            256
#define VR_COMPRESSION_UNIT     16
#define VR_MOVE_RETRIES         4

typedef struct _VR_EXTENT
{
    ULONGLONG Vcn;
    ULONGLONG Length;
    LONGLONG Lcn;
} VR_EXTENT, *PVR_EXTENT;

typedef struct _VR_CONTEXT
{
    HANDLE Volume;
    HANDLE Heap;
    ULONG BytesPerCluster;
    ULONGLONG TotalClusters;
    ULONGLONG Boundary;
    PUCHAR Bitmap;
    ULONGLONG Cursor;
    ULONGLONG ClustersToMove;
    ULONGLONG ClustersMoved;
    PVOLRESIZE_PROGRESS Progress;
    PVOID ProgressContext;
    ULONG LastPercent;
    NTSTATUS Failure;
    PVR_EXTENT Extents;
    ULONG ExtentCapacity;
} VR_CONTEXT, *PVR_CONTEXT;

static
PVOID
VrAlloc(
    _In_ SIZE_T Size)
{
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

static
VOID
VrFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static
NTSTATUS
VrFsctl(
    _In_ HANDLE Handle,
    _In_ ULONG Code,
    _In_opt_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_opt_ PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK Iosb;

    return NtFsControlFile(Handle, NULL, NULL, NULL, &Iosb, Code,
                           Input, InputLength, Output, OutputLength);
}

static
NTSTATUS
VrIoctl(
    _In_ HANDLE Handle,
    _In_ ULONG Code,
    _In_opt_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_opt_ PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK Iosb;

    return NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &Iosb, Code,
                                 Input, InputLength, Output, OutputLength);
}

static
NTSTATUS
VrOpen(
    _In_opt_ HANDLE Root,
    _In_ PCUNICODE_STRING Name,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Options,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;

    InitializeObjectAttributes(&Attributes,
                               (PUNICODE_STRING)Name,
                               OBJ_CASE_INSENSITIVE,
                               Root,
                               NULL);
    return NtOpenFile(Handle,
                      Access | SYNCHRONIZE,
                      &Attributes,
                      &Iosb,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      Options | FILE_SYNCHRONOUS_IO_NONALERT);
}

static
NTSTATUS
VrOpenRelative(
    _In_ HANDLE Root,
    _In_reads_bytes_(NameBytes) PCWSTR Name,
    _In_ USHORT NameBytes,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Options,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING String;

    String.Buffer = (PWSTR)Name;
    String.Length = NameBytes;
    String.MaximumLength = NameBytes;
    return VrOpen(Root, &String, Access,
                  Options | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT,
                  Handle);
}

static
NTSTATUS
VrOpenVolume(
    _In_ PCUNICODE_STRING VolumePath,
    _Out_ PHANDLE Handle)
{
    return VrOpen(NULL, VolumePath, FILE_READ_DATA | FILE_WRITE_DATA, 0, Handle);
}

static
NTSTATUS
VrOpenRoot(
    _In_ PCUNICODE_STRING VolumePath,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Root;
    NTSTATUS Status;

    Root.MaximumLength = VolumePath->Length + 2 * sizeof(WCHAR);
    Root.Buffer = VrAlloc(Root.MaximumLength);
    if (!Root.Buffer)
        return STATUS_NO_MEMORY;
    RtlCopyMemory(Root.Buffer, VolumePath->Buffer, VolumePath->Length);
    Root.Length = VolumePath->Length;
    Root.Buffer[Root.Length / sizeof(WCHAR)] = L'\\';
    Root.Length += sizeof(WCHAR);

    Status = VrOpen(NULL, &Root, FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                    FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT, Handle);
    VrFree(Root.Buffer);
    return Status;
}

static
NTSTATUS
VrGetExtents(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE File,
    _Out_ PULONG Count)
{
    UCHAR Buffer[FIELD_OFFSET(RETRIEVAL_POINTERS_BUFFER, Extents) +
                 VR_EXTENT_BATCH * 2 * sizeof(LARGE_INTEGER)];
    PRETRIEVAL_POINTERS_BUFFER Pointers = (PRETRIEVAL_POINTERS_BUFFER)Buffer;
    STARTING_VCN_INPUT_BUFFER Input;
    ULONGLONG Vcn;
    NTSTATUS Status;
    ULONG Index;

    *Count = 0;
    Input.StartingVcn.QuadPart = 0;
    for (;;)
    {
        Status = VrFsctl(File, FSCTL_GET_RETRIEVAL_POINTERS,
                         &Input, sizeof(Input), Buffer, sizeof(Buffer));
        if (Status == STATUS_END_OF_FILE)
            return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
            return Status;

        Vcn = (ULONGLONG)Pointers->StartingVcn.QuadPart;
        for (Index = 0; Index < Pointers->ExtentCount; Index++)
        {
            ULONGLONG Next = (ULONGLONG)Pointers->Extents[Index].NextVcn.QuadPart;

            if (*Count == Context->ExtentCapacity)
            {
                ULONG Capacity = Context->ExtentCapacity ? Context->ExtentCapacity * 2 : 256;
                PVR_EXTENT Grown = VrAlloc(Capacity * sizeof(VR_EXTENT));

                if (!Grown)
                    return STATUS_NO_MEMORY;
                if (Context->Extents)
                {
                    RtlCopyMemory(Grown, Context->Extents, *Count * sizeof(VR_EXTENT));
                    VrFree(Context->Extents);
                }
                Context->Extents = Grown;
                Context->ExtentCapacity = Capacity;
            }
            if (Next <= Vcn)
                return STATUS_FILE_CORRUPT_ERROR;
            Context->Extents[*Count].Vcn = Vcn;
            Context->Extents[*Count].Length = Next - Vcn;
            Context->Extents[*Count].Lcn = Pointers->Extents[Index].Lcn.QuadPart;
            (*Count)++;
            Vcn = Next;
        }
        if (Status != STATUS_BUFFER_OVERFLOW || Pointers->ExtentCount == 0)
            return STATUS_SUCCESS;
        Input.StartingVcn.QuadPart = (LONGLONG)Vcn;
    }
}

static
ULONGLONG
VrExtentsEnd(
    _In_ PVR_CONTEXT Context,
    _In_ ULONG Count)
{
    ULONGLONG End = 0;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PVR_EXTENT Extent = &Context->Extents[Index];

        if (Extent->Lcn >= 0 && (ULONGLONG)Extent->Lcn + Extent->Length > End)
            End = (ULONGLONG)Extent->Lcn + Extent->Length;
    }
    return End;
}

static
BOOLEAN
VrIsFree(
    _In_ PVR_CONTEXT Context,
    _In_ ULONGLONG Lcn)
{
    return !(Context->Bitmap[Lcn >> 3] & (1 << (Lcn & 7)));
}

static
VOID
VrMarkUsed(
    _In_ PVR_CONTEXT Context,
    _In_ ULONGLONG Lcn,
    _In_ ULONGLONG Count)
{
    while (Count--)
    {
        Context->Bitmap[Lcn >> 3] |= (UCHAR)(1 << (Lcn & 7));
        Lcn++;
    }
}

static
NTSTATUS
VrLoadBitmap(
    _In_ PVR_CONTEXT Context)
{
    STARTING_LCN_INPUT_BUFFER Input;
    PVOLUME_BITMAP_BUFFER Buffer;
    ULONGLONG Bytes = (Context->TotalClusters + 7) / 8;
    ULONG Length;
    NTSTATUS Status;

    if (Bytes > MAXULONG - FIELD_OFFSET(VOLUME_BITMAP_BUFFER, Buffer))
        return STATUS_NOT_SUPPORTED;
    Length = (ULONG)Bytes + FIELD_OFFSET(VOLUME_BITMAP_BUFFER, Buffer);
    Buffer = VrAlloc(Length);
    if (!Buffer)
        return STATUS_NO_MEMORY;

    Input.StartingLcn.QuadPart = 0;
    Status = VrFsctl(Context->Volume, FSCTL_GET_VOLUME_BITMAP,
                     &Input, sizeof(Input), Buffer, Length);
    if (NT_SUCCESS(Status))
    {
        if (!Context->Bitmap)
            Context->Bitmap = VrAlloc((SIZE_T)Bytes);
        if (!Context->Bitmap)
            Status = STATUS_NO_MEMORY;
        else
            RtlCopyMemory(Context->Bitmap, Buffer->Buffer, (SIZE_T)Bytes);
    }
    VrFree(Buffer);
    Context->Cursor = 0;
    return Status;
}

static
BOOLEAN
VrFindFree(
    _In_ PVR_CONTEXT Context,
    _In_ ULONGLONG Wanted,
    _In_ BOOLEAN Contiguous,
    _Out_ PULONGLONG Lcn,
    _Out_ PULONGLONG Length)
{
    ULONGLONG Start;
    ULONGLONG Pass;

    for (Pass = 0; Pass < 2; Pass++)
    {
        for (Start = Pass ? 0 : Context->Cursor; Start < Context->Boundary; Start++)
        {
            ULONGLONG Run = 0;

            if (!VrIsFree(Context, Start))
                continue;
            while (Start + Run < Context->Boundary && Run < Wanted && VrIsFree(Context, Start + Run))
                Run++;
            if (Contiguous && Run < Wanted)
            {
                Start += Run;
                continue;
            }
            *Lcn = Start;
            *Length = Run;
            Context->Cursor = Start;
            return TRUE;
        }
        if (Context->Cursor == 0)
            break;
    }
    return FALSE;
}

static
VOID
VrReport(
    _In_ PVR_CONTEXT Context)
{
    ULONG Percent;

    if (!Context->Progress || Context->ClustersToMove == 0)
        return;
    Percent = (ULONG)min(100, Context->ClustersMoved * 100 / Context->ClustersToMove);
    if (Percent != Context->LastPercent)
    {
        Context->LastPercent = Percent;
        Context->Progress(Context->ProgressContext, Percent);
    }
}

static
NTSTATUS
VrMove(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE File,
    _In_ ULONGLONG Vcn,
    _In_ ULONG Count,
    _In_ ULONGLONG Lcn)
{
    MOVE_FILE_DATA Move;

    Move.FileHandle = File;
    Move.StartingVcn.QuadPart = (LONGLONG)Vcn;
    Move.StartingLcn.QuadPart = (LONGLONG)Lcn;
    Move.ClusterCount = Count;
    return VrFsctl(Context->Volume, FSCTL_MOVE_FILE, &Move, sizeof(Move), NULL, 0);
}

static
NTSTATUS
VrRelocateRange(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE File,
    _In_ ULONGLONG Vcn,
    _In_ ULONGLONG Count)
{
    ULONG Retries = 0;
    NTSTATUS Status;

    while (Count != 0)
    {
        ULONGLONG Lcn;
        ULONGLONG Length;
        ULONG Chunk;

        if (!VrFindFree(Context, min(Count, VR_MOVE_CHUNK), FALSE, &Lcn, &Length))
            return STATUS_DISK_FULL;
        Chunk = (ULONG)Length;
        Status = VrMove(Context, File, Vcn, Chunk, Lcn);
        if (Status == STATUS_ALREADY_COMMITTED && Retries++ < VR_MOVE_RETRIES)
        {
            Status = VrLoadBitmap(Context);
            if (!NT_SUCCESS(Status))
                return Status;
            continue;
        }
        if (!NT_SUCCESS(Status))
            return Status;
        VrMarkUsed(Context, Lcn, Chunk);
        Vcn += Chunk;
        Count -= Chunk;
        Context->ClustersMoved += Chunk;
        Retries = 0;
        VrReport(Context);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
VrRelocateUnits(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE File,
    _In_ ULONG Count)
{
    ULONGLONG Unit = (ULONGLONG)-1;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < Count; Index++)
    {
        PVR_EXTENT Extent = &Context->Extents[Index];
        ULONGLONG First;
        ULONGLONG Last;

        if (Extent->Lcn < 0 || (ULONGLONG)Extent->Lcn + Extent->Length <= Context->Boundary)
            continue;

        First = Extent->Vcn / VR_COMPRESSION_UNIT;
        Last = (Extent->Vcn + Extent->Length - 1) / VR_COMPRESSION_UNIT;
        for (; First <= Last; First++)
        {
            ULONGLONG UnitStart = First * VR_COMPRESSION_UNIT;
            ULONGLONG Allocated = 0;
            ULONGLONG Lcn;
            ULONGLONG Length;
            ULONG Scan;

            if (First == Unit)
                continue;
            Unit = First;
            for (Scan = 0; Scan < Count; Scan++)
            {
                PVR_EXTENT Piece = &Context->Extents[Scan];
                ULONGLONG Low = max(Piece->Vcn, UnitStart);
                ULONGLONG High = min(Piece->Vcn + Piece->Length, UnitStart + VR_COMPRESSION_UNIT);

                if (Piece->Lcn >= 0 && Low < High)
                    Allocated += High - Low;
            }
            if (Allocated == 0)
                continue;
            if (!VrFindFree(Context, Allocated, TRUE, &Lcn, &Length))
                return STATUS_DISK_FULL;
            Status = VrMove(Context, File, UnitStart, VR_COMPRESSION_UNIT, Lcn);
            if (Status == STATUS_ALREADY_COMMITTED)
            {
                Status = VrLoadBitmap(Context);
                if (!NT_SUCCESS(Status))
                    return Status;
                if (!VrFindFree(Context, Allocated, TRUE, &Lcn, &Length))
                    return STATUS_DISK_FULL;
                Status = VrMove(Context, File, UnitStart, VR_COMPRESSION_UNIT, Lcn);
            }
            if (!NT_SUCCESS(Status))
                return Status;
            VrMarkUsed(Context, Lcn, Allocated);
            Context->ClustersMoved += Allocated;
            VrReport(Context);
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
VrRelocateHandle(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE File,
    _In_ BOOLEAN Units)
{
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    if (!NT_SUCCESS(VrGetExtents(Context, File, &Count)) ||
        VrExtentsEnd(Context, Count) <= Context->Boundary)
        return STATUS_SUCCESS;
    if (Units)
        return VrRelocateUnits(Context, File, Count);

    for (Index = 0; Index < Count; Index++)
    {
        VR_EXTENT Extent = Context->Extents[Index];
        ULONGLONG Skip;

        if (Extent.Lcn < 0 || (ULONGLONG)Extent.Lcn + Extent.Length <= Context->Boundary)
            continue;
        Skip = (ULONGLONG)Extent.Lcn < Context->Boundary
            ? Context->Boundary - (ULONGLONG)Extent.Lcn
            : 0;
        Status = VrRelocateRange(Context, File, Extent.Vcn + Skip, Extent.Length - Skip);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
VrRecordFailure(
    _In_ PVR_CONTEXT Context,
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Context->Failure))
        Context->Failure = Status;
}

static
VOID
VrRelocateStreams(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE Parent,
    _In_reads_bytes_(NameBytes) PCWSTR Name,
    _In_ USHORT NameBytes,
    _In_ HANDLE File,
    _In_ BOOLEAN Units)
{
    PFILE_STREAM_INFORMATION Stream;
    IO_STATUS_BLOCK Iosb;
    PUCHAR Buffer;
    ULONG Size = VR_STREAM_BUFFER;
    NTSTATUS Status;

    for (;;)
    {
        Buffer = VrAlloc(Size);
        if (!Buffer)
            return;
        Status = NtQueryInformationFile(File, &Iosb, Buffer, Size, FileStreamInformation);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        VrFree(Buffer);
        if (Size >= 1024 * 1024)
            return;
        Size *= 4;
    }
    if (!NT_SUCCESS(Status) || Iosb.Information == 0)
    {
        VrFree(Buffer);
        return;
    }

    Stream = (PFILE_STREAM_INFORMATION)Buffer;
    for (;;)
    {
        static const WCHAR Default[] = L"::$DATA";
        USHORT StreamBytes = (USHORT)Stream->StreamNameLength;

        if (Stream->StreamAllocationSize.QuadPart != 0 &&
            !(StreamBytes == sizeof(Default) - sizeof(WCHAR) &&
              RtlCompareMemory(Stream->StreamName, Default, StreamBytes) == StreamBytes) &&
            (ULONG)NameBytes + StreamBytes <= MAXUSHORT)
        {
            PWSTR Full = VrAlloc(NameBytes + StreamBytes);
            HANDLE StreamHandle;

            if (Full)
            {
                RtlCopyMemory(Full, Name, NameBytes);
                RtlCopyMemory((PUCHAR)Full + NameBytes, Stream->StreamName, StreamBytes);
                Status = VrOpenRelative(Parent, Full, NameBytes + StreamBytes,
                                        FILE_READ_ATTRIBUTES, FILE_NON_DIRECTORY_FILE,
                                        &StreamHandle);
                if (NT_SUCCESS(Status))
                {
                    Status = VrRelocateHandle(Context, StreamHandle, Units);
                    NtClose(StreamHandle);
                }
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("Moving stream %.*S failed (Status 0x%08lx)\n",
                            (NameBytes + StreamBytes) / sizeof(WCHAR), Full, Status);
                    VrRecordFailure(Context, Status);
                }
                VrFree(Full);
            }
        }
        if (Stream->NextEntryOffset == 0)
            break;
        Stream = (PFILE_STREAM_INFORMATION)((PUCHAR)Stream + Stream->NextEntryOffset);
    }
    VrFree(Buffer);
}

static
VOID
VrWalkDirectory(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE Directory,
    _In_ ULONG Depth)
{
    PFILE_DIRECTORY_INFORMATION Entry;
    IO_STATUS_BLOCK Iosb;
    BOOLEAN Restart = TRUE;
    PUCHAR Buffer;
    NTSTATUS Status;

    if (Depth > VR_MAX_DEPTH)
        return;
    Buffer = VrAlloc(VR_DIRECTORY_BUFFER);
    if (!Buffer)
        return;

    for (;;)
    {
        Status = NtQueryDirectoryFile(Directory, NULL, NULL, NULL, &Iosb, Buffer,
                                      VR_DIRECTORY_BUFFER, FileDirectoryInformation,
                                      FALSE, NULL, Restart);
        Restart = FALSE;
        if (!NT_SUCCESS(Status))
            break;

        Entry = (PFILE_DIRECTORY_INFORMATION)Buffer;
        for (;;)
        {
            BOOLEAN IsDirectory = !!(Entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            BOOLEAN IsReparse = !!(Entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
            BOOLEAN Units = !!(Entry->FileAttributes &
                               (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE));
            USHORT NameBytes = (USHORT)Entry->FileNameLength;
            HANDLE Child;

            if (!((NameBytes == sizeof(WCHAR) && Entry->FileName[0] == L'.') ||
                  (NameBytes == 2 * sizeof(WCHAR) && Entry->FileName[0] == L'.' &&
                   Entry->FileName[1] == L'.')))
            {
                Status = VrOpenRelative(Directory, Entry->FileName, NameBytes,
                                        FILE_READ_ATTRIBUTES |
                                            (IsDirectory && !IsReparse ? FILE_LIST_DIRECTORY : 0),
                                        0, &Child);
                if (NT_SUCCESS(Status))
                {
                    Status = VrRelocateHandle(Context, Child, IsDirectory ? FALSE : Units);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("Moving %.*S failed (Status 0x%08lx)\n",
                                NameBytes / sizeof(WCHAR), Entry->FileName, Status);
                        VrRecordFailure(Context, Status);
                    }
                    VrRelocateStreams(Context, Directory, Entry->FileName, NameBytes,
                                      Child, Units);
                    if (IsDirectory && !IsReparse)
                        VrWalkDirectory(Context, Child, Depth + 1);
                    NtClose(Child);
                }
            }
            if (Entry->NextEntryOffset == 0)
                break;
            Entry = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset);
        }
    }
    VrFree(Buffer);
}

static
VOID
VrRelocateNamed(
    _In_ PVR_CONTEXT Context,
    _In_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_ BOOLEAN Walk)
{
    HANDLE Handle;
    NTSTATUS Status;

    Status = VrOpenRelative(Root, Name, (USHORT)(wcslen(Name) * sizeof(WCHAR)),
                            FILE_READ_ATTRIBUTES | (Walk ? FILE_LIST_DIRECTORY : 0),
                            0, &Handle);
    if (!NT_SUCCESS(Status))
        return;
    Status = VrRelocateHandle(Context, Handle, FALSE);
    if (!NT_SUCCESS(Status))
        VrRecordFailure(Context, Status);
    if (Walk)
        VrWalkDirectory(Context, Handle, 1);
    NtClose(Handle);
}

static
VOID
VrDecodeRecordEnd(
    _In_reads_bytes_(Length) PUCHAR Record,
    _In_ ULONG Length,
    _In_ ULONG BytesPerSector,
    _Inout_ PULONGLONG End)
{
    USHORT UsaOffset;
    USHORT UsaCount;
    USHORT Offset;
    USHORT Index;

    if (Length < 0x30 || RtlCompareMemory(Record, "FILE", 4) != 4)
        return;
    UsaOffset = *(PUSHORT)(Record + 4);
    UsaCount = *(PUSHORT)(Record + 6);
    if (BytesPerSector >= 512 && UsaCount > 1 &&
        UsaOffset + UsaCount * sizeof(USHORT) <= Length &&
        (ULONG)(UsaCount - 1) * BytesPerSector <= Length)
    {
        USHORT Usn = *(PUSHORT)(Record + UsaOffset);

        for (Index = 1; Index < UsaCount; Index++)
        {
            PUSHORT Tail = (PUSHORT)(Record + Index * BytesPerSector - sizeof(USHORT));

            if (*Tail == Usn)
                *Tail = *(PUSHORT)(Record + UsaOffset + Index * sizeof(USHORT));
        }
    }

    Offset = *(PUSHORT)(Record + 0x14);
    while ((ULONG)Offset + 0x10 <= Length)
    {
        ULONG Type = *(PULONG)(Record + Offset);
        ULONG AttributeLength = *(PULONG)(Record + Offset + 4);

        if (Type == 0xFFFFFFFF || AttributeLength < 0x18 || Offset + AttributeLength > Length)
            break;
        if (Record[Offset + 8] && AttributeLength >= 0x40)
        {
            PUCHAR Pairs = Record + Offset + *(PUSHORT)(Record + Offset + 0x20);
            PUCHAR Limit = Record + Offset + AttributeLength;
            LONGLONG Lcn = 0;

            while (Pairs < Limit && *Pairs)
            {
                ULONG LengthBytes = *Pairs & 0x0F;
                ULONG OffsetBytes = *Pairs >> 4;
                ULONGLONG RunLength = 0;
                LONGLONG Delta = 0;
                ULONG Byte;

                Pairs++;
                if (LengthBytes == 0 || LengthBytes > 8 || OffsetBytes > 8 ||
                    Pairs + LengthBytes + OffsetBytes > Limit)
                {
                    break;
                }
                for (Byte = 0; Byte < LengthBytes; Byte++)
                    RunLength |= (ULONGLONG)Pairs[Byte] << (Byte * 8);
                Pairs += LengthBytes;
                if (OffsetBytes)
                {
                    for (Byte = 0; Byte < OffsetBytes; Byte++)
                        Delta |= (LONGLONG)((ULONGLONG)Pairs[Byte] << (Byte * 8));
                    if (OffsetBytes < 8 && (Pairs[OffsetBytes - 1] & 0x80))
                        Delta |= -((LONGLONG)1 << (OffsetBytes * 8));
                    Pairs += OffsetBytes;
                    Lcn += Delta;
                    if (Lcn >= 0 && (ULONGLONG)Lcn + RunLength > *End)
                        *End = (ULONGLONG)Lcn + RunLength;
                }
            }
        }
        Offset += (USHORT)AttributeLength;
    }
}

static
BOOLEAN
VrIsRawVolume(
    _In_ HANDLE Volume)
{
    UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 32 * sizeof(WCHAR)];
    PFILE_FS_ATTRIBUTE_INFORMATION Attribute = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Status = NtQueryVolumeInformationFile(Volume, &Iosb, Buffer, sizeof(Buffer),
                                          FileFsAttributeInformation);
    if (!NT_SUCCESS(Status))
        return FALSE;
    return Attribute->FileSystemNameLength == 3 * sizeof(WCHAR) &&
           RtlCompareMemory(Attribute->FileSystemName, L"RAW", 3 * sizeof(WCHAR)) ==
               3 * sizeof(WCHAR);
}

static
NTSTATUS
VrQueryVolume(
    _In_ PCUNICODE_STRING VolumePath,
    _In_ HANDLE Volume,
    _In_ PVR_CONTEXT Context,
    _Out_ PVOLRESIZE_INFO Info)
{
    static const ULONG Records[] = { 1, 2, 6, 7, 8 };
    static const PCWSTR Pinned[] = { L"pagefile.sys", L"hiberfil.sys", L"swapfile.sys" };
    NTFS_VOLUME_DATA_BUFFER Ntfs;
    GET_LENGTH_INFORMATION Length;
    PARTITION_INFORMATION_EX Partition;
    STORAGE_DEVICE_NUMBER Number;
    NTFS_FILE_RECORD_INPUT_BUFFER RecordInput;
    PNTFS_FILE_RECORD_OUTPUT_BUFFER RecordOutput;
    ULONGLONG End = 0;
    ULONGLONG Limit;
    ULONGLONG FreeBytes;
    HANDLE Root;
    HANDLE File;
    NTSTATUS Status;
    ULONG Count;
    ULONG Index;

    RtlZeroMemory(Info, sizeof(*Info));

    Status = VrIoctl(Volume, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &Length, sizeof(Length));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VrIoctl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Partition, sizeof(Partition));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VrIoctl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = VrFsctl(Volume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &Ntfs, sizeof(Ntfs));
    if (!NT_SUCCESS(Status))
    {
        DISK_GEOMETRY Geometry;

        if (!VrIsRawVolume(Volume))
            return STATUS_NOT_SUPPORTED;
        Status = VrIoctl(Volume, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &Geometry, sizeof(Geometry));
        Info->BytesPerSector = NT_SUCCESS(Status) && Geometry.BytesPerSector ? Geometry.BytesPerSector : 512;
        Info->PartitionBytes = (ULONGLONG)Length.Length.QuadPart;
        Info->VolumeBytes = 0;
        Info->DiskNumber = Number.DeviceNumber;
        Info->PartitionNumber = Partition.PartitionNumber;
        Info->Raw = TRUE;
        Info->MaxReclaimableBytes = Info->PartitionBytes > VR_RAW_MINIMUM_BYTES
            ? Info->PartitionBytes - VR_RAW_MINIMUM_BYTES
            : 0;
        return STATUS_SUCCESS;
    }

    Info->PartitionBytes = (ULONGLONG)Length.Length.QuadPart;
    Info->TotalClusters = (ULONGLONG)Ntfs.TotalClusters.QuadPart;
    Info->FreeClusters = (ULONGLONG)Ntfs.FreeClusters.QuadPart;
    Info->BytesPerCluster = Ntfs.BytesPerCluster;
    Info->BytesPerSector = Ntfs.BytesPerSector;
    Info->DiskNumber = Number.DeviceNumber;
    Info->PartitionNumber = Partition.PartitionNumber;
    if (Info->BytesPerCluster == 0 || Info->BytesPerSector == 0)
        return STATUS_FILE_CORRUPT_ERROR;
    Info->VolumeBytes = ((ULONGLONG)Ntfs.NumberSectors.QuadPart + 1) * Info->BytesPerSector;
    if (Info->VolumeBytes > Info->PartitionBytes)
        Info->VolumeBytes = Info->PartitionBytes;

    Context->Volume = Volume;
    Context->BytesPerCluster = Info->BytesPerCluster;
    Context->TotalClusters = Info->TotalClusters;

    RecordOutput = VrAlloc(VR_RECORD_BUFFER);
    if (!RecordOutput)
        return STATUS_NO_MEMORY;
    for (Index = 0; Index < RTL_NUMBER_OF(Records); Index++)
    {
        RecordInput.FileReferenceNumber.QuadPart = Records[Index];
        Status = VrFsctl(Volume, FSCTL_GET_NTFS_FILE_RECORD,
                         &RecordInput, sizeof(RecordInput), RecordOutput, VR_RECORD_BUFFER);
        if (NT_SUCCESS(Status) &&
            (RecordOutput->FileReferenceNumber.QuadPart & 0x0000FFFFFFFFFFFFLL) == Records[Index])
        {
            VrDecodeRecordEnd(RecordOutput->FileRecordBuffer,
                              min(RecordOutput->FileRecordLength,
                                  VR_RECORD_BUFFER - FIELD_OFFSET(NTFS_FILE_RECORD_OUTPUT_BUFFER,
                                                                  FileRecordBuffer)),
                              Info->BytesPerSector,
                              &End);
        }
    }
    VrFree(RecordOutput);

    Status = VrOpenRoot(VolumePath, &Root);
    if (NT_SUCCESS(Status))
    {
        static const WCHAR MftName[] = L"$MFT";

        if (NT_SUCCESS(VrOpenRelative(Root, MftName, sizeof(MftName) - sizeof(WCHAR),
                                      FILE_READ_ATTRIBUTES, 0, &File)))
        {
            if (NT_SUCCESS(VrGetExtents(Context, File, &Count)))
                End = max(End, VrExtentsEnd(Context, Count));
            NtClose(File);
        }
        for (Index = 0; Index < RTL_NUMBER_OF(Pinned); Index++)
        {
            if (NT_SUCCESS(VrOpenRelative(Root, Pinned[Index],
                                          (USHORT)(wcslen(Pinned[Index]) * sizeof(WCHAR)),
                                          FILE_READ_ATTRIBUTES, FILE_NON_DIRECTORY_FILE, &File)))
            {
                if (NT_SUCCESS(VrGetExtents(Context, File, &Count)))
                    End = max(End, VrExtentsEnd(Context, Count));
                NtClose(File);
            }
        }
        NtClose(Root);
    }

    Info->UnmovableEndCluster = End;
    Limit = End * Info->BytesPerCluster;
    Limit = Info->VolumeBytes > Limit ? Info->VolumeBytes - Limit : 0;
    FreeBytes = (Info->FreeClusters > (ULONGLONG)Ntfs.TotalReserved.QuadPart
                 ? Info->FreeClusters - (ULONGLONG)Ntfs.TotalReserved.QuadPart
                 : 0) * Info->BytesPerCluster;
    if (FreeBytes < Limit)
        Limit = FreeBytes;
    Info->MaxReclaimableBytes = Limit > VR_RESERVE_BYTES ? Limit - VR_RESERVE_BYTES : 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VrGrowPartition(
    _In_ PVOLRESIZE_INFO Info,
    _In_ LONGLONG Bytes)
{
    WCHAR Path[64];
    UNICODE_STRING Name;
    DISK_GROW_PARTITION Grow;
    HANDLE Disk;
    NTSTATUS Status;

    _snwprintf(Path, RTL_NUMBER_OF(Path), L"\\Device\\Harddisk%lu\\Partition0", Info->DiskNumber);
    Path[RTL_NUMBER_OF(Path) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&Name, Path);
    Status = VrOpen(NULL, &Name, FILE_READ_DATA | FILE_WRITE_DATA, 0, &Disk);
    if (!NT_SUCCESS(Status))
        return Status;

    Grow.PartitionNumber = Info->PartitionNumber;
    Grow.BytesToGrow.QuadPart = Bytes;
    Status = VrIoctl(Disk, IOCTL_DISK_GROW_PARTITION, &Grow, sizeof(Grow), NULL, 0);
    NtClose(Disk);
    return Status;
}

static
VOID
VrCleanup(
    _In_ PVR_CONTEXT Context)
{
    VrFree(Context->Bitmap);
    VrFree(Context->Extents);
}

NTSTATUS
NTAPI
VolResizeQuery(
    _In_ PCUNICODE_STRING VolumePath,
    _Out_ PVOLRESIZE_INFO Info)
{
    VR_CONTEXT Context;
    HANDLE Volume;
    NTSTATUS Status;

    RtlZeroMemory(&Context, sizeof(Context));
    Status = VrOpenVolume(VolumePath, &Volume);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VrQueryVolume(VolumePath, Volume, &Context, Info);
    NtClose(Volume);
    VrCleanup(&Context);
    return Status;
}

NTSTATUS
NTAPI
VolResizeShrink(
    _In_ PCUNICODE_STRING VolumePath,
    _In_ ULONGLONG ReclaimBytes,
    _In_opt_ PVOLRESIZE_PROGRESS Progress,
    _In_opt_ PVOID ProgressContext)
{
    static const PCWSTR Metadata[] =
    {
        L"$Secure:$SDS:$DATA", L"$Secure:$SDH:$INDEX_ALLOCATION", L"$Secure:$SII:$INDEX_ALLOCATION",
        L"$UpCase", L"$AttrDef", L"$MFT::$BITMAP",
        L"$Extend\\$ObjId:$O:$INDEX_ALLOCATION", L"$Extend\\$Quota:$O:$INDEX_ALLOCATION",
        L"$Extend\\$Quota:$Q:$INDEX_ALLOCATION", L"$Extend\\$Reparse:$R:$INDEX_ALLOCATION",
    };
    SHRINK_VOLUME_INFORMATION Shrink;
    VOLRESIZE_INFO Info;
    VR_CONTEXT Context;
    ULONGLONG NewSectors;
    ULONGLONG Lcn;
    HANDLE Volume;
    HANDLE Root;
    NTSTATUS Status;
    ULONG Pass;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Progress = Progress;
    Context.ProgressContext = ProgressContext;
    Context.LastPercent = MAXULONG;

    Status = VrOpenVolume(VolumePath, &Volume);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Opening %wZ failed (Status 0x%08lx)\n", VolumePath, Status);
        return Status;
    }
    Status = VrQueryVolume(VolumePath, Volume, &Context, &Info);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Querying %wZ failed (Status 0x%08lx)\n", VolumePath, Status);
        goto Done;
    }
    if (ReclaimBytes == 0 || ReclaimBytes > Info.MaxReclaimableBytes)
    {
        DPRINT1("Cannot reclaim %I64u of %I64u bytes\n", ReclaimBytes, Info.MaxReclaimableBytes);
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    NewSectors = (Info.PartitionBytes - ReclaimBytes) / Info.BytesPerSector;
    if (Info.Raw || NewSectors * Info.BytesPerSector >= Info.VolumeBytes)
    {
        Status = VrGrowPartition(&Info, (LONGLONG)(NewSectors * Info.BytesPerSector) -
                                        (LONGLONG)Info.PartitionBytes);
        if (!NT_SUCCESS(Status))
            DPRINT1("Shrinking the partition failed (Status 0x%08lx)\n", Status);
        goto Done;
    }
    RtlZeroMemory(&Shrink, sizeof(Shrink));
    Shrink.ShrinkRequestType = ShrinkPrepare;
    Shrink.NewNumberOfSectors = (LONGLONG)NewSectors;
    Status = VrFsctl(Volume, FSCTL_SHRINK_VOLUME, &Shrink, sizeof(Shrink), NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Preparing the shrink to %I64u sectors failed (Status 0x%08lx)\n", NewSectors, Status);
        goto Done;
    }
    Context.Boundary = (NewSectors - 1) / (Info.BytesPerCluster / Info.BytesPerSector);

    Status = VrOpenRoot(VolumePath, &Root);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Opening the root of %wZ failed (Status 0x%08lx)\n", VolumePath, Status);
        goto Abort;
    }

    for (Pass = 0; Pass < 2; Pass++)
    {
        Status = VrLoadBitmap(&Context);
        if (!NT_SUCCESS(Status))
            break;
        Context.ClustersToMove = 0;
        Context.ClustersMoved = 0;
        for (Lcn = Context.Boundary; Lcn < Context.TotalClusters; Lcn++)
        {
            if (!VrIsFree(&Context, Lcn))
                Context.ClustersToMove++;
        }
        if (Context.ClustersToMove != 0)
        {
            ULONG Index;

            Context.Failure = STATUS_SUCCESS;
            Status = VrRelocateHandle(&Context, Root, FALSE);
            if (!NT_SUCCESS(Status))
                VrRecordFailure(&Context, Status);
            VrWalkDirectory(&Context, Root, 1);
            for (Index = 0; Index < RTL_NUMBER_OF(Metadata); Index++)
                VrRelocateNamed(&Context, Root, Metadata[Index], FALSE);
            VrRelocateNamed(&Context, Root, L"$Extend", TRUE);
        }

        Shrink.ShrinkRequestType = ShrinkCommit;
        Shrink.NewNumberOfSectors = 0;
        Status = VrFsctl(Volume, FSCTL_SHRINK_VOLUME, &Shrink, sizeof(Shrink), NULL, 0);
        if (Status != STATUS_ALREADY_COMMITTED)
            break;
    }
    NtClose(Root);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_ALREADY_COMMITTED && NT_SUCCESS(VrLoadBitmap(&Context)))
        {
            for (Lcn = Context.Boundary; Lcn < Context.TotalClusters && VrIsFree(&Context, Lcn); Lcn++)
                ;
            DPRINT1("Shrink commit failed: cluster %I64u of %I64u is still in use (boundary %I64u)\n",
                    Lcn, Context.TotalClusters, Context.Boundary);
        }
        DPRINT1("Shrink failed (Status 0x%08lx, first move failure 0x%08lx)\n", Status, Context.Failure);
        if (Status == STATUS_ALREADY_COMMITTED && !NT_SUCCESS(Context.Failure))
            Status = Context.Failure;
        goto Abort;
    }

    if (Progress)
        Progress(ProgressContext, 100);
    Status = VrGrowPartition(&Info, (LONGLONG)(NewSectors * Info.BytesPerSector) -
                                    (LONGLONG)Info.PartitionBytes);
    if (!NT_SUCCESS(Status))
        DPRINT1("Shrinking the partition failed (Status 0x%08lx)\n", Status);
    goto Done;

Abort:
    Shrink.ShrinkRequestType = ShrinkAbort;
    Shrink.NewNumberOfSectors = 0;
    VrFsctl(Volume, FSCTL_SHRINK_VOLUME, &Shrink, sizeof(Shrink), NULL, 0);

Done:
    NtClose(Volume);
    VrCleanup(&Context);
    return Status;
}

NTSTATUS
NTAPI
VolResizeExtend(
    _In_ PCUNICODE_STRING VolumePath,
    _In_ ULONGLONG AddBytes,
    _In_ BOOLEAN ExtendFileSystem)
{
    PARTITION_INFORMATION_EX Partition;
    STORAGE_DEVICE_NUMBER Number;
    GET_LENGTH_INFORMATION Length;
    VOLRESIZE_INFO Info;
    LONGLONG Sectors;
    HANDLE Volume;
    NTSTATUS Status;

    if (AddBytes > MAXLONGLONG || (AddBytes == 0 && !ExtendFileSystem))
        return STATUS_INVALID_PARAMETER;

    Status = VrOpenVolume(VolumePath, &Volume);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Info, sizeof(Info));
    Status = VrIoctl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Partition, sizeof(Partition));
    if (NT_SUCCESS(Status))
        Status = VrIoctl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number));
    if (NT_SUCCESS(Status))
    {
        Info.DiskNumber = Number.DeviceNumber;
        Info.PartitionNumber = Partition.PartitionNumber;
        if (AddBytes != 0)
            Status = VrGrowPartition(&Info, (LONGLONG)AddBytes);
    }
    if (NT_SUCCESS(Status) && ExtendFileSystem)
    {
        NTFS_VOLUME_DATA_BUFFER Ntfs;

        Status = VrFsctl(Volume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &Ntfs, sizeof(Ntfs));
        if (NT_SUCCESS(Status))
            Status = VrIoctl(Volume, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &Length, sizeof(Length));
        if (NT_SUCCESS(Status))
        {
            Sectors = Length.Length.QuadPart / Ntfs.BytesPerSector;
            Status = VrFsctl(Volume, FSCTL_EXTEND_VOLUME, &Sectors, sizeof(Sectors), NULL, 0);
        }
    }
    NtClose(Volume);
    return Status;
}
