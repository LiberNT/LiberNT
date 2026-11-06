/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a console input handle stays signaled while records remain after a read through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(ConsoleInputSignal)
{
    INPUT_RECORD Records[3], Read[3];
    BOOL Allocated = FALSE;
    DWORD Count, Index;
    HANDLE Input;
    BOOL Success;

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE && AllocConsole())
    {
        Allocated = TRUE;
        Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);
    }
    ok(Input != INVALID_HANDLE_VALUE, "Opening CONIN$ failed with error %lu\n", GetLastError());
    if (Input == INVALID_HANDLE_VALUE)
        return;

    ok(FlushConsoleInputBuffer(Input), "FlushConsoleInputBuffer failed with error %lu\n", GetLastError());
    ok_long(WaitForSingleObject(Input, 0), WAIT_TIMEOUT);

    ZeroMemory(Records, sizeof(Records));
    for (Index = 0; Index < ARRAYSIZE(Records); Index++)
    {
        Records[Index].EventType = KEY_EVENT;
        Records[Index].Event.KeyEvent.bKeyDown = TRUE;
        Records[Index].Event.KeyEvent.wRepeatCount = 1;
        Records[Index].Event.KeyEvent.uChar.UnicodeChar = (WCHAR)(L'a' + Index);
    }
    Count = 0;
    Success = WriteConsoleInputW(Input, Records, ARRAYSIZE(Records), &Count);
    ok(Success && Count == ARRAYSIZE(Records), "WriteConsoleInputW wrote %lu records, error %lu\n", Count, GetLastError());
    ok_long(WaitForSingleObject(Input, 0), WAIT_OBJECT_0);

    Count = 0;
    Success = ReadConsoleInputW(Input, Read, 1, &Count);
    ok(Success && Count == 1, "ReadConsoleInputW read %lu records, error %lu\n", Count, GetLastError());
    ok(WaitForSingleObject(Input, 0) == WAIT_OBJECT_0, "The handle is not signaled with two records left\n");

    Count = 0;
    Success = ReadConsoleInputW(Input, Read, ARRAYSIZE(Read), &Count);
    ok(Success && Count == 2, "ReadConsoleInputW read %lu records, error %lu\n", Count, GetLastError());
    if (Success && Count == 2)
        ok(Read[1].Event.KeyEvent.uChar.UnicodeChar == L'c', "Last record is 0x%x\n", Read[1].Event.KeyEvent.uChar.UnicodeChar);
    ok(WaitForSingleObject(Input, 0) == WAIT_TIMEOUT, "The handle is signaled with no record left\n");

    CloseHandle(Input);
    if (Allocated)
        FreeConsole();
}
