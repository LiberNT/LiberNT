/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the SQM session entry points of ntdll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HANDLE (NTAPI *FN_WinSqmStartSession)(GUID *, ULONG, ULONG);
typedef NTSTATUS (NTAPI *FN_WinSqmEndSession)(HANDLE);

START_TEST(WinSqmSession)
{
    static GUID SessionGuid = { 0x6d5c0a51, 0x1f2b, 0x4c3e, { 0x9a, 0x7d, 0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb } };
    HMODULE NtDll = GetModuleHandleW(L"ntdll.dll");
    FN_WinSqmStartSession pWinSqmStartSession;
    FN_WinSqmEndSession pWinSqmEndSession;
    HANDLE Session;
    NTSTATUS Status;

    pWinSqmStartSession = (FN_WinSqmStartSession)GetProcAddress(NtDll, "WinSqmStartSession");
    pWinSqmEndSession = (FN_WinSqmEndSession)GetProcAddress(NtDll, "WinSqmEndSession");
    if (!pWinSqmStartSession || !pWinSqmEndSession)
    {
        skip("WinSqm session exports are missing\n");
        return;
    }

    Session = pWinSqmStartSession(&SessionGuid, 1, 0);
    ok_ptr(Session, INVALID_HANDLE_VALUE);

    Session = pWinSqmStartSession(NULL, 0, 0);
    ok_ptr(Session, INVALID_HANDLE_VALUE);

    Status = pWinSqmEndSession(INVALID_HANDLE_VALUE);
    ok_ntstatus(Status, STATUS_NOT_IMPLEMENTED);
}
