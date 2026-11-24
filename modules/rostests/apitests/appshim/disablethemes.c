/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the DisableThemes layer applied to a process as it starts
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ndk/pstypes.h>
#include <ndk/rtlfuncs.h>
#include <strsafe.h>
#include "wine/test.h"

#define CHILD_RAN 0x1000
#define CHILD_SHIM_LOADED 0x100
#define CHILD_THEME_MASK 0xff

static DWORD ShimModules(void)
{
    PPEB_LDR_DATA Ldr = NtCurrentPeb()->Ldr;
    PLIST_ENTRY Entry;
    WCHAR AppPatch[MAX_PATH];
    UNICODE_STRING Prefix;

    GetWindowsDirectoryW(AppPatch, ARRAYSIZE(AppPatch));
    StringCchCatW(AppPatch, ARRAYSIZE(AppPatch), L"\\AppPatch\\");
    RtlInitUnicodeString(&Prefix, AppPatch);
    for (Entry = Ldr->InLoadOrderModuleList.Flink; Entry != &Ldr->InLoadOrderModuleList; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (RtlPrefixUnicodeString(&Prefix, &Module->FullDllName, TRUE))
            return CHILD_SHIM_LOADED;
    }
    return 0;
}

static DWORD RunChild(PCWSTR Layer)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD ExitCode = 0xdeadbeef;
    BOOL Created;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" disablethemes child", Path);
    SetEnvironmentVariableW(L"__COMPAT_LAYER", Layer);
    Created = CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process);
    SetEnvironmentVariableW(L"__COMPAT_LAYER", NULL);
    ok(Created, "CreateProcessW failed: %lu\n", GetLastError());
    if (!Created)
        return ExitCode;
    ok(WaitForSingleObject(Process.hProcess, 20000) == WAIT_OBJECT_0, "the child did not exit\n");
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return ExitCode;
}

START_TEST(disablethemes)
{
    char **argv;
    HMODULE UxTheme;
    DWORD (WINAPI *pGetThemeAppProperties)(void);
    DWORD Plain, Layered;

    if (winetest_get_mainargs(&argv) >= 3 && !strcmp(argv[2], "child"))
    {
        UxTheme = LoadLibraryW(L"uxtheme.dll");
        pGetThemeAppProperties = (void *)GetProcAddress(UxTheme, "GetThemeAppProperties");
        ExitProcess(pGetThemeAppProperties ? CHILD_RAN | ShimModules() | pGetThemeAppProperties() : 1);
    }

    Plain = RunChild(NULL);
    ok_hex(Plain & ~CHILD_THEME_MASK, CHILD_RAN);
    ok((Plain & CHILD_THEME_MASK) != 0, "child without a layer has theme properties 0x%lx\n", Plain);
    Layered = RunChild(L"DisableThemes");
    if (is_reactos())
    {
        ok_hex(Layered, CHILD_RAN | CHILD_SHIM_LOADED);
    }
    else
    {
        /* Windows 11 loads no shim for this layer */
        ok_hex(Layered, Plain);
    }
}
