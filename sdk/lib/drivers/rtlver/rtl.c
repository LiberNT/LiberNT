/*
 * PROJECT:     LiberNT Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTDDI version check for drivers built against the WDM library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>

BOOLEAN
WdmlibRtlIsNtDdiVersionAvailable(
    _In_ ULONG Version)
{
    RTL_OSVERSIONINFOEXW Information;
    ULONG Running;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.dwOSVersionInfoSize = sizeof(Information);
    if (!NT_SUCCESS(RtlGetVersion((PRTL_OSVERSIONINFOW)&Information)))
        return FALSE;

    Running = (Information.dwMajorVersion << 24) |
              (Information.dwMinorVersion << 16) |
              ((ULONG)Information.wServicePackMajor << 8) |
              Information.wServicePackMinor;

    return Version <= Running;
}
