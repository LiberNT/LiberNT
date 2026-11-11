/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calendar date functions working on CALDATETIME
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <k32.h>
#include "japanese.h"

#define NDEBUG
#include <debug.h>

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

#define CAL_FIRST_YEAR      1601
#define CAL_LAST_YEAR       9999
#define CAL_TICKS_PER_MS    10000
#define CAL_TICKS_PER_SEC   10000000

/* FIXME: the documented ERROR_DATE_OUT_OF_RANGE has no published value; out-of-range dates report ERROR_INVALID_PARAMETER */
#define CAL_ERROR_DATE_OUT_OF_RANGE ERROR_INVALID_PARAMETER

typedef struct _CAL_ERA_RANGE
{
    LONG FirstDay;
    ULONG FirstYear;
} CAL_ERA_RANGE;

static BOOL CAL_IsLeapYear(ULONG Year)
{
    return (Year % 4 == 0) && (Year % 100 != 0 || Year % 400 == 0);
}

static ULONG CAL_DaysInMonth(ULONG Year, ULONG Month)
{
    static const UCHAR Days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (Month == 2 && CAL_IsLeapYear(Year))
        return 29;
    return Days[Month - 1];
}

static LONG CAL_DaysFromDate(ULONG Year, ULONG Month, ULONG Day)
{
    ULONG Years = Year - CAL_FIRST_YEAR;
    ULONG i;
    LONG Days;

    Days = (LONG)(Years * 365 + Years / 4 - Years / 100 + Years / 400);
    for (i = 1; i < Month; i++)
        Days += (LONG)CAL_DaysInMonth(Year, i);
    return Days + (LONG)Day - 1;
}

static VOID CAL_DateFromDays(LONG Days, PULONG Year, PULONG Month, PULONG Day)
{
    ULONG Remaining = (ULONG)Days;
    ULONG Cycles400, Cycles100, Cycles4, Years;

    Cycles400 = Remaining / 146097;
    Remaining %= 146097;
    Cycles100 = min(Remaining / 36524, 3);
    Remaining -= Cycles100 * 36524;
    Cycles4 = Remaining / 1461;
    Remaining %= 1461;
    Years = min(Remaining / 365, 3);
    Remaining -= Years * 365;

    *Year = CAL_FIRST_YEAR + Cycles400 * 400 + Cycles100 * 100 + Cycles4 * 4 + Years;
    for (*Month = 1; Remaining >= CAL_DaysInMonth(*Year, *Month); (*Month)++)
        Remaining -= CAL_DaysInMonth(*Year, *Month);
    *Day = Remaining + 1;
}

static UINT CAL_DayOfWeek(LONG Days)
{
    return (UINT)((Days + 1) % 7);
}

static BOOL CAL_IsGregorianFamily(CALID CalId)
{
    switch (CalId)
    {
    case CAL_GREGORIAN:
    case CAL_GREGORIAN_US:
    case CAL_GREGORIAN_ME_FRENCH:
    case CAL_GREGORIAN_ARABIC:
    case CAL_GREGORIAN_XLIT_ENGLISH:
    case CAL_GREGORIAN_XLIT_FRENCH:
        return TRUE;
    default:
        return FALSE;
    }
}

static ULONG CAL_GetEras(CALID CalId, CAL_ERA_RANGE *Eras, ULONG MaxEras)
{
    PCJAPANESE_ERA Table;
    DWORD Count = 0, i;

    if (CAL_IsGregorianFamily(CalId) || CalId == CAL_KOREA || CalId == CAL_THAI)
    {
        Eras[0].FirstDay = 0;
        Eras[0].FirstYear = CAL_FIRST_YEAR;
        return 1;
    }

    if (CalId == CAL_TAIWAN)
    {
        Eras[0].FirstDay = CAL_DaysFromDate(1912, 1, 1);
        Eras[0].FirstYear = 1912;
        return 1;
    }

    if (CalId == CAL_JAPAN)
    {
        Table = JapaneseEra_GetTable(&Count);
        for (i = 0; i < Count && i < MaxEras; i++)
        {
            if (Table[i].wYear < CAL_FIRST_YEAR || Table[i].wYear > CAL_LAST_YEAR ||
                Table[i].wMonth < 1 || Table[i].wMonth > 12 ||
                Table[i].wDay < 1 || Table[i].wDay > CAL_DaysInMonth(Table[i].wYear, Table[i].wMonth))
            {
                return 0;
            }
            Eras[i].FirstDay = CAL_DaysFromDate(Table[i].wYear, Table[i].wMonth, Table[i].wDay);
            Eras[i].FirstYear = Table[i].wYear;
        }
        return i;
    }

    /* TODO: Hijri, Hebrew, Persian and UmAlQura calendars are not supported; they fail with ERROR_INVALID_PARAMETER */
    return 0;
}

static LONG CAL_YearOffset(CALID CalId)
{
    switch (CalId)
    {
    case CAL_KOREA:
        return 2333;
    case CAL_THAI:
        return 543;
    default:
        return 0;
    }
}

static BOOL CAL_GregorianYearFromCalendar(CALID CalId, UINT Era, UINT Year, PULONG GregorianYear)
{
    CAL_ERA_RANGE Eras[16];
    ULONG Count, LastYear;

    Count = CAL_GetEras(CalId, Eras, ARRAYSIZE(Eras));
    if (!Count || Era < 1 || Era > Count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    LastYear = (Era < Count) ? Eras[Era].FirstYear : CAL_LAST_YEAR;
    if (CalId == CAL_KOREA || CalId == CAL_THAI)
    {
        if (Year <= (ULONG)CAL_YearOffset(CalId) ||
            Year - (ULONG)CAL_YearOffset(CalId) < CAL_FIRST_YEAR ||
            Year - (ULONG)CAL_YearOffset(CalId) > CAL_LAST_YEAR)
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        *GregorianYear = Year - (ULONG)CAL_YearOffset(CalId);
        return TRUE;
    }

    if (CAL_IsGregorianFamily(CalId))
    {
        if (Year < CAL_FIRST_YEAR || Year > CAL_LAST_YEAR)
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        *GregorianYear = Year;
        return TRUE;
    }

    if (Year < 1 || Year > LastYear - Eras[Era - 1].FirstYear + 1)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }
    *GregorianYear = Eras[Era - 1].FirstYear + Year - 1;
    return TRUE;
}

static BOOL CAL_ToDays(const CALDATETIME *CalDateTime, PLONG Days)
{
    CAL_ERA_RANGE Eras[16];
    ULONG GregorianYear;
    LONG Result;

    if (!CalDateTime)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!CAL_GregorianYearFromCalendar(CalDateTime->CalId, CalDateTime->Era, CalDateTime->Year, &GregorianYear))
        return FALSE;

    if (CalDateTime->Month < 1 || CalDateTime->Month > 12 ||
        CalDateTime->Day < 1 || CalDateTime->Day > CAL_DaysInMonth(GregorianYear, CalDateTime->Month) ||
        CalDateTime->Hour > 23 || CalDateTime->Minute > 59 || CalDateTime->Second > 59 ||
        CalDateTime->Tick >= CAL_TICKS_PER_SEC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Result = CAL_DaysFromDate(GregorianYear, CalDateTime->Month, CalDateTime->Day);
    if (CAL_GetEras(CalDateTime->CalId, Eras, ARRAYSIZE(Eras)) && Result < Eras[0].FirstDay)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    *Days = Result;
    return TRUE;
}

static BOOL CAL_FromDays(CALID CalId, LONG Days, LPCALDATETIME CalDateTime)
{
    CAL_ERA_RANGE Eras[16];
    ULONG Count, Era, Year, Month, Day;

    Count = CAL_GetEras(CalId, Eras, ARRAYSIZE(Eras));
    if (!Count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Days < Eras[0].FirstDay || Days > CAL_DaysFromDate(CAL_LAST_YEAR, 12, 31))
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    Era = Count;
    while (Era > 1 && Days < Eras[Era - 1].FirstDay)
        Era--;
    CAL_DateFromDays(Days, &Year, &Month, &Day);

    CalDateTime->CalId = CalId;
    CalDateTime->Era = Era;
    if (CalId == CAL_JAPAN)
        CalDateTime->Year = Year - Eras[Era - 1].FirstYear + 1;
    else if (CalId == CAL_TAIWAN)
        CalDateTime->Year = Year - 1911;
    else
        CalDateTime->Year = Year + (ULONG)CAL_YearOffset(CalId);
    CalDateTime->Month = Month;
    CalDateTime->Day = Day;
    CalDateTime->DayOfWeek = CAL_DayOfWeek(Days);
    return TRUE;
}

static BOOL CAL_ToSystemTime(const CALDATETIME *CalDateTime, LPSYSTEMTIME SystemTime)
{
    ULONG Year, Month, Day;
    LONG Days;

    if (!CAL_ToDays(CalDateTime, &Days))
        return FALSE;

    CAL_DateFromDays(Days, &Year, &Month, &Day);
    SystemTime->wYear = (WORD)Year;
    SystemTime->wMonth = (WORD)Month;
    SystemTime->wDayOfWeek = (WORD)CAL_DayOfWeek(Days);
    SystemTime->wDay = (WORD)Day;
    SystemTime->wHour = (WORD)CalDateTime->Hour;
    SystemTime->wMinute = (WORD)CalDateTime->Minute;
    SystemTime->wSecond = (WORD)CalDateTime->Second;
    SystemTime->wMilliseconds = (WORD)(CalDateTime->Tick / CAL_TICKS_PER_MS);
    return TRUE;
}

BOOL
WINAPI
IsCalendarLeapYear(
    _In_ CALID calId,
    _In_ UINT year,
    _In_ UINT era)
{
    ULONG GregorianYear;

    if (!CAL_GregorianYearFromCalendar(calId, era, year, &GregorianYear))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return CAL_IsLeapYear(GregorianYear);
}

BOOL
WINAPI
UpdateCalendarDayOfWeek(
    _Inout_ LPCALDATETIME lpCalDateTime)
{
    LONG Days;

    if (!CAL_ToDays(lpCalDateTime, &Days))
        return FALSE;

    lpCalDateTime->DayOfWeek = CAL_DayOfWeek(Days);
    return TRUE;
}

BOOL
WINAPI
ConvertCalDateTimeToSystemTime(
    _In_ const LPCALDATETIME lpCalDateTime,
    _Out_ SYSTEMTIME *lpSysTime)
{
    SYSTEMTIME SystemTime;

    if (!lpSysTime)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!CAL_ToSystemTime(lpCalDateTime, &SystemTime))
        return FALSE;

    *lpSysTime = SystemTime;
    return TRUE;
}

BOOL
WINAPI
ConvertSystemTimeToCalDateTime(
    _In_ const SYSTEMTIME *lpSysTime,
    _In_ CALID calId,
    _Out_ LPCALDATETIME lpCalDateTime)
{
    CALDATETIME CalDateTime;
    LONG Days;

    if (!lpSysTime || !lpCalDateTime ||
        lpSysTime->wYear < CAL_FIRST_YEAR || lpSysTime->wYear > CAL_LAST_YEAR ||
        lpSysTime->wMonth < 1 || lpSysTime->wMonth > 12 ||
        lpSysTime->wDay < 1 || lpSysTime->wDay > CAL_DaysInMonth(lpSysTime->wYear, lpSysTime->wMonth) ||
        lpSysTime->wHour > 23 || lpSysTime->wMinute > 59 || lpSysTime->wSecond > 59 ||
        lpSysTime->wMilliseconds > 999)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Days = CAL_DaysFromDate(lpSysTime->wYear, lpSysTime->wMonth, lpSysTime->wDay);
    if (!CAL_FromDays(calId, Days, &CalDateTime))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    CalDateTime.Hour = lpSysTime->wHour;
    CalDateTime.Minute = lpSysTime->wMinute;
    CalDateTime.Second = lpSysTime->wSecond;
    CalDateTime.Tick = (ULONG)lpSysTime->wMilliseconds * CAL_TICKS_PER_MS;
    *lpCalDateTime = CalDateTime;
    return TRUE;
}

BOOL
WINAPI
AdjustCalendarDate(
    _Inout_ LPCALDATETIME lpCalDateTime,
    _In_ CALDATETIME_DATEUNIT calUnit,
    _In_ INT amount)
{
    CALDATETIME Result;
    ULONG Year, Month, Day;
    LONGLONG NewDays, NewYear, Months;
    LONG Days;

    if (!CAL_ToDays(lpCalDateTime, &Days))
        return FALSE;

    switch (calUnit)
    {
    case YearUnit:
    case MonthUnit:
        CAL_DateFromDays(Days, &Year, &Month, &Day);
        Months = (LONGLONG)Year * 12 + (Month - 1) + ((calUnit == YearUnit) ? (LONGLONG)amount * 12 : amount);
        NewYear = Months / 12;
        if (Months < 0 || NewYear < CAL_FIRST_YEAR || NewYear > CAL_LAST_YEAR)
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        Year = (ULONG)NewYear;
        Month = (ULONG)(Months % 12) + 1;
        Day = min(Day, CAL_DaysInMonth(Year, Month));
        NewDays = CAL_DaysFromDate(Year, Month, Day);
        break;

    case WeekUnit:
        NewDays = (LONGLONG)Days + (LONGLONG)amount * 7;
        break;

    case DayUnit:
        NewDays = (LONGLONG)Days + amount;
        break;

    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (NewDays < 0 || NewDays > CAL_DaysFromDate(CAL_LAST_YEAR, 12, 31))
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    Result = *lpCalDateTime;
    if (!CAL_FromDays(lpCalDateTime->CalId, (LONG)NewDays, &Result))
        return FALSE;

    *lpCalDateTime = Result;
    return TRUE;
}

BOOL
WINAPI
GetCalendarSupportedDateRange(
    _In_ CALID Calendar,
    _Out_ LPCALDATETIME lpCalMinDateTime,
    _Out_ LPCALDATETIME lpCalMaxDateTime)
{
    CAL_ERA_RANGE Eras[16];
    CALDATETIME MinDateTime, MaxDateTime;

    if (!lpCalMinDateTime || !lpCalMaxDateTime || !CAL_GetEras(Calendar, Eras, ARRAYSIZE(Eras)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!CAL_FromDays(Calendar, Eras[0].FirstDay, &MinDateTime) ||
        !CAL_FromDays(Calendar, CAL_DaysFromDate(CAL_LAST_YEAR, 12, 31), &MaxDateTime))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    MinDateTime.Hour = 0;
    MinDateTime.Minute = 0;
    MinDateTime.Second = 0;
    MinDateTime.Tick = 0;
    MaxDateTime.Hour = 23;
    MaxDateTime.Minute = 59;
    MaxDateTime.Second = 59;
    MaxDateTime.Tick = CAL_TICKS_PER_SEC - 1;

    *lpCalMinDateTime = MinDateTime;
    *lpCalMaxDateTime = MaxDateTime;
    return TRUE;
}

BOOL
WINAPI
GetCalendarDateFormatEx(
    _In_opt_ LPCWSTR lpszLocale,
    _In_ DWORD dwFlags,
    _In_ const LPCALDATETIME lpCalDateTime,
    _In_opt_ LPCWSTR lpFormat,
    _Out_writes_opt_(cchDate) LPWSTR lpDateStr,
    _In_ int cchDate)
{
    static const DWORD FormatFlags = DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH;
    static const DWORD ReadingFlags = DATE_LTRREADING | DATE_RTLREADING;
    SYSTEMTIME SystemTime;
    DWORD CalendarType;

    if (cchDate < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (cchDate && !lpDateStr)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    if ((lpFormat && dwFlags) ||
        (dwFlags & ~(FormatFlags | ReadingFlags | LOCALE_NOUSEROVERRIDE)) ||
        ((dwFlags & FormatFlags) & ((dwFlags & FormatFlags) - 1)) ||
        (dwFlags & ReadingFlags) == ReadingFlags)
    {
        SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    if (!CAL_ToSystemTime(lpCalDateTime, &SystemTime))
        return FALSE;

    if (!GetLocaleInfoEx(lpszLocale, LOCALE_ICALENDARTYPE | LOCALE_RETURN_NUMBER | (dwFlags & LOCALE_NOUSEROVERRIDE),
                         (LPWSTR)&CalendarType, sizeof(CalendarType) / sizeof(WCHAR)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (CalendarType != lpCalDateTime->CalId)
    {
        /* TODO: format calendars other than the locale's current calendar; they fail with ERROR_INVALID_PARAMETER */
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return GetDateFormatEx(lpszLocale, dwFlags, &SystemTime, lpFormat, lpDateStr, cchDate, NULL);
}
