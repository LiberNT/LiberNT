/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that exports Windows 11 implements as stubs are not export forwarders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _STUB_EXPORT
{
    PCSTR Module;
    PCSTR Name;
} STUB_EXPORT;

static const STUB_EXPORT StubExports[] =
{
    { "kernel32.dll", "GetVersionExW" },
    { "kernel32.dll", "GetVersionExA" },
    { "kernel32.dll", "MultiByteToWideChar" },
    { "kernel32.dll", "WideCharToMultiByte" },
    { "kernel32.dll", "GetLocaleInfoW" },
    { "kernel32.dll", "CompareStringW" },
    { "kernel32.dll", "LCMapStringW" },
    { "kernel32.dll", "FormatMessageW" },
    { "kernel32.dll", "lstrcmpW" },
    { "kernel32.dll", "lstrcmpiW" },
    { "kernel32.dll", "RegOpenKeyExW" },
    { "kernel32.dll", "RegQueryValueExW" },
    { "kernel32.dll", "WriteConsoleW" },
    { "advapi32.dll", "RegOpenKeyExW" },
    { "advapi32.dll", "RegQueryValueExW" },
    { "advapi32.dll", "RegSetValueExW" },
    { "advapi32.dll", "AddAccessAllowedAce" },
    { "shell32.dll", "StrChrW" },
    { "shell32.dll", "StrCmpNW" },
};

/* Walks the export directory the way programs with their own GetProcAddress do; a forwarder is not followed */
static FARPROC WalkExport(PBYTE Base, PCSTR Name, PBOOL Forwarded)
{
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)(Base + ((PIMAGE_DOS_HEADER)Base)->e_lfanew);
    PIMAGE_DATA_DIRECTORY Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    PIMAGE_EXPORT_DIRECTORY Exports = (PIMAGE_EXPORT_DIRECTORY)(Base + Directory->VirtualAddress);
    PDWORD Names = (PDWORD)(Base + Exports->AddressOfNames);
    PDWORD Functions = (PDWORD)(Base + Exports->AddressOfFunctions);
    PWORD Ordinals = (PWORD)(Base + Exports->AddressOfNameOrdinals);
    DWORD Index;

    *Forwarded = FALSE;
    for (Index = 0; Index < Exports->NumberOfNames; Index++)
    {
        DWORD Rva;

        if (strcmp((PCSTR)(Base + Names[Index]), Name))
            continue;
        Rva = Functions[Ordinals[Index]];
        if (Rva >= Directory->VirtualAddress && Rva < Directory->VirtualAddress + Directory->Size)
        {
            *Forwarded = TRUE;
            return NULL;
        }
        return (FARPROC)(Base + Rva);
    }
    return NULL;
}

START_TEST(ExportStubs)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(StubExports); Index++)
    {
        HMODULE Module = LoadLibraryA(StubExports[Index].Module);
        PIMAGE_NT_HEADERS Nt;
        FARPROC Walked, Resolved;
        BOOL Forwarded;

        ok(Module != NULL, "LoadLibraryA(%s) failed with %lu\n", StubExports[Index].Module, GetLastError());
        if (!Module)
            continue;
        Nt = (PIMAGE_NT_HEADERS)((PBYTE)Module + ((PIMAGE_DOS_HEADER)Module)->e_lfanew);
        Walked = WalkExport((PBYTE)Module, StubExports[Index].Name, &Forwarded);
        Resolved = GetProcAddress(Module, StubExports[Index].Name);
        ok(!Forwarded, "%s!%s is an export forwarder\n", StubExports[Index].Module, StubExports[Index].Name);
        ok(Walked != NULL, "%s!%s not found by an export walk\n", StubExports[Index].Module, StubExports[Index].Name);
        ok(Resolved == Walked, "%s!%s: GetProcAddress %p, export walk %p\n", StubExports[Index].Module, StubExports[Index].Name, Resolved, Walked);
        ok((PBYTE)Resolved >= (PBYTE)Module && (PBYTE)Resolved < (PBYTE)Module + Nt->OptionalHeader.SizeOfImage,
           "%s!%s resolves outside the module: %p\n", StubExports[Index].Module, StubExports[Index].Name, Resolved);
    }

    {
        BOOL Forwarded;
        BOOL (WINAPI *pGetVersionExW)(LPOSVERSIONINFOW) =
            (BOOL (WINAPI *)(LPOSVERSIONINFOW))WalkExport((PBYTE)GetModuleHandleA("kernel32.dll"), "GetVersionExW", &Forwarded);
        int (WINAPI *pMultiByteToWideChar)(UINT, DWORD, LPCSTR, int, LPWSTR, int) =
            (int (WINAPI *)(UINT, DWORD, LPCSTR, int, LPWSTR, int))WalkExport((PBYTE)GetModuleHandleA("kernel32.dll"), "MultiByteToWideChar", &Forwarded);
        OSVERSIONINFOW Version = { sizeof(Version) };
        WCHAR Wide[8] = { 0 };

        if (pGetVersionExW)
        {
            ok(pGetVersionExW(&Version), "GetVersionExW through the export walk failed with %lu\n", GetLastError());
            ok(Version.dwPlatformId == VER_PLATFORM_WIN32_NT, "dwPlatformId %lu\n", Version.dwPlatformId);
        }
        if (pMultiByteToWideChar)
        {
            ok(pMultiByteToWideChar(CP_ACP, 0, "abc", -1, Wide, ARRAYSIZE(Wide)) == 4, "MultiByteToWideChar through the export walk failed\n");
            ok(!wcscmp(Wide, L"abc"), "converted '%ls'\n", Wide);
        }
    }
}
