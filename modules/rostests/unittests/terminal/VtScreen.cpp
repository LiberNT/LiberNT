/*
 * PROJECT:     LiberNT Terminal - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the terminal VT screen against a console buffer with VT processing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <string>

#include "vt.h"

#define COLS 20
#define ROWS 5

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

struct VtCase
{
    const char *Input;
    const char *Rows[ROWS];
    int CursorX;
    int CursorY;
    int ColorX;
    int ColorY;
    int Foreground;
};

static const VtCase Cases[] =
{
    { "abc", { "abc" }, 3, 0, -1, 0, 0 },
    { "abc\r\ndef", { "abc", "def" }, 3, 1, -1, 0, 0 },
    { "\x1b[3;5Hx", { "", "", "    x" }, 5, 2, -1, 0, 0 },
    { "abcdef\x1b[3D\x1b[K", { "abc" }, 3, 0, -1, 0, 0 },
    { "12345\x1b[1;2H\x1b[2P", { "145" }, 1, 0, -1, 0, 0 },
    { "12345\x1b[1;2H\x1b[2@", { "1  2345" }, 1, 0, -1, 0, 0 },
    { "abcdefghijklmnopqrstuvwxy", { "abcdefghijklmnopqrst", "uvwxy" }, 5, 1, -1, 0, 0 },
    { "\x1b[31mR\x1b[mN", { "RN" }, 2, 0, 0, 0, 1 },
    { "a\r\nb\r\nc\x1b[2;1H\x1b[J", { "a" }, 0, 1, -1, 0, 0 },
    { "1\r\n2\r\n3\r\n4\r\n5\r\n6", { "2", "3", "4", "5", "6" }, 1, 4, -1, 0, 0 },
    { "abcdefghij\x1b[1;3H\x1b[4X", { "ab    ghij" }, 2, 0, -1, 0, 0 },
    { "\x1b[2;4r\x1b[4;1Ha\nb", { "", "", "a", " b" }, 2, 3, -1, 0, 0 },
};

static std::string Trim(const std::wstring &text)
{
    std::string out;

    for (wchar_t ch : text)
        out += ch < 0x80 ? (char)ch : '?';
    while (!out.empty() && out[out.size() - 1] == ' ')
        out.pop_back();
    return out;
}

static void CheckEngine(size_t index, const VtCase &test)
{
    Vt::Screen screen(COLS, ROWS, 100);

    screen.Write(test.Input, strlen(test.Input));
    for (int row = 0; row < ROWS; row++)
    {
        const Vt::Line &line = screen.LineAt(screen.HistorySize() + row);
        std::wstring text;

        for (const Vt::Cell &cell : line.Cells)
            text += cell.Ch < 0x10000 ? (wchar_t)cell.Ch : L'?';
        ok(Trim(text) == (test.Rows[row] ? test.Rows[row] : ""), "engine case %u row %d: '%s'\n", (unsigned)index, row,
           Trim(text).c_str());
    }
    ok(screen.CursorX() == test.CursorX && screen.CursorY() == test.CursorY, "engine case %u cursor %d,%d\n",
       (unsigned)index, screen.CursorX(), screen.CursorY());
    if (test.ColorX >= 0)
    {
        const Vt::Cell &cell = screen.LineAt(screen.HistorySize() + test.ColorY).Cells[test.ColorX];
        const Vt::Cell &next = screen.LineAt(screen.HistorySize() + test.ColorY).Cells[test.ColorX + 1];

        ok(cell.Attr.Foreground == Vt::IndexedColor(test.Foreground), "engine case %u color 0x%x\n", (unsigned)index,
           cell.Attr.Foreground);
        ok(next.Attr.Foreground == Vt::ColorDefault, "engine case %u next color 0x%x\n", (unsigned)index,
           next.Attr.Foreground);
    }
}

static HANDLE CreateVtBuffer(void)
{
    HANDLE buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                              CONSOLE_TEXTMODE_BUFFER, NULL);
    SMALL_RECT window = { 0, 0, COLS - 1, ROWS - 1 };
    COORD size = { COLS, ROWS };

    if (buffer == INVALID_HANDLE_VALUE)
        return NULL;
    if (!SetConsoleWindowInfo(buffer, TRUE, &window) || !SetConsoleScreenBufferSize(buffer, size) ||
        !SetConsoleMode(buffer, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                                ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN))
    {
        CloseHandle(buffer);
        return NULL;
    }
    return buffer;
}

static void CheckConsole(size_t index, const VtCase &test)
{
    HANDLE buffer = CreateVtBuffer();
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD written;

    if (!buffer)
    {
        skip("case %u: no VT console buffer (%lu)\n", (unsigned)index, GetLastError());
        return;
    }
    WriteConsoleA(buffer, test.Input, (DWORD)strlen(test.Input), &written, NULL);
    for (int row = 0; row < ROWS; row++)
    {
        wchar_t text[COLS + 1] = L"";
        COORD origin = { 0, (SHORT)row };
        DWORD read = 0;

        ReadConsoleOutputCharacterW(buffer, text, COLS, origin, &read);
        text[read] = 0;
        ok(Trim(text) == (test.Rows[row] ? test.Rows[row] : ""), "console case %u row %d: '%s'\n", (unsigned)index, row,
           Trim(text).c_str());
    }
    ok(GetConsoleScreenBufferInfo(buffer, &info), "GetConsoleScreenBufferInfo failed with %lu\n", GetLastError());
    ok(info.dwCursorPosition.X == test.CursorX && info.dwCursorPosition.Y == test.CursorY,
       "console case %u cursor %d,%d\n", (unsigned)index, info.dwCursorPosition.X, info.dwCursorPosition.Y);
    if (test.ColorX >= 0)
    {
        static const WORD Ansi[8] = { 0, FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_RED | FOREGROUND_GREEN,
                                      FOREGROUND_BLUE, FOREGROUND_RED | FOREGROUND_BLUE,
                                      FOREGROUND_GREEN | FOREGROUND_BLUE, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE };
        WORD attributes[2] = {};
        COORD origin = { (SHORT)test.ColorX, (SHORT)test.ColorY };
        DWORD read = 0;

        ReadConsoleOutputAttribute(buffer, attributes, 2, origin, &read);
        ok((attributes[0] & 0x0F) == Ansi[test.Foreground], "console case %u color 0x%x\n", (unsigned)index,
           attributes[0]);
        ok((attributes[1] & 0x0F) != Ansi[test.Foreground], "console case %u next color 0x%x\n", (unsigned)index,
           attributes[1]);
    }
    CloseHandle(buffer);
}

START_TEST(VtScreen)
{
    if (!GetConsoleWindow() && !AllocConsole())
        trace("AllocConsole failed with %lu\n", GetLastError());
    for (size_t i = 0; i < ARRAYSIZE(Cases); i++)
    {
        CheckEngine(i, Cases[i]);
        CheckConsole(i, Cases[i]);
    }
}
