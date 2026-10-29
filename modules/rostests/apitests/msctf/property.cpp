/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfProperty
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static const GUID GUID_PROP_TEST = {0x6b3a3f20, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

#define check_spans(ec, p, n, s) check_spans_(__LINE__, ec, p, n, s)
static void check_spans_(int line, TfEditCookie ec, ITfProperty *pProperty, ULONG cExpected, const char *pszExpected)
{
    IEnumTfRanges *pEnum = NULL;
    ITfRange *pRange;
    ULONG cFetched, cSpans = 0;
    char szSpans[256] = "";
    HRESULT hr = pProperty->EnumRanges(ec, &pEnum, NULL);

    ok_(__FILE__, line)(hr == S_OK, "EnumRanges returned 0x%lx\n", hr);
    while (pEnum && pEnum->Next(1, &pRange, &cFetched) == S_OK && cFetched)
    {
        VARIANT var;
        LONG s, e;
        char sz[48];
        Range_GetExtent(ec, pRange, &s, &e);
        VariantInit(&var);
        pProperty->GetValue(ec, pRange, &var);
        StringCchPrintfA(sz, _countof(sz), "%s[%ld,%ld]=%ld", cSpans ? " " : "", s, e,
                         V_VT(&var) == VT_I4 ? V_I4(&var) : -1);
        StringCchCatA(szSpans, _countof(szSpans), sz);
        VariantClear(&var);
        pRange->Release();
        ++cSpans;
    }
    if (pEnum)
        pEnum->Release();
    ok_(__FILE__, line)(cSpans == cExpected && !strcmp(szSpans, pszExpected), "spans are \"%s\", expected \"%s\"\n",
                        szSpans, pszExpected);
}

static HRESULT SetI4(TfEditCookie ec, ITfProperty *pProperty, ITfRange *pRange, LONG lValue)
{
    VARIANT var;
    V_VT(&var) = VT_I4;
    V_I4(&var) = lValue;
    return pProperty->SetValue(ec, pRange, &var);
}

static void PropertySession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfProperty *pProperty = NULL;
    ITfRange *pRange, *pFound = NULL;
    VARIANT var;
    LONG s, e;
    HRESULT hr;

    hr = pFixture->pContext->GetProperty(GUID_PROP_TEST, &pProperty);
    ok_hr(hr, E_FAIL);
    ok(pProperty == NULL, "property %p\n", pProperty);
    if (pProperty)
        pProperty->Release();
    pProperty = NULL;

    hr = pFixture->pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty);
    ok_hr(hr, S_OK);
    if (!pProperty)
        return;
    check_spans(ec, pProperty, 0, "");

    pRange = Range_New(pFixture, ec, 2, 5);
    ok_hr(SetI4(ec, pProperty, pRange, 7), S_OK);
    VariantInit(&var);
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_OK);
    ok(V_VT(&var) == VT_I4 && V_I4(&var) == 7, "vt %d\n", V_VT(&var));
    pRange->Release();
    check_spans(ec, pProperty, 1, "[2,5]=7");

    pRange = Range_New(pFixture, ec, 0, 1);
    VariantInit(&var);
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_FALSE);
    ok_int(V_VT(&var), VT_EMPTY);
    pRange->Release();

    pRange = Range_New(pFixture, ec, 1, 3);
    VariantInit(&var);
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_FALSE);
    ok_int(V_VT(&var), VT_EMPTY);
    pRange->Release();

    pRange = Range_New(pFixture, ec, 4, 7);
    ok_hr(SetI4(ec, pProperty, pRange, 8), S_OK);
    pRange->Release();
    check_spans(ec, pProperty, 2, "[2,4]=7 [4,7]=8");

    pRange = Range_New(pFixture, ec, 3, 3);
    hr = pProperty->FindRange(ec, pRange, &pFound, TF_ANCHOR_START);
    ok_hr(hr, S_OK);
    if (pFound)
    {
        Range_GetExtent(ec, pFound, &s, &e);
        ok(s == 2 && e == 4, "found [%ld,%ld]\n", s, e);
        pFound->Release();
        pFound = NULL;
    }
    pRange->Release();

    pRange = Range_New(pFixture, ec, 8, 9);
    hr = pProperty->FindRange(ec, pRange, &pFound, TF_ANCHOR_START);
    ok_hr(hr, S_FALSE);
    ok(pFound == NULL, "found %p\n", pFound);
    pRange->Release();

    pRange = Range_New(pFixture, ec, 3, 5);
    ok_hr(pProperty->Clear(ec, pRange), S_OK);
    pRange->Release();
    check_spans(ec, pProperty, 3, "[2,3]=7 [3,5]=-1 [5,7]=8");

    pRange = Range_New(pFixture, ec, 6, 6);
    ok_hr(pRange->SetText(ec, 0, L"xy", 2), S_OK);
    pRange->Release();
    check_spans(ec, pProperty, 5, "[2,3]=7 [3,5]=-1 [5,6]=8 [6,8]=-1 [8,9]=8");

    pRange = Range_New(pFixture, ec, 1, 6);
    ok_hr(pRange->SetText(ec, 0, L"", 0), S_OK);
    pRange->Release();
    check_spans(ec, pProperty, 1, "[3,4]=8");

    ok_hr(pProperty->Clear(ec, NULL), S_OK);
    check_spans(ec, pProperty, 0, "");

    pProperty->Release();
}

static const GUID GUID_ATTR_TEST = {0x6b3a3f21, 0x51d2, 0x4c1e, {0x9a, 0x37, 0x1f, 0x52, 0x8c, 0x0e, 0x4d, 0x71}};

static void AppPropertySession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfReadOnlyProperty *pProperty = NULL;
    ITfRange *pRange = NULL;
    IEnumTfRanges *pEnum = NULL;
    GUID guid;
    VARIANT var;
    LONG lCount;
    HRESULT hr;

    hr = pFixture->pContext->GetAppProperty(GUID_ATTR_TEST, NULL);
    ok_hr(hr, E_INVALIDARG);
    Log_Reset();
    hr = pFixture->pContext->GetAppProperty(GUID_ATTR_TEST, &pProperty);
    ok_hr(hr, S_OK);
    ok_str(Log_Get(), "supported(0)");
    if (!pProperty)
        return;
    hr = pProperty->GetType(&guid);
    ok_hr(hr, S_OK);
    ok(IsEqualGUID(guid, GUID_ATTR_TEST), "wrong type\n");

    pFixture->pContext->GetStart(ec, &pRange);
    pRange->ShiftEnd(ec, 4, &lCount, NULL);
    pRange->ShiftStart(ec, 3, &lCount, NULL);
    Log_Reset();
    VariantInit(&var);
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_OK);
    ok(V_VT(&var) == VT_I4 && V_I4(&var) == 7, "value at [3,4): vt %d\n", V_VT(&var));
    ok_str(Log_Get(), "at(3,0)");
    VariantClear(&var);

    pRange->ShiftStart(ec, -3, &lCount, NULL);
    pRange->ShiftEnd(ec, -3, &lCount, NULL);
    Log_Reset();
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_OK);
    ok(V_VT(&var) == VT_EMPTY, "value at [0,1): vt %d\n", V_VT(&var));
    ok_str(Log_Get(), "at(0,0)");
    VariantClear(&var);

    pRange->ShiftEnd(ec, 9, &lCount, NULL);
    Log_Reset();
    hr = pProperty->GetValue(ec, pRange, &var);
    ok_hr(hr, S_OK);
    ok(V_VT(&var) == VT_EMPTY, "value at [0,10): vt %d\n", V_VT(&var));
    ok_str(Log_Get(), "at(0,0)");
    VariantClear(&var);

    Log_Reset();
    hr = pProperty->EnumRanges(ec, &pEnum, NULL);
    ok_hr(hr, E_INVALIDARG);
    ok_str(Log_Get(), "");
    if (pEnum)
        pEnum->Release();
    pRange->Release();
    pProperty->Release();
}

START_TEST(property)
{
    TSF_FIXTURE fixture;
    HRESULT hr;

    CoInitialize(NULL);
    if (Fixture_Init(&fixture, L"0123456789"))
    {
        hr = Fixture_RunSync(&fixture, PropertySession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
        ok_hr(hr, S_OK);
    }
    Fixture_Done(&fixture);
    if (Fixture_Init(&fixture, L"0123456789"))
    {
        fixture.pStore->m_fAttr = TRUE;
        fixture.pStore->m_attrId = GUID_ATTR_TEST;
        fixture.pStore->m_attrStart = 2;
        fixture.pStore->m_attrEnd = 5;
        fixture.pStore->m_attrValue = 7;
        hr = Fixture_RunSync(&fixture, AppPropertySession, &fixture, TF_ES_SYNC | TF_ES_READ);
        ok_hr(hr, S_OK);
    }
    Fixture_Done(&fixture);
    CoUninitialize();
}
