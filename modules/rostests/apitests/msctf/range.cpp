/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ITfRange
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define check_extent(ec, range, s, e) check_extent_(__LINE__, ec, range, s, e)
static void check_extent_(int line, TfEditCookie ec, ITfRange *pRange, LONG s, LONG e)
{
    LONG start, end;
    Range_GetExtent(ec, pRange, &start, &end);
    ok_(__FILE__, line)(start == s && end == e, "extent is [%ld,%ld], expected [%ld,%ld]\n", start, end, s, e);
}

static void ShiftSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pRange = NULL, *pHalt;
    TF_HALTCOND halt;
    LONG cch;
    HRESULT hr;

    hr = pFixture->pContext->GetStart(ec, &pRange);
    ok_hr(hr, S_OK);
    if (!pRange)
        return;
    check_extent(ec, pRange, 0, 0);

    hr = pRange->ShiftEnd(ec, 5, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 5);
    check_extent(ec, pRange, 0, 5);

    hr = pRange->ShiftStart(ec, 2, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 2);
    check_extent(ec, pRange, 2, 5);

    hr = pRange->ShiftEnd(ec, LONG_MAX, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 5);
    check_extent(ec, pRange, 2, 10);

    hr = pRange->ShiftStart(ec, LONG_MIN, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, -2);
    check_extent(ec, pRange, 0, 10);

    hr = pRange->ShiftStart(ec, LONG_MAX, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 10);
    check_extent(ec, pRange, 10, 10);

    hr = pRange->ShiftEnd(ec, LONG_MIN, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, -10);
    check_extent(ec, pRange, 0, 0);

    hr = pRange->ShiftEnd(ec, 0, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 0);

    hr = pRange->ShiftEnd(ec, 20, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 10);
    check_extent(ec, pRange, 0, 10);

    hr = pRange->ShiftStart(ec, -5, &cch, NULL);
    ok_hr(hr, S_OK);
    ok_long(cch, 0);
    check_extent(ec, pRange, 0, 10);

    pHalt = Range_New(pFixture, ec, 3, 6);
    check_extent(ec, pHalt, 3, 6);
    halt.pHaltRange = pHalt;
    halt.dwFlags = 0;

    pRange->Collapse(ec, TF_ANCHOR_START);
    halt.aHaltPos = TF_ANCHOR_START;
    hr = pRange->ShiftEnd(ec, 8, &cch, &halt);
    ok_hr(hr, S_OK);
    ok_long(cch, 3);
    check_extent(ec, pRange, 0, 3);

    pRange->Collapse(ec, TF_ANCHOR_START);
    halt.aHaltPos = TF_ANCHOR_END;
    hr = pRange->ShiftEnd(ec, 8, &cch, &halt);
    ok_hr(hr, S_OK);
    ok_long(cch, 6);
    check_extent(ec, pRange, 0, 6);

    hr = pRange->ShiftEnd(ec, LONG_MAX, &cch, NULL);
    pRange->Collapse(ec, TF_ANCHOR_END);
    check_extent(ec, pRange, 10, 10);
    hr = pRange->ShiftStart(ec, -8, &cch, &halt);
    ok_hr(hr, S_OK);
    ok_long(cch, -4);
    check_extent(ec, pRange, 6, 10);

    hr = pRange->ShiftStart(ec, 1, NULL, NULL);
    ok_hr(hr, E_INVALIDARG);

    pHalt->Release();
    pRange->Release();
}

static void CompareSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pA = Range_New(pFixture, ec, 2, 5);
    ITfRange *pB = Range_New(pFixture, ec, 4, 8);
    ITfRange *pC = NULL;
    LONG lResult;
    BOOL fEqual;
    HRESULT hr;

    if (!pA || !pB)
        return;
    check_extent(ec, pA, 2, 5);
    check_extent(ec, pB, 4, 8);

    hr = pA->CompareStart(ec, pB, TF_ANCHOR_START, &lResult);
    ok_hr(hr, S_OK);
    ok_long(lResult, -1);
    hr = pA->CompareStart(ec, pB, TF_ANCHOR_END, &lResult);
    ok_hr(hr, S_OK);
    ok_long(lResult, -1);
    hr = pA->CompareEnd(ec, pB, TF_ANCHOR_START, &lResult);
    ok_hr(hr, S_OK);
    ok_long(lResult, 1);
    hr = pB->CompareEnd(ec, pA, TF_ANCHOR_END, &lResult);
    ok_hr(hr, S_OK);
    ok_long(lResult, 1);

    hr = pA->IsEqualStart(ec, pB, TF_ANCHOR_START, &fEqual);
    ok_hr(hr, S_OK);
    ok_int(fEqual, FALSE);
    hr = pA->Clone(&pC);
    ok_hr(hr, S_OK);
    hr = pA->IsEqualEnd(ec, pC, TF_ANCHOR_END, &fEqual);
    ok_hr(hr, S_OK);
    ok_int(fEqual, TRUE);

    hr = pC->ShiftStartToRange(ec, pB, TF_ANCHOR_END);
    ok_hr(hr, S_OK);
    check_extent(ec, pC, 8, 8);

    pA->Clone(&pC);
    hr = pC->ShiftEndToRange(ec, pB, TF_ANCHOR_START);
    ok_hr(hr, S_OK);
    check_extent(ec, pC, 2, 4);
    hr = pC->ShiftEndToRange(ec, pA, TF_ANCHOR_START);
    ok_hr(hr, S_OK);
    check_extent(ec, pC, 2, 2);
    hr = pC->ShiftEndToRange(ec, pB, TF_ANCHOR_START);
    hr = pC->ShiftStartToRange(ec, pB, TF_ANCHOR_END);
    ok_hr(hr, S_OK);
    check_extent(ec, pC, 8, 8);

    hr = pA->Collapse(ec, TF_ANCHOR_END);
    ok_hr(hr, S_OK);
    check_extent(ec, pA, 5, 5);
    hr = pB->Collapse(ec, TF_ANCHOR_START);
    ok_hr(hr, S_OK);
    check_extent(ec, pB, 4, 4);

    pC->Release();
    pA->Release();
    pB->Release();
}

static void TextSession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pRange = Range_New(pFixture, ec, 2, 5);
    WCHAR sz[16];
    ULONG cch;
    HRESULT hr;

    if (!pRange)
        return;

    ZeroMemory(sz, sizeof(sz));
    hr = pRange->GetText(ec, 0, sz, _countof(sz), &cch);
    ok_hr(hr, S_OK);
    ok_long(cch, 3);
    ok_wstr(sz, L"234");
    check_extent(ec, pRange, 2, 5);

    ZeroMemory(sz, sizeof(sz));
    hr = pRange->GetText(ec, TF_TF_MOVESTART, sz, 2, &cch);
    ok_hr(hr, S_OK);
    ok_long(cch, 2);
    ok_wstr(sz, L"23");
    check_extent(ec, pRange, 4, 5);

    ZeroMemory(sz, sizeof(sz));
    hr = pRange->GetText(ec, TF_TF_IGNOREEND, sz, 4, &cch);
    ok_hr(hr, S_OK);
    ok_long(cch, 4);
    ok_wstr(sz, L"4567");
    check_extent(ec, pRange, 4, 5);

    ZeroMemory(sz, sizeof(sz));
    hr = pRange->GetText(ec, TF_TF_MOVESTART | TF_TF_IGNOREEND, sz, 3, &cch);
    ok_hr(hr, S_OK);
    ok_long(cch, 3);
    ok_wstr(sz, L"456");
    check_extent(ec, pRange, 7, 7);

    pRange->Release();
    pRange = Range_New(pFixture, ec, 2, 5);
    pFixture->pStore->m_lastSetTextFlags = 0xdeadbeef;
    hr = pRange->SetText(ec, 0, L"ab", 2);
    ok_hr(hr, S_OK);
    ok_wstr(pFixture->pStore->Text(), L"01ab56789");
    ok_long(pFixture->pStore->m_lastSetTextFlags, 0);
    check_extent(ec, pRange, 2, 4);

    hr = pRange->SetText(ec, TF_ST_CORRECTION, L"xyz", 3);
    ok_hr(hr, S_OK);
    ok_wstr(pFixture->pStore->Text(), L"01xyz56789");
    ok_long(pFixture->pStore->m_lastSetTextFlags, TS_ST_CORRECTION);
    check_extent(ec, pRange, 2, 5);

    hr = pRange->SetText(ec, 0, L"", 0);
    ok_hr(hr, S_OK);
    ok_wstr(pFixture->pStore->Text(), L"0156789");
    check_extent(ec, pRange, 2, 2);

    pRange->Release();
}

static void GravitySession(TfEditCookie ec, void *pv)
{
    TSF_FIXTURE *pFixture = (TSF_FIXTURE *)pv;
    ITfRange *pRange = Range_New(pFixture, ec, 3, 5);
    ITfRange *pInsert;
    TfGravity gStart, gEnd;
    HRESULT hr;

    if (!pRange)
        return;

    hr = pRange->GetGravity(&gStart, &gEnd);
    ok_hr(hr, S_OK);
    ok_long(gStart, TF_GRAVITY_BACKWARD);
    ok_long(gEnd, TF_GRAVITY_FORWARD);

    pInsert = Range_New(pFixture, ec, 3, 3);
    pInsert->SetText(ec, 0, L"a", 1);
    pInsert->Release();
    check_extent(ec, pRange, 3, 6);

    pInsert = Range_New(pFixture, ec, 6, 6);
    pInsert->SetText(ec, 0, L"b", 1);
    pInsert->Release();
    check_extent(ec, pRange, 3, 7);

    pInsert = Range_New(pFixture, ec, 4, 4);
    pInsert->SetText(ec, 0, L"c", 1);
    pInsert->Release();
    check_extent(ec, pRange, 3, 8);

    pInsert = Range_New(pFixture, ec, 0, 1);
    pInsert->SetText(ec, 0, L"", 0);
    pInsert->Release();
    check_extent(ec, pRange, 2, 7);

    hr = pRange->SetGravity(ec, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD);
    ok_hr(hr, S_OK);
    hr = pRange->GetGravity(&gStart, &gEnd);
    ok_long(gStart, TF_GRAVITY_FORWARD);
    ok_long(gEnd, TF_GRAVITY_BACKWARD);

    pInsert = Range_New(pFixture, ec, 2, 2);
    pInsert->SetText(ec, 0, L"d", 1);
    pInsert->Release();
    check_extent(ec, pRange, 3, 8);

    pInsert = Range_New(pFixture, ec, 8, 8);
    pInsert->SetText(ec, 0, L"e", 1);
    pInsert->Release();
    check_extent(ec, pRange, 3, 8);

    pInsert = Range_New(pFixture, ec, 2, 9);
    pInsert->SetText(ec, 0, L"", 0);
    pInsert->Release();
    check_extent(ec, pRange, 2, 2);

    pRange->Release();
}

START_TEST(range)
{
    TSF_FIXTURE fixture;
    HRESULT hr;

    CoInitialize(NULL);

    if (Fixture_Init(&fixture, L"0123456789"))
    {
        hr = Fixture_RunSync(&fixture, ShiftSession, &fixture, TF_ES_SYNC | TF_ES_READ);
        ok_hr(hr, S_OK);
        hr = Fixture_RunSync(&fixture, CompareSession, &fixture, TF_ES_SYNC | TF_ES_READ);
        ok_hr(hr, S_OK);
    }
    Fixture_Done(&fixture);

    if (Fixture_Init(&fixture, L"0123456789"))
    {
        hr = Fixture_RunSync(&fixture, TextSession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
        ok_hr(hr, S_OK);
    }
    Fixture_Done(&fixture);

    if (Fixture_Init(&fixture, L"0123456789"))
    {
        hr = Fixture_RunSync(&fixture, GravitySession, &fixture, TF_ES_SYNC | TF_ES_READWRITE);
        ok_hr(hr, S_OK);
    }
    Fixture_Done(&fixture);

    CoUninitialize();
}
