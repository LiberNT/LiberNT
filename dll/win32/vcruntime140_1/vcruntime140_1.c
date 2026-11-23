/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VC++ 2019+ exception handler module
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

BOOL msvcrt_init_handler4(void);
void msvcrt_free_handler4(void);

BOOL
WINAPI
DllMainCRTStartup(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            return msvcrt_init_handler4();

        case DLL_PROCESS_DETACH:
            if (Reserved == NULL)
                msvcrt_free_handler4();
            break;
    }

    return TRUE;
}
