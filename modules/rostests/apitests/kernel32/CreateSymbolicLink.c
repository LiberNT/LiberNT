/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the CreateSymbolicLinkW flag contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static BOOL WriteSmallFile(PCWSTR Path, PCSTR Text)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Written = 0;
    BOOL Result;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) && Written == strlen(Text);
    CloseHandle(File);
    return Result;
}

static BOOL ReadSmallFile(PCWSTR Path, PSTR Buffer, DWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Read = 0;
    BOOL Result;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = ReadFile(File, Buffer, Size - 1, &Read, NULL);
    Buffer[Result ? Read : 0] = '\0';
    CloseHandle(File);
    return Result;
}

static void TestFileLink(PCWSTR Link, PCWSTR Target, DWORD Flags)
{
    CHAR Content[32];
    DWORD Attributes;
    BOOLEAN Result;

    SetLastError(0xdeadbeef);
    Result = CreateSymbolicLinkW(Link, Target, Flags);
    ok(Result, "flags 0x%lx: error %lu\n", Flags, GetLastError());
    if (!Result)
        return;
    Attributes = GetFileAttributesW(Link);
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT),
       "flags 0x%lx: link attributes 0x%lx\n", Flags, Attributes);
    ok(Attributes == INVALID_FILE_ATTRIBUTES || !(Attributes & FILE_ATTRIBUTE_DIRECTORY), "flags 0x%lx: file link is a directory\n", Flags);
    ok(ReadSmallFile(Link, Content, sizeof(Content)), "flags 0x%lx: cannot read through the link: %lu\n", Flags, GetLastError());
    ok(!strcmp(Content, "symlink-target"), "flags 0x%lx: read '%s' through the link\n", Flags, Content);
    ok(DeleteFileW(Link), "flags 0x%lx: DeleteFileW(link) failed with %lu\n", Flags, GetLastError());
    ok(GetFileAttributesW(Target) != INVALID_FILE_ATTRIBUTES, "flags 0x%lx: deleting the link deleted the target\n", Flags);
}

START_TEST(CreateSymbolicLink)
{
    WCHAR Directory[MAX_PATH], Target[MAX_PATH], Link[MAX_PATH], TargetDirectory[MAX_PATH], DirectoryLink[MAX_PATH];
    DWORD Attributes;
    BOOLEAN Result;

    ok(GetTempPathW(ARRAYSIZE(Directory), Directory) != 0, "GetTempPathW failed with %lu\n", GetLastError());
    wcscat(Directory, L"k32symlink");
    ok(CreateDirectoryW(Directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryW failed with %lu\n", GetLastError());
    swprintf(Target, ARRAYSIZE(Target), L"%ls\\target.txt", Directory);
    swprintf(Link, ARRAYSIZE(Link), L"%ls\\link.txt", Directory);
    swprintf(TargetDirectory, ARRAYSIZE(TargetDirectory), L"%ls\\dir", Directory);
    swprintf(DirectoryLink, ARRAYSIZE(DirectoryLink), L"%ls\\dirlink", Directory);
    DeleteFileW(Link);
    RemoveDirectoryW(DirectoryLink);
    ok(WriteSmallFile(Target, "symlink-target"), "cannot write the target file: %lu\n", GetLastError());
    ok(CreateDirectoryW(TargetDirectory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryW failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = CreateSymbolicLinkW(Link, Target, 0);
    if (!Result && GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
    {
        skip("the token cannot create symbolic links\n");
    }
    else
    {
        ok(Result, "flags 0: error %lu\n", GetLastError());
        if (Result)
            ok(DeleteFileW(Link), "DeleteFileW(link) failed with %lu\n", GetLastError());

        TestFileLink(Link, Target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        /* Windows 11 ignores the flag bits it does not define */
        TestFileLink(Link, Target, 0x4);
        TestFileLink(Link, Target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | 0x80000000);

        SetLastError(0xdeadbeef);
        Result = CreateSymbolicLinkW(Link, Target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        ok(Result, "link: error %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Result = CreateSymbolicLinkW(Link, Target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        ok(!Result && GetLastError() == ERROR_ALREADY_EXISTS, "existing link: result %u error %lu\n", Result, GetLastError());
        DeleteFileW(Link);

        SetLastError(0xdeadbeef);
        Result = CreateSymbolicLinkW(TargetDirectory, Target, 0);
        ok(!Result && GetLastError() == ERROR_ACCESS_DENIED, "file link over a directory: result %u error %lu\n", Result, GetLastError());
        SetLastError(0xdeadbeef);
        Result = CreateSymbolicLinkW(Target, TargetDirectory, SYMBOLIC_LINK_FLAG_DIRECTORY);
        ok(!Result && GetLastError() == ERROR_ALREADY_EXISTS, "directory link over a file: result %u error %lu\n", Result, GetLastError());
        ok(GetFileAttributesW(TargetDirectory) == FILE_ATTRIBUTE_DIRECTORY, "the directory changed: 0x%lx\n", GetFileAttributesW(TargetDirectory));

        Result = CreateSymbolicLinkW(DirectoryLink, TargetDirectory,
                                     SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        ok(Result, "directory link: error %lu\n", GetLastError());
        Attributes = GetFileAttributesW(DirectoryLink);
        ok(Attributes != INVALID_FILE_ATTRIBUTES &&
           (Attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY),
           "directory link attributes 0x%lx\n", Attributes);
        ok(RemoveDirectoryW(DirectoryLink), "RemoveDirectoryW(link) failed with %lu\n", GetLastError());
        ok(GetFileAttributesW(TargetDirectory) != INVALID_FILE_ATTRIBUTES, "removing the link removed the target directory\n");
    }

    DeleteFileW(Target);
    RemoveDirectoryW(TargetDirectory);
    RemoveDirectoryW(Directory);
}
