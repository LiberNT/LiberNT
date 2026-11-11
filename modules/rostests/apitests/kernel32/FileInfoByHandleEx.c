/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the GetFileInformationByHandleEx and SetFileInformationByHandle classes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_RENAME_REPLACE_IF_EXISTS 0x00000001
#define TEST_RENAME_IGNORE_READONLY_ATTRIBUTE 0x00000040
#define TEST_CS_FLAG_CASE_SENSITIVE_DIR 0x00000001

typedef struct _TEST_FILE_RENAME_INFO
{
    union
    {
        BOOLEAN ReplaceIfExists;
        DWORD Flags;
    };
    HANDLE RootDirectory;
    DWORD FileNameLength;
    WCHAR FileName[MAX_PATH];
} TEST_FILE_RENAME_INFO;

typedef struct _TEST_CASE_SENSITIVE_INFO
{
    ULONG Flags;
} TEST_CASE_SENSITIVE_INFO;

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
FileExists(
    _In_ PCWSTR Path)
{
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static
HANDLE
CreateTestFile(
    _In_ PCWSTR Path,
    _In_ DWORD Access,
    _In_ DWORD Share,
    _In_ DWORD Attributes)
{
    HANDLE File;
    DWORD Written;

    File = CreateFileW(Path, Access | GENERIC_WRITE, Share, NULL, CREATE_ALWAYS, Attributes, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        WriteFile(File, "data", 4, &Written, NULL);
    return File;
}

static
VOID
TestGetClasses(VOID)
{
    WCHAR Path[MAX_PATH];
    WCHAR StreamPath[MAX_PATH];
    WCHAR Name[MAX_PATH];
    UCHAR Buffer[sizeof(FILE_NAME_INFO) + MAX_PATH * sizeof(WCHAR)];
    UCHAR StreamBuffer[0x400];
    PFILE_NAME_INFO NameInfo = (PFILE_NAME_INFO)Buffer;
    PFILE_STREAM_INFO StreamInfo;
    FILE_REMOTE_PROTOCOL_INFO Remote;
    TEST_FILE_RENAME_INFO RenameInfo;
    BOOLEAN FoundData = FALSE;
    BOOLEAN FoundAlternate = FALSE;
    HANDLE File;
    HANDLE Stream;
    HANDLE Directory;
    DWORD DispositionFlags;
    DWORD Written;
    BOOL Result;

    MakePath(Path, L"get.txt");
    File = CreateTestFile(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, FileRemoteProtocolInfo, &Remote, sizeof(Remote));
    ok(!Result, "FileRemoteProtocolInfo succeeded on a local file\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "FileRemoteProtocolInfo failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, FileDispositionInfoEx, &DispositionFlags, sizeof(DispositionFlags));
    ok(!Result, "FileDispositionInfoEx query succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "FileDispositionInfoEx query failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, FileRenameInfoEx, &RenameInfo, sizeof(RenameInfo));
    ok(!Result, "FileRenameInfoEx query succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "FileRenameInfoEx query failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, (FILE_INFO_BY_HANDLE_CLASS)0xdeadbeef, Buffer, sizeof(Buffer));
    ok(!Result, "Class 0xdeadbeef succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Class 0xdeadbeef failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, MaximumFileInfoByHandleClass, Buffer, sizeof(Buffer));
    ok(!Result, "MaximumFileInfoByHandleClass succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "MaximumFileInfoByHandleClass failed with %lu\n", GetLastError());

    memset(Buffer, 0xCC, sizeof(Buffer));
    Result = GetFileInformationByHandleEx(File, FileNormalizedNameInfo, Buffer, sizeof(Buffer));
    ok(Result, "FileNormalizedNameInfo failed with %lu\n", GetLastError());
    if (Result)
    {
        ok(NameInfo->FileNameLength == wcslen(Path + 2) * sizeof(WCHAR),
           "FileNormalizedNameInfo length %lu, expected %Iu\n", NameInfo->FileNameLength, wcslen(Path + 2) * sizeof(WCHAR));
        StringCchCopyNW(Name, RTL_NUMBER_OF(Name), NameInfo->FileName, NameInfo->FileNameLength / sizeof(WCHAR));
        ok(!_wcsicmp(Name, Path + 2), "FileNormalizedNameInfo returned %S, expected %S\n", Name, Path + 2);
    }

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, FileNormalizedNameInfo, Buffer, sizeof(FILE_NAME_INFO));
    ok(!Result, "FileNormalizedNameInfo with a short buffer succeeded\n");
    ok(GetLastError() == ERROR_MORE_DATA, "FileNormalizedNameInfo with a short buffer failed with %lu\n", GetLastError());

    StringCchPrintfW(StreamPath, RTL_NUMBER_OF(StreamPath), L"%s:ads", Path);
    Stream = CreateFileW(StreamPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, CREATE_ALWAYS, 0, NULL);
    ok(Stream != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", StreamPath, GetLastError());
    if (Stream != INVALID_HANDLE_VALUE)
    {
        WriteFile(Stream, "x", 1, &Written, NULL);
        CloseHandle(Stream);
    }

    memset(StreamBuffer, 0xCC, sizeof(StreamBuffer));
    Result = GetFileInformationByHandleEx(File, FileStreamInfo, StreamBuffer, sizeof(StreamBuffer));
    ok(Result, "FileStreamInfo failed with %lu\n", GetLastError());
    StreamInfo = (PFILE_STREAM_INFO)StreamBuffer;
    while (Result)
    {
        StringCchCopyNW(Name, RTL_NUMBER_OF(Name), StreamInfo->StreamName, StreamInfo->StreamNameLength / sizeof(WCHAR));
        if (!wcscmp(Name, L"::$DATA"))
        {
            FoundData = TRUE;
            ok(StreamInfo->StreamSize.QuadPart == 4, "::$DATA size %I64d\n", StreamInfo->StreamSize.QuadPart);
        }
        else if (!wcscmp(Name, L":ads:$DATA"))
        {
            FoundAlternate = TRUE;
            ok(StreamInfo->StreamSize.QuadPart == 1, ":ads:$DATA size %I64d\n", StreamInfo->StreamSize.QuadPart);
        }
        if (!StreamInfo->NextEntryOffset)
            break;
        StreamInfo = (PFILE_STREAM_INFO)((PUCHAR)StreamInfo + StreamInfo->NextEntryOffset);
    }
    ok(FoundData, "FileStreamInfo did not return ::$DATA\n");
    ok(FoundAlternate, "FileStreamInfo did not return :ads:$DATA\n");

    CloseHandle(File);
    DeleteFileW(Path);

    Directory = CreateFileW(TestDirectory, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Directory != INVALID_HANDLE_VALUE, "Opening the test directory failed with %lu\n", GetLastError());
    if (Directory != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Result = GetFileInformationByHandleEx(Directory, FileStreamInfo, StreamBuffer, sizeof(StreamBuffer));
        ok(!Result, "FileStreamInfo succeeded on a directory without streams\n");
        ok(GetLastError() == ERROR_HANDLE_EOF, "FileStreamInfo on a directory failed with %lu\n", GetLastError());
        CloseHandle(Directory);
    }
}

static
VOID
TestSetQueryOnlyClasses(VOID)
{
    static const FILE_INFO_BY_HANDLE_CLASS Classes[] =
    {
        FileNameInfo, FileStreamInfo, FileIdBothDirectoryInfo, FileIdBothDirectoryRestartInfo,
        FileFullDirectoryInfo, FileFullDirectoryRestartInfo, FileStorageInfo, FileAlignmentInfo,
        FileIdInfo, FileIdExtdDirectoryInfo, FileIdExtdDirectoryRestartInfo, FileNormalizedNameInfo,
        FileStandardInfo, FileCompressionInfo, FileAttributeTagInfo, FileRemoteProtocolInfo
    };
    UCHAR Buffer[0x200];
    WCHAR Path[MAX_PATH];
    HANDLE File;
    ULONG Index;
    BOOL Result;

    MakePath(Path, L"set.txt");
    File = CreateTestFile(Path, GENERIC_READ | FILE_WRITE_ATTRIBUTES | DELETE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(Classes); Index++)
    {
        memset(Buffer, 0, sizeof(Buffer));
        SetLastError(0xdeadbeef);
        Result = SetFileInformationByHandle(File, Classes[Index], Buffer, sizeof(Buffer));
        ok(!Result, "SetFileInformationByHandle class %u succeeded\n", Classes[Index]);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "SetFileInformationByHandle class %u failed with %lu\n",
           Classes[Index], GetLastError());
    }

    CloseHandle(File);
    DeleteFileW(Path);
}

static
VOID
TestCaseSensitive(VOID)
{
    TEST_CASE_SENSITIVE_INFO Info;
    WCHAR Path[MAX_PATH];
    HANDLE Directory;
    BOOL Result;

    MakePath(Path, L"cs");
    ok(CreateDirectoryW(Path, NULL), "CreateDirectory(%S) failed with %lu\n", Path, GetLastError());
    Directory = CreateFileW(Path, GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Directory != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", Path, GetLastError());
    if (Directory == INVALID_HANDLE_VALUE)
    {
        RemoveDirectoryW(Path);
        return;
    }

    Info.Flags = 0xdeadbeef;
    Result = GetFileInformationByHandleEx(Directory, FileCaseSensitiveInfo, &Info, sizeof(Info));
    ok(Result, "FileCaseSensitiveInfo query failed with %lu\n", GetLastError());
    ok(Result && Info.Flags == 0, "New directory case sensitivity flags 0x%lx\n", Info.Flags);

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(Directory, FileCaseSensitiveInfo, &Info, 0);
    ok(!Result, "FileCaseSensitiveInfo query with no buffer succeeded\n");
    ok(GetLastError() == ERROR_BAD_LENGTH, "FileCaseSensitiveInfo query with no buffer failed with %lu\n", GetLastError());

    Info.Flags = TEST_CS_FLAG_CASE_SENSITIVE_DIR;
    Result = SetFileInformationByHandle(Directory, FileCaseSensitiveInfo, &Info, sizeof(Info));
    ok(Result, "Setting FileCaseSensitiveInfo failed with %lu\n", GetLastError());
    Info.Flags = 0;
    Result = GetFileInformationByHandleEx(Directory, FileCaseSensitiveInfo, &Info, sizeof(Info));
    ok(Result && (Info.Flags & TEST_CS_FLAG_CASE_SENSITIVE_DIR), "Case sensitivity flags after set 0x%lx (%lu)\n",
       Info.Flags, GetLastError());

    Info.Flags = 0;
    Result = SetFileInformationByHandle(Directory, FileCaseSensitiveInfo, &Info, sizeof(Info));
    ok(Result, "Clearing FileCaseSensitiveInfo failed with %lu\n", GetLastError());
    Info.Flags = 0xdeadbeef;
    Result = GetFileInformationByHandleEx(Directory, FileCaseSensitiveInfo, &Info, sizeof(Info));
    ok(Result && Info.Flags == 0, "Case sensitivity flags after clear 0x%lx (%lu)\n", Info.Flags, GetLastError());

    CloseHandle(Directory);
    RemoveDirectoryW(Path);
}

static
BOOL
RenameEx(
    _In_ HANDLE File,
    _In_ DWORD Flags,
    _In_ PCWSTR Target)
{
    TEST_FILE_RENAME_INFO Info;

    ZeroMemory(&Info, sizeof(Info));
    Info.Flags = Flags;
    Info.RootDirectory = NULL;
    Info.FileNameLength = (DWORD)(wcslen(Target) * sizeof(WCHAR));
    StringCchCopyW(Info.FileName, RTL_NUMBER_OF(Info.FileName), Target);
    return SetFileInformationByHandle(File, FileRenameInfoEx, &Info, sizeof(Info));
}

static
VOID
TestRenameEx(VOID)
{
    WCHAR From[MAX_PATH];
    WCHAR To1[MAX_PATH];
    WCHAR To2[MAX_PATH];
    WCHAR ReadOnly[MAX_PATH];
    HANDLE File;
    BOOL Result;

    MakePath(From, L"from.txt");
    MakePath(To1, L"to1.txt");
    MakePath(To2, L"to2.txt");
    MakePath(ReadOnly, L"readonly.txt");

    File = CreateTestFile(To1, 0, 0, 0);
    CloseHandle(File);
    File = CreateTestFile(ReadOnly, 0, 0, FILE_ATTRIBUTE_READONLY);
    CloseHandle(File);
    File = CreateTestFile(From, GENERIC_READ | DELETE, 0, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Result = RenameEx(File, 0, To1);
    ok(!Result, "FileRenameInfoEx onto an existing file succeeded\n");
    ok(GetLastError() == ERROR_ALREADY_EXISTS, "FileRenameInfoEx onto an existing file failed with %lu\n", GetLastError());

    Result = RenameEx(File, TEST_RENAME_REPLACE_IF_EXISTS, To1);
    ok(Result, "FileRenameInfoEx with FILE_RENAME_REPLACE_IF_EXISTS failed with %lu\n", GetLastError());

    Result = RenameEx(File, 0, To2);
    ok(Result, "FileRenameInfoEx to a free name failed with %lu\n", GetLastError());
    CloseHandle(File);

    ok(!FileExists(From), "%S still exists\n", From);
    ok(!FileExists(To1), "%S still exists\n", To1);
    ok(FileExists(To2), "%S does not exist\n", To2);

    File = CreateFileW(To2, GENERIC_READ | DELETE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", To2, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Result = RenameEx(File, TEST_RENAME_REPLACE_IF_EXISTS, ReadOnly);
        ok(!Result, "FileRenameInfoEx onto a read-only file succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "FileRenameInfoEx onto a read-only file failed with %lu\n", GetLastError());
        ok(FileExists(To2), "%S does not exist\n", To2);
        ok(FileExists(ReadOnly), "%S does not exist\n", ReadOnly);

        Result = RenameEx(File, TEST_RENAME_REPLACE_IF_EXISTS | TEST_RENAME_IGNORE_READONLY_ATTRIBUTE, ReadOnly);
        ok(Result, "FileRenameInfoEx with FILE_RENAME_IGNORE_READONLY_ATTRIBUTE failed with %lu\n", GetLastError());
        CloseHandle(File);
        ok(!FileExists(To2), "%S still exists\n", To2);
        ok(FileExists(ReadOnly), "%S does not exist\n", ReadOnly);
    }

    SetFileAttributesW(ReadOnly, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(ReadOnly);
    DeleteFileW(To2);
    DeleteFileW(To1);
    DeleteFileW(From);
}

static
VOID
TestRenameRelative(VOID)
{
    TEST_FILE_RENAME_INFO Info;
    WCHAR From[MAX_PATH];
    WCHAR To[MAX_PATH];
    HANDLE Directory;
    HANDLE File;
    BOOL Result;

    MakePath(From, L"relfrom.txt");
    MakePath(To, L"relto.txt");
    Directory = CreateFileW(TestDirectory, FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Directory != INVALID_HANDLE_VALUE, "Opening the test directory failed with %lu\n", GetLastError());
    File = CreateTestFile(From, GENERIC_READ | DELETE, 0, 0);
    if (Directory == INVALID_HANDLE_VALUE || File == INVALID_HANDLE_VALUE)
    {
        if (Directory != INVALID_HANDLE_VALUE)
            CloseHandle(Directory);
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
        DeleteFileW(From);
        return;
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.ReplaceIfExists = FALSE;
    Info.RootDirectory = Directory;
    Info.FileNameLength = (DWORD)(wcslen(L"relto.txt") * sizeof(WCHAR));
    StringCchCopyW(Info.FileName, RTL_NUMBER_OF(Info.FileName), L"relto.txt");
    Result = SetFileInformationByHandle(File, FileRenameInfo, &Info, sizeof(Info));
    ok(Result, "FileRenameInfo relative to RootDirectory failed with %lu\n", GetLastError());
    CloseHandle(File);
    CloseHandle(Directory);

    ok(!FileExists(From), "%S still exists\n", From);
    ok(FileExists(To), "%S does not exist\n", To);
    DeleteFileW(To);
    DeleteFileW(From);
}

START_TEST(FileInfoByHandleEx)
{
    WCHAR TempPath[MAX_PATH];
    WCHAR LongPath[MAX_PATH];
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    Length = GetLongPathNameW(TempPath, LongPath, RTL_NUMBER_OF(LongPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(LongPath), "GetLongPathName failed with %lu\n", GetLastError());
    StringCchPrintfW(TestDirectory, RTL_NUMBER_OF(TestDirectory), L"%sfibhex%lx", LongPath, GetCurrentProcessId());
    ok(CreateDirectoryW(TestDirectory, NULL), "CreateDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());

    TestGetClasses();
    TestSetQueryOnlyClasses();
    TestCaseSensitive();
    TestRenameEx();
    TestRenameRelative();

    ok(RemoveDirectoryW(TestDirectory), "RemoveDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());
}
