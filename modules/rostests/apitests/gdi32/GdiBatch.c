/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for the validation of GDI batch commands queued in the TEB
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#define NTOS_MODE_USER
#include <ndk/pstypes.h>

#define TEST_SIZE 96

typedef enum _BATCH_TAMPER
{
    TamperNone,
    TamperCount,
    TamperDxSize,
    TamperPdyWithoutDx,
    TamperNegativeSize,
    TamperSizePastBuffer
} BATCH_TAMPER;

static
ULONG
CountInk(
    _In_ HDC Source,
    _In_ HDC Target,
    _In_ const DWORD *Bits)
{
    ULONG Count = 0;
    ULONG Index;

    ok(BitBlt(Target, 0, 0, TEST_SIZE, TEST_SIZE, Source, 0, 0, SRCCOPY), "BitBlt failed, error %lu\n", GetLastError());
    GdiFlush();

    for (Index = 0; Index < TEST_SIZE * TEST_SIZE; Index++)
    {
        if ((Bits[Index] & 0x00FFFFFF) != 0x00FFFFFF)
            Count++;
    }
    return Count;
}

static
BOOL
QueueText(
    _In_ HDC Dc,
    _In_ BATCH_TAMPER Tamper)
{
    PTEB Teb = NtCurrentTeb();
    PGDIBSTEXTOUT Entry = (PGDIBSTEXTOUT)Teb->GdiTebBatch.Buffer;

    ok(PatBlt(Dc, 0, 0, TEST_SIZE, TEST_SIZE, WHITENESS), "PatBlt failed, error %lu\n", GetLastError());
    GdiFlush();
    ok_long(Teb->GdiBatchCount, 0);

    ok(ExtTextOutW(Dc, 2, 2, 0, NULL, L"WMW", 3, NULL), "ExtTextOutW failed, error %lu\n", GetLastError());
    if (Teb->GdiBatchCount != 1 || Entry->gbHdr.Cmd != GdiBCTextOut || Entry->cbCount != 3 || Entry->Size != 0)
    {
        GdiFlush();
        return FALSE;
    }

    switch (Tamper)
    {
        case TamperNone:
            break;
        case TamperCount:
            Entry->cbCount = 0x200;
            break;
        case TamperDxSize:
            Entry->Size = 0x7FF0;
            break;
        case TamperPdyWithoutDx:
            Entry->Options |= ETO_PDY;
            break;
        case TamperNegativeSize:
            Entry->gbHdr.Size = (SHORT)0x8000;
            break;
        case TamperSizePastBuffer:
            Entry->gbHdr.Size = GDIBATCHBUFSIZE + sizeof(ULONG);
            break;
    }
    return TRUE;
}

START_TEST(GdiBatch)
{
    static const struct
    {
        BATCH_TAMPER Tamper;
        BOOL Draws;
    } Cases[] =
    {
        { TamperNone, TRUE },
        { TamperCount, FALSE },
        { TamperDxSize, FALSE },
        { TamperPdyWithoutDx, FALSE },
        { TamperNegativeSize, FALSE },
        { TamperSizePastBuffer, FALSE },
    };
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER), TEST_SIZE, -TEST_SIZE, 1, 32, BI_RGB } };
    HDC Screen, Dc, Target;
    HBITMAP Bitmap, OldBitmap, TargetBitmap, OldTargetBitmap;
    HFONT Font, OldFont;
    DWORD *Bits = NULL;
    ULONG Index;

    Screen = GetDC(NULL);
    Dc = CreateCompatibleDC(Screen);
    Target = CreateCompatibleDC(Screen);
    Bitmap = CreateCompatibleBitmap(Screen, TEST_SIZE, TEST_SIZE);
    TargetBitmap = CreateDIBSection(Target, &Info, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    ok(Dc != NULL && Target != NULL && Bitmap != NULL && TargetBitmap != NULL && Bits != NULL, "GDI object creation failed\n");
    if (Dc == NULL || Target == NULL || Bitmap == NULL || TargetBitmap == NULL || Bits == NULL)
        return;
    Font = CreateFontW(-48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    ok(Font != NULL, "CreateFontW failed\n");
    OldBitmap = SelectObject(Dc, Bitmap);
    OldTargetBitmap = SelectObject(Target, TargetBitmap);
    OldFont = SelectObject(Dc, Font);
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, RGB(0, 0, 0));

    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        ULONG Ink;

        if (!QueueText(Dc, Cases[Index].Tamper))
        {
            skip("ExtTextOutW was not queued in the LiberNT batch format\n");
            break;
        }

        Ink = CountInk(Dc, Target, Bits);
        ok_long(NtCurrentTeb()->GdiBatchCount, 0);
        ok_long(NtCurrentTeb()->GdiTebBatch.Offset, 0);
        if (Cases[Index].Draws)
            ok(Ink != 0, "Case %lu: the batched text drew nothing\n", Index);
        else
            ok(Ink == 0, "Case %lu: the malformed batched text drew %lu pixels\n", Index, Ink);
    }

    SelectObject(Dc, OldFont);
    SelectObject(Target, OldTargetBitmap);
    SelectObject(Dc, OldBitmap);
    DeleteObject(Font);
    DeleteObject(TargetBitmap);
    DeleteObject(Bitmap);
    DeleteDC(Target);
    DeleteDC(Dc);
    ReleaseDC(NULL, Screen);
}
