/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests LOAD_WITH_ALTERED_SEARCH_PATH loads after SetDefaultDllDirectories
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"
#include <strsafe.h>

typedef BOOL (WINAPI *FN_SetDefaultDllDirectories)(DWORD);

static WCHAR TestDir[MAX_PATH];
static WCHAR SystemDir[MAX_PATH];

static VOID TestLoad(PCWSTR Name, DWORD Flags, BOOL ExpectLoaded, DWORD ExpectedError, int Line)
{
    HMODULE Module;
    DWORD Error;

    SetLastError(0xdeadbeef);
    Module = LoadLibraryExW(Name, NULL, Flags);
    Error = GetLastError();
    if (ExpectLoaded)
    {
        ok_(__FILE__, Line)(Module != NULL, "LoadLibraryExW(%S, 0x%lx) failed with error %lu\n", Name, Flags, Error);
    }
    else
    {
        ok_(__FILE__, Line)(Module == NULL, "LoadLibraryExW(%S, 0x%lx) returned %p\n", Name, Flags, Module);
        ok_(__FILE__, Line)(Error == ExpectedError, "LoadLibraryExW(%S, 0x%lx) error %lu, expected %lu\n",
                            Name, Flags, Error, ExpectedError);
    }
    if (Module)
        FreeLibrary(Module);
}

static BOOL PrepareFiles(VOID)
{
    WCHAR Source[MAX_PATH], Target[MAX_PATH], TempDir[MAX_PATH];

    GetSystemDirectoryW(SystemDir, ARRAYSIZE(SystemDir));
    GetTempPathW(ARRAYSIZE(TempDir), TempDir);
    StringCchPrintfW(TestDir, ARRAYSIZE(TestDir), L"%sllaltered", TempDir);
    CreateDirectoryW(TestDir, NULL);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\sub", TestDir);
    CreateDirectoryW(Target, NULL);

    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%s\\version.dll", SystemDir);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\llaltered_cwd.dll", TestDir);
    if (!CopyFileW(Source, Target, FALSE))
        return FALSE;
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\sub\\llaltered_sub.dll", TestDir);
    return CopyFileW(Source, Target, FALSE);
}

static VOID RunChild(VOID)
{
    FN_SetDefaultDllDirectories pSetDefaultDllDirectories;
    WCHAR Path[MAX_PATH];
    HMODULE Module, Other;

    pSetDefaultDllDirectories = (FN_SetDefaultDllDirectories)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                            "SetDefaultDllDirectories");
    ok(pSetDefaultDllDirectories != NULL, "SetDefaultDllDirectories is missing\n");
    if (!pSetDefaultDllDirectories)
        return;

    ok(pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS), "SetDefaultDllDirectories failed with error %lu\n",
       GetLastError());
    ok(SetCurrentDirectoryW(TestDir), "SetCurrentDirectoryW failed with error %lu\n", GetLastError());
    ok(GetModuleHandleW(L"msimg32.dll") == NULL, "msimg32.dll is already loaded\n");

    TestLoad(L"msimg32", LOAD_WITH_ALTERED_SEARCH_PATH, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(L"msimg32.dll", LOAD_WITH_ALTERED_SEARCH_PATH, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(L"kernel32", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"llaltered_cwd.dll", LOAD_WITH_ALTERED_SEARCH_PATH, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(L"sub\\llaltered_sub.dll", LOAD_WITH_ALTERED_SEARCH_PATH, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(L"llaltered_cwd.dll", 0, FALSE, ERROR_MOD_NOT_FOUND, __LINE__);

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\msimg32.dll", SystemDir);
    TestLoad(Path, LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\sub\\llaltered_sub.dll", TestDir);
    TestLoad(Path, LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);

    TestLoad(L"msimg32.dll", LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(L"msimg32.dll", LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_SYSTEM32, FALSE,
             ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Path, LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, TRUE, 0, __LINE__);
    TestLoad(Path, LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_SYSTEM32, TRUE, 0, __LINE__);
    TestLoad(L"kernel32", LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, TRUE, 0, __LINE__);
    TestLoad(L"kernel32", LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_SYSTEM32, TRUE, 0, __LINE__);

    ok(GetModuleHandleW(L"wldap32.dll") == NULL, "wldap32.dll is already loaded\n");
    ok(GetModuleHandleW(L"normaliz.dll") == NULL, "normaliz.dll is already loaded\n");
    TestLoad(L"wldap32", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"normaliz.dll", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\msimg32.dll", SystemDir);
    Module = LoadLibraryW(Path);
    ok(Module != NULL, "LoadLibraryW(%S) failed with error %lu\n", Path, GetLastError());
    TestLoad(L"msimg32", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"MSIMG32.DLL", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"msimg32.dll", LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, TRUE, 0, __LINE__);
    TestLoad(L"msimg32.dll", 0, TRUE, 0, __LINE__);
    TestLoad(L"llaltered_cwd.dll", LOAD_WITH_ALTERED_SEARCH_PATH, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\llaltered_cwd.dll", TestDir);
    Other = LoadLibraryW(Path);
    ok(Other != NULL, "LoadLibraryW(%S) failed with error %lu\n", Path, GetLastError());
    TestLoad(L"llaltered_cwd.dll", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"llaltered_cwd.dll", 0, TRUE, 0, __LINE__);
    if (Other)
        FreeLibrary(Other);
    if (Module)
        FreeLibrary(Module);
}

START_TEST(LoadLibraryAlteredSearchPath)
{
    WCHAR Exe[MAX_PATH], CommandLine[MAX_PATH + 64], OldDir[MAX_PATH];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    char **Arguments;

    if (!PrepareFiles())
    {
        skip("Preparing the test DLLs failed with error %lu\n", GetLastError());
        return;
    }

    if (winetest_get_mainargs(&Arguments) >= 3 && !strcmp(Arguments[2], "child"))
    {
        RunChild();
        return;
    }

    GetCurrentDirectoryW(ARRAYSIZE(OldDir), OldDir);
    ok(SetCurrentDirectoryW(TestDir), "SetCurrentDirectoryW failed with error %lu\n", GetLastError());
    TestLoad(L"msimg32", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"llaltered_cwd.dll", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"sub\\llaltered_sub.dll", LOAD_WITH_ALTERED_SEARCH_PATH, TRUE, 0, __LINE__);
    TestLoad(L"msimg32.dll", LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, FALSE, ERROR_INVALID_PARAMETER, __LINE__);
    SetCurrentDirectoryW(OldDir);

    GetModuleFileNameW(NULL, Exe, ARRAYSIZE(Exe));
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" LoadLibraryAlteredSearchPath child", Exe);
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with error %lu\n", GetLastError());
        return;
    }
    winetest_wait_child_process(ProcessInfo.hProcess);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
