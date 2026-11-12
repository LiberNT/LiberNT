/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that changing the time zone keeps the system time and moves only the local time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SECOND_100NS 10000000LL
#define MINUTE_100NS (60 * SECOND_100NS)
#define SLACK_100NS (2 * SECOND_100NS)

static LONGLONG
FileTimeValue(
    _In_ const FILETIME *Time)
{
    return ((LONGLONG)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
}

static LONGLONG
SystemTimeValue(
    _In_ const SYSTEMTIME *Time)
{
    FILETIME File;

    if (!SystemTimeToFileTime(Time, &File))
        return 0;
    return FileTimeValue(&File);
}

static BOOL
EnableTimeZonePrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Result = LookupPrivilegeValueW(NULL, L"SeTimeZonePrivilege", &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Result;
}

static VOID
CheckTimeAfterZone(
    _In_ LONG ExpectedBias,
    _In_ LONGLONG Before)
{
    SYSTEMTIME Universal, Local;
    FILETIME After;
    LONGLONG Delta, Offset;

    GetSystemTimeAsFileTime(&After);
    Delta = FileTimeValue(&After) - Before;
    ok(Delta >= 0 && Delta < SLACK_100NS, "System time moved by %I64d ms across the time zone change\n",
       Delta / 10000);

    GetSystemTime(&Universal);
    GetLocalTime(&Local);
    Offset = SystemTimeValue(&Universal) - SystemTimeValue(&Local);
    ok(Offset > (LONGLONG)ExpectedBias * MINUTE_100NS - SLACK_100NS &&
       Offset < (LONGLONG)ExpectedBias * MINUTE_100NS + SLACK_100NS,
       "UTC minus local time is %I64d s, expected %ld min\n", Offset / SECOND_100NS, ExpectedBias);
}

START_TEST(TimeZoneKeepsTime)
{
    DYNAMIC_TIME_ZONE_INFORMATION Original;
    TIME_ZONE_INFORMATION Zone;
    FILETIME Before;
    LONG Bias;
    DWORD Id;

    if (!EnableTimeZonePrivilege())
    {
        skip("SeTimeZonePrivilege is not available: %lu\n", GetLastError());
        return;
    }

    Id = GetDynamicTimeZoneInformation(&Original);
    ok(Id != TIME_ZONE_ID_INVALID, "GetDynamicTimeZoneInformation failed: %lu\n", GetLastError());
    if (Id == TIME_ZONE_ID_INVALID)
        return;

    Bias = (Original.Bias > 0) ? Original.Bias - 300 : Original.Bias + 300;
    ZeroMemory(&Zone, sizeof(Zone));
    Zone.Bias = Bias;
    lstrcpyW(Zone.StandardName, L"LiberNT Test Standard Time");
    lstrcpyW(Zone.DaylightName, L"LiberNT Test Daylight Time");

    GetSystemTimeAsFileTime(&Before);
    ok(SetTimeZoneInformation(&Zone), "SetTimeZoneInformation failed: %lu\n", GetLastError());
    CheckTimeAfterZone(Bias, FileTimeValue(&Before));

    GetSystemTimeAsFileTime(&Before);
    ok(SetDynamicTimeZoneInformation(&Original), "SetDynamicTimeZoneInformation failed: %lu\n", GetLastError());
    Id = GetTimeZoneInformation(&Zone);
    ok(Id != TIME_ZONE_ID_INVALID, "GetTimeZoneInformation failed: %lu\n", GetLastError());
    CheckTimeAfterZone(Zone.Bias + ((Id == TIME_ZONE_ID_DAYLIGHT) ? Zone.DaylightBias : Zone.StandardBias),
                       FileTimeValue(&Before));
}
