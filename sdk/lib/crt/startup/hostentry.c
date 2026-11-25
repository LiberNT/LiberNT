/*
 * PROJECT:     LiberNT CRT startup
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry point that lets a DLL with its own entry point load only on LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

int __cdecl __libernt_host_check(void);

#ifdef LIBERNT_HOST_ENTRY
BOOL WINAPI LIBERNT_HOST_ENTRY(HINSTANCE Instance, DWORD Reason, LPVOID Reserved);
#endif

BOOL WINAPI
__libernt_host_entry(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    static BOOL Refused;

    if (Reason == DLL_PROCESS_ATTACH && !__libernt_host_check())
        Refused = TRUE;
    if (Refused)
        return FALSE;

#ifdef LIBERNT_HOST_ENTRY
    return LIBERNT_HOST_ENTRY(Instance, Reason, Reserved);
#else
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
#endif
}
