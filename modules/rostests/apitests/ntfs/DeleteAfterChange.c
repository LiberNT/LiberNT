/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test deleting an open file after its name or links changed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"

static WCHAR VolumeRoot[] = L"?:\\";

static
BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR System[MAX_PATH];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            return TRUE;
        }
    }

    if (GetSystemDirectoryW(System, ARRAYSIZE(System)) == 0)
        return FALSE;
    VolumeRoot[0] = System[0];
    return GetVolumeInformationW(VolumeRoot, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) &&
           !wcscmp(FileSystem, L"NTFS");
}

static
BOOL
MakeFile(
    _In_ PCWSTR Path,
    _In_ DWORD Value)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Done;
    BOOL Ok;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = WriteFile(File, &Value, sizeof(Value), &Done, NULL) && Done == sizeof(Value);
    CloseHandle(File);
    return Ok;
}

static
VOID
TestDeleteRenamed(VOID)
{
    WCHAR Old[MAX_PATH], New[MAX_PATH];
    BYTE Buffer[sizeof(FILE_RENAME_INFO) + MAX_PATH * sizeof(WCHAR)];
    FILE_RENAME_INFO *Rename = (FILE_RENAME_INFO *)Buffer;
    FILE_DISPOSITION_INFO Disposition = { TRUE };
    HANDLE File;

    StringCchPrintfW(Old, ARRAYSIZE(Old), L"%sdelchg_old.txt", VolumeRoot);
    StringCchPrintfW(New, ARRAYSIZE(New), L"%sdelchg_new.txt", VolumeRoot);
    DeleteFileW(New);
    ok(MakeFile(Old, 1), "Creating %ls failed: %lu\n", Old, GetLastError());

    File = CreateFileW(Old, DELETE | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed: %lu\n", Old, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(Buffer, sizeof(Buffer));
    Rename->ReplaceIfExists = FALSE;
    Rename->FileNameLength = (DWORD)(wcslen(New) * sizeof(WCHAR));
    StringCchCopyW(Rename->FileName, MAX_PATH, New);
    ok(SetFileInformationByHandle(File, FileRenameInfo, Rename, sizeof(Buffer)), "Renaming failed: %lu\n", GetLastError());
    ok(SetFileInformationByHandle(File, FileDispositionInfo, &Disposition, sizeof(Disposition)),
       "Setting the disposition failed: %lu\n", GetLastError());
    CloseHandle(File);

    ok(GetFileAttributesW(New) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "The renamed file %ls survived its delete: %lu\n", New, GetLastError());
    ok(GetFileAttributesW(Old) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "The old name %ls came back: %lu\n", Old, GetLastError());
}

static
VOID
TestDeleteLinked(VOID)
{
    WCHAR Original[MAX_PATH], Link[MAX_PATH];
    HANDLE File;
    DWORD Value = 0, Done = 0;

    StringCchPrintfW(Original, ARRAYSIZE(Original), L"%sdelchg_orig.txt", VolumeRoot);
    StringCchPrintfW(Link, ARRAYSIZE(Link), L"%sdelchg_link.txt", VolumeRoot);
    DeleteFileW(Link);
    ok(MakeFile(Original, 0x51ED), "Creating %ls failed: %lu\n", Original, GetLastError());

    File = CreateFileW(Original, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed: %lu\n", Original, GetLastError());
    ok(CreateHardLinkW(Link, Original, NULL), "Linking %ls failed: %lu\n", Link, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    ok(GetFileAttributesW(Original) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "%ls survived its delete-on-close handle: %lu\n", Original, GetLastError());
    File = CreateFileW(Link, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "The link %ls is gone: %lu\n", Link, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        ok(ReadFile(File, &Value, sizeof(Value), &Done, NULL) && Done == sizeof(Value) && Value == 0x51ED,
           "The link %ls lost its data: %lu bytes, 0x%lx\n", Link, Done, Value);
        CloseHandle(File);
    }
    ok(DeleteFileW(Link), "Deleting %ls failed: %lu\n", Link, GetLastError());
}

START_TEST(DeleteAfterChange)
{
    if (!FindTestVolume())
    {
        skip("No NTFS volume to test on\n");
        return;
    }
    TestDeleteRenamed();
    TestDeleteLinked();
}
