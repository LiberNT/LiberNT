/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test listing a directory whose index is not cached, also while it is edited
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define COLD_FILES 400
#define EXTRA_FILES 64
#define EDIT_WAIT_MS 10000

static WCHAR VolumeRoot[] = L"?:\\";
static WCHAR VolumeDevice[] = L"\\\\.\\?:";
static WCHAR Directory[MAX_PATH];
static volatile LONG StopEditing;

static BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            VolumeDevice[4] = Letter;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
Remount(VOID)
{
    DWORD Returned;
    HANDLE Volume;
    BOOL Done;

    Volume = CreateFileW(VolumeDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;
    Done = DeviceIoControl(Volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Returned, NULL) &&
           DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Returned, NULL);
    CloseHandle(Volume);
    return Done;
}

static VOID
MakeName(_Out_writes_(MAX_PATH) PWSTR Buffer, _In_ PCWSTR Prefix, _In_ ULONG Index)
{
    StringCchPrintfW(Buffer, MAX_PATH, L"%s\\%s_%04lu_long_directory_entry_name.dat", Directory, Prefix, Index);
}

static BOOL
CreateEntry(_In_ PCWSTR Prefix, _In_ ULONG Index)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;

    MakeName(Path, Prefix, Index);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(File);
    return TRUE;
}

static BOOL
DeleteEntry(_In_ PCWSTR Prefix, _In_ ULONG Index)
{
    WCHAR Path[MAX_PATH];

    MakeName(Path, Prefix, Index);
    return DeleteFileW(Path);
}

static DWORD
WINAPI
EditDirectory(_In_ PVOID Parameter)
{
    ULONG Index;
    ULONG Failures = 0;

    UNREFERENCED_PARAMETER(Parameter);
    for (Index = 0; Index < EXTRA_FILES && !StopEditing; Index++)
    {
        if (!CreateEntry(L"extra", Index) || !DeleteEntry(L"extra", Index))
            Failures++;
    }
    return Failures;
}

/* Number of listed entries of the directory that are neither cold nor extra entries */
static ULONG
ListDirectory(_Out_writes_(COLD_FILES) PUCHAR Seen)
{
    WIN32_FIND_DATAW Data;
    WCHAR Pattern[MAX_PATH];
    HANDLE Search;
    ULONG Unexpected = 0;
    ULONG Index;
    WCHAR Tail;

    ZeroMemory(Seen, COLD_FILES);
    StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Directory);
    Search = FindFirstFileW(Pattern, &Data);
    ok(Search != INVALID_HANDLE_VALUE, "FindFirstFileW failed: %lu\n", GetLastError());
    if (Search == INVALID_HANDLE_VALUE)
        return 1;

    do
    {
        if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
            continue;
        if (swscanf(Data.cFileName, L"cold_%04lu_long_directory_entry_name.da%lc", &Index, &Tail) == 2 &&
            Tail == L't' && Index < COLD_FILES)
        {
            if (Seen[Index] < MAXUCHAR)
                Seen[Index]++;
            continue;
        }
        if (swscanf(Data.cFileName, L"extra_%04lu_long_directory_entry_name.da%lc", &Index, &Tail) == 2 &&
            Tail == L't' && Index < EXTRA_FILES)
        {
            continue;
        }
        trace("Unexpected entry %ls\n", Data.cFileName);
        Unexpected++;
    } while (FindNextFileW(Search, &Data));
    ok(GetLastError() == ERROR_NO_MORE_FILES, "FindNextFileW stopped with %lu\n", GetLastError());
    FindClose(Search);
    return Unexpected;
}

static VOID
CheckSeenOnce(_In_reads_(COLD_FILES) const UCHAR *Seen, _In_ PCSTR Pass)
{
    ULONG Missing = 0;
    ULONG Repeated = 0;
    ULONG Index;

    for (Index = 0; Index < COLD_FILES; Index++)
    {
        if (Seen[Index] == 0)
            Missing++;
        else if (Seen[Index] > 1)
            Repeated++;
    }
    ok(Missing == 0, "%s: %lu entries not listed\n", Pass, Missing);
    ok(Repeated == 0, "%s: %lu entries listed more than once\n", Pass, Repeated);
}

static VOID
CheckColdListings(VOID)
{
    UCHAR Seen[COLD_FILES];
    WIN32_FIND_DATAW Data;
    WCHAR Path[MAX_PATH];
    HANDLE Search;
    HANDLE Editor;
    DWORD Failures = MAXDWORD;

    ok(Remount(), "Dismount failed: %lu\n", GetLastError());
    ok(ListDirectory(Seen) == 0, "Cold listing returned entries it should not\n");
    CheckSeenOnce(Seen, "Cold listing");

    ok(Remount(), "Dismount failed: %lu\n", GetLastError());
    MakeName(Path, L"cold", COLD_FILES * 3 / 5);
    Search = FindFirstFileW(Path, &Data);
    ok(Search != INVALID_HANDLE_VALUE, "Exact lookup failed: %lu\n", GetLastError());
    if (Search != INVALID_HANDLE_VALUE)
    {
        ok(!wcscmp(Data.cFileName, wcsrchr(Path, L'\\') + 1), "Exact lookup returned %ls\n", Data.cFileName);
        ok(!FindNextFileW(Search, &Data) && GetLastError() == ERROR_NO_MORE_FILES,
           "Exact lookup returned a second entry %ls, error %lu\n", Data.cFileName, GetLastError());
        FindClose(Search);
    }

    ok(Remount(), "Dismount failed: %lu\n", GetLastError());
    StopEditing = FALSE;
    Editor = CreateThread(NULL, 0, EditDirectory, NULL, 0, NULL);
    ok(Editor != NULL, "CreateThread failed: %lu\n", GetLastError());
    ok(ListDirectory(Seen) == 0, "Edited cold listing returned entries it should not\n");
    CheckSeenOnce(Seen, "Edited cold listing");
    if (Editor)
    {
        InterlockedExchange(&StopEditing, TRUE);
        ok(WaitForSingleObject(Editor, EDIT_WAIT_MS) == WAIT_OBJECT_0, "Editing thread did not finish\n");
        ok(GetExitCodeThread(Editor, &Failures), "GetExitCodeThread failed: %lu\n", GetLastError());
        ok(Failures == 0, "%lu extra entries could not be created and deleted\n", Failures);
        CloseHandle(Editor);
    }
}

START_TEST(ColdDirectory)
{
    ULONG Created = 0;
    ULONG Index;

    if (!FindTestVolume())
    {
        skip("No NTFSTEST volume\n");
        return;
    }

    StringCchPrintfW(Directory, ARRAYSIZE(Directory), L"%sColdDirectory_%08lx", VolumeRoot, GetTickCount());
    if (!CreateDirectoryW(Directory, NULL))
    {
        skip("Cannot create %ls, error %lu\n", Directory, GetLastError());
        return;
    }
    for (Index = 0; Index < COLD_FILES; Index++)
    {
        if (!CreateEntry(L"cold", Index))
        {
            ok(0, "Create %lu failed: %lu\n", Index, GetLastError());
            break;
        }
        Created++;
    }
    if (Created == COLD_FILES)
        CheckColdListings();

    for (Index = 0; Index < EXTRA_FILES; Index++)
        DeleteEntry(L"extra", Index);
    for (Index = 0; Index < Created; Index++)
        ok(DeleteEntry(L"cold", Index), "Delete %lu failed: %lu\n", Index, GetLastError());
    ok(RemoveDirectoryW(Directory), "RemoveDirectoryW failed: %lu\n", GetLastError());
}
