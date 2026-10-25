/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for LdrGetDllHandleByName
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *FN_LdrGetDllHandleByName)(PUNICODE_STRING, PUNICODE_STRING, PVOID *);

static FN_LdrGetDllHandleByName pLdrGetDllHandleByName;

static NTSTATUS
ByName(PCWSTR BaseName, PCWSTR FullName, PVOID *Handle)
{
    UNICODE_STRING Base, Full;

    if (BaseName)
        RtlInitUnicodeString(&Base, BaseName);
    if (FullName)
        RtlInitUnicodeString(&Full, FullName);
    *Handle = (PVOID)1;
    return pLdrGetDllHandleByName(BaseName ? &Base : NULL, FullName ? &Full : NULL, Handle);
}

START_TEST(LdrGetDllHandleByName)
{
    static const PCWSTR Candidates[] = { L"loadperf.dll", L"t2embed.dll", L"cryptdll.dll", L"sfc.dll" };
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    WCHAR FullPath[MAX_PATH], UpperPath[MAX_PATH];
    HMODULE hLoaded;
    NTSTATUS Status;
    PVOID Handle;
    UINT i;

    pLdrGetDllHandleByName = (FN_LdrGetDllHandleByName)GetProcAddress(hNtdll, "LdrGetDllHandleByName");
    ok(pLdrGetDllHandleByName != NULL, "ntdll!LdrGetDllHandleByName is missing\n");
    if (!pLdrGetDllHandleByName)
        return;

    ok(GetModuleFileNameW(hNtdll, FullPath, ARRAYSIZE(FullPath)) != 0, "GetModuleFileNameW failed\n");
    for (i = 0; FullPath[i]; i++)
        UpperPath[i] = towupper(FullPath[i]);
    UpperPath[i] = UNICODE_NULL;

    Status = ByName(L"ntdll.dll", NULL, &Handle);
    ok(Status == STATUS_SUCCESS, "base name: status 0x%08lx\n", Status);
    ok(Handle == hNtdll, "base name: handle %p\n", Handle);

    Status = ByName(L"NTDLL.DLL", NULL, &Handle);
    ok(Status == STATUS_SUCCESS, "upper-case base name: status 0x%08lx\n", Status);
    ok(Handle == hNtdll, "upper-case base name: handle %p\n", Handle);

    Status = ByName(L"ntdll", NULL, &Handle);
    ok(Status == STATUS_DLL_NOT_FOUND, "base name without extension: status 0x%08lx\n", Status);
    ok(Handle == (PVOID)1, "base name without extension: handle %p\n", Handle);

    Status = ByName(L"liberntnotloaded.dll", NULL, &Handle);
    ok(Status == STATUS_DLL_NOT_FOUND, "module not loaded: status 0x%08lx\n", Status);
    ok(Handle == (PVOID)1, "module not loaded: handle %p\n", Handle);

    Status = ByName(NULL, FullPath, &Handle);
    ok(Status == STATUS_SUCCESS, "full name: status 0x%08lx\n", Status);
    ok(Handle == hNtdll, "full name: handle %p\n", Handle);

    Status = ByName(NULL, UpperPath, &Handle);
    ok(Status == STATUS_SUCCESS, "upper-case full name: status 0x%08lx\n", Status);
    ok(Handle == hNtdll, "upper-case full name: handle %p\n", Handle);

    Status = ByName(NULL, L"C:\\Windows\\ntdll.dll", &Handle);
    ok(Status == STATUS_DLL_NOT_FOUND, "full name in another directory: status 0x%08lx\n", Status);
    ok(Handle == (PVOID)1, "full name in another directory: handle %p\n", Handle);

    Status = ByName(L"ntdll.dll", FullPath, &Handle);
    ok(Status == STATUS_SUCCESS, "both names: status 0x%08lx\n", Status);
    ok(Handle == hNtdll, "both names: handle %p\n", Handle);

    Status = ByName(L"kernel32.dll", FullPath, &Handle);
    ok(Status == STATUS_DLL_NOT_FOUND, "names of different modules: status 0x%08lx\n", Status);
    ok(Handle == (PVOID)1, "names of different modules: handle %p\n", Handle);

    for (i = 0; i < ARRAYSIZE(Candidates); i++)
    {
        if (GetModuleHandleW(Candidates[i]))
            continue;
        hLoaded = LoadLibraryW(Candidates[i]);
        if (!hLoaded)
            continue;
        FreeLibrary(hLoaded);
        if (!GetModuleHandleW(Candidates[i]))
            break;
    }
    if (i == ARRAYSIZE(Candidates))
    {
        skip("no module unloads cleanly\n");
        return;
    }

    hLoaded = LoadLibraryW(Candidates[i]);
    ok(hLoaded != NULL, "LoadLibraryW(%ls) failed, error %lu\n", Candidates[i], GetLastError());
    if (!hLoaded)
        return;
    Status = ByName(Candidates[i], NULL, &Handle);
    ok(Status == STATUS_SUCCESS, "loaded module: status 0x%08lx\n", Status);
    ok(Handle == hLoaded, "loaded module: handle %p\n", Handle);
    ok(FreeLibrary(hLoaded), "FreeLibrary failed\n");
    ok(GetModuleHandleW(Candidates[i]) == hLoaded, "the lookup must take a module reference\n");
    if (GetModuleHandleW(Candidates[i]))
    {
        ok(FreeLibrary(hLoaded), "second FreeLibrary failed\n");
        ok(GetModuleHandleW(Candidates[i]) == NULL, "the second FreeLibrary must unload the module\n");
    }
}
