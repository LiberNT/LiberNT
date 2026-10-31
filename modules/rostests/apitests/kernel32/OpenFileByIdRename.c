/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test OpenFileById before and after the parent directory of the file is renamed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HANDLE OpenById(HANDLE Hint, LONGLONG Id)
{
    FILE_ID_DESCRIPTOR Descriptor;

    Descriptor.dwSize = sizeof(Descriptor);
    Descriptor.Type = FileIdType;
    Descriptor.FileId.QuadPart = Id;
    return OpenFileById(Hint, &Descriptor, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, 0);
}

static void CheckOpen(HANDLE Hint, LONGLONG Id, LPCWSTR Suffix, int Line)
{
    WCHAR Final[MAX_PATH];
    char Data[8] = { 0 };
    DWORD Read = 0, Length;
    SIZE_T SuffixLength = wcslen(Suffix);
    HANDLE File = OpenById(Hint, Id);

    ok(File != INVALID_HANDLE_VALUE, "Line %d: OpenFileById failed with %lu\n", Line, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(ReadFile(File, Data, 4, &Read, NULL) && Read == 4 && !memcmp(Data, "data", 4),
       "Line %d: read %lu bytes '%s'\n", Line, Read, Data);
    Length = GetFinalPathNameByHandleW(File, Final, ARRAYSIZE(Final), FILE_NAME_NORMALIZED);
    ok(Length > SuffixLength && Length < ARRAYSIZE(Final) && !_wcsicmp(Final + Length - SuffixLength, Suffix),
       "Line %d: final path %S does not end with %S\n", Line, Final, Suffix);
    CloseHandle(File);
}

START_TEST(OpenFileByIdRename)
{
    WCHAR Temp[MAX_PATH], Volume[MAX_PATH], FileSystem[32], OldDir[MAX_PATH], NewDir[MAX_PATH];
    WCHAR OldFile[MAX_PATH], NewFile[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION Information;
    DWORD Written;
    LONGLONG Id;
    HANDLE File, Hint;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    if (!GetVolumePathNameW(Temp, Volume, ARRAYSIZE(Volume)) ||
        !GetVolumeInformationW(Volume, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) ||
        wcscmp(FileSystem, L"NTFS") != 0)
    {
        skip("%S is not on an NTFS volume\n", Temp);
        return;
    }
    StringCchPrintfW(OldDir, ARRAYSIZE(OldDir), L"%sbyid_old", Temp);
    StringCchPrintfW(NewDir, ARRAYSIZE(NewDir), L"%sbyid_new", Temp);
    StringCchPrintfW(OldFile, ARRAYSIZE(OldFile), L"%s\\file.txt", OldDir);
    StringCchPrintfW(NewFile, ARRAYSIZE(NewFile), L"%s\\file.txt", NewDir);
    DeleteFileW(OldFile);
    DeleteFileW(NewFile);
    RemoveDirectoryW(OldDir);
    RemoveDirectoryW(NewDir);

    ok(CreateDirectoryW(OldDir, NULL), "CreateDirectoryW(%S) failed with %lu\n", OldDir, GetLastError());
    File = CreateFileW(OldFile, GENERIC_WRITE | GENERIC_READ, 0, NULL, CREATE_NEW, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%S) failed with %lu\n", OldFile, GetLastError());
    ok(WriteFile(File, "data", 4, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
    ok(GetFileInformationByHandle(File, &Information), "GetFileInformationByHandle failed with %lu\n", GetLastError());
    CloseHandle(File);
    Id = ((LONGLONG)Information.nFileIndexHigh << 32) | Information.nFileIndexLow;

    Hint = CreateFileW(Volume, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Hint != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", Volume, GetLastError());

    File = OpenById(Hint, Id);
    if (File == INVALID_HANDLE_VALUE && (GetLastError() == ERROR_INVALID_FUNCTION || GetLastError() == ERROR_NOT_SUPPORTED))
    {
        skip("%S does not open files by ID\n", Volume);
        CloseHandle(Hint);
        DeleteFileW(OldFile);
        RemoveDirectoryW(OldDir);
        return;
    }
    CloseHandle(File);
    File = CreateFileW(OldFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CloseHandle(File);
    CheckOpen(Hint, Id, L"\\byid_old\\file.txt", __LINE__);

    ok(MoveFileW(OldDir, NewDir), "MoveFileW(%S, %S) failed with %lu\n", OldDir, NewDir, GetLastError());
    CheckOpen(Hint, Id, L"\\byid_new\\file.txt", __LINE__);

    ok(MoveFileW(NewFile, OldFile) == FALSE, "MoveFileW into the old directory succeeded\n");
    ok(CreateDirectoryW(OldDir, NULL), "CreateDirectoryW(%S) failed with %lu\n", OldDir, GetLastError());
    ok(MoveFileW(NewFile, OldFile), "MoveFileW(%S, %S) failed with %lu\n", NewFile, OldFile, GetLastError());
    CheckOpen(Hint, Id, L"\\byid_old\\file.txt", __LINE__);

    CloseHandle(Hint);
    DeleteFileW(OldFile);
    RemoveDirectoryW(OldDir);
    RemoveDirectoryW(NewDir);
}
