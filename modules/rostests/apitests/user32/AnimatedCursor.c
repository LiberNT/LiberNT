/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that animated cursors and the bitmaps of their frames are created
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_SIZE 32
#define TEST_RT_ANICURSOR MAKEINTRESOURCEW(21)

typedef HCURSOR (WINAPI *PFN_GET_CURSOR_FRAME_INFO)(HCURSOR, DWORD, DWORD, PDWORD, PDWORD);

static VOID
TestFrameBitmaps(VOID)
{
    DWORD Pixels[TEST_SIZE * TEST_SIZE * 2];
    BYTE Info[sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD)];
    BITMAPINFO *Bmi = (BITMAPINFO *)Info;
    HBITMAP Color, Mask, Old;
    HDC Screen, Dc;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Pixels); Index++)
        Pixels[Index] = 0x80402010;

    Screen = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    ok(Screen != NULL, "CreateDCW failed: %lu\n", GetLastError());
    if (!Screen)
        return;
    Dc = CreateCompatibleDC(Screen);
    ok(Dc != NULL, "CreateCompatibleDC failed: %lu\n", GetLastError());
    Color = CreateCompatibleBitmap(Screen, TEST_SIZE, TEST_SIZE);
    ok(Color != NULL, "CreateCompatibleBitmap failed: %lu\n", GetLastError());
    Mask = CreateBitmap(TEST_SIZE, TEST_SIZE, 1, 1, NULL);
    ok(Mask != NULL, "CreateBitmap failed: %lu\n", GetLastError());

    if (Dc && Color && Mask)
    {
        ZeroMemory(Info, sizeof(Info));
        Bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Bmi->bmiHeader.biWidth = TEST_SIZE;
        Bmi->bmiHeader.biHeight = TEST_SIZE;
        Bmi->bmiHeader.biPlanes = 1;
        Bmi->bmiHeader.biBitCount = 32;
        Bmi->bmiHeader.biCompression = BI_RGB;

        Old = SelectObject(Dc, Color);
        ok(Old != NULL, "SelectObject(color) failed\n");
        ok(StretchDIBits(Dc, 0, 0, TEST_SIZE, TEST_SIZE, 0, 0, TEST_SIZE, TEST_SIZE, Pixels, Bmi,
                         DIB_RGB_COLORS, SRCCOPY) != 0,
           "StretchDIBits(color) failed: %lu\n", GetLastError());

        Bmi->bmiHeader.biBitCount = 1;
        Bmi->bmiHeader.biClrUsed = Bmi->bmiHeader.biClrImportant = 2;
        Bmi->bmiColors[1].rgbBlue = Bmi->bmiColors[1].rgbGreen = Bmi->bmiColors[1].rgbRed = 0xff;
        ok(SelectObject(Dc, Mask) != NULL, "SelectObject(mask) failed\n");
        ok(StretchDIBits(Dc, 0, 0, TEST_SIZE, TEST_SIZE, 0, 0, TEST_SIZE, TEST_SIZE, Pixels, Bmi,
                         DIB_RGB_COLORS, SRCCOPY) != 0,
           "StretchDIBits(mask) failed: %lu\n", GetLastError());
        if (Old)
            SelectObject(Dc, Old);
    }

    if (Mask)
        DeleteObject(Mask);
    if (Color)
        DeleteObject(Color);
    if (Dc)
        DeleteDC(Dc);
    DeleteDC(Screen);
}

static VOID
TestSystemWaitCursor(VOID)
{
    PFN_GET_CURSOR_FRAME_INFO GetCursorFrameInfo;
    DWORD Rate = 0, Steps = 0;
    HCURSOR Cursor;

    Cursor = LoadImageW(NULL, MAKEINTRESOURCEW(OCR_WAIT), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    ok(Cursor != NULL, "The system wait cursor did not load: %lu\n", GetLastError());
    if (!Cursor)
        return;

    GetCursorFrameInfo = (PFN_GET_CURSOR_FRAME_INFO)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                    "GetCursorFrameInfo");
    if (!GetCursorFrameInfo)
    {
        skip("No GetCursorFrameInfo\n");
        return;
    }
    ok(GetCursorFrameInfo(Cursor, 0, 0, &Rate, &Steps) != NULL, "GetCursorFrameInfo failed\n");
    ok(Steps > 1, "The wait cursor has %lu steps\n", Steps);
}

static VOID
TestAnimatedResource(VOID)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    HCURSOR Cursor;
    HRSRC Resource;
    HGLOBAL Data;
    DWORD Size;

    Resource = FindResourceW(User32, MAKEINTRESOURCEW(OCR_WAIT), TEST_RT_ANICURSOR);
    if (!Resource)
    {
        skip("user32 has no animated wait cursor resource\n");
        return;
    }
    Size = SizeofResource(User32, Resource);
    Data = LoadResource(User32, Resource);
    ok(Data != NULL && Size != 0, "LoadResource failed: %lu\n", GetLastError());
    if (!Data || !Size)
        return;

    Cursor = CreateIconFromResourceEx(LockResource(Data), Size, FALSE, 0x00030000, TEST_SIZE, TEST_SIZE, 0);
    ok(Cursor != NULL, "CreateIconFromResourceEx failed on the animated cursor: %lu\n", GetLastError());
    if (Cursor)
        DestroyCursor(Cursor);
}

START_TEST(AnimatedCursor)
{
    TestFrameBitmaps();
    TestSystemWaitCursor();
    TestAnimatedResource();
}
