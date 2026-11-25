/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that LiberNT programs and DLLs start only on LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

static
BOOL
HostIsLiberNT(VOID)
{
    static const WCHAR Product[] = L"LiberNT Operating System";
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HRSRC Resource;
    const WCHAR *Block;
    DWORD Count, Index;

    Resource = FindResourceW(Ntdll, MAKEINTRESOURCEW(VS_VERSION_INFO), (LPCWSTR)RT_VERSION);
    if (!Resource)
        return FALSE;
    Block = LockResource(LoadResource(Ntdll, Resource));
    if (!Block)
        return FALSE;
    Count = SizeofResource(Ntdll, Resource) / sizeof(WCHAR);
    for (Index = 0; Index + ARRAYSIZE(Product) - 1 <= Count; Index++)
    {
        if (!memcmp(Block + Index, Product, sizeof(Product) - sizeof(WCHAR)))
            return TRUE;
    }
    return FALSE;
}

static
BOOL
BesideSelf(
    PCWSTR Name,
    PWSTR Path,
    DWORD Size)
{
    PWSTR Slash;

    if (!GetModuleFileNameW(NULL, Path, Size))
        return FALSE;
    Slash = wcsrchr(Path, L'\\');
    if (!Slash || (DWORD)(Slash - Path) + 1 + wcslen(Name) + 1 > Size)
        return FALSE;
    wcscpy(Slash + 1, Name);
    return TRUE;
}

START_TEST(HostBinding)
{
    WCHAR Path[MAX_PATH];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HMODULE Module;
    DWORD Error, ExitCode = 0xDEADBEEF;
    BOOL LiberNT = HostIsLiberNT();

    trace("host is %s\n", LiberNT ? "LiberNT" : "not LiberNT");

    ok(BesideSelf(L"hostbind_dll.dll", Path, ARRAYSIZE(Path)), "No path for the DLL\n");
    SetLastError(0xDEADBEEF);
    Module = LoadLibraryW(Path);
    Error = GetLastError();
    if (LiberNT)
    {
        ok(Module != NULL, "A bound DLL was refused on LiberNT with %lu\n", Error);
    }
    else
    {
        ok(Module == NULL, "A bound DLL loaded on another system\n");
        ok(Error == ERROR_DLL_INIT_FAILED, "A bound DLL was refused with %lu\n", Error);
    }
    if (Module)
        FreeLibrary(Module);

    ok(BesideSelf(L"hostbind_child.exe", Path, ARRAYSIZE(Path)), "No path for the child\n");
    if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcessW failed with %lu\n", GetLastError());
        return;
    }
    WaitForSingleObject(Process.hProcess, INFINITE);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    ok(ExitCode == (LiberNT ? 0 : ERROR_NOT_SUPPORTED), "A bound program exited with %lu\n", ExitCode);
}
