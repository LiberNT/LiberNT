/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a console call on an input handle does not wait for a read blocked on that handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _SHARED_INPUT_CALL
{
    HANDLE Input;
    BOOL Success;
    DWORD Value;
    DWORD Error;
} SHARED_INPUT_CALL;

static
DWORD
WINAPI
BlockedReadThread(
    _In_ PVOID Parameter)
{
    SHARED_INPUT_CALL *Call = Parameter;
    INPUT_RECORD Record;

    Call->Success = ReadConsoleInputW(Call->Input, &Record, 1, &Call->Value);
    Call->Error = GetLastError();
    return 0;
}

static
DWORD
WINAPI
ModeThread(
    _In_ PVOID Parameter)
{
    SHARED_INPUT_CALL *Call = Parameter;

    Call->Success = GetConsoleMode(Call->Input, &Call->Value);
    Call->Error = GetLastError();
    return 0;
}

START_TEST(ConsoleReadSharedInput)
{
    SHARED_INPUT_CALL ReadCall = { 0 }, ModeCall = { 0 };
    HANDLE Input, Second, Reader, Caller;
    INPUT_RECORD Record;
    BOOL Allocated = FALSE;
    DWORD Count;
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

    Second = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    ok(Second != INVALID_HANDLE_VALUE, "Opening CONIN$ again failed with error %lu\n", GetLastError());
    if (Second == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Input);
        return;
    }

    ok(FlushConsoleInputBuffer(Second), "FlushConsoleInputBuffer failed with error %lu\n", GetLastError());

    ReadCall.Input = Input;
    Reader = CreateThread(NULL, 0, BlockedReadThread, &ReadCall, 0, NULL);
    ok(Reader != NULL, "CreateThread failed with error %lu\n", GetLastError());
    ok_long(WaitForSingleObject(Reader, 50), WAIT_TIMEOUT);

    ModeCall.Input = Input;
    Caller = CreateThread(NULL, 0, ModeThread, &ModeCall, 0, NULL);
    ok(Caller != NULL, "CreateThread failed with error %lu\n", GetLastError());
    ok(WaitForSingleObject(Caller, 1000) == WAIT_OBJECT_0, "GetConsoleMode waits for the read blocked on the same handle\n");
    ok_long(WaitForSingleObject(Reader, 0), WAIT_TIMEOUT);

    ZeroMemory(&Record, sizeof(Record));
    Record.EventType = KEY_EVENT;
    Record.Event.KeyEvent.bKeyDown = TRUE;
    Record.Event.KeyEvent.wRepeatCount = 1;
    Record.Event.KeyEvent.uChar.UnicodeChar = L'a';
    Count = 0;
    Success = WriteConsoleInputW(Second, &Record, 1, &Count);
    ok(Success && Count == 1, "WriteConsoleInputW wrote %lu records, error %lu\n", Count, GetLastError());

    ok_long(WaitForSingleObject(Reader, 1000), WAIT_OBJECT_0);
    ok(ReadCall.Success && ReadCall.Value == 1, "ReadConsoleInputW read %lu records, error %lu\n", ReadCall.Value, ReadCall.Error);
    ok_long(WaitForSingleObject(Caller, 1000), WAIT_OBJECT_0);
    ok(ModeCall.Success, "GetConsoleMode failed with error %lu\n", ModeCall.Error);

    CloseHandle(Caller);
    CloseHandle(Reader);
    CloseHandle(Second);
    CloseHandle(Input);
    if (Allocated)
        FreeConsole();
}
