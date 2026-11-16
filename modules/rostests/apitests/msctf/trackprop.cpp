/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfContext::TrackProperties
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

class CTrackCompositionSink : public ITfCompositionSink
{
public:
    CTrackCompositionSink() : m_cRefs(1) { }
    virtual ~CTrackCompositionSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompositionSink))
        {
            *ppv = static_cast<ITfCompositionSink *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) override
    {
        return S_OK;
    }

    LONG m_cRefs;
};

struct TRACK_TEST
{
    TSF_FIXTURE *pFixture;
    CTrackCompositionSink *pSink;
    ITfComposition *pComposition;
};

static const GUID g_GuidZero = { 0 };
static const GUID *g_TrackedGuids[] = { &GUID_PROP_COMPOSING, &GUID_PROP_ATTRIBUTE };

static void AppendValue(char *psz, size_t cch, const TF_PROPERTYVAL *pValue)
{
    char sz[32];
    const char *pszName = IsEqualGUID(pValue->guidId, GUID_PROP_COMPOSING) ? "C" :
                          IsEqualGUID(pValue->guidId, GUID_PROP_ATTRIBUTE) ? "A" : "?";

    if (V_VT(&pValue->varValue) == VT_I4)
        StringCchPrintfA(sz, _countof(sz), "%s=%ld", pszName, V_I4(&pValue->varValue));
    else if (V_VT(&pValue->varValue) == VT_EMPTY)
        StringCchPrintfA(sz, _countof(sz), "%s=-", pszName);
    else
        StringCchPrintfA(sz, _countof(sz), "%s=vt%d", pszName, V_VT(&pValue->varValue));
    StringCchCatA(psz, cch, sz);
}

static void DescribeValue(TfEditCookie ec, ITfReadOnlyProperty *pProperty, ITfRange *pRange, char *psz, size_t cch)
{
    IEnumTfPropertyValue *pEnum = NULL;
    TF_PROPERTYVAL value;
    VARIANT var;
    ULONG cFetched, cValues = 0;
    HRESULT hr;
    char sz[32];

    VariantInit(&var);
    hr = pProperty->GetValue(ec, pRange, &var);
    if (hr != S_OK || V_VT(&var) != VT_UNKNOWN)
    {
        StringCchPrintfA(sz, _countof(sz), "{hr %lx vt %d}", hr, V_VT(&var));
        StringCchCatA(psz, cch, sz);
        VariantClear(&var);
        return;
    }
    if (!V_UNKNOWN(&var) || FAILED(V_UNKNOWN(&var)->QueryInterface(IID_IEnumTfPropertyValue, (void **)&pEnum)))
    {
        StringCchCatA(psz, cch, "{no enum}");
        VariantClear(&var);
        return;
    }
    StringCchCatA(psz, cch, "{");
    while (pEnum->Next(1, &value, &cFetched) == S_OK && cFetched)
    {
        if (cValues++)
            StringCchCatA(psz, cch, ",");
        AppendValue(psz, cch, &value);
        VariantClear(&value.varValue);
    }
    StringCchCatA(psz, cch, "}");
    pEnum->Release();
    VariantClear(&var);
}

static void DescribeRanges(TfEditCookie ec, ITfReadOnlyProperty *pProperty, ITfRange *pTarget, char *psz, size_t cch)
{
    IEnumTfRanges *pEnum = NULL;
    ITfRange *pRange;
    ULONG cFetched, cRanges = 0;
    HRESULT hr;
    char sz[32];

    psz[0] = 0;
    hr = pProperty->EnumRanges(ec, &pEnum, pTarget);
    if (hr != S_OK)
    {
        StringCchPrintfA(psz, cch, "hr %lx", hr);
        if (pEnum)
            pEnum->Release();
        return;
    }
    while (pEnum->Next(1, &pRange, &cFetched) == S_OK && cFetched)
    {
        LONG s, e;
        Range_GetExtent(ec, pRange, &s, &e);
        StringCchPrintfA(sz, _countof(sz), "%s[%ld,%ld]", cRanges++ ? " " : "", s, e);
        StringCchCatA(psz, cch, sz);
        DescribeValue(ec, pProperty, pRange, psz, cch);
        pRange->Release();
    }
    pEnum->Release();
}

static ITfRange *WholeRange(TSF_FIXTURE *pFixture, TfEditCookie ec)
{
    ITfRange *pRange = NULL, *pEnd = NULL;

    pFixture->pContext->GetStart(ec, &pRange);
    pFixture->pContext->GetEnd(ec, &pEnd);
    if (pRange && pEnd)
        ok_hr(pRange->ShiftEndToRange(ec, pEnd, TF_ANCHOR_END), S_OK);
    if (pEnd)
        pEnd->Release();
    return pRange;
}

#define check_tracked(ec, f, t, s) check_tracked_(__LINE__, ec, f, t, s)
static void check_tracked_(int line, TfEditCookie ec, TSF_FIXTURE *pFixture, ITfRange *pTarget, const char *pszExpected)
{
    ITfReadOnlyProperty *pProperty = NULL;
    char sz[512];
    HRESULT hr;

    hr = pFixture->pContext->TrackProperties(g_TrackedGuids, _countof(g_TrackedGuids), NULL, 0, &pProperty);
    ok_(__FILE__, line)(hr == S_OK, "TrackProperties returned 0x%lx\n", hr);
    if (!pProperty)
        return;
    DescribeRanges(ec, pProperty, pTarget, sz, _countof(sz));
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "tracked \"%s\", expected \"%s\"\n", sz, pszExpected);
    pProperty->Release();
}

#define check_composing(ec, f, s) check_composing_(__LINE__, ec, f, s)
static void check_composing_(int line, TfEditCookie ec, TSF_FIXTURE *pFixture, const char *pszExpected)
{
    ITfProperty *pProperty = NULL;
    IEnumTfRanges *pEnum = NULL;
    ITfRange *pRange;
    ULONG cFetched, cRanges = 0;
    char sz[256] = "", szRange[48];
    HRESULT hr;

    hr = pFixture->pContext->GetProperty(GUID_PROP_COMPOSING, &pProperty);
    ok_(__FILE__, line)(hr == S_OK, "GetProperty returned 0x%lx\n", hr);
    if (!pProperty)
        return;
    hr = pProperty->EnumRanges(ec, &pEnum, NULL);
    ok_(__FILE__, line)(hr == S_OK, "EnumRanges returned 0x%lx\n", hr);
    while (pEnum && pEnum->Next(1, &pRange, &cFetched) == S_OK && cFetched)
    {
        VARIANT var;
        LONG s, e;
        Range_GetExtent(ec, pRange, &s, &e);
        VariantInit(&var);
        pProperty->GetValue(ec, pRange, &var);
        StringCchPrintfA(szRange, _countof(szRange), "%s[%ld,%ld]=%ld", cRanges++ ? " " : "", s, e,
                         V_VT(&var) == VT_I4 ? V_I4(&var) : -1);
        StringCchCatA(sz, _countof(sz), szRange);
        VariantClear(&var);
        pRange->Release();
    }
    if (pEnum)
        pEnum->Release();
    ok_(__FILE__, line)(!strcmp(sz, pszExpected), "composing \"%s\", expected \"%s\"\n", sz, pszExpected);
    pProperty->Release();
}

static void SetAttribute(TSF_FIXTURE *pFixture, TfEditCookie ec, LONG lStart, LONG lEnd, LONG lValue)
{
    ITfProperty *pProperty = NULL;
    ITfRange *pRange;
    VARIANT var;

    ok_hr(pFixture->pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty), S_OK);
    if (!pProperty)
        return;
    pRange = Range_New(pFixture, ec, lStart, lEnd);
    V_VT(&var) = VT_I4;
    V_I4(&var) = lValue;
    ok_hr(pProperty->SetValue(ec, pRange, &var), S_OK);
    pRange->Release();
    pProperty->Release();
}

static void ArgumentSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfReadOnlyProperty *pProperty = (ITfReadOnlyProperty *)(ULONG_PTR)1;
    GUID guid;
    HRESULT hr;

    hr = pFixture->pContext->TrackProperties(g_TrackedGuids, _countof(g_TrackedGuids), NULL, 0, NULL);
    ok_hr(hr, E_INVALIDARG);
    hr = pFixture->pContext->TrackProperties(NULL, 0, NULL, 0, &pProperty);
    ok_hr(hr, S_OK);
    ok(pProperty && pProperty != (ITfReadOnlyProperty *)(ULONG_PTR)1, "property %p\n", pProperty);
    if (SUCCEEDED(hr) && pProperty && pProperty != (ITfReadOnlyProperty *)(ULONG_PTR)1)
        pProperty->Release();

    pProperty = NULL;
    hr = pFixture->pContext->TrackProperties(g_TrackedGuids, _countof(g_TrackedGuids), NULL, 0, &pProperty);
    ok_hr(hr, S_OK);
    if (!pProperty)
        return;
    memset(&guid, 0xcc, sizeof(guid));
    hr = pProperty->GetType(&guid);
    ok_hr(hr, E_NOTIMPL);
    ok(IsEqualGUID(guid, g_GuidZero), "type %s\n", wine_dbgstr_guid(&guid));
    pProperty->Release();
}

static void AttributeSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pTarget, *pInside;

    SetAttribute(pFixture, ec, 2, 5, 7);
    pTarget = WholeRange(pFixture, ec);
    check_tracked(ec, pFixture, pTarget, "[0,2]{C=-,A=-} [2,5]{C=-,A=7} [5,10]{C=-,A=-}");
    check_tracked(ec, pFixture, NULL, "hr 80070057");
    pInside = Range_New(pFixture, ec, 3, 8);
    check_tracked(ec, pFixture, pInside, "[3,5]{C=-,A=7} [5,8]{C=-,A=-}");
    pInside->Release();
    check_composing(ec, pFixture, "");
    pTarget->Release();
}

static void StartSession(TfEditCookie ec, void *pv)
{
    TRACK_TEST *pTest = (TRACK_TEST *)pv;
    ITfContextComposition *pContextComposition = NULL;
    ITfRange *pRange, *pTarget;

    ok_hr(pTest->pFixture->pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition), S_OK);
    if (!pContextComposition)
        return;
    pRange = Range_New(pTest->pFixture, ec, 3, 7);
    ok_hr(pContextComposition->StartComposition(ec, pRange, pTest->pSink, &pTest->pComposition), S_OK);
    pRange->Release();
    pContextComposition->Release();
    SetAttribute(pTest->pFixture, ec, 3, 5, 9);

    pTarget = WholeRange(pTest->pFixture, ec);
    check_tracked(ec, pTest->pFixture, pTarget, "[0,3]{C=-,A=-} [3,5]{C=1,A=9} [5,7]{C=1,A=-} [7,10]{C=-,A=-}");
    check_composing(ec, pTest->pFixture, "[3,7]=1");
    pTarget->Release();
}

static void CheckComposingSession(TfEditCookie ec, void *pv)
{
    TRACK_TEST *pTest = (TRACK_TEST *)pv;
    ITfRange *pTarget = WholeRange(pTest->pFixture, ec);

    check_composing(ec, pTest->pFixture, "[3,7]=1");
    check_tracked(ec, pTest->pFixture, pTarget, "[0,3]{C=-,A=-} [3,5]{C=1,A=9} [5,7]{C=1,A=-} [7,10]{C=-,A=-}");
    pTarget->Release();
}

static void EndSession(TfEditCookie ec, void *pv)
{
    TRACK_TEST *pTest = (TRACK_TEST *)pv;
    ITfRange *pTarget;

    if (!pTest->pComposition)
        return;
    ok_hr(pTest->pComposition->EndComposition(ec), S_OK);
    pTarget = WholeRange(pTest->pFixture, ec);
    check_composing(ec, pTest->pFixture, "");
    check_tracked(ec, pTest->pFixture, pTarget, "[0,3]{C=-,A=-} [3,5]{C=-,A=9} [5,10]{C=-,A=-}");
    pTarget->Release();
}

START_TEST(trackprop)
{
    TSF_FIXTURE fixture;
    TRACK_TEST test = { &fixture };

    CoInitialize(NULL);
    if (Fixture_Init(&fixture, L"0123456789"))
    {
        ok_hr(Fixture_RunSync(&fixture, ArgumentSession, &fixture, TF_ES_SYNC | TF_ES_READ), S_OK);
        ok_hr(Fixture_RunSync(&fixture, AttributeSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE), S_OK);
    }
    Fixture_Done(&fixture);
    if (Fixture_Init(&fixture, L"0123456789"))
    {
        test.pSink = new CTrackCompositionSink();
        ok_hr(Fixture_RunSync(&fixture, StartSession, &test, TF_ES_SYNC | TF_ES_READWRITE), S_OK);
        ok_hr(Fixture_RunSync(&fixture, CheckComposingSession, &test, TF_ES_SYNC | TF_ES_READ), S_OK);
        ok_hr(Fixture_RunSync(&fixture, EndSession, &test, TF_ES_SYNC | TF_ES_READWRITE), S_OK);
        if (test.pComposition)
            test.pComposition->Release();
        test.pSink->Release();
    }
    Fixture_Done(&fixture);
    CoUninitialize();
}
