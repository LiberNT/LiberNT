/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DLL whose DLL_PROCESS_ATTACH waits for the LoaderLock test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <windef.h>
#include <winbase.h>

static LONG Initialized;

BOOL
WINAPI
LoaderWaitInitialized(VOID)
{
    return InterlockedCompareExchange(&Initialized, 0, 0) != 0;
}

BOOL
WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    PVOID Reserved)
{
    WCHAR Name[64];
    HANDLE Attaching, Proceed;

    if (Reason != DLL_PROCESS_ATTACH)
        return TRUE;
    _snwprintf(Name, sizeof(Name) / sizeof(Name[0]), L"LoaderWaitAttaching-%lu", GetCurrentProcessId());
    Attaching = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name);
    _snwprintf(Name, sizeof(Name) / sizeof(Name[0]), L"LoaderWaitProceed-%lu", GetCurrentProcessId());
    Proceed = OpenEventW(SYNCHRONIZE, FALSE, Name);
    if (Attaching)
    {
        SetEvent(Attaching);
        CloseHandle(Attaching);
    }
    if (Proceed)
    {
        WaitForSingleObject(Proceed, 10000);
        CloseHandle(Proceed);
    }
    InterlockedExchange(&Initialized, 1);
    return TRUE;
}
