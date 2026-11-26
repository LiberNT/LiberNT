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
    { "kernelbase.dll", "VirtualAlloc" },
    { "kernelbase.dll", "VirtualFree" },
    { "kernelbase.dll", "VirtualProtect" },
    { "kernelbase.dll", "VirtualQuery" },
    { "kernelbase.dll", "GetProcAddress" },
    { "kernelbase.dll", "LoadLibraryExW" },
    { "kernelbase.dll", "GetModuleHandleW" },
    { "kernelbase.dll", "CreateFileW" },
    { "kernelbase.dll", "CreateEventW" },
    { "kernelbase.dll", "CloseHandle" },
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

static ULONG CountForwardersTo(PBYTE Base, PCSTR Prefix)
{
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)(Base + ((PIMAGE_DOS_HEADER)Base)->e_lfanew);
    PIMAGE_DATA_DIRECTORY Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    PIMAGE_EXPORT_DIRECTORY Exports = (PIMAGE_EXPORT_DIRECTORY)(Base + Directory->VirtualAddress);
    PDWORD Functions = (PDWORD)(Base + Exports->AddressOfFunctions);
    SIZE_T PrefixLength = strlen(Prefix);
    ULONG Count = 0;
    DWORD Index;

    for (Index = 0; Index < Exports->NumberOfFunctions; Index++)
    {
        DWORD Rva = Functions[Index];

        if (Rva >= Directory->VirtualAddress && Rva < Directory->VirtualAddress + Directory->Size &&
            !_strnicmp((PCSTR)(Base + Rva), Prefix, PrefixLength))
        {
            if (Count < 5)
                trace("forwarder %s\n", (PCSTR)(Base + Rva));
            Count++;
        }
    }
    return Count;
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

    {
        PBYTE KernelBase = (PBYTE)GetModuleHandleA("kernelbase.dll");
        BOOL Forwarded;
        LPVOID (WINAPI *pVirtualAlloc)(LPVOID, SIZE_T, DWORD, DWORD);
        BOOL (WINAPI *pVirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD);
        BOOL (WINAPI *pVirtualFree)(LPVOID, SIZE_T, DWORD);
        MEMORY_BASIC_INFORMATION Info;
        DWORD OldProtect = 0;
        PBYTE Memory;

        ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
        if (!KernelBase)
            return;
        todo_ros ok(CountForwardersTo(KernelBase, "kernel32.") == 0, "kernelbase.dll forwards exports to kernel32.dll\n");

        pVirtualAlloc = (LPVOID (WINAPI *)(LPVOID, SIZE_T, DWORD, DWORD))WalkExport(KernelBase, "VirtualAlloc", &Forwarded);
        pVirtualProtect = (BOOL (WINAPI *)(LPVOID, SIZE_T, DWORD, PDWORD))WalkExport(KernelBase, "VirtualProtect", &Forwarded);
        pVirtualFree = (BOOL (WINAPI *)(LPVOID, SIZE_T, DWORD))WalkExport(KernelBase, "VirtualFree", &Forwarded);
        if (!pVirtualAlloc || !pVirtualProtect || !pVirtualFree)
            return;

        Memory = pVirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        ok(Memory != NULL, "VirtualAlloc through the export walk failed with %lu\n", GetLastError());
        if (!Memory)
            return;
        Memory[0] = 0x5A;
        Memory[0xFFFF] = 0xA5;
        ok(pVirtualProtect(Memory, 0x1000, PAGE_READONLY, &OldProtect), "VirtualProtect through the export walk failed with %lu\n", GetLastError());
        ok(OldProtect == PAGE_READWRITE, "old protection %#lx\n", OldProtect);
        ok(VirtualQuery(Memory, &Info, sizeof(Info)) == sizeof(Info), "VirtualQuery failed with %lu\n", GetLastError());
        ok(Info.Protect == PAGE_READONLY, "protection %#lx\n", Info.Protect);
        ok(Info.RegionSize == 0x1000, "region size %#Ix\n", (UINT_PTR)Info.RegionSize);
        ok(Memory[0] == 0x5A && Memory[0xFFFF] == 0xA5, "contents changed\n");
        ok(pVirtualFree(Memory, 0, MEM_RELEASE), "VirtualFree through the export walk failed with %lu\n", GetLastError());
        ok(VirtualQuery(Memory, &Info, sizeof(Info)) == sizeof(Info), "VirtualQuery after release failed with %lu\n", GetLastError());
        ok(Info.State == MEM_FREE, "state after release %#lx\n", Info.State);
    }
}
