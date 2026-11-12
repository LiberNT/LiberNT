/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the rename of NTFS data streams and of a file by a simple name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>

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
WriteStream(
    _In_ PCWSTR Path,
    _In_ PCSTR Data)
{
    HANDLE File;
    DWORD Written = 0;
    DWORD Length = (DWORD)strlen(Data);
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS, NULL);
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
ReadStream(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(Size) PCHAR Data,
    _In_ DWORD Size)
{
    HANDLE File;
    DWORD Read = 0;

    memset(Data, 0, Size);
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return MAXDWORD;
    if (!ReadFile(File, Data, Size - 1, &Read, NULL))
        Read = MAXDWORD;
    CloseHandle(File);
    return Read;
}

static
HANDLE
OpenStream(
    _In_ PCWSTR Path,
    _In_ DWORD Access)
{
    HANDLE File;

    File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    return File;
}

static
NTSTATUS
Rename(
    _In_ HANDLE File,
    _In_ PCWSTR NewName,
    _In_ BOOLEAN Replace,
    _In_ FILE_INFORMATION_CLASS Class)
{
    UCHAR Buffer[sizeof(FILE_RENAME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    PFILE_RENAME_INFORMATION Info = (PFILE_RENAME_INFORMATION)Buffer;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG Length = (ULONG)(wcslen(NewName) * sizeof(WCHAR));

    memset(Buffer, 0, sizeof(Buffer));
    if (Class == FileRenameInformationEx)
        *(PULONG)Info = Replace ? 1 : 0;
    else
        Info->ReplaceIfExists = Replace;
    Info->RootDirectory = NULL;
    Info->FileNameLength = Length;
    memcpy(Info->FileName, NewName, Length);
    return NtSetInformationFile(File, &IoStatusBlock, Info, FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + Length,
                                Class);
}

static
ULONG
ListStreams(
    _In_ PCWSTR Path,
    _Out_writes_(256) PWSTR Names)
{
    WIN32_FIND_STREAM_DATA Data;
    HANDLE Find;
    ULONG Count = 0;

    Names[0] = UNICODE_NULL;
    Find = FindFirstStreamW(Path, FindStreamInfoStandard, &Data, 0);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        StringCchCatW(Names, 256, Data.cStreamName);
        StringCchCatW(Names, 256, L"|");
        Count++;
    } while (Count < 16 && FindNextStreamW(Find, &Data));
    FindClose(Find);
    return Count;
}

static
VOID
TestNamedStreams(VOID)
{
    WCHAR Path[MAX_PATH];
    WCHAR StreamA[MAX_PATH];
    WCHAR StreamB[MAX_PATH];
    WCHAR StreamC[MAX_PATH];
    WCHAR StreamD[MAX_PATH];
    WCHAR Names[256];
    CHAR Data[64];
    HANDLE File;
    HANDLE Other;
    DWORD Read;
    NTSTATUS Status;

    MakePath(Path, L"named.bin");
    StringCchPrintfW(StreamA, MAX_PATH, L"%s:a", Path);
    StringCchPrintfW(StreamB, MAX_PATH, L"%s:b", Path);
    StringCchPrintfW(StreamC, MAX_PATH, L"%s:c", Path);
    StringCchPrintfW(StreamD, MAX_PATH, L"%s:d", Path);
    if (!WriteStream(Path, "main") || !WriteStream(StreamA, "alpha") || !WriteStream(StreamC, "cc"))
        return;

    File = OpenStream(StreamA, DELETE | GENERIC_READ);
    if (File == INVALID_HANDLE_VALUE)
        return;
    Status = Rename(File, L":b", FALSE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Renaming :a to :b returned 0x%lx\n", Status);
    memset(Data, 0, sizeof(Data));
    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ok(ReadFile(File, Data, sizeof(Data) - 1, &Read, NULL) && Read == 5 && !strcmp(Data, "alpha"),
       "The renamed handle reads %lu bytes '%s'\n", Read, Data);
    Read = ReadStream(StreamB, Data, sizeof(Data));
    ok(Read == 5 && !strcmp(Data, "alpha"), "The new stream name reads %lu bytes '%s'\n", Read, Data);
    ok(GetFileAttributesW(StreamA) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "The old stream name still opens (error %lu)\n", GetLastError());
    ok(ListStreams(Path, Names) == 3 && !wcscmp(Names, L"::$DATA|:b:$DATA|:c:$DATA|"), "Streams after the rename: %S\n", Names);

    Status = Rename(File, L":a:$DATA", FALSE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Renaming :b to :a:$DATA returned 0x%lx\n", Status);
    Status = Rename(File, L":A", FALSE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Renaming :a to :A returned 0x%lx\n", Status);
    ok(ListStreams(Path, Names) == 3 && !wcscmp(Names, L"::$DATA|:a:$DATA|:c:$DATA|"),
       "A rename to the same name in another case changed the name: %S\n", Names);

    Status = Rename(File, L":c", FALSE, FileRenameInformation);
    ok(Status == STATUS_OBJECT_NAME_COLLISION, "Renaming onto an existing stream returned 0x%lx\n", Status);
    Status = Rename(File, L":c", TRUE, FileRenameInformation);
    ok(Status == STATUS_INVALID_PARAMETER, "Replacing a stream with data returned 0x%lx\n", Status);
    ok(WriteStream(StreamC, ""), "Emptying :c failed\n");
    Status = Rename(File, L":c", TRUE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Replacing an empty stream returned 0x%lx\n", Status);
    Read = ReadStream(StreamC, Data, sizeof(Data));
    ok(Read == 5 && !strcmp(Data, "alpha"), "The replaced stream reads %lu bytes '%s'\n", Read, Data);
    ok(ListStreams(Path, Names) == 2 && !wcscmp(Names, L"::$DATA|:c:$DATA|"), "Streams after the replacement: %S\n", Names);

    ok(WriteStream(StreamD, ""), "Creating :d failed\n");
    Other = OpenStream(StreamD, GENERIC_READ);
    Status = Rename(File, L":d", TRUE, FileRenameInformationEx);
    ok(Status == STATUS_INVALID_PARAMETER, "Replacing an open stream returned 0x%lx\n", Status);
    if (Other != INVALID_HANDLE_VALUE)
        CloseHandle(Other);
    Status = Rename(File, L":d", TRUE, FileRenameInformationEx);
    ok(Status == STATUS_SUCCESS, "FileRenameInformationEx replacing an empty stream returned 0x%lx\n", Status);

    Status = Rename(File, L":x:", FALSE, FileRenameInformation);
    ok(Status == STATUS_INVALID_PARAMETER, "A name ending in ':' returned 0x%lx\n", Status);
    Status = Rename(File, L":a:b:c:d", FALSE, FileRenameInformation);
    ok(Status == STATUS_INVALID_PARAMETER, "A name with four ':' returned 0x%lx\n", Status);
    Status = Rename(File, L":x\\y", FALSE, FileRenameInformation);
    ok(Status == STATUS_INVALID_PARAMETER, "A name with '\\' returned 0x%lx\n", Status);
    Status = Rename(File, L":x:$INDEX_ALLOCATION", FALSE, FileRenameInformation);
    ok(Status == STATUS_OBJECT_TYPE_MISMATCH, "A data stream renamed to $INDEX_ALLOCATION returned 0x%lx\n", Status);
    CloseHandle(File);

    File = OpenStream(StreamD, GENERIC_READ);
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = Rename(File, L":e", FALSE, FileRenameInformation);
        ok(Status == STATUS_ACCESS_DENIED, "A rename without DELETE access returned 0x%lx\n", Status);
        CloseHandle(File);
    }
    DeleteFileW(Path);
}

static
VOID
TestDefaultStream(VOID)
{
    WCHAR Path[MAX_PATH];
    WCHAR Moved[MAX_PATH];
    CHAR Data[64];
    HANDLE File;
    DWORD Read;
    NTSTATUS Status;

    MakePath(Path, L"default.bin");
    StringCchPrintfW(Moved, MAX_PATH, L"%s:moved", Path);
    if (!WriteStream(Path, "default"))
        return;

    File = OpenStream(Path, DELETE | GENERIC_READ);
    if (File == INVALID_HANDLE_VALUE)
        return;
    Status = Rename(File, L":moved", FALSE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Renaming the default stream returned 0x%lx\n", Status);
    Read = ReadStream(Path, Data, sizeof(Data));
    ok(Read == 0, "The default stream left behind has %lu bytes\n", Read);
    Read = ReadStream(Moved, Data, sizeof(Data));
    ok(Read == 7 && !strcmp(Data, "default"), "The moved stream reads %lu bytes '%s'\n", Read, Data);
    memset(Data, 0, sizeof(Data));
    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ok(ReadFile(File, Data, sizeof(Data) - 1, &Read, NULL) && Read == 7 && !strcmp(Data, "default"),
       "The renaming handle reads %lu bytes '%s'\n", Read, Data);

    Status = Rename(File, L"::$DATA", FALSE, FileRenameInformation);
    ok(Status == STATUS_OBJECT_NAME_COLLISION, "Renaming onto the default stream without replace returned 0x%lx\n", Status);
    Status = Rename(File, L"::$DATA", TRUE, FileRenameInformation);
    ok(Status == STATUS_SUCCESS, "Renaming onto the empty default stream returned 0x%lx\n", Status);
    CloseHandle(File);
    Read = ReadStream(Path, Data, sizeof(Data));
    ok(Read == 7 && !strcmp(Data, "default"), "The default stream reads %lu bytes '%s'\n", Read, Data);
    ok(GetFileAttributesW(Moved) == INVALID_FILE_ATTRIBUTES, "The moved stream is still there\n");
    DeleteFileW(Path);
}

static
VOID
TestDirectoryStreams(VOID)
{
    WCHAR Path[MAX_PATH];
    WCHAR Stream[MAX_PATH];
    WCHAR Stream2[MAX_PATH];
    CHAR Data[64];
    HANDLE File;
    DWORD Read;
    NTSTATUS Status;

    MakePath(Path, L"dir");
    StringCchPrintfW(Stream, MAX_PATH, L"%s:ds", Path);
    StringCchPrintfW(Stream2, MAX_PATH, L"%s:ds2", Path);
    ok(CreateDirectoryW(Path, NULL), "CreateDirectory(%S) failed with %lu\n", Path, GetLastError());
    if (!WriteStream(Stream, "dir stream"))
    {
        RemoveDirectoryW(Path);
        return;
    }

    File = OpenStream(Stream, DELETE | GENERIC_READ);
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = Rename(File, L":ds2", FALSE, FileRenameInformation);
        ok(Status == STATUS_SUCCESS, "Renaming a directory stream returned 0x%lx\n", Status);
        Status = Rename(File, L"::$DATA", TRUE, FileRenameInformation);
        ok(Status == STATUS_INVALID_PARAMETER, "Renaming a directory stream to the default stream returned 0x%lx\n", Status);
        CloseHandle(File);
    }
    Read = ReadStream(Stream2, Data, sizeof(Data));
    ok(Read == 10 && !strcmp(Data, "dir stream"), "The renamed directory stream reads %lu bytes '%s'\n", Read, Data);

    File = OpenStream(Path, DELETE | FILE_READ_ATTRIBUTES);
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = Rename(File, L":x", FALSE, FileRenameInformation);
        ok(Status == STATUS_INVALID_PARAMETER, "Renaming the directory index stream returned 0x%lx\n", Status);
        CloseHandle(File);
    }
    DeleteFileW(Stream2);
    ok(RemoveDirectoryW(Path), "RemoveDirectory(%S) failed with %lu\n", Path, GetLastError());
}

static
VOID
TestWin32AndSimpleName(VOID)
{
    UCHAR Buffer[sizeof(FILE_RENAME_INFO) + MAX_PATH * sizeof(WCHAR)];
    PFILE_RENAME_INFO Info = (PFILE_RENAME_INFO)Buffer;
    WCHAR Path[MAX_PATH];
    WCHAR Stream[MAX_PATH];
    WCHAR Renamed[MAX_PATH];
    WCHAR Simple[MAX_PATH];
    CHAR Data[64];
    HANDLE File;
    DWORD Read;
    NTSTATUS Status;

    MakePath(Path, L"win32.bin");
    MakePath(Simple, L"simple.bin");
    StringCchPrintfW(Stream, MAX_PATH, L"%s:w", Path);
    StringCchPrintfW(Renamed, MAX_PATH, L"%s:k", Path);
    if (!WriteStream(Path, "main") || !WriteStream(Stream, "win32"))
        return;

    File = OpenStream(Stream, DELETE | GENERIC_READ);
    if (File != INVALID_HANDLE_VALUE)
    {
        memset(Buffer, 0, sizeof(Buffer));
        Info->ReplaceIfExists = FALSE;
        Info->RootDirectory = NULL;
        Info->FileNameLength = 2 * sizeof(WCHAR);
        memcpy(Info->FileName, L":k", 3 * sizeof(WCHAR));
        ok(SetFileInformationByHandle(File, FileRenameInfo, Info, sizeof(Buffer)),
           "SetFileInformationByHandle with a stream name failed with %lu\n", GetLastError());
        CloseHandle(File);
    }
    Read = ReadStream(Renamed, Data, sizeof(Data));
    ok(Read == 5 && !strcmp(Data, "win32"), "The stream renamed through kernel32 reads %lu bytes '%s'\n", Read, Data);

    File = OpenStream(Path, DELETE | GENERIC_READ | FILE_WRITE_ATTRIBUTES);
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = Rename(File, L"bad:name", FALSE, FileRenameInformation);
        ok(Status == STATUS_OBJECT_NAME_INVALID, "A rename to a simple name with ':' returned 0x%lx\n", Status);
        Status = Rename(File, L"bad:name", FALSE, FileLinkInformation);
        ok(Status == STATUS_OBJECT_NAME_INVALID, "A link with a simple name with ':' returned 0x%lx\n", Status);
        Status = Rename(File, L"link.bin", FALSE, FileLinkInformation);
        ok(Status == STATUS_SUCCESS, "A link with a simple name returned 0x%lx\n", Status);
        Status = Rename(File, L"simple.bin", FALSE, FileRenameInformation);
        ok(Status == STATUS_SUCCESS, "A rename to a simple name returned 0x%lx\n", Status);
        CloseHandle(File);
    }
    Read = ReadStream(Simple, Data, sizeof(Data));
    ok(Read == 4 && !strcmp(Data, "main"), "The file renamed by a simple name reads %lu bytes '%s'\n", Read, Data);
    ok(GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES, "The old file name is still there\n");
    MakePath(Path, L"link.bin");
    Read = ReadStream(Path, Data, sizeof(Data));
    ok(Read == 4 && !strcmp(Data, "main"), "The link made by a simple name reads %lu bytes '%s'\n", Read, Data);
    DeleteFileW(Simple);
    DeleteFileW(Path);
}

START_TEST(StreamRename)
{
    WCHAR TempPath[MAX_PATH];
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    StringCchPrintfW(TestDirectory, RTL_NUMBER_OF(TestDirectory), L"%ssrename%lx", TempPath, GetCurrentProcessId());
    ok(CreateDirectoryW(TestDirectory, NULL), "CreateDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());

    TestNamedStreams();
    TestDefaultStream();
    TestDirectoryStreams();
    TestWin32AndSimpleName();

    ok(RemoveDirectoryW(TestDirectory), "RemoveDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());
}
