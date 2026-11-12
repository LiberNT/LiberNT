/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Backup and restore of the streams of a file or directory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>
#include <winioctl.h>

#define NDEBUG
#include <debug.h>

#define BACKUP_STREAM_HEADER_SIZE FIELD_OFFSET(WIN32_STREAM_ID, cStreamName)

typedef enum _BACKUP_PHASE
{
    BackupPhaseSecurity,
    BackupPhaseData,
    BackupPhaseSparse,
    BackupPhaseEa,
    BackupPhaseAlternateData,
    BackupPhaseReparse,
    BackupPhaseObjectId,
    BackupPhaseDone
} BACKUP_PHASE;

typedef struct _BACKUP_CONTEXT
{
    BOOLEAN Restore;

    BOOLEAN InStream;
    DWORD StreamId;
    DWORD StreamAttributes;
    PUCHAR Header;
    ULONG HeaderCapacity;
    ULONG HeaderLength;
    ULONG HeaderDone;
    LONGLONG DataSize;
    LONGLONG DataDone;
    UCHAR Prefix[sizeof(LARGE_INTEGER)];
    ULONG PrefixLength;
    PUCHAR Memory;
    HANDLE StreamHandle;
    BOOLEAN FromFile;
    BOOLEAN Skipped;
    LONGLONG FileOffset;

    BACKUP_PHASE Phase;
    ULONG FileAttributes;
    LONGLONG EndOfFile;
    PFILE_STREAM_INFORMATION Streams;
    PFILE_STREAM_INFORMATION NextStream;
    PFILE_ALLOCATED_RANGE_BUFFER Ranges;
    ULONG RangeCount;
    ULONG NextRange;
} BACKUP_CONTEXT, *PBACKUP_CONTEXT;

static const WCHAR BasepUnnamedDataStream[] = L"::$DATA";

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
BasepBackupEndStream(
    _Inout_ PBACKUP_CONTEXT Context)
{
    if (Context->StreamHandle)
    {
        NtClose(Context->StreamHandle);
        Context->StreamHandle = NULL;
    }
    if (Context->Memory)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context->Memory);
        Context->Memory = NULL;
    }
    Context->InStream = FALSE;
    Context->FromFile = FALSE;
    Context->Skipped = FALSE;
    Context->PrefixLength = 0;
    Context->HeaderLength = 0;
    Context->HeaderDone = 0;
    Context->DataSize = 0;
    Context->DataDone = 0;
    Context->FileOffset = 0;
}

static
VOID
BasepBackupFreeContext(
    _In_opt_ PBACKUP_CONTEXT Context)
{
    if (!Context)
        return;

    BasepBackupEndStream(Context);
    if (Context->Header)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context->Header);
    if (Context->Streams)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context->Streams);
    if (Context->Ranges)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context->Ranges);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
}

static
NTSTATUS
BasepBackupReserveHeader(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ ULONG Length)
{
    PUCHAR Header;

    if (Length <= Context->HeaderCapacity)
        return STATUS_SUCCESS;

    Header = Context->Header
        ? RtlReAllocateHeap(RtlGetProcessHeap(), 0, Context->Header, Length)
        : RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!Header)
        return STATUS_NO_MEMORY;

    Context->Header = Header;
    Context->HeaderCapacity = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupStartStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ DWORD StreamId,
    _In_ DWORD StreamAttributes,
    _In_reads_bytes_opt_(NameSize) PCWSTR Name,
    _In_ ULONG NameSize,
    _In_ LONGLONG DataSize)
{
    LARGE_INTEGER Size;
    NTSTATUS Status;

    Status = BasepBackupReserveHeader(Context, BACKUP_STREAM_HEADER_SIZE + NameSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Size.QuadPart = DataSize;
    RtlCopyMemory(Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamId), &StreamId, sizeof(DWORD));
    RtlCopyMemory(Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamAttributes), &StreamAttributes, sizeof(DWORD));
    RtlCopyMemory(Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, Size), &Size, sizeof(Size));
    RtlCopyMemory(Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamNameSize), &NameSize, sizeof(DWORD));
    if (NameSize)
        RtlCopyMemory(Context->Header + BACKUP_STREAM_HEADER_SIZE, Name, NameSize);

    Context->InStream = TRUE;
    Context->StreamId = StreamId;
    Context->StreamAttributes = StreamAttributes;
    Context->HeaderLength = BACKUP_STREAM_HEADER_SIZE + NameSize;
    Context->HeaderDone = 0;
    Context->DataSize = DataSize;
    Context->DataDone = 0;
    return STATUS_SUCCESS;
}

static
VOID
BasepBackupSetFileBody(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_opt_ HANDLE StreamHandle,
    _In_ LONGLONG FileOffset)
{
    Context->FromFile = TRUE;
    Context->StreamHandle = StreamHandle;
    Context->FileOffset = FileOffset;
}

static
BOOLEAN
BasepBackupIsUnnamedStream(
    _In_ PFILE_STREAM_INFORMATION Stream)
{
    return Stream->StreamNameLength == sizeof(BasepUnnamedDataStream) - sizeof(WCHAR) &&
           RtlCompareMemory(Stream->StreamName, BasepUnnamedDataStream, Stream->StreamNameLength) ==
               Stream->StreamNameLength;
}

static
NTSTATUS
BasepBackupOpenStream(
    _In_ HANDLE File,
    _In_reads_bytes_(NameSize) PCWSTR Name,
    _In_ ULONG NameSize,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG Disposition,
    _Out_ PHANDLE StreamHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING StreamName;

    if (NameSize == 0 || NameSize > MAXUSHORT || (NameSize & (sizeof(WCHAR) - 1)))
        return STATUS_OBJECT_NAME_INVALID;

    StreamName.Buffer = (PWSTR)Name;
    StreamName.Length = StreamName.MaximumLength = (USHORT)NameSize;
    InitializeObjectAttributes(&ObjectAttributes, &StreamName, 0, File, NULL);

    return NtCreateFile(StreamHandle,
                        DesiredAccess | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        NULL,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        Disposition,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT,
                        NULL,
                        0);
}

static
NTSTATUS
BasepBackupQueryStreams(
    _In_ HANDLE File,
    _Out_ PFILE_STREAM_INFORMATION *Streams)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_STREAM_INFORMATION Buffer;
    ULONG Size = 0x1000;
    NTSTATUS Status;

    *Streams = NULL;
    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Buffer)
            return STATUS_NO_MEMORY;

        Status = NtQueryInformationFile(File, &IoStatusBlock, Buffer, Size, FileStreamInformation);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;

        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Size > MAXULONG / 2)
            return STATUS_NO_MEMORY;
        Size *= 2;
    }

    if (!NT_SUCCESS(Status) || IoStatusBlock.Information == 0)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
    }

    *Streams = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupQuerySecurity(
    _In_ HANDLE File,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    FILE_ACCESS_INFORMATION Access;
    IO_STATUS_BLOCK IoStatusBlock;
    SECURITY_INFORMATION Information = OWNER_SECURITY_INFORMATION |
                                       GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION |
                                       LABEL_SECURITY_INFORMATION;
    PUCHAR Buffer;
    ULONG Size = 0x200;
    ULONG Needed;
    NTSTATUS Status;

    Status = NtQueryInformationFile(File, &IoStatusBlock, &Access, sizeof(Access), FileAccessInformation);
    if (NT_SUCCESS(Status) && (Access.AccessFlags & ACCESS_SYSTEM_SECURITY))
        Information |= SACL_SECURITY_INFORMATION;

    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Buffer)
            return STATUS_NO_MEMORY;

        Needed = 0;
        Status = NtQuerySecurityObject(File, Information, Buffer, Size, &Needed);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Needed <= Size)
            return STATUS_INVALID_SECURITY_DESCR;
        Size = Needed;
    }

    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return Status;
    }

    *Data = Buffer;
    *Length = RtlLengthSecurityDescriptor((PSECURITY_DESCRIPTOR)Buffer);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupQueryEa(
    _In_ HANDLE File,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    FILE_EA_INFORMATION EaInformation;
    IO_STATUS_BLOCK IoStatusBlock;
    PUCHAR Buffer;
    ULONG Size;
    NTSTATUS Status;

    *Data = NULL;
    *Length = 0;

    Status = NtQueryInformationFile(File, &IoStatusBlock, &EaInformation, sizeof(EaInformation), FileEaInformation);
    if (!NT_SUCCESS(Status) || EaInformation.EaSize == 0)
        return STATUS_SUCCESS;

    Size = EaInformation.EaSize;
    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Buffer)
            return STATUS_NO_MEMORY;

        Status = NtQueryEaFile(File, &IoStatusBlock, Buffer, Size, FALSE, NULL, 0, NULL, TRUE);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;

        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Size > MAXULONG / 2)
            return STATUS_NO_MEMORY;
        Size *= 2;
    }

    if (Status == STATUS_NO_EAS_ON_FILE || (NT_SUCCESS(Status) && IoStatusBlock.Information == 0))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return Status;
    }

    *Data = Buffer;
    *Length = (ULONG)IoStatusBlock.Information;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupQueryReparse(
    _In_ HANDLE File,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PUCHAR Buffer;
    NTSTATUS Status;

    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (!Buffer)
        return STATUS_NO_MEMORY;

    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_GET_REPARSE_POINT,
                             NULL, 0, Buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return Status;
    }

    *Data = Buffer;
    *Length = (ULONG)IoStatusBlock.Information;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupQueryObjectId(
    _In_ HANDLE File,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PUCHAR Buffer;
    NTSTATUS Status;

    *Data = NULL;
    *Length = 0;
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(FILE_OBJECTID_BUFFER));
    if (!Buffer)
        return STATUS_NO_MEMORY;

    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_GET_OBJECT_ID,
                             NULL, 0, Buffer, sizeof(FILE_OBJECTID_BUFFER));
    if (!NT_SUCCESS(Status) || IoStatusBlock.Information != sizeof(FILE_OBJECTID_BUFFER))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Status == STATUS_OBJECTID_NOT_FOUND || Status == STATUS_INVALID_DEVICE_REQUEST ||
            Status == STATUS_VOLUME_NOT_UPGRADED || Status == STATUS_NOT_IMPLEMENTED ||
            Status == STATUS_NOT_SUPPORTED || NT_SUCCESS(Status))
        {
            return STATUS_SUCCESS;
        }
        return Status;
    }

    *Data = Buffer;
    *Length = sizeof(FILE_OBJECTID_BUFFER);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupQueryRanges(
    _In_ HANDLE File,
    _In_ LONGLONG EndOfFile,
    _Out_ PFILE_ALLOCATED_RANGE_BUFFER *Ranges,
    _Out_ PULONG RangeCount)
{
    FILE_ALLOCATED_RANGE_BUFFER Query;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_ALLOCATED_RANGE_BUFFER Buffer;
    ULONG Size = 16 * sizeof(FILE_ALLOCATED_RANGE_BUFFER);
    NTSTATUS Status;

    *Ranges = NULL;
    *RangeCount = 0;
    if (EndOfFile == 0)
        return STATUS_SUCCESS;

    Query.FileOffset.QuadPart = 0;
    Query.Length.QuadPart = EndOfFile;
    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Buffer)
            return STATUS_NO_MEMORY;

        Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_QUERY_ALLOCATED_RANGES,
                                 &Query, sizeof(Query), Buffer, Size);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;

        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Size > MAXULONG / 2)
            return STATUS_NO_MEMORY;
        Size *= 2;
    }

    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return Status;
    }

    *Ranges = Buffer;
    *RangeCount = (ULONG)(IoStatusBlock.Information / sizeof(FILE_ALLOCATED_RANGE_BUFFER));
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupCreateReadContext(
    _In_ HANDLE File,
    _Out_ PBACKUP_CONTEXT *Context)
{
    FILE_ATTRIBUTE_TAG_INFORMATION TagInformation;
    FILE_STANDARD_INFORMATION StandardInformation;
    IO_STATUS_BLOCK IoStatusBlock;
    PBACKUP_CONTEXT NewContext;
    NTSTATUS Status;

    Status = NtQueryInformationFile(File, &IoStatusBlock, &TagInformation, sizeof(TagInformation),
                                    FileAttributeTagInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtQueryInformationFile(File, &IoStatusBlock, &StandardInformation, sizeof(StandardInformation),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    NewContext = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*NewContext));
    if (!NewContext)
        return STATUS_NO_MEMORY;

    Status = BasepBackupQueryStreams(File, &NewContext->Streams);
    if (!NT_SUCCESS(Status))
    {
        BasepBackupFreeContext(NewContext);
        return Status;
    }

    NewContext->Phase = BackupPhaseSecurity;
    NewContext->FileAttributes = TagInformation.FileAttributes;
    NewContext->EndOfFile = StandardInformation.Directory ? 0 : StandardInformation.EndOfFile.QuadPart;
    *Context = NewContext;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupStartMemoryStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ DWORD StreamId,
    _In_ DWORD StreamAttributes,
    _In_ PUCHAR Data,
    _In_ ULONG Length)
{
    NTSTATUS Status;

    Status = BasepBackupStartStream(Context, StreamId, StreamAttributes, NULL, 0, Length);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Data);
        return Status;
    }

    Context->Memory = Data;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupStartDataStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _Out_ PBOOLEAN Found)
{
    PFILE_STREAM_INFORMATION Stream = Context->Streams;
    NTSTATUS Status;

    while (Stream && !BasepBackupIsUnnamedStream(Stream))
        Stream = Stream->NextEntryOffset ? (PFILE_STREAM_INFORMATION)((PUCHAR)Stream + Stream->NextEntryOffset) : NULL;
    if (!Stream)
        return STATUS_SUCCESS;

    if (!(Context->FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE))
    {
        if (Stream->StreamSize.QuadPart == 0)
            return STATUS_SUCCESS;

        Status = BasepBackupStartStream(Context, BACKUP_DATA, STREAM_NORMAL_ATTRIBUTE, NULL, 0,
                                        Stream->StreamSize.QuadPart);
        if (!NT_SUCCESS(Status))
            return Status;
        BasepBackupSetFileBody(Context, NULL, 0);
        *Found = TRUE;
        return STATUS_SUCCESS;
    }

    Status = BasepBackupQueryRanges(File, Context->EndOfFile, &Context->Ranges, &Context->RangeCount);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Context->RangeCount == 1)
    {
        Status = BasepBackupStartStream(Context, BACKUP_DATA, STREAM_SPARSE_ATTRIBUTE, NULL, 0,
                                        Context->EndOfFile);
        if (!NT_SUCCESS(Status))
            return Status;
        BasepBackupSetFileBody(Context, NULL, 0);
        *Found = TRUE;
        return STATUS_SUCCESS;
    }

    Status = BasepBackupStartStream(Context, BACKUP_DATA, STREAM_SPARSE_ATTRIBUTE, NULL, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Context->EndOfFile != 0)
        Context->Phase = BackupPhaseSparse;
    Context->NextRange = 0;
    *Found = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupStartSparseBlock(
    _Inout_ PBACKUP_CONTEXT Context)
{
    PFILE_ALLOCATED_RANGE_BUFFER Range;
    LARGE_INTEGER Offset;
    LONGLONG Length = 0;
    NTSTATUS Status;

    if (Context->NextRange < Context->RangeCount)
    {
        Range = &Context->Ranges[Context->NextRange++];
        Offset = Range->FileOffset;
        Length = Range->Length.QuadPart;
    }
    else
    {
        Offset.QuadPart = Context->EndOfFile;
        Context->Phase = BackupPhaseEa;
    }

    Status = BasepBackupStartStream(Context, BACKUP_SPARSE_BLOCK, STREAM_SPARSE_ATTRIBUTE, NULL, 0,
                                    sizeof(Offset) + Length);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(Context->Prefix, &Offset, sizeof(Offset));
    Context->PrefixLength = sizeof(Offset);
    BasepBackupSetFileBody(Context, NULL, Offset.QuadPart);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupStartAlternateStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _Out_ PBOOLEAN Found)
{
    PFILE_STREAM_INFORMATION Stream;
    HANDLE StreamHandle;
    NTSTATUS Status;

    while (Context->NextStream)
    {
        Stream = Context->NextStream;
        Context->NextStream = Stream->NextEntryOffset
            ? (PFILE_STREAM_INFORMATION)((PUCHAR)Stream + Stream->NextEntryOffset)
            : NULL;
        if (BasepBackupIsUnnamedStream(Stream))
            continue;

        Status = BasepBackupOpenStream(File, Stream->StreamName, Stream->StreamNameLength,
                                       FILE_READ_DATA, FILE_OPEN, &StreamHandle);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = BasepBackupStartStream(Context, BACKUP_ALTERNATE_DATA, STREAM_NORMAL_ATTRIBUTE,
                                        Stream->StreamName, Stream->StreamNameLength,
                                        Stream->StreamSize.QuadPart);
        if (!NT_SUCCESS(Status))
        {
            NtClose(StreamHandle);
            return Status;
        }
        BasepBackupSetFileBody(Context, StreamHandle, 0);
        *Found = TRUE;
        return STATUS_SUCCESS;
    }

    Context->Phase = BackupPhaseReparse;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupNextReadStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _In_ BOOL ProcessSecurity,
    _Out_ PBOOLEAN Found)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUCHAR Data;
    ULONG Length;

    *Found = FALSE;
    while (NT_SUCCESS(Status) && !*Found && Context->Phase != BackupPhaseDone)
    {
        switch (Context->Phase)
        {
            case BackupPhaseSecurity:
                Context->Phase = BackupPhaseData;
                if (!ProcessSecurity)
                    break;
                Status = BasepBackupQuerySecurity(File, &Data, &Length);
                if (NT_SUCCESS(Status))
                    Status = BasepBackupStartMemoryStream(Context, BACKUP_SECURITY_DATA,
                                                          STREAM_CONTAINS_SECURITY, Data, Length);
                *Found = NT_SUCCESS(Status);
                break;

            case BackupPhaseData:
                Context->Phase = BackupPhaseEa;
                Status = BasepBackupStartDataStream(Context, File, Found);
                break;

            case BackupPhaseSparse:
                Status = BasepBackupStartSparseBlock(Context);
                *Found = NT_SUCCESS(Status);
                break;

            case BackupPhaseEa:
                Context->Phase = BackupPhaseAlternateData;
                Context->NextStream = Context->Streams;
                Status = BasepBackupQueryEa(File, &Data, &Length);
                if (NT_SUCCESS(Status) && Data)
                {
                    Status = BasepBackupStartMemoryStream(Context, BACKUP_EA_DATA,
                                                          STREAM_NORMAL_ATTRIBUTE, Data, Length);
                    *Found = NT_SUCCESS(Status);
                }
                break;

            case BackupPhaseAlternateData:
                Status = BasepBackupStartAlternateStream(Context, File, Found);
                break;

            case BackupPhaseReparse:
                Context->Phase = BackupPhaseObjectId;
                if (!(Context->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    break;
                Status = BasepBackupQueryReparse(File, &Data, &Length);
                if (NT_SUCCESS(Status))
                    Status = BasepBackupStartMemoryStream(Context, BACKUP_REPARSE_DATA,
                                                          STREAM_NORMAL_ATTRIBUTE, Data, Length);
                *Found = NT_SUCCESS(Status);
                break;

            case BackupPhaseObjectId:
                Context->Phase = BackupPhaseDone;
                Status = BasepBackupQueryObjectId(File, &Data, &Length);
                if (NT_SUCCESS(Status) && Data)
                {
                    Status = BasepBackupStartMemoryStream(Context, BACKUP_OBJECT_ID,
                                                          STREAM_NORMAL_ATTRIBUTE, Data, Length);
                    *Found = NT_SUCCESS(Status);
                }
                break;

            default:
                Context->Phase = BackupPhaseDone;
                break;
        }
    }

    return Status;
}

static
NTSTATUS
BasepBackupReadData(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _Out_writes_bytes_to_(Space, *Copied) PUCHAR Buffer,
    _In_ ULONG Space,
    _Out_ PULONG Copied)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    LONGLONG Body;
    ULONG Chunk;
    NTSTATUS Status;

    *Copied = 0;
    Chunk = (ULONG)min((LONGLONG)Space, Context->DataSize - Context->DataDone);

    if (Context->DataDone < Context->PrefixLength)
    {
        Chunk = min(Chunk, Context->PrefixLength - (ULONG)Context->DataDone);
        RtlCopyMemory(Buffer, Context->Prefix + Context->DataDone, Chunk);
        Context->DataDone += Chunk;
        *Copied = Chunk;
        return STATUS_SUCCESS;
    }

    Body = Context->DataDone - Context->PrefixLength;
    if (Context->Memory)
    {
        RtlCopyMemory(Buffer, Context->Memory + Body, Chunk);
        Context->DataDone += Chunk;
        *Copied = Chunk;
        return STATUS_SUCCESS;
    }

    Offset.QuadPart = Context->FileOffset + Body;
    Status = NtReadFile(Context->StreamHandle ? Context->StreamHandle : File, NULL, NULL, NULL,
                        &IoStatusBlock, Buffer, Chunk, &Offset, NULL);
    if (NT_SUCCESS(Status) && IoStatusBlock.Information == 0)
        Status = STATUS_END_OF_FILE;
    if (!NT_SUCCESS(Status))
        return Status;

    Context->DataDone += IoStatusBlock.Information;
    *Copied = (ULONG)IoStatusBlock.Information;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupRestoreSecurity(
    _In_ HANDLE File,
    _In_ PUCHAR Data,
    _In_ ULONG Length)
{
    PSECURITY_DESCRIPTOR Descriptor = (PSECURITY_DESCRIPTOR)Data;
    SECURITY_INFORMATION Information = 0;
    FILE_ACCESS_INFORMATION Access;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN Present;
    BOOLEAN Defaulted;
    PSID Sid;
    PACL Acl;
    NTSTATUS Status;

    if (!RtlValidRelativeSecurityDescriptor(Descriptor, Length, 0))
        return STATUS_INVALID_SECURITY_DESCR;

    Status = NtQueryInformationFile(File, &IoStatusBlock, &Access, sizeof(Access), FileAccessInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Access.AccessFlags & WRITE_OWNER)
    {
        if (NT_SUCCESS(RtlGetOwnerSecurityDescriptor(Descriptor, &Sid, &Defaulted)) && Sid)
            Information |= OWNER_SECURITY_INFORMATION;
        if (NT_SUCCESS(RtlGetGroupSecurityDescriptor(Descriptor, &Sid, &Defaulted)) && Sid)
            Information |= GROUP_SECURITY_INFORMATION;
        if (NT_SUCCESS(RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Acl, &Defaulted)) && Present)
            Information |= LABEL_SECURITY_INFORMATION;
    }
    if ((Access.AccessFlags & WRITE_DAC) &&
        NT_SUCCESS(RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Acl, &Defaulted)) && Present)
    {
        Information |= DACL_SECURITY_INFORMATION;
    }
    if ((Access.AccessFlags & ACCESS_SYSTEM_SECURITY) &&
        NT_SUCCESS(RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Acl, &Defaulted)) && Present)
    {
        Information |= SACL_SECURITY_INFORMATION;
    }
    if (!Information)
        return STATUS_SUCCESS;

    return NtSetSecurityObject(File, Information, Descriptor);
}

static
NTSTATUS
BasepBackupRestoreLink(
    _In_ HANDLE File,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    PFILE_LINK_INFORMATION LinkInformation;
    UNICODE_STRING NtName;
    IO_STATUS_BLOCK IoStatusBlock;
    PWSTR Path;
    ULONG Characters = Length / sizeof(WCHAR);
    ULONG Size;
    NTSTATUS Status;

    while (Characters && ((PCWSTR)Data)[Characters - 1] == UNICODE_NULL)
        Characters--;
    if (!Characters)
        return STATUS_OBJECT_NAME_INVALID;

    Path = RtlAllocateHeap(RtlGetProcessHeap(), 0, (Characters + 1) * sizeof(WCHAR));
    if (!Path)
        return STATUS_NO_MEMORY;
    RtlCopyMemory(Path, Data, Characters * sizeof(WCHAR));
    Path[Characters] = UNICODE_NULL;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(Path, &NtName, NULL, NULL);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Path);
    if (!NT_SUCCESS(Status))
        return Status;

    Size = FIELD_OFFSET(FILE_LINK_INFORMATION, FileName) + NtName.Length;
    LinkInformation = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (!LinkInformation)
    {
        RtlFreeUnicodeString(&NtName);
        return STATUS_NO_MEMORY;
    }

    LinkInformation->ReplaceIfExists = FALSE;
    LinkInformation->RootDirectory = NULL;
    LinkInformation->FileNameLength = NtName.Length;
    RtlCopyMemory(LinkInformation->FileName, NtName.Buffer, NtName.Length);
    RtlFreeUnicodeString(&NtName);

    Status = NtSetInformationFile(File, &IoStatusBlock, LinkInformation, Size, FileLinkInformation);
    RtlFreeHeap(RtlGetProcessHeap(), 0, LinkInformation);
    return Status;
}

static
VOID
BasepBackupRestoreObjectId(
    _In_ HANDLE File,
    _In_reads_bytes_(sizeof(FILE_OBJECTID_BUFFER)) PUCHAR Data)
{
    PFILE_OBJECTID_BUFFER Saved = (PFILE_OBJECTID_BUFFER)Data;
    FILE_OBJECTID_BUFFER Current;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN HadCurrent;
    NTSTATUS Status;

    /* FIXME: whether an object identifier that cannot be set fails the restore is not documented; it is skipped */
    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_GET_OBJECT_ID,
                             NULL, 0, &Current, sizeof(Current));
    HadCurrent = NT_SUCCESS(Status) && IoStatusBlock.Information == sizeof(Current);
    if (HadCurrent && RtlEqualMemory(Current.ObjectId, Saved->ObjectId, sizeof(Saved->ObjectId)))
    {
        if (!RtlEqualMemory(Current.ExtendedInfo, Saved->ExtendedInfo, sizeof(Saved->ExtendedInfo)))
        {
            NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_OBJECT_ID_EXTENDED,
                            Saved->ExtendedInfo, sizeof(Saved->ExtendedInfo), NULL, 0);
        }
        return;
    }

    if (HadCurrent)
        NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0);
    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_OBJECT_ID,
                             Saved, sizeof(*Saved), NULL, 0);
    if (!NT_SUCCESS(Status) && HadCurrent)
    {
        NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_OBJECT_ID,
                        &Current, sizeof(Current), NULL, 0);
    }
}

static
NTSTATUS
BasepBackupAllocateMemory(
    _Inout_ PBACKUP_CONTEXT Context)
{
    if (Context->DataSize > MAXULONG)
        return STATUS_NO_MEMORY;
    Context->Memory = RtlAllocateHeap(RtlGetProcessHeap(), 0, (SIZE_T)max(Context->DataSize, 1));
    return Context->Memory ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

static
NTSTATUS
BasepBackupBeginRestoreStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _In_ BOOL ProcessSecurity)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Size;
    DWORD NameSize;
    HANDLE StreamHandle;
    NTSTATUS Status;

    RtlCopyMemory(&Context->StreamId, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamId), sizeof(DWORD));
    RtlCopyMemory(&Context->StreamAttributes, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamAttributes), sizeof(DWORD));
    RtlCopyMemory(&Size, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, Size), sizeof(Size));
    RtlCopyMemory(&NameSize, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamNameSize), sizeof(DWORD));
    if (Size.QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    Context->InStream = TRUE;
    Context->DataSize = Size.QuadPart;
    Context->DataDone = 0;

    switch (Context->StreamId)
    {
        case BACKUP_DATA:
            if (Context->StreamAttributes & STREAM_SPARSE_ATTRIBUTE)
            {
                NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_SPARSE,
                                NULL, 0, NULL, 0);
            }
            BasepBackupSetFileBody(Context, NULL, 0);
            break;

        case BACKUP_ALTERNATE_DATA:
            Status = BasepBackupOpenStream(File, (PCWSTR)(Context->Header + BACKUP_STREAM_HEADER_SIZE), NameSize,
                                           FILE_WRITE_DATA, FILE_OVERWRITE_IF, &StreamHandle);
            if (!NT_SUCCESS(Status))
                return Status;
            BasepBackupSetFileBody(Context, StreamHandle, 0);
            break;

        case BACKUP_SPARSE_BLOCK:
            if (Context->DataSize < (LONGLONG)sizeof(LARGE_INTEGER))
                return STATUS_INVALID_PARAMETER;
            StreamHandle = NULL;
            if (NameSize)
            {
                Status = BasepBackupOpenStream(File, (PCWSTR)(Context->Header + BACKUP_STREAM_HEADER_SIZE), NameSize,
                                               FILE_WRITE_DATA, FILE_OPEN_IF, &StreamHandle);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
            Context->PrefixLength = sizeof(LARGE_INTEGER);
            BasepBackupSetFileBody(Context, StreamHandle, 0);
            break;

        case BACKUP_LINK:
            if (Context->DataSize == 0 || Context->DataSize > UNICODE_STRING_MAX_BYTES ||
                (Context->DataSize & (sizeof(WCHAR) - 1)))
            {
                return STATUS_OBJECT_NAME_INVALID;
            }
            return BasepBackupAllocateMemory(Context);

        case BACKUP_SECURITY_DATA:
            if (!ProcessSecurity)
                break;
            return BasepBackupAllocateMemory(Context);

        case BACKUP_EA_DATA:
        case BACKUP_REPARSE_DATA:
            return BasepBackupAllocateMemory(Context);

        case BACKUP_OBJECT_ID:
            if (Context->DataSize != sizeof(FILE_OBJECTID_BUFFER))
                return STATUS_INVALID_PARAMETER;
            return BasepBackupAllocateMemory(Context);

        case BACKUP_TXFS_DATA:
            break;

        case BACKUP_GHOSTED_FILE_EXTENTS:
            /* TODO: ghosted extents are skipped: the file system interface that recreates them is not documented */
            break;

        default:
            /* FIXME: the last error for a stream type outside [MS-BKUP] 2.2 is not documented */
            return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
BasepBackupEndRestoreStream(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _In_ BOOL ProcessSecurity)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Context->Skipped)
    {
        BasepBackupEndStream(Context);
        return STATUS_SUCCESS;
    }

    switch (Context->StreamId)
    {
        case BACKUP_EA_DATA:
            if (Context->DataSize)
                Status = NtSetEaFile(File, &IoStatusBlock, Context->Memory, (ULONG)Context->DataSize);
            break;

        case BACKUP_REPARSE_DATA:
            if (Context->DataSize)
            {
                Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_REPARSE_POINT,
                                         Context->Memory, (ULONG)Context->DataSize, NULL, 0);
            }
            break;

        case BACKUP_SECURITY_DATA:
            if (ProcessSecurity && Context->Memory && Context->DataSize)
                Status = BasepBackupRestoreSecurity(File, Context->Memory, (ULONG)Context->DataSize);
            break;

        case BACKUP_LINK:
            Status = BasepBackupRestoreLink(File, Context->Memory, (ULONG)Context->DataSize);
            break;

        case BACKUP_OBJECT_ID:
            BasepBackupRestoreObjectId(File, Context->Memory);
            break;

        case BACKUP_SPARSE_BLOCK:
            if (Context->DataSize == sizeof(LARGE_INTEGER) && Context->DataDone == Context->DataSize)
            {
                EndOfFile.EndOfFile.QuadPart = Context->FileOffset;
                Status = NtSetInformationFile(Context->StreamHandle ? Context->StreamHandle : File,
                                              &IoStatusBlock, &EndOfFile, sizeof(EndOfFile),
                                              FileEndOfFileInformation);
            }
            break;

        default:
            break;
    }

    BasepBackupEndStream(Context);
    return Status;
}

static
NTSTATUS
BasepBackupWriteData(
    _Inout_ PBACKUP_CONTEXT Context,
    _In_ HANDLE File,
    _In_reads_bytes_(Available) PUCHAR Buffer,
    _In_ ULONG Available,
    _Out_ PULONG Consumed)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    LONGLONG Body;
    ULONG Chunk;
    NTSTATUS Status;

    *Consumed = 0;
    Chunk = (ULONG)min((LONGLONG)Available, Context->DataSize - Context->DataDone);

    if (Context->DataDone < Context->PrefixLength)
    {
        Chunk = min(Chunk, Context->PrefixLength - (ULONG)Context->DataDone);
        RtlCopyMemory(Context->Prefix + Context->DataDone, Buffer, Chunk);
        Context->DataDone += Chunk;
        if (Context->DataDone == Context->PrefixLength)
            RtlCopyMemory(&Context->FileOffset, Context->Prefix, sizeof(Context->FileOffset));
        *Consumed = Chunk;
        return STATUS_SUCCESS;
    }

    Body = Context->DataDone - Context->PrefixLength;
    if (Context->Memory)
    {
        RtlCopyMemory(Context->Memory + Body, Buffer, Chunk);
    }
    else if (Context->FromFile)
    {
        Offset.QuadPart = Context->FileOffset + Body;
        Status = NtWriteFile(Context->StreamHandle ? Context->StreamHandle : File, NULL, NULL, NULL,
                             &IoStatusBlock, Buffer, Chunk, &Offset, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        Chunk = (ULONG)IoStatusBlock.Information;
        if (Chunk == 0)
            return STATUS_DISK_FULL;
    }

    Context->DataDone += Chunk;
    *Consumed = Chunk;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
BackupRead(
    HANDLE hFile,
    LPBYTE lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    BOOL bAbort,
    BOOL bProcessSecurity,
    LPVOID *lpContext)
{
    PBACKUP_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Found;
    ULONG Done = 0;
    ULONG Chunk;

    if (bAbort)
    {
        if (lpContext)
        {
            BasepBackupFreeContext(*lpContext);
            *lpContext = NULL;
        }
        return TRUE;
    }

    Context = *lpContext;
    if (!Context)
    {
        Status = BasepBackupCreateReadContext(hFile, &Context);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
        *lpContext = Context;
    }
    else if (Context->Restore)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    while (Done < nNumberOfBytesToRead)
    {
        if (!Context->InStream)
        {
            Status = BasepBackupNextReadStream(Context, hFile, bProcessSecurity, &Found);
            if (!NT_SUCCESS(Status) || !Found)
                break;
        }

        if (Context->HeaderDone < Context->HeaderLength)
        {
            Chunk = min(Context->HeaderLength - Context->HeaderDone, nNumberOfBytesToRead - Done);
            RtlCopyMemory(lpBuffer + Done, Context->Header + Context->HeaderDone, Chunk);
            Context->HeaderDone += Chunk;
            Done += Chunk;
        }
        else if (Context->DataDone < Context->DataSize)
        {
            Status = BasepBackupReadData(Context, hFile, lpBuffer + Done, nNumberOfBytesToRead - Done, &Chunk);
            Done += Chunk;
            if (!NT_SUCCESS(Status))
                break;
        }
        else
        {
            BasepBackupEndStream(Context);
        }
    }

    *lpNumberOfBytesRead = Done;
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
BackupSeek(
    HANDLE hFile,
    DWORD dwLowBytesToSeek,
    DWORD dwHighBytesToSeek,
    LPDWORD lpdwLowByteSeeked,
    LPDWORD lpdwHighByteSeeked,
    LPVOID *lpContext)
{
    PBACKUP_CONTEXT Context = lpContext ? *lpContext : NULL;
    ULARGE_INTEGER Requested;
    ULARGE_INTEGER Seeked;
    BOOL Result = TRUE;

    Requested.LowPart = dwLowBytesToSeek;
    Requested.HighPart = dwHighBytesToSeek;
    Seeked.QuadPart = 0;

    if (!Context)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Context->InStream && Context->HeaderDone == Context->HeaderLength)
    {
        Seeked.QuadPart = min(Requested.QuadPart, (ULONGLONG)(Context->DataSize - Context->DataDone));
        Context->DataDone += Seeked.QuadPart;
        if (Context->Restore && Seeked.QuadPart)
            Context->Skipped = TRUE;
        if (Seeked.QuadPart < Requested.QuadPart || (Context->Restore && Context->DataDone == Context->DataSize))
            BasepBackupEndStream(Context);
    }

    if (Seeked.QuadPart < Requested.QuadPart)
    {
        /* FIXME: the last error of a seek past the end of a substream is not documented; ERROR_SEEK is a guess */
        SetLastError(ERROR_SEEK);
        Result = FALSE;
    }

    *lpdwLowByteSeeked = Seeked.LowPart;
    *lpdwHighByteSeeked = Seeked.HighPart;
    return Result;
}

/*
 * @implemented
 */
BOOL
WINAPI
BackupWrite(
    HANDLE hFile,
    LPBYTE lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    BOOL bAbort,
    BOOL bProcessSecurity,
    LPVOID *lpContext)
{
    PBACKUP_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Done = 0;
    ULONG Needed;
    ULONG Chunk;
    DWORD NameSize;

    if (bAbort)
    {
        if (lpContext)
        {
            BasepBackupFreeContext(*lpContext);
            *lpContext = NULL;
        }
        return TRUE;
    }

    Context = *lpContext;
    if (!Context)
    {
        Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
        if (!Context)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        Context->Restore = TRUE;
        Context->Phase = BackupPhaseDone;
        *lpContext = Context;
    }
    else if (!Context->Restore)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    while (Done < nNumberOfBytesToWrite && NT_SUCCESS(Status))
    {
        if (!Context->InStream)
        {
            Needed = BACKUP_STREAM_HEADER_SIZE;
            if (Context->HeaderDone >= BACKUP_STREAM_HEADER_SIZE)
            {
                RtlCopyMemory(&NameSize, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamNameSize),
                              sizeof(DWORD));
                if (NameSize > MAXULONG - BACKUP_STREAM_HEADER_SIZE)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                Needed += NameSize;
            }

            Status = BasepBackupReserveHeader(Context, Needed);
            if (!NT_SUCCESS(Status))
                break;

            Chunk = min(Needed - Context->HeaderDone, nNumberOfBytesToWrite - Done);
            RtlCopyMemory(Context->Header + Context->HeaderDone, lpBuffer + Done, Chunk);
            Context->HeaderDone += Chunk;
            Done += Chunk;

            if (Context->HeaderDone == BACKUP_STREAM_HEADER_SIZE && Needed == BACKUP_STREAM_HEADER_SIZE)
            {
                RtlCopyMemory(&NameSize, Context->Header + FIELD_OFFSET(WIN32_STREAM_ID, dwStreamNameSize),
                              sizeof(DWORD));
                if (NameSize)
                    continue;
            }
            if (Context->HeaderDone < Needed)
                continue;

            Context->HeaderLength = Context->HeaderDone;
            Status = BasepBackupBeginRestoreStream(Context, hFile, bProcessSecurity);
            if (NT_SUCCESS(Status) && Context->DataSize == 0)
                Status = BasepBackupEndRestoreStream(Context, hFile, bProcessSecurity);
            continue;
        }

        Status = BasepBackupWriteData(Context, hFile, lpBuffer + Done, nNumberOfBytesToWrite - Done, &Chunk);
        Done += Chunk;
        if (NT_SUCCESS(Status) && Context->DataDone == Context->DataSize)
            Status = BasepBackupEndRestoreStream(Context, hFile, bProcessSecurity);
    }

    *lpNumberOfBytesWritten = Done;
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

/* EOF */
