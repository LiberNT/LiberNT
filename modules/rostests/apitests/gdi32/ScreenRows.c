/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every screen row keeps its columns from GDI to scanout
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define BLOCK 32
#define SAMPLE_STEP 3

static COLORREF
BlockColor(int Column, int Row)
{
    return RGB((Column * 37) & 0xFF, (Row * 53) & 0xFF, 0x40 | ((Column + Row) & 1) << 7);
}

static LRESULT CALLBACK
WindowProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        RECT Client;
        HDC hdc = BeginPaint(hwnd, &Paint);
        int Column, Row;

        GetClientRect(hwnd, &Client);
        for (Row = 0; Row * BLOCK < Client.bottom; ++Row)
        {
            for (Column = 0; Column * BLOCK < Client.right; ++Column)
            {
                RECT Block = {Column * BLOCK, Row * BLOCK, (Column + 1) * BLOCK, (Row + 1) * BLOCK};
                HBRUSH Brush = CreateSolidBrush(BlockColor(Column, Row));

                FillRect(hdc, &Block, Brush);
                DeleteObject(Brush);
            }
        }
        EndPaint(hwnd, &Paint);
        return 0;
    }
    return DefWindowProcW(hwnd, Message, wParam, lParam);
}

static void
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

START_TEST(ScreenRows)
{
    WNDCLASSW Class = {0};
    int Width = GetSystemMetrics(SM_CXSCREEN);
    int Height = GetSystemMetrics(SM_CYSCREEN);
    int Column, Row, Mismatches = 0, Samples = 0;
    HWND hwnd;
    HDC hdc;

    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"ScreenRowsTest";
    Class.hCursor = NULL;
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    hwnd = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"ScreenRows", WS_POPUP | WS_VISIBLE,
                           0, 0, Width, Height, NULL, NULL, Class.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (hwnd == NULL)
        return;

    SetCursorPos(Width - 1, Height - 1);
    UpdateWindow(hwnd);
    GdiFlush();
    Pump(1500);

    hdc = GetDC(NULL);
    ok(hdc != NULL, "GetDC(NULL) failed\n");
    for (Row = 0; hdc != NULL && (Row + 1) * BLOCK <= Height - BLOCK; Row += SAMPLE_STEP)
    {
        for (Column = 0; (Column + 1) * BLOCK <= Width - BLOCK; Column += SAMPLE_STEP)
        {
            int X = Column * BLOCK + BLOCK / 2;
            int Y = Row * BLOCK + BLOCK / 2;
            COLORREF Expected = BlockColor(Column, Row);
            COLORREF Actual = GetPixel(hdc, X, Y);

            ++Samples;
            if (Actual != Expected && Mismatches++ < 8)
                ok(0, "Screen pixel (%d,%d) is 0x%06lX, expected 0x%06lX\n", X, Y, Actual, Expected);
        }
    }
    ok(Mismatches == 0, "%d of %d screen samples differ\n", Mismatches, Samples);
    trace("SCREEN_ROWS_END width=%d height=%d samples=%d mismatches=%d\n", Width, Height, Samples, Mismatches);
    Pump(4000);

    if (hdc != NULL)
        ReleaseDC(NULL, hdc);
    DestroyWindow(hwnd);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
