/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the echo of wide characters by a cooked console read
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winuser.h>

static HANDLE Input, Output;

typedef struct _READ_LINE
{
    WCHAR Buffer[64];
    DWORD Count;
    BOOL Result;
} READ_LINE;

static DWORD WINAPI ReadLineThread(PVOID Parameter)
{
    READ_LINE *Line = Parameter;

    Line->Result = ReadConsoleW(Input, Line->Buffer, ARRAYSIZE(Line->Buffer), &Line->Count, NULL);
    return 0;
}

static SHORT CursorX(void)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;

    if (!GetConsoleScreenBufferInfo(Output, &Info))
        return -1;
    return Info.dwCursorPosition.X;
}

static void TypeChar(WCHAR Char, WORD Key)
{
    INPUT_RECORD Records[2];
    DWORD Written;

    ZeroMemory(Records, sizeof(Records));
    Records[0].EventType = KEY_EVENT;
    Records[0].Event.KeyEvent.bKeyDown = TRUE;
    Records[0].Event.KeyEvent.wRepeatCount = 1;
    Records[0].Event.KeyEvent.wVirtualKeyCode = Key;
    Records[0].Event.KeyEvent.uChar.UnicodeChar = Char;
    Records[1] = Records[0];
    Records[1].Event.KeyEvent.bKeyDown = FALSE;
    ok(WriteConsoleInputW(Input, Records, 2, &Written) && Written == 2,
       "WriteConsoleInputW failed with error %lu\n", GetLastError());
}

static SHORT WaitCursorMove(SHORT From)
{
    SHORT X = From;
    DWORD Tries;

    for (Tries = 0; Tries < 100 && X == From; Tries++)
    {
        Sleep(20);
        X = CursorX();
    }
    return X;
}

START_TEST(ConsoleEchoWide)
{
    static const WCHAR Typed[] = { 0x65e5, 0x672c, L'a' };
    CONSOLE_SCREEN_BUFFER_INFO Info;
    READ_LINE Line = { { 0 } };
    WCHAR Cells[8];
    COORD Start;
    DWORD Mode, Read, Index;
    HANDLE Thread;
    SHORT X;

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE || Output == INVALID_HANDLE_VALUE)
    {
        skip("No console\n");
        return;
    }
    trace("output code page %u\n", GetConsoleOutputCP());

    WriteConsoleW(Output, L"\r\n> ", 4, &Read, NULL);
    ok(GetConsoleScreenBufferInfo(Output, &Info), "GetConsoleScreenBufferInfo failed with error %lu\n", GetLastError());
    Start = Info.dwCursorPosition;

    GetConsoleMode(Input, &Mode);
    SetConsoleMode(Input, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    FlushConsoleInputBuffer(Input);
    Thread = CreateThread(NULL, 0, ReadLineThread, &Line, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with error %lu\n", GetLastError());
    if (!Thread)
    {
        SetConsoleMode(Input, Mode);
        return;
    }

    X = Start.X;
    for (Index = 0; Index < ARRAYSIZE(Typed); Index++)
    {
        TypeChar(Typed[Index], Typed[Index] == L'a' ? 'A' : 0);
        X = WaitCursorMove(X);
    }
    ok(X == Start.X + 5, "cursor moved %d cells\n", X - Start.X);

    ZeroMemory(Cells, sizeof(Cells));
    Read = 0;
    ok(ReadConsoleOutputCharacterW(Output, Cells, 5, Start, &Read),
       "ReadConsoleOutputCharacterW failed with error %lu\n", GetLastError());
    for (Index = 0; Index < Read; Index++)
        trace("cell %lu: 0x%x\n", Index, Cells[Index]);
    ok(Read == 3, "ReadConsoleOutputCharacterW read %lu characters\n", Read);
    ok(!memcmp(Cells, Typed, sizeof(Typed)), "the cells hold %S\n", Cells);

    TypeChar(L'\r', VK_RETURN);
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "ReadConsoleW did not return\n");
    CloseHandle(Thread);
    ok(Line.Result && Line.Count == 5, "ReadConsoleW returned %lu characters\n", Line.Count);
    ok(!memcmp(Line.Buffer, Typed, sizeof(Typed)), "ReadConsoleW returned %S\n", Line.Buffer);

    SetConsoleMode(Input, Mode);
    CloseHandle(Input);
    CloseHandle(Output);
}
