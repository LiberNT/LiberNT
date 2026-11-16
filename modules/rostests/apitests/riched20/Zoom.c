/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that rich edit character positions follow the zoom factor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <richedit.h>

static const WCHAR TestText[] = L"The quick brown fox jumps over the lazy dog";

static HWND
CreateEdit(LONG Twips)
{
    CHARFORMAT2W Format;
    HWND Edit;

    Edit = CreateWindowExW(0, L"RichEdit20W", NULL, WS_POPUP, 0, 0, 8000, 400, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Edit != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (Edit == NULL)
        return NULL;

    ok(SendMessageW(Edit, WM_SETTEXT, 0, (LPARAM)TestText) == TRUE, "WM_SETTEXT failed\n");

    ZeroMemory(&Format, sizeof(Format));
    Format.cbSize = sizeof(Format);
    Format.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_CHARSET;
    Format.yHeight = Twips;
    Format.bCharSet = DEFAULT_CHARSET;
    wcscpy(Format.szFaceName, L"Tahoma");
    ok(SendMessageW(Edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&Format) != 0, "EM_SETCHARFORMAT failed\n");
    return Edit;
}

static LONG
TextWidth(HWND Edit)
{
    POINTL First = { 0, 0 };
    POINTL Last = { 0, 0 };

    SendMessageW(Edit, EM_POSFROMCHAR, (WPARAM)&First, 0);
    SendMessageW(Edit, EM_POSFROMCHAR, (WPARAM)&Last, RTL_NUMBER_OF(TestText) - 1);
    return Last.x - First.x;
}

static void
CheckZoom(HWND Edit, int ExpectedNumerator, int ExpectedDenominator)
{
    int Numerator = -1;
    int Denominator = -1;

    ok(SendMessageW(Edit, EM_GETZOOM, (WPARAM)&Numerator, (LPARAM)&Denominator) == TRUE, "EM_GETZOOM failed\n");
    ok_int(Numerator, ExpectedNumerator);
    ok_int(Denominator, ExpectedDenominator);
}

static void
TestZoomIn(void)
{
    HWND Edit;
    HWND Reference;
    LONG Plain;
    LONG Zoomed;
    LONG Restored;
    LONG Doubled;

    Edit = CreateEdit(200);
    Reference = CreateEdit(400);
    if (Edit == NULL || Reference == NULL)
    {
        skip("No rich edit control\n");
        if (Edit != NULL)
            DestroyWindow(Edit);
        if (Reference != NULL)
            DestroyWindow(Reference);
        return;
    }

    CheckZoom(Edit, 0, 0);
    Plain = TextWidth(Edit);
    Doubled = TextWidth(Reference);
    ok(Plain > 100, "Text is %ld pixels wide\n", Plain);
    ok(Doubled > Plain + Plain / 2, "Text is %ld pixels wide at 10 points and %ld at 20\n", Plain, Doubled);

    ok(SendMessageW(Edit, EM_SETZOOM, 2, 1) == TRUE, "EM_SETZOOM 2/1 failed\n");
    CheckZoom(Edit, 2, 1);
    Zoomed = TextWidth(Edit);
    ok(Zoomed == Doubled, "Text is %ld pixels wide at 10 points zoomed 2/1 and %ld at 20 points\n", Zoomed, Doubled);

    ok(SendMessageW(Edit, EM_SETZOOM, 4, 1) == TRUE, "EM_SETZOOM 4/1 failed\n");
    CheckZoom(Edit, 4, 1);
    Zoomed = TextWidth(Edit);
    ok(Zoomed > Plain * 7 / 2 && Zoomed < Plain * 9 / 2, "Text is %ld pixels wide, %ld zoomed 4/1\n", Plain, Zoomed);

    ok(SendMessageW(Edit, EM_SETZOOM, 0, 0) == TRUE, "EM_SETZOOM 0/0 failed\n");
    CheckZoom(Edit, 0, 0);
    Restored = TextWidth(Edit);
    ok(Restored == Plain, "Text is %ld pixels wide, %ld after the zoom was removed\n", Plain, Restored);

    DestroyWindow(Reference);
    DestroyWindow(Edit);
}

static void
TestZoomOut(void)
{
    HWND Edit;
    HWND Reference;
    LONG Plain;
    LONG Zoomed;
    LONG Half;

    Edit = CreateEdit(400);
    Reference = CreateEdit(200);
    if (Edit == NULL || Reference == NULL)
    {
        skip("No rich edit control\n");
        if (Edit != NULL)
            DestroyWindow(Edit);
        if (Reference != NULL)
            DestroyWindow(Reference);
        return;
    }

    Plain = TextWidth(Edit);
    Half = TextWidth(Reference);

    ok(SendMessageW(Edit, EM_SETZOOM, 1, 2) == TRUE, "EM_SETZOOM 1/2 failed\n");
    CheckZoom(Edit, 1, 2);
    Zoomed = TextWidth(Edit);
    ok(Zoomed == Half, "Text is %ld pixels wide at 20 points zoomed 1/2 and %ld at 10 points\n", Zoomed, Half);
    ok(Zoomed < Plain * 2 / 3, "Text is %ld pixels wide, %ld zoomed 1/2\n", Plain, Zoomed);

    DestroyWindow(Reference);
    DestroyWindow(Edit);
}

START_TEST(Zoom)
{
    HMODULE RichEdit;

    RichEdit = LoadLibraryW(L"riched20.dll");
    if (RichEdit == NULL)
    {
        skip("riched20.dll is not available, error %lu\n", GetLastError());
        return;
    }

    TestZoomIn();
    TestZoomOut();

    FreeLibrary(RichEdit);
}
