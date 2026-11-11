/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the version fields of KUSER_SHARED_DATA
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(SharedUserData)
{
    PPEB Peb = NtCurrentPeb();
    RTL_OSVERSIONINFOEXW Version;

    ok_dec(SharedUserData->NtMajorVersion, Peb->OSMajorVersion);
    ok_dec(SharedUserData->NtMinorVersion, Peb->OSMinorVersion);
    ok_dec(SharedUserData->NtBuildNumber, Peb->OSBuildNumber);
    ok(SharedUserData->NtBuildNumber >= 10240, "NtBuildNumber is %lu\n", SharedUserData->NtBuildNumber);

    ok_hex(SharedUserData->SuiteMask, VER_SUITE_TERMINAL | VER_SUITE_SINGLEUSERTS);
    RtlZeroMemory(&Version, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
    ok_ntstatus(RtlGetVersion((PRTL_OSVERSIONINFOW)&Version), STATUS_SUCCESS);
    ok_hex(Version.wSuiteMask, VER_SUITE_SINGLEUSERTS);
}
