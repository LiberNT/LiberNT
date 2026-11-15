/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the flags GetDateFormat and GetTimeFormat accept and the reading-order marks they add
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define LRM 0x200E
#define RLM 0x200F

static const SYSTEMTIME TestTime = { 2026, 11, 0, 15, 10, 4, 2, 0 };
static const LCID EnUs = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);

static BOOL DateFlagValid(DWORD Flags, BOOL HasFormat, BOOL Ansi)
{
    DWORD Reading = Flags & (DATE_LTRREADING | DATE_RTLREADING | DATE_AUTOLAYOUT);
    DWORD Kind = Flags & (DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH);

    if (Flags & ~(0x7F | LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE))
        return FALSE;
    if (Reading & (Reading - 1))
        return FALSE;
    if (Kind & (Kind - 1))
        return FALSE;
    if (Ansi && (Flags & (DATE_LTRREADING | DATE_RTLREADING)))
        return FALSE;
    if (HasFormat && (Kind || (Flags & LOCALE_NOUSEROVERRIDE)))
        return FALSE;
    return TRUE;
}

static BOOL TimeFlagValid(DWORD Flags, BOOL HasFormat)
{
    if (Flags & ~(0xF | LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE))
        return FALSE;
    if (HasFormat && (Flags & LOCALE_NOUSEROVERRIDE))
        return FALSE;
    return TRUE;
}

static void CheckResult(const char *Api, DWORD Flags, BOOL HasFormat, int Ret, DWORD Error, BOOL Valid)
{
    if (Valid)
        ok(Ret > 0, "%s flags 0x%08lx format %d failed with %lu\n", Api, Flags, HasFormat, Error);
    else
        ok(Ret == 0 && Error == ERROR_INVALID_FLAGS, "%s flags 0x%08lx format %d returned %d error %lu, expected ERROR_INVALID_FLAGS\n",
           Api, Flags, HasFormat, Ret, Error);
}

static void TestFlags(DWORD Flags)
{
    WCHAR Buffer[128];
    CHAR AnsiBuffer[128];
    BOOL HasFormat;
    DWORD Error;
    int Ret;

    for (HasFormat = FALSE; HasFormat <= TRUE; HasFormat++)
    {
        SetLastError(0xdeadbeef);
        Ret = GetDateFormatEx(L"en-US", Flags, &TestTime, HasFormat ? L"yyyy'-'MM'-'dd" : NULL, Buffer, ARRAYSIZE(Buffer), NULL);
        Error = GetLastError();
        CheckResult("GetDateFormatEx", Flags, HasFormat, Ret, Error, DateFlagValid(Flags, HasFormat, FALSE));

        SetLastError(0xdeadbeef);
        Ret = GetDateFormatW(EnUs, Flags, &TestTime, HasFormat ? L"yyyy'-'MM'-'dd" : NULL, Buffer, ARRAYSIZE(Buffer));
        Error = GetLastError();
        CheckResult("GetDateFormatW", Flags, HasFormat, Ret, Error, DateFlagValid(Flags, HasFormat, FALSE));

        SetLastError(0xdeadbeef);
        Ret = GetDateFormatA(LOCALE_INVARIANT, Flags, &TestTime, HasFormat ? "yyyy'-'MM'-'dd" : NULL, AnsiBuffer, ARRAYSIZE(AnsiBuffer));
        Error = GetLastError();
        CheckResult("GetDateFormatA", Flags, HasFormat, Ret, Error, DateFlagValid(Flags, HasFormat, TRUE));

        SetLastError(0xdeadbeef);
        Ret = GetTimeFormatEx(L"en-US", Flags, &TestTime, HasFormat ? L"HH'-'mm'-'ss" : NULL, Buffer, ARRAYSIZE(Buffer));
        Error = GetLastError();
        CheckResult("GetTimeFormatEx", Flags, HasFormat, Ret, Error, TimeFlagValid(Flags, HasFormat));

        SetLastError(0xdeadbeef);
        Ret = GetTimeFormatW(EnUs, Flags, &TestTime, HasFormat ? L"HH'-'mm'-'ss" : NULL, Buffer, ARRAYSIZE(Buffer));
        Error = GetLastError();
        CheckResult("GetTimeFormatW", Flags, HasFormat, Ret, Error, TimeFlagValid(Flags, HasFormat));

        SetLastError(0xdeadbeef);
        Ret = GetTimeFormatA(LOCALE_INVARIANT, Flags, &TestTime, HasFormat ? "HH'-'mm'-'ss" : NULL, AnsiBuffer, ARRAYSIZE(AnsiBuffer));
        Error = GetLastError();
        CheckResult("GetTimeFormatA", Flags, HasFormat, Ret, Error, TimeFlagValid(Flags, HasFormat));
    }
}

static void TestFlagValidation(void)
{
    static const DWORD Combinations[] =
    {
        0x3, 0x5, 0x6, 0x9, 0xA, 0xC, 0xF, 0x11, 0x12, 0x14, 0x18, 0x21, 0x30, 0x41, 0x44, 0x50, 0x60, 0x70,
        LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE, LOCALE_USE_CP_ACP | DATE_LTRREADING, LOCALE_NOUSEROVERRIDE | DATE_SHORTDATE,
    };
    ULONG Bit, Index;

    TestFlags(0);
    for (Bit = 0; Bit < 32; Bit++)
        TestFlags(1UL << Bit);
    for (Index = 0; Index < ARRAYSIZE(Combinations); Index++)
        TestFlags(Combinations[Index]);
}

static void CheckDate(PCWSTR Locale, DWORD Flags, PCWSTR Format, PCWSTR Expected)
{
    WCHAR Buffer[128];
    int Ret, Size;

    SetLastError(0xdeadbeef);
    Size = GetDateFormatEx(Locale, Flags, &TestTime, Format, NULL, 0, NULL);
    Ret = GetDateFormatEx(Locale, Flags, &TestTime, Format, Buffer, ARRAYSIZE(Buffer), NULL);
    ok(Ret == (int)wcslen(Expected) + 1, "%ls 0x%lx %ls: returned %d error %lu, expected %d\n",
       Locale, Flags, Format ? Format : L"(default)", Ret, GetLastError(), (int)wcslen(Expected) + 1);
    ok(Size == Ret, "%ls 0x%lx %ls: size query returned %d, format returned %d\n", Locale, Flags, Format ? Format : L"(default)", Size, Ret);
    if (Ret > 0)
        ok(!wcscmp(Buffer, Expected), "%ls 0x%lx %ls: got %s, expected %s\n",
           Locale, Flags, Format ? Format : L"(default)", wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));
}

static void CheckLeadingMark(PCWSTR Locale, DWORD Flags, PCWSTR Format, WCHAR Mark)
{
    WCHAR Buffer[128];
    int Ret;

    Ret = GetDateFormatEx(Locale, Flags, &TestTime, Format, Buffer, ARRAYSIZE(Buffer), NULL);
    ok(Ret > 1, "%ls 0x%lx %ls: returned %d error %lu\n", Locale, Flags, Format ? Format : L"(default)", Ret, GetLastError());
    if (Ret > 1)
        ok(Buffer[0] == Mark, "%ls 0x%lx %ls: got %s, expected a leading U+%04X\n",
           Locale, Flags, Format ? Format : L"(default)", wine_dbgstr_w(Buffer), Mark);
}

static void TestReadingMarks(void)
{
    CheckDate(L"en-US", DATE_LTRREADING, NULL, L"\x200e" L"11/\x200e" L"15/\x200e" L"2026");
    CheckDate(L"en-US", DATE_RTLREADING, NULL, L"\x200f" L"11/\x200f" L"15/\x200f" L"2026");
    CheckDate(L"en-US", DATE_AUTOLAYOUT, NULL, L"\x200e" L"11/\x200e" L"15/\x200e" L"2026");
    CheckDate(L"en-US", DATE_LTRREADING | DATE_LONGDATE, NULL,
              L"\x200e" L"Sunday, \x200e" L"November \x200e" L"15, \x200e" L"2026");
    CheckDate(L"en-US", DATE_LTRREADING | DATE_YEARMONTH, NULL, L"\x200e" L"November \x200e" L"2026");
    CheckDate(L"en-US", DATE_LTRREADING, L"yyyy'-'MM'-'dd",
              L"\x200e" L"2026\x200e" L"-\x200e" L"11\x200e" L"-\x200e" L"15");
    CheckDate(L"en-US", DATE_RTLREADING, L"ddd', 'MMMM d",
              L"\x200f" L"Sun\x200f" L", \x200f" L"November \x200f" L"15");
    CheckDate(L"en-US", DATE_AUTOLAYOUT, L"'Day 'd", L"\x200e" L"Day \x200e" L"15");
    CheckDate(L"en-US", DATE_AUTOLAYOUT, L"yyyy''MM", L"\x200e" L"2026\x200e\x200e" L"11");
    CheckDate(L"en-US", DATE_AUTOLAYOUT | LOCALE_USE_CP_ACP, L"dd/MM/yyyy",
              L"\x200e" L"15/\x200e" L"11/\x200e" L"2026");

    CheckLeadingMark(L"ar-EG", DATE_AUTOLAYOUT, L"d/M/yyyy", RLM);
    CheckLeadingMark(L"ar-SA", DATE_AUTOLAYOUT, NULL, RLM);
    CheckLeadingMark(L"fa-IR", DATE_AUTOLAYOUT, L"yyyy", RLM);
    CheckLeadingMark(L"ur-PK", DATE_AUTOLAYOUT, L"d/M/yyyy", RLM);
    CheckLeadingMark(L"he-IL", DATE_AUTOLAYOUT, L"d/M/yyyy", LRM);
    CheckLeadingMark(L"he-IL", DATE_AUTOLAYOUT, L"yyyy", LRM);
    CheckLeadingMark(L"he-IL", DATE_AUTOLAYOUT, L"d MMM yyyy", RLM);
    CheckLeadingMark(L"he-IL", DATE_AUTOLAYOUT, L"dddd", RLM);
    CheckLeadingMark(L"ar-SA", DATE_LTRREADING, NULL, LRM);
    CheckLeadingMark(L"ja-JP", DATE_AUTOLAYOUT, NULL, LRM);
}

START_TEST(DateTimeFormatFlags)
{
    TestFlagValidation();
    TestReadingMarks();
}
