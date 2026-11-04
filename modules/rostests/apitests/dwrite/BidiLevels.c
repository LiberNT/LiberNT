/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the explicit and resolved levels IDWriteTextAnalyzer::AnalyzeBidi reports
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite.h>

static const struct
{
    BOOL Rtl;
    const WCHAR *Text;
    UINT8 Explicit[8];
    UINT8 Resolved[8];
} Tests[] =
{
    { FALSE, L"\x2066" L"ab\x2069", { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { TRUE, L"\x2067" L"ab\x2069", { 1, 1, 1, 1 }, { 1, 2, 2, 2 } },
    { FALSE, L"a\x2067\x627" L"b\x2069" L"c", { 0, 0, 0, 0, 0, 0 }, { 0, 0, 1, 0, 0, 0 } },
    { FALSE, L"\x627\x2066 ", { 0, 0, 0 }, { 1, 1, 0 } },
    { FALSE, L"\x202d" L"ab\x202c", { 2, 2, 2, 2 }, { 0, 2, 2, 2 } },
    { TRUE, L"\x202e" L"a1\x627\x202c", { 3, 3, 3, 3, 3 }, { 1, 3, 3, 3, 3 } },
    { FALSE, L"\x202d\x202b" L"a\x202c" L"b", { 2, 3, 3, 3, 2 }, { 0, 0, 4, 4, 2 } },
    { FALSE, L"a\x202d\x627" L"1\x202c", { 0, 2, 2, 2, 2 }, { 0, 0, 2, 2, 2 } },
    { FALSE, L"\x202a" L"ab\x202c", { 2, 2, 2, 2 }, { 0, 2, 2, 2 } },
    { TRUE, L"a\x202a", { 1, 2 }, { 2, 2 } },
    { FALSE, L"\x202a\xad" L"a\x202c", { 2, 2, 2, 2 }, { 0, 0, 2, 2 } },
    { TRUE, L"\x202b\x200d\x202c", { 3, 3, 3 }, { 1, 1, 1 } },
    { TRUE, L"ab  ", { 1, 1, 1, 1 }, { 2, 2, 1, 1 } },
    { TRUE, L"a \x202a", { 1, 1, 2 }, { 2, 1, 1 } },
    { TRUE, L"a\x202a\x9" L"b", { 1, 2, 1, 2 }, { 2, 2, 1, 2 } },
    { FALSE, L"a\x202a\x9" L"b", { 0, 2, 0, 2 }, { 0, 0, 0, 2 } },
    { TRUE, L"a \x2029" L"b", { 1, 1, 1, 1 }, { 2, 1, 1, 2 } },
    { FALSE, L"\x627\xad" L"a", { 0, 0, 0 }, { 1, 1, 0 } },
};

static const WCHAR *Text;
static UINT32 Length;
static DWRITE_READING_DIRECTION Direction;
static UINT8 Explicit[8], Resolved[8];

static HRESULT WINAPI
Source_QueryInterface(IDWriteTextAnalysisSource *Iface, REFIID Riid, void **Object)
{
    if (IsEqualIID(Riid, &IID_IDWriteTextAnalysisSource) || IsEqualIID(Riid, &IID_IUnknown))
    {
        *Object = Iface;
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Source_AddRef(IDWriteTextAnalysisSource *Iface)
{
    return 2;
}

static ULONG WINAPI
Source_Release(IDWriteTextAnalysisSource *Iface)
{
    return 1;
}

static HRESULT WINAPI
Source_GetTextAtPosition(IDWriteTextAnalysisSource *Iface, UINT32 Position, const WCHAR **String, UINT32 *Count)
{
    *String = Position < Length ? Text + Position : NULL;
    *Count = Position < Length ? Length - Position : 0;
    return S_OK;
}

static HRESULT WINAPI
Source_GetTextBeforePosition(IDWriteTextAnalysisSource *Iface, UINT32 Position, const WCHAR **String, UINT32 *Count)
{
    *String = Position && Position <= Length ? Text : NULL;
    *Count = Position && Position <= Length ? Position : 0;
    return S_OK;
}

static DWRITE_READING_DIRECTION WINAPI
Source_GetParagraphReadingDirection(IDWriteTextAnalysisSource *Iface)
{
    return Direction;
}

static HRESULT WINAPI
Source_GetLocaleName(IDWriteTextAnalysisSource *Iface, UINT32 Position, UINT32 *Count, const WCHAR **Locale)
{
    *Locale = NULL;
    *Count = 0;
    return S_OK;
}

static HRESULT WINAPI
Source_GetNumberSubstitution(IDWriteTextAnalysisSource *Iface, UINT32 Position, UINT32 *Count,
                             IDWriteNumberSubstitution **Substitution)
{
    *Substitution = NULL;
    *Count = Length - Position;
    return S_OK;
}

static IDWriteTextAnalysisSourceVtbl SourceVtbl =
{
    Source_QueryInterface, Source_AddRef, Source_Release, Source_GetTextAtPosition, Source_GetTextBeforePosition,
    Source_GetParagraphReadingDirection, Source_GetLocaleName, Source_GetNumberSubstitution
};

static IDWriteTextAnalysisSource Source = { &SourceVtbl };

static HRESULT WINAPI
Sink_QueryInterface(IDWriteTextAnalysisSink *Iface, REFIID Riid, void **Object)
{
    if (IsEqualIID(Riid, &IID_IDWriteTextAnalysisSink) || IsEqualIID(Riid, &IID_IUnknown))
    {
        *Object = Iface;
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Sink_AddRef(IDWriteTextAnalysisSink *Iface)
{
    return 2;
}

static ULONG WINAPI
Sink_Release(IDWriteTextAnalysisSink *Iface)
{
    return 1;
}

static HRESULT WINAPI
Sink_SetScriptAnalysis(IDWriteTextAnalysisSink *Iface, UINT32 Position, UINT32 Count, const DWRITE_SCRIPT_ANALYSIS *Analysis)
{
    return S_OK;
}

static HRESULT WINAPI
Sink_SetLineBreakpoints(IDWriteTextAnalysisSink *Iface, UINT32 Position, UINT32 Count, const DWRITE_LINE_BREAKPOINT *Breakpoints)
{
    return S_OK;
}

static HRESULT WINAPI
Sink_SetBidiLevel(IDWriteTextAnalysisSink *Iface, UINT32 Position, UINT32 Count, UINT8 ExplicitLevel, UINT8 ResolvedLevel)
{
    UINT32 Index;

    for (Index = Position; Index < Position + Count && Index < ARRAYSIZE(Explicit); Index++)
    {
        Explicit[Index] = ExplicitLevel;
        Resolved[Index] = ResolvedLevel;
    }
    return S_OK;
}

static HRESULT WINAPI
Sink_SetNumberSubstitution(IDWriteTextAnalysisSink *Iface, UINT32 Position, UINT32 Count, IDWriteNumberSubstitution *Substitution)
{
    return S_OK;
}

static IDWriteTextAnalysisSinkVtbl SinkVtbl =
{
    Sink_QueryInterface, Sink_AddRef, Sink_Release, Sink_SetScriptAnalysis, Sink_SetLineBreakpoints,
    Sink_SetBidiLevel, Sink_SetNumberSubstitution
};

static IDWriteTextAnalysisSink Sink = { &SinkVtbl };

START_TEST(BidiLevels)
{
    IDWriteTextAnalyzer *Analyzer = NULL;
    IDWriteFactory *Factory = NULL;
    UINT32 Test, Index;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok(hr == S_OK, "DWriteCreateFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDWriteFactory_CreateTextAnalyzer(Factory, &Analyzer);
    ok(hr == S_OK, "CreateTextAnalyzer failed, hr %#lx\n", hr);

    for (Test = 0; Analyzer && Test < ARRAYSIZE(Tests); Test++)
    {
        Text = Tests[Test].Text;
        Length = lstrlenW(Text);
        Direction = Tests[Test].Rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        memset(Explicit, 0xff, sizeof(Explicit));
        memset(Resolved, 0xff, sizeof(Resolved));
        hr = IDWriteTextAnalyzer_AnalyzeBidi(Analyzer, &Source, 0, Length, &Sink);
        ok(hr == S_OK, "%u: AnalyzeBidi failed, hr %#lx\n", Test, hr);
        for (Index = 0; Index < Length; Index++)
        {
            ok(Explicit[Index] == Tests[Test].Explicit[Index], "%u: at %u: explicit level %u, expected %u\n",
               Test, Index, Explicit[Index], Tests[Test].Explicit[Index]);
            ok(Resolved[Index] == Tests[Test].Resolved[Index], "%u: at %u: resolved level %u, expected %u\n",
               Test, Index, Resolved[Index], Tests[Test].Resolved[Index]);
        }
    }

    if (Analyzer) IDWriteTextAnalyzer_Release(Analyzer);
    IDWriteFactory_Release(Factory);
}
