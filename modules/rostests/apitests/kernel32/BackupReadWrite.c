/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test BackupRead, BackupSeek and BackupWrite
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/setypes.h>

#define HEADER_SIZE 20
#define MAX_STREAMS 64
#define MANY_STREAMS 48
#define CAPTURE_SIZE 0x120000
#define SPARSE_SIZE 0x100000
#define SPARSE_SECOND 0x80000

typedef struct _PARSED_STREAM
{
    DWORD Id;
    DWORD Attributes;
    LONGLONG Size;
    DWORD NameSize;
    WCHAR Name[MAX_PATH];
    PUCHAR Data;
} PARSED_STREAM, *PPARSED_STREAM;

static WCHAR TestDirectory[MAX_PATH];

static
VOID
MakePath(
    _Out_writes_(MAX_PATH) PWSTR Path,
    _In_ PCWSTR Name)
{
    StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", TestDirectory, Name);
}

static
BOOL
WriteTestFile(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Length) const void *Data,
    _In_ DWORD Length)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = !Length || (WriteFile(File, Data, Length, &Written, NULL) && Written == Length);
    ok(Result, "WriteFile(%S) failed with %lu\n", Path, GetLastError());
    CloseHandle(File);
    return Result;
}

static
DWORD
ReadTestFile(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(Size) void *Data,
    _In_ DWORD Size)
{
    HANDLE File;
    DWORD Read = 0;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return MAXDWORD;
    ok(ReadFile(File, Data, Size, &Read, NULL), "ReadFile(%S) failed with %lu\n", Path, GetLastError());
    CloseHandle(File);
    return Read;
}

static
BOOL
Capture(
    _In_ HANDLE File,
    _In_ BOOL ProcessSecurity,
    _In_ DWORD ChunkSize,
    _Out_writes_bytes_to_(CAPTURE_SIZE, *Length) PUCHAR Output,
    _Out_ PULONG Length)
{
    LPVOID Context = NULL;
    DWORD Read;
    BOOL Result = TRUE;

    *Length = 0;
    while (*Length + ChunkSize <= CAPTURE_SIZE)
    {
        Read = 0xdeadbeef;
        Result = BackupRead(File, Output + *Length, ChunkSize, &Read, FALSE, ProcessSecurity, &Context);
        ok(Result, "BackupRead failed with %lu after %lu bytes\n", GetLastError(), *Length);
        if (!Result)
            break;
        ok(Read <= ChunkSize, "BackupRead returned %lu bytes for a %lu-byte buffer\n", Read, ChunkSize);
        if (Read == 0 || Read > ChunkSize)
            break;
        *Length += Read;
    }
    ok(BackupRead(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupRead abort failed with %lu\n", GetLastError());
    return Result;
}

static
BOOL
Parse(
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _Out_writes_(MAX_STREAMS) PPARSED_STREAM Streams,
    _Out_ PULONG Count)
{
    ULONG Offset = 0;
    LARGE_INTEGER Size;

    *Count = 0;
    while (Offset < Length)
    {
        PPARSED_STREAM Stream = &Streams[*Count];

        if (*Count == MAX_STREAMS || Length - Offset < HEADER_SIZE)
            return FALSE;
        memcpy(&Stream->Id, Data + Offset, sizeof(DWORD));
        memcpy(&Stream->Attributes, Data + Offset + 4, sizeof(DWORD));
        memcpy(&Size, Data + Offset + 8, sizeof(Size));
        memcpy(&Stream->NameSize, Data + Offset + 16, sizeof(DWORD));
        Stream->Size = Size.QuadPart;
        Offset += HEADER_SIZE;
        if (Stream->NameSize > Length - Offset || Stream->NameSize >= sizeof(Stream->Name))
            return FALSE;
        memset(Stream->Name, 0, sizeof(Stream->Name));
        memcpy(Stream->Name, Data + Offset, Stream->NameSize);
        Offset += Stream->NameSize;
        if (Stream->Size < 0 || Stream->Size > Length - Offset)
            return FALSE;
        Stream->Data = Data + Offset;
        Offset += (ULONG)Stream->Size;
        (*Count)++;
    }
    return TRUE;
}

static
PPARSED_STREAM
FindStream(
    _In_reads_(Count) PPARSED_STREAM Streams,
    _In_ ULONG Count,
    _In_ DWORD Id)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Streams[Index].Id == Id)
            return &Streams[Index];
    }
    return NULL;
}

static
BOOL
SetEa(
    _In_ HANDLE File)
{
    UCHAR Buffer[64];
    PFILE_FULL_EA_INFORMATION Ea = (PFILE_FULL_EA_INFORMATION)Buffer;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    memset(Buffer, 0, sizeof(Buffer));
    Ea->NextEntryOffset = 0;
    Ea->Flags = 0;
    Ea->EaNameLength = 7;
    Ea->EaValueLength = 5;
    memcpy(Ea->EaName, "P04TEST", 8);
    memcpy(Ea->EaName + 8, "value", 5);
    Status = NtSetEaFile(File, &IoStatusBlock, Ea, FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 8 + 5);
    ok(Status == STATUS_SUCCESS, "NtSetEaFile failed with 0x%lx\n", Status);
    return NT_SUCCESS(Status);
}

static
VOID
CheckEaList(
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _In_ PCSTR Where)
{
    PFILE_FULL_EA_INFORMATION Ea = (PFILE_FULL_EA_INFORMATION)Data;
    BOOLEAN Found = FALSE;

    while (Length >= FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName))
    {
        if (Ea->EaNameLength == 7 && !_strnicmp(Ea->EaName, "P04TEST", 7))
        {
            Found = TRUE;
            ok(Ea->EaValueLength == 5 && !memcmp(Ea->EaName + 8, "value", 5),
               "%s: EA value length %u\n", Where, Ea->EaValueLength);
        }
        if (!Ea->NextEntryOffset || Ea->NextEntryOffset > Length)
            break;
        Length -= Ea->NextEntryOffset;
        Ea = (PFILE_FULL_EA_INFORMATION)((PUCHAR)Ea + Ea->NextEntryOffset);
    }
    ok(Found, "%s: EA P04TEST not found\n", Where);
}

static
BOOL
SetExplicitDacl(
    _In_ HANDLE File)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    SECURITY_DESCRIPTOR Descriptor;
    UCHAR AclBuffer[256];
    PACL Acl = (PACL)AclBuffer;
    PSID SystemSid = NULL;
    PSID AdminsSid = NULL;
    NTSTATUS Status;

    Status = RtlAllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &SystemSid);
    if (NT_SUCCESS(Status))
        Status = RtlAllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                             0, 0, 0, 0, 0, 0, &AdminsSid);
    if (NT_SUCCESS(Status))
        Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, SystemSid);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, AdminsSid);
    if (NT_SUCCESS(Status))
        Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);
    if (NT_SUCCESS(Status))
        Status = NtSetSecurityObject(File, DACL_SECURITY_INFORMATION, &Descriptor);
    ok(Status == STATUS_SUCCESS, "Setting the test DACL failed with 0x%lx\n", Status);

    if (SystemSid)
        RtlFreeSid(SystemSid);
    if (AdminsSid)
        RtlFreeSid(AdminsSid);
    return NT_SUCCESS(Status);
}

static
ULONG
QueryDacl(
    _In_ HANDLE File,
    _Out_writes_bytes_(Size) PUCHAR Output,
    _In_ ULONG Size)
{
    UCHAR Buffer[1024];
    BOOLEAN Present;
    BOOLEAN Defaulted;
    PACL Dacl = NULL;
    ULONG Needed;
    NTSTATUS Status;

    Status = NtQuerySecurityObject(File, DACL_SECURITY_INFORMATION, Buffer, sizeof(Buffer), &Needed);
    ok(Status == STATUS_SUCCESS, "NtQuerySecurityObject failed with 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return 0;
    Status = RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)Buffer, &Present, &Dacl, &Defaulted);
    if (!NT_SUCCESS(Status) || !Present || !Dacl || Dacl->AclSize > Size)
        return 0;
    memcpy(Output, Dacl, Dacl->AclSize);
    return Dacl->AclSize;
}

static
VOID
TestReadStreams(
    _In_ PUCHAR Buffer,
    _In_ PUCHAR Buffer2)
{
    static const char Data[] = "backup data";
    static const char StreamData[] = "stream";
    PARSED_STREAM Streams[MAX_STREAMS];
    PPARSED_STREAM Stream;
    WCHAR Path[MAX_PATH];
    WCHAR StreamPath[MAX_PATH];
    HANDLE File;
    ULONG Length;
    ULONG Length2;
    ULONG Count;

    MakePath(Path, L"read.bin");
    StringCchPrintfW(StreamPath, RTL_NUMBER_OF(StreamPath), L"%s:ads", Path);
    if (!WriteTestFile(Path, Data, sizeof(Data) - 1) || !WriteTestFile(StreamPath, StreamData, sizeof(StreamData) - 1))
        return;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    if (Capture(File, FALSE, 0x1000, Buffer, &Length))
    {
        ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
        ok(Count == 2, "BackupRead returned %lu streams, expected 2\n", Count);

        Stream = FindStream(Streams, Count, BACKUP_DATA);
        ok(Stream != NULL, "No BACKUP_DATA stream\n");
        if (Stream)
        {
            ok(Stream->Attributes == STREAM_NORMAL_ATTRIBUTE, "BACKUP_DATA attributes 0x%lx\n", Stream->Attributes);
            ok(Stream->NameSize == 0, "BACKUP_DATA name size %lu\n", Stream->NameSize);
            ok(Stream->Size == sizeof(Data) - 1, "BACKUP_DATA size %I64d\n", Stream->Size);
            ok(Stream->Size == sizeof(Data) - 1 && !memcmp(Stream->Data, Data, sizeof(Data) - 1), "BACKUP_DATA content differs\n");
        }

        Stream = FindStream(Streams, Count, BACKUP_ALTERNATE_DATA);
        ok(Stream != NULL, "No BACKUP_ALTERNATE_DATA stream\n");
        if (Stream)
        {
            ok(Stream->Attributes == STREAM_NORMAL_ATTRIBUTE, "BACKUP_ALTERNATE_DATA attributes 0x%lx\n", Stream->Attributes);
            ok(Stream->NameSize == wcslen(L":ads:$DATA") * sizeof(WCHAR), "BACKUP_ALTERNATE_DATA name size %lu\n", Stream->NameSize);
            ok(!wcscmp(Stream->Name, L":ads:$DATA"), "BACKUP_ALTERNATE_DATA name %S\n", Stream->Name);
            ok(Stream->Size == sizeof(StreamData) - 1, "BACKUP_ALTERNATE_DATA size %I64d\n", Stream->Size);
            ok(Stream->Size == sizeof(StreamData) - 1 && !memcmp(Stream->Data, StreamData, sizeof(StreamData) - 1),
               "BACKUP_ALTERNATE_DATA content differs\n");
        }
        ok(FindStream(Streams, Count, BACKUP_SECURITY_DATA) == NULL, "BACKUP_SECURITY_DATA without bProcessSecurity\n");

        if (Capture(File, FALSE, sizeof(WIN32_STREAM_ID) + 1, Buffer2, &Length2))
        {
            ok(Length2 == Length, "Small reads returned %lu bytes, expected %lu\n", Length2, Length);
            ok(Length2 == Length && !memcmp(Buffer, Buffer2, Length), "Small reads returned different bytes\n");
        }
    }

    if (Capture(File, TRUE, 0x1000, Buffer, &Length))
    {
        ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
        ok(Count == 3, "BackupRead with security returned %lu streams, expected 3\n", Count);
        Stream = FindStream(Streams, Count, BACKUP_SECURITY_DATA);
        ok(Stream != NULL, "No BACKUP_SECURITY_DATA stream\n");
        if (Stream)
        {
            ok(Stream->Attributes == STREAM_CONTAINS_SECURITY, "BACKUP_SECURITY_DATA attributes 0x%lx\n", Stream->Attributes);
            ok(Stream->NameSize == 0, "BACKUP_SECURITY_DATA name size %lu\n", Stream->NameSize);
            ok(RtlValidRelativeSecurityDescriptor(Stream->Data, (ULONG)Stream->Size, 0),
               "BACKUP_SECURITY_DATA is not a valid self-relative security descriptor\n");
            ok(RtlLengthSecurityDescriptor(Stream->Data) == Stream->Size, "Security descriptor length %lu, stream size %I64d\n",
               RtlLengthSecurityDescriptor(Stream->Data), Stream->Size);
        }
    }

    CloseHandle(File);
    DeleteFileW(Path);
}

static
VOID
TestEmptyAndDirectory(
    _In_ PUCHAR Buffer)
{
    PARSED_STREAM Streams[MAX_STREAMS];
    WCHAR Path[MAX_PATH];
    HANDLE File;
    ULONG Length;
    ULONG Count;

    MakePath(Path, L"empty.bin");
    if (WriteTestFile(Path, NULL, 0))
    {
        File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
        if (File != INVALID_HANDLE_VALUE)
        {
            if (Capture(File, FALSE, 0x1000, Buffer, &Length))
                ok(Length == 0, "BackupRead of an empty file returned %lu bytes\n", Length);
            if (Capture(File, TRUE, 0x1000, Buffer, &Length))
            {
                ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
                ok(Count == 1 && Streams[0].Id == BACKUP_SECURITY_DATA,
                   "Empty file with security: %lu streams, first id %lu\n", Count, Count ? Streams[0].Id : 0);
            }
            CloseHandle(File);
        }
        DeleteFileW(Path);
    }

    File = CreateFileW(TestDirectory, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening the test directory failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        if (Capture(File, FALSE, 0x1000, Buffer, &Length))
            ok(Length == 0, "BackupRead of a directory returned %lu bytes\n", Length);
        if (Capture(File, TRUE, 0x1000, Buffer, &Length))
        {
            ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
            ok(Count == 1 && Streams[0].Id == BACKUP_SECURITY_DATA,
               "Directory with security: %lu streams, first id %lu\n", Count, Count ? Streams[0].Id : 0);
        }
        CloseHandle(File);
    }
}

static
BOOL
ReadHeader(
    _In_ HANDLE File,
    _Inout_ LPVOID *Context,
    _Out_ PPARSED_STREAM Stream)
{
    UCHAR Header[HEADER_SIZE + MAX_PATH * sizeof(WCHAR)];
    LARGE_INTEGER Size;
    DWORD Read;
    BOOL Result;

    Result = BackupRead(File, Header, HEADER_SIZE, &Read, FALSE, FALSE, Context);
    ok(Result && Read == HEADER_SIZE, "Reading a stream header returned %d, %lu bytes, error %lu\n", Result, Read, GetLastError());
    if (!Result || Read != HEADER_SIZE)
        return FALSE;
    memcpy(&Stream->Id, Header, sizeof(DWORD));
    memcpy(&Size, Header + 8, sizeof(Size));
    memcpy(&Stream->NameSize, Header + 16, sizeof(DWORD));
    Stream->Size = Size.QuadPart;
    if (Stream->NameSize == 0)
        return TRUE;
    if (Stream->NameSize > sizeof(Header) - HEADER_SIZE)
        return FALSE;
    Result = BackupRead(File, Header + HEADER_SIZE, Stream->NameSize, &Read, FALSE, FALSE, Context);
    ok(Result && Read == Stream->NameSize, "Reading a stream name returned %d, %lu bytes\n", Result, Read);
    return Result && Read == Stream->NameSize;
}

static
VOID
TestSeek(VOID)
{
    static const char Data[] = "seek data";
    static const char StreamData[] = "seek stream";
    PARSED_STREAM Stream;
    WCHAR Path[MAX_PATH];
    WCHAR StreamPath[MAX_PATH];
    LPVOID Context = NULL;
    const char *Expected;
    UCHAR Tail[2];
    HANDLE File;
    DWORD Low;
    DWORD High;
    DWORD Read;
    BOOL Result;

    MakePath(Path, L"seek.bin");
    StringCchPrintfW(StreamPath, RTL_NUMBER_OF(StreamPath), L"%s:ads", Path);
    if (!WriteTestFile(Path, Data, sizeof(Data) - 1) || !WriteTestFile(StreamPath, StreamData, sizeof(StreamData) - 1))
        return;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    if (ReadHeader(File, &Context, &Stream))
    {
        Expected = Stream.Id == BACKUP_DATA ? Data : StreamData;
        Low = High = 0xdeadbeef;
        Result = BackupSeek(File, (DWORD)Stream.Size - 2, 0, &Low, &High, &Context);
        ok(Result, "BackupSeek inside a stream failed with %lu\n", GetLastError());
        ok(Low == Stream.Size - 2 && High == 0, "BackupSeek inside a stream seeked %lu:%lu\n", High, Low);

        Result = BackupRead(File, Tail, sizeof(Tail), &Read, FALSE, FALSE, &Context);
        ok(Result && Read == sizeof(Tail), "Reading after BackupSeek returned %d, %lu bytes\n", Result, Read);
        ok(Result && !memcmp(Tail, Expected + Stream.Size - 2, sizeof(Tail)), "Wrong bytes after BackupSeek\n");

        Low = High = 0xdeadbeef;
        Result = BackupSeek(File, 5, 0, &Low, &High, &Context);
        ok(!Result, "BackupSeek across a stream header succeeded\n");
        ok(Low == 0 && High == 0, "BackupSeek across a stream header seeked %lu:%lu\n", High, Low);

        if (ReadHeader(File, &Context, &Stream))
        {
            ok(Stream.Id == BACKUP_DATA || Stream.Id == BACKUP_ALTERNATE_DATA, "Second stream id %lu\n", Stream.Id);
            Low = High = 0xdeadbeef;
            Result = BackupSeek(File, (DWORD)Stream.Size + 10, 0, &Low, &High, &Context);
            ok(!Result, "BackupSeek past the end of a stream succeeded\n");
            ok(Low == Stream.Size && High == 0, "BackupSeek past the end seeked %lu:%lu, expected %I64d\n", High, Low, Stream.Size);

            Read = 0xdeadbeef;
            Result = BackupRead(File, Tail, sizeof(Tail), &Read, FALSE, FALSE, &Context);
            ok(Result && Read == 0, "BackupRead after the last stream returned %d, %lu bytes\n", Result, Read);
        }
    }
    ok(BackupRead(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupRead abort failed with %lu\n", GetLastError());

    CloseHandle(File);
    DeleteFileW(Path);
}

static
BOOL
RestoreWithAccess(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _In_ DWORD ChunkSize,
    _In_ BOOL ProcessSecurity,
    _In_ ACCESS_MASK Access)
{
    LPVOID Context = NULL;
    HANDLE File;
    ULONG Offset = 0;
    DWORD Written;
    DWORD Chunk;
    BOOL Result = TRUE;

    File = CreateFileW(Path, Access, 0, NULL, CREATE_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    while (Offset < Length && Result)
    {
        Chunk = min(ChunkSize, Length - Offset);
        Written = 0xdeadbeef;
        Result = BackupWrite(File, Data + Offset, Chunk, &Written, FALSE, ProcessSecurity, &Context);
        ok(Result, "BackupWrite failed with %lu at offset %lu\n", GetLastError(), Offset);
        ok(!Result || Written == Chunk, "BackupWrite wrote %lu bytes of %lu\n", Written, Chunk);
        Offset += Chunk;
    }
    ok(BackupWrite(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupWrite abort failed with %lu\n", GetLastError());
    CloseHandle(File);
    return Result;
}

static
BOOL
Restore(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _In_ DWORD ChunkSize,
    _In_ BOOL ProcessSecurity)
{
    return RestoreWithAccess(Path, Data, Length, ChunkSize, ProcessSecurity,
                             GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER);
}

static
ULONG
AppendStream(
    _Out_writes_bytes_(HEADER_SIZE + NameSize + Size) PUCHAR Buffer,
    _In_ DWORD Id,
    _In_reads_bytes_opt_(NameSize) PCWSTR Name,
    _In_ DWORD NameSize,
    _In_reads_bytes_opt_(Size) const void *Data,
    _In_ DWORD Size)
{
    DWORD Attributes = 0;
    LARGE_INTEGER Size64;

    Size64.QuadPart = Size;
    memcpy(Buffer, &Id, sizeof(DWORD));
    memcpy(Buffer + 4, &Attributes, sizeof(DWORD));
    memcpy(Buffer + 8, &Size64, sizeof(Size64));
    memcpy(Buffer + 16, &NameSize, sizeof(DWORD));
    if (NameSize)
        memcpy(Buffer + HEADER_SIZE, Name, NameSize);
    if (Size)
        memcpy(Buffer + HEADER_SIZE + NameSize, Data, Size);
    return HEADER_SIZE + NameSize + Size;
}

static
BOOL
WriteOnce(
    _In_ HANDLE File,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _Out_ PDWORD Error)
{
    LPVOID Context = NULL;
    DWORD Written = 0xdeadbeef;
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = BackupWrite(File, Data, Length, &Written, FALSE, FALSE, &Context);
    *Error = GetLastError();
    ok(!Result || Written == Length, "BackupWrite wrote %lu bytes of %lu\n", Written, Length);
    ok(BackupWrite(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupWrite abort failed with %lu\n", GetLastError());
    return Result;
}

static
VOID
TestLinkStream(
    _In_ PUCHAR Buffer)
{
    static const char Data[] = "link data";
    BY_HANDLE_FILE_INFORMATION Info;
    BY_HANDLE_FILE_INFORMATION LinkInfo;
    PARSED_STREAM Streams[MAX_STREAMS];
    WCHAR Path[MAX_PATH];
    WCHAR Link[MAX_PATH];
    WCHAR Link2[MAX_PATH];
    UCHAR Check[64];
    HANDLE File;
    HANDLE Other;
    ULONG Length;
    ULONG Count;
    DWORD Error;
    DWORD Read;

    MakePath(Path, L"linksrc.bin");
    MakePath(Link, L"linkdst.bin");
    MakePath(Link2, L"linkdst2.bin");
    if (!WriteTestFile(Path, Data, sizeof(Data) - 1))
        return;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Length = AppendStream(Buffer, BACKUP_LINK, NULL, 0, Link, (DWORD)(wcslen(Link) * sizeof(WCHAR)));
    ok(WriteOnce(File, Buffer, Length, &Error), "BACKUP_LINK restore failed with %lu\n", Error);
    ok(GetFileInformationByHandle(File, &Info), "GetFileInformationByHandle failed with %lu\n", GetLastError());
    ok(Info.nNumberOfLinks == 2, "%lu links after a BACKUP_LINK restore, expected 2\n", Info.nNumberOfLinks);

    Other = CreateFileW(Link, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, 0, NULL);
    ok(Other != INVALID_HANDLE_VALUE, "The BACKUP_LINK name %S cannot be opened: %lu\n", Link, GetLastError());
    if (Other != INVALID_HANDLE_VALUE)
    {
        ok(GetFileInformationByHandle(Other, &LinkInfo), "GetFileInformationByHandle failed with %lu\n", GetLastError());
        ok(LinkInfo.nFileIndexLow == Info.nFileIndexLow && LinkInfo.nFileIndexHigh == Info.nFileIndexHigh,
           "The BACKUP_LINK name is another file\n");
        CloseHandle(Other);
    }
    Read = ReadTestFile(Link, Check, sizeof(Check));
    ok(Read == sizeof(Data) - 1 && !memcmp(Check, Data, Read), "Data through the restored link differs (%lu bytes)\n", Read);

    Length = AppendStream(Buffer, BACKUP_LINK, NULL, 0, Link2, (DWORD)((wcslen(Link2) + 1) * sizeof(WCHAR)));
    ok(WriteOnce(File, Buffer, Length, &Error), "BACKUP_LINK restore of a null-terminated path failed with %lu\n", Error);
    ok(GetFileAttributesW(Link2) != INVALID_FILE_ATTRIBUTES, "Null-terminated BACKUP_LINK name not created\n");

    Length = AppendStream(Buffer, BACKUP_LINK, NULL, 0, Link, (DWORD)(wcslen(Link) * sizeof(WCHAR)));
    ok(!WriteOnce(File, Buffer, Length, &Error), "BACKUP_LINK onto an existing name succeeded\n");
    ok(Error == ERROR_ALREADY_EXISTS, "BACKUP_LINK onto an existing name failed with %lu\n", Error);

    Length = AppendStream(Buffer, BACKUP_LINK, NULL, 0, NULL, 0);
    ok(!WriteOnce(File, Buffer, Length, &Error), "BACKUP_LINK without a name succeeded\n");

    if (Capture(File, FALSE, 0x1000, Buffer, &Length))
    {
        ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
        ok(FindStream(Streams, Count, BACKUP_LINK) == NULL, "BackupRead returned a BACKUP_LINK stream\n");
        ok(Count == 1, "BackupRead of a linked file returned %lu streams, expected 1\n", Count);
    }

    CloseHandle(File);
    DeleteFileW(Link2);
    DeleteFileW(Link);
    DeleteFileW(Path);
}

static
VOID
TestStreamTypes(
    _In_ PUCHAR Buffer)
{
    static const char Data[] = "types";
    static const UCHAR Payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    WCHAR Path[MAX_PATH];
    UCHAR Check[64];
    HANDLE File;
    ULONG Length;
    DWORD Error;
    DWORD Read;

    MakePath(Path, L"types.bin");
    if (!WriteTestFile(Path, Data, sizeof(Data) - 1))
        return;
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Length = AppendStream(Buffer, BACKUP_TXFS_DATA, NULL, 0, Payload, sizeof(Payload));
    ok(WriteOnce(File, Buffer, Length, &Error), "BACKUP_TXFS_DATA was not ignored: error %lu\n", Error);

    Length = AppendStream(Buffer, BACKUP_PROPERTY_DATA, NULL, 0, Payload, sizeof(Payload));
    ok(!WriteOnce(File, Buffer, Length, &Error), "BACKUP_PROPERTY_DATA was accepted\n");

    Length = AppendStream(Buffer, 0, NULL, 0, Payload, sizeof(Payload));
    ok(!WriteOnce(File, Buffer, Length, &Error), "Stream type 0 was accepted\n");

    Length = AppendStream(Buffer, 0x20, NULL, 0, Payload, sizeof(Payload));
    ok(!WriteOnce(File, Buffer, Length, &Error), "Stream type 0x20 was accepted\n");

    CloseHandle(File);
    Read = ReadTestFile(Path, Check, sizeof(Check));
    ok(Read == sizeof(Data) - 1 && !memcmp(Check, Data, Read), "Ignored and refused streams changed the data (%lu bytes)\n", Read);
    DeleteFileW(Path);
}

static
VOID
TestSeekRestore(
    _In_ PUCHAR Buffer)
{
    PARSED_STREAM Streams[MAX_STREAMS];
    PPARSED_STREAM Stream;
    IO_STATUS_BLOCK IoStatusBlock;
    UCHAR EaBuffer[512];
    WCHAR Path[MAX_PATH];
    WCHAR Copy[MAX_PATH];
    WCHAR Copy2[MAX_PATH];
    LPVOID Context = NULL;
    HANDLE File;
    ULONG Length;
    ULONG Count;
    ULONG EaLength;
    DWORD Written;
    DWORD Low;
    DWORD High;
    NTSTATUS Status;

    MakePath(Path, L"seekea.bin");
    MakePath(Copy, L"seekea1.bin");
    MakePath(Copy2, L"seekea2.bin");
    if (!WriteTestFile(Path, "ea", 2))
        return;
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    SetEa(File);
    if (!Capture(File, FALSE, 0x1000, Buffer, &Length))
    {
        CloseHandle(File);
        return;
    }
    CloseHandle(File);

    ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
    Stream = FindStream(Streams, Count, BACKUP_EA_DATA);
    ok(Stream != NULL, "No BACKUP_EA_DATA stream\n");
    if (Stream && Stream->Size < sizeof(EaBuffer))
    {
        EaLength = (ULONG)Stream->Size;
        Length = AppendStream(EaBuffer, BACKUP_EA_DATA, NULL, 0, Stream->Data, EaLength);
        if (Restore(Copy, EaBuffer, Length, Length, FALSE))
        {
            File = CreateFileW(Copy2, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Copy2, GetLastError());
            if (File != INVALID_HANDLE_VALUE)
            {
                ok(BackupWrite(File, EaBuffer, HEADER_SIZE, &Written, FALSE, FALSE, &Context) && Written == HEADER_SIZE,
                   "Writing the EA stream header failed with %lu\n", GetLastError());
                Low = High = 0xdeadbeef;
                ok(BackupSeek(File, EaLength, 0, &Low, &High, &Context), "BackupSeek over the EA data failed with %lu\n",
                   GetLastError());
                ok(Low == EaLength && High == 0, "BackupSeek over the EA data seeked %lu:%lu\n", High, Low);
                ok(BackupWrite(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupWrite abort failed with %lu\n",
                   GetLastError());
                Status = NtQueryEaFile(File, &IoStatusBlock, EaBuffer, sizeof(EaBuffer), FALSE, NULL, 0, NULL, TRUE);
                ok(Status == STATUS_NO_EAS_ON_FILE, "A seeked-over EA stream was applied: 0x%lx\n", Status);
                CloseHandle(File);
            }

            memset(EaBuffer, 0, sizeof(EaBuffer));
            Length = AppendStream(EaBuffer, BACKUP_EA_DATA, NULL, 0, Stream->Data, EaLength + 8);
            File = CreateFileW(Copy2, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Copy2, GetLastError());
            if (File != INVALID_HANDLE_VALUE)
            {
                Context = NULL;
                ok(BackupWrite(File, EaBuffer, HEADER_SIZE + EaLength, &Written, FALSE, FALSE, &Context) &&
                   Written == HEADER_SIZE + EaLength, "Writing the EA stream failed with %lu\n", GetLastError());
                Low = High = 0xdeadbeef;
                ok(BackupSeek(File, 8, 0, &Low, &High, &Context), "BackupSeek over the EA padding failed with %lu\n",
                   GetLastError());
                ok(Low == 8 && High == 0, "BackupSeek over the EA padding seeked %lu:%lu\n", High, Low);
                ok(BackupWrite(File, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupWrite abort failed with %lu\n",
                   GetLastError());
                Status = NtQueryEaFile(File, &IoStatusBlock, EaBuffer, sizeof(EaBuffer), FALSE, NULL, 0, NULL, TRUE);
                ok(Status == STATUS_NO_EAS_ON_FILE, "A partly seeked-over EA stream was applied: 0x%lx\n", Status);
                CloseHandle(File);
            }
        }
    }

    DeleteFileW(Copy2);
    DeleteFileW(Copy);
    DeleteFileW(Path);
}

static
BOOL
SetLowLabel(
    _In_ HANDLE File)
{
    SID_IDENTIFIER_AUTHORITY LabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    SECURITY_DESCRIPTOR Descriptor;
    UCHAR AclBuffer[64];
    UCHAR AceBuffer[32];
    PSYSTEM_MANDATORY_LABEL_ACE Ace = (PSYSTEM_MANDATORY_LABEL_ACE)AceBuffer;
    PACL Acl = (PACL)AclBuffer;
    PSID LowSid = NULL;
    NTSTATUS Status;

    Status = RtlAllocateAndInitializeSid(&LabelAuthority, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &LowSid);
    if (NT_SUCCESS(Status))
    {
        Ace->Header.AceType = SYSTEM_MANDATORY_LABEL_ACE_TYPE;
        Ace->Header.AceFlags = 0;
        Ace->Header.AceSize = (USHORT)(FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) + RtlLengthSid(LowSid));
        Ace->Mask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP;
        Status = RtlCopySid(RtlLengthSid(LowSid), &Ace->SidStart, LowSid);
    }
    if (NT_SUCCESS(Status))
        Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlAddAce(Acl, ACL_REVISION, MAXULONG, Ace, Ace->Header.AceSize);
    if (NT_SUCCESS(Status))
        Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlSetSaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);
    if (NT_SUCCESS(Status))
        Status = NtSetSecurityObject(File, LABEL_SECURITY_INFORMATION, &Descriptor);
    ok(Status == STATUS_SUCCESS, "Setting the low integrity label failed with 0x%lx\n", Status);
    if (LowSid)
        RtlFreeSid(LowSid);
    return NT_SUCCESS(Status);
}

static
BOOL
DescriptorHasLowLabel(
    _In_ PSECURITY_DESCRIPTOR Descriptor)
{
    SID_IDENTIFIER_AUTHORITY LabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    PSYSTEM_MANDATORY_LABEL_ACE Ace;
    BOOLEAN Present = FALSE;
    BOOLEAN Defaulted;
    PACL Sacl = NULL;
    PSID LowSid = NULL;
    BOOL Found = FALSE;
    ULONG Index;

    if (!NT_SUCCESS(RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Sacl, &Defaulted)) || !Present || !Sacl)
        return FALSE;
    if (!NT_SUCCESS(RtlAllocateAndInitializeSid(&LabelAuthority, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &LowSid)))
        return FALSE;
    for (Index = 0; Index < Sacl->AceCount; Index++)
    {
        if (!NT_SUCCESS(RtlGetAce(Sacl, Index, (PVOID *)&Ace)))
            break;
        if (Ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE &&
            Ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP &&
            RtlEqualSid((PSID)&Ace->SidStart, LowSid))
        {
            Found = TRUE;
        }
    }
    RtlFreeSid(LowSid);
    return Found;
}

static
BOOL
FileHasLowLabel(
    _In_ PCWSTR Path)
{
    UCHAR Buffer[512];
    HANDLE File;
    ULONG Needed;
    NTSTATUS Status;

    File = CreateFileW(Path, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Status = NtQuerySecurityObject(File, LABEL_SECURITY_INFORMATION, Buffer, sizeof(Buffer), &Needed);
    ok(Status == STATUS_SUCCESS, "Querying the label of %S failed with 0x%lx\n", Path, Status);
    CloseHandle(File);
    return NT_SUCCESS(Status) && DescriptorHasLowLabel((PSECURITY_DESCRIPTOR)Buffer);
}

static
VOID
TestLabel(
    _In_ PUCHAR Buffer)
{
    PARSED_STREAM Streams[MAX_STREAMS];
    PPARSED_STREAM Stream;
    WCHAR Path[MAX_PATH];
    WCHAR Copy[MAX_PATH];
    WCHAR Copy2[MAX_PATH];
    HANDLE File;
    ULONG Length;
    ULONG Count;
    BOOL Labelled;

    MakePath(Path, L"label.bin");
    MakePath(Copy, L"label1.bin");
    MakePath(Copy2, L"label2.bin");
    if (!WriteTestFile(Path, "label", 5))
        return;
    File = CreateFileW(Path, READ_CONTROL | WRITE_OWNER, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Labelled = SetLowLabel(File);
    CloseHandle(File);
    if (!Labelled)
    {
        DeleteFileW(Path);
        return;
    }
    ok(FileHasLowLabel(Path), "The low label did not stick\n");

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    if (Capture(File, TRUE, 0x1000, Buffer, &Length))
    {
        ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
        Stream = FindStream(Streams, Count, BACKUP_SECURITY_DATA);
        ok(Stream != NULL, "No BACKUP_SECURITY_DATA stream\n");
        if (Stream)
        {
            ok(RtlValidRelativeSecurityDescriptor(Stream->Data, (ULONG)Stream->Size, 0),
               "BACKUP_SECURITY_DATA is not a valid self-relative security descriptor\n");
            ok(DescriptorHasLowLabel(Stream->Data), "BACKUP_SECURITY_DATA without ACCESS_SYSTEM_SECURITY lacks the label\n");
        }

        if (RestoreWithAccess(Copy, Buffer, Length, Length, TRUE, GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER))
            ok(FileHasLowLabel(Copy), "The label was not restored through a WRITE_OWNER handle\n");
        if (RestoreWithAccess(Copy2, Buffer, Length, Length, TRUE, GENERIC_READ | GENERIC_WRITE | WRITE_DAC))
            ok(!FileHasLowLabel(Copy2), "The label was restored without WRITE_OWNER\n");
    }
    CloseHandle(File);

    DeleteFileW(Copy2);
    DeleteFileW(Copy);
    DeleteFileW(Path);
}

static
NTSTATUS
QueryObjectId(
    _In_ PCWSTR Path,
    _Out_ PFILE_OBJECTID_BUFFER ObjectId)
{
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    NTSTATUS Status;

    File = CreateFileW(Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return STATUS_UNSUCCESSFUL;
    memset(ObjectId, 0, sizeof(*ObjectId));
    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_GET_OBJECT_ID, NULL, 0,
                             ObjectId, sizeof(*ObjectId));
    CloseHandle(File);
    return Status;
}

static
VOID
TestObjectIdStream(
    _In_ PUCHAR Buffer,
    _In_ PUCHAR Buffer2)
{
    FILE_OBJECTID_BUFFER Id;
    FILE_OBJECTID_BUFFER Queried;
    PARSED_STREAM Streams[MAX_STREAMS];
    PPARSED_STREAM Stream;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR Path[MAX_PATH];
    WCHAR Copy[MAX_PATH];
    WCHAR Copy2[MAX_PATH];
    BOOLEAN Old;
    HANDLE File;
    ULONG Length;
    ULONG Length2;
    ULONG Count;
    DWORD Error;
    NTSTATUS Status;

    MakePath(Path, L"objid.bin");
    MakePath(Copy, L"objid1.bin");
    MakePath(Copy2, L"objid2.bin");
    if (!WriteTestFile(Path, "objid", 5))
        return;
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_CREATE_OR_GET_OBJECT_ID, NULL, 0,
                             &Id, sizeof(Id));
    ok(Status == STATUS_SUCCESS, "FSCTL_CREATE_OR_GET_OBJECT_ID failed with 0x%lx\n", Status);
    if (!NT_SUCCESS(Status) || !Capture(File, FALSE, 0x1000, Buffer, &Length))
    {
        CloseHandle(File);
        DeleteFileW(Path);
        return;
    }
    CloseHandle(File);

    ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
    ok(Count == 2, "BackupRead of a file with an object identifier returned %lu streams, expected 2\n", Count);
    Stream = FindStream(Streams, Count, BACKUP_OBJECT_ID);
    ok(Stream != NULL, "No BACKUP_OBJECT_ID stream\n");
    if (Stream)
    {
        ok(Stream->Attributes == STREAM_NORMAL_ATTRIBUTE, "BACKUP_OBJECT_ID attributes 0x%lx\n", Stream->Attributes);
        ok(Stream->NameSize == 0, "BACKUP_OBJECT_ID name size %lu\n", Stream->NameSize);
        ok(Stream->Size == sizeof(Id), "BACKUP_OBJECT_ID size %I64d\n", Stream->Size);
        ok(Stream->Size == sizeof(Id) && !memcmp(Stream->Data, &Id, sizeof(Id)), "BACKUP_OBJECT_ID content differs\n");
    }

    ok(NT_SUCCESS(RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Old)), "Enabling SeRestorePrivilege failed\n");
    if (Restore(Copy, Buffer, Length, Length, FALSE))
    {
        Status = QueryObjectId(Copy, &Queried);
        ok(Status == STATUS_OBJECTID_NOT_FOUND, "An identifier in use was restored: 0x%lx\n", Status);
    }
    ok(DeleteFileW(Path), "DeleteFile(%S) failed with %lu\n", Path, GetLastError());
    if (Restore(Copy2, Buffer, Length, Length, FALSE))
    {
        Status = QueryObjectId(Copy2, &Queried);
        ok(Status == STATUS_SUCCESS, "The object identifier was not restored: 0x%lx\n", Status);
        ok(!memcmp(&Queried, &Id, sizeof(Id)), "The restored object identifier differs\n");
    }
    DeleteFileW(Copy2);

    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, FALSE, FALSE, &Old);
    if (Restore(Copy, Buffer, Length, Length, FALSE))
    {
        Status = QueryObjectId(Copy, &Queried);
        ok(Status == STATUS_OBJECTID_NOT_FOUND, "The identifier was restored without restore access: 0x%lx\n", Status);
    }

    File = CreateFileW(Copy, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Copy, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Length2 = AppendStream(Buffer2, BACKUP_OBJECT_ID, NULL, 0, &Id, 16);
        ok(!WriteOnce(File, Buffer2, Length2, &Error), "A 16-byte BACKUP_OBJECT_ID stream was accepted\n");
        CloseHandle(File);
    }
    DeleteFileW(Copy);
}

static
VOID
TestWrite(
    _In_ PUCHAR Buffer,
    _In_ PUCHAR Buffer2)
{
    static const char Data[] = "restore data";
    static const char StreamData[] = "restore stream";
    PARSED_STREAM Streams[MAX_STREAMS];
    PPARSED_STREAM Stream;
    WCHAR Path[MAX_PATH];
    WCHAR StreamPath[MAX_PATH];
    WCHAR Copy[MAX_PATH];
    WCHAR CopyStream[MAX_PATH];
    UCHAR SourceDacl[512];
    UCHAR CopyDacl[512];
    UCHAR Check[64];
    UCHAR EaBuffer[512];
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    ULONG SourceDaclLength;
    ULONG CopyDaclLength;
    ULONG Length;
    ULONG Count;
    DWORD Read;
    NTSTATUS Status;

    MakePath(Path, L"source.bin");
    StringCchPrintfW(StreamPath, RTL_NUMBER_OF(StreamPath), L"%s:ads", Path);
    MakePath(Copy, L"copy.bin");
    StringCchPrintfW(CopyStream, RTL_NUMBER_OF(CopyStream), L"%s:ads", Copy);
    if (!WriteTestFile(Path, Data, sizeof(Data) - 1) || !WriteTestFile(StreamPath, StreamData, sizeof(StreamData) - 1))
        return;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE | WRITE_DAC, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    SetEa(File);
    SetExplicitDacl(File);
    SourceDaclLength = QueryDacl(File, SourceDacl, sizeof(SourceDacl));
    ok(SourceDaclLength != 0, "Source DACL not readable\n");

    if (!Capture(File, TRUE, 0x1000, Buffer, &Length))
    {
        CloseHandle(File);
        return;
    }
    CloseHandle(File);

    ok(Parse(Buffer, Length, Streams, &Count), "Malformed backup stream of %lu bytes\n", Length);
    ok(Count == 4, "BackupRead returned %lu streams, expected 4\n", Count);
    Stream = FindStream(Streams, Count, BACKUP_EA_DATA);
    ok(Stream != NULL, "No BACKUP_EA_DATA stream\n");
    if (Stream)
    {
        ok(Stream->Attributes == STREAM_NORMAL_ATTRIBUTE, "BACKUP_EA_DATA attributes 0x%lx\n", Stream->Attributes);
        ok(Stream->NameSize == 0, "BACKUP_EA_DATA name size %lu\n", Stream->NameSize);
        CheckEaList(Stream->Data, (ULONG)Stream->Size, "BACKUP_EA_DATA");
    }

    if (Restore(Copy, Buffer, Length, Length, TRUE))
    {
        Read = ReadTestFile(Copy, Check, sizeof(Check));
        ok(Read == sizeof(Data) - 1 && !memcmp(Check, Data, Read), "Restored data differs (%lu bytes)\n", Read);
        Read = ReadTestFile(CopyStream, Check, sizeof(Check));
        ok(Read == sizeof(StreamData) - 1 && !memcmp(Check, StreamData, Read), "Restored stream differs (%lu bytes)\n", Read);

        File = CreateFileW(Copy, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Copy, GetLastError());
        if (File != INVALID_HANDLE_VALUE)
        {
            CopyDaclLength = QueryDacl(File, CopyDacl, sizeof(CopyDacl));
            ok(CopyDaclLength == SourceDaclLength && !memcmp(CopyDacl, SourceDacl, SourceDaclLength),
               "Restored DACL differs (%lu bytes, source %lu)\n", CopyDaclLength, SourceDaclLength);
            Status = NtQueryEaFile(File, &IoStatusBlock, EaBuffer, sizeof(EaBuffer), FALSE, NULL, 0, NULL, TRUE);
            ok(Status == STATUS_SUCCESS, "NtQueryEaFile on the restored file failed with 0x%lx\n", Status);
            if (NT_SUCCESS(Status))
                CheckEaList(EaBuffer, (ULONG)IoStatusBlock.Information, "Restored EA");
            CloseHandle(File);
        }
    }

    if (Restore(Copy, Buffer, Length, sizeof(WIN32_STREAM_ID) + 1, FALSE))
    {
        Read = ReadTestFile(Copy, Check, sizeof(Check));
        ok(Read == sizeof(Data) - 1 && !memcmp(Check, Data, Read), "Restored data differs after small writes (%lu bytes)\n", Read);
        Read = ReadTestFile(CopyStream, Check, sizeof(Check));
        ok(Read == sizeof(StreamData) - 1 && !memcmp(Check, StreamData, Read),
           "Restored stream differs after small writes (%lu bytes)\n", Read);
    }

    UNREFERENCED_PARAMETER(Buffer2);
    DeleteFileW(Copy);
    DeleteFileW(Path);
}

static
VOID
TestSparse(
    _In_ PUCHAR Buffer)
{
    PARSED_STREAM Streams[MAX_STREAMS];
    FILE_ALLOCATED_RANGE_BUFFER Query;
    FILE_ALLOCATED_RANGE_BUFFER Ranges[16];
    LARGE_INTEGER Offset;
    WCHAR Path[MAX_PATH];
    WCHAR Copy[MAX_PATH];
    PUCHAR Image = NULL;
    PUCHAR Expected = NULL;
    PUCHAR Restored = NULL;
    HANDLE File;
    ULONG Length;
    ULONG Count;
    ULONG RangeCount;
    ULONG Index;
    DWORD Returned;
    DWORD Written;
    BOOL Result;

    MakePath(Path, L"sparse.bin");
    MakePath(Copy, L"sparse2.bin");
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Result = DeviceIoControl(File, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &Returned, NULL);
    ok(Result, "FSCTL_SET_SPARSE failed with %lu\n", GetLastError());
    Offset.QuadPart = SPARSE_SIZE;
    Result = Result && SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) && SetEndOfFile(File);
    Offset.QuadPart = 0;
    Result = Result && SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) && WriteFile(File, "x", 1, &Written, NULL);
    Offset.QuadPart = SPARSE_SECOND;
    Result = Result && SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) && WriteFile(File, "y", 1, &Written, NULL);
    ok(Result, "Preparing the sparse file failed with %lu\n", GetLastError());

    Query.FileOffset.QuadPart = 0;
    Query.Length.QuadPart = SPARSE_SIZE;
    Result = Result && DeviceIoControl(File, FSCTL_QUERY_ALLOCATED_RANGES, &Query, sizeof(Query),
                                       Ranges, sizeof(Ranges), &Returned, NULL);
    ok(Result, "FSCTL_QUERY_ALLOCATED_RANGES failed with %lu\n", GetLastError());
    RangeCount = Result ? Returned / sizeof(Ranges[0]) : 0;

    Image = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SPARSE_SIZE);
    Expected = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SPARSE_SIZE);
    Restored = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SPARSE_SIZE);
    if (Result && Image && Expected && Restored && Capture(File, FALSE, 0x1000, Buffer, &Length))
    {
        Expected[0] = 'x';
        Expected[SPARSE_SECOND] = 'y';
        ok(Parse(Buffer, Length, Streams, &Count), "Malformed sparse backup stream of %lu bytes\n", Length);
        ok(Count >= 1 && Streams[0].Id == BACKUP_DATA, "First sparse stream id %lu\n", Count ? Streams[0].Id : 0);
        trace("%lu allocated ranges, %lu streams\n", RangeCount, Count);
        if (RangeCount == 1)
        {
            ok(Count == 1 && Streams[0].Size == SPARSE_SIZE, "One range: %lu streams, data size %I64d\n",
               Count, Count ? Streams[0].Size : 0);
        }
        else if (Count >= 1 && Streams[0].Id == BACKUP_DATA)
        {
            ok(Streams[0].Size == 0, "Sparse data stream size %I64d\n", Streams[0].Size);
            ok(Streams[0].Attributes & STREAM_SPARSE_ATTRIBUTE, "Sparse data stream attributes 0x%lx\n", Streams[0].Attributes);
            ok(Count == RangeCount + 2, "%lu streams for %lu ranges\n", Count, RangeCount);
            for (Index = 1; Index < Count; Index++)
            {
                LARGE_INTEGER BlockOffset;

                ok(Streams[Index].Id == BACKUP_SPARSE_BLOCK, "Stream %lu id %lu\n", Index, Streams[Index].Id);
                ok(Streams[Index].Size >= 8, "Sparse block %lu size %I64d\n", Index, Streams[Index].Size);
                if (Streams[Index].Id != BACKUP_SPARSE_BLOCK || Streams[Index].Size < 8)
                    continue;
                memcpy(&BlockOffset, Streams[Index].Data, sizeof(BlockOffset));
                if (Index + 1 == Count)
                {
                    ok(Streams[Index].Size == 8 && BlockOffset.QuadPart == SPARSE_SIZE,
                       "Final sparse block size %I64d offset %I64d\n", Streams[Index].Size, BlockOffset.QuadPart);
                }
                else if (BlockOffset.QuadPart + Streams[Index].Size - 8 <= SPARSE_SIZE)
                {
                    memcpy(Image + BlockOffset.QuadPart, Streams[Index].Data + 8, (SIZE_T)(Streams[Index].Size - 8));
                }
            }
            ok(!memcmp(Image, Expected, SPARSE_SIZE), "Sparse blocks do not rebuild the file\n");
        }

        CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
        if (Restore(Copy, Buffer, Length, Length, FALSE))
        {
            Returned = ReadTestFile(Copy, Restored, SPARSE_SIZE);
            ok(Returned == SPARSE_SIZE, "Restored sparse file has %lu bytes\n", Returned);
            ok(Returned == SPARSE_SIZE && !memcmp(Restored, Expected, SPARSE_SIZE), "Restored sparse file differs\n");
        }
    }

    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (Image)
        HeapFree(GetProcessHeap(), 0, Image);
    if (Expected)
        HeapFree(GetProcessHeap(), 0, Expected);
    if (Restored)
        HeapFree(GetProcessHeap(), 0, Restored);
    DeleteFileW(Copy);
    DeleteFileW(Path);
}

static
VOID
TestManyStreams(VOID)
{
    WIN32_FIND_STREAM_DATA FindData;
    WCHAR Path[MAX_PATH];
    WCHAR StreamPath[MAX_PATH];
    HANDLE Find;
    HANDLE Stream;
    ULONG Index;
    ULONG Found;

    MakePath(Path, L"many.bin");
    if (!WriteTestFile(Path, "m", 1))
        return;
    for (Index = 0; Index < MANY_STREAMS; Index++)
    {
        StringCchPrintfW(StreamPath, RTL_NUMBER_OF(StreamPath), L"%s:stream_with_a_rather_long_name_%02lu", Path, Index);
        Stream = CreateFileW(StreamPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        ok(Stream != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", StreamPath, GetLastError());
        if (Stream == INVALID_HANDLE_VALUE)
            break;
        CloseHandle(Stream);
    }

    Find = FindFirstStreamW(Path, FindStreamInfoStandard, &FindData, 0);
    ok(Find != INVALID_HANDLE_VALUE, "FindFirstStreamW with %u streams failed with %lu\n", MANY_STREAMS + 1, GetLastError());
    if (Find != INVALID_HANDLE_VALUE)
    {
        Found = 1;
        while (Found <= MANY_STREAMS + 1 && FindNextStreamW(Find, &FindData))
            Found++;
        ok(GetLastError() == ERROR_HANDLE_EOF, "FindNextStreamW ended with %lu\n", GetLastError());
        ok(Found == MANY_STREAMS + 1, "FindFirstStreamW enumerated %lu streams, expected %u\n", Found, MANY_STREAMS + 1);
        FindClose(Find);
    }

    DeleteFileW(Path);
}

START_TEST(BackupReadWrite)
{
    WCHAR TempPath[MAX_PATH];
    LPVOID Context = NULL;
    PUCHAR Buffer;
    PUCHAR Buffer2;
    DWORD Length;

    ok(BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupRead abort without a context failed with %lu\n", GetLastError());
    ok(BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &Context), "BackupWrite abort without a context failed with %lu\n", GetLastError());

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    StringCchPrintfW(TestDirectory, RTL_NUMBER_OF(TestDirectory), L"%sbackup%lx", TempPath, GetCurrentProcessId());
    ok(CreateDirectoryW(TestDirectory, NULL), "CreateDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());

    Buffer = HeapAlloc(GetProcessHeap(), 0, CAPTURE_SIZE);
    Buffer2 = HeapAlloc(GetProcessHeap(), 0, CAPTURE_SIZE);
    if (Buffer && Buffer2)
    {
        TestReadStreams(Buffer, Buffer2);
        TestEmptyAndDirectory(Buffer);
        TestSeek();
        TestWrite(Buffer, Buffer2);
        TestSparse(Buffer);
        TestManyStreams();
        TestLinkStream(Buffer);
        TestStreamTypes(Buffer);
        TestSeekRestore(Buffer);
        TestLabel(Buffer);
        TestObjectIdStream(Buffer, Buffer2);
    }
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
    if (Buffer2)
        HeapFree(GetProcessHeap(), 0, Buffer2);

    ok(RemoveDirectoryW(TestDirectory), "RemoveDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());
}
