/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests which DLLs the \KnownDlls directory holds and that the others load from the search path
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static NTSTATUS
OpenKnownDll(
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Section;
    NTSTATUS Status;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\KnownDlls\\%ls", Name);
    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenSection(&Section, SECTION_QUERY, &Attributes);
    if (NT_SUCCESS(Status))
        NtClose(Section);
    return Status;
}

static void
TestLocalCopy(
    _In_ PCWSTR Directory,
    _In_ PCWSTR Name)
{
    WCHAR System[MAX_PATH], Local[MAX_PATH], Loaded[MAX_PATH];
    HMODULE Module;

    if (GetModuleHandleW(Name))
    {
        skip("%ls is already loaded in this process\n", Name);
        return;
    }
    GetSystemDirectoryW(System, ARRAYSIZE(System));
    StringCchCatW(System, ARRAYSIZE(System), L"\\");
    StringCchCatW(System, ARRAYSIZE(System), Name);
    StringCchPrintfW(Local, ARRAYSIZE(Local), L"%ls\\%ls", Directory, Name);
    if (!CopyFileW(System, Local, FALSE))
    {
        skip("cannot copy %ls: %lu\n", System, GetLastError());
        return;
    }
    Module = LoadLibraryW(Name);
    ok(Module != NULL, "%ls: load failed: %lu\n", Name, GetLastError());
    if (Module)
    {
        Loaded[0] = UNICODE_NULL;
        GetModuleFileNameW(Module, Loaded, ARRAYSIZE(Loaded));
        ok(!_wcsicmp(Loaded, Local), "%ls: loaded %ls instead of %ls\n", Name, Loaded, Local);
        FreeLibrary(Module);
    }
    DeleteFileW(Local);
}

START_TEST(KnownDlls)
{
    static const PCWSTR Listed[] =
    {
        L"kernel32.dll", L"user32.dll", L"imagehlp.dll", L"setupapi.dll", L"shell32.dll", L"comdlg32.dll",
        L"crypt32.dll", L"wintrust.dll",
    };
    static const PCWSTR NotListed[] =
    {
        L"dbghelp.dll", L"version.dll", L"cabinet.dll", L"cfgmgr32.dll", L"comctl32.dll", L"bcrypt.dll",
        L"ncrypt.dll", L"windows.storage.dll", L"winspool.drv",
    };
    WCHAR Directory[MAX_PATH];
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Listed); i++)
    {
        Status = OpenKnownDll(Listed[i]);
        ok(Status == STATUS_SUCCESS, "%ls: %lx\n", Listed[i], Status);
    }
    for (i = 0; i < ARRAYSIZE(NotListed); i++)
    {
        Status = OpenKnownDll(NotListed[i]);
        ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "%ls: %lx\n", NotListed[i], Status);
    }

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    StringCchCatW(Directory, ARRAYSIZE(Directory), L"KnownDllsTest");
    CreateDirectoryW(Directory, NULL);
    ok(SetDllDirectoryW(Directory), "SetDllDirectoryW failed: %lu\n", GetLastError());
    TestLocalCopy(Directory, L"version.dll");
    TestLocalCopy(Directory, L"dbghelp.dll");
    SetDllDirectoryW(NULL);
    RemoveDirectoryW(Directory);
}
