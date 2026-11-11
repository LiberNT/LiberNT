/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the date and time format enumerations of a locale
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *FN_EnumDateFormatsExEx)(DATEFMT_ENUMPROCEXEX, LPCWSTR, DWORD, LPARAM);
typedef BOOL (WINAPI *FN_EnumTimeFormatsEx)(TIMEFMT_ENUMPROCEX, LPCWSTR, DWORD, LPARAM);

#define ENUM_LCID_EN_US MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT)
#define ENUM_LCID_JA_JP MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), SORT_DEFAULT)

static struct
{
    unsigned int count;
    unsigned int stop_after;
    unsigned int japanese;
    unsigned int other_calendar;
    LPARAM lparam;
    BOOL lparam_ok;
    WCHAR first[128];
    char firstA[128];
} enum_state;

static void reset_state(unsigned int stop_after, LPARAM lparam)
{
    memset(&enum_state, 0, sizeof(enum_state));
    enum_state.stop_after = stop_after;
    enum_state.lparam = lparam;
    enum_state.lparam_ok = TRUE;
}

static BOOL record_w(const WCHAR *format)
{
    if (!enum_state.count++)
        StringCchCopyW(enum_state.first, ARRAYSIZE(enum_state.first), format);
    return !enum_state.stop_after || enum_state.count < enum_state.stop_after;
}

static void record_calendar(CALID calendar)
{
    if (calendar == CAL_JAPAN)
        enum_state.japanese++;
    else if (calendar != CAL_GREGORIAN)
        enum_state.other_calendar++;
}

static BOOL CALLBACK date_proc_w(LPWSTR format)
{
    return record_w(format);
}

static BOOL CALLBACK date_proc_a(LPSTR format)
{
    if (!enum_state.count++)
        StringCchCopyA(enum_state.firstA, ARRAYSIZE(enum_state.firstA), format);
    return !enum_state.stop_after || enum_state.count < enum_state.stop_after;
}

static BOOL CALLBACK date_proc_ex_a(LPSTR format, CALID calendar)
{
    record_calendar(calendar);
    return date_proc_a(format);
}

static BOOL CALLBACK date_proc_ex_w(LPWSTR format, CALID calendar)
{
    record_calendar(calendar);
    return record_w(format);
}

static BOOL CALLBACK date_proc_exex(LPWSTR format, CALID calendar, LPARAM lparam)
{
    if (lparam != enum_state.lparam)
        enum_state.lparam_ok = FALSE;
    record_calendar(calendar);
    return record_w(format);
}

static BOOL CALLBACK time_proc_ex(LPWSTR format, LPARAM lparam)
{
    if (lparam != enum_state.lparam)
        enum_state.lparam_ok = FALSE;
    return record_w(format);
}

static void expect_first_w(LCID lcid, LCTYPE type, const char *what)
{
    WCHAR expected[128];
    int ret;

    ret = GetLocaleInfoW(lcid, type, expected, ARRAYSIZE(expected));
    ok(ret > 0, "%s: GetLocaleInfoW(%04lx) failed, error %lu\n", what, type, GetLastError());
    ok(!lstrcmpW(enum_state.first, expected), "%s: first format %S, expected %S\n", what, enum_state.first, expected);
}

static void Test_DateFormatsW(void)
{
    BOOL ret;

    reset_state(0, 0);
    ret = EnumDateFormatsW(date_proc_w, ENUM_LCID_EN_US, DATE_SHORTDATE);
    ok(ret, "EnumDateFormatsW(DATE_SHORTDATE) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsW(DATE_SHORTDATE) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SSHORTDATE, "EnumDateFormatsW(DATE_SHORTDATE)");

    reset_state(0, 0);
    ret = EnumDateFormatsW(date_proc_w, ENUM_LCID_EN_US, 0);
    ok(ret, "EnumDateFormatsW(0) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsW(0) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SSHORTDATE, "EnumDateFormatsW(0)");

    reset_state(0, 0);
    ret = EnumDateFormatsW(date_proc_w, ENUM_LCID_EN_US, DATE_LONGDATE);
    ok(ret, "EnumDateFormatsW(DATE_LONGDATE) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsW(DATE_LONGDATE) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SLONGDATE, "EnumDateFormatsW(DATE_LONGDATE)");

    reset_state(0, 0);
    ret = EnumDateFormatsW(date_proc_w, ENUM_LCID_EN_US, DATE_YEARMONTH);
    ok(ret, "EnumDateFormatsW(DATE_YEARMONTH) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 1, "EnumDateFormatsW(DATE_YEARMONTH) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SYEARMONTH, "EnumDateFormatsW(DATE_YEARMONTH)");

    reset_state(1, 0);
    ret = EnumDateFormatsW(date_proc_w, ENUM_LCID_EN_US, DATE_SHORTDATE);
    ok(ret, "EnumDateFormatsW with a stopping callback failed, error %lu\n", GetLastError());
    ok(enum_state.count == 1, "EnumDateFormatsW called the stopping callback %u times\n", enum_state.count);

    SetLastError(0xdeadbeef);
    ret = EnumDateFormatsW(NULL, ENUM_LCID_EN_US, DATE_SHORTDATE);
    ok(!ret, "EnumDateFormatsW(NULL) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "EnumDateFormatsW(NULL) error %lu\n", GetLastError());

}

static void Test_DateFormatsA(void)
{
    char expected[128];
    BOOL ret;

    reset_state(0, 0);
    ret = EnumDateFormatsA(date_proc_a, ENUM_LCID_EN_US, DATE_LONGDATE);
    ok(ret, "EnumDateFormatsA(DATE_LONGDATE) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsA(DATE_LONGDATE) gave %u formats\n", enum_state.count);
    ok(GetLocaleInfoA(ENUM_LCID_EN_US, LOCALE_SLONGDATE, expected, sizeof(expected)) > 0,
       "GetLocaleInfoA(LOCALE_SLONGDATE) failed, error %lu\n", GetLastError());
    ok(!lstrcmpA(enum_state.firstA, expected), "EnumDateFormatsA first format %s, expected %s\n",
       enum_state.firstA, expected);

    reset_state(0, 0);
    ret = EnumDateFormatsExA(date_proc_ex_a, ENUM_LCID_EN_US, DATE_SHORTDATE);
    ok(ret, "EnumDateFormatsExA(DATE_SHORTDATE) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsExA(DATE_SHORTDATE) gave %u formats\n", enum_state.count);
    ok(!enum_state.japanese && !enum_state.other_calendar, "EnumDateFormatsExA en-US reported %u/%u non-Gregorian formats\n",
       enum_state.japanese, enum_state.other_calendar);
}

static void Test_DateFormatsEx(void)
{
    FN_EnumDateFormatsExEx pEnumDateFormatsExEx;
    BOOL ret;

    reset_state(0, 0);
    ret = EnumDateFormatsExW(date_proc_ex_w, ENUM_LCID_EN_US, DATE_SHORTDATE);
    ok(ret, "EnumDateFormatsExW(DATE_SHORTDATE) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsExW(DATE_SHORTDATE) gave %u formats\n", enum_state.count);
    ok(!enum_state.japanese && !enum_state.other_calendar, "EnumDateFormatsExW en-US reported %u/%u non-Gregorian formats\n",
       enum_state.japanese, enum_state.other_calendar);

    reset_state(0, 0);
    ret = EnumDateFormatsExW(date_proc_ex_w, ENUM_LCID_JA_JP, DATE_SHORTDATE);
    ok(ret, "EnumDateFormatsExW(ja-JP) failed, error %lu\n", GetLastError());
    ok(enum_state.japanese >= 1, "EnumDateFormatsExW(ja-JP) reported %u Japanese era formats\n", enum_state.japanese);

    pEnumDateFormatsExEx = (FN_EnumDateFormatsExEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "EnumDateFormatsExEx");
    if (!pEnumDateFormatsExEx)
    {
        skip("EnumDateFormatsExEx not found\n");
        return;
    }

    reset_state(0, 0x1234);
    ret = pEnumDateFormatsExEx(date_proc_exex, L"en-US", DATE_LONGDATE, 0x1234);
    ok(ret, "EnumDateFormatsExEx(en-US) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumDateFormatsExEx(en-US) gave %u formats\n", enum_state.count);
    ok(enum_state.lparam_ok, "EnumDateFormatsExEx did not pass the lParam through\n");
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SLONGDATE, "EnumDateFormatsExEx(en-US)");

    reset_state(0, 0);
    SetLastError(0xdeadbeef);
    ret = pEnumDateFormatsExEx(date_proc_exex, L"deadbeef", DATE_SHORTDATE, 0);
    ok(!ret, "EnumDateFormatsExEx(deadbeef) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "EnumDateFormatsExEx(deadbeef) error %lu\n", GetLastError());
    ok(enum_state.count == 0, "EnumDateFormatsExEx(deadbeef) called the callback %u times\n", enum_state.count);
}

static void Test_TimeFormats(void)
{
    FN_EnumTimeFormatsEx pEnumTimeFormatsEx;
    BOOL ret;

    reset_state(0, 0);
    ret = EnumTimeFormatsW(date_proc_w, ENUM_LCID_EN_US, 0);
    ok(ret, "EnumTimeFormatsW(0) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumTimeFormatsW(0) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_STIMEFORMAT, "EnumTimeFormatsW(0)");

    reset_state(0, 0);
    ret = EnumTimeFormatsW(date_proc_w, ENUM_LCID_EN_US, TIME_NOSECONDS);
    ok(ret, "EnumTimeFormatsW(TIME_NOSECONDS) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumTimeFormatsW(TIME_NOSECONDS) gave %u formats\n", enum_state.count);
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SSHORTTIME, "EnumTimeFormatsW(TIME_NOSECONDS)");

    reset_state(1, 0);
    ret = EnumTimeFormatsW(date_proc_w, ENUM_LCID_EN_US, 0);
    ok(ret, "EnumTimeFormatsW with a stopping callback failed, error %lu\n", GetLastError());
    ok(enum_state.count == 1, "EnumTimeFormatsW called the stopping callback %u times\n", enum_state.count);

    reset_state(0, 0);
    ret = EnumTimeFormatsA((TIMEFMT_ENUMPROCA)date_proc_a, ENUM_LCID_EN_US, 0);
    ok(ret, "EnumTimeFormatsA(0) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumTimeFormatsA(0) gave %u formats\n", enum_state.count);

    pEnumTimeFormatsEx = (FN_EnumTimeFormatsEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "EnumTimeFormatsEx");
    if (!pEnumTimeFormatsEx)
    {
        skip("EnumTimeFormatsEx not found\n");
        return;
    }

    reset_state(0, 0x5678);
    ret = pEnumTimeFormatsEx(time_proc_ex, L"en-US", TIME_NOSECONDS, 0x5678);
    ok(ret, "EnumTimeFormatsEx(en-US) failed, error %lu\n", GetLastError());
    ok(enum_state.count >= 2, "EnumTimeFormatsEx(en-US) gave %u formats\n", enum_state.count);
    ok(enum_state.lparam_ok, "EnumTimeFormatsEx did not pass the lParam through\n");
    expect_first_w(ENUM_LCID_EN_US, LOCALE_SSHORTTIME, "EnumTimeFormatsEx(en-US)");

    reset_state(0, 0);
    SetLastError(0xdeadbeef);
    ret = pEnumTimeFormatsEx(time_proc_ex, L"deadbeef", 0, 0);
    ok(!ret, "EnumTimeFormatsEx(deadbeef) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "EnumTimeFormatsEx(deadbeef) error %lu\n", GetLastError());
    ok(enum_state.count == 0, "EnumTimeFormatsEx(deadbeef) called the callback %u times\n", enum_state.count);
}

START_TEST(EnumDateTimeFormats)
{
    Test_DateFormatsW();
    Test_DateFormatsA();
    Test_DateFormatsEx();
    Test_TimeFormats();
}
