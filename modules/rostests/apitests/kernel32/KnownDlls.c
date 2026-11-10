/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the known DLLs the loader maps from \KnownDlls and \KnownDlls32
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/mmfuncs.h>
#include <ndk/obfuncs.h>

typedef BOOL (WINAPI *PFN_ISWOW64PROCESS2)(HANDLE, USHORT *, USHORT *);

static
BOOL
GetMachines(USHORT *ProcessMachine, USHORT *NativeMachine)
{
    PFN_ISWOW64PROCESS2 pIsWow64Process2;

    pIsWow64Process2 = (PFN_ISWOW64PROCESS2)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (!pIsWow64Process2)
    {
        skip("IsWow64Process2 is not available\n");
        return FALSE;
    }

    if (!pIsWow64Process2(GetCurrentProcess(), ProcessMachine, NativeMachine))
    {
        ok(FALSE, "IsWow64Process2 failed with %lu\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
OpenKnownDllObject(PCWSTR Directory, PCWSTR Name, BOOLEAN Section, PHANDLE Handle)
{
    UNICODE_STRING DirectoryName, ObjectName;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE DirectoryHandle;
    NTSTATUS Status;

    RtlInitUnicodeString(&DirectoryName, Directory);
    InitializeObjectAttributes(&Attributes, &DirectoryName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenDirectoryObject(&DirectoryHandle, DIRECTORY_QUERY | DIRECTORY_TRAVERSE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ObjectName, Name);
    InitializeObjectAttributes(&Attributes, &ObjectName, OBJ_CASE_INSENSITIVE, DirectoryHandle, NULL);
    if (Section)
        Status = NtOpenSection(Handle, SECTION_MAP_READ | SECTION_MAP_EXECUTE, &Attributes);
    else
        Status = NtOpenSymbolicLinkObject(Handle, SYMBOLIC_LINK_QUERY, &Attributes);

    NtClose(DirectoryHandle);
    return Status;
}

static
VOID
TestKnownDllSection(PCWSTR Directory, PCWSTR Name, BOOL Expected)
{
    HANDLE Section = NULL;
    NTSTATUS Status;

    Status = OpenKnownDllObject(Directory, Name, TRUE, &Section);
    if (Expected)
        ok(Status == STATUS_SUCCESS, "%ls\\%ls: status 0x%08lx, expected a section\n", Directory, Name, Status);
    else
        ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "%ls\\%ls: status 0x%08lx, expected no section\n", Directory, Name, Status);

    if (NT_SUCCESS(Status))
        NtClose(Section);
}

static
VOID
TestKnownDllPath(PCWSTR Directory, PCWSTR ExpectedPath)
{
    WCHAR Buffer[MAX_PATH];
    UNICODE_STRING Target, Expected;
    HANDLE Link = NULL;
    NTSTATUS Status;

    Status = OpenKnownDllObject(Directory, L"KnownDllPath", FALSE, &Link);
    ok(Status == STATUS_SUCCESS, "%ls\\KnownDllPath: status 0x%08lx\n", Directory, Status);
    if (!NT_SUCCESS(Status))
        return;

    RtlInitEmptyUnicodeString(&Target, Buffer, sizeof(Buffer));
    Status = NtQuerySymbolicLinkObject(Link, &Target, NULL);
    ok(Status == STATUS_SUCCESS, "NtQuerySymbolicLinkObject(%ls\\KnownDllPath) returned 0x%08lx\n", Directory, Status);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&Expected, ExpectedPath);
        ok(RtlEqualUnicodeString(&Target, &Expected, TRUE), "%ls\\KnownDllPath is '%.*ls', expected '%ls'\n",
           Directory, (int)(Target.Length / sizeof(WCHAR)), Target.Buffer, ExpectedPath);
    }

    NtClose(Link);
}

static
VOID
TestApplicationDirLoad(PCWSTR Name, BOOL Expected, PCWSTR SystemDirectory)
{
    WCHAR ExpectedPath[MAX_PATH], Path[MAX_PATH];
    HMODULE Module;
    DWORD Error;

    if (GetModuleHandleW(Name))
    {
        skip("%ls is already loaded in this process\n", Name);
        return;
    }

    SetLastError(0xdeadbeef);
    Module = LoadLibraryExW(Name, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    Error = GetLastError();
    if (!Expected)
    {
        ok(Module == NULL, "LoadLibraryExW(%ls, LOAD_LIBRARY_SEARCH_APPLICATION_DIR) loaded %p\n", Name, Module);
        ok(Error == ERROR_MOD_NOT_FOUND, "LoadLibraryExW(%ls) error %lu\n", Name, Error);
        if (Module)
            FreeLibrary(Module);
        return;
    }

    ok(Module != NULL, "LoadLibraryExW(%ls, LOAD_LIBRARY_SEARCH_APPLICATION_DIR) failed with %lu\n", Name, Error);
    if (!Module)
        return;

    StringCchPrintfW(ExpectedPath, RTL_NUMBER_OF(ExpectedPath), L"%ls\\%ls", SystemDirectory, Name);
    ok(GetModuleFileNameW(Module, Path, RTL_NUMBER_OF(Path)) != 0, "GetModuleFileNameW failed with %lu\n", GetLastError());
    ok(!_wcsicmp(Path, ExpectedPath), "%ls loaded from '%ls', expected '%ls'\n", Name, Path, ExpectedPath);
    FreeLibrary(Module);
}

START_TEST(KnownDlls)
{
    USHORT ProcessMachine, NativeMachine;
    WCHAR SystemDirectory[MAX_PATH];
    PCWSTR Directory;
    BOOL Wow64, Expected;

    if (!GetMachines(&ProcessMachine, &NativeMachine))
        return;

    Wow64 = (ProcessMachine != IMAGE_FILE_MACHINE_UNKNOWN);
    if (Wow64)
    {
        if (ProcessMachine != IMAGE_FILE_MACHINE_I386)
        {
            skip("WoW64 machine 0x%x is not covered\n", ProcessMachine);
            return;
        }

        ok(GetSystemWow64DirectoryW(SystemDirectory, RTL_NUMBER_OF(SystemDirectory)) != 0,
           "GetSystemWow64DirectoryW failed with %lu\n", GetLastError());
        Directory = L"\\KnownDlls32";
        Expected = TRUE;
        TestKnownDllPath(Directory, SystemDirectory);
    }
    else
    {
        if (NativeMachine != IMAGE_FILE_MACHINE_AMD64 && NativeMachine != IMAGE_FILE_MACHINE_ARM64)
        {
            skip("Native machine 0x%x is not covered\n", NativeMachine);
            return;
        }

        ok(GetSystemDirectoryW(SystemDirectory, RTL_NUMBER_OF(SystemDirectory)) != 0,
           "GetSystemDirectoryW failed with %lu\n", GetLastError());
        Directory = L"\\KnownDlls";
        Expected = (NativeMachine == IMAGE_FILE_MACHINE_AMD64);
    }

    TestKnownDllSection(Directory, L"difxapi.dll", Expected);
    TestKnownDllSection(Directory, L"wintrust.dll", Expected);
    TestKnownDllSection(Directory, L"crypt32.dll", Expected);
    TestApplicationDirLoad(L"wintrust.dll", Expected, SystemDirectory);
}
