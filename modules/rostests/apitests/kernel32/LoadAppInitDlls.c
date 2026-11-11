/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test LoadAppInitDlls and the AppInit_DLLs registry values
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef VOID (WINAPI *PFN_LOAD_APP_INIT_DLLS)(VOID);

#define APPINIT_TEST_DLL L"kernel32_appinit.dll"

static const WCHAR WindowsKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows";

typedef struct _SAVED_VALUE
{
    PCWSTR Name;
    LONG Error;
    DWORD Type;
    DWORD Size;
    BYTE Data[2048];
} SAVED_VALUE, *PSAVED_VALUE;

static
VOID
SaveValue(
    _In_ HKEY Key,
    _Inout_ PSAVED_VALUE Value)
{
    Value->Size = sizeof(Value->Data);
    Value->Error = RegQueryValueExW(Key, Value->Name, NULL, &Value->Type, Value->Data, &Value->Size);
}

static
VOID
RestoreValue(
    _In_ HKEY Key,
    _In_ PSAVED_VALUE Value)
{
    LONG Error;

    if (Value->Error == ERROR_SUCCESS)
        Error = RegSetValueExW(Key, Value->Name, 0, Value->Type, Value->Data, Value->Size);
    else
        Error = RegDeleteValueW(Key, Value->Name);
    ok(Error == ERROR_SUCCESS || (Value->Error != ERROR_SUCCESS && Error == ERROR_FILE_NOT_FOUND),
       "Restoring %S failed with %ld\n", Value->Name, Error);
}

static
VOID
SetConfiguration(
    _In_ HKEY Key,
    _In_ DWORD Load,
    _In_ DWORD RequireSigned,
    _In_ PCWSTR List)
{
    ok_long(RegSetValueExW(Key, L"LoadAppInit_DLLs", 0, REG_DWORD, (const BYTE *)&Load, sizeof(Load)), ERROR_SUCCESS);
    ok_long(RegSetValueExW(Key, L"RequireSignedAppInit_DLLs", 0, REG_DWORD,
                           (const BYTE *)&RequireSigned, sizeof(RequireSigned)), ERROR_SUCCESS);
    ok_long(RegSetValueExW(Key, L"AppInit_DLLs", 0, REG_SZ, (const BYTE *)List,
                           (DWORD)((wcslen(List) + 1) * sizeof(WCHAR))), ERROR_SUCCESS);
}

static
DWORD
ChildLoadedDll(
    _In_ PCWSTR ExePath)
{
    WCHAR CommandLine[MAX_PATH + 64];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode = 0xdeadbeef, Wait;

    StringCchPrintfW(CommandLine, RTL_NUMBER_OF(CommandLine), L"\"%s\" LoadAppInitDlls child", ExePath);
    RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(ExePath, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(FALSE, "CreateProcessW failed with %lu\n", GetLastError());
        return ExitCode;
    }
    Wait = WaitForSingleObject(ProcessInfo.hProcess, 10000);
    ok_long(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(ProcessInfo.hProcess, 0xdead);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return ExitCode;
}

static
NTSTATUS
CallLoadAppInitDlls(
    _In_ PFN_LOAD_APP_INIT_DLLS Function)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        Function();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

static
VOID
TestExports(
    _Out_ PFN_LOAD_APP_INIT_DLLS *Function)
{
    PFN_LOAD_APP_INIT_DLLS pKernelBaseLoadAppInitDlls, pKernel32LoadAppInitDlls;

    pKernelBaseLoadAppInitDlls = (PFN_LOAD_APP_INIT_DLLS)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                                                                        "LoadAppInitDlls");
    pKernel32LoadAppInitDlls = (PFN_LOAD_APP_INIT_DLLS)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                      "LoadAppInitDlls");
    *Function = pKernelBaseLoadAppInitDlls;
    if (!pKernelBaseLoadAppInitDlls)
        return;
    ok(pKernel32LoadAppInitDlls == pKernelBaseLoadAppInitDlls,
       "kernel32 LoadAppInitDlls %p, kernelbase %p\n", pKernel32LoadAppInitDlls, pKernelBaseLoadAppInitDlls);

    ok_hex(CallLoadAppInitDlls(pKernelBaseLoadAppInitDlls), STATUS_SUCCESS);
}

START_TEST(LoadAppInitDlls)
{
    PFN_LOAD_APP_INIT_DLLS pLoadAppInitDlls;
    SAVED_VALUE Saved[3] = {{L"AppInit_DLLs"}, {L"LoadAppInit_DLLs"}, {L"RequireSignedAppInit_DLLs"}};
    WCHAR ExePath[MAX_PATH], DllPath[MAX_PATH], ShortPath[MAX_PATH], List[MAX_PATH + 16];
    PWSTR Slash;
    char **Arguments;
    ULONG Index;
    LONG Error;
    HKEY Key;

    if (winetest_get_mainargs(&Arguments) >= 3 && strcmp(Arguments[2], "child") == 0)
        ExitProcess(GetModuleHandleW(APPINIT_TEST_DLL) != NULL ? 1 : 0);

    TestExports(&pLoadAppInitDlls);
    if (!pLoadAppInitDlls)
    {
        skip("LoadAppInitDlls is not available\n");
        return;
    }

    GetModuleFileNameW(NULL, ExePath, RTL_NUMBER_OF(ExePath));
    StringCchCopyW(DllPath, RTL_NUMBER_OF(DllPath), ExePath);
    Slash = wcsrchr(DllPath, L'\\');
    if (!Slash)
    {
        skip("Unexpected executable path %S\n", ExePath);
        return;
    }
    StringCchCopyW(Slash + 1, RTL_NUMBER_OF(DllPath) - (Slash + 1 - DllPath), APPINIT_TEST_DLL);
    if (!GetShortPathNameW(DllPath, ShortPath, RTL_NUMBER_OF(ShortPath)))
    {
        skip("%S is missing (%lu)\n", DllPath, GetLastError());
        return;
    }
    ok(GetModuleHandleW(APPINIT_TEST_DLL) == NULL, "The test DLL is already loaded\n");

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, WindowsKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
    {
        skip("Cannot open the AppInit_DLLs key for writing: %ld\n", Error);
        return;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Saved); Index++)
        SaveValue(Key, &Saved[Index]);

    StringCchPrintfW(List, RTL_NUMBER_OF(List), L" ,%s", ShortPath);
    SetConfiguration(Key, 1, 0, List);
    ok_long(ChildLoadedDll(ExePath), 1);

    ok_hex(CallLoadAppInitDlls(pLoadAppInitDlls), STATUS_SUCCESS);
    ok(GetModuleHandleW(APPINIT_TEST_DLL) == NULL, "A second LoadAppInitDlls call loaded the list\n");

    SetConfiguration(Key, 0, 0, List);
    ok_long(ChildLoadedDll(ExePath), 0);

    SetConfiguration(Key, 1, 1, List);
    ok_long(ChildLoadedDll(ExePath), 0);

    StringCchPrintfW(List, RTL_NUMBER_OF(List), L"%s;", ShortPath);
    SetConfiguration(Key, 1, 0, List);
    ok_long(ChildLoadedDll(ExePath), 0);

    for (Index = 0; Index < RTL_NUMBER_OF(Saved); Index++)
        RestoreValue(Key, &Saved[Index]);
    RegCloseKey(Key);
}
