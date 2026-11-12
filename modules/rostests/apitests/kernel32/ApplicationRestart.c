/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for application restart registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef RESTART_NO_CRASH
#define RESTART_NO_CRASH  1
#define RESTART_NO_HANG   2
#define RESTART_NO_PATCH  4
#define RESTART_NO_REBOOT 8
#endif

typedef HRESULT (WINAPI *FN_RegisterApplicationRestart)(PCWSTR, DWORD);
typedef HRESULT (WINAPI *FN_UnregisterApplicationRestart)(VOID);
typedef HRESULT (WINAPI *FN_GetApplicationRestartSettings)(HANDLE, PWSTR, PDWORD, PDWORD);

static FN_RegisterApplicationRestart pRegisterApplicationRestart;
static FN_UnregisterApplicationRestart pUnregisterApplicationRestart;
static FN_GetApplicationRestartSettings pGetApplicationRestartSettings;

static WCHAR CommandLine[RESTART_MAX_CMD_LINE + 1];

#define CHILD_WAIT_MS 15000

static
VOID
RunChild(
    _In_ HANDLE ReadyEvent,
    _In_ HANDLE DoneEvent,
    _In_ PCSTR Mode)
{
    if (!strcmp(Mode, "register"))
        pRegisterApplicationRestart(L"/child -r:7", RESTART_NO_HANG);

    SetEvent(ReadyEvent);
    WaitForSingleObject(DoneEvent, CHILD_WAIT_MS);
    ExitProcess(0);
}

static
HANDLE
StartChild(
    _In_ HANDLE ReadyEvent,
    _In_ HANDLE DoneEvent,
    _In_ PCSTR Mode)
{
    CHAR Path[MAX_PATH], Arguments[MAX_PATH * 2];
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    DWORD Wait;

    if (!GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path)))
        return NULL;

    StringCbPrintfA(Arguments, sizeof(Arguments), "\"%s\" ApplicationRestart child %lu %lu %s",
                    Path, HandleToUlong(ReadyEvent), HandleToUlong(DoneEvent), Mode);
    if (!CreateProcessA(NULL, Arguments, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcess(%s) failed: %lu\n", Mode, GetLastError());
        return NULL;
    }
    CloseHandle(ProcessInfo.hThread);

    Wait = WaitForSingleObject(ReadyEvent, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "%s child not ready (%lu)\n", Mode, Wait);
    return ProcessInfo.hProcess;
}

static
VOID
StopChild(
    _In_ HANDLE Process,
    _In_ HANDLE DoneEvent)
{
    DWORD Wait;

    SetEvent(DoneEvent);
    Wait = WaitForSingleObject(Process, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "child did not exit (%lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process, 0xDEAD);
    CloseHandle(Process);
    ResetEvent(DoneEvent);
}

static
VOID
TestRemote(VOID)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    HANDLE ReadyEvent, DoneEvent, Process, Limited;
    WCHAR Buffer[64];
    DWORD Size, Flags;
    HRESULT hr;

    ReadyEvent = CreateEventW(&Attributes, FALSE, FALSE, NULL);
    DoneEvent = CreateEventW(&Attributes, TRUE, FALSE, NULL);
    ok(ReadyEvent && DoneEvent, "CreateEvent failed: %lu\n", GetLastError());
    if (!ReadyEvent || !DoneEvent)
        return;

    Process = StartChild(ReadyEvent, DoneEvent, "register");
    if (Process)
    {
        Size = ARRAYSIZE(Buffer);
        Flags = 0xDEADBEEF;
        hr = pGetApplicationRestartSettings(Process, Buffer, &Size, &Flags);
        ok_hex(hr, S_OK);
        ok_long(Size, 12);
        ok_wstr(Buffer, L"/child -r:7");
        ok_long(Flags, RESTART_NO_HANG);

        Size = 0;
        hr = pGetApplicationRestartSettings(Process, NULL, &Size, NULL);
        ok_hex(hr, S_OK);
        ok_long(Size, 12);

        Limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetProcessId(Process));
        ok(Limited != NULL, "OpenProcess failed: %lu\n", GetLastError());
        if (Limited)
        {
            Size = ARRAYSIZE(Buffer);
            hr = pGetApplicationRestartSettings(Limited, Buffer, &Size, NULL);
            ok(FAILED(hr), "GetApplicationRestartSettings without PROCESS_VM_READ returned 0x%lx\n", hr);
            CloseHandle(Limited);
        }

        StopChild(Process, DoneEvent);
    }

    Process = StartChild(ReadyEvent, DoneEvent, "none");
    if (Process)
    {
        Size = ARRAYSIZE(Buffer);
        hr = pGetApplicationRestartSettings(Process, Buffer, &Size, NULL);
        ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        StopChild(Process, DoneEvent);
    }

    CloseHandle(ReadyEvent);
    CloseHandle(DoneEvent);
}

START_TEST(ApplicationRestart)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WCHAR Buffer[RESTART_MAX_CMD_LINE + 1];
    DWORD Size, Flags;
    HRESULT hr;
    ULONG Index;
    char **Arguments;
    int ArgumentCount;

    pRegisterApplicationRestart = (FN_RegisterApplicationRestart)GetProcAddress(Kernel32, "RegisterApplicationRestart");
    pUnregisterApplicationRestart = (FN_UnregisterApplicationRestart)GetProcAddress(Kernel32, "UnregisterApplicationRestart");
    pGetApplicationRestartSettings = (FN_GetApplicationRestartSettings)GetProcAddress(Kernel32, "GetApplicationRestartSettings");
    if (!pRegisterApplicationRestart || !pUnregisterApplicationRestart || !pGetApplicationRestartSettings)
    {
        skip("Application restart exports are missing\n");
        return;
    }

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 6 && !strcmp(Arguments[2], "child"))
    {
        RunChild(ULongToHandle(strtoul(Arguments[3], NULL, 10)),
                 ULongToHandle(strtoul(Arguments[4], NULL, 10)),
                 Arguments[5]);
        return;
    }

    Size = 0;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), NULL, &Size, NULL);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    hr = pRegisterApplicationRestart(L"/restart -r:42", RESTART_NO_PATCH | RESTART_NO_REBOOT);
    ok_hex(hr, S_OK);

    Size = 0;
    Flags = 0xDEADBEEF;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), NULL, &Size, &Flags);
    ok_hex(hr, S_OK);
    ok_long(Size, 15);

    Size = 4;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), NULL, &Size, NULL);
    ok_hex(hr, E_INVALIDARG);

    Size = 14;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), Buffer, &Size, NULL);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    ok_long(Size, 15);

    Size = ARRAYSIZE(Buffer);
    Flags = 0xDEADBEEF;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), Buffer, &Size, &Flags);
    ok_hex(hr, S_OK);
    ok_long(Size, 15);
    ok_wstr(Buffer, L"/restart -r:42");
    ok_long(Flags, RESTART_NO_PATCH | RESTART_NO_REBOOT);

    hr = pGetApplicationRestartSettings(GetCurrentProcess(), Buffer, NULL, NULL);
    ok_hex(hr, E_INVALIDARG);

    hr = pRegisterApplicationRestart(L"/restart", 0x10);
    ok_hex(hr, E_INVALIDARG);

    for (Index = 0; Index < RESTART_MAX_CMD_LINE; Index++)
        CommandLine[Index] = L'x';
    CommandLine[RESTART_MAX_CMD_LINE] = UNICODE_NULL;
    hr = pRegisterApplicationRestart(CommandLine, 0);
    ok_hex(hr, E_INVALIDARG);

    CommandLine[RESTART_MAX_CMD_LINE - 1] = UNICODE_NULL;
    hr = pRegisterApplicationRestart(CommandLine, RESTART_NO_CRASH | RESTART_NO_HANG);
    ok_hex(hr, S_OK);
    Size = 0;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), NULL, &Size, NULL);
    ok_hex(hr, S_OK);
    ok_long(Size, RESTART_MAX_CMD_LINE);

    hr = pUnregisterApplicationRestart();
    ok_hex(hr, S_OK);
    Size = ARRAYSIZE(Buffer);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), Buffer, &Size, NULL);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    TestRemote();
}
