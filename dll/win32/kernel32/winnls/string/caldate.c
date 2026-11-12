/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calendar date functions working on CALDATETIME
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <k32.h>
#include <math.h>
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

int WINAPI GetCalendarInfoEx(LPCWSTR lpLocaleName, CALID Calendar, LPCWSTR lpReserved, CALTYPE CalType,
                             LPWSTR lpCalData, int cchData, LPDWORD lpValue);

#define CAL_FIRST_YEAR      1601
#define CAL_LAST_YEAR       9999
#define CAL_TICKS_PER_MS    10000
#define CAL_TICKS_PER_SEC   10000000
#define CAL_RD_FIRST_DAY    584389
#define CAL_MAX_STRING      80
#define CAL_PI              3.14159265358979323846
#define CAL_SECONDS_PER_DAY 86400.0
#define CAL_PERSIAN_MERIDIAN 52.5
#define CAL_HEBREW_EPOCH_RD (-1373427)
#define CAL_UMALQURA_FIRST_YEAR 1318

/* FIXME: the documented ERROR_DATE_OUT_OF_RANGE has no published value; out-of-range dates report ERROR_INVALID_PARAMETER */
#define CAL_ERROR_DATE_OUT_OF_RANGE ERROR_INVALID_PARAMETER

typedef struct _CAL_ERA_RANGE
{
    LONG FirstDay;
    ULONG FirstYear;
} CAL_ERA_RANGE;

typedef struct _CAL_CONTEXT
{
    CALID CalId;
    LONG HijriAdjustment;
} CAL_CONTEXT;

typedef struct _CAL_SOLAR_TERM
{
    double Amplitude;
    double Phase;
    double Rate;
} CAL_SOLAR_TERM;

typedef struct _CAL_OUTPUT
{
    LPWSTR Buffer;
    int Size;
    int Length;
    BOOL Overflow;
} CAL_OUTPUT;

static const USHORT CAL_UmAlQuraMonths[] =
{
    0x2EA, 0x6E9, 0xED2, 0xEA4, 0xD4A, 0xA96, 0x536, 0xAB5, 0xDAA, 0xBA4, 0xB49, 0xA93,
    0x52B, 0xA57, 0x4B6, 0xAB5, 0x5AA, 0xD55, 0xD2A, 0xA56, 0x4AE, 0x95D, 0x2EC, 0x6D5,
    0x6AA, 0x555, 0x4AB, 0x95B, 0x2BA, 0x575, 0xBB2, 0x764, 0x749, 0x655, 0x2AB, 0x55B,
    0xADA, 0x6D4, 0xEC9, 0xD92, 0xD25, 0xA4D, 0x2AD, 0x56D, 0xB6A, 0xB52, 0xAA5, 0xA4B,
    0x497, 0x937, 0x2B6, 0x575, 0xD6A, 0xD52, 0xA96, 0x92D, 0x25D, 0x4DD, 0xADA, 0x5D4,
    0xDA9, 0xD52, 0xAAA, 0x4D6, 0x9B6, 0x374, 0x769, 0x752, 0x6A5, 0x54B, 0xAAB, 0x55A,
    0xAD5, 0xDD2, 0xDA4, 0xD49, 0xA95, 0x52D, 0xA5D, 0x55A, 0xAD5, 0x6AA, 0x695, 0x52B,
    0xA57, 0x4AE, 0x976, 0x56C, 0xB55, 0xAAA, 0xA55, 0x4AD, 0x95D, 0x2DA, 0x5D9, 0xDB2,
    0xBA4, 0xB4A, 0xA55, 0x2B5, 0x575, 0xB6A, 0xBD2, 0xBC4, 0xB89, 0xA95, 0x52D, 0x5AD,
    0xB6A, 0x6D4, 0xDC9, 0xD92, 0xAA6, 0x956, 0x2AE, 0x56D, 0x36A, 0xB55, 0xAAA, 0x94D,
    0x49D, 0x95D, 0x2BA, 0x5B5, 0x5AA, 0xD55, 0xA9A, 0x92E, 0x26E, 0x55D, 0xADA, 0x6D4,
    0x6A5, 0x54B, 0xA97, 0x54E, 0xAAE, 0x5AC, 0xBA9, 0xD92, 0xB25, 0x64B, 0xCAB, 0x55A,
    0xB55, 0x6D2, 0xEA5, 0xE4A, 0xA95, 0x52D, 0xAAD, 0x36C, 0x759, 0x6D2, 0x695, 0x52D,
    0xA5B, 0x4BA, 0x9BA, 0x3B4, 0xB69, 0xB52, 0xAA6, 0x4B6, 0x96D, 0x2EC, 0x6D9, 0xEB2,
    0xD54, 0xD2A, 0xA56, 0x4AE, 0x96D, 0xD6A, 0xB54, 0xB29, 0xA93, 0x52B, 0xA57, 0x536,
    0xAB5, 0x6AA, 0xE93,
};

static const CAL_SOLAR_TERM CAL_SolarTerms[] =
{
    { 403406, 270.54861, 0.9287892 }, { 195207, 340.19128, 35999.1376958 }, { 119433, 63.91854, 35999.4089666 },
    { 112392, 331.26220, 35998.7287385 }, { 3891, 317.843, 71998.20261 }, { 2819, 86.631, 71998.4403 },
    { 1721, 240.052, 36000.35726 }, { 660, 310.26, 71997.4812 }, { 350, 247.23, 32964.4678 },
    { 334, 260.87, -19.4410 }, { 314, 297.82, 445267.1117 }, { 268, 343.14, 45036.8840 },
    { 242, 166.79, 3.1008 }, { 234, 81.53, 22518.4434 }, { 158, 3.50, -19.9739 },
    { 132, 132.75, 65928.9345 }, { 129, 182.95, 9038.0293 }, { 114, 162.03, 3034.7684 },
    { 99, 29.8, 33718.148 }, { 93, 266.4, 3034.448 }, { 86, 249.2, -2280.773 },
    { 78, 157.6, 29929.992 }, { 72, 257.8, 31556.493 }, { 68, 185.1, 149.588 },
    { 64, 69.9, 9037.750 }, { 46, 8.0, 107997.405 }, { 38, 197.1, -4444.176 },
    { 37, 250.4, 151.771 }, { 32, 65.3, 67555.316 }, { 29, 162.7, 31556.080 },
    { 28, 341.5, -4561.540 }, { 27, 291.6, 107996.706 }, { 27, 98.5, 1221.655 },
    { 25, 146.7, 62894.167 }, { 24, 110.0, 31437.369 }, { 21, 5.2, 14578.298 },
    { 21, 342.6, -31931.757 }, { 20, 230.9, 34777.243 }, { 18, 256.1, 1221.999 },
    { 17, 45.3, 62894.511 }, { 14, 242.9, -4442.039 }, { 13, 115.2, 107997.909 },
    { 13, 151.8, 119.066 }, { 13, 285.3, 16859.071 }, { 12, 53.3, -4.578 },
    { 10, 126.6, 26895.292 }, { 10, 205.7, -39.127 }, { 10, 85.9, 12297.536 },
    { 10, 146.1, 90073.778 },
};

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

static LONG CAL_RdFromGregorian(LONG Year, ULONG Month, ULONG Day)
{
    LONG PriorYears = Year - 1;
    LONG Rd = PriorYears * 365 + PriorYears / 4 - PriorYears / 100 + PriorYears / 400;
    ULONG i;

    for (i = 1; i < Month; i++)
        Rd += (LONG)CAL_DaysInMonth((ULONG)Year, i);
    return Rd + (LONG)Day;
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

static BOOL CAL_HasOwnMonths(CALID CalId)
{
    return CalId == CAL_HIJRI || CalId == CAL_HEBREW || CalId == CAL_PERSIAN || CalId == CAL_UMALQURA;
}

static LONG CAL_GetHijriAdjustment(VOID)
{
    static const UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"Control Panel\\International");
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"AddHijriDate");
    static const UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"AddHijriDate");
    ULONG Buffer[(FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 32 * sizeof(WCHAR)) / sizeof(ULONG) + 1];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Value;
    HANDLE CurrentUser, KeyHandle;
    ULONG ReturnLength, Count, i;
    LONG Adjustment = 0;
    BOOL Negative = FALSE;
    NTSTATUS Status;

    if (!NT_SUCCESS(RtlOpenCurrentUser(KEY_QUERY_VALUE, &CurrentUser)))
        return 0;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&KeyName, OBJ_CASE_INSENSITIVE, CurrentUser, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    NtClose(CurrentUser);
    if (!NT_SUCCESS(Status))
        return 0;

    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&ValueName, KeyValuePartialInformation,
                             Information, sizeof(Buffer), &ReturnLength);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status) || (Information->Type != REG_SZ && Information->Type != REG_EXPAND_SZ))
        return 0;

    Value.Buffer = (PWSTR)Information->Data;
    Count = Information->DataLength / sizeof(WCHAR);
    while (Count && !Value.Buffer[Count - 1])
        Count--;
    Value.Length = Value.MaximumLength = (USHORT)(Count * sizeof(WCHAR));
    if (!RtlPrefixUnicodeString(&Prefix, &Value, TRUE))
        return 0;

    i = Prefix.Length / sizeof(WCHAR);
    if (i == Count)
        return -1;

    if (Value.Buffer[i] == L'+' || Value.Buffer[i] == L'-')
    {
        Negative = (Value.Buffer[i] == L'-');
        i++;
    }
    if (i == Count)
        return 0;

    for (; i < Count; i++)
    {
        if (Value.Buffer[i] < L'0' || Value.Buffer[i] > L'9' || Adjustment > 2)
            return 0;
        Adjustment = Adjustment * 10 + (Value.Buffer[i] - L'0');
    }

    if (Adjustment > 2)
        return 0;
    return Negative ? -Adjustment : Adjustment;
}

static VOID CAL_InitContext(CAL_CONTEXT *Context, CALID CalId)
{
    Context->CalId = CalId;
    Context->HijriAdjustment = (CalId == CAL_HIJRI) ? CAL_GetHijriAdjustment() : 0;
}

static double CAL_SinDegrees(double Degrees)
{
    return sin(Degrees * (CAL_PI / 180.0));
}

static double CAL_CosDegrees(double Degrees)
{
    return cos(Degrees * (CAL_PI / 180.0));
}

static double CAL_Polynomial(const double *Coefficients, ULONG Count, double X)
{
    double Sum = 0.0;

    while (Count--)
        Sum = Sum * X + Coefficients[Count];
    return Sum;
}

static double CAL_EphemerisCorrection(LONG Year)
{
    static const double Since1900[] = { -0.00002, 0.000297, 0.025184, -0.181133, 0.553040, -0.861938, 0.677066, -0.212591 };
    static const double Since1800[] = { -0.000009, 0.003844, 0.083563, 0.865736, 4.867575, 15.845535, 31.332267,
                                        38.291999, 28.316289, 11.636204, 2.043794 };
    static const double Since1700[] = { 8.118780842, -0.005092142, 0.003336121, -0.0000266484 };
    static const double Since1600[] = { 196.58333, -4.0675, 0.0219167 };
    double Centuries, X;

    if (Year >= 1988 && Year <= 2019)
        return (Year - 1933) / CAL_SECONDS_PER_DAY;

    if (Year >= 1800 && Year <= 1987)
    {
        Centuries = (CAL_RdFromGregorian(Year, 7, 1) - CAL_RdFromGregorian(1900, 1, 1)) / 36525.0;
        if (Year >= 1900)
            return CAL_Polynomial(Since1900, ARRAYSIZE(Since1900), Centuries);
        return CAL_Polynomial(Since1800, ARRAYSIZE(Since1800), Centuries);
    }

    if (Year >= 1700 && Year <= 1799)
        return CAL_Polynomial(Since1700, ARRAYSIZE(Since1700), Year - 1700) / CAL_SECONDS_PER_DAY;

    if (Year >= 1620 && Year <= 1699)
        return CAL_Polynomial(Since1600, ARRAYSIZE(Since1600), Year - 1600) / CAL_SECONDS_PER_DAY;

    X = 0.5 + (CAL_RdFromGregorian(Year, 1, 1) - CAL_RdFromGregorian(1810, 1, 1));
    return (X * X / 41048480.0 - 15.0) / CAL_SECONDS_PER_DAY;
}

static double CAL_JulianCenturies(double Moment, LONG Year)
{
    return (Moment + CAL_EphemerisCorrection(Year) - 730120.5) / 36525.0;
}

static double CAL_SolarLongitude(double Moment, LONG Year)
{
    double Centuries = CAL_JulianCenturies(Moment, Year);
    double Sum = 0.0, Longitude;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(CAL_SolarTerms); i++)
        Sum += CAL_SolarTerms[i].Amplitude * CAL_SinDegrees(CAL_SolarTerms[i].Phase + CAL_SolarTerms[i].Rate * Centuries);

    Longitude = 282.7771834 + 36000.76953744 * Centuries + 0.000005729577951308232 * Sum;
    Longitude += 0.0000974 * CAL_CosDegrees(177.63 + 35999.01848 * Centuries) - 0.005575;
    Longitude += -0.004778 * CAL_SinDegrees(124.90 - 1934.134 * Centuries + 0.002063 * Centuries * Centuries) -
                 0.0003667 * CAL_SinDegrees(201.11 + 72001.5377 * Centuries + 0.00057 * Centuries * Centuries);
    return Longitude - 360.0 * floor(Longitude / 360.0);
}

static double CAL_EquationOfTime(double Moment, LONG Year)
{
    double Centuries = CAL_JulianCenturies(Moment, Year);
    double Longitude = 280.46645 + 36000.76983 * Centuries + 0.0003032 * Centuries * Centuries;
    double Anomaly = 357.52910 + 35999.05030 * Centuries - 0.0001559 * Centuries * Centuries -
                     0.00000048 * Centuries * Centuries * Centuries;
    double Eccentricity = 0.016708617 - 0.000042037 * Centuries - 0.0000001236 * Centuries * Centuries;
    double Obliquity = 23.0 + 26.0 / 60.0 + (21.448 - 46.8150 * Centuries - 0.00059 * Centuries * Centuries +
                       0.001813 * Centuries * Centuries * Centuries) / 3600.0;
    double HalfObliquityTangent = tan(Obliquity / 2.0 * (CAL_PI / 180.0));
    double Y = HalfObliquityTangent * HalfObliquityTangent;
    double Equation;

    Equation = (Y * CAL_SinDegrees(2.0 * Longitude) -
                2.0 * Eccentricity * CAL_SinDegrees(Anomaly) +
                4.0 * Eccentricity * Y * CAL_SinDegrees(Anomaly) * CAL_CosDegrees(2.0 * Longitude) -
                0.5 * Y * Y * CAL_SinDegrees(4.0 * Longitude) -
                1.25 * Eccentricity * Eccentricity * CAL_SinDegrees(2.0 * Anomaly)) / (2.0 * CAL_PI);

    if (Equation > 0.5)
        return 0.5;
    if (Equation < -0.5)
        return -0.5;
    return Equation;
}

static LONG CAL_PersianNewYearRd(LONG GregorianYear)
{
    LONG Rd = CAL_RdFromGregorian(GregorianYear, 3, 15);
    LONG Last = Rd + 12;
    double Noon;

    for (; Rd < Last; Rd++)
    {
        Noon = Rd + 0.5 - CAL_PERSIAN_MERIDIAN / 360.0;
        Noon -= CAL_EquationOfTime(Noon, GregorianYear);
        if (CAL_SolarLongitude(Noon, GregorianYear) <= 2.0)
            break;
    }
    return Rd;
}

static LONG CAL_HebrewElapsedDays(LONG Year)
{
    LONGLONG MonthsElapsed = (235LL * Year - 234) / 19;
    LONGLONG PartsElapsed = 12084 + 13753 * MonthsElapsed;
    LONGLONG Day = 29 * MonthsElapsed + PartsElapsed / 25920;

    if ((3 * (Day + 1)) % 7 < 3)
        Day++;
    return (LONG)Day;
}

static LONG CAL_HebrewNewYearRd(LONG Year)
{
    LONG Previous = CAL_HebrewElapsedDays(Year - 1);
    LONG Current = CAL_HebrewElapsedDays(Year);
    LONG Next = CAL_HebrewElapsedDays(Year + 1);
    LONG Correction = 0;

    if (Next - Current == 356)
        Correction = 2;
    else if (Current - Previous == 382)
        Correction = 1;
    return CAL_HEBREW_EPOCH_RD + Current + Correction;
}

static BOOL CAL_HebrewIsLeapYear(LONG Year)
{
    return (7 * Year + 1) % 19 < 7;
}

static ULONG CAL_HebrewMonthLength(LONG Year, ULONG Month)
{
    static const UCHAR Common[12] = { 30, 29, 30, 29, 30, 29, 30, 29, 30, 29, 30, 29 };
    static const UCHAR Leap[13] = { 30, 29, 30, 29, 30, 30, 29, 30, 29, 30, 29, 30, 29 };
    LONG YearLength = CAL_HebrewNewYearRd(Year + 1) - CAL_HebrewNewYearRd(Year);

    if (Month == 2 && YearLength % 10 == 5)
        return 30;
    if (Month == 3 && YearLength % 10 == 3)
        return 29;
    return CAL_HebrewIsLeapYear(Year) ? Leap[Month - 1] : Common[Month - 1];
}

static LONGLONG CAL_HebrewMonthsBefore(LONG Year)
{
    return (235LL * Year - 234) / 19;
}

static LONG CAL_HijriEpoch(VOID)
{
    return CAL_RdFromGregorian(622, 7, 18) - CAL_RD_FIRST_DAY;
}

static BOOL CAL_HijriIsLeapYear(LONG Year)
{
    return (14 + 11 * Year) % 30 < 11;
}

static ULONG CAL_UmAlQuraYearLength(LONG Year)
{
    USHORT Months = CAL_UmAlQuraMonths[Year - CAL_UMALQURA_FIRST_YEAR];
    ULONG Length = 0, i;

    for (i = 0; i < 12; i++)
        Length += (Months & (1 << i)) ? 30 : 29;
    return Length;
}

static VOID CAL_OwnDayRange(const CAL_CONTEXT *Context, PLONG MinDays, PLONG MaxDays)
{
    *MinDays = 0;
    *MaxDays = CAL_DaysFromDate(CAL_LAST_YEAR, 12, 31);

    if (Context->CalId == CAL_HEBREW)
    {
        *MaxDays = CAL_DaysFromDate(2239, 9, 29);
    }
    else if (Context->CalId == CAL_UMALQURA)
    {
        *MinDays = CAL_DaysFromDate(1900, 4, 30);
        *MaxDays = CAL_DaysFromDate(2077, 11, 16);
    }
}

static LONG CAL_OwnYearStart(const CAL_CONTEXT *Context, LONG Year)
{
    LONG Days, i;

    switch (Context->CalId)
    {
    case CAL_HIJRI:
        return CAL_HijriEpoch() + 354 * (Year - 1) + (3 + 11 * Year) / 30 - Context->HijriAdjustment;

    case CAL_HEBREW:
        return CAL_HebrewNewYearRd(Year) - CAL_RD_FIRST_DAY;

    case CAL_PERSIAN:
        return CAL_PersianNewYearRd(Year + 621) - CAL_RD_FIRST_DAY;

    default:
        Days = CAL_DaysFromDate(1900, 4, 30);
        for (i = CAL_UMALQURA_FIRST_YEAR; i < Year; i++)
            Days += (LONG)CAL_UmAlQuraYearLength(i);
        return Days;
    }
}

static ULONG CAL_OwnMonthsInYear(const CAL_CONTEXT *Context, LONG Year)
{
    if (Context->CalId == CAL_HEBREW && CAL_HebrewIsLeapYear(Year))
        return 13;
    return 12;
}

static ULONG CAL_OwnMonthLength(const CAL_CONTEXT *Context, LONG Year, ULONG Month)
{
    switch (Context->CalId)
    {
    case CAL_HIJRI:
        if (Month == 12 && CAL_HijriIsLeapYear(Year))
            return 30;
        return (Month % 2) ? 30 : 29;

    case CAL_HEBREW:
        return CAL_HebrewMonthLength(Year, Month);

    case CAL_PERSIAN:
        if (Month <= 6)
            return 31;
        if (Month <= 11)
            return 30;
        return (ULONG)(CAL_OwnYearStart(Context, Year + 1) - CAL_OwnYearStart(Context, Year) - 336);

    default:
        return (CAL_UmAlQuraMonths[Year - CAL_UMALQURA_FIRST_YEAR] & (1 << (Month - 1))) ? 30 : 29;
    }
}

static BOOL CAL_OwnIsLeapYear(const CAL_CONTEXT *Context, LONG Year)
{
    switch (Context->CalId)
    {
    case CAL_HIJRI:
        return CAL_HijriIsLeapYear(Year);

    case CAL_HEBREW:
        return CAL_HebrewIsLeapYear(Year);

    case CAL_PERSIAN:
        return CAL_OwnYearStart(Context, Year + 1) - CAL_OwnYearStart(Context, Year) == 366;

    default:
        return CAL_UmAlQuraYearLength(Year) == 355;
    }
}

static LONG CAL_OwnYearOf(const CAL_CONTEXT *Context, LONG Days)
{
    ULONG GregorianYear, Month, Day;
    LONG Year;

    switch (Context->CalId)
    {
    case CAL_HIJRI:
        Year = (LONG)((30LL * (Days + Context->HijriAdjustment - CAL_HijriEpoch()) + 10646) / 10631);
        break;

    case CAL_HEBREW:
        Year = (LONG)((LONGLONG)(Days + CAL_RD_FIRST_DAY - CAL_HEBREW_EPOCH_RD) * 98496 / 35975351) + 1;
        break;

    case CAL_PERSIAN:
        CAL_DateFromDays(Days, &GregorianYear, &Month, &Day);
        Year = (LONG)GregorianYear - 621;
        break;

    default:
        Year = CAL_UMALQURA_FIRST_YEAR;
        while (Year < CAL_UMALQURA_FIRST_YEAR + (LONG)ARRAYSIZE(CAL_UmAlQuraMonths) - 1 &&
               CAL_OwnYearStart(Context, Year + 1) <= Days)
        {
            Year++;
        }
        return Year;
    }

    while (CAL_OwnYearStart(Context, Year + 1) <= Days)
        Year++;
    while (CAL_OwnYearStart(Context, Year) > Days)
        Year--;
    return Year;
}

static LONG CAL_OwnDateToDays(const CAL_CONTEXT *Context, LONG Year, ULONG Month, ULONG Day)
{
    LONG Days = CAL_OwnYearStart(Context, Year);
    ULONG i;

    for (i = 1; i < Month; i++)
        Days += (LONG)CAL_OwnMonthLength(Context, Year, i);
    return Days + (LONG)Day - 1;
}

static VOID CAL_OwnYearRange(const CAL_CONTEXT *Context, PLONG MinYear, PLONG MaxYear)
{
    LONG MinDays, MaxDays;

    CAL_OwnDayRange(Context, &MinDays, &MaxDays);
    *MinYear = CAL_OwnYearOf(Context, MinDays);
    *MaxYear = CAL_OwnYearOf(Context, MaxDays);
}

static LONGLONG CAL_OwnMonthIndex(const CAL_CONTEXT *Context, LONG Year, ULONG Month)
{
    if (Context->CalId == CAL_HEBREW)
        return CAL_HebrewMonthsBefore(Year) + Month - 1;
    return (LONGLONG)Year * 12 + Month - 1;
}

static VOID CAL_OwnFromMonthIndex(const CAL_CONTEXT *Context, LONGLONG Index, PLONG Year, PULONG Month)
{
    if (Context->CalId != CAL_HEBREW)
    {
        *Year = (LONG)(Index / 12);
        *Month = (ULONG)(Index % 12) + 1;
        return;
    }

    *Year = (LONG)((19 * Index + 234) / 235) + 1;
    while (CAL_HebrewMonthsBefore(*Year) > Index)
        (*Year)--;
    while (CAL_HebrewMonthsBefore(*Year + 1) <= Index)
        (*Year)++;
    *Month = (ULONG)(Index - CAL_HebrewMonthsBefore(*Year)) + 1;
}

static BOOL CAL_OwnToDays(const CALDATETIME *CalDateTime, PLONG Days)
{
    CAL_CONTEXT Context;
    LONG MinDays, MaxDays, MinYear, MaxYear, Result;

    CAL_InitContext(&Context, CalDateTime->CalId);
    CAL_OwnDayRange(&Context, &MinDays, &MaxDays);
    CAL_OwnYearRange(&Context, &MinYear, &MaxYear);

    if (CalDateTime->Era != 1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((LONGLONG)CalDateTime->Year < MinYear || (LONGLONG)CalDateTime->Year > MaxYear)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    if (CalDateTime->Month < 1 || CalDateTime->Month > CAL_OwnMonthsInYear(&Context, (LONG)CalDateTime->Year) ||
        CalDateTime->Day < 1 || CalDateTime->Day > CAL_OwnMonthLength(&Context, (LONG)CalDateTime->Year, CalDateTime->Month) ||
        CalDateTime->Hour > 23 || CalDateTime->Minute > 59 || CalDateTime->Second > 59 ||
        CalDateTime->Tick >= CAL_TICKS_PER_SEC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Result = CAL_OwnDateToDays(&Context, (LONG)CalDateTime->Year, CalDateTime->Month, CalDateTime->Day);
    if (Result < MinDays || Result > MaxDays)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    *Days = Result;
    return TRUE;
}

static BOOL CAL_OwnFromDays(CALID CalId, LONG Days, LPCALDATETIME CalDateTime)
{
    CAL_CONTEXT Context;
    LONG MinDays, MaxDays, Year, Remaining;
    ULONG Month;

    CAL_InitContext(&Context, CalId);
    CAL_OwnDayRange(&Context, &MinDays, &MaxDays);
    if (Days < MinDays || Days > MaxDays)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }

    Year = CAL_OwnYearOf(&Context, Days);
    Remaining = Days - CAL_OwnYearStart(&Context, Year);
    for (Month = 1; Remaining >= (LONG)CAL_OwnMonthLength(&Context, Year, Month); Month++)
        Remaining -= (LONG)CAL_OwnMonthLength(&Context, Year, Month);

    CalDateTime->CalId = CalId;
    CalDateTime->Era = 1;
    CalDateTime->Year = (UINT)Year;
    CalDateTime->Month = Month;
    CalDateTime->Day = (UINT)Remaining + 1;
    CalDateTime->DayOfWeek = CAL_DayOfWeek(Days);
    return TRUE;
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

    if (CAL_HasOwnMonths(CalDateTime->CalId))
        return CAL_OwnToDays(CalDateTime, Days);

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

    if (CAL_HasOwnMonths(CalId))
        return CAL_OwnFromDays(CalId, Days, CalDateTime);

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

static BOOL CAL_OwnAdjust(LPCALDATETIME CalDateTime, LONG Days, CALDATETIME_DATEUNIT Unit, INT Amount, PLONG NewDays)
{
    CAL_CONTEXT Context;
    LONG MinDays, MaxDays, MinYear, MaxYear, Year;
    LONGLONG Index, Target;
    ULONG Month, Day;

    CAL_InitContext(&Context, CalDateTime->CalId);
    CAL_OwnDayRange(&Context, &MinDays, &MaxDays);
    CAL_OwnYearRange(&Context, &MinYear, &MaxYear);

    switch (Unit)
    {
    case YearUnit:
        Target = (LONGLONG)CalDateTime->Year + Amount;
        if (Target < MinYear || Target > MaxYear)
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        Year = (LONG)Target;
        Month = min(CalDateTime->Month, CAL_OwnMonthsInYear(&Context, Year));
        break;

    case MonthUnit:
        Index = CAL_OwnMonthIndex(&Context, (LONG)CalDateTime->Year, CalDateTime->Month) + Amount;
        if (Index < CAL_OwnMonthIndex(&Context, MinYear, 1) ||
            Index > CAL_OwnMonthIndex(&Context, MaxYear, CAL_OwnMonthsInYear(&Context, MaxYear)))
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        CAL_OwnFromMonthIndex(&Context, Index, &Year, &Month);
        break;

    case WeekUnit:
    case DayUnit:
        Target = (LONGLONG)Days + ((Unit == WeekUnit) ? (LONGLONG)Amount * 7 : Amount);
        if (Target < MinDays || Target > MaxDays)
        {
            SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
            return FALSE;
        }
        *NewDays = (LONG)Target;
        return TRUE;

    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Day = min(CalDateTime->Day, CAL_OwnMonthLength(&Context, Year, Month));
    *NewDays = CAL_OwnDateToDays(&Context, Year, Month, Day);
    if (*NewDays < MinDays || *NewDays > MaxDays)
    {
        SetLastError(CAL_ERROR_DATE_OUT_OF_RANGE);
        return FALSE;
    }
    return TRUE;
}

static VOID CAL_Append(CAL_OUTPUT *Output, LPCWSTR String, int Count)
{
    int i;

    for (i = 0; i < Count; i++)
    {
        if (Output->Size)
        {
            if (Output->Length >= Output->Size)
            {
                Output->Overflow = TRUE;
                return;
            }
            Output->Buffer[Output->Length] = String[i];
        }
        Output->Length++;
    }
}

static VOID CAL_AppendNumber(CAL_OUTPUT *Output, ULONG Value, ULONG MinDigits)
{
    WCHAR Digits[16];
    ULONG Count = 0;

    do
    {
        Digits[ARRAYSIZE(Digits) - 1 - Count++] = (WCHAR)(L'0' + Value % 10);
        Value /= 10;
    } while (Value || Count < MinDigits);

    CAL_Append(Output, &Digits[ARRAYSIZE(Digits) - Count], (int)Count);
}

static VOID CAL_AppendCalendarString(CAL_OUTPUT *Output, LPCWSTR Locale, CALID CalId, CALTYPE Type)
{
    WCHAR Name[CAL_MAX_STRING];

    if (GetCalendarInfoEx(Locale, CalId, NULL, Type, Name, ARRAYSIZE(Name), NULL))
        CAL_Append(Output, Name, (int)wcslen(Name));
}

static VOID CAL_AppendEra(CAL_OUTPUT *Output, LPCWSTR Locale, const CALDATETIME *CalDateTime)
{
    PCJAPANESE_ERA Table;
    DWORD Count = 0;

    if (CalDateTime->CalId != CAL_JAPAN)
    {
        CAL_AppendCalendarString(Output, Locale, CalDateTime->CalId, CAL_SERASTRING);
        return;
    }

    Table = JapaneseEra_GetTable(&Count);
    if (CalDateTime->Era >= 1 && CalDateTime->Era <= Count)
        CAL_Append(Output, Table[CalDateTime->Era - 1].szEraName, (int)wcslen(Table[CalDateTime->Era - 1].szEraName));
}

static VOID CAL_AppendYear(CAL_OUTPUT *Output, const CALDATETIME *CalDateTime, int Count)
{
    static const WCHAR Gannen[] = { 0x5143 };

    if (CalDateTime->CalId == CAL_JAPAN || CalDateTime->CalId == CAL_TAIWAN)
    {
        if (CalDateTime->CalId == CAL_JAPAN && CalDateTime->Year == 1 && JapaneseEra_IsFirstYearGannen())
            CAL_Append(Output, Gannen, ARRAYSIZE(Gannen));
        else
            CAL_AppendNumber(Output, CalDateTime->Year, (Count >= 2) ? 2 : 1);
        return;
    }

    if (Count >= 4)
        CAL_AppendNumber(Output, CalDateTime->Year, 4);
    else
        CAL_AppendNumber(Output, CalDateTime->Year % 100, (Count >= 2) ? 2 : 1);
}

static ULONG CAL_MonthNameIndex(const CALDATETIME *CalDateTime)
{
    if (CalDateTime->CalId == CAL_HEBREW && !CAL_HebrewIsLeapYear((LONG)CalDateTime->Year) && CalDateTime->Month >= 6)
        return CalDateTime->Month + 1;
    return CalDateTime->Month;
}

static int CAL_FormatDate(LPCWSTR Locale, DWORD Flags, BOOL CurrentCalendar, const CALDATETIME *CalDateTime,
                          LPCWSTR Format, LPWSTR Buffer, int Size)
{
    WCHAR Picture[CAL_MAX_STRING];
    CAL_OUTPUT Output = { Buffer, Size, 0, FALSE };
    DWORD Value;
    ULONG DayIndex;
    WCHAR Character;
    int Count;

    if (!GetCalendarInfoEx(Locale, CalDateTime->CalId, NULL, CAL_ICALINTVALUE | CAL_RETURN_NUMBER, NULL, 0, &Value))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!Format)
    {
        if ((CurrentCalendar &&
             !GetLocaleInfoEx(Locale, ((Flags & DATE_LONGDATE) ? LOCALE_SLONGDATE :
                                       (Flags & DATE_YEARMONTH) ? LOCALE_SYEARMONTH : LOCALE_SSHORTDATE) |
                                      (Flags & LOCALE_NOUSEROVERRIDE),
                              Picture, ARRAYSIZE(Picture))) ||
            (!CurrentCalendar &&
             !GetCalendarInfoEx(Locale, CalDateTime->CalId, NULL,
                                (Flags & DATE_LONGDATE) ? CAL_SLONGDATE : (Flags & DATE_YEARMONTH) ? CAL_SYEARMONTH : CAL_SSHORTDATE,
                                Picture, ARRAYSIZE(Picture), NULL)))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        Format = Picture;
    }

    /* FIXME: DATE_LTRREADING and DATE_RTLREADING are accepted but no reading-order marks are inserted */
    DayIndex = (CalDateTime->DayOfWeek + 6) % 7;
    while (*Format && !Output.Overflow)
    {
        Character = *Format;
        if (Character == L'\'')
        {
            for (Format++; *Format; Format++)
            {
                if (*Format == L'\'')
                {
                    if (Format[1] != L'\'')
                    {
                        Format++;
                        break;
                    }
                    Format++;
                }
                CAL_Append(&Output, Format, 1);
            }
            continue;
        }

        if (Character != L'd' && Character != L'M' && Character != L'y' && Character != L'g')
        {
            CAL_Append(&Output, Format++, 1);
            continue;
        }

        for (Count = 0; *Format == Character; Format++)
            Count++;

        switch (Character)
        {
        case L'd':
            if (Count >= 4)
                CAL_AppendCalendarString(&Output, Locale, CalDateTime->CalId, CAL_SDAYNAME1 + DayIndex);
            else if (Count == 3)
                CAL_AppendCalendarString(&Output, Locale, CalDateTime->CalId, CAL_SABBREVDAYNAME1 + DayIndex);
            else
                CAL_AppendNumber(&Output, CalDateTime->Day, (ULONG)Count);
            break;

        case L'M':
            if (Count >= 4)
                CAL_AppendCalendarString(&Output, Locale, CalDateTime->CalId, CAL_SMONTHNAME1 + CAL_MonthNameIndex(CalDateTime) - 1);
            else if (Count == 3)
                CAL_AppendCalendarString(&Output, Locale, CalDateTime->CalId, CAL_SABBREVMONTHNAME1 + CAL_MonthNameIndex(CalDateTime) - 1);
            else
                CAL_AppendNumber(&Output, CalDateTime->Month, (ULONG)Count);
            break;

        case L'y':
            CAL_AppendYear(&Output, CalDateTime, Count);
            break;

        default:
            CAL_AppendEra(&Output, Locale, CalDateTime);
            break;
        }
    }

    CAL_Append(&Output, L"", 1);
    if (Output.Overflow)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return Output.Length;
}

BOOL
WINAPI
IsCalendarLeapYear(
    _In_ CALID calId,
    _In_ UINT year,
    _In_ UINT era)
{
    CAL_CONTEXT Context;
    ULONG GregorianYear;
    LONG MinYear, MaxYear;

    if (CAL_HasOwnMonths(calId))
    {
        CAL_InitContext(&Context, calId);
        CAL_OwnYearRange(&Context, &MinYear, &MaxYear);
        if (era != 1 || (LONGLONG)year < MinYear || (LONGLONG)year > MaxYear)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return CAL_OwnIsLeapYear(&Context, (LONG)year);
    }

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
    LONG Days, OwnDays;

    if (!CAL_ToDays(lpCalDateTime, &Days))
        return FALSE;

    if (CAL_HasOwnMonths(lpCalDateTime->CalId))
    {
        if (!CAL_OwnAdjust(lpCalDateTime, Days, calUnit, amount, &OwnDays))
            return FALSE;

        Result = *lpCalDateTime;
        if (!CAL_FromDays(lpCalDateTime->CalId, OwnDays, &Result))
            return FALSE;

        *lpCalDateTime = Result;
        return TRUE;
    }

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
    CAL_CONTEXT Context;
    CALDATETIME MinDateTime, MaxDateTime;
    LONG MinDays, MaxDays;

    if (!lpCalMinDateTime || !lpCalMaxDateTime)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (CAL_HasOwnMonths(Calendar))
    {
        CAL_InitContext(&Context, Calendar);
        CAL_OwnDayRange(&Context, &MinDays, &MaxDays);
    }
    else if (CAL_GetEras(Calendar, Eras, ARRAYSIZE(Eras)))
    {
        MinDays = Eras[0].FirstDay;
        MaxDays = CAL_DaysFromDate(CAL_LAST_YEAR, 12, 31);
    }
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!CAL_FromDays(Calendar, MinDays, &MinDateTime) ||
        !CAL_FromDays(Calendar, MaxDays, &MaxDateTime))
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
    CALDATETIME CalDateTime;
    SYSTEMTIME SystemTime;
    DWORD CalendarType;
    LONG Days;

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

    if (!CAL_ToDays(lpCalDateTime, &Days))
        return FALSE;

    if (!GetLocaleInfoEx(lpszLocale, LOCALE_ICALENDARTYPE | LOCALE_RETURN_NUMBER | (dwFlags & LOCALE_NOUSEROVERRIDE),
                         (LPWSTR)&CalendarType, sizeof(CalendarType) / sizeof(WCHAR)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (CalendarType == lpCalDateTime->CalId && CAL_IsGregorianFamily(CalendarType))
    {
        if (!CAL_ToSystemTime(lpCalDateTime, &SystemTime))
            return FALSE;
        return GetDateFormatEx(lpszLocale, dwFlags, &SystemTime, lpFormat, lpDateStr, cchDate, NULL);
    }

    CalDateTime = *lpCalDateTime;
    if (!CAL_FromDays(lpCalDateTime->CalId, Days, &CalDateTime))
        return FALSE;

    return CAL_FormatDate(lpszLocale, dwFlags, CalendarType == lpCalDateTime->CalId, &CalDateTime,
                          lpFormat, lpDateStr, cchDate);
}
