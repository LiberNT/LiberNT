/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the CALDATETIME calendar functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

typedef struct _caldatetime
{
    CALID CalId;
    UINT Era;
    UINT Year;
    UINT Month;
    UINT Day;
    UINT DayOfWeek;
    UINT Hour;
    UINT Minute;
    UINT Second;
    ULONG Tick;
} CALDATETIME, *LPCALDATETIME;

typedef enum CALDATETIME_DATEUNIT
{
    EraUnit,
    YearUnit,
    MonthUnit,
    WeekUnit,
    DayUnit,
    HourUnit,
    MinuteUnit,
    SecondUnit,
    TickUnit
} CALDATETIME_DATEUNIT;

static BOOL (WINAPI *pAdjustCalendarDate)(LPCALDATETIME, CALDATETIME_DATEUNIT, INT);
static BOOL (WINAPI *pConvertCalDateTimeToSystemTime)(const LPCALDATETIME, SYSTEMTIME *);
static BOOL (WINAPI *pConvertSystemTimeToCalDateTime)(const SYSTEMTIME *, CALID, LPCALDATETIME);
static int (WINAPI *pGetCalendarDateFormatEx)(LPCWSTR, DWORD, const LPCALDATETIME, LPCWSTR, LPWSTR, int);
static BOOL (WINAPI *pGetCalendarSupportedDateRange)(CALID, LPCALDATETIME, LPCALDATETIME);
static BOOL (WINAPI *pIsCalendarLeapYear)(CALID, UINT, UINT);
static BOOL (WINAPI *pUpdateCalendarDayOfWeek)(LPCALDATETIME);

static CALDATETIME MakeCal(CALID CalId, UINT Era, UINT Year, UINT Month, UINT Day)
{
    CALDATETIME Cal;

    ZeroMemory(&Cal, sizeof(Cal));
    Cal.CalId = CalId;
    Cal.Era = Era;
    Cal.Year = Year;
    Cal.Month = Month;
    Cal.Day = Day;
    Cal.DayOfWeek = 0xdeadbeef;
    return Cal;
}

#define ok_cal(Cal, ECalId, EEra, EYear, EMonth, EDay, EDow) \
    ok((Cal).CalId == (ECalId) && (Cal).Era == (EEra) && (Cal).Year == (EYear) && (Cal).Month == (EMonth) && \
       (Cal).Day == (EDay) && (Cal).DayOfWeek == (EDow), \
       "Got cal %lu era %u %u-%u-%u dow %u, expected cal %u era %u %u-%u-%u dow %u\n", \
       (Cal).CalId, (Cal).Era, (Cal).Year, (Cal).Month, (Cal).Day, (Cal).DayOfWeek, \
       (UINT)(ECalId), (UINT)(EEra), (UINT)(EYear), (UINT)(EMonth), (UINT)(EDay), (UINT)(EDow))

#define ok_error(Ret, Error) \
    do { ok(!(Ret), "Call succeeded\n"); ok(GetLastError() == (Error), "Got error %lu, expected %lu\n", GetLastError(), (DWORD)(Error)); } while (0)

#define ok_out_of_range(Ret) \
    do { ok(!(Ret), "Call succeeded\n"); ok(GetLastError() != 0xdeadbeef && GetLastError() != ERROR_SUCCESS, "No error set\n"); \
         trace("Out-of-range date error %lu\n", GetLastError()); } while (0)

static void TestIsCalendarLeapYear(void)
{
    BOOL Ret;

    ok(pIsCalendarLeapYear(CAL_GREGORIAN, 2000, 1) == TRUE, "2000 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_GREGORIAN, 2004, 1) == TRUE, "2004 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_GREGORIAN, 1900, 1) == FALSE, "1900 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_GREGORIAN, 2001, 1) == FALSE, "2001 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_GREGORIAN_US, 2400, 1) == TRUE, "2400 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_TAIWAN, 89, 1) == TRUE, "Taiwan 89 (2000) is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_THAI, 2543, 1) == TRUE, "Thai 2543 (2000) is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_KOREA, 4333, 1) == TRUE, "Korea 4333 (2000) is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_JAPAN, 12, 4) == TRUE, "Heisei 12 (2000) is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_JAPAN, 2, 5) == TRUE, "Reiwa 2 (2020) is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_JAPAN, 3, 5) == FALSE, "Reiwa 3 (2021) is not a leap year\n");

    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_GREGORIAN, 2000, 0);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_GREGORIAN, 2000, 2);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(0, 2000, 1);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_GREGORIAN, 1600, 1);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
}

static void TestConvertSystemTimeToCalDateTime(void)
{
    SYSTEMTIME st = { 2008, 3, 0, 14, 13, 45, 30, 250 };
    CALDATETIME Cal;
    BOOL Ret;

    memset(&Cal, 0xcc, sizeof(Cal));
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, &Cal);
    ok(Ret, "ConvertSystemTimeToCalDateTime failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2008, 3, 14, 5);
    ok(Cal.Hour == 13 && Cal.Minute == 45 && Cal.Second == 30, "Got %u:%u:%u\n", Cal.Hour, Cal.Minute, Cal.Second);
    ok(Cal.Tick == 2500000, "Got tick %lu\n", Cal.Tick);

    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_TAIWAN, &Cal);
    ok(Ret, "CAL_TAIWAN failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_TAIWAN, 1, 97, 3, 14, 5);
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_THAI, &Cal);
    ok(Ret, "CAL_THAI failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_THAI, 1, 2551, 3, 14, 5);
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_KOREA, &Cal);
    ok(Ret, "CAL_KOREA failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_KOREA, 1, 4341, 3, 14, 5);

    st.wYear = 2019; st.wMonth = 4; st.wDay = 30;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_JAPAN, &Cal);
    ok(Ret, "CAL_JAPAN failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_JAPAN, 4, 31, 4, 30, 2);
    st.wMonth = 5; st.wDay = 1;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_JAPAN, &Cal);
    ok(Ret, "CAL_JAPAN failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_JAPAN, 5, 1, 5, 1, 3);

    st.wYear = 1601; st.wMonth = 1; st.wDay = 1; st.wMilliseconds = 0;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, &Cal);
    ok(Ret, "1601-01-01 failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 1601, 1, 1, 1);

    SetLastError(0xdeadbeef);
    st.wYear = 1600; st.wMonth = 12; st.wDay = 31;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, &Cal);
    ok_error(Ret, ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    st.wYear = 1911;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_TAIWAN, &Cal);
    ok_error(Ret, ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    st.wYear = 2008; st.wMonth = 13;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, &Cal);
    ok_error(Ret, ERROR_INVALID_PARAMETER);

    st.wMonth = 3;
    SetLastError(0xdeadbeef);
    Ret = pConvertSystemTimeToCalDateTime(&st, 0, &Cal);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pConvertSystemTimeToCalDateTime(NULL, CAL_GREGORIAN, &Cal);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, NULL);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
}

static void TestConvertCalDateTimeToSystemTime(void)
{
    CALDATETIME Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    SYSTEMTIME st, Back;
    CALDATETIME Bad;
    BOOL Ret;

    Cal.Hour = 13; Cal.Minute = 45; Cal.Second = 30; Cal.Tick = 2500000;
    memset(&st, 0xcc, sizeof(st));
    Ret = pConvertCalDateTimeToSystemTime(&Cal, &st);
    ok(Ret, "ConvertCalDateTimeToSystemTime failed, error %lu\n", GetLastError());
    ok(st.wYear == 2008 && st.wMonth == 3 && st.wDay == 14 && st.wDayOfWeek == 5 &&
       st.wHour == 13 && st.wMinute == 45 && st.wSecond == 30 && st.wMilliseconds == 250,
       "Got %u-%u-%u dow %u %u:%u:%u.%u\n", st.wYear, st.wMonth, st.wDay, st.wDayOfWeek,
       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_GREGORIAN, &Cal);
    ok(Ret, "ConvertSystemTimeToCalDateTime failed, error %lu\n", GetLastError());
    Ret = pConvertCalDateTimeToSystemTime(&Cal, &Back);
    ok(Ret && memcmp(&st, &Back, sizeof(st)) == 0, "Round trip changed the time\n");

    Cal = MakeCal(CAL_JAPAN, 5, 1, 5, 1);
    Ret = pConvertCalDateTimeToSystemTime(&Cal, &st);
    ok(Ret, "CAL_JAPAN failed, error %lu\n", GetLastError());
    ok(st.wYear == 2019 && st.wMonth == 5 && st.wDay == 1 && st.wDayOfWeek == 3,
       "Got %u-%u-%u dow %u\n", st.wYear, st.wMonth, st.wDay, st.wDayOfWeek);

    Cal = MakeCal(CAL_TAIWAN, 1, 97, 3, 14);
    Ret = pConvertCalDateTimeToSystemTime(&Cal, &st);
    ok(Ret && st.wYear == 2008, "CAL_TAIWAN got %u, error %lu\n", st.wYear, GetLastError());

    Bad = MakeCal(CAL_GREGORIAN, 1, 2008, 13, 1);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Bad, &st), ERROR_INVALID_PARAMETER);
    Bad = MakeCal(CAL_GREGORIAN, 1, 2007, 2, 29);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Bad, &st), ERROR_INVALID_PARAMETER);
    Bad = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    Bad.Hour = 24;
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Bad, &st), ERROR_INVALID_PARAMETER);
    Bad.Hour = 0;
    Bad.Tick = 10000000;
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Bad, &st), ERROR_INVALID_PARAMETER);
    Bad = MakeCal(CAL_GREGORIAN, 2, 2008, 3, 14);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Bad, &st), ERROR_INVALID_PARAMETER);
    Bad = MakeCal(CAL_GREGORIAN, 1, 1600, 12, 31);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pConvertCalDateTimeToSystemTime(&Bad, &st));
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(NULL, &st), ERROR_INVALID_PARAMETER);
}

static void TestUpdateCalendarDayOfWeek(void)
{
    CALDATETIME Cal;
    BOOL Ret;

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    Cal.Hour = 7;
    Ret = pUpdateCalendarDayOfWeek(&Cal);
    ok(Ret, "UpdateCalendarDayOfWeek failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2008, 3, 14, 5);
    ok(Cal.Hour == 7, "Hour changed to %u\n", Cal.Hour);

    Cal = MakeCal(CAL_GREGORIAN, 1, 1601, 1, 1);
    ok(pUpdateCalendarDayOfWeek(&Cal) && Cal.DayOfWeek == 1, "1601-01-01 got dow %u\n", Cal.DayOfWeek);
    Cal = MakeCal(CAL_GREGORIAN, 1, 9999, 12, 31);
    ok(pUpdateCalendarDayOfWeek(&Cal) && Cal.DayOfWeek == 5, "9999-12-31 got dow %u\n", Cal.DayOfWeek);
    Cal = MakeCal(CAL_JAPAN, 4, 31, 4, 30);
    ok(pUpdateCalendarDayOfWeek(&Cal) && Cal.DayOfWeek == 2, "Heisei 31-04-30 got dow %u\n", Cal.DayOfWeek);

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 32);
    SetLastError(0xdeadbeef);
    ok_error(pUpdateCalendarDayOfWeek(&Cal), ERROR_INVALID_PARAMETER);
    ok(Cal.DayOfWeek == 0xdeadbeef, "Failed call changed DayOfWeek to %u\n", Cal.DayOfWeek);
    Cal = MakeCal(CAL_GREGORIAN, 1, 10000, 1, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pUpdateCalendarDayOfWeek(&Cal));
    SetLastError(0xdeadbeef);
    ok_error(pUpdateCalendarDayOfWeek(NULL), ERROR_INVALID_PARAMETER);
}

static void TestAdjustCalendarDate(void)
{
    CALDATETIME Cal, Saved;
    BOOL Ret;

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 2, 28);
    Cal.Hour = 10; Cal.Minute = 20; Cal.Second = 30; Cal.Tick = 40;
    Ret = pAdjustCalendarDate(&Cal, DayUnit, 1);
    ok(Ret, "DayUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2008, 2, 29, 5);
    ok(Cal.Hour == 10 && Cal.Minute == 20 && Cal.Second == 30 && Cal.Tick == 40, "Time changed\n");

    Ret = pAdjustCalendarDate(&Cal, WeekUnit, -1);
    ok(Ret, "WeekUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2008, 2, 22, 5);

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 1, 31);
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, 1);
    ok(Ret, "MonthUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2008, 2, 29, 5);

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 15);
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, -13);
    ok(Ret, "MonthUnit -13 failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2007, 2, 15, 4);

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 2, 29);
    Ret = pAdjustCalendarDate(&Cal, YearUnit, 1);
    ok(Ret, "YearUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_GREGORIAN, 1, 2009, 2, 28, 6);

    Cal = MakeCal(CAL_JAPAN, 4, 31, 4, 30);
    Ret = pAdjustCalendarDate(&Cal, DayUnit, 1);
    ok(Ret, "CAL_JAPAN DayUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_JAPAN, 5, 1, 5, 1, 3);

    Cal = MakeCal(CAL_GREGORIAN, 1, 9999, 12, 31);
    Saved = Cal;
    SetLastError(0xdeadbeef);
    ok_out_of_range(pAdjustCalendarDate(&Cal, DayUnit, 1));
    ok(memcmp(&Cal, &Saved, sizeof(Cal)) == 0, "Failed call changed the date\n");
    Cal = MakeCal(CAL_GREGORIAN, 1, 1601, 1, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pAdjustCalendarDate(&Cal, YearUnit, -1));

    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    SetLastError(0xdeadbeef);
    ok_error(pAdjustCalendarDate(&Cal, HourUnit, 1), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_error(pAdjustCalendarDate(&Cal, EraUnit, 1), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 13, 14);
    SetLastError(0xdeadbeef);
    ok_error(pAdjustCalendarDate(&Cal, DayUnit, 1), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_error(pAdjustCalendarDate(NULL, DayUnit, 1), ERROR_INVALID_PARAMETER);
}

static void TestGetCalendarSupportedDateRange(void)
{
    CALDATETIME Min, Max;
    BOOL Ret;

    memset(&Min, 0xcc, sizeof(Min));
    memset(&Max, 0xcc, sizeof(Max));
    Ret = pGetCalendarSupportedDateRange(CAL_GREGORIAN, &Min, &Max);
    ok(Ret, "GetCalendarSupportedDateRange failed, error %lu\n", GetLastError());
    ok_cal(Min, CAL_GREGORIAN, 1, 1601, 1, 1, 1);
    ok(Min.Hour == 0 && Min.Minute == 0 && Min.Second == 0 && Min.Tick == 0, "Min time %u:%u:%u.%lu\n",
       Min.Hour, Min.Minute, Min.Second, Min.Tick);
    ok_cal(Max, CAL_GREGORIAN, 1, 9999, 12, 31, 5);
    ok(Max.Hour == 23 && Max.Minute == 59 && Max.Second == 59 && Max.Tick == 9999999, "Max time %u:%u:%u.%lu\n",
       Max.Hour, Max.Minute, Max.Second, Max.Tick);

    Ret = pGetCalendarSupportedDateRange(CAL_TAIWAN, &Min, &Max);
    ok(Ret, "CAL_TAIWAN failed, error %lu\n", GetLastError());
    ok_cal(Min, CAL_TAIWAN, 1, 1, 1, 1, 1);

    SetLastError(0xdeadbeef);
    ok_error(pGetCalendarSupportedDateRange(0, &Min, &Max), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_error(pGetCalendarSupportedDateRange(CAL_GREGORIAN, NULL, &Max), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_error(pGetCalendarSupportedDateRange(CAL_GREGORIAN, &Min, NULL), ERROR_INVALID_PARAMETER);
}

static void TestGetCalendarDateFormatEx(void)
{
    static const WCHAR Format[] = L"dddd, MMMM dd, yyyy";
    CALDATETIME Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    SYSTEMTIME st = { 2008, 3, 5, 14, 0, 0, 0, 0 };
    WCHAR Buffer[80], Expected[80];
    int Ret, ExpectedRet;

    memset(Buffer, 0x55, sizeof(Buffer));
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, Buffer, ARRAY_SIZE(Buffer));
    ok(Ret == (int)wcslen(L"Friday, March 14, 2008") + 1, "Got %d, error %lu\n", Ret, GetLastError());
    ok(wcscmp(Buffer, L"Friday, March 14, 2008") == 0, "Got %S\n", Buffer);

    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, NULL, 0);
    ok(Ret == (int)wcslen(L"Friday, March 14, 2008") + 1, "Size query got %d\n", Ret);

    ExpectedRet = GetDateFormatEx(L"en-US", DATE_LONGDATE, &st, NULL, Expected, ARRAY_SIZE(Expected), NULL);
    ok(ExpectedRet > 0, "GetDateFormatEx failed, error %lu\n", GetLastError());
    memset(Buffer, 0x55, sizeof(Buffer));
    Ret = pGetCalendarDateFormatEx(L"en-US", DATE_LONGDATE, &Cal, NULL, Buffer, ARRAY_SIZE(Buffer));
    ok(Ret == ExpectedRet && wcscmp(Buffer, Expected) == 0, "Got %d %S, expected %d %S\n", Ret, Buffer, ExpectedRet, Expected);

    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, Buffer, 5);
    ok_error(Ret, ERROR_INSUFFICIENT_BUFFER);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, NULL, 10);
    ok_error(Ret, ERROR_INSUFFICIENT_BUFFER);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", DATE_LONGDATE, &Cal, Format, Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_FLAGS);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", DATE_SHORTDATE | DATE_LONGDATE, &Cal, NULL, Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_FLAGS);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", DATE_LTRREADING | DATE_RTLREADING, &Cal, NULL, Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_FLAGS);

    Cal.Day = 32;
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_GREGORIAN, 1, 1600, 12, 31);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, Format, Buffer, ARRAY_SIZE(Buffer));
    ok_out_of_range(Ret);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, NULL, Format, Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_PARAMETER);
}

static void TestOwnCalendarLeapYears(void)
{
    BOOL Ret;

    ok(pIsCalendarLeapYear(CAL_HIJRI, 1445, 1) == TRUE, "Hijri 1445 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_HIJRI, 1446, 1) == FALSE, "Hijri 1446 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_HEBREW, 5784, 1) == TRUE, "Hebrew 5784 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_HEBREW, 5785, 1) == FALSE, "Hebrew 5785 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_PERSIAN, 1403, 1) == TRUE, "Persian 1403 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_PERSIAN, 1402, 1) == FALSE, "Persian 1402 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_PERSIAN, 1404, 1) == FALSE, "Persian 1404 is not a leap year\n");
    ok(pIsCalendarLeapYear(CAL_UMALQURA, 1447, 1) == TRUE, "UmAlQura 1447 is a leap year\n");
    ok(pIsCalendarLeapYear(CAL_UMALQURA, 1446, 1) == FALSE, "UmAlQura 1446 is not a leap year\n");

    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_HIJRI, 1445, 2);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_HEBREW, 5784, 0);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_HEBREW, 6000, 1);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_UMALQURA, 1501, 1);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    Ret = pIsCalendarLeapYear(CAL_UMALQURA, 1317, 1);
    ok_error(Ret, ERROR_INVALID_PARAMETER);
}

static void TestOwnCalendarConversions(void)
{
    static const struct
    {
        WORD Year, Month, Day;
        CALID CalId;
        UINT CalYear, CalMonth, CalDay, DayOfWeek;
    } Tests[] =
    {
        { 2001,  1,  1, CAL_HIJRI,    1421, 10,  6, 1 },
        { 9999, 12, 31, CAL_HIJRI,    9666,  4,  3, 5 },
        { 2001,  1,  1, CAL_HEBREW,   5761,  4,  6, 1 },
        { 2024, 10,  3, CAL_HEBREW,   5785,  1,  1, 4 },
        { 2024,  4, 23, CAL_HEBREW,   5784,  8, 15, 2 },
        { 2024,  3, 11, CAL_HEBREW,   5784,  7,  1, 1 },
        { 2025,  3, 14, CAL_HEBREW,   5785,  6, 14, 5 },
        { 2239,  9, 29, CAL_HEBREW,   5999, 13, 29, 0 },
        { 2002,  3, 21, CAL_PERSIAN,  1381,  1,  1, 4 },
        { 2024,  3, 20, CAL_PERSIAN,  1403,  1,  1, 3 },
        { 2025,  3, 20, CAL_PERSIAN,  1403, 12, 30, 4 },
        { 2025,  3, 21, CAL_PERSIAN,  1404,  1,  1, 5 },
        { 9999, 12, 31, CAL_PERSIAN,  9378, 10, 13, 5 },
        { 1900,  4, 30, CAL_UMALQURA, 1318,  1,  1, 1 },
        { 2024,  3, 11, CAL_UMALQURA, 1445,  9,  1, 1 },
        { 2024,  4, 10, CAL_UMALQURA, 1445, 10,  1, 3 },
        { 2024,  7,  7, CAL_UMALQURA, 1446,  1,  1, 0 },
        { 2077, 11, 16, CAL_UMALQURA, 1500, 12, 30, 2 },
    };
    static const struct
    {
        WORD Year, Month, Day;
        CALID CalId;
    } OutOfRange[] =
    {
        { 2239,  9, 30, CAL_HEBREW },
        { 1900,  4, 29, CAL_UMALQURA },
        { 2077, 11, 17, CAL_UMALQURA },
    };
    SYSTEMTIME st, Back;
    CALDATETIME Cal;
    ULONG i;
    BOOL Ret;

    for (i = 0; i < ARRAY_SIZE(Tests); i++)
    {
        ZeroMemory(&st, sizeof(st));
        st.wYear = Tests[i].Year;
        st.wMonth = Tests[i].Month;
        st.wDay = Tests[i].Day;
        st.wHour = 6;
        memset(&Cal, 0xcc, sizeof(Cal));
        Ret = pConvertSystemTimeToCalDateTime(&st, Tests[i].CalId, &Cal);
        ok(Ret, "%lu: ConvertSystemTimeToCalDateTime failed, error %lu\n", i, GetLastError());
        ok_cal(Cal, Tests[i].CalId, 1, Tests[i].CalYear, Tests[i].CalMonth, Tests[i].CalDay, Tests[i].DayOfWeek);
        ok(Cal.Hour == 6 && Cal.Minute == 0 && Cal.Second == 0 && Cal.Tick == 0, "%lu: Got time %u:%u:%u.%lu\n",
           i, Cal.Hour, Cal.Minute, Cal.Second, Cal.Tick);

        Cal = MakeCal(Tests[i].CalId, 1, Tests[i].CalYear, Tests[i].CalMonth, Tests[i].CalDay);
        memset(&Back, 0xcc, sizeof(Back));
        Ret = pConvertCalDateTimeToSystemTime(&Cal, &Back);
        ok(Ret, "%lu: ConvertCalDateTimeToSystemTime failed, error %lu\n", i, GetLastError());
        ok(Back.wYear == Tests[i].Year && Back.wMonth == Tests[i].Month && Back.wDay == Tests[i].Day &&
           Back.wDayOfWeek == Tests[i].DayOfWeek && Back.wHour == 0,
           "%lu: Got %u-%u-%u dow %u hour %u\n", i, Back.wYear, Back.wMonth, Back.wDay, Back.wDayOfWeek, Back.wHour);

        Ret = pUpdateCalendarDayOfWeek(&Cal);
        ok(Ret && Cal.DayOfWeek == Tests[i].DayOfWeek, "%lu: UpdateCalendarDayOfWeek got %d dow %u\n", i, Ret, Cal.DayOfWeek);
    }

    for (i = 0; i < ARRAY_SIZE(OutOfRange); i++)
    {
        ZeroMemory(&st, sizeof(st));
        st.wYear = OutOfRange[i].Year;
        st.wMonth = OutOfRange[i].Month;
        st.wDay = OutOfRange[i].Day;
        SetLastError(0xdeadbeef);
        Ret = pConvertSystemTimeToCalDateTime(&st, OutOfRange[i].CalId, &Cal);
        ok_error(Ret, ERROR_INVALID_PARAMETER);
    }

    Cal = MakeCal(CAL_HEBREW, 1, 5785, 13, 1);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_HEBREW, 1, 5784, 13, 29);
    ok(pConvertCalDateTimeToSystemTime(&Cal, &st) && st.wYear == 2024 && st.wMonth == 10 && st.wDay == 2,
       "Hebrew 5784-13-29 got %u-%u-%u\n", st.wYear, st.wMonth, st.wDay);
    Cal = MakeCal(CAL_HIJRI, 1, 1445, 12, 30);
    ok(pConvertCalDateTimeToSystemTime(&Cal, &st) && st.wYear == 2024 && st.wMonth == 7 && st.wDay == 6,
       "Hijri 1445-12-30 got %u-%u-%u\n", st.wYear, st.wMonth, st.wDay);
    Cal = MakeCal(CAL_HIJRI, 1, 1446, 12, 30);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_PERSIAN, 1, 1402, 12, 30);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_PERSIAN, 1, 1403, 7, 31);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_UMALQURA, 1, 1445, 9, 30);
    ok(pConvertCalDateTimeToSystemTime(&Cal, &st) && st.wYear == 2024 && st.wMonth == 4 && st.wDay == 9,
       "UmAlQura 1445-09-30 got %u-%u-%u\n", st.wYear, st.wMonth, st.wDay);
    Cal = MakeCal(CAL_UMALQURA, 1, 1445, 10, 30);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_HIJRI, 2, 1445, 1, 1);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_PERSIAN, 0, 1403, 1, 1);
    SetLastError(0xdeadbeef);
    ok_error(pConvertCalDateTimeToSystemTime(&Cal, &st), ERROR_INVALID_PARAMETER);

    Cal = MakeCal(CAL_HEBREW, 1, 6000, 1, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pConvertCalDateTimeToSystemTime(&Cal, &st));
    Cal = MakeCal(CAL_HEBREW, 1, 5361, 4, 26);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pConvertCalDateTimeToSystemTime(&Cal, &st));
    Cal = MakeCal(CAL_UMALQURA, 1, 1501, 1, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pConvertCalDateTimeToSystemTime(&Cal, &st));
    Cal = MakeCal(CAL_UMALQURA, 1, 1317, 12, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pConvertCalDateTimeToSystemTime(&Cal, &st));
}

static void TestOwnCalendarAdjust(void)
{
    CALDATETIME Cal, Saved;
    BOOL Ret;

    Cal = MakeCal(CAL_HEBREW, 1, 5784, 5, 30);
    Cal.Hour = 9;
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, 1);
    ok(Ret, "Hebrew MonthUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HEBREW, 1, 5784, 6, 30, 0);
    ok(Cal.Hour == 9, "Hour changed to %u\n", Cal.Hour);
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, 1);
    ok(Ret, "Hebrew MonthUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HEBREW, 1, 5784, 7, 29, 1);

    Cal = MakeCal(CAL_HEBREW, 1, 5784, 13, 29);
    Ret = pAdjustCalendarDate(&Cal, YearUnit, 1);
    ok(Ret, "Hebrew YearUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HEBREW, 1, 5785, 12, 29, 1);

    Cal = MakeCal(CAL_HEBREW, 1, 5784, 12, 1);
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, 2);
    ok(Ret, "Hebrew MonthUnit 2 failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HEBREW, 1, 5785, 1, 1, 4);

    Cal = MakeCal(CAL_HIJRI, 1, 1445, 12, 30);
    Ret = pAdjustCalendarDate(&Cal, YearUnit, 1);
    ok(Ret, "Hijri YearUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HIJRI, 1, 1446, 12, 29, 3);

    Cal = MakeCal(CAL_HIJRI, 1, 1445, 11, 15);
    Ret = pAdjustCalendarDate(&Cal, MonthUnit, 2);
    ok(Ret, "Hijri MonthUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_HIJRI, 1, 1446, 1, 15, 0);

    Cal = MakeCal(CAL_PERSIAN, 1, 1403, 12, 30);
    Ret = pAdjustCalendarDate(&Cal, YearUnit, 1);
    ok(Ret, "Persian YearUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_PERSIAN, 1, 1404, 12, 29, 5);

    Cal = MakeCal(CAL_UMALQURA, 1, 1445, 9, 1);
    Ret = pAdjustCalendarDate(&Cal, DayUnit, 30);
    ok(Ret, "UmAlQura DayUnit failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_UMALQURA, 1, 1445, 10, 1, 3);

    Cal = MakeCal(CAL_UMALQURA, 1, 1500, 12, 30);
    Saved = Cal;
    SetLastError(0xdeadbeef);
    ok_out_of_range(pAdjustCalendarDate(&Cal, DayUnit, 1));
    ok(memcmp(&Cal, &Saved, sizeof(Cal)) == 0, "Failed call changed the date\n");
    Cal = MakeCal(CAL_HEBREW, 1, 5999, 13, 29);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pAdjustCalendarDate(&Cal, WeekUnit, 1));
    Cal = MakeCal(CAL_UMALQURA, 1, 1318, 1, 1);
    SetLastError(0xdeadbeef);
    ok_out_of_range(pAdjustCalendarDate(&Cal, MonthUnit, -1));
    Cal = MakeCal(CAL_PERSIAN, 1, 1403, 1, 1);
    SetLastError(0xdeadbeef);
    ok_error(pAdjustCalendarDate(&Cal, EraUnit, 1), ERROR_INVALID_PARAMETER);
}

static void TestOwnCalendarRanges(void)
{
    static const struct
    {
        CALID CalId;
        UINT MinYear, MinMonth, MinDay, MinDow;
        UINT MaxYear, MaxMonth, MaxDay, MaxDow;
    } Tests[] =
    {
        { CAL_HIJRI,    1009,  6, 26, 1, 9666,  4,  3, 5 },
        { CAL_HEBREW,   5361,  4, 27, 1, 5999, 13, 29, 0 },
        { CAL_PERSIAN,   979, 10, 11, 1, 9378, 10, 13, 5 },
        { CAL_UMALQURA, 1318,  1,  1, 1, 1500, 12, 30, 2 },
    };
    CALDATETIME Min, Max;
    ULONG i;
    BOOL Ret;

    for (i = 0; i < ARRAY_SIZE(Tests); i++)
    {
        memset(&Min, 0xcc, sizeof(Min));
        memset(&Max, 0xcc, sizeof(Max));
        Ret = pGetCalendarSupportedDateRange(Tests[i].CalId, &Min, &Max);
        ok(Ret, "%lu: GetCalendarSupportedDateRange failed, error %lu\n", i, GetLastError());
        ok_cal(Min, Tests[i].CalId, 1, Tests[i].MinYear, Tests[i].MinMonth, Tests[i].MinDay, Tests[i].MinDow);
        ok_cal(Max, Tests[i].CalId, 1, Tests[i].MaxYear, Tests[i].MaxMonth, Tests[i].MaxDay, Tests[i].MaxDow);
        ok(Min.Hour == 0 && Min.Minute == 0 && Min.Second == 0 && Min.Tick == 0, "%lu: Min time %u:%u:%u.%lu\n",
           i, Min.Hour, Min.Minute, Min.Second, Min.Tick);
        ok(Max.Hour == 23 && Max.Minute == 59 && Max.Second == 59 && Max.Tick == 9999999, "%lu: Max time %u:%u:%u.%lu\n",
           i, Max.Hour, Max.Minute, Max.Second, Max.Tick);
    }
}

static void SetHijriAdjustment(HKEY Key, LPCWSTR Value)
{
    LONG Error = RegSetValueExW(Key, L"AddHijriDate", 0, REG_SZ, (const BYTE *)Value, (DWORD)((wcslen(Value) + 1) * sizeof(WCHAR)));
    ok(Error == ERROR_SUCCESS, "RegSetValueExW(%S) failed, error %ld\n", Value, Error);
}

static void TestHijriAdjustment(void)
{
    static const struct
    {
        LPCWSTR Value;
        UINT Day;
    } Tests[] =
    {
        { L"AddHijriDate+1", 7 },
        { L"AddHijriDate+2", 8 },
        { L"AddHijriDate", 5 },
        { L"AddHijriDate-1", 5 },
        { L"addhijridate-2", 4 },
        { L"AddHijriDate+3", 6 },
        { L"AddHijriDate-3", 6 },
        { L"AddHijriDatex", 6 },
        { L"", 6 },
        { L"+1", 6 },
    };
    SYSTEMTIME st = { 2001, 1, 1, 1, 0, 0, 0, 0 };
    WCHAR Saved[64];
    DWORD SavedSize = sizeof(Saved), Type = REG_NONE;
    CALDATETIME Cal;
    BOOL HadValue;
    HKEY Key;
    LONG Error;
    ULONG i;
    BOOL Ret;

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW failed, error %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    HadValue = (RegQueryValueExW(Key, L"AddHijriDate", NULL, &Type, (BYTE *)Saved, &SavedSize) == ERROR_SUCCESS);

    for (i = 0; i < ARRAY_SIZE(Tests); i++)
    {
        SetHijriAdjustment(Key, Tests[i].Value);
        Ret = pConvertSystemTimeToCalDateTime(&st, CAL_HIJRI, &Cal);
        ok(Ret, "%S: ConvertSystemTimeToCalDateTime failed, error %lu\n", Tests[i].Value, GetLastError());
        ok_cal(Cal, CAL_HIJRI, 1, 1421, 10, Tests[i].Day, 1);
    }

    SetHijriAdjustment(Key, L"AddHijriDate+1");
    Cal = MakeCal(CAL_HIJRI, 1, 1421, 10, 7);
    Ret = pConvertCalDateTimeToSystemTime(&Cal, &st);
    ok(Ret && st.wYear == 2001 && st.wMonth == 1 && st.wDay == 1, "Adjusted Hijri got %u-%u-%u\n", st.wYear, st.wMonth, st.wDay);
    st.wYear = 2024; st.wMonth = 3; st.wDay = 11;
    Ret = pConvertSystemTimeToCalDateTime(&st, CAL_UMALQURA, &Cal);
    ok(Ret, "CAL_UMALQURA failed, error %lu\n", GetLastError());
    ok_cal(Cal, CAL_UMALQURA, 1, 1445, 9, 1, 1);

    if (HadValue)
        RegSetValueExW(Key, L"AddHijriDate", 0, Type, (const BYTE *)Saved, SavedSize);
    else
        RegDeleteValueW(Key, L"AddHijriDate");
    RegCloseKey(Key);
}

static int (WINAPI *pGetCalendarInfoEx)(LPCWSTR, CALID, LPCWSTR, CALTYPE, LPWSTR, int, LPDWORD);

static void GetCalString(LPCWSTR Locale, CALID CalId, CALTYPE Type, LPWSTR Buffer, int Size)
{
    Buffer[0] = UNICODE_NULL;
    ok(pGetCalendarInfoEx(Locale, CalId, NULL, Type, Buffer, Size, NULL) > 0,
       "GetCalendarInfoEx(%S, %lu, 0x%lx) failed, error %lu\n", Locale, CalId, Type, GetLastError());
}

static void ok_format(LPCWSTR Locale, DWORD Flags, CALDATETIME *Cal, LPCWSTR Format, LPCWSTR Expected)
{
    WCHAR Buffer[128];
    int Ret;

    memset(Buffer, 0x55, sizeof(Buffer));
    Ret = pGetCalendarDateFormatEx(Locale, Flags, Cal, Format, Buffer, ARRAY_SIZE(Buffer));
    ok(Ret == (int)wcslen(Expected) + 1 && wcscmp(Buffer, Expected) == 0,
       "%S %lu %S: got %d %S, expected %S\n", Locale, Cal->CalId, Format ? Format : L"(null)", Ret,
       Ret ? Buffer : L"", Expected);
}

static void TestOtherCalendarFormat(void)
{
    WCHAR Name[80], Name2[80], Expected[160], Picture[80], Buffer[160];
    CALDATETIME Cal;
    int Ret;

    if (!pGetCalendarInfoEx)
    {
        skip("GetCalendarInfoEx is not exported\n");
        return;
    }

    Cal = MakeCal(CAL_HIJRI, 1, 1421, 10, 6);
    ok_format(L"ar-SA", 0, &Cal, L"yyyy/MM/dd", L"1421/10/06");
    Cal = MakeCal(CAL_UMALQURA, 1, 1445, 9, 1);
    ok_format(L"ar-SA", 0, &Cal, L"d/M/yy y", L"1/9/45 45");
    GetCalString(L"ar-SA", CAL_UMALQURA, CAL_SMONTHNAME9, Name, ARRAY_SIZE(Name));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"%s %s", Name, L"1445");
    ok_format(L"ar-SA", 0, &Cal, L"MMMM yyyy", Expected);

    Cal = MakeCal(CAL_HEBREW, 1, 5784, 8, 15);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SMONTHNAME8, Name, ARRAY_SIZE(Name));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"15 %s 5784 yyyy'", Name);
    ok_format(L"he-IL", 0, &Cal, L"d MMMM yyyy 'yyyy'''", Expected);
    Cal = MakeCal(CAL_HEBREW, 1, 5784, 6, 1);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SMONTHNAME6, Name, ARRAY_SIZE(Name));
    ok_format(L"he-IL", 0, &Cal, L"MMMM", Name);
    Cal = MakeCal(CAL_HEBREW, 1, 5784, 7, 1);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SMONTHNAME7, Name, ARRAY_SIZE(Name));
    ok_format(L"he-IL", 0, &Cal, L"MMMM", Name);
    Cal = MakeCal(CAL_HEBREW, 1, 5785, 6, 14);
    ok_format(L"he-IL", 0, &Cal, L"MMMM", Name);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SABBREVMONTHNAME7, Name, ARRAY_SIZE(Name));
    ok_format(L"he-IL", 0, &Cal, L"MMM", Name);
    Cal = MakeCal(CAL_HEBREW, 1, 5785, 7, 1);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SMONTHNAME8, Name, ARRAY_SIZE(Name));
    ok_format(L"he-IL", 0, &Cal, L"MMMM", Name);

    Cal = MakeCal(CAL_HEBREW, 1, 5785, 1, 1);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SDAYNAME4, Name, ARRAY_SIZE(Name));
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SABBREVDAYNAME4, Name2, ARRAY_SIZE(Name2));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"%s|%s|01|1", Name, Name2);
    ok_format(L"he-IL", 0, &Cal, L"dddd|ddd|dd|M", Expected);
    GetCalString(L"he-IL", CAL_HEBREW, CAL_SERASTRING, Name, ARRAY_SIZE(Name));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"%s 85", Name);
    ok_format(L"he-IL", 0, &Cal, L"gg yy", Expected);

    GetCalString(L"he-IL", CAL_HEBREW, CAL_SLONGDATE, Picture, ARRAY_SIZE(Picture));
    Ret = pGetCalendarDateFormatEx(L"he-IL", 0, &Cal, Picture, Expected, ARRAY_SIZE(Expected));
    ok(Ret > 0, "Explicit long date failed, error %lu\n", GetLastError());
    ok_format(L"he-IL", DATE_LONGDATE, &Cal, NULL, Expected);
    Ret = pGetCalendarDateFormatEx(L"he-IL", DATE_LONGDATE, &Cal, NULL, NULL, 0);
    ok(Ret == (int)wcslen(Expected) + 1, "Size query got %d, expected %u\n", Ret, (UINT)wcslen(Expected) + 1);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"he-IL", DATE_LONGDATE, &Cal, NULL, Buffer, (int)wcslen(Expected));
    ok_error(Ret, ERROR_INSUFFICIENT_BUFFER);

    Cal = MakeCal(CAL_PERSIAN, 1, 1403, 1, 1);
    ok_format(L"fa-IR", 0, &Cal, L"yyyy/MM/dd", L"1403/01/01");

    Cal = MakeCal(CAL_THAI, 1, 2551, 3, 14);
    ok_format(L"th-TH", 0, &Cal, L"d/M/yyyy", L"14/3/2551");
    Ret = GetLocaleInfoEx(L"th-TH", LOCALE_SSHORTDATE, Picture, ARRAY_SIZE(Picture));
    ok(Ret > 0, "GetLocaleInfoEx failed, error %lu\n", GetLastError());
    Ret = pGetCalendarDateFormatEx(L"th-TH", 0, &Cal, Picture, Expected, ARRAY_SIZE(Expected));
    ok(Ret > 0, "Explicit short date failed, error %lu\n", GetLastError());
    ok_format(L"th-TH", DATE_SHORTDATE, &Cal, NULL, Expected);
    Cal = MakeCal(CAL_GREGORIAN, 1, 2008, 3, 14);
    GetCalString(L"th-TH", CAL_GREGORIAN, CAL_SMONTHNAME3, Name, ARRAY_SIZE(Name));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"%s 2008", Name);
    ok_format(L"th-TH", 0, &Cal, L"MMMM yyyy", Expected);

    Cal = MakeCal(CAL_JAPAN, 5, 2, 1, 1);
    GetCalString(L"ja-JP", CAL_JAPAN, CAL_SERASTRING, Name, ARRAY_SIZE(Name));
    StringCchPrintfW(Expected, ARRAY_SIZE(Expected), L"%s 02 2 02", Name);
    ok_format(L"ja-JP", 0, &Cal, L"gg yy y yyyy", Expected);

    Cal = MakeCal(CAL_HIJRI, 1, 1421, 10, 6);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"en-US", 0, &Cal, L"yyyy", Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_TAIWAN, 1, 97, 3, 14);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"ja-JP", 0, &Cal, L"yyyy", Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_PARAMETER);
    Cal = MakeCal(CAL_HEBREW, 1, 5785, 13, 1);
    SetLastError(0xdeadbeef);
    Ret = pGetCalendarDateFormatEx(L"he-IL", 0, &Cal, L"yyyy", Buffer, ARRAY_SIZE(Buffer));
    ok_error(Ret, ERROR_INVALID_PARAMETER);
}

START_TEST(CalendarDateTime)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pAdjustCalendarDate = (PVOID)GetProcAddress(Kernel32, "AdjustCalendarDate");
    pConvertCalDateTimeToSystemTime = (PVOID)GetProcAddress(Kernel32, "ConvertCalDateTimeToSystemTime");
    pConvertSystemTimeToCalDateTime = (PVOID)GetProcAddress(Kernel32, "ConvertSystemTimeToCalDateTime");
    pGetCalendarDateFormatEx = (PVOID)GetProcAddress(Kernel32, "GetCalendarDateFormatEx");
    pGetCalendarSupportedDateRange = (PVOID)GetProcAddress(Kernel32, "GetCalendarSupportedDateRange");
    pIsCalendarLeapYear = (PVOID)GetProcAddress(Kernel32, "IsCalendarLeapYear");
    pUpdateCalendarDayOfWeek = (PVOID)GetProcAddress(Kernel32, "UpdateCalendarDayOfWeek");
    pGetCalendarInfoEx = (PVOID)GetProcAddress(Kernel32, "GetCalendarInfoEx");
    if (!pAdjustCalendarDate || !pConvertCalDateTimeToSystemTime || !pConvertSystemTimeToCalDateTime ||
        !pGetCalendarDateFormatEx || !pGetCalendarSupportedDateRange || !pIsCalendarLeapYear ||
        !pUpdateCalendarDayOfWeek)
    {
        ok(FALSE, "A CALDATETIME function is not exported\n");
        return;
    }

    TestIsCalendarLeapYear();
    TestConvertSystemTimeToCalDateTime();
    TestConvertCalDateTimeToSystemTime();
    TestUpdateCalendarDayOfWeek();
    TestAdjustCalendarDate();
    TestGetCalendarSupportedDateRange();
    TestGetCalendarDateFormatEx();
    TestOwnCalendarLeapYears();
    TestOwnCalendarConversions();
    TestOwnCalendarAdjust();
    TestOwnCalendarRanges();
    TestHijriAdjustment();
    TestOtherCalendarFormat();
}
