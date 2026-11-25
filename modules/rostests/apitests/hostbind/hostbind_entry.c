/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DLL with its own entry point, bound to LiberNT by the build
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

__declspec(dllexport) LONG HostbindAttached;

BOOL
WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason == DLL_PROCESS_ATTACH)
        HostbindAttached++;
    return TRUE;
}
