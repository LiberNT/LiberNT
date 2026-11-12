/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A window station open or create that is refused reports why through GetLastError
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(WindowStationErrors)
{
    HWINSTA hWinSta;
    DWORD Error;

    if (!ImpersonateAnonymousToken(GetCurrentThread()))
    {
        skip("ImpersonateAnonymousToken failed %lu\n", GetLastError());
        return;
    }

    SetLastError(0xdeadbeef);
    hWinSta = OpenWindowStationW(L"WinSta0", FALSE, WINSTA_ENUMDESKTOPS);
    Error = GetLastError();
    ok(hWinSta == NULL && Error == ERROR_ACCESS_DENIED, "anonymous open of WinSta0: %p, error %lu\n", hWinSta, Error);
    if (hWinSta != NULL)
        CloseWindowStation(hWinSta);

    SetLastError(0xdeadbeef);
    hWinSta = CreateWindowStationW(L"LiberNTAnonymousTestStation", 0, WINSTA_ALL_ACCESS, NULL);
    Error = GetLastError();
    ok(hWinSta == NULL && Error == ERROR_ACCESS_DENIED, "anonymous create: %p, error %lu\n", hWinSta, Error);
    if (hWinSta != NULL)
        CloseWindowStation(hWinSta);

    ok(RevertToSelf(), "RevertToSelf failed %lu\n", GetLastError());
}
