/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process started from a renamed image and a renamed open file report their new names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/psfuncs.h>
#include <tlhelp32.h>

static BOOL
EndsWith(PCWSTR String, PCWSTR Suffix)
{
    SIZE_T Length = wcslen(String), SuffixLength = wcslen(Suffix);

    return Length > SuffixLength && String[Length - SuffixLength - 1] == L'\\' &&
           !_wcsicmp(String + Length - SuffixLength, Suffix);
}

static void
GetSnapshotName(DWORD ProcessId, PWSTR Name)
{
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W Entry = {sizeof(Entry)};
    BOOL More;

    Name[0] = UNICODE_NULL;
    for (More = Process32FirstW(Snapshot, &Entry); More; More = Process32NextW(Snapshot, &Entry))
    {
        if (Entry.th32ProcessID == ProcessId)
            wcscpy(Name, Entry.szExeFile);
    }
    CloseHandle(Snapshot);
}

static void
GetImageName(HANDLE Process, PROCESSINFOCLASS Class, PWSTR Name)
{
    BYTE Buffer[sizeof(UNICODE_STRING) + MAX_PATH * 2 * sizeof(WCHAR)];
    UNICODE_STRING *String = (UNICODE_STRING *)Buffer;
    NTSTATUS Status;

    Name[0] = UNICODE_NULL;
    Status = NtQueryInformationProcess(Process, Class, Buffer, sizeof(Buffer), NULL);
    ok(NT_SUCCESS(Status), "class %d returned %#lx\n", Class, Status);
    if (NT_SUCCESS(Status))
    {
        memcpy(Name, String->Buffer, String->Length);
        Name[String->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }
}

static void
CheckProcess(const char *What, PROCESS_INFORMATION *Info, PCWSTR Started, PCWSTR Current)
{
    WCHAR Name[MAX_PATH * 2];
    DWORD Size;

    GetSnapshotName(Info->dwProcessId, Name);
    ok(!_wcsicmp(Name, Started), "%s: snapshot name %ls, expected %ls\n", What, Name, Started);

    GetImageName(Info->hProcess, ProcessImageFileName, Name);
    ok(EndsWith(Name, Started), "%s: native image name %ls, expected ...%ls\n", What, Name, Started);
    Size = ARRAYSIZE(Name);
    Name[0] = UNICODE_NULL;
    ok(QueryFullProcessImageNameW(Info->hProcess, PROCESS_NAME_NATIVE, Name, &Size), "%s: native query failed: %lu\n",
       What, GetLastError());
    ok(EndsWith(Name, Started), "%s: native full name %ls, expected ...%ls\n", What, Name, Started);

    GetImageName(Info->hProcess, ProcessImageFileNameWin32, Name);
    ok(EndsWith(Name, Current), "%s: Win32 image name %ls, expected ...%ls\n", What, Name, Current);
    Size = ARRAYSIZE(Name);
    Name[0] = UNICODE_NULL;
    ok(QueryFullProcessImageNameW(Info->hProcess, 0, Name, &Size), "%s: Win32 query failed: %lu\n", What,
       GetLastError());
    ok(EndsWith(Name, Current), "%s: Win32 full name %ls, expected ...%ls\n", What, Name, Current);
}

static BOOL
StartSuspended(PCWSTR Path, PROCESS_INFORMATION *Info)
{
    STARTUPINFOW Startup = {sizeof(Startup)};
    WCHAR CommandLine[MAX_PATH * 2];

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\" /c exit", Path);
    return CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL,
                          &Startup, Info);
}

static void
CheckOpenFile(PCWSTR Directory)
{
    WCHAR Old[MAX_PATH], New[MAX_PATH], Name[MAX_PATH * 2];
    HANDLE File;

    StringCchPrintfW(Old, ARRAYSIZE(Old), L"%ls\\data_old.txt", Directory);
    StringCchPrintfW(New, ARRAYSIZE(New), L"%ls\\data_new.txt", Directory);
    DeleteFileW(Old);
    DeleteFileW(New);
    File = CreateFileW(Old, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(MoveFileW(Old, New), "renaming the open file failed: %lu\n", GetLastError());

    Name[0] = UNICODE_NULL;
    ok(GetFinalPathNameByHandleW(File, Name, ARRAYSIZE(Name), FILE_NAME_NORMALIZED) != 0,
       "GetFinalPathNameByHandle failed: %lu\n", GetLastError());
    ok(EndsWith(Name, L"data_new.txt"), "open file path %ls\n", Name);
    Name[0] = UNICODE_NULL;
    ok(GetFinalPathNameByHandleW(File, Name, ARRAYSIZE(Name), FILE_NAME_NORMALIZED | VOLUME_NAME_NT) != 0,
       "GetFinalPathNameByHandle(VOLUME_NAME_NT) failed: %lu\n", GetLastError());
    ok(EndsWith(Name, L"data_new.txt"), "open file NT path %ls\n", Name);

    CloseHandle(File);
    DeleteFileW(New);
}

START_TEST(ProcessImageRename)
{
    WCHAR Directory[MAX_PATH], First[MAX_PATH], Second[MAX_PATH], Source[MAX_PATH];
    PROCESS_INFORMATION Before, After;

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    wcscat(Directory, L"ProcessImageRename");
    CreateDirectoryW(Directory, NULL);
    StringCchPrintfW(First, ARRAYSIZE(First), L"%ls\\new_probe.exe", Directory);
    StringCchPrintfW(Second, ARRAYSIZE(Second), L"%ls\\probe.exe", Directory);
    DeleteFileW(First);
    DeleteFileW(Second);
    GetSystemDirectoryW(Source, ARRAYSIZE(Source));
    wcscat(Source, L"\\cmd.exe");
    ok(CopyFileW(Source, First, FALSE), "CopyFile failed: %lu\n", GetLastError());

    ok(StartSuspended(First, &Before), "CreateProcess(new_probe.exe) failed: %lu\n", GetLastError());
    if (!Before.hProcess)
        return;
    CheckProcess("before the rename", &Before, L"new_probe.exe", L"new_probe.exe");

    ok(MoveFileW(First, Second), "renaming the running image failed: %lu\n", GetLastError());
    ok(StartSuspended(Second, &After), "CreateProcess(probe.exe) failed: %lu\n", GetLastError());
    if (After.hProcess)
    {
        CheckProcess("started after the rename", &After, L"probe.exe", L"probe.exe");
        TerminateProcess(After.hProcess, 0);
        WaitForSingleObject(After.hProcess, 5000);
        CloseHandle(After.hProcess);
        CloseHandle(After.hThread);
    }
    CheckProcess("started before the rename", &Before, L"new_probe.exe", L"probe.exe");

    TerminateProcess(Before.hProcess, 0);
    WaitForSingleObject(Before.hProcess, 5000);
    CloseHandle(Before.hProcess);
    CloseHandle(Before.hThread);
    DeleteFileW(Second);

    CheckOpenFile(Directory);
    RemoveDirectoryW(Directory);
}
