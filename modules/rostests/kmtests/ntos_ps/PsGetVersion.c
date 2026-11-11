/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PsGetVersion reports the same version as RtlGetVersion and the shared user page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(PsGetVersion)
{
    RTL_OSVERSIONINFOW Info;
    ULONG Major = 0, Minor = 0, Build = 0;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.dwOSVersionInfoSize = sizeof(Info);
    ok_eq_hex(RtlGetVersion(&Info), STATUS_SUCCESS);

    PsGetVersion(&Major, &Minor, &Build, NULL);
    ok_eq_ulong(Major, Info.dwMajorVersion);
    ok_eq_ulong(Minor, Info.dwMinorVersion);
    ok_eq_ulong(Build, Info.dwBuildNumber);
    ok_eq_ulong(Build, (ULONG)SharedUserData->NtBuildNumber);
}
