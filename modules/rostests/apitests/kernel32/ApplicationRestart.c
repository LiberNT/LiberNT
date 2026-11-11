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

START_TEST(ApplicationRestart)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WCHAR Buffer[RESTART_MAX_CMD_LINE + 1];
    DWORD Size, Flags;
    HRESULT hr;
    ULONG Index;

    pRegisterApplicationRestart = (FN_RegisterApplicationRestart)GetProcAddress(Kernel32, "RegisterApplicationRestart");
    pUnregisterApplicationRestart = (FN_UnregisterApplicationRestart)GetProcAddress(Kernel32, "UnregisterApplicationRestart");
    pGetApplicationRestartSettings = (FN_GetApplicationRestartSettings)GetProcAddress(Kernel32, "GetApplicationRestartSettings");
    if (!pRegisterApplicationRestart || !pUnregisterApplicationRestart || !pGetApplicationRestartSettings)
    {
        skip("Application restart exports are missing\n");
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
}
