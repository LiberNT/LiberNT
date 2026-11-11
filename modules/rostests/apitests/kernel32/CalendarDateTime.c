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
}
