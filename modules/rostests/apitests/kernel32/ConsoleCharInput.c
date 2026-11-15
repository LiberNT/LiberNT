/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the input records the console window makes from characters without a key
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winuser.h>

static DWORD ReadRecords(HANDLE Input, INPUT_RECORD *Records, DWORD Max)
{
    DWORD Total = 0, Count;

    while (Total < Max && WaitForSingleObject(Input, Total ? 200 : 2000) == WAIT_OBJECT_0)
    {
        Count = 0;
        if (!ReadConsoleInputW(Input, Records + Total, Max - Total, &Count) || !Count)
            break;
        Total += Count;
    }
    return Total;
}

static void TestMessage(HWND Window, HANDLE Input, UINT Message, WCHAR Char, const char *Name)
{
    INPUT_RECORD Records[8];
    const KEY_EVENT_RECORD *Key = &Records[0].Event.KeyEvent;
    DWORD Count;

    FlushConsoleInputBuffer(Input);
    ok(PostMessageW(Window, Message, Char, 1), "%s: PostMessageW failed with error %lu\n", Name, GetLastError());
    Count = ReadRecords(Input, Records, ARRAYSIZE(Records));

    ok(Count == 1, "%s: %lu records\n", Name, Count);
    if (!Count)
        return;
    ok(Records[0].EventType == KEY_EVENT, "%s: event type %u\n", Name, Records[0].EventType);
    if (Records[0].EventType != KEY_EVENT)
        return;
    ok(Key->bKeyDown == TRUE, "%s: bKeyDown %d\n", Name, Key->bKeyDown);
    ok(Key->wRepeatCount == 1, "%s: wRepeatCount %u\n", Name, Key->wRepeatCount);
    ok(Key->wVirtualKeyCode == 0, "%s: wVirtualKeyCode 0x%x\n", Name, Key->wVirtualKeyCode);
    ok(Key->wVirtualScanCode == 0, "%s: wVirtualScanCode 0x%x\n", Name, Key->wVirtualScanCode);
    ok(Key->uChar.UnicodeChar == Char, "%s: UnicodeChar 0x%x\n", Name, Key->uChar.UnicodeChar);
    ok(Key->dwControlKeyState == 0, "%s: dwControlKeyState 0x%lx\n", Name, Key->dwControlKeyState);
}

START_TEST(ConsoleCharInput)
{
    HANDLE Input;
    HWND Window;
    DWORD Mode;

    Window = GetConsoleWindow();
    if (!Window)
    {
        skip("No console window\n");
        return;
    }

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    ok(Input != INVALID_HANDLE_VALUE, "Opening CONIN$ failed with error %lu\n", GetLastError());
    if (Input == INVALID_HANDLE_VALUE)
        return;

    GetConsoleMode(Input, &Mode);
    SetConsoleMode(Input, Mode & ~ENABLE_VIRTUAL_TERMINAL_INPUT);

    TestMessage(Window, Input, WM_CHAR, L'a', "WM_CHAR a");
    TestMessage(Window, Input, WM_CHAR, 0x3042, "WM_CHAR U+3042");
    TestMessage(Window, Input, WM_IME_CHAR, 0x3044, "WM_IME_CHAR U+3044");

    SetConsoleMode(Input, Mode);
    FlushConsoleInputBuffer(Input);
    CloseHandle(Input);
}
