/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that back-to-back console requests each get their own complete answer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SYNCHRONOUS_ROUNDS 5000
#define OVERLAPPED_ROUNDS 500

static
ULONG
QueryRounds(HANDLE Output, const CONSOLE_SCREEN_BUFFER_INFO *First, ULONG Rounds, const char *Name)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    CONSOLE_CURSOR_INFO Cursor;
    ULONG Round, Bad = 0;

    for (Round = 0; Round < Rounds; Round++)
    {
        memset(&Info, 0xcc, sizeof(Info));
        SetLastError(0xdeadbeef);
        if (!GetConsoleScreenBufferInfo(Output, &Info) ||
            Info.dwSize.X != First->dwSize.X || Info.dwSize.Y != First->dwSize.Y)
        {
            if (Bad++ < 3)
                trace("%s round %lu: screen buffer info error %lu, size %dx%d\n",
                      Name, Round, GetLastError(), Info.dwSize.X, Info.dwSize.Y);
        }
        memset(&Cursor, 0xcc, sizeof(Cursor));
        SetLastError(0xdeadbeef);
        if (!GetConsoleCursorInfo(Output, &Cursor) || Cursor.dwSize == 0 || Cursor.dwSize > 100)
        {
            if (Bad++ < 3)
                trace("%s round %lu: cursor info error %lu, size %lu\n", Name, Round, GetLastError(), Cursor.dwSize);
        }
    }
    return Bad;
}

START_TEST(ConsoleRequestStress)
{
    CONSOLE_SCREEN_BUFFER_INFO First;
    BOOL Allocated = FALSE;
    HANDLE Output;

    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Output == INVALID_HANDLE_VALUE && AllocConsole())
    {
        Allocated = TRUE;
        Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    }
    ok(Output != INVALID_HANDLE_VALUE, "Opening CONOUT$ failed with error %lu\n", GetLastError());
    if (Output == INVALID_HANDLE_VALUE)
        return;

    ok(GetConsoleScreenBufferInfo(Output, &First), "GetConsoleScreenBufferInfo failed with error %lu\n", GetLastError());
    ok_long(QueryRounds(Output, &First, SYNCHRONOUS_ROUNDS, "synchronous"), 0);
    CloseHandle(Output);

    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    ok(Output != INVALID_HANDLE_VALUE, "Opening CONOUT$ for overlapped I/O failed with error %lu\n", GetLastError());
    if (Output != INVALID_HANDLE_VALUE)
    {
        ok_long(QueryRounds(Output, &First, OVERLAPPED_ROUNDS, "overlapped"), 0);
        CloseHandle(Output);
    }

    if (Allocated)
        FreeConsole();
}
