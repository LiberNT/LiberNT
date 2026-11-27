/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that Uniscribe joins Arabic letters in the system fonts that carry them
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <strsafe.h>
#include <usp10.h>

#define MAX_GLYPHS 16
#define TAG(a, b, c, d) \
    ((OPENTYPE_TAG)(a) | ((OPENTYPE_TAG)(b) << 8) | ((OPENTYPE_TAG)(c) << 16) | ((OPENTYPE_TAG)(d) << 24))

typedef struct _JOIN_CASE
{
    const WCHAR *Name;
    const WCHAR *Text;
    int GlyphCount;
    WCHAR Forms[4];
    BOOL Isolated[4];
} JOIN_CASE;

static const JOIN_CASE JoinCases[] =
{
    { L"bayt", L"\x0628\x064A\x062A", 3, { 0xFE91, 0xFEF4, 0xFE96 } },
    { L"salam", L"\x0633\x0644\x0627\x0645", 3, { 0xFEB3, 0xFEFC, 0xFEFC, 0xFEE1 }, { FALSE, FALSE, FALSE, TRUE } },
    { L"la", L"\x0644\x0627", 1, { 0xFEFB, 0xFEFB } },
};

static BOOL
HasTag(const OPENTYPE_TAG *Tags, int Count, OPENTYPE_TAG Tag)
{
    int i;

    for (i = 0; i < Count; ++i)
    {
        if (Tags[i] == Tag)
            return TRUE;
    }
    return FALSE;
}

static WORD
NominalGlyph(HDC hDC, WCHAR Char)
{
    WORD Glyph = 0xFFFF;

    if (GetGlyphIndicesW(hDC, &Char, 1, &Glyph, GGI_MARK_NONEXISTING_GLYPHS) != 1)
        return 0xFFFF;
    return Glyph;
}

static void
TestJoinCase(HDC hDC, SCRIPT_CACHE *Cache, const WCHAR *FontName, const JOIN_CASE *Case)
{
    SCRIPT_ITEM Items[8];
    SCRIPT_VISATTR VisAttrs[MAX_GLYPHS];
    WORD Glyphs[MAX_GLYPHS], Clusters[MAX_GLYPHS];
    int Length = lstrlenW(Case->Text);
    int ItemCount = 0, GlyphCount = 0, i;
    HRESULT hr;

    hr = ScriptItemize(Case->Text, Length, ARRAYSIZE(Items) - 1, NULL, NULL, Items, &ItemCount);
    ok(hr == S_OK && ItemCount == 1, "%ls %ls: ScriptItemize hr 0x%lx, %d items\n", FontName, Case->Name, hr,
       ItemCount);
    if (hr != S_OK || ItemCount != 1)
        return;
    ok(Items[0].a.fRTL, "%ls %ls: the item is not right to left\n", FontName, Case->Name);

    hr = ScriptShape(hDC, Cache, Case->Text, Length, ARRAYSIZE(Glyphs), &Items[0].a, Glyphs, Clusters, VisAttrs,
                     &GlyphCount);
    ok(hr == S_OK, "%ls %ls: ScriptShape hr 0x%lx\n", FontName, Case->Name, hr);
    if (hr != S_OK)
        return;
    ok(GlyphCount == Case->GlyphCount, "%ls %ls: %d glyphs, expected %d\n", FontName, Case->Name, GlyphCount,
       Case->GlyphCount);

    for (i = 0; i < Length; ++i)
    {
        WORD Expected, Nominal = NominalGlyph(hDC, Case->Text[i]);
        WORD Shaped;

        ok(Clusters[i] < GlyphCount, "%ls %ls: character %d maps to glyph %u of %d\n", FontName, Case->Name, i,
           Clusters[i], GlyphCount);
        if (Clusters[i] >= GlyphCount)
            continue;
        Shaped = Glyphs[Clusters[i]];

        Expected = NominalGlyph(hDC, Case->Forms[i]);
        ok(Expected != 0xFFFF, "%ls: no glyph for U+%04X\n", FontName, Case->Forms[i]);
        if (Case->Isolated[i])
        {
            ok(Shaped == Nominal || Shaped == Expected,
               "%ls %ls: character %d is glyph %u, expected %u or U+%04X glyph %u\n", FontName, Case->Name, i,
               Shaped, Nominal, Case->Forms[i], Expected);
            continue;
        }
        ok(Shaped != Nominal, "%ls %ls: character %d kept its isolated glyph %u\n", FontName, Case->Name, i,
           Shaped);
        ok(Shaped == Expected, "%ls %ls: character %d is glyph %u, expected U+%04X glyph %u\n", FontName,
           Case->Name, i, Shaped, Case->Forms[i], Expected);
    }
}

static void
TestFont(const WCHAR *FontName)
{
    static const WCHAR Probe[] = L"\x0628";
    OPENTYPE_TAG Tags[64];
    SCRIPT_ITEM Items[4];
    SCRIPT_CACHE Cache = NULL;
    WCHAR Face[LF_FACESIZE];
    LOGFONTW LogFont;
    HGDIOBJ OldFont;
    HFONT hFont;
    HDC hDC;
    int Count = 0, ItemCount = 0, i;
    HRESULT hr;

    hDC = CreateCompatibleDC(NULL);
    ok(hDC != NULL, "CreateCompatibleDC failed\n");
    if (!hDC)
        return;

    ZeroMemory(&LogFont, sizeof(LogFont));
    LogFont.lfHeight = -24;
    LogFont.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(LogFont.lfFaceName, ARRAYSIZE(LogFont.lfFaceName), FontName);
    hFont = CreateFontIndirectW(&LogFont);
    ok(hFont != NULL, "%ls: CreateFontIndirectW failed\n", FontName);
    OldFont = SelectObject(hDC, hFont);

    GetTextFaceW(hDC, ARRAYSIZE(Face), Face);
    ok(!lstrcmpiW(Face, FontName), "%ls: selected face is %ls\n", FontName, Face);

    hr = ScriptItemize(Probe, 1, ARRAYSIZE(Items) - 1, NULL, NULL, Items, &ItemCount);
    ok(hr == S_OK && ItemCount == 1, "%ls: ScriptItemize hr 0x%lx, %d items\n", FontName, hr, ItemCount);
    if (hr == S_OK && ItemCount == 1)
    {
        hr = ScriptGetFontScriptTags(hDC, &Cache, &Items[0].a, ARRAYSIZE(Tags), Tags, &Count);
        ok(hr == S_OK, "%ls: ScriptGetFontScriptTags hr 0x%lx\n", FontName, hr);
        ok(hr == S_OK && HasTag(Tags, Count, TAG('a', 'r', 'a', 'b')), "%ls: no arab script among %d tags\n",
           FontName, Count);
    }

    for (i = 0; i < ARRAYSIZE(JoinCases); ++i)
        TestJoinCase(hDC, &Cache, FontName, &JoinCases[i]);

    ScriptFreeCache(&Cache);
    SelectObject(hDC, OldFont);
    DeleteObject(hFont);
    DeleteDC(hDC);
}

START_TEST(ArabicJoining)
{
    TestFont(L"Tahoma");
    TestFont(L"Courier New");
}
