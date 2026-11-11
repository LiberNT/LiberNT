/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlCompareAltitudes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef LONG (NTAPI *FN_RtlCompareAltitudes)(PCUNICODE_STRING, PCUNICODE_STRING);

static FN_RtlCompareAltitudes pRtlCompareAltitudes;

static
LONG
Sign(
    _In_ LONG Value)
{
    return (Value > 0) - (Value < 0);
}

static
VOID
CheckAltitudes(
    _In_ PCWSTR First,
    _In_ PCWSTR Second,
    _In_ LONG Expected)
{
    UNICODE_STRING Altitude1, Altitude2;
    LONG Result;

    RtlInitUnicodeString(&Altitude1, First);
    RtlInitUnicodeString(&Altitude2, Second);
    Result = pRtlCompareAltitudes(&Altitude1, &Altitude2);
    ok(Sign(Result) == Expected, "\"%S\" vs \"%S\" returned %ld, expected sign %ld\n", First, Second, Result, Expected);
    Result = pRtlCompareAltitudes(&Altitude2, &Altitude1);
    ok(Sign(Result) == -Expected, "\"%S\" vs \"%S\" returned %ld, expected sign %ld\n", Second, First, Result, -Expected);
}

START_TEST(RtlCompareAltitudes)
{
    UNICODE_STRING Counted, Plain;
    WCHAR Buffer[] = L"38510099";
    LONG Result;

    pRtlCompareAltitudes = (FN_RtlCompareAltitudes)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                  "RtlCompareAltitudes");
    if (!pRtlCompareAltitudes)
    {
        skip("RtlCompareAltitudes is not exported\n");
        return;
    }

    CheckAltitudes(L"385100", L"385100", 0);
    CheckAltitudes(L"03333", L"100.123456", 1);
    CheckAltitudes(L"100", L"0100", 0);
    CheckAltitudes(L"0", L"000", 0);
    CheckAltitudes(L"100.5", L"100.50", 0);
    CheckAltitudes(L"100", L"100.0", 0);
    CheckAltitudes(L"9", L"10", -1);
    CheckAltitudes(L"385100", L"385000", 1);
    CheckAltitudes(L"385100.5", L"385100", 1);
    CheckAltitudes(L"385100.05", L"385100.5", -1);
    CheckAltitudes(L"1.10", L"1.9", -1);
    CheckAltitudes(L"0.001", L"0", 1);
    CheckAltitudes(L"429998", L"40000", 1);

    Counted.Buffer = Buffer;
    Counted.Length = 6 * sizeof(WCHAR);
    Counted.MaximumLength = sizeof(Buffer);
    RtlInitUnicodeString(&Plain, L"385100");
    Result = pRtlCompareAltitudes(&Counted, &Plain);
    ok(Result == 0, "Counted altitude compared %ld, expected 0\n", Result);
}
