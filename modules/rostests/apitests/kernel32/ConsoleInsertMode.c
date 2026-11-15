/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the insert mode changes only with ENABLE_EXTENDED_FLAGS
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winuser.h>

#define COOKED_MODE (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)

static HANDLE Input;

typedef struct _READ_LINE
{
    WCHAR Buffer[16];
    DWORD Count;
    BOOL Result;
} READ_LINE;

static DWORD WINAPI ReadLineThread(PVOID Parameter)
{
    READ_LINE *Line = Parameter;

    Line->Result = ReadConsoleW(Input, Line->Buffer, ARRAYSIZE(Line->Buffer) - 1, &Line->Count, NULL);
    return 0;
}

static void TypeKeys(PCWSTR Chars, const WORD *Keys, DWORD Count)
{
    INPUT_RECORD Records[16];
    DWORD Index, Written;

    ZeroMemory(Records, sizeof(Records));
    for (Index = 0; Index < Count; Index++)
    {
        Records[Index * 2].EventType = KEY_EVENT;
        Records[Index * 2].Event.KeyEvent.bKeyDown = TRUE;
        Records[Index * 2].Event.KeyEvent.wRepeatCount = 1;
        Records[Index * 2].Event.KeyEvent.wVirtualKeyCode = Keys[Index];
        Records[Index * 2].Event.KeyEvent.uChar.UnicodeChar = Chars[Index];
        Records[Index * 2 + 1] = Records[Index * 2];
        Records[Index * 2 + 1].Event.KeyEvent.bKeyDown = FALSE;
    }
    ok(WriteConsoleInputW(Input, Records, Count * 2, &Written) && Written == Count * 2,
       "WriteConsoleInputW failed with error %lu\n", GetLastError());
}

static void ReadEdited(PCWSTR Expected, PCSTR Step)
{
    static const WORD Keys[] = { 'A', 'B', VK_LEFT, 'C', VK_RETURN };
    READ_LINE Line = { { 0 } };
    HANDLE Thread;
    DWORD Length = (DWORD)wcslen(Expected);

    FlushConsoleInputBuffer(Input);
    Thread = CreateThread(NULL, 0, ReadLineThread, &Line, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed with error %lu\n", Step, GetLastError());
    if (!Thread)
        return;
    TypeKeys(L"ab\0c\r", Keys, ARRAYSIZE(Keys));
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: ReadConsoleW did not return\n", Step);
    CloseHandle(Thread);
    Line.Buffer[min(Line.Count, ARRAYSIZE(Line.Buffer) - 1)] = 0;
    trace("%s: %lu characters %S\n", Step, Line.Count, Line.Buffer);
    ok(Line.Result && Line.Count == Length + 2 && !wcsncmp(Line.Buffer, Expected, Length),
       "%s: ReadConsoleW returned %lu characters %S, expected %S\n", Step, Line.Count, Line.Buffer, Expected);
}

static void SetAndCheckMode(DWORD Set, DWORD Expected, PCSTR Step)
{
    DWORD Mode = 0xdeadbeef;

    ok(SetConsoleMode(Input, Set), "%s: SetConsoleMode(0x%lx) failed with error %lu\n", Step, Set, GetLastError());
    ok(GetConsoleMode(Input, &Mode), "%s: GetConsoleMode failed with error %lu\n", Step, GetLastError());
    trace("%s: set 0x%lx, got 0x%lx\n", Step, Set, Mode);
    ok(Mode == Expected, "%s: mode 0x%lx, expected 0x%lx\n", Step, Mode, Expected);
}

START_TEST(ConsoleInsertMode)
{
    DWORD Mode;

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE)
    {
        skip("No console\n");
        return;
    }
    ok(GetConsoleMode(Input, &Mode), "GetConsoleMode failed with error %lu\n", GetLastError());
    trace("initial mode 0x%lx\n", Mode);

    SetAndCheckMode(COOKED_MODE | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS,
                    COOKED_MODE | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS, "insert on");
    ReadEdited(L"acb", "insert on");
    SetAndCheckMode(COOKED_MODE, COOKED_MODE, "no flags after insert on");
    ReadEdited(L"acb", "no flags after insert on");
    SetAndCheckMode(COOKED_MODE | ENABLE_EXTENDED_FLAGS, COOKED_MODE | ENABLE_EXTENDED_FLAGS, "insert off");
    ReadEdited(L"ac", "insert off");
    SetAndCheckMode(COOKED_MODE, COOKED_MODE, "no flags after insert off");
    ReadEdited(L"ac", "no flags after insert off");
    SetAndCheckMode(COOKED_MODE | ENABLE_INSERT_MODE, COOKED_MODE | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS,
                    "insert without extended flags");
    ReadEdited(L"acb", "insert without extended flags");

    SetConsoleMode(Input, Mode | ENABLE_EXTENDED_FLAGS);
    CloseHandle(Input);
}
