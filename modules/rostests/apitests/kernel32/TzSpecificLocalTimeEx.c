/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SystemTimeToTzSpecificLocalTimeEx and TzSpecificLocalTimeToSystemTimeEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

typedef struct _TZ_CASE
{
    const DYNAMIC_TIME_ZONE_INFORMATION *TimeZone;
    SYSTEMTIME Input;
    WORD ExpectedHour;
} TZ_CASE;

static DYNAMIC_TIME_ZONE_INFORMATION TzEast, TzWest, TzSouth;
static BOOL (WINAPI *pSystemTimeToTzSpecificLocalTimeEx)(const DYNAMIC_TIME_ZONE_INFORMATION *, const SYSTEMTIME *, LPSYSTEMTIME);
static BOOL (WINAPI *pTzSpecificLocalTimeToSystemTimeEx)(const DYNAMIC_TIME_ZONE_INFORMATION *, const SYSTEMTIME *, LPSYSTEMTIME);

static void SetRule(SYSTEMTIME *Rule, WORD Month, WORD DayOfWeek, WORD Week, WORD Hour)
{
    ZeroMemory(Rule, sizeof(*Rule));
    Rule->wMonth = Month;
    Rule->wDayOfWeek = DayOfWeek;
    Rule->wDay = Week;
    Rule->wHour = Hour;
}

static void InitStaticZones(void)
{
    ZeroMemory(&TzEast, sizeof(TzEast));
    TzEast.Bias = -600;
    TzEast.DaylightBias = -60;
    SetRule(&TzEast.StandardDate, 10, 0, 5, 3);
    SetRule(&TzEast.DaylightDate, 3, 0, 5, 2);
    TzEast.DynamicDaylightTimeDisabled = TRUE;

    ZeroMemory(&TzWest, sizeof(TzWest));
    TzWest.Bias = 240;
    TzWest.DaylightBias = -60;
    SetRule(&TzWest.StandardDate, 10, 0, 4, 2);
    SetRule(&TzWest.DaylightDate, 4, 0, 1, 2);
    TzWest.DynamicDaylightTimeDisabled = TRUE;

    ZeroMemory(&TzSouth, sizeof(TzSouth));
    TzSouth.Bias = 240;
    TzSouth.DaylightBias = -60;
    SetRule(&TzSouth.StandardDate, 4, 0, 1, 2);
    SetRule(&TzSouth.DaylightDate, 10, 0, 4, 2);
    TzSouth.DynamicDaylightTimeDisabled = TRUE;
}

static void TestLocalToUtcStatic(void)
{
    /* Wine's TzSpecificLocalTimeToSystemTime table for 2004; a skipped local time is standard time for the Ex function */
    static const TZ_CASE Cases[] =
    {
        { &TzEast,  { 2004, 3, 0, 28, 1, 0, 0, 0 }, 15 },
        { &TzEast,  { 2004, 3, 0, 28, 1, 59, 59, 999 }, 15 },
        { &TzEast,  { 2004, 3, 0, 28, 2, 0, 0, 0 }, 16 },
        { &TzEast,  { 2004, 10, 0, 31, 2, 0, 0, 0 }, 15 },
        { &TzEast,  { 2004, 10, 0, 31, 2, 59, 59, 999 }, 15 },
        { &TzEast,  { 2004, 10, 0, 31, 3, 0, 0, 0 }, 17 },
        { &TzWest,  { 2004, 4, 0, 4, 1, 0, 0, 0 }, 5 },
        { &TzWest,  { 2004, 4, 0, 4, 1, 59, 59, 999 }, 5 },
        { &TzWest,  { 2004, 4, 0, 4, 2, 0, 0, 0 }, 6 },
        { &TzWest,  { 2004, 10, 0, 24, 1, 0, 0, 0 }, 4 },
        { &TzWest,  { 2004, 10, 0, 24, 1, 59, 59, 999 }, 4 },
        { &TzWest,  { 2004, 10, 0, 24, 2, 0, 0, 0 }, 6 },
        { &TzSouth, { 2004, 4, 0, 4, 1, 0, 0, 0 }, 4 },
        { &TzSouth, { 2004, 4, 0, 4, 1, 59, 59, 999 }, 4 },
        { &TzSouth, { 2004, 4, 0, 4, 2, 0, 0, 0 }, 6 },
        { &TzSouth, { 2004, 10, 0, 24, 1, 0, 0, 0 }, 5 },
        { &TzSouth, { 2004, 10, 0, 24, 1, 59, 59, 999 }, 5 },
        { &TzSouth, { 2004, 10, 0, 24, 2, 0, 0, 0 }, 6 },
    };
    SYSTEMTIME Result;
    ULONG i;
    BOOL Ret;

    for (i = 0; i < ARRAY_SIZE(Cases); i++)
    {
        memset(&Result, 0xcc, sizeof(Result));
        Ret = pTzSpecificLocalTimeToSystemTimeEx(Cases[i].TimeZone, &Cases[i].Input, &Result);
        ok(Ret, "Case %lu: TzSpecificLocalTimeToSystemTimeEx failed, error %lu\n", i, GetLastError());
        ok(Result.wHour == Cases[i].ExpectedHour, "Case %lu: got %04u-%02u-%02u %02u:%02u, expected hour %u\n",
           i, Result.wYear, Result.wMonth, Result.wDay, Result.wHour, Result.wMinute, Cases[i].ExpectedHour);
    }
}

static void TestUtcToLocalStatic(void)
{
    /* Wine's SystemTimeToTzSpecificLocalTime table for 2004 */
    static const TZ_CASE Cases[] =
    {
        { &TzEast,  { 2004, 3, 0, 27, 15, 0, 0, 0 }, 1 },
        { &TzEast,  { 2004, 3, 0, 27, 15, 59, 59, 999 }, 1 },
        { &TzEast,  { 2004, 3, 0, 27, 16, 0, 0, 0 }, 3 },
        { &TzEast,  { 2004, 10, 0, 30, 15, 0, 0, 0 }, 2 },
        { &TzEast,  { 2004, 10, 0, 30, 15, 59, 59, 999 }, 2 },
        { &TzEast,  { 2004, 10, 0, 30, 16, 0, 0, 0 }, 2 },
        { &TzWest,  { 2004, 4, 0, 4, 5, 0, 0, 0 }, 1 },
        { &TzWest,  { 2004, 4, 0, 4, 5, 59, 59, 999 }, 1 },
        { &TzWest,  { 2004, 4, 0, 4, 6, 0, 0, 0 }, 3 },
        { &TzWest,  { 2004, 10, 0, 24, 4, 0, 0, 0 }, 1 },
        { &TzWest,  { 2004, 10, 0, 24, 4, 59, 59, 999 }, 1 },
        { &TzWest,  { 2004, 10, 0, 24, 5, 0, 0, 0 }, 1 },
        { &TzSouth, { 2004, 4, 0, 4, 4, 0, 0, 0 }, 1 },
        { &TzSouth, { 2004, 4, 0, 4, 4, 59, 59, 999 }, 1 },
        { &TzSouth, { 2004, 4, 0, 4, 5, 0, 0, 0 }, 1 },
        { &TzSouth, { 2004, 10, 0, 24, 5, 0, 0, 0 }, 1 },
        { &TzSouth, { 2004, 10, 0, 24, 5, 59, 59, 999 }, 1 },
        { &TzSouth, { 2004, 10, 0, 24, 6, 0, 0, 0 }, 3 },
    };
    SYSTEMTIME Result;
    ULONG i;
    BOOL Ret;

    for (i = 0; i < ARRAY_SIZE(Cases); i++)
    {
        memset(&Result, 0xcc, sizeof(Result));
        Ret = pSystemTimeToTzSpecificLocalTimeEx(Cases[i].TimeZone, &Cases[i].Input, &Result);
        ok(Ret, "Case %lu: SystemTimeToTzSpecificLocalTimeEx failed, error %lu\n", i, GetLastError());
        ok(Result.wHour == Cases[i].ExpectedHour, "Case %lu: got %04u-%02u-%02u %02u:%02u, expected hour %u\n",
           i, Result.wYear, Result.wMonth, Result.wDay, Result.wHour, Result.wMinute, Cases[i].ExpectedHour);
    }
}

static void InitEgypt(DYNAMIC_TIME_ZONE_INFORMATION *Info, BOOL DynamicDisabled)
{
    ZeroMemory(Info, sizeof(*Info));
    Info->Bias = -120;
    Info->DaylightBias = -60;
    SetRule(&Info->StandardDate, 10, 4, 5, 23);
    SetRule(&Info->DaylightDate, 4, 4, 5, 23);
    lstrcpyW(Info->TimeZoneKeyName, L"Egypt Standard Time");
    Info->DynamicDaylightTimeDisabled = (BOOLEAN)DynamicDisabled;
}

static void CheckLocalToUtc(const DYNAMIC_TIME_ZONE_INFORMATION *Info, WORD Year, WORD ExpectedHour)
{
    SYSTEMTIME Local = { Year, 7, 0, 1, 12, 0, 0, 0 };
    SYSTEMTIME Utc, Back;
    BOOL Ret;

    memset(&Utc, 0xcc, sizeof(Utc));
    Ret = pTzSpecificLocalTimeToSystemTimeEx(Info, &Local, &Utc);
    ok(Ret, "%u: TzSpecificLocalTimeToSystemTimeEx failed, error %lu\n", Year, GetLastError());
    ok(Utc.wYear == Year && Utc.wMonth == 7 && Utc.wDay == 1 && Utc.wHour == ExpectedHour && Utc.wMinute == 0,
       "%u: got %04u-%02u-%02u %02u:%02u, expected hour %u\n",
       Year, Utc.wYear, Utc.wMonth, Utc.wDay, Utc.wHour, Utc.wMinute, ExpectedHour);

    memset(&Back, 0xcc, sizeof(Back));
    Ret = pSystemTimeToTzSpecificLocalTimeEx(Info, &Utc, &Back);
    ok(Ret, "%u: SystemTimeToTzSpecificLocalTimeEx failed, error %lu\n", Year, GetLastError());
    ok(Back.wYear == Year && Back.wMonth == 7 && Back.wDay == 1 && Back.wHour == 12 && Back.wMinute == 0,
       "%u: round trip got %04u-%02u-%02u %02u:%02u\n", Year, Back.wYear, Back.wMonth, Back.wDay, Back.wHour, Back.wMinute);
}

static void TestDynamic(void)
{
    DYNAMIC_TIME_ZONE_INFORMATION Egypt;

    /* Dynamic DST years from Wine's GetTimeZoneInformationForYear test: no DST in 2013 and 2015, DST from May 15 to September 25 in 2014 */
    InitEgypt(&Egypt, FALSE);
    CheckLocalToUtc(&Egypt, 2013, 10);
    CheckLocalToUtc(&Egypt, 2014, 9);
    CheckLocalToUtc(&Egypt, 2015, 10);

    InitEgypt(&Egypt, TRUE);
    CheckLocalToUtc(&Egypt, 2013, 9);
    CheckLocalToUtc(&Egypt, 2015, 9);
}

static void TestCurrentZone(void)
{
    SYSTEMTIME Utc, LocalEx, Local, Back;
    BOOL Ret;

    GetSystemTime(&Utc);
    Utc.wMilliseconds = 0;

    memset(&LocalEx, 0xcc, sizeof(LocalEx));
    Ret = pSystemTimeToTzSpecificLocalTimeEx(NULL, &Utc, &LocalEx);
    ok(Ret, "SystemTimeToTzSpecificLocalTimeEx(NULL) failed, error %lu\n", GetLastError());
    Ret = SystemTimeToTzSpecificLocalTime(NULL, &Utc, &Local);
    ok(Ret, "SystemTimeToTzSpecificLocalTime(NULL) failed, error %lu\n", GetLastError());
    ok(memcmp(&LocalEx, &Local, sizeof(Local)) == 0, "Ex got %02u:%02u, non-Ex got %02u:%02u\n",
       LocalEx.wHour, LocalEx.wMinute, Local.wHour, Local.wMinute);

    memset(&Back, 0xcc, sizeof(Back));
    Ret = pTzSpecificLocalTimeToSystemTimeEx(NULL, &LocalEx, &Back);
    ok(Ret, "TzSpecificLocalTimeToSystemTimeEx(NULL) failed, error %lu\n", GetLastError());
    ok(memcmp(&Back, &Utc, sizeof(Utc)) == 0, "Round trip got %02u:%02u, expected %02u:%02u\n",
       Back.wHour, Back.wMinute, Utc.wHour, Utc.wMinute);
}

static void TestInvalid(void)
{
    SYSTEMTIME Valid = { 2004, 7, 0, 1, 12, 0, 0, 0 };
    SYSTEMTIME BadMonth = { 2004, 13, 0, 1, 12, 0, 0, 0 };
    DYNAMIC_TIME_ZONE_INFORMATION BadRules;
    SYSTEMTIME Result;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSystemTimeToTzSpecificLocalTimeEx(&TzWest, NULL, &Result);
    ok(!Ret, "NULL universal time succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSystemTimeToTzSpecificLocalTimeEx(&TzWest, &Valid, NULL);
    ok(!Ret, "NULL local time succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pTzSpecificLocalTimeToSystemTimeEx(&TzWest, NULL, &Result);
    ok(!Ret, "NULL local time succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pTzSpecificLocalTimeToSystemTimeEx(&TzWest, &Valid, NULL);
    ok(!Ret, "NULL universal time succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSystemTimeToTzSpecificLocalTimeEx(&TzWest, &BadMonth, &Result);
    ok(!Ret, "Month 13 succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pTzSpecificLocalTimeToSystemTimeEx(&TzWest, &BadMonth, &Result);
    ok(!Ret, "Month 13 succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    BadRules = TzWest;
    BadRules.StandardDate.wMonth = 0;
    SetLastError(0xdeadbeef);
    Ret = pSystemTimeToTzSpecificLocalTimeEx(&BadRules, &Valid, &Result);
    ok(!Ret, "Daylight date without standard date succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pTzSpecificLocalTimeToSystemTimeEx(&BadRules, &Valid, &Result);
    ok(!Ret, "Daylight date without standard date succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());
}

START_TEST(TzSpecificLocalTimeEx)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pSystemTimeToTzSpecificLocalTimeEx = (PVOID)GetProcAddress(Kernel32, "SystemTimeToTzSpecificLocalTimeEx");
    pTzSpecificLocalTimeToSystemTimeEx = (PVOID)GetProcAddress(Kernel32, "TzSpecificLocalTimeToSystemTimeEx");
    if (!pSystemTimeToTzSpecificLocalTimeEx || !pTzSpecificLocalTimeToSystemTimeEx)
    {
        ok(FALSE, "SystemTimeToTzSpecificLocalTimeEx or TzSpecificLocalTimeToSystemTimeEx is not exported\n");
        return;
    }

    InitStaticZones();
    TestLocalToUtcStatic();
    TestUtcToLocalStatic();
    TestDynamic();
    TestCurrentZone();
    TestInvalid();
}
