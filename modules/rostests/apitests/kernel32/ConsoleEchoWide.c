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

static COORD CursorPos(void)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    COORD Pos = { -1, -1 };

    if (GetConsoleScreenBufferInfo(Output, &Info))
        Pos = Info.dwCursorPosition;
    return Pos;
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

static COORD TypeAndWait(WCHAR Char, WORD Key)
{
    COORD From = CursorPos(), Pos = From;
    DWORD Tries;

    TypeChar(Char, Key);
    for (Tries = 0; Tries < 100 && Pos.X == From.X && Pos.Y == From.Y; Tries++)
    {
        Sleep(20);
        Pos = CursorPos();
    }
    return Pos;
}

static HANDLE StartRead(READ_LINE *Line)
{
    SetConsoleMode(Input, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
                          ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS);
    FlushConsoleInputBuffer(Input);
    return CreateThread(NULL, 0, ReadLineThread, Line, 0, NULL);
}

static void TestEcho(void)
{
    static const WCHAR Typed[] = { 0x65e5, 0x672c, L'a' };
    CONSOLE_SCREEN_BUFFER_INFO Info;
    READ_LINE Line = { { 0 } };
    WCHAR Cells[8];
    COORD Start, Pos;
    DWORD Read, Index;
    HANDLE Thread;

    WriteConsoleW(Output, L"\r\n> ", 4, &Read, NULL);
    ok(GetConsoleScreenBufferInfo(Output, &Info), "GetConsoleScreenBufferInfo failed with error %lu\n", GetLastError());
    Start = Info.dwCursorPosition;

    Thread = StartRead(&Line);
    ok(Thread != NULL, "CreateThread failed with error %lu\n", GetLastError());
    if (!Thread)
        return;

    Pos = Start;
    for (Index = 0; Index < ARRAYSIZE(Typed); Index++)
        Pos = TypeAndWait(Typed[Index], Typed[Index] == L'a' ? 'A' : 0);
    ok(Pos.X == Start.X + 5, "cursor moved %d cells\n", Pos.X - Start.X);

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
}

static void CheckCells(SHORT X, SHORT Y, DWORD CellCount, PCWSTR Expected, PCSTR Step)
{
    WCHAR Cells[8];
    COORD At;
    DWORD Read;
    DWORD Length = (DWORD)wcslen(Expected);

    At.X = X;
    At.Y = Y;
    ZeroMemory(Cells, sizeof(Cells));
    Read = 0;
    ok(ReadConsoleOutputCharacterW(Output, Cells, CellCount, At, &Read),
       "%s: ReadConsoleOutputCharacterW failed with error %lu\n", Step, GetLastError());
    ok(Read == Length && !memcmp(Cells, Expected, Length * sizeof(WCHAR)),
       "%s: the cells hold %lu characters %S\n", Step, Read, Cells);
}

static void TestWrap(void)
{
    static const struct
    {
        WCHAR Char;
        WORD Key;
        SHORT X;
        SHORT Row;
        PCSTR Name;
    } Steps[] =
    {
        { L'a',   'A',     -1, 0, "a" },
        { 0x65e5, 0,        2, 1, "wide" },
        { L'b',   'B',      3, 1, "b" },
        { L'\b',  VK_BACK,  2, 1, "backspace 1" },
        { L'\b',  VK_BACK, -1, 0, "backspace 2" },
        { 0x672c, 0,        2, 1, "wide 2" },
        { 0,      VK_LEFT, -1, 0, "left" },
        { L'c',   'C',      0, 1, "c" },
    };
    static const WCHAR Expected[] = { L'a', L'c', 0x672c, L'\r', L'\n' };
    CONSOLE_SCREEN_BUFFER_INFO Info;
    READ_LINE Line = { { 0 } };
    COORD Start, Pos;
    DWORD Written, Index;
    HANDLE Thread;

    WriteConsoleW(Output, L"\r\n\r\n\r\n", 6, &Written, NULL);
    ok(GetConsoleScreenBufferInfo(Output, &Info), "GetConsoleScreenBufferInfo failed with error %lu\n", GetLastError());
    Start.X = Info.dwSize.X - 2;
    Start.Y = Info.dwCursorPosition.Y - 2;
    ok(SetConsoleCursorPosition(Output, Start), "SetConsoleCursorPosition failed with error %lu\n", GetLastError());

    Thread = StartRead(&Line);
    ok(Thread != NULL, "CreateThread failed with error %lu\n", GetLastError());
    if (!Thread)
        return;

    for (Index = 0; Index < ARRAYSIZE(Steps); Index++)
    {
        SHORT X = Steps[Index].X < 0 ? Info.dwSize.X + Steps[Index].X : Steps[Index].X;

        Pos = TypeAndWait(Steps[Index].Char, Steps[Index].Key);
        ok(Pos.X == X && Pos.Y == Start.Y + Steps[Index].Row, "%s: cursor at %d,%d, expected %d,%d\n",
           Steps[Index].Name, Pos.X, Pos.Y - Start.Y, X, Steps[Index].Row);
        if (Index == 2)
            CheckCells(Start.X, Start.Y, 5, L"a \x65e5" L"b", Steps[Index].Name);
        else if (Index == 4)
            CheckCells(Start.X, Start.Y, 3, L"a  ", Steps[Index].Name);
    }
    CheckCells(Start.X, Start.Y, 4, L"ac\x672c", "c");

    TypeChar(L'\r', VK_RETURN);
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "ReadConsoleW did not return\n");
    CloseHandle(Thread);
    ok(Line.Result && Line.Count == ARRAYSIZE(Expected), "ReadConsoleW returned %lu characters\n", Line.Count);
    ok(!memcmp(Line.Buffer, Expected, sizeof(Expected)), "ReadConsoleW returned %S\n", Line.Buffer);
}

START_TEST(ConsoleEchoWide)
{
    DWORD Mode;

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

    GetConsoleMode(Input, &Mode);
    TestEcho();
    TestWrap();
    SetConsoleMode(Input, Mode);
    CloseHandle(Input);
    CloseHandle(Output);
}
