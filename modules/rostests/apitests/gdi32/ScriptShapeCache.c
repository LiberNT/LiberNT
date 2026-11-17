/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a script cache shapes and places a font without layout tables the same way every time
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <strsafe.h>
#include <usp10.h>

#define TEXT_LENGTH 11

typedef struct _SHAPE_RESULT
{
    HRESULT ShapeStatus;
    HRESULT PlaceStatus;
    int GlyphCount;
    WORD Glyphs[TEXT_LENGTH * 2];
    int Advances[TEXT_LENGTH * 2];
    ABC Abc;
} SHAPE_RESULT;

static void
ShapeAndPlace(HDC hDC, SCRIPT_CACHE *Cache, const SCRIPT_ANALYSIS *Analysis, const WCHAR *Text, SHAPE_RESULT *Result)
{
    SCRIPT_VISATTR VisAttrs[TEXT_LENGTH * 2];
    GOFFSET Offsets[TEXT_LENGTH * 2];
    WORD Clusters[TEXT_LENGTH];
    SCRIPT_ANALYSIS Item = *Analysis;

    ZeroMemory(Result, sizeof(*Result));
    Result->ShapeStatus = ScriptShape(hDC, Cache, Text, TEXT_LENGTH, ARRAYSIZE(Result->Glyphs), &Item, Result->Glyphs,
                                      Clusters, VisAttrs, &Result->GlyphCount);
    if (FAILED(Result->ShapeStatus))
        return;
    Result->PlaceStatus = ScriptPlace(hDC, Cache, Result->Glyphs, Result->GlyphCount, VisAttrs, &Item,
                                      Result->Advances, Offsets, &Result->Abc);
}

static void
CompareResults(const SHAPE_RESULT *First, const SHAPE_RESULT *Second, const char *Name)
{
    int i;

    ok(First->GlyphCount == Second->GlyphCount, "%s: %d glyphs, then %d\n", Name, First->GlyphCount, Second->GlyphCount);
    for (i = 0; i < First->GlyphCount && i < Second->GlyphCount; ++i)
    {
        ok(First->Glyphs[i] == Second->Glyphs[i], "%s: glyph %d is %u, then %u\n", Name, i, First->Glyphs[i],
           Second->Glyphs[i]);
        ok(First->Advances[i] == Second->Advances[i], "%s: advance %d is %d, then %d\n", Name, i, First->Advances[i],
           Second->Advances[i]);
    }
    ok(First->Abc.abcB == Second->Abc.abcB, "%s: abcB %u, then %u\n", Name, First->Abc.abcB, Second->Abc.abcB);
}

START_TEST(ScriptShapeCache)
{
    static const WCHAR Text[TEXT_LENGTH + 1] = L"Hello World";
    SCRIPT_ITEM Items[32];
    SCRIPT_CACHE Cache = NULL;
    SHAPE_RESULT First, Again, Cached;
    HANDLE FontHandle;
    LOGFONTW LogFont;
    HRSRC Resource;
    HGDIOBJ OldFont;
    HFONT hFont;
    DWORD Size, Count = 0;
    HDC hDC;
    int ItemCount = 0;
    HRESULT hr;

    Resource = FindResourceA(GetModuleHandleA(NULL), "ExampleFont.ttf", MAKEINTRESOURCEA(RT_RCDATA));
    ok(Resource != NULL, "ExampleFont.ttf resource not found\n");
    if (!Resource)
        return;
    Size = SizeofResource(GetModuleHandleA(NULL), Resource);
    FontHandle = AddFontMemResourceEx(LockResource(LoadResource(GetModuleHandleA(NULL), Resource)), Size, NULL, &Count);
    ok(FontHandle != NULL && Count == 1, "AddFontMemResourceEx failed, count %lu\n", Count);
    if (!FontHandle)
        return;

    hDC = CreateCompatibleDC(NULL);
    ZeroMemory(&LogFont, sizeof(LogFont));
    LogFont.lfHeight = -32;
    StringCchCopyW(LogFont.lfFaceName, ARRAYSIZE(LogFont.lfFaceName), L"EnglishDisplayName");
    hFont = CreateFontIndirectW(&LogFont);
    OldFont = SelectObject(hDC, hFont);

    hr = ScriptItemize(Text, TEXT_LENGTH, ARRAYSIZE(Items) - 1, NULL, NULL, Items, &ItemCount);
    ok(hr == S_OK && ItemCount == 1, "ScriptItemize hr 0x%lx, %d items\n", hr, ItemCount);
    if (SUCCEEDED(hr) && ItemCount == 1)
    {
        ShapeAndPlace(hDC, &Cache, &Items[0].a, Text, &First);
        ok(First.ShapeStatus == S_OK, "first ScriptShape hr 0x%lx\n", First.ShapeStatus);
        ok(First.PlaceStatus == S_OK, "first ScriptPlace hr 0x%lx\n", First.PlaceStatus);
        ok(First.GlyphCount == TEXT_LENGTH, "first shaping gave %d glyphs\n", First.GlyphCount);

        /* The font has no GSUB or GPOS table; shaping it again through the same cache changes nothing. */
        ShapeAndPlace(hDC, &Cache, &Items[0].a, Text, &Again);
        ok(Again.ShapeStatus == S_OK && Again.PlaceStatus == S_OK, "second pass hr 0x%lx, 0x%lx\n",
           Again.ShapeStatus, Again.PlaceStatus);
        CompareResults(&First, &Again, "second pass");

        /* Everything the run needs is cached, so the cache alone shapes and places it. */
        ShapeAndPlace(NULL, &Cache, &Items[0].a, Text, &Cached);
        ok(Cached.ShapeStatus == S_OK, "cached ScriptShape hr 0x%lx\n", Cached.ShapeStatus);
        ok(Cached.PlaceStatus == S_OK, "cached ScriptPlace hr 0x%lx\n", Cached.PlaceStatus);
        if (Cached.ShapeStatus == S_OK && Cached.PlaceStatus == S_OK)
            CompareResults(&First, &Cached, "cache only");
    }

    ScriptFreeCache(&Cache);
    SelectObject(hDC, OldFont);
    DeleteObject(hFont);
    DeleteDC(hDC);
    ok(RemoveFontMemResourceEx(FontHandle), "RemoveFontMemResourceEx failed\n");
}
