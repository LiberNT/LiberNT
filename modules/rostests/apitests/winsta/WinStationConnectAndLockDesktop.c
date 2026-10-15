/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for WinStationConnectAndLockDesktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

typedef BOOLEAN (WINAPI *PFN_CONNECTANDLOCK)(HANDLE, ULONG);

START_TEST(WinStationConnectAndLockDesktop)
{
    HMODULE WinSta = LoadLibraryW(L"winsta.dll");
    PFN_CONNECTANDLOCK pWinStationConnectAndLockDesktop;
    DWORD SessionId = 0;
    BOOLEAN Result;

    ok(WinSta != NULL, "winsta.dll is missing\n");
    if (!WinSta)
        return;

    pWinStationConnectAndLockDesktop = (PFN_CONNECTANDLOCK)GetProcAddress(WinSta, "WinStationConnectAndLockDesktop");
    ok(pWinStationConnectAndLockDesktop != NULL, "WinStationConnectAndLockDesktop is missing\n");
    if (!pWinStationConnectAndLockDesktop)
        return;

    SetLastError(0xdeadbeef);
    Result = pWinStationConnectAndLockDesktop(NULL, 12345);
    ok(!Result && GetLastError() == ERROR_FILE_NOT_FOUND, "unknown session: %d %lu\n", Result, GetLastError());

    SetLastError(0xdeadbeef);
    Result = pWinStationConnectAndLockDesktop(NULL, 0);
    ok(!Result && GetLastError() == ERROR_NOT_SUPPORTED, "session 0: %d %lu\n", Result, GetLastError());

    ok(ProcessIdToSessionId(GetCurrentProcessId(), &SessionId), "ProcessIdToSessionId failed with %lu\n", GetLastError());
    if (SessionId == 0)
    {
        skip("Running in session 0\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Result = pWinStationConnectAndLockDesktop(NULL, SessionId);
    ok(!Result && GetLastError() == ERROR_ACCESS_DENIED, "current session %lu: %d %lu\n", SessionId, Result, GetLastError());
}
