/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the Uniscribe font tag queries on a font without OpenType layout tables
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <usp10.h>

#define TAG(a, b, c, d) ((OPENTYPE_TAG)(a) | ((OPENTYPE_TAG)(b) << 8) | ((OPENTYPE_TAG)(c) << 16) | ((OPENTYPE_TAG)(d) << 24))

START_TEST(ScriptGetFontTags)
{
    static const struct
    {
        int query;
        BOOL analysis;
        OPENTYPE_TAG script;
        HRESULT hr;
    } tests[] =
    {
        { 0, FALSE, 0, S_OK },
        { 0, TRUE, 0, S_OK },
        { 1, FALSE, TAG('l', 'a', 't', 'n'), S_OK },
        { 1, TRUE, TAG('l', 'a', 't', 'n'), S_OK },
        { 1, FALSE, TAG('D', 's', 'r', 't'), S_OK },
        { 1, TRUE, TAG('D', 's', 'r', 't'), E_INVALIDARG },
        { 1, FALSE, TAG('z', 'z', 'z', 'z'), S_OK },
        { 2, FALSE, TAG('l', 'a', 't', 'n'), S_OK },
        { 2, TRUE, TAG('l', 'a', 't', 'n'), S_OK },
        { 2, FALSE, TAG('D', 's', 'r', 't'), S_OK },
        { 2, TRUE, TAG('D', 's', 'r', 't'), E_INVALIDARG },
        { 2, FALSE, TAG('z', 'z', 'z', 'z'), S_OK },
    };
    SCRIPT_CONTROL control = { 0 };
    SCRIPT_STATE state = { 0 };
    SCRIPT_CACHE cache = NULL;
    SCRIPT_ITEM items[4];
    OPENTYPE_TAG tags[8];
    LOGFONTW lf = { 0 };
    HFONT font, old;
    unsigned int i;
    int count;
    HRESULT hr;
    HDC hdc;

    hdc = CreateCompatibleDC(NULL);
    lf.lfHeight = 10;
    lf.lfWidth = 10;
    lf.lfWeight = 3;
    wcscpy(lf.lfFaceName, L"MS Sans Serif");
    font = CreateFontIndirectW(&lf);
    old = SelectObject(hdc, font);
    if (GetFontData(hdc, TAG('G', 'S', 'U', 'B'), 0, NULL, 0) != GDI_ERROR ||
        GetFontData(hdc, TAG('G', 'P', 'O', 'S'), 0, NULL, 0) != GDI_ERROR)
    {
        skip("MS Sans Serif has OpenType layout tables\n");
        goto done;
    }

    hr = ScriptItemize(L"abc", 3, ARRAYSIZE(items), &control, &state, items, &count);
    ok(hr == S_OK, "ScriptItemize hr %#lx\n", hr);

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        SCRIPT_ANALYSIS *sa = tests[i].analysis ? &items[0].a : NULL;

        count = -1;
        if (tests[i].query == 0)
            hr = ScriptGetFontScriptTags(hdc, &cache, sa, ARRAYSIZE(tags), tags, &count);
        else if (tests[i].query == 1)
            hr = ScriptGetFontLanguageTags(hdc, &cache, sa, tests[i].script, ARRAYSIZE(tags), tags, &count);
        else
            hr = ScriptGetFontFeatureTags(hdc, &cache, sa, tests[i].script, 0, ARRAYSIZE(tags), tags, &count);
        ok(hr == tests[i].hr, "%u: hr %#lx expected %#lx\n", i, hr, tests[i].hr);
        ok(count == (hr == S_OK ? 0 : -1), "%u: count %d\n", i, count);
    }
    ScriptFreeCache(&cache);

done:
    SelectObject(hdc, old);
    DeleteObject(font);
    DeleteDC(hdc);
}
