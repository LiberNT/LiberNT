/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for StartServiceCtrlDispatcher with a table that names no service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
VOID
WINAPI
NeverCalledMainW(
    DWORD Argc,
    LPWSTR *Argv)
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);
}

START_TEST(StartServiceCtrlDispatcher)
{
    SERVICE_TABLE_ENTRYW EmptyW[] = { { NULL, NULL } };
    SERVICE_TABLE_ENTRYA EmptyA[] = { { NULL, NULL } };
    SERVICE_TABLE_ENTRYW NoMainW[] = { { L"LntNoMain", NULL }, { L"LntAfterEnd", NeverCalledMainW }, { NULL, NULL } };
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = StartServiceCtrlDispatcherW(EmptyW);
    ok(Result == FALSE, "StartServiceCtrlDispatcherW returned %d for an empty table\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = StartServiceCtrlDispatcherA(EmptyA);
    ok(Result == FALSE, "StartServiceCtrlDispatcherA returned %d for an empty table\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = StartServiceCtrlDispatcherW(NoMainW);
    ok(Result == FALSE, "StartServiceCtrlDispatcherW returned %d for a table ending at its first entry\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());
}
