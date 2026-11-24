/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that CheckElevationEnabled reports the shared elevation flag
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#ifndef SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED
#define SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED (1UL << 1)
#endif

typedef DWORD (WINAPI *PFN_CHECK_ELEVATION_ENABLED)(PBOOL);

START_TEST(CheckElevationEnabled)
{
    PFN_CHECK_ELEVATION_ENABLED pCheckElevationEnabled;
    BOOL Enabled, Again, Shared;
    DWORD Result;
    NTSTATUS Code;

    pCheckElevationEnabled = (PFN_CHECK_ELEVATION_ENABLED)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                         "CheckElevationEnabled");
    if (!pCheckElevationEnabled)
    {
        skip("CheckElevationEnabled is not exported\n");
        return;
    }

    Shared = (SharedUserData->SharedDataFlags & SHARED_GLOBAL_FLAGS_ELEVATION_ENABLED) != 0;
    trace("SharedDataFlags 0x%08lx\n", SharedUserData->SharedDataFlags);

    Enabled = (BOOL)0xCCCCCCCC;
    SetLastError(0xdeadbeef);
    Result = pCheckElevationEnabled(&Enabled);
    ok_long(Result, ERROR_SUCCESS);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(Enabled, Shared);

    Again = (BOOL)0x55555555;
    Result = pCheckElevationEnabled(&Again);
    ok_long(Result, ERROR_SUCCESS);
    ok_long(Again, Enabled);

    Code = STATUS_SUCCESS;
    Result = 0xdeadbeef;
    _SEH2_TRY
    {
        Result = pCheckElevationEnabled(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_hex(Code, STATUS_ACCESS_VIOLATION);
    ok_long(Result, 0xdeadbeef);
}
